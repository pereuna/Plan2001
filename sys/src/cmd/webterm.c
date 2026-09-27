/*
 * webterm - a WebSocket to this machine's rcpu or auth service, for
 * drawterm in a browser (Monolith).  aux/listen runs it for each
 * connection (/rc/bin/service/tcp17080), the connection on fd 0 and 1.
 *
 * GET /17019 is rcpu, GET /567 is auth; nothing else.  The WebSocket
 * carries the service's bytes unchanged in binary frames, so drawterm's
 * own auth and TLS run inside it as over TCP.
 */
#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>

enum {
	Maxhdr	= 8192,
	Maxmsg	= 1<<20,	/* largest frame taken from the browser */
	Iosize	= 32*1024,
};

static char *services[] = { "17019", "567", nil };
static char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static void
reply(char *status)
{
	fprint(1, "HTTP/1.1 %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", status);
	exits(status);
}

/* the request up to its blank line, as a string */
static char*
readhdr(void)
{
	static char buf[Maxhdr+1];
	int n;

	for(n = 0; n < Maxhdr; n++){
		if(read(0, buf+n, 1) != 1)
			exits("eof");
		buf[n+1] = 0;
		if(n >= 3 && strcmp(buf+n-3, "\r\n\r\n") == 0)
			return buf;
	}
	reply("431 Request Header Fields Too Large");
	return nil;
}

static char*
header(char *hdr, char *name)
{
	char *p, *e;
	int n;

	n = strlen(name);
	for(p = hdr; (p = strstr(p, "\r\n")) != nil; ){
		p += 2;
		if(cistrncmp(p, name, n) == 0 && p[n] == ':'){
			p += n+1;
			while(*p == ' ' || *p == '\t')
				p++;
			if((e = strstr(p, "\r\n")) == nil)
				return nil;
			*e = 0;
			return p;
		}
	}
	return nil;
}

/* one frame to the browser: unmasked, binary (or op) */
static int
sendframe(int op, uchar *data, long n)
{
	uchar *b;
	int h, r;

	b = malloc(n+10);
	if(b == nil)
		return -1;
	b[0] = 0x80 | op;
	if(n < 126){
		b[1] = n;
		h = 2;
	}else if(n < 65536){
		b[1] = 126;
		b[2] = n>>8;
		b[3] = n;
		h = 4;
	}else{
		b[1] = 127;
		b[2] = b[3] = b[4] = b[5] = 0;
		b[6] = n>>24;
		b[7] = n>>16;
		b[8] = n>>8;
		b[9] = n;
		h = 10;
	}
	memmove(b+h, data, n);
	r = write(1, b, h+n);
	free(b);
	return r == h+n ? 0 : -1;
}

/* service to browser */
static void
downstream(int sfd)
{
	uchar buf[Iosize];
	long n;

	while((n = read(sfd, buf, sizeof buf)) > 0)
		if(sendframe(2, buf, n) < 0)
			break;
	sendframe(8, nil, 0);
}

/* browser to service: masked frames; data, ping, close */
static void
upstream(int sfd)
{
	uchar h[14], *p, *mask;
	vlong n;
	int op, i;

	for(;;){
		if(readn(0, h, 2) != 2)
			return;
		op = h[0] & 0x0F;
		if((h[1] & 0x80) == 0)
			return;		/* clients must mask */
		n = h[1] & 0x7F;
		if(n == 126){
			if(readn(0, h+2, 2) != 2)
				return;
			n = h[2]<<8 | h[3];
		}else if(n == 127){
			if(readn(0, h+2, 8) != 8)
				return;
			n = 0;
			for(i = 2; i < 10; i++)
				n = n<<8 | h[i];
		}
		if(n > Maxmsg)
			return;
		mask = h+10;
		if(readn(0, mask, 4) != 4)
			return;
		p = malloc(n+1);
		if(p == nil)
			return;
		if(readn(0, p, n) != n){
			free(p);
			return;
		}
		for(i = 0; i < n; i++)
			p[i] ^= mask[i%4];
		switch(op){
		case 0:		/* continuation */
		case 1:		/* text */
		case 2:		/* binary */
			if(write(sfd, p, n) != n){
				free(p);
				return;
			}
			break;
		case 8:		/* close */
			free(p);
			return;
		case 9:		/* ping */
			sendframe(10, p, n);
			break;
		}
		free(p);
	}
}

void
main(int argc, char **argv)
{
	char *hdr, *path, *key, *e, **s, buf[128], accept[64];
	uchar digest[SHA1dlen];
	int sfd, pid;

	ARGBEGIN{
	}ARGEND
	USED(argc, argv);

	hdr = readhdr();
	if(strncmp(hdr, "GET /", 5) != 0)
		reply("405 Method Not Allowed");
	path = hdr+5;
	if((e = strchr(path, ' ')) == nil)
		reply("400 Bad Request");
	*e = 0;
	for(s = services; *s != nil; s++)
		if(strcmp(path, *s) == 0)
			break;
	if(*s == nil)
		reply("404 Not Found");
	*e = ' ';
	if((key = header(hdr, "Sec-WebSocket-Key")) == nil)
		reply("400 Bad Request");

	snprint(buf, sizeof buf, "%s%s", key, guid);
	sha1((uchar*)buf, strlen(buf), digest, nil);
	enc64(accept, sizeof accept, digest, SHA1dlen);

	sfd = dial(netmkaddr(sysname(), "tcp", *s), nil, nil, nil);
	if(sfd < 0)
		reply("502 Bad Gateway");
	fprint(1, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
		"Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept);

	switch(pid = rfork(RFPROC|RFFDG|RFMEM)){
	case -1:
		exits("rfork");
	case 0:
		downstream(sfd);
		exits(nil);
	}
	upstream(sfd);
	postnote(PNPROC, pid, "kill");
	exits(nil);
}
