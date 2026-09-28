/*
 * gui-web: drawterm's screen, mouse and keyboard in the browser.
 * The screen is an XBGR32 Memimage whose changed rectangles
 * flushmemscreen gives the page (js_flush); the page's mouse, key and
 * resize events come through events.c to a kproc here.  monolith.h is
 * the whole boundary with the page; wsock.c the connections.
 */
#include "u.h"
#include "lib.h"
#include "dat.h"
#include "fns.h"
#include "error.h"

#include <draw.h>
#include <memdraw.h>
#include <keyboard.h>
#include <cursor.h>
#include <libsec.h>
#include "screen.h"

#include "monolith.h"

int	wsrcpuplain(void);

Memimage *gscreen;
static char *snarfbuf;

void
guimain(void)
{
	cpubody();
}

static void
inputproc(void *v)
{
	int t, a, b, c;

	USED(v);
	for(;;){
		mo_nextinput(&t, &a, &b, &c);
		switch(t){
		case Evmouse:
			absmousetrack(a, b, c, ticks());
			break;
		case Evkey:
			kbdkey(a, b);
			break;
		case Evresize:
			screenresize(Rect(0, 0, a, b));
			break;
		}
	}
}

void
screeninit(void)
{
	Rectangle r;
	int w, h;

	memimageinit();
	js_screensize(&w, &h);
	r = Rect(0, 0, w, h);
	screensize(r, XBGR32);
	if(gscreen == nil)
		panic("screensize failed");
	gscreen->clipr = r;
	kproc("webinput", inputproc, nil);

	qlock(&drawlock);
	terminit();
	flushmemscreen(gscreen->clipr);
	qunlock(&drawlock);
}

void
screensize(Rectangle r, ulong chan)
{
	Memimage *m;

	m = allocmemimage(r, chan);
	if(m == nil)
		return;
	if(gscreen != nil)
		freememimage(gscreen);
	gscreen = m;
	gscreen->clipr = ZR;
	js_resize(Dx(r), Dy(r));
}

Memdata*
attachscreen(Rectangle *r, ulong *chan, int *depth, int *width, int *softscreen)
{
	*r = gscreen->clipr;
	*chan = gscreen->chan;
	*depth = gscreen->depth;
	*width = gscreen->width;
	*softscreen = 1;

	gscreen->data->ref++;
	return gscreen->data;
}

void
flushmemscreen(Rectangle r)
{
	assert(!canqlock(&drawlock));
	if(rectclip(&r, gscreen->clipr) == 0)
		return;
	js_flush(byteaddr(gscreen, gscreen->r.min), gscreen->width*sizeof(ulong),
		r.min.x, r.min.y, r.max.x, r.max.y);
}

void
getcolor(ulong i, ulong *r, ulong *g, ulong *b)
{
	ulong v;

	v = cmap2rgb(i);
	*r = (v>>16)&0xFF;
	*g = (v>>8)&0xFF;
	*b = v&0xFF;
}

void
setcolor(ulong i, ulong r, ulong g, ulong b)
{
}

char*
clipread(void)
{
	if(snarfbuf)
		return strdup(snarfbuf);
	return nil;
}

int
clipwrite(char *buf)
{
	free(snarfbuf);
	snarfbuf = strdup(buf);
	return 0;
}

void
mouseset(Point p)
{
	/* a page cannot move the pointer */
}

void
setcursor(void)
{
	uchar img[16*16*4], *p;
	int x, y, bit, i;

	qlock(&drawlock);
	for(y = 0; y < 16; y++)
		for(x = 0; x < 16; x++){
			i = y*2 + x/8;
			bit = 0x80 >> (x%8);
			p = img + (y*16 + x)*4;
			if(cursor.set[i] & bit){
				p[0] = p[1] = p[2] = 0x00;
				p[3] = 0xFF;
			}else if(cursor.clr[i] & bit){
				p[0] = p[1] = p[2] = 0xFF;
				p[3] = 0xFF;
			}else
				p[0] = p[1] = p[2] = p[3] = 0;
		}
	js_cursor(img, -cursor.offset.x, -cursor.offset.y);
	qunlock(&drawlock);
}

void
titlewrite(char *buf)
{
}

/*
 * cpu.c's tlsClient (-DtlsClient=monolithtlsclient for cpu.c only): over
 * wss, rcpu's connection is webterm's /rcpu, authenticated and not
 * encrypted again, so its TLS-PSK (pskID p9secret, cpu.c p9authtls) is
 * left out.  Everything else is libsec's.
 */
int
monolithtlsclient(int fd, TLSconn *c)
{
	if(wsrcpuplain() && c->pskID != nil && strcmp(c->pskID, "p9secret") == 0)
		return fd;
	return tlsClient(fd, c);
}
