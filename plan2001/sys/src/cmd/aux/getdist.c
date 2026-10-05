/*
 * getdist - a part of the wasm32 distribution (tools/dist-parts: base, src,
 * games; tar.gz) from the server's aux/distd, on standard output, for the
 * machine's installer (/boot/install: aux/getdist base | gunzip | tar x).
 * The server sends blocks, each its length (4 bytes, big-endian) and its
 * bytes, and waits for a byte before the next: a WebSocket has no flow
 * control of its own, and the page holds only so much (platform.js's
 * QMAXRAW).  A block of 0 ends it; -1, and a line after it, is an error.
 *
 *	aux/getdist [-s server] [-v] name
 *
 * server: its address (tcp!HOST!17050), else $cpu's; -v: the bytes so
 * far on standard error, now and then.
 */
#include <u.h>
#include <libc.h>

enum {
	Nblock	= 256*1024,
};

static void
usage(void)
{
	fprint(2, "usage: aux/getdist [-s server] [-v] name\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *server, *addr, buf[256];
	uchar len[4], *b;
	long n, m, tot, shown;
	int fd, verbose;

	server = nil;
	verbose = 0;
	ARGBEGIN{
	case 's':
		server = EARGF(usage());
		break;
	case 'v':
		verbose = 1;
		break;
	default:
		usage();
	}ARGEND
	if(argc != 1)
		usage();
	if(server == nil && (server = getenv("cpu")) == nil)
		sysfatal("no server: -s or $cpu");
	addr = strchr(server, '!') != nil ? server : netmkaddr(server, "tcp", "17050");
	if((fd = dial(addr, nil, nil, nil)) < 0)
		sysfatal("dial %s: %r", addr);
	if(fprint(fd, "%s\n", argv[0]) < 0)
		sysfatal("write: %r");
	b = malloc(Nblock);
	if(b == nil)
		sysfatal("malloc: %r");
	tot = shown = 0;
	for(;;){
		if(readn(fd, len, 4) != 4)
			sysfatal("%s: the connection ended", argv[0]);
		n = (long)len[0]<<24 | len[1]<<16 | len[2]<<8 | len[3];
		if(n == 0)
			break;
		if(n < 0){
			m = read(fd, buf, sizeof buf - 1);
			buf[m > 0 ? m : 0] = 0;
			sysfatal("%s: %s", argv[0], buf);
		}
		if(n > Nblock || readn(fd, b, n) != n)
			sysfatal("%s: a bad block", argv[0]);
		if(write(1, b, n) != n)
			sysfatal("write: %r");
		if(write(fd, "a", 1) != 1)	/* the next */
			sysfatal("write: %r");
		tot += n;
		if(verbose && tot - shown >= 4*1024*1024){
			fprint(2, "%s: %ld MB\n", argv[0], tot/(1024*1024));
			shown = tot;
		}
	}
	if(verbose)
		fprint(2, "%s: %ld bytes\n", argv[0], tot);
	exits(nil);
}
