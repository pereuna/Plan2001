/*
 * Plan2001 Boot ABI v1 (x86-64): how a loader hands the machine to the
 * kernel, and the BootInfo blob that carries everything it learned.
 * docs/boot-abi.md is the prose version of this contract; both sides
 * include this file.
 *
 * Entry: the loader jumps to the kernel's entry point (_efi64 in
 * sys/src/9/pc64/l.s) with the CPU in long mode, 4-level paging on,
 * interrupts off, DF clear and a usable stack, and
 *	RDI	physical address of the BootInfo blob
 *	RSI	0
 * Every other register is undefined.  The page tables need only
 * identity-map what the kernel touches before it switches to its own: its
 * image, where it is loaded, and its boot page tables and Mach at physical
 * 0x13000-0x1C000 (CPU0PML4..CPU0END in sys/src/9/pc64/mem.h), which it
 * clears first - so neither the loader's page tables, nor its stack, nor
 * the blob may lie there or in the image.  The kernel does not read the
 * blob through them; it maps it itself.  RDI is the kernel's only input:
 * there is no fixed address to look anything up at.
 *
 * The blob is one contiguous, page-aligned range of physical memory:
 *
 *	BootInfo header		(headersize bytes)
 *	config			plan9.ini text, configlen bytes, NUL-terminated
 *	loader log		the loader's printed text, loglen bytes
 *	memory map		mmapcount BootMem entries, mmapentsize bytes apart
 *
 * Offsets (…off) are from the start of the blob; addresses of things
 * outside it (acpi, fbbase) are physical.  The blob belongs to the kernel
 * from entry on and stays valid until the kernel itself releases it; it
 * must reserve [RDI, RDI+totalsize) before handing out any memory the map
 * calls free, as the blob itself sits in EfiLoaderData.
 *
 * Compatibility: a kernel takes a blob with its magic and version and a
 * headersize at least its own sizeof(BootInfo); fields past what it knows
 * are ignored.  A change that breaks that needs a new BootInfoVersion.
 */
enum {
	BootInfoMagic	= 0x49423250,	/* "P2BI" */
	BootInfoVersion	= 1,
};

/* BootMem.type: UEFI EFI_MEMORY_TYPE */
enum {
	BootMemReserved,
	BootMemLoaderCode,
	BootMemLoaderData,
	BootMemBootCode,
	BootMemBootData,
	BootMemRuntimeCode,
	BootMemRuntimeData,
	BootMemConventional,
	BootMemUnusable,
	BootMemACPIReclaim,
	BootMemACPINVS,
	BootMemMMIO,
	BootMemMMIOPort,
	BootMemPalCode,
	BootMemPersistent,
};

typedef struct BootInfo BootInfo;
typedef struct BootMem BootMem;

/* one entry of the UEFI memory map taken just before ExitBootServices */
struct BootMem {
	u64int	base;
	u64int	len;		/* bytes */
	u32int	type;		/* BootMem* */
	u32int	attr;		/* EFI_MEMORY_* attributes, low 32 bits */
};

struct BootInfo {
	u32int	magic;		/* BootInfoMagic */
	u32int	version;	/* BootInfoVersion */
	u32int	headersize;	/* sizeof(BootInfo) of the loader that made it */
	u32int	totalsize;	/* the whole blob: header, sections, padding */
	u32int	flags;		/* zero */

	u32int	configoff;	/* plan9.ini text */
	u32int	configlen;	/* bytes, including the terminating NUL */
	u32int	logoff;		/* the loader's own printed text */
	u32int	loglen;		/* bytes, 0 if none */
	u32int	mmapoff;	/* the UEFI memory map */
	u32int	mmapcount;	/* entries */
	u32int	mmapentsize;	/* bytes from one entry to the next, >= sizeof(BootMem) */

	u64int	acpi;		/* physical address of the ACPI RSDP, 0 if none */

	u64int	tscfreq;	/* TSC frequency in Hz, measured by the loader; 0 if unknown */
	u64int	epoch;		/* UTC seconds since 1970, from UEFI GetTime; 0 if unknown */

	/*
	 * Entropy from EFI_RNG_PROTOCOL, rngseedlen bytes valid (0 if the
	 * protocol was not found).  The kernel zeroes this once it has used
	 * it, so it does not sit around in memory as a readable secret.
	 */
	uchar	rngseed[64];
	u32int	rngseedlen;

	/* linear framebuffer from the UEFI graphics output protocol; fbbase is 0 if none */
	u64int	fbbase;
	u64int	fbsize;		/* bytes */
	u32int	fbwidth;	/* visible pixels per line */
	u32int	fbheight;
	u32int	fbstride;	/* pixels per scan line, ie the pitch */
	u32int	fbdepth;	/* bits per pixel */
	char	fbchan[16];	/* Plan 9 channel descriptor, eg x8r8g8b8 */
};
