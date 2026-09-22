#include <u.h>
#include "fns.h"
#include "efi.h"
#include "mem.h"

UINTN MK;
EFI_HANDLE IH;
EFI_SYSTEM_TABLE *ST;

void* (*open)(char *name);
int (*read)(void *f, void *data, int len);
void (*close)(void *f);
void (*stop)(void);

/*
 * on ia32 and amd64, we use IMAGE_FILE_RELOCS_STRIPPED which
 * disables relocations, so this is a no-op.
 *
 * on arm64, the EFI loader can move our code, so we need to
 * update some of our stored addresses (such as callbacks)
 * which assume we are loaded at our requested base address.
 */
extern void *rebase(void *addr);

void
putc(int c)
{
	CHAR16 w[2];

	w[0] = c;
	w[1] = 0;
	eficall(ST->ConOut->OutputString, ST->ConOut, w);
}

int
getc(void)
{
	EFI_INPUT_KEY k;

	if(eficall(ST->ConIn->ReadKeyStroke, ST->ConIn, &k))
		return 0;
	return k.UnicodeChar;
}

void
usleep(int us)
{
	eficall(ST->BootServices->Stall, (UINTN)us);
}

/*
 * Claim [pa, pa+len) as loader code (AllocateAddress = 2).  Firmware marks
 * free memory no-execute, which matters as the kernel is entered with the
 * firmware's page tables still active.  Returns non-zero if it fails.
 */
int
efialloc(uvlong pa, uvlong len)
{
	uvlong a;

	a = pa;
	return eficall(ST->BootServices->AllocatePages, (UINTN)2, (UINTN)EfiLoaderCode,
		(UINTN)((len + 4095) / 4096), &a) != 0;
}

/*
 * Release pages efialloc() claimed, eg after a later failure: without this
 * a retry (efimain's loop, on a bad read or a rejected kernel) finds the
 * same range already allocated and fails immediately instead of trying
 * again.
 */
void
efifree(uvlong pa, uvlong len)
{
	eficall(ST->BootServices->FreePages, (UINTN)pa, (UINTN)((len + 4095) / 4096));
}

/*
 * The BootInfo the kernel is entered with, built in low memory (see
 * sys/include/bootinfo.h).
 */
static BootInfo *bi = (BootInfo*)BOOTINFO;

/*
 * Fetch the final memory map and leave boot services, retrying with a fresh
 * map if the firmware reports that it changed, as the specification requires.
 * No printing between GetMemoryMap and ExitBootServices: it changes the map.
 * Returns non-zero if boot services could not be left; they are still usable
 * then.  On success the map is recorded in the BootInfo, adjacent entries of
 * the same type and attributes merged, and no boot service may be used again.
 */
int
bootexit(void)
{
	UINTN mapsize, entsize;
	EFI_MEMORY_DESCRIPTOR *t;
	uchar mapbuf[96*1024], *p;
	UINT32 entvers;
	BootMem *m;
	int try;

	for(try = 0; try < 4; try++){
		mapsize = sizeof(mapbuf);
		entsize = sizeof(EFI_MEMORY_DESCRIPTOR);
		entvers = 1;
		if(eficall(ST->BootServices->GetMemoryMap, &mapsize, mapbuf, &MK, &entsize, &entvers))
			return -1;
		if(eficall(ST->BootServices->ExitBootServices, IH, MK) == 0)
			goto Left;
	}
	return -1;

Left:
	bi->nmem = 0;
	for(p = mapbuf; mapsize >= entsize; p += entsize, mapsize -= entsize){
		t = (EFI_MEMORY_DESCRIPTOR*)p;
		if(t->NumberOfPages == 0)
			continue;
		m = nil;
		if(bi->nmem > 0){
			m = &bi->mem[bi->nmem-1];
			if(m->type != t->Type || m->attr != (UINT32)t->Attribute
			|| m->base + m->len != t->PhysicalStart)
				m = nil;
		}
		if(m != nil){
			m->len += t->NumberOfPages * 4096ULL;
			continue;
		}
		if(bi->nmem >= BootInfoMaxMem){
			bi->flags |= BootInfoMemTrunc;
			break;
		}
		m = &bi->mem[bi->nmem++];
		m->base = t->PhysicalStart;
		m->len = t->NumberOfPages * 4096ULL;
		m->type = t->Type;
		m->attr = (UINT32)t->Attribute;
	}
	bi->size = (uchar*)&bi->mem[bi->nmem] - (uchar*)bi;
	return 0;
}

/*
 * TSC frequency, measured against the firmware's own timer (Stall).  Not as
 * accurate as the kernel's own HPET-based measurement, but available before
 * any kernel code runs, and a sanity check for it.  0 if RDTSC is not usable
 * this early for some reason; the kernel falls back to its own calibration.
 */
static void
tscconf(void)
{
	uvlong t0, t1;
	enum { Ms = 50 };

	t0 = rdtsc();
	eficall(ST->BootServices->Stall, (UINTN)(Ms*1000));
	t1 = rdtsc();
	if(t1 > t0)
		bi->tscfreq = (t1 - t0) * 1000 / Ms;
}

/*
 * Wall clock time from the runtime service, callable before boot services
 * end.  civilsecs converts a Gregorian date to seconds since 1970-01-01,
 * using days_from_civil (Howard Hinnant, public domain) for the date part;
 * GetTime() returns local time, and per the UEFI spec Localtime = UTC -
 * TimeZone (minutes), so TimeZone is added to local to recover UTC.
 * TimeZone may be unspecified (already UTC, the common case for firmware
 * that keeps the RTC in UTC).
 */
static uvlong
civilsecs(EFI_TIME *t)
{
	int y, era, yoe, doy, doe;
	long days;

	y = t->Year - (t->Month <= 2);
	era = (y >= 0? y: y-399) / 400;
	yoe = y - era*400;
	doy = (153*(t->Month + (t->Month > 2? -3: 9)) + 2)/5 + t->Day-1;
	doe = yoe*365 + yoe/4 - yoe/100 + doy;
	days = era*146097L + doe - 719468;

	return (uvlong)days*24*3600 + t->Hour*3600 + t->Minute*60 + t->Second;
}

static void
timeconf(void)
{
	EFI_TIME t;

	if(eficall(ST->RuntimeServices->GetTime, &t, nil))
		return;
	bi->epoch = civilsecs(&t);
	if(t.TimeZone != EfiUnspecifiedTimeZone)
		bi->epoch += t.TimeZone*60;
}

/*
 * Entropy from the firmware's RNG, if it has one; otherwise rngseedlen stays
 * 0 and the kernel's own entropy collection is unaffected.
 */
static void
rngconf(void)
{
	static EFI_GUID EFI_RNG_PROTOCOL_GUID = {
		0x3152bca5, 0xeade, 0x433d,
		0x86, 0x2e, 0xc0, 0x1c,
		0xdc, 0x29, 0x1f, 0x44,
	};
	EFI_RNG_PROTOCOL *rng;
	UINTN len;

	rng = nil;
	if(eficall(ST->BootServices->LocateProtocol, &EFI_RNG_PROTOCOL_GUID, nil, &rng) || rng == nil)
		return;
	len = sizeof(bi->rngseed);
	if(eficall(rng->GetRNG, rng, nil, len, bi->rngseed) == 0)
		bi->rngseedlen = len;
}

static void
acpiconf(void)
{
	static EFI_GUID ACPI_20_TABLE_GUID = {
		0x8868e871, 0xe4f1, 0x11d3,
		0xbc, 0x22, 0x00, 0x80,
		0xc7, 0x3c, 0x88, 0x81,
	};
	static EFI_GUID ACPI_10_TABLE_GUID = {
		0xeb9d2d30, 0x2d88, 0x11d3,
		0x9a, 0x16, 0x00, 0x90,
		0x27, 0x3f, 0xc1, 0x4d,
	};
	EFI_CONFIGURATION_TABLE *t;
	uintptr pa;
	char buf[32], *s;
	int n;

	pa = 0;
	t = ST->ConfigurationTable;
	n = ST->NumberOfTableEntries;
	while(--n >= 0){
		if(memcmp(&t->VendorGuid, &ACPI_10_TABLE_GUID, sizeof(EFI_GUID)) == 0){
			if(pa == 0)
				pa = (uintptr)t->VendorTable;
		} else if(memcmp(&t->VendorGuid, &ACPI_20_TABLE_GUID, sizeof(EFI_GUID)) == 0)
			pa = (uintptr)t->VendorTable;
		t++;
	}

	bi->acpi = pa;
	if(pa){
		s = buf;
		memmove(s, "acpi=0x", 7), s += 7;
		s = hexfmt(s, 0, pa), *s++ = '\n';
		*s = '\0';
		print(buf);
	}
}

static int
topbit(ulong mask)
{
	int bit = 0;

	while(mask != 0){
		mask >>= 1;
		bit++;
	}
	return bit;
}

static int
lowbit(ulong mask)
{
	int bit = 0;

	while((mask & 1) == 0){
		mask >>= 1;
		bit++;
	}
	return bit;
}

static void
screenconf(void)
{
	static EFI_GUID EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID = {
		0x9042a9de, 0x23dc, 0x4a38,
		0x96, 0xfb, 0x7a, 0xde,
		0xd0, 0x80, 0x51, 0x6a,
	};
	EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
	EFI_HANDLE *Handles;
	UINTN Count;

	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
	ulong mr, mg, mb, mx, mc;
	int i, bits, depth;
	char buf[96], *s;

	Count = 0;
	Handles = nil;
	if(eficall(ST->BootServices->LocateHandleBuffer,
		ByProtocol, &EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, nil, &Count, &Handles))
		return;

	for(i=0; i<Count; i++){
		gop = nil;
		if(eficall(ST->BootServices->HandleProtocol,
			Handles[i], &EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, &gop))
			continue;

		if(gop == nil)
			continue;
		if((info = gop->Mode->Info) == nil)
			continue;

		switch(info->PixelFormat){
		default:
			continue;	/* unsupported */

		case PixelRedGreenBlueReserved8BitPerColor:
			mr = 0x000000ff;
			mg = 0x0000ff00;
			mb = 0x00ff0000;
			mx = 0xff000000;
			break;

		case PixelBlueGreenRedReserved8BitPerColor:
			mb = 0x000000ff;
			mg = 0x0000ff00;
			mr = 0x00ff0000;
			mx = 0xff000000;
			break;

		case PixelBitMask:
			mr = info->PixelInformation.RedMask;
			mg = info->PixelInformation.GreenMask;
			mb = info->PixelInformation.BlueMask;
			mx = info->PixelInformation.ReservedMask;
			break;
		}

		if((depth = topbit(mr | mg | mb | mx)) == 0)
			continue;

		/* make sure we have linear framebuffer */
		if(gop->Mode->FrameBufferBase == 0)
			continue;
		if(gop->Mode->FrameBufferSize == 0)
			continue;

		goto Found;
	}
	return;

Found:
	bi->fbbase = gop->Mode->FrameBufferBase;
	bi->fbsize = gop->Mode->FrameBufferSize;
	bi->fbwidth = info->HorizontalResolution;
	bi->fbheight = info->VerticalResolution;
	bi->fbstride = info->PixelsPerScanLine;
	bi->fbdepth = depth;

	/* channel descriptor, eg x8r8g8b8 */
	s = bi->fbchan;
	while(depth > 0 && s < bi->fbchan + sizeof(bi->fbchan) - 4){
		if(depth == topbit(mr)){
			mc = mr;
			*s++ = 'r';
		} else if(depth == topbit(mg)){
			mc = mg;
			*s++ = 'g';
		} else if(depth == topbit(mb)){
			mc = mb;
			*s++ = 'b';
		} else if(depth == topbit(mx)){
			mc = mx;
			*s++ = 'x';
		} else {
			break;
		}
		bits = depth - lowbit(mc);
		s = decfmt(s, 0, bits);
		depth -= bits;
	}
	*s = '\0';

	s = buf;
	memmove(s, "fb=", 3), s += 3;
	s = decfmt(s, 0, bi->fbwidth), *s++ = 'x';
	s = decfmt(s, 0, bi->fbheight), *s++ = 'x';
	s = decfmt(s, 0, bi->fbdepth), *s++ = ' ';
	memmove(s, "stride ", 7), s += 7;
	s = decfmt(s, 0, bi->fbstride), *s++ = ' ';
	memmove(s, bi->fbchan, strlen(bi->fbchan)), s += strlen(bi->fbchan);
	memmove(s, " 0x", 3), s += 3;
	s = hexfmt(s, 0, bi->fbbase), *s++ = '\n';
	*s = '\0';
	print(buf);
}

void
eficonfig(void)
{
	memset(bi, 0, sizeof(*bi));
	bi->magic = BootInfoMagic;
	bi->version = BootInfoVersion;
	bi->size = sizeof(*bi) - sizeof(bi->mem);

	/* the memory map is added by bootexit(), right before leaving boot services */
	acpiconf();
	screenconf();
	tscconf();
	timeconf();
	rngconf();
}

EFI_STATUS
efimain(EFI_HANDLE ih, EFI_SYSTEM_TABLE *st)
{
	char path[MAXPATH], *kern;
	void *f;

	IH = ih;
	ST = st;

	/*
	 * Claim the low scratch area (plan9.ini text and BootInfo,
	 * CONFADDR..BOOTSCRATCHEND) from the firmware's own allocator before
	 * writing anything there, so it cannot be handed to some other
	 * boot-time allocation in the meantime.  Not fatal if it fails: this
	 * is defense in depth on top of the kernel's own memreserve(0,
	 * PADDR(CPU0END)) in pc/memory.c, which protects the same range (and
	 * more) once the kernel is running - this call only narrows the
	 * window before that.
	 */
	if(efialloc(CONFADDR, BOOTSCRATCHEND-CONFADDR) != 0)
		print("[Plan2001 efi.c] warning: firmware would not reserve low memory for us\n");

	f = nil;
	if(pxeinit(&f) && isoinit(&f) && fsinit(&f))
		print("no boot devices\n");

	open = rebase(open);
	read = rebase(read);
	close = rebase(close);
	if(stop) stop = rebase(stop);

	for(;;){
		kern = configure(f, path);
		f = open(kern);
		if(f == nil){
			print("not found\n");
			continue;
		}
		print(bootkern(f));
		print("\n");
		close(f);
		f = nil;
	}
}
