/*
 * distd - the wasm32 distribution's parts (tools/dist-parts: base, src,
 * games; tar.gz in dir) for the machines' installer (aux/getdist): a line
 * from the connection names the part; its bytes go in blocks, each its
 * length (4 bytes, big-endian) and its bytes, the next once the machine
 * has sent a byte (a WebSocket has no flow control of its own); a block
 * of 0 ends it, -1 and a line is an error.  aux/listen's
 * service/tcp17050, as none: the parts are public.
 *
 *	aux/distd [-d dir]	default /sys/lib/plan2001/dist
 */
#include <u.h>
#include <libc.h>

enum {
	Nblock	= 256*1024,
};

static void
block(long n)
{
	uchar len[4];

	len[0] = n>>24;
	len[1] = n>>16;
	len[2] = n>>8;
	len[3] = n;
	if(write(1, len, 4) != 4)
		exits("write");
}

static void
fail(char *why)
{
	block(-1);
	fprint(1, "%s\n", why);
	exits(why);
}

void
main(int argc, char **argv)
{
	char *dir, line[64], *p, *path;
	uchar *b, c;
	long n;
	int fd, i;

	dir = "/sys/lib/plan2001/dist";
	ARGBEGIN{
	case 'd':
		dir = EARGF(exits("usage"));
		break;
	}ARGEND

	for(i = 0; i < sizeof line - 1; i++){
		if(read(0, line+i, 1) != 1)
			exits("no line");
		if(line[i] == '\n')
			break;
	}
	line[i] = 0;
	if((p = strchr(line, '\r')) != nil)
		*p = 0;
	if(strcmp(line, "base") != 0 && strcmp(line, "src") != 0 && strcmp(line, "games") != 0)
		fail("no such part");
	path = smprint("%s/%s.tgz", dir, line);
	if((fd = open(path, OREAD)) < 0)
		fail("not here");
	b = malloc(Nblock);
	if(b == nil)
		fail("no memory");
	while((n = readn(fd, b, Nblock)) > 0){
		block(n);
		if(write(1, b, n) != n)
			exits("write");
		if(read(0, &c, 1) != 1)	/* the machine has it */
			exits("hangup");
	}
	block(0);
	exits(nil);
}
