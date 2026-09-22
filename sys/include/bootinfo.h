/*
 * BootInfo: what the UEFI loader (sys/src/boot/efi) learned from the firmware
 * and tells the kernel, as a versioned structure instead of plan9.ini text.
 *
 * The loader builds it at physical address BOOTINFO (sys/src/9/pc64/mem.h)
 * and enters the kernel with it in place.  Everything that can be found out
 * through UEFI before ExitBootServices belongs here; plan9.ini stays for
 * user configuration only.  Both sides include this file; a change of layout
 * needs a new BootInfoVersion.
 */
enum {
	BootInfoMagic	= 0x464e4942,	/* "BINF" */
	BootInfoVersion	= 2,
	BootInfoMaxMem	= 600,		/* memory map entries; typical firmware has 50-200 */
};

/* BootInfo.flags */
enum {
	BootInfoMemTrunc = 1<<0,	/* the memory map did not fit, entries were dropped */
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
	u32int	size;		/* bytes in use: header and memory map */
	u32int	flags;

	u64int	acpi;		/* physical address of the ACPI RSDP, 0 if none */

	u64int	tscfreq;	/* TSC frequency in Hz, measured by the loader; 0 if unknown */
	u64int	epoch;		/* UTC seconds since 1970, from UEFI GetTime; 0 if unknown */

	/*
	 * Entropy from EFI_RNG_PROTOCOL, rngseedlen bytes valid (0 if the
	 * protocol was not found).  The kernel zeroes this once it has used
	 * it, so it does not sit around in low memory as a readable secret.
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

	u32int	nmem;		/* entries used in mem[] */
	u32int	rsvd[7];	/* zero */

	BootMem	mem[BootInfoMaxMem];
};
