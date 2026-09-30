/*
 * No graphics at all: 9pterm (Make.9pterm), drawterm as a text
 * terminal.  The screen functions the kernel's draw device and the
 * console call exist and do nothing; 9pterm never opens a screen.
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

Memimage *gscreen;	/* no screen: nil, which devcons and devmouse check */

extern void cpubody(void);

void
screeninit(void)
{
}

void
screensize(Rectangle r, ulong chan)
{
	USED(r);
	USED(chan);
}

Memdata*
attachscreen(Rectangle *r, ulong *chan, int *depth, int *width, int *softscreen)
{
	USED(r);
	USED(chan);
	USED(depth);
	USED(width);
	USED(softscreen);
	return nil;
}

void
flushmemscreen(Rectangle r)
{
	USED(r);
}

void
getcolor(ulong i, ulong *r, ulong *g, ulong *b)
{
	USED(i);
	*r = *g = *b = 0;
}

void
setcolor(ulong i, ulong r, ulong g, ulong b)
{
	USED(i);
	USED(r);
	USED(g);
	USED(b);
}

char*
clipread(void)
{
	return nil;
}

int
clipwrite(char *buf)
{
	USED(buf);
	return 0;
}

void
mouseset(Point p)
{
	USED(p);
}

void
setcursor(void)
{
}

void
titlewrite(char *buf)
{
	USED(buf);
}

void
guimain(void)
{
	cpubody();
}
