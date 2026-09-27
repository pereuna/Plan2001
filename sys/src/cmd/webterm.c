/*
 * webterm - a WebSocket to this machine's rcpu or auth service, for
 * drawterm in a browser (Monolith).  aux/listen runs it for each
 * connection (/rc/bin/service/tcp17080), the connection on fd 0 and 1.
 *
 * GET /17019 is rcpu, GET /567 is auth; nothing else.  The WebSocket
 * carries the service's bytes unchanged in binary frames, so drawterm's
 * own auth and TLS run inside it as over TCP.
 *
 * With -w DIR it also serves the page: GET / or /NAME for a file in DIR
 * (no subdirectories), NAME.gz instead when the browser takes gzip, with
 * the COOP/COEP headers the page's threads need; POST /log appends the
 * page's log line (its ?log=1) to /sys/log/monolith.  tlssrv in front of it
 * (/rc/bin/service/tcp17443) makes that https and wss on one origin.
 *
 * With -s (the connection is already TLS, as behind tlssrv) GET /rcpu is
 * rcpu without its own TLS: webterm does what tlssrv -a does for rcpu -
 * p9any as the server, auth_chuid, the rcpu server script - but the
 * authenticated bytes go straight through the WebSocket, not TLS-PSK.
 */
#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>
#include <auth.h>

enum {
	Maxhdr	= 8192,
	Maxmsg	= 1<<20,	/* largest frame taken from the browser */
	Iosize	= 32*1024,
};

static char *services[] = { "17019", "567", nil };
static char *webdir;
static int secure;	/* -s: the connection is TLS already */

/* the rcpu server (/rc/bin/service/tcp17019 runs it after tlssrv -a) */
static char rcpuscript[] = ". <{n=`{read} && ! ~ $#n 0 && read -c $n} >[2=1]";
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

/* the value of header name, or nil; the request is not changed */
static char*
header(char *hdr, char *name)
{
	static char val[Maxhdr];
	char *p, *e;
	int n;

	n = strlen(name);
	for(p = hdr; (p = strstr(p, "\r\n")) != nil; ){
		p += 2;
		if(cistrncmp(p, name, n) == 0 && p[n] == ':'){
			p += n+1;
			while(*p == ' ' || *p == '\t')
				p++;
			if((e = strstr(p, "\r\n")) == nil || e-p >= sizeof val)
				return nil;
			memmove(val, p, e-p);
			val[e-p] = 0;
			return val;
		}
	}
	return nil;
}

static char*
httpdate(long t)
{
	static char *days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	static char buf[64];
	Tm *tm;

	tm = gmtime(t);
	snprint(buf, sizeof buf, "%s, %02d %s %d %02d:%02d:%02d GMT",
		days[tm->wday], tm->mday, months[tm->mon], tm->year+1900,
		tm->hour, tm->min, tm->sec);
	return buf;
}

static char*
mimetype(char *name)
{
	char *e;

	e = strrchr(name, '.');
	if(e == nil)
		return "application/octet-stream";
	if(strcmp(e, ".html") == 0)
		return "text/html; charset=utf-8";
	if(strcmp(e, ".js") == 0)
		return "text/javascript";
	if(strcmp(e, ".wasm") == 0)
		return "application/wasm";
	if(strcmp(e, ".png") == 0)
		return "image/png";
	if(strcmp(e, ".crt") == 0)
		return "application/x-x509-ca-cert";
	return "application/octet-stream";
}

/* an answer without a body, the connection kept */
static void
status(char *s)
{
	fprint(1, "HTTP/1.1 %s\r\nContent-Length: 0\r\n\r\n", s);
}

/* GET of path (the request line's, query cut off) from webdir */
static void
servefile(char *hdr, char *path)
{
	char name[256], file[512], *ae, *enc, *ims, *lm, *cc, buf[Iosize];
	Dir *d;
	int fd;
	long n;

	if(strcmp(path, "/") == 0)
		path = "/index.html";
	path++;
	if(*path == 0 || *path == '.' || strchr(path, '/') != nil || strlen(path) >= sizeof name - 4){
		status("404 Not Found");
		return;
	}
	strcpy(name, path);
	enc = nil;
	fd = -1;
	ae = header(hdr, "Accept-Encoding");
	if(ae != nil && strstr(ae, "gzip") != nil){
		snprint(file, sizeof file, "%s/%s.gz", webdir, name);
		if((fd = open(file, OREAD)) >= 0)
			enc = "gzip";
	}
	if(fd < 0){
		snprint(file, sizeof file, "%s/%s", webdir, name);
		fd = open(file, OREAD);
	}
	if(fd < 0 || (d = dirfstat(fd)) == nil || (d->mode & DMDIR) != 0){
		if(fd >= 0)
			close(fd);
		status("404 Not Found");
		return;
	}
	lm = strdup(httpdate(d->mtime));
	ims = header(hdr, "If-Modified-Since");
	/* the page itself is never kept: a new one must not wait for revalidation */
	cc = strstr(name, ".html") != nil ? "no-store" : "no-cache";
	if(ims != nil && strcmp(ims, lm) == 0 && strcmp(cc, "no-cache") == 0){
		fprint(1, "HTTP/1.1 304 Not Modified\r\nLast-Modified: %s\r\n"
			"Cross-Origin-Opener-Policy: same-origin\r\nCross-Origin-Embedder-Policy: require-corp\r\n"
			"Content-Length: 0\r\n\r\n", lm);
	}else{
		fprint(1, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n%s%s%s"
			"Content-Length: %lld\r\nLast-Modified: %s\r\nCache-Control: %s\r\nVary: Accept-Encoding\r\n"
			"Cross-Origin-Opener-Policy: same-origin\r\nCross-Origin-Embedder-Policy: require-corp\r\n\r\n",
			mimetype(name), enc ? "Content-Encoding: " : "", enc ? enc : "", enc ? "\r\n" : "",
			d->length, lm, cc);
		while((n = read(fd, buf, sizeof buf)) > 0)
			if(write(1, buf, n) != n)
				exits("write");
	}
	free(lm);
	free(d);
	close(fd);
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

/* POST /log: one line of the page's log, to /sys/log/monolith */
static void
logline(char *hdr)
{
	char *cl, buf[4096+1];
	long n;
	int fd;

	cl = header(hdr, "Content-Length");
	n = cl != nil ? atol(cl) : 0;
	if(n < 0 || n > 4096)
		reply("413 Content Too Large");
	if(readn(0, buf, n) != n)
		exits("eof");
	buf[n] = 0;
	for(cl = buf; *cl; cl++)
		if(*cl == '\n' || *cl == '\r')
			*cl = ' ';
	if((fd = open("/sys/log/monolith", OWRITE)) >= 0){
		seek(fd, 0, 2);
		fprint(fd, "%s\n", buf);
		close(fd);
	}
	status("204 No Content");
}

static void
usage(void)
{
	fprint(2, "usage: webterm [-s] [-w webdir]\n");
	exits("usage");
}

/* the handshake's 101 answer */
static void
wsaccept(char *hdr)
{
	char *key, buf[128], accept[64];
	uchar digest[SHA1dlen];

	if((key = header(hdr, "Sec-WebSocket-Key")) == nil)
		reply("400 Bad Request");
	snprint(buf, sizeof buf, "%s%s", key, guid);
	sha1((uchar*)buf, strlen(buf), digest, nil);
	enc64(accept, sizeof accept, digest, SHA1dlen);
	fprint(1, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
		"Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept);
}

/* the WebSocket's frames to and from fd; does not return */
static void
relay(int fd)
{
	int pid;

	switch(pid = rfork(RFPROC|RFFDG|RFMEM)){
	case -1:
		exits("rfork");
	case 0:
		downstream(fd);
		exits(nil);
	}
	upstream(fd);
	postnote(PNPROC, pid, "kill");
	exits(nil);
}

/* the WebSocket to service s; does not return */
static void
websocket(char *hdr, char *s)
{
	int sfd;

	sfd = dial(netmkaddr(sysname(), "tcp", s), nil, nil, nil);
	if(sfd < 0)
		reply("502 Bad Gateway");
	wsaccept(hdr);
	relay(sfd);
}

/*
 * rcpu over the WebSocket without TLS-PSK: a child authenticates on one
 * end of a pipe, becomes the user and runs the rcpu script; the frames
 * go to the other end.  Does not return.
 */
static void
rcpu(char *hdr)
{
	AuthInfo *ai;
	int p[2];

	if(pipe(p) < 0)
		reply("500 Internal Server Error");
	wsaccept(hdr);
	switch(rfork(RFPROC|RFFDG|RFNOTEG)){
	case -1:
		exits("rfork");
	case 0:
		close(p[0]);
		dup(p[1], 0);
		dup(p[1], 1);
		dup(p[1], 2);	/* not the network: tlssrv's TLS is on 0 and 1 only */
		close(p[1]);
		ai = auth_proxy(0, nil, "proto=p9any role=server");
		if(ai == nil)
			exits("auth_proxy");
		if(auth_chuid(ai, nil) < 0)
			exits("auth_chuid");
		auth_freeAI(ai);
		execl("/bin/rc", "rc", "-c", rcpuscript, nil);
		exits("exec");
	}
	close(p[1]);
	relay(p[0]);
}

void
main(int argc, char **argv)
{
	char *hdr, *path, *e, *up, *conn, **s;

	ARGBEGIN{
	case 'w':
		webdir = EARGF(usage());
		break;
	case 's':
		secure = 1;
		break;
	default:
		usage();
	}ARGEND
	if(argc != 0)
		usage();

	/* requests on one connection until it closes or becomes a WebSocket */
	for(;;){
		hdr = readhdr();
		if(webdir != nil && strncmp(hdr, "POST /log ", 10) == 0){
			logline(hdr);
			continue;
		}
		if(strncmp(hdr, "GET /", 5) != 0)
			reply("405 Method Not Allowed");
		path = hdr+4;
		if((e = strchr(path, ' ')) == nil)
			reply("400 Bad Request");
		*e = 0;
		path = strdup(path);
		*e = ' ';
		if((e = strchr(path, '?')) != nil)
			*e = 0;
		up = header(hdr, "Upgrade");
		if(up != nil && cistrcmp(up, "websocket") == 0){
			if(secure && strcmp(path, "/rcpu") == 0)
				rcpu(hdr);
			for(s = services; *s != nil; s++)
				if(strcmp(path+1, *s) == 0)
					websocket(hdr, *s);
			reply("404 Not Found");
		}
		if(webdir == nil)
			reply("404 Not Found");
		servefile(hdr, path);
		free(path);
		conn = header(hdr, "Connection");
		if(conn != nil && cistrcmp(conn, "close") == 0)
			exits(nil);
	}
}
