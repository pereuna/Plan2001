#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
 * The loader passes what it learned from UEFI in a BootInfo at BOOTINFO
 * (sys/include/bootinfo.h).  It is the only source of the memory map on
 * this UEFI-only kernel, so anything short of a complete, self-consistent
 * structure is fatal: bootinfoinit() below rejects an empty or truncated
 * memory map too, not just a garbled header, precisely so meminit0()
 * (pc/memory.c) never falls through to its old BIOS-era ramscan() fallback
 * - keeping that path unreachable is the point, not an accident. Nothing
 * can be printed this early; halt with interrupts off.
 */

BootInfo *bootinfo;

/*
 * Entropy from the loader's BootInfo, mixed in on top of whatever hwrandbuf
 * cpuidentify() (pc/devarch.c) already set (RDRAND, or nil), rather than
 * replacing it: more sources of entropy are never worse, and this way an
 * absent EFI_RNG_PROTOCOL or a CPU without RDRAND still leaves the other one
 * working.  Used once by randominit() (port/random.c) to seed the entropy
 * pool alongside the kernel's own timing-based collection.  Self-clearing:
 * the seed is copied at most once, then wiped from BootInfo (it would
 * otherwise sit in low memory, readable, for the life of the machine) and
 * hwrandbuf is put back to what it chains to, so a second call is cheap and
 * does not re-touch already-zeroed memory.
 */
static void (*bootinforandnext)(void*, ulong);

static void
bootinforand(void *p, ulong n)
{
	uchar *q;
	ulong i, m;

	if(bootinforandnext != nil)
		(*bootinforandnext)(p, n);

	q = p;
	m = n;
	if(m > bootinfo->rngseedlen)
		m = bootinfo->rngseedlen;
	for(i = 0; i < m; i++)
		q[i] ^= bootinfo->rngseed[i];

	memset(bootinfo->rngseed, 0, sizeof(bootinfo->rngseed));
	bootinfo->rngseedlen = 0;
	hwrandbuf = bootinforandnext;
}

void
bootinfoinit(void)
{
	BootInfo *b;
	uintptr hdrsize;

	b = (BootInfo*)BOOTINFO;
	hdrsize = (uchar*)&b->mem[0] - (uchar*)b;
	if(b->magic != BootInfoMagic || b->version != BootInfoVersion
	|| b->size < hdrsize || b->size > BOOTINFOLEN
	|| b->nmem == 0 || b->nmem > BootInfoMaxMem
	|| b->size != hdrsize + (uintptr)b->nmem*sizeof(BootMem)
	|| (b->flags & BootInfoMemTrunc) != 0)
		for(;;)
			halt();
	bootinfo = b;
}

/*
 * Wire the BootInfo entropy in.  cpuidentify() (pc/devarch.c) sets hwrandbuf
 * unconditionally, and every CPU calls it once: the boot processor early in
 * main(), and each AP from squidboy() during its own bring-up.  This must
 * therefore run after all of them have, or a later one would clobber our
 * wiring; arch->intrinit() (pc/mp.c) starts the APs one at a time and each
 * mpstartap() call waits for the AP's apic->online flag, which squidboy()
 * only sets after its own cpuidentify(), so intrinit() returning is the
 * first point where no cpuidentify() call is still pending.
 */
void
bootinforandinit(void)
{
	if(bootinfo == nil || bootinfo->rngseedlen == 0)
		return;
	bootinforandnext = hwrandbuf;
	hwrandbuf = bootinforand;
}

/*
 * Seed the wall clock from the loader's reading of the UEFI real-time clock,
 * the same way a write to #c/time does (port/devcons.c), so the clock is
 * right from the first line of boot output instead of only once /boot/boot
 * or some other user program next sets it.  todset() calls todinit() on
 * first use, which starts a clock0 link (port/tod.c todfix) and arms it
 * right away through arch->timerset(); call this after arch->clockenable(),
 * not from bootinfoinit() (too early: no malloc yet, and lapictimerset()
 * divides by an LAPIC timer divisor that clockenable() has not set up yet).
 *
 * tscfreq, measured by the loader against its own timer before any kernel
 * code ran, is not used to skip the kernel's own TSC calibration (i8253.c,
 * hpet.c): that calibration also derives delaylcycles, which delayloop()
 * needs and which tscfreq alone does not give us.  It is only printed here,
 * as a cross-check against the value the kernel measures a little later.
 */
void
bootinfoclock(void)
{
	if(bootinfo == nil)
		return;
	if(bootinfo->epoch != 0)
		todset(bootinfo->epoch * 1000000000LL, 0, 0);
	if(bootinfo->epoch != 0 || bootinfo->tscfreq != 0)
		print("bootinfo: epoch %llud tscfreq %llud\n", bootinfo->epoch, bootinfo->tscfreq);
}
