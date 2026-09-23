#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#define	Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

static Memimage *conscol;
static Memimage *back;

enum {
	Ncol	= 3,		/* the boot log is kept in columns, not scrolled
				 * away, so more of it stays on screen at once -
				 * see vgascroll() */
};

static Point curpos;
static Rectangle window;	/* the current column's rectangle */
static Rectangle fullwin;	/* the whole console area, across all columns */
static int curcol;
static int *xp;
static int xbuf[256];
Lock vgascreenlock;

void
vgaimageinit(ulong)
{
	conscol = memblack;
	back = memwhite;
}

/* the rectangle for column c (0..Ncol-1) of fullwin */
static Rectangle
colrect(int c)
{
	int w;
	Rectangle r;

	w = Dx(fullwin)/Ncol;
	r.min.x = fullwin.min.x + c*w;
	r.max.x = c == Ncol-1? fullwin.max.x: r.min.x+w;
	r.min.y = fullwin.min.y;
	r.max.y = fullwin.max.y;
	return r;
}

/*
 * The console's bottom is reached: move on to the next column instead of
 * scrolling this one away, so earlier output stays visible. Only once all
 * Ncol columns are full does this wrap back to column 0, clearing the whole
 * console area - real scrolling, now Ncol times rarer.
 */
static void
vgascroll(VGAscr* scr)
{
	if(++curcol >= Ncol){
		curcol = 0;
		memimagedraw(scr->gscreen, fullwin, back, ZP, nil, ZP, S);
	}
	window = colrect(curcol);
	curpos = window.min;
}

static void
vgascreenputc(VGAscr* scr, char* buf, Rectangle *flushr)
{
	Point p;
	int h, w, pos;
	Rectangle r;

	if(xp < xbuf || xp >= &xbuf[nelem(xbuf)])
		xp = xbuf;

	h = scr->memdefont->height;
	switch(buf[0]){
	case '\n':
		if(curpos.y+h >= window.max.y){
			vgascroll(scr);
			*flushr = fullwin;
		} else
			curpos.y += h;
		vgascreenputc(scr, "\r", flushr);
		break;

	case '\r':
		xp = xbuf;
		curpos.x = window.min.x;
		break;

	case '\t':
		p = memsubfontwidth(scr->memdefont, " ");
		w = p.x;
		if(curpos.x >= window.max.x-4*w)
			vgascreenputc(scr, "\n", flushr);

		pos = (curpos.x-window.min.x)/w;
		pos = 4-(pos%4);
		*xp++ = curpos.x;
		r = Rect(curpos.x, curpos.y, curpos.x+pos*w, curpos.y + h);
		memimagedraw(scr->gscreen, r, back, ZP, nil, ZP, S);
		curpos.x += pos*w;
		break;

	case '\b':
		if(xp <= xbuf)
			break;
		xp--;
		r = Rect(*xp, curpos.y, curpos.x, curpos.y+h);
		memimagedraw(scr->gscreen, r, back, r.min, nil, ZP, S);
		combinerect(flushr, r);
		curpos.x = *xp;
		break;

	case '\0':
		break;

	default:
		p = memsubfontwidth(scr->memdefont, buf);
		w = p.x;

		if(curpos.x >= window.max.x-w)
			vgascreenputc(scr, "\n", flushr);

		*xp++ = curpos.x;
		r = Rect(curpos.x, curpos.y, curpos.x+w, curpos.y+h);
		memimagedraw(scr->gscreen, r, back, r.min, nil, ZP, S);
		memimagestring(scr->gscreen, curpos, conscol, ZP, scr->memdefont, buf);
		combinerect(flushr, r);
		curpos.x += w;
	}
}

static void
vgascreenputs(char* s, int n)
{
	static char rb[UTFmax+1];
	static int nrb;
	char *e;
	int gotdraw;
	VGAscr *scr;
	Rectangle flushr;

	scr = &vgascreen[0];

	if(!islo()){
		/*
		 * Don't deadlock trying to
		 * print in an interrupt.
		 */
		if(!canlock(&vgascreenlock))
			return;
	}
	else {
		while(!canlock(&vgascreenlock))
			;
	}

	/*
	 * Be nice to hold this, but not going to deadlock
	 * waiting for it.  Just try and see.
	 */
	gotdraw = canqlock(&drawlock);

	flushr = Rect(10000, 10000, -10000, -10000);

	e = s + n;
	while(s < e){
		rb[nrb++] = *s++;
		if(nrb >= UTFmax || fullrune(rb, nrb)){
			rb[nrb] = 0;
			vgascreenputc(scr, rb, &flushr);
			nrb = 0;
		}
	}
	flushmemscreen(flushr);

	if(gotdraw)
		qunlock(&drawlock);
	unlock(&vgascreenlock);
}

/*
 * Plain console, full screen: no border, no title bar. The top
 * bootmarkheight() rows are left untouched - by now they hold the boot
 * marker row (loader's squares and bootfb.c's, see sys/src/9/pc/bootfb.c),
 * which this must not erase, only avoid drawing text over.
 */
void
vgascreenwin(VGAscr* scr)
{
	Rectangle r;
	int mh;

	mh = bootmarkheight();

	r = scr->gscreen->r;
	r.min.y += mh;
	memimagedraw(scr->gscreen, r, back, ZP, memopaque, ZP, S);

	fullwin = scr->gscreen->clipr;
	fullwin.min.y += mh;
	curcol = 0;
	window = colrect(curcol);
	curpos = window.min;

	flushmemscreen(scr->gscreen->r);
	{
		char *log;
		int logn;

		/* the loader's own text first, then the kernel's - see bootlogtext() */
		if((log = bootlogtext(&logn)) != nil)
			vgascreenputs(log, logn);
	}
	vgascreenputs(kmesg.buf, kmesg.n);
	screenputs = vgascreenputs;
}

/*
 * Supposedly this is the way to turn DPMS
 * monitors off using just the VGA registers.
 * Unfortunately, it seems to mess up the video mode
 * on the cards I've tried.
 */
void
vgablank(VGAscr*, int blank)
{
	uchar seq1, crtc17;

	if(blank) {
		seq1 = 0x00;
		crtc17 = 0x80;
	} else {
		seq1 = 0x20;
		crtc17 = 0x00;
	}

	outs(Seqx, 0x0100);			/* synchronous reset */
	seq1 |= vgaxi(Seqx, 1) & ~0x20;
	vgaxo(Seqx, 1, seq1);
	crtc17 |= vgaxi(Crtx, 0x17) & ~0x80;
	delay(10);
	vgaxo(Crtx, 0x17, crtc17);
	outs(Crtx, 0x0300);				/* end synchronous reset */
}

void
addvgaseg(char *name, uvlong pa, vlong size)
{
	Physseg seg;

	if((uintptr)pa != pa || size <= 0 || -(uintptr)pa < size){
		print("addvgaseg %s: bad address %llux-%llux pc %#p\n",
			name, pa, pa+size, getcallerpc(&name));
		return;
	}
	memset(&seg, 0, sizeof seg);
	seg.attr = SG_PHYSICAL | SG_DEVICE | SG_NOEXEC;
	seg.name = name;
	seg.pa = (uintptr)pa;
	seg.size = size;
	addphysseg(&seg);
}
