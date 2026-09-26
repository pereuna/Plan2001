#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
 * The loader passes what it learned in a BootInfo blob (Plan2001 Boot ABI
 * v1, sys/include/bootinfo.h, docs/boot-abi.md) and tells us where: each
 * ISA's entry code saves the address it was handed (AMD64: RDI, see
 * pc64/l.s) in bootinfopa.  There is no fixed address to look at.  This
 * file is the same for every ISA; what differs - mapping the blob before
 * the kernel's memory allocator exists, and the framebuffer's caching - is
 * bootearlymap() and fbmap() of the ISA port (pc64/bootarch.c).  The blob
 * is the only source of the memory map on this UEFI-only kernel, so
 * anything short of a complete, self-consistent blob is fatal:
 * bootinfoinit() below rejects an empty memory map or a section that does
 * not fit, not just a garbled header, precisely so meminit0() (pc/memory.c)
 * never falls through to its old BIOS-era ramscan() fallback - keeping that
 * path unreachable is the point, not an accident.  Nothing can be printed
 * this early; halt with interrupts off.
 */

BootInfo *bootinfo;

static void
bootinfohalt(void)
{
	for(;;)
		halt();
}

/*
 * Entropy from the loader's BootInfo, mixed in on top of whatever hwrandbuf
 * cpuidentify() (pc/devarch.c) already set (RDRAND, or nil), rather than
 * replacing it: more sources of entropy are never worse, and this way an
 * absent EFI_RNG_PROTOCOL or a CPU without RDRAND still leaves the other one
 * working.  Used once by randominit() (port/random.c) to seed the entropy
 * pool alongside the kernel's own timing-based collection.  Self-clearing:
 * the seed is copied at most once, then wiped from BootInfo (it would
 * otherwise sit in the blob, readable, for the life of the machine) and
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

/* does the section [off, off+len) lie within the blob, after the header? */
static int
bootinfoin(BootInfo *b, uvlong off, uvlong len)
{
	return off >= b->headersize && off <= b->totalsize && len <= b->totalsize - off;
}

/* do [a, a+al) and [b, b+bl), both within the blob, share a byte? */
static int
bootinfooverlap(uvlong a, uvlong al, uvlong b, uvlong bl)
{
	return al != 0 && bl != 0 && a < b + bl && b < a + al;
}

void
bootinfoinit(void)
{
	BootInfo *b;
	uvlong mmaplen;

	if(bootinfopa == 0 || (bootinfopa & (BY2PG-1)) != 0)
		bootinfohalt();
	if((b = bootearlymap(bootinfopa, BY2PG)) == nil)
		bootinfohalt();
	if(b->magic != BootInfoMagic || b->version != BootInfoVersion
	|| b->headersize < sizeof(BootInfo) || b->headersize > BY2PG
	|| b->totalsize < b->headersize)
		bootinfohalt();
	if(bootearlymap(bootinfopa, b->totalsize) != b)
		bootinfohalt();
	mmaplen = (uvlong)b->mmapcount * b->mmapentsize;
	if(b->configlen == 0 || !bootinfoin(b, b->configoff, b->configlen)
	|| ((char*)b)[b->configoff + b->configlen - 1] != '\0'
	|| !bootinfoin(b, b->logoff, b->loglen)
	|| b->mmapcount == 0 || b->mmapentsize < sizeof(BootMem)
	|| !bootinfoin(b, b->mmapoff, mmaplen)
	|| bootinfooverlap(b->configoff, b->configlen, b->logoff, b->loglen)
	|| bootinfooverlap(b->configoff, b->configlen, b->mmapoff, mmaplen)
	|| bootinfooverlap(b->logoff, b->loglen, b->mmapoff, mmaplen))
		bootinfohalt();
	bootinfo = b;
}

/* entry i of the memory map, i < bootinfo->mmapcount */
BootMem*
bootmem(int i)
{
	return (BootMem*)((uchar*)bootinfo + bootinfo->mmapoff + (uvlong)i*bootinfo->mmapentsize);
}

/*
 * What a memory map entry is to the kernel, from its UEFI type (BootMem*,
 * sys/include/bootinfo.h): the ISA port turns these into its own memory
 * kinds (pc/memory.c: MemRAM, MemACPI, MemReserved); -1 for a type this
 * kernel does not know, which it should leave alone.
 */
int
bootmemclass(u32int type)
{
	switch(type){
	case BootMemLoaderCode:
	case BootMemLoaderData:
	case BootMemBootCode:
	case BootMemBootData:
	case BootMemConventional:
		return BootClassRAM;
	case BootMemACPIReclaim:
		return BootClassACPI;
	case BootMemReserved:
	case BootMemRuntimeCode:
	case BootMemRuntimeData:
	case BootMemUnusable:
	case BootMemACPINVS:
	case BootMemMMIO:
	case BootMemMMIOPort:
	case BootMemPalCode:
		return BootClassReserved;
	}
	return -1;
}

/* the plan9.ini text, NUL-terminated (checked by bootinfoinit()) */
char*
bootconfig(void)
{
	return (char*)bootinfo + bootinfo->configoff;
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
