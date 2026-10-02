#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#define	Image	IMAGE
#include	<draw.h>
#include	<memdraw.h>
#include	<cursor.h>
#include	"screen.h"

/*
 * wasm32's screen (docs/architecture.md, phase C): XRGB32 in the
 * kernel's memory; flushmemscreen tells the page which part changed and
 * the page draws it from the shared memory on its canvas (platform.js).
 * The cursor is the page's own (platcursor: the bitmaps), the mouse the
 * page's pointer: its events come through a ring (mouseproc).
 */
Memimage	*gscreen;

typedef struct Mring Mring;
struct Mring
{
	long	w;		/* the page's: events written */
	long	r;		/* ours: read */
	long	ev[64][4];	/* x, y, buttons, msec */
};
static Mring	mring;

static void
mouseproc(void*)
{
	long *e;

	for(;;){
		while(mring.r == mring.w)
			platwait(&mring.w, mring.r, -1);
		e = mring.ev[mring.r % nelem(mring.ev)];
		absmousetrack(e[0], e[1], e[2], e[3]);
		mring.r++;
	}
}

/* the page's size: the screen's */
void
screeninit(void)
{
	int w, h;

	w = 1024;
	h = 768;
	platscreen(&w, &h);
	memimageinit();
	gscreen = allocmemimage(Rect(0, 0, w, h), XRGB32);
	if(gscreen == nil)
		panic("screeninit: no %dx%d screen", w, h);
	memfillcolor(gscreen, 0x777777FF);
	platfb(byteaddr(gscreen, ZP), gscreen->width*sizeof(ulong), w, h);
	flushmemscreen(gscreen->r);
}

void
mouseinput(void)
{
	platmousering(&mring);
	kproc("mousein", mouseproc, nil);
}

Memdata*
attachscreen(Rectangle *r, ulong *chan, int *d, int *width, int *softscreen)
{
	if(gscreen == nil)
		return nil;
	*r = gscreen->clipr;
	*chan = gscreen->chan;
	*d = gscreen->depth;
	*width = gscreen->width;
	*softscreen = 1;
	gscreen->data->ref++;
	return gscreen->data;
}

void
flushmemscreen(Rectangle r)
{
	if(gscreen == nil || !rectclip(&r, gscreen->r))
		return;
	platflush(r.min.x, r.min.y, r.max.x, r.max.y);
}

/* no colour map: XRGB32 */
void
getcolor(ulong, ulong *r, ulong *g, ulong *b)
{
	*r = *g = *b = 0;
}

int
setcolor(ulong, ulong, ulong, ulong)
{
	return 0;
}

void
blankscreen(int)
{
}

/* the page's cursor: shown and moved there */
void
cursoron(void)
{
}

void
cursoroff(void)
{
}

void
setcursor(Cursor *c)
{
	platcursor(c->offset.x, c->offset.y, c->clr, c->set);
}

void
mousectl(Cmdbuf*)
{
	error(Ebadctl);
}
