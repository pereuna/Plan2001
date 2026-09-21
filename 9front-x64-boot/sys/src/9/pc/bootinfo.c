#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
 * The loader passes what it learned from UEFI in a BootInfo at BOOTINFO
 * (sys/include/bootinfo.h).  It is the only source of the memory map, so a
 * missing or unknown structure is fatal.  Nothing can be printed this early;
 * halt with interrupts off.
 */

BootInfo *bootinfo;

void
bootinfoinit(void)
{
	BootInfo *b;

	b = (BootInfo*)BOOTINFO;
	if(b->magic != BootInfoMagic || b->version != BootInfoVersion
	|| b->size > BOOTINFOLEN || b->nmem > BootInfoMaxMem)
		for(;;)
			halt();
	bootinfo = b;
}
