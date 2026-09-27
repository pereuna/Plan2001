/*
 * gui-web: drawterm's screen, mouse and keyboard in the browser.
 * Phase 1b runs drawterm with -G only, so these are the backend's entry
 * points without a screen; phase 1c draws gscreen into a canvas.
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
#include "screen.h"

Memimage *gscreen;
static char *snarfbuf;

void
guimain(void)
{
	cpubody();
}

void
screeninit(void)
{
	panic("gui-web: no screen yet, run with -G");
}

void
screensize(Rectangle r, ulong chan)
{
}

Memdata*
attachscreen(Rectangle *r, ulong *chan, int *depth, int *width, int *softscreen)
{
	return nil;
}

void
flushmemscreen(Rectangle r)
{
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
}

void
setcursor(void)
{
}

void
titlewrite(char *buf)
{
}
