/*
 * webterm - a WebSocket to this machine's rcpu or auth service, for
 * drawterm in a browser (Monolith).  aux/listen runs it for each
 * connection (/rc/bin/service/tcp17080), the connection on fd 0 and 1.
 *
 * GET /17019 is rcpu, GET /567 is auth, GET /17030 is the compute pool
 * (crsrv: a browser tab as a compute resource); nothing else.  The WebSocket
 * carries the service's bytes unchanged in binary frames, so drawterm's
 * own auth and TLS run inside it as over TCP.
 *
 * With -w DIR it also serves the page: GET / or /NAME for a file in DIR
 * (no subdirectories; GET /app/APP is the page too, and /app/NAME.EXT the
 * file, for a tab that runs APP), NAME.gz instead when the browser takes gzip, with
 * the COOP/COEP headers the page's threads need; POST /log appends the
 * page's log line (its ?log=1) to /sys/log/monolith.  tlssrv in front of it
 * (/rc/bin/service/tcp17443) makes that https and wss on one origin.
 *
 * With -s (the connection is already TLS, as behind tlssrv) GET /rcpu is
 * rcpu without its own TLS: webterm does what tlssrv -a does for rcpu -
 * p9any as the server, auth_chuid, the rcpu server script - but the
 * authenticated bytes go straight through the WebSocket, not TLS-PSK.
 * Such a session survives its WebSocket: GET /resume/TOKEN/N (below).
 * GET /rcpu/APP names the session's program (the page's /app/APP; the page
 * runs it).  A process of the user's shows it and whether a page is
 * attached in its args, "APP attached|detached" (ps -a: webterm [APP
 * STATE]; apps lists them): webterm runs as none, whose processes the user cannot see.
 *
 * A browser says where its page came from (Origin); only this server's
 * own https origin (with -w) and the -o origins may open WebSockets or
 * post the log, so no other site can use a visitor's browser to reach
 * rcpu.  A request without Origin is not from a browser page.
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

static char *services[] = { "17019", "567", "17030", nil };	/* rcpu, auth, crsrv */
static char *webdir;
static char *origins[16];	/* -o */
static int norigins;
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
	if(strncmp(path, "/app/", 5) == 0){	/* a tab for an app: the page, its files */
		path += 4;
		if(strchr(path, '.') == nil)
			path = "/index.html";
	}
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

/* one frame to the browser on fd: unmasked, binary (or op) */
static int
sendframeto(int fd, int op, uchar *data, long n)
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
	r = write(fd, b, h+n);
	free(b);
	return r == h+n ? 0 : -1;
}

static int
sendframe(int op, uchar *data, long n)
{
	return sendframeto(1, op, data, n);
}

/* one frame from the browser on fd (masked), unmasked into *pp (malloc'd) */
static int
readframe(int fd, int *op, uchar **pp, long *np)
{
	uchar h[14], *p, *mask;
	vlong n;
	int i;

	if(readn(fd, h, 2) != 2)
		return -1;
	*op = h[0] & 0x0F;
	if((h[1] & 0x80) == 0)
		return -1;		/* clients must mask */
	n = h[1] & 0x7F;
	if(n == 126){
		if(readn(fd, h+2, 2) != 2)
			return -1;
		n = h[2]<<8 | h[3];
	}else if(n == 127){
		if(readn(fd, h+2, 8) != 8)
			return -1;
		n = 0;
		for(i = 2; i < 10; i++)
			n = n<<8 | h[i];
	}
	if(n > Maxmsg)
		return -1;
	mask = h+10;
	if(readn(fd, mask, 4) != 4)
		return -1;
	p = malloc(n+1);
	if(p == nil)
		return -1;
	if(readn(fd, p, n) != n){
		free(p);
		return -1;
	}
	for(i = 0; i < n; i++)
		p[i] ^= mask[i%4];
	p[n] = 0;
	*pp = p;
	*np = n;
	return 0;
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
	uchar *p;
	long n;
	int op;

	while(readframe(0, &op, &p, &n) == 0){
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

/* the request's Origin, if any, is this server's or a -o one */
static int
originok(char *hdr)
{
	char *o, *host, own[256];
	int i;

	if((o = header(hdr, "Origin")) == nil)
		return 1;
	o = strdup(o);
	for(i = 0; i < norigins; i++)
		if(cistrcmp(o, origins[i]) == 0)
			goto ok;
	if(webdir != nil && (host = header(hdr, "Host")) != nil){
		snprint(own, sizeof own, "https://%s", host);
		if(cistrcmp(o, own) == 0)
			goto ok;
	}
	free(o);
	return 0;
ok:
	free(o);
	return 1;
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
	fprint(2, "usage: webterm [-s] [-w webdir] [-o origin]...\n");
	exits("usage");
}

/* the handshake's 101 answer */
static void
wsaccept(char *hdr)
{
	char *key, *v, buf[128], accept[64];
	uchar digest[SHA1dlen];

	/* header() returns one static buffer: the version first, then the key */
	if((v = header(hdr, "Sec-WebSocket-Version")) == nil || strcmp(v, "13") != 0)
		reply("426 Upgrade Required");
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
 * rcpu sessions that outlive their WebSocket (a phone's browser drops
 * its connections in the background).  The session process holds the
 * rcpu end and talks WebSocket frames to one attachment at a time: first
 * the connection that made it, then each GET /resume/TOKEN/N.  The page
 * and the session count the bytes each has received; data frames stay in
 * a buffer until acknowledged, and a new attachment gets again what the
 * other side has not.  Text frames are control:
 *	s TOKEN	(to the page, first) the session's token
 *	r N	(to the page, on attach) the session has N bytes from the page
 *	a N	(both ways) acknowledgement: N bytes received
 *	x	(from the page) end the session
 *	e	(to the page) the session has ended, or there is none
 * An attachment reaches the session through /srv: its ctl pipe,
 * /srv/webterm.HASH (HASH from the token, so a listing of /srv does not
 * give the token), gets "attach TOKEN SRVNAME N", and SRVNAME in /srv is
 * the attachment's own pipe.  A session without an attachment for Keep
 * seconds ends.
 *
 * What the page has not acknowledged is bounded: at Highwater the
 * session stops reading rcpu until acknowledgements bring it below
 * Lowwater, so a page that is away or slow holds rcpu back through its
 * pipe instead of growing the buffer.
 */
enum {
	Keep	= 10*60,
	Ackevery	= 16*1024,
	Highwater	= 8*1024*1024,
	Lowwater	= 4*1024*1024,
};

typedef struct Session Session;
struct Session {
	QLock	lk;
	Rendez	attached;
	Rendez	room;		/* the buffer is below Lowwater again */
	int	full;		/* at Highwater, until below Lowwater */
	QLock	wlk;		/* frames to the attachment */
	char	token[33];
	int	sfd;		/* rcpu */
	int	att;		/* the attachment, -1 none */
	int	gen;
	long	since;		/* without an attachment since */
	uchar	*buf;		/* to the page, unacknowledged: [base, base+nbuf), Highwater */
	vlong	base;
	long	nbuf;
	vlong	rcvd;		/* bytes from the page */
	vlong	acked;		/* rcvd as last acknowledged */
	int	done;
	char	app[33];	/* /rcpu/APP */
	int	st;		/* the state to the user's status process */
};

static Session ses;

/* n bytes at p as lower-case hex into buf */
static void
hex(char *buf, uchar *p, int n)
{
	int i;

	for(i = 0; i < n; i++)
		sprint(buf+2*i, "%02x", p[i]);
}

/* an app's name: what /rcpu/APP may say */
static int
appok(char *a)
{
	int n;

	n = strlen(a);
	return n > 0 && n <= 32 && strspn(a, "abcdefghijklmnopqrstuvwxyz0123456789-") == n;
}

static void
srvname(char *buf, int nbuf, char *token)
{
	uchar d[SHA1dlen];
	char h[17];

	sha1((uchar*)token, strlen(token), d, nil);
	hex(h, d, 8);
	snprint(buf, nbuf, "webterm.%s", h);
}

static int
text(Session *s, int fd, char *fmt, ...)
{
	char buf[128];
	va_list arg;
	int r;

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	qlock(&s->wlk);
	r = sendframeto(fd, 1, (uchar*)buf, strlen(buf));
	qunlock(&s->wlk);
	return r;
}

/* a pipe whose reader is gone (status process, attachment): write fails */
static int
pipenote(void*, char *msg)
{
	return strcmp(msg, "sys: write on closed pipe") == 0;
}

/* the session's state to its status process (below); s->lk held */
static void
label(Session *s)
{
	fprint(s->st, "%s %s", s->app, s->att >= 0 ? "attached" : "detached");
}

/*
 * The status process: the user's, in the rcpu child after auth_chuid.
 * Each line from the session becomes its args; it ends with the session.
 */
static void
statusproc(int fd)
{
	char buf[128], path[40];
	int n, a;

	snprint(path, sizeof path, "/proc/%d/args", getpid());
	while((n = read(fd, buf, sizeof buf - 1)) > 0){
		if((a = open(path, OWRITE)) < 0)
			break;
		write(a, buf, n);
		close(a);
	}
	exits(nil);
}

/* the attachment of generation gen failed or closed */
static void
detach(Session *s, int gen)
{
	qlock(&s->lk);
	if(s->gen == gen && s->att >= 0){
		close(s->att);
		s->att = -1;
		s->since = time(0);
		label(s);
	}
	qunlock(&s->lk);
}

static void
sessionend(Session *s)
{
	s->done = 1;
	close(s->sfd);
	postnote(PNGROUP, getpid(), "kill");
	exits(nil);
}

/* rcpu to the page */
static void
sessiondown(Session *s)
{
	uchar buf[Iosize];
	long n, m;
	int fd, gen, r;

	for(;;){
		/* no more than the buffer can take: rcpu waits for the page */
		qlock(&s->lk);
		if(s->nbuf >= Highwater)
			s->full = 1;
		while(s->full)
			rsleep(&s->room);
		m = Highwater - s->nbuf;
		qunlock(&s->lk);
		if(m > sizeof buf)
			m = sizeof buf;
		if((n = read(s->sfd, buf, m)) <= 0)
			break;
		qlock(&s->lk);
		memmove(s->buf+s->nbuf, buf, n);
		s->nbuf += n;
		fd = s->att;
		gen = s->gen;
		qunlock(&s->lk);
		if(fd >= 0){
			qlock(&s->wlk);
			r = sendframeto(fd, 2, buf, n);
			qunlock(&s->wlk);
			if(r < 0)
				detach(s, gen);
		}
	}
	qlock(&s->lk);
	if(s->att >= 0){
		qlock(&s->wlk);
		sendframeto(s->att, 1, (uchar*)"e", 1);
		sendframeto(s->att, 8, nil, 0);
		qunlock(&s->wlk);
	}
	qunlock(&s->lk);
	sessionend(s);
}

/* drop what the page has acknowledged */
static void
trim(Session *s, vlong n)
{
	long d;

	if(n <= s->base || n > s->base + s->nbuf)
		return;
	d = n - s->base;
	memmove(s->buf, s->buf+d, s->nbuf-d);
	s->nbuf -= d;
	s->base = n;
	if(s->full && s->nbuf < Lowwater){
		s->full = 0;
		rwakeup(&s->room);
	}
}

/* the page to rcpu, from whichever attachment there is */
static void
sessionup(Session *s)
{
	uchar *p;
	long n;
	int fd, gen, op, ack;

	for(;;){
		qlock(&s->lk);
		while(s->att < 0)
			rsleep(&s->attached);
		fd = s->att;
		gen = s->gen;
		qunlock(&s->lk);
		while(readframe(fd, &op, &p, &n) == 0){
			switch(op){
			case 0:
			case 2:
				if(write(s->sfd, p, n) != n)
					sessionend(s);
				qlock(&s->lk);
				s->rcvd += n;
				ack = s->rcvd - s->acked >= Ackevery;
				if(ack)
					s->acked = s->rcvd;
				qunlock(&s->lk);
				if(ack)
					text(s, fd, "a %lld", s->acked);
				break;
			case 1:
				if(p[0] == 'a' && p[1] == ' '){
					qlock(&s->lk);
					trim(s, strtoll((char*)p+2, nil, 10));
					qunlock(&s->lk);
				}else if(p[0] == 'x')
					sessionend(s);
				break;
			case 9:
				qlock(&s->wlk);
				sendframeto(fd, 10, p, n);
				qunlock(&s->wlk);
				break;
			}
			free(p);
			if(op == 8)
				break;
		}
		detach(s, gen);
	}
}

/* attachments: "attach TOKEN SRVNAME N" on the ctl pipe */
static void
sessionctl(Session *s, int ctl)
{
	char buf[256], path[128], *f[5];
	vlong have;
	int n, fd, first;

	while((n = read(ctl, buf, sizeof buf - 1)) > 0){
		buf[n] = 0;
		if(tokenize(buf, f, nelem(f)) != 4 || strcmp(f[0], "attach") != 0
		|| strcmp(f[1], s->token) != 0 || strchr(f[2], '/') != nil)
			continue;
		snprint(path, sizeof path, "/srv/%s", f[2]);
		fd = open(path, ORDWR);
		remove(path);
		if(fd < 0)
			continue;
		have = strtoll(f[3], nil, 10);
		qlock(&s->lk);
		if(have < s->base || have > s->base + s->nbuf){
			qunlock(&s->lk);
			close(fd);		/* cannot continue from there */
			continue;
		}
		trim(s, have);
		if(s->att >= 0)
			close(s->att);	/* the old one's splice ends */
		s->att = fd;
		s->gen++;
		first = s->gen == 1;
		qlock(&s->wlk);
		if(first){
			snprint(buf, sizeof buf, "s %s", s->token);
			sendframeto(fd, 1, (uchar*)buf, strlen(buf));
		}
		snprint(buf, sizeof buf, "r %lld", s->rcvd);
		sendframeto(fd, 1, (uchar*)buf, strlen(buf));
		if(s->nbuf > 0)
			sendframeto(fd, 2, s->buf, s->nbuf);
		qunlock(&s->wlk);
		label(s);
		rwakeup(&s->attached);
		qunlock(&s->lk);
	}
}

/* acknowledgements now and then; the end of an unattached session */
static void
sessiontimer(Session *s)
{
	int fd;
	vlong n;

	for(;;){
		sleep(2000);
		qlock(&s->lk);
		if(s->att < 0 && time(0) - s->since > Keep){
			qunlock(&s->lk);
			sessionend(s);
		}
		fd = -1;
		n = 0;
		if(s->att >= 0 && s->rcvd != s->acked){
			s->acked = n = s->rcvd;
			fd = s->att;
		}
		qunlock(&s->lk);
		if(fd >= 0)
			text(s, fd, "a %lld", n);
	}
}

/* the page's side: fd 0 and 1 to and from pipe end fd; does not return */
static void
splice(int fd)
{
	char buf[Iosize];
	long n;
	int pid, parent;

	parent = getpid();
	switch(pid = rfork(RFPROC|RFFDG|RFMEM)){
	case -1:
		exits("rfork");
	case 0:
		/* the session is done with us: end the other half too, which
		   may be waiting on a connection that is gone */
		while((n = read(fd, buf, sizeof buf)) > 0)
			if(write(1, buf, n) != n)
				break;
		postnote(PNPROC, parent, "kill");
		exits(nil);
	}
	while((n = read(0, buf, sizeof buf)) > 0)
		if(write(fd, buf, n) != n)
			break;
	postnote(PNPROC, pid, "kill");
	exits(nil);
}

/* attach this connection to the session with token, having n bytes of it */
static void
attach(char *token, vlong n)
{
	char name[64], att[80];
	int ctl, p[2], fd;

	srvname(name, sizeof name, token);
	snprint(att, sizeof att, "/srv/%s", name);
	if((ctl = open(att, OWRITE)) < 0){
		sendframe(1, (uchar*)"e", 1);
		sendframe(8, nil, 0);
		exits("no session");
	}
	if(pipe(p) < 0)
		exits("pipe");
	snprint(att, sizeof att, "/srv/%s.%d", name, getpid());
	if((fd = create(att, OWRITE, 0600)) < 0)
		exits("srv");
	fprint(fd, "%d", p[1]);
	close(fd);
	close(p[1]);
	fprint(ctl, "attach %s %s.%d %lld", token, name, getpid(), n);
	close(ctl);
	splice(p[0]);
}

/* GET /resume/TOKEN/N: attach to that session; does not return */
static void
resume(char *hdr, char *arg)
{
	char *f[3], *tok;
	int i;

	if(getfields(arg, f, nelem(f), 1, "/") != 2 || strlen(f[0]) != 32)
		reply("404 Not Found");
	tok = f[0];
	for(i = 0; i < 32; i++)
		if(strchr("0123456789abcdef", tok[i]) == nil)
			reply("404 Not Found");
	wsaccept(hdr);
	attach(tok, strtoll(f[1], nil, 10));
}

/*
 * rcpu over the WebSocket without TLS-PSK: a child authenticates on one
 * end of a pipe, becomes the user and runs the rcpu script; a session
 * holds the other end, and this connection is its first attachment.
 * Does not return.
 */
static void
rcpu(char *hdr, char *app)
{
	AuthInfo *ai;
	uchar rnd[16];
	char name[64], path[80];
	int p[2], c[2], st[2], fd;

	if(pipe(p) < 0 || pipe(c) < 0 || pipe(st) < 0)
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
		close(st[0]);
		switch(rfork(RFPROC|RFFDG|RFNOTEG)){
		case 0:
			close(0);
			close(1);
			close(2);
			statusproc(st[1]);
		}
		close(st[1]);
		execl("/bin/rc", "rc", "-c", rcpuscript, nil);
		exits("exec");
	}
	close(p[1]);
	close(st[1]);
	ses.st = st[0];

	genrandom(rnd, sizeof rnd);
	hex(ses.token, rnd, sizeof rnd);
	srvname(name, sizeof name, ses.token);
	snprint(path, sizeof path, "/srv/%s", name);
	if((fd = create(path, OWRITE|ORCLOSE, 0600)) < 0)
		exits("srv");
	fprint(fd, "%d", c[1]);
	close(c[1]);
	ses.sfd = p[0];
	strecpy(ses.app, ses.app+sizeof ses.app, app);
	ses.att = -1;
	ses.since = time(0);
	ses.attached.l = &ses.lk;
	ses.room.l = &ses.lk;
	if((ses.buf = malloc(Highwater)) == nil)
		exits("no memory");

	switch(rfork(RFPROC|RFFDG|RFNOTEG)){
	case -1:
		exits("rfork");
	case 0:
		/* the session: rcpu, ctl and its srv entry; not the network */
		close(0);
		close(1);
		open("/dev/null", OREAD);
		open("/dev/null", OWRITE);
		atnotify(pipenote, 1);
		qlock(&ses.lk);
		label(&ses);
		qunlock(&ses.lk);
		if(rfork(RFPROC|RFMEM) == 0)
			sessiondown(&ses);
		if(rfork(RFPROC|RFMEM) == 0)
			sessionup(&ses);
		if(rfork(RFPROC|RFMEM) == 0)
			sessiontimer(&ses);
		sessionctl(&ses, c[0]);
		sessionend(&ses);
	}
	close(p[0]);
	close(c[0]);
	close(st[0]);
	close(fd);
	attach(ses.token, 0);
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
	case 'o':
		if(norigins == nelem(origins))
			sysfatal("too many -o");
		origins[norigins++] = EARGF(usage());
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
			if(!originok(hdr))
				reply("403 Forbidden");
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
			if(!originok(hdr))
				reply("403 Forbidden");
			if(secure && strcmp(path, "/rcpu") == 0)
				rcpu(hdr, "term");
			if(secure && strncmp(path, "/rcpu/", 6) == 0 && appok(path+6))
				rcpu(hdr, path+6);
			if(secure && strncmp(path, "/resume/", 8) == 0)
				resume(hdr, path+8);
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
