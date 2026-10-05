/*
 * aux/reimage - the whole disk rewritten from the network, in the boot
 * environment (/rc/lib/reimage.rc, from plan9.ini's reimage=): there no
 * file system of the disk is in use, as in a rescue system.
 *
 *	aux/reimage -p port -k key -h sha256 -s size -d /dev/sdXX [-t secs]
 *
 * One connection on tcp port: it gets "plan2001 reimage", must give key
 * as its first line, then a gzip stream (gzip -n: no name or extras) of
 * the raw image, size bytes.  The image goes to the disk's second half
 * first (at the first 64 KiB block past the image), the compressed
 * stream hashed as it comes; only when its SHA-256, the size and the
 * inflation all agree is it copied to the start of the disk and the
 * machine rebooted.  Else - or no sender within secs (default 1800) - the
 * old system has not been touched: exit with an error, bootrc goes on.
 * What happens goes to the sender, a line each, and to stderr.
 */
#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>
#include <flate.h>

enum {
	Blk	= 64*1024,
	Bufsz	= 64*1024,
};

static int net = -1;
static int disk = -1;
static uchar inbuf[Bufsz];
static int inn, inp;
static vlong got;		/* compressed bytes */
static DigestState *ds;
static uchar *outblk;
static int outn;
static vlong outoff, wrote;	/* where the next block goes; bytes inflated */

static void
say(char *fmt, ...)
{
	char buf[256];
	va_list arg;

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf - 1, fmt, arg);
	va_end(arg);
	fprint(2, "reimage: %s\n", buf);
	if(net >= 0)
		fprint(net, "%s\n", buf);
}

static void
fail(char *fmt, ...)
{
	char buf[256];
	va_list arg;

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	say("%s: the old system stays", buf);
	exits(buf);
}

/* the next compressed byte, the stream hashed as it comes */
static int
getbyte(void*)
{
	if(inp == inn){
		inn = read(net, inbuf, sizeof inbuf);
		if(inn <= 0){
			inn = inp = 0;
			return -1;
		}
		ds = sha2_256(inbuf, inn, nil, ds);
		got += inn;
		inp = 0;
	}
	return inbuf[inp++];
}

static int
flush(void)
{
	if(outn == 0)
		return 0;
	if(pwrite(disk, outblk, outn, outoff) != outn)
		return -1;
	outoff += outn;
	outn = 0;
	return 0;
}

static int
putbytes(void*, void *p, int n)
{
	uchar *b;
	int m, n0;

	b = p;
	n0 = n;
	wrote += n;
	while(n > 0){
		m = Blk - outn;
		if(m > n)
			m = n;
		memmove(outblk + outn, b, m);
		outn += m;
		b += m;
		n -= m;
		if(outn == Blk && flush() < 0)
			return -1;
	}
	return n0;
}

static int
dial1(char *port, long secs)
{
	char adir[40], ldir[40], *a;
	int afd, lfd, fd;

	a = smprint("tcp!*!%s", port);
	if((afd = announce(a, adir)) < 0)
		sysfatal("announce %s: %r", a);
	alarm(secs*1000);
	lfd = listen(adir, ldir);
	alarm(0);
	if(lfd < 0)
		sysfatal("no sender in %ld s", secs);
	fd = accept(lfd, ldir);
	close(lfd);
	close(afd);
	if(fd < 0)
		sysfatal("accept: %r");
	return fd;
}

static void
usage(void)
{
	fprint(2, "usage: aux/reimage -p port -k key -h sha256 -s size -d disk [-t secs]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *port, *key, *hash, *dname, line[256], hex[2*SHA2_256dlen+1];
	uchar h[SHA2_256dlen], g[10], *cp;
	vlong size, total, stage, off;
	long secs;
	Dir *d;
	int i, n, c, r;

	port = key = hash = dname = nil;
	size = -1;
	secs = 1800;
	ARGBEGIN{
	case 'p': port = EARGF(usage()); break;
	case 'k': key = EARGF(usage()); break;
	case 'h': hash = EARGF(usage()); break;
	case 's': size = strtoll(EARGF(usage()), nil, 10); break;
	case 'd': dname = EARGF(usage()); break;
	case 't': secs = atol(EARGF(usage())); break;
	default: usage();
	}ARGEND
	if(port == nil || key == nil || hash == nil || dname == nil || size <= 0)
		usage();

	/* the disk, and room for the image twice */
	snprint(line, sizeof line, "%s/data", dname);
	if((disk = open(line, ORDWR)) < 0)
		sysfatal("%s: %r", line);
	if((d = dirfstat(disk)) == nil)
		sysfatal("%s: %r", line);
	total = d->length;
	free(d);
	stage = (size + Blk - 1) / Blk * Blk + 16*Blk;
	if(stage + size > total)
		sysfatal("the disk, %lld bytes, is less than twice the image, %lld", total, size);
	fprint(2, "reimage: %s, %lld bytes, image %lld, staged at %lld; waiting on tcp %s\n",
		dname, total, size, stage, port);

	net = dial1(port, secs);
	fprint(net, "plan2001 reimage\n");
	for(n = 0; n < sizeof line - 1; n++){
		if(read(net, line+n, 1) != 1)
			fail("no key");
		if(line[n] == '\n')
			break;
	}
	line[n] = 0;
	if(strlen(line) != strlen(key) || tsmemcmp(line, key, strlen(key)) != 0)
		fail("wrong key");
	say("receiving");

	/* gzip -n: a 10-byte header, deflate, then crc and size */
	for(i = 0; i < 10; i++){
		if((c = getbyte(nil)) < 0)
			fail("short stream");
		g[i] = c;
	}
	if(g[0] != 0x1f || g[1] != 0x8b || g[2] != 8 || g[3] != 0)
		fail("not gzip -n");
	outblk = malloc(Blk);
	if(outblk == nil)
		fail("no memory");
	outoff = stage;
	inflateinit();
	if((r = inflate(nil, putbytes, nil, getbyte)) != FlateOk)
		fail("inflate: %s", flateerr(r));
	if(flush() < 0)
		fail("write at %lld: %r", outoff);
	while(getbyte(nil) >= 0)	/* the trailer, hashed too */
		;
	sha2_256(nil, 0, h, ds);
	for(i = 0; i < SHA2_256dlen; i++)
		sprint(hex+2*i, "%.2ux", h[i]);
	if(cistrcmp(hex, hash) != 0)
		fail("hash %s is not %s", hex, hash);
	if(wrote != size)
		fail("%lld bytes, not %lld", wrote, size);
	say("verified: %lld compressed bytes, %lld bytes; copying to the start of the disk", got, wrote);

	/* staged copy to the start, the old system's place */
	cp = outblk;
	for(off = 0; off < size; off += n){
		n = size - off > Blk ? Blk : size - off;
		if(pread(disk, cp, n, stage + off) != n || pwrite(disk, cp, n, off) != n)
			fail("copy at %lld: %r", off);
	}
	say("done, rebooting into the new system");
	close(net);
	sleep(1000);
	if((i = open("#c/reboot", OWRITE)) >= 0)
		fprint(i, "reboot");
	exits(nil);
}
