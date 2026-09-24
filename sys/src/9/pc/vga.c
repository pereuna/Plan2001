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
	Maxrows	= 256,		/* text rows tracked for line wrapping */
	Maxesc	= 512,
};

/*
 * Two console modes.  Boot log: Ncol columns, never scrolled.  Interactive:
 * one full-width column that really scrolls, entered by the first ESC[2J
 * (aux/kbdfs sends one when it starts, ie when the boot-time prompts begin),
 * with a help bar in the top strip.  Interactive mode understands a small
 * ANSI subset - ESC[nC, ESC[nD, ESC[K, ESC[H, ESC[2J (ESC[..m is ignored) -
 * which is what kbdfs's line editor uses; serial consoles understand the
 * same bytes natively.
 */
static Point curpos;
static Rectangle window;	/* the current column's rectangle */
static Rectangle fullwin;	/* the whole console area, across all columns */
static int curcol;
static int ncol = Ncol;
static int interactive;
static int cellw;		/* width of one character cell, for the column gutters */
static int fonth;		/* font height */
static int pendwrap;		/* just wrapped: cursor is at the start of an empty continuation row */
static uchar softrow[Maxrows];	/* row r continues row r-1 (wrapped, not a new line) */
static int rowend[Maxrows];	/* for such a row: x where the previous row's text ended */
static int escs, escn, eschave;	/* escape sequence parser state */
static int *xp;
enum { Curgap = 2 };	/* cursor bar reaches this many pixels into the previous cell */
static Memimage *cursave;	/* the screen cell under the text cursor while it is drawn */
static int curvis;
static Point curat;
static int xbuf[256];
Lock vgascreenlock;

static char helptext[] =
	"Home/End move   Shift+Home/End cut to start/end   Shift+Left/Right cut   ^W cut word   ^V paste   Up/Down history";

void
vgaimageinit(ulong)
{
	conscol = memblack;
	back = memwhite;
}

/* the rectangle for column c (0..ncol-1) of fullwin */
static Rectangle
colrect(int c)
{
	int w;
	Rectangle r;

	w = Dx(fullwin)/ncol;
	r.min.x = fullwin.min.x + c*w + cellw;	/* blank cell at the left edge of every column */
	r.max.x = c == ncol-1? fullwin.max.x: fullwin.min.x + (c+1)*w;
	r.min.y = fullwin.min.y;
	r.max.y = fullwin.max.y;
	return r;
}

static int
currow(void)
{
	int r;

	r = (curpos.y - fullwin.min.y)/fonth;
	if(r < 0 || r >= Maxrows)
		return -1;
	return r;
}

static void
markrow(int soft, int endx)
{
	int r;

	if((r = currow()) < 0)
		return;
	softrow[r] = soft;
	rowend[r] = endx;
}

/*
 * The console's bottom is reached.  Boot log: move on to the next column
 * instead of scrolling this one away, so earlier output stays visible;
 * only once all ncol columns are full does this wrap back to column 0,
 * clearing the whole console area.  Interactive: really scroll, 8 rows at
 * a time, and leave the cursor at the start of the row after the last one.
 */
static void
vgascroll(VGAscr* scr, Rectangle *flushr)
{
	int o, n, i;
	Rectangle r;
	Point p;

	if(interactive){
		o = 8*fonth;
		if(o >= Dy(window))
			o = fonth;
		r = Rpt(window.min, Pt(window.max.x, window.max.y-o));
		p = Pt(window.min.x, window.min.y+o);
		memimagedraw(scr->gscreen, r, scr->gscreen, p, nil, p, S);
		r = Rpt(Pt(window.min.x, window.max.y-o), window.max);
		memimagedraw(scr->gscreen, r, back, ZP, nil, ZP, S);
		curpos.y = window.max.y - o;
		n = o/fonth;
		for(i = 0; i < Maxrows; i++){
			softrow[i] = i+n < Maxrows? softrow[i+n]: 0;
			rowend[i] = i+n < Maxrows? rowend[i+n]: 0;
		}
		combinerect(flushr, fullwin);
		return;
	}
	if(++curcol >= ncol){
		curcol = 0;
		memimagedraw(scr->gscreen, fullwin, back, ZP, nil, ZP, S);
		combinerect(flushr, fullwin);	/* the whole area was actually
						 * redrawn; a plain column
						 * switch below redraws nothing
						 * itself, so only the new
						 * column's rect (added by the
						 * caller) needs to flush */
	}
	window = colrect(curcol);
	curpos = window.min;
}

/* move to the start of the next row, scrolling / changing column if at the bottom */
static void
nextrow(VGAscr *scr, Rectangle *flushr)
{
	if(curpos.y+fonth >= window.max.y){
		vgascroll(scr, flushr);
		if(!interactive)
			combinerect(flushr, window);
	} else
		curpos.y += fonth;
	curpos.x = window.min.x;
	markrow(0, 0);
}

/*
 * The line ran out of horizontal room: continue on the next row.  The row
 * is remembered as a continuation (softrow/rowend) so ESC[D / ESC[C can walk
 * across the wrap, and backspace-style editing works on wrapped lines.
 */
static void
wrapline(VGAscr *scr, Rectangle *flushr)
{
	int endx;

	endx = curpos.x;
	nextrow(scr, flushr);
	markrow(1, endx);
	xp = xbuf;
	pendwrap = 1;
}

static void
curleft(void)
{
	int row;

	pendwrap = 0;
	if(curpos.x - cellw >= window.min.x){
		curpos.x -= cellw;
		return;
	}
	row = currow();
	if(row > 0 && softrow[row]){
		curpos.y -= fonth;
		curpos.x = rowend[row] - cellw;
	}
}

static void
curright(void)
{
	int row;

	pendwrap = 0;
	row = currow();
	if(row >= 0 && row+1 < Maxrows && softrow[row+1] && curpos.x + cellw >= rowend[row+1]){
		curpos.y += fonth;
		curpos.x = window.min.x;
	} else if(curpos.x + 2*cellw <= window.max.x)
		curpos.x += cellw;
}

/* erase from the cursor to the end of the logical line: this row and its wrapped continuations */
static void
eraseeol(VGAscr *scr, Rectangle *flushr)
{
	int row;
	Rectangle r;

	r = Rect(curpos.x, curpos.y, window.max.x, curpos.y+fonth);
	memimagedraw(scr->gscreen, r, back, ZP, nil, ZP, S);
	combinerect(flushr, r);
	row = currow();
	if(row < 0)
		return;
	while(++row < Maxrows && softrow[row]){
		r = Rect(window.min.x, fullwin.min.y + row*fonth, window.max.x, fullwin.min.y + (row+1)*fonth);
		if(r.max.y > window.max.y)
			break;
		memimagedraw(scr->gscreen, r, back, ZP, nil, ZP, S);
		combinerect(flushr, r);
		softrow[row] = 0;
	}
}

/*
 * ESC[2J: clear the console and switch to interactive mode - one
 * full-width scrolling column below a help bar in the top strip (markers
 * stay at its left end).
 */
static void
interactivemode(VGAscr *scr, Rectangle *flushr)
{
	int mh, barh, textx, i;
	Rectangle r;

	mh = bootmarkheight();
	barh = mh > fonth? mh: fonth;
	textx = 100;			/* right of the marker squares */

	interactive = 1;
	ncol = 1;

	r = scr->gscreen->r;
	r.min.y = mh;
	r.max.y = barh;
	memimagedraw(scr->gscreen, r, memblack, ZP, memopaque, ZP, S);
	r = Rect(textx, scr->gscreen->r.min.y, scr->gscreen->r.max.x, barh);
	memimagedraw(scr->gscreen, r, memblack, ZP, memopaque, ZP, S);
	memimagestring(scr->gscreen, Pt(textx, (barh-fonth)/2), memwhite, ZP, scr->memdefont, helptext);

	r = scr->gscreen->r;
	r.min.y = barh;
	memimagedraw(scr->gscreen, r, back, ZP, memopaque, ZP, S);

	fullwin = scr->gscreen->clipr;
	fullwin.min.y = barh;
	i = Dy(fullwin)/fonth - 1;	/* whole rows, last one left blank: see vgascreenwin() */
	fullwin.max.y = fullwin.min.y + i*fonth;
	fullwin.max.x -= cellw;

	curcol = 0;
	window = colrect(0);
	curpos = window.min;
	xp = xbuf;
	pendwrap = 0;
	memset(softrow, 0, sizeof softrow);
	combinerect(flushr, scr->gscreen->r);
}

/*
 * Feed one character to the escape sequence parser (state in escs).
 * Returns 0 if the character is not part of a sequence and must be
 * handled as ordinary text.
 */
static int
escfeed(VGAscr *scr, int c, Rectangle *flushr)
{
	int n, i;

	if(escs == 1){
		if(c == '['){
			escs = 2;
			escn = 0;
			eschave = 0;
			return 1;
		}
		escs = 0;
		return 0;
	}
	if(c >= '0' && c <= '9'){
		if(escn < 10000)
			escn = escn*10 + c - '0';
		eschave = 1;
		return 1;
	}
	if(c == ';' || c == '?')
		return 1;
	escs = 0;
	if(c < 0x40 || c > 0x7e)
		return 0;
	n = eschave? escn: 1;
	if(n > Maxesc)
		n = Maxesc;
	switch(c){
	case 'C':
		for(i = 0; i < n; i++)
			curright();
		break;
	case 'D':
		for(i = 0; i < n; i++)
			curleft();
		break;
	case 'K':
		eraseeol(scr, flushr);
		break;
	case 'H':
		curpos = window.min;
		xp = xbuf;
		pendwrap = 0;
		break;
	case 'J':
		if(eschave && escn == 2)
			interactivemode(scr, flushr);
		break;
	}
	return 1;
}

/*
 * Text cursor (interactive mode only): an upside-down T between the previous and the current character.
 * The cell is saved first and put back before any output, so the text
 * under it is never disturbed.
 */
static void
txtcuroff(VGAscr *scr, Rectangle *flushr)
{
	Rectangle r;

	if(!curvis)
		return;
	curvis = 0;
	r = Rect(curat.x-Curgap, curat.y, curat.x+cellw, curat.y+fonth);
	memimagedraw(scr->gscreen, r, cursave, ZP, nil, ZP, S);
	combinerect(flushr, r);
}

static void
txtcuron(VGAscr *scr, Rectangle *flushr)
{
	Rectangle r, u;

	if(!interactive || cursave == nil || curvis)
		return;
	if(curpos.x+cellw > window.max.x || curpos.y+fonth > window.max.y)
		return;
	curat = curpos;
	r = Rect(curat.x-Curgap, curat.y, curat.x+cellw, curat.y+fonth);
	memimagedraw(cursave, cursave->r, scr->gscreen, r.min, nil, ZP, S);

	/*
	 * An upside-down T between two characters: a 2px bar straddling the
	 * cell boundary with a short 4px foot, no top bar (so it cannot be
	 * mistaken for a letter I).
	 */
	u = Rect(curat.x-1, curat.y, curat.x+1, r.max.y);
	memimagedraw(scr->gscreen, u, conscol, ZP, nil, ZP, S);
	u = Rect(curat.x-2, r.max.y-2, curat.x+2, r.max.y);
	memimagedraw(scr->gscreen, u, conscol, ZP, nil, ZP, S);
	r = Rect(curat.x-Curgap, curat.y, curat.x+cellw, curat.y+fonth);

	combinerect(flushr, r);
	curvis = 1;
}

static void
vgascreenputc(VGAscr* scr, char* buf, Rectangle *flushr)
{
	Point p;
	int w, pos, h;
	Rectangle r;

	if(xp < xbuf || xp >= &xbuf[nelem(xbuf)])
		xp = xbuf;

	if(escs && escfeed(scr, buf[0], flushr))
		return;
	if(buf[0] == '\033'){
		escs = 1;
		return;
	}

	h = fonth;
	switch(buf[0]){
	case '\n':
		if(pendwrap){
			/* the line exactly filled its row: we are already on a fresh one */
			pendwrap = 0;
			markrow(0, 0);
		} else
			nextrow(scr, flushr);
		xp = xbuf;
		curpos.x = window.min.x;
		break;

	case '\r':
		xp = xbuf;
		curpos.x = window.min.x;
		break;

	case '\t':
		p = memsubfontwidth(scr->memdefont, " ");
		w = p.x;
		if(curpos.x >= window.max.x-4*w)
			wrapline(scr, flushr);

		pos = (curpos.x-window.min.x)/w;
		pos = 4-(pos%4);
		*xp++ = curpos.x;
		r = Rect(curpos.x, curpos.y, curpos.x+pos*w, curpos.y + h);
		memimagedraw(scr->gscreen, r, back, ZP, nil, ZP, S);
		curpos.x += pos*w;
		pendwrap = 0;
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
			wrapline(scr, flushr);

		*xp++ = curpos.x;
		r = Rect(curpos.x, curpos.y, curpos.x+w, curpos.y+h);
		memimagedraw(scr->gscreen, r, back, r.min, nil, ZP, S);
		memimagestring(scr->gscreen, curpos, conscol, ZP, scr->memdefont, buf);
		combinerect(flushr, r);
		curpos.x += w;
		pendwrap = 0;
		/*
		 * Interactive mode wraps as soon as the row is full, not on the
		 * next character, so the cursor position is never ambiguous
		 * between the end of a row and the start of the next.
		 */
		if(interactive && curpos.x >= window.max.x-w)
			wrapline(scr, flushr);
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

	txtcuroff(scr, &flushr);
	e = s + n;
	while(s < e){
		rb[nrb++] = *s++;
		if(nrb >= UTFmax || fullrune(rb, nrb)){
			rb[nrb] = 0;
			vgascreenputc(scr, rb, &flushr);
			nrb = 0;
		}
	}
	txtcuron(scr, &flushr);
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
	Point cell;
	int mh, h, rows;

	mh = bootmarkheight();
	h = scr->memdefont->height;
	cell = memsubfontwidth(scr->memdefont, " ");
	cellw = cell.x;
	fonth = h;
	ncol = Ncol;
	interactive = 0;
	curvis = 0;
	if(cursave == nil)
		cursave = allocmemimage(Rect(0, 0, cellw+Curgap, h), scr->gscreen->chan);
	memset(softrow, 0, sizeof softrow);

	r = scr->gscreen->r;
	r.min.y += mh;
	memimagedraw(scr->gscreen, r, back, ZP, memopaque, ZP, S);

	fullwin = scr->gscreen->clipr;
	fullwin.min.y += mh;

	/*
	 * Whole text rows only, and one of them left blank at the bottom: the
	 * last row on screen is often cut off by the display, and a partial
	 * row (the scroll test in vgascreenputc() assumes the window height is
	 * a multiple of the font height) is worse than none. Same at the right
	 * edge: leave the last character cell empty.
	 */
	rows = Dy(fullwin)/h - 1;
	fullwin.max.y = fullwin.min.y + rows*h;
	fullwin.max.x -= cellw;

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
