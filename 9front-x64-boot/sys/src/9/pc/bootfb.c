#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
 * Boot progress markers on the UEFI framebuffer.
 *
 * No text, no font, no /dev/cons: a row of small squares in the top left
 * corner of the screen, one per boot stage, and a red bar when the kernel
 * panics.  Unreached stages show as dark squares, so the first dark square
 * is the stage the kernel never got past.
 *
 * The loader passes the framebuffer in *bootscreen (see efi.c screenconf):
 *
 *	*bootscreen=PIXELSPERSCANLINExHEIGHTxDEPTH CHAN PA
 *
 * The first number is the row pitch in pixels, not the visible width.
 * Only 32 bits per pixel (x8r8g8b8 or x8b8g8r8) are handled; anything else
 * leaves the markers off.  Marks made before the framebuffer is mapped
 * (bootfbinit needs the memory allocator) are remembered and drawn then.
 */

enum {
	Nmark	= 5,		/* stages drawn as squares: BMMain..BMExec */
	Size	= 24,		/* square size in pixels */
	Gap	= 8,
	Margin	= 16,
	Barh	= Margin+Size+Margin,	/* height of the panic bar */

	Dark	= 0x303030,
	White	= 0xFFFFFF,
	Green	= 0x00FF00,
	Red	= 0xFF0000,
};

static u32int *fb;		/* mapped framebuffer, 32 bits per pixel */
static ulong stride;		/* pixels per scan line */
static ulong height;
static int bgr;			/* x8b8g8r8 instead of x8r8g8b8 */
static ulong reached;		/* one bit per stage, set even while fb == nil */

static void
rect(int x, int y, int w, int h, u32int rgb)
{
	u32int *p, c;
	int i, j;

	c = rgb;
	if(bgr)
		c = (rgb & 0x00FF00) | (rgb>>16 & 0xFF) | (rgb & 0xFF)<<16;
	if(y+h > height)
		h = height-y;
	for(j = 0; j < h; j++){
		p = fb + (y+j)*stride + x;
		for(i = 0; i < w; i++)
			p[i] = c;
	}
}

static void
draw(int n)
{
	if(n == BMPanic){
		rect(0, 0, stride, Barh, Red);
		return;
	}
	rect(Margin + n*(Size+Gap), Margin, Size, Size, n == BMExec? Green: White);
}

void
bootfbinit(void)
{
	char *s;
	ulong w, h, sz;
	uvlong pa;
	void *v;
	int i;

	if(fb != nil || (s = getconf("*bootscreen")) == nil)
		return;
	w = strtoul(s, &s, 0);
	if(w == 0 || *s++ != 'x')
		return;
	h = strtoul(s, &s, 0);
	if(h == 0 || *s++ != 'x')
		return;
	if(strtoul(s, &s, 0) != 32 || *s++ != ' ')
		return;
	if(strncmp(s, "x8r8g8b8 ", 9) == 0)
		bgr = 0;
	else if(strncmp(s, "x8b8g8r8 ", 9) == 0)
		bgr = 1;
	else
		return;
	pa = strtoull(s+9, nil, 0);
	if(pa == 0)
		return;
	sz = w*h*4;
	if((v = vmap(pa, sz)) == nil)
		return;
	patwc(v, sz);
	stride = w;
	height = h;
	fb = v;

	/* there is a display: let panic hang with the marks visible, not reboot */
	conf.monitor = 1;

	for(i = 0; i < Nmark; i++)
		rect(Margin + i*(Size+Gap), Margin, Size, Size, Dark);
	for(i = 0; i <= BMPanic; i++)
		if(reached & 1UL<<i)
			draw(i);
}

void
bootmark(int n)
{
	reached |= 1UL<<n;
	if(fb != nil)
		draw(n);
}
