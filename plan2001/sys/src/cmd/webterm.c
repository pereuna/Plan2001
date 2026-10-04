/*
 * webterm - a WebSocket to this machine's rcpu or auth service, for
 * drawterm in a browser (Monolith).  aux/listen runs it for each
 * connection (/rc/bin/service/tcp17080), the connection on fd 0 and 1.
 *
 * GET /17019 is rcpu, GET /567 is auth, GET /5356 secstore; with -s GET /17030 is the compute
 * pool too (crsrv: a browser tab as a compute resource); nothing else.  The WebSocket
 * carries the service's bytes unchanged in binary frames, so drawterm's
 * own auth and TLS run inside it as over TCP.
 *
 * It serves no pages (docs/architecture.md, D7): on https, port 17443,
 * 9front's rc-httpd does - the wasm32 machine's page, its firmware and
 * root - and hands it the WebSocket requests (Plan2001's select-handler,
 * /rc/bin/rc-httpd/select-handler): with -r the request is rc-httpd's,
 * already read, from its $request and $reqlines.  tlssrv in front
 * (/rc/bin/service/tcp17443) makes that https and wss on one origin.
 *
 * With -s (the connection is already TLS, as behind tlssrv) GET /rcpu is
 * rcpu without its own TLS: webterm does what tlssrv -a does for rcpu -
 * p9any as the server, auth_chuid, the rcpu server script - but the
 * authenticated bytes go straight through the WebSocket, not TLS-PSK.
 * Such a session survives its WebSocket: GET /resume/TOKEN/N (below).
 *
 * With -s the origin is the app (docs/app-origins.md): Host APP.MACHINE
 * names /lib/app/APP (else the app is term), whose policy says what its
 * WebSockets may open (rcpu: /rcpu, /resume, /567; cr: /17030) and whose
 * processes may use the compute pool (compute: $computepool 1 for the
 * app's namespace file, which mounts it on /compute), and whose
 * image the rcpu session runs, after its namespace file - webterm's own
 * script, not the client's, which is read and dropped.  A session keeps
 * its origin, and only that origin may resume it.  A process of the
 * user's shows the app and whether a page is attached in its args, "APP
 * attached|detached" (ps -a: webterm [APP STATE]; apps lists them):
 * webterm runs as none, whose processes the user cannot see.
 *
 * The policy words signup and login are GET /17040 and /17041, signupd
 * and passkeyd (aux/listen's service.auth): Plan2001's accounts and their
 * passkeys (docs/webauthn.md).
 *
 * The policy word secstore is GET /5356, the secstore server: factotum's
 * keys for the wasm32 machine (its /boot/init, docs/architecture.md
 * "Avainten paikka"), secstored on this machine.
 *
 * The policy word cpu is rcpu as rcpu itself: GET /17019, the client's
 * own script inside its TLS - the wasm32 terminal's rcpu (term.MACHINE,
 * whose user runs what she likes anyway); an app's origin has only its
 * own session, /rcpu.
 *
 * A browser says where its page came from (Origin); only this server's
 * own https origin (with -s) and the -o origins may open WebSockets, so
 * no other site can use a visitor's browser to reach rcpu.  A request
 * without Origin is not from a browser page.
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

static char *services[] = { "17019", "567", "5356", "17040", "17041", nil };	/* without -s: rcpu, auth, secstore, signupd, passkeyd */
static char *origins[16];	/* -o */
static int norigins;
static int secure;	/* -s: the connection is TLS already */
static int fromhttpd;	/* -r: the request is rc-httpd's ($request, $reqlines) */

/*
 * The rcpu session of an app: the client's script (rcpu's, from the wasm32
 * machine's /boot/app) is read and dropped; the terminal is mounted as
 * rcpu's server script would, and the app's namespace file and image run,
 * as the user (rc -l: the profile).  Its end is rcpu's server's
 * (/rc/bin/rcpu: fn server): the client's interrupt and hangup through
 * its cpunote, the app's status back, and its "lost connection" taken
 * away.
 */
#define RCPUEND \
	"mainproc=$apid\n" \
	"rm -f /mnt/term/env/rfailed\n" \
	"noteproc=()\n" \
	"if(test -d /mnt/term/mnt/cpunote){\n" \
	"	{cat; echo -n hangup} </mnt/term/mnt/cpunote/data >/proc/$mainproc/notepg &\n" \
	"	noteproc=$apid\n" \
	"}\n" \
	"wait $mainproc\n" \
	"echo -n $status >/mnt/term/env/rstatus >[2]/dev/null\n" \
	"~ $#noteproc 0 || echo -n hangup >/proc/$noteproc/notepg\n" \
	"echo -n hangup >/proc/$pid/notepg\n"
static char appscript[] =
	"n=`{read} && ! ~ $#n 0 && read -c $n >/dev/null || exit\n"
	"mount -nc /fd/0 /mnt/term || exit\n"
	"bind -q /mnt/term/dev/cons /dev/cons\n"
	"if(test -r /mnt/term/dev/kbd){\n"
	"	</dev/cons >/dev/cons >[2=1] aux/kbdfs -dq -m /mnt/term/dev\n"
	"	bind -q /mnt/term/dev/cons /dev/cons\n"
	"}\n"
	"</dev/cons >/dev/cons >[2=1] service=cpu app=%s computepool=%d rc -lc '. /lib/app/$app/namespace; exec /lib/app/$app/image' &\n"
	RCPUEND;
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

/*
 * The request rc-httpd has read (-r): its request line and header lines,
 * one to a line, back into a request as readhdr makes one
 */
static char*
httpdhdr(void)
{
	char *req, *lines, *p, *e, *buf;
	int n;

	req = getenv("request");
	lines = getenv("reqlines");
	if(req == nil || lines == nil)
		reply("400 Bad Request");
	buf = malloc(strlen(req) + 2*strlen(lines) + 8);
	if(buf == nil)
		reply("500 Internal Server Error");
	n = sprint(buf, "%s\r\n", req);
	for(p = lines; *p != 0; p = e+1){
		if((e = strchr(p, '\n')) == nil)
			e = p + strlen(p);
		if(e > p){
			memmove(buf+n, p, e-p);
			n += e-p;
			strcpy(buf+n, "\r\n");
			n += 2;
		}
		if(*e == 0)
			break;
	}
	strcpy(buf+n, "\r\n");
	if(n + 2 > Maxhdr)
		reply("431 Request Header Fields Too Large");
	free(req);
	free(lines);
	return buf;
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
	if(secure && (host = header(hdr, "Host")) != nil){
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

static void
usage(void)
{
	fprint(2, "usage: webterm [-s] [-r] [-o origin]...\n");
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
	Authwait	= 30,		/* seconds for p9any: a session that has not authenticated by then ends */
	Maxsessions	= 32,		/* sessions on this machine at once, authenticated or not */
	Lowwater	= 4*1024*1024,
};

typedef struct Session Session;
struct Session {
	QLock	lk;
	Rendez	attached;
	Rendez	room;		/* the buffer is below Lowwater again */
	int	full;		/* at Highwater, until below Lowwater */
	QLock	wlk;		/* frames to the attachment */
	QLock	uplk;		/* a frame from the page into rcpu, and rcvd with it, against an attach (the review's) */
	char	token[33];
	int	sfd;		/* rcpu */
	int	att;		/* the attachment, -1 none */
	int	gen;
	long	since;		/* without an attachment since */
	uchar	*buf;		/* to the page, unacknowledged: [base, base+nbuf), at most Highwater */
	long	cap;		/* buf's size: it grows as rcpu sends, not up front (the review's) */
	int	authed;		/* p9any done: the rcpu child said so */
	long	start;
	vlong	base;
	long	nbuf;
	vlong	rcvd;		/* bytes from the page */
	vlong	acked;		/* rcvd as last acknowledged */
	int	done;
	char	app[33];	/* its origin's app */
	char	*origin;	/* the page's Origin ("" without): only it may resume */
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

/* an app's name */
static int
appok(char *a)
{
	int n;

	n = strlen(a);
	return n > 0 && n <= 32 && strspn(a, "abcdefghijklmnopqrstuvwxyz0123456789-") == n;
}

/* the app of a request: Host APP.MACHINE[:PORT] with /lib/app/APP, else term */
static char*
hostapp(char *hdr)
{
	static char app[40];
	char *h, *e, path[64];
	Dir *d;

	strcpy(app, "term");
	if((h = header(hdr, "Host")) == nil || (e = strchr(h, '.')) == nil || e - h >= sizeof app)
		return app;
	memmove(app, h, e - h);
	app[e - h] = 0;
	snprint(path, sizeof path, "/lib/app/%s", app);
	if(!appok(app) || (d = dirstat(path)) == nil || (d->mode & DMDIR) == 0)
		strcpy(app, "term");
	else
		free(d);
	return app;
}

/* whether app's policy (/lib/app/APP/policy: words) has service */
static int
allowed(char *app, char *service)
{
	char path[64], buf[512], *f[32];
	int fd, n, i;

	snprint(path, sizeof path, "/lib/app/%s/policy", app);
	if((fd = open(path, OREAD)) < 0)
		return 0;
	n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if(n <= 0)
		return 0;
	buf[n] = 0;
	n = tokenize(buf, f, nelem(f));
	for(i = 0; i < n; i++)
		if(strcmp(f[i], service) == 0)
			return 1;
	return 0;
}

/*
 * One of Maxsessions slots for a new session: /srv/webterm.slot.N, made
 * only if it is not there (devsrv's create looks and makes under one
 * lock), so taking one is atomic - a count of /srv and a create later was
 * not (the review's: connections at once all saw room).  The slot goes
 * when the session's last process does (ORCLOSE, its fd in each of them).
 * -1: none free.
 */
static int
slot(void)
{
	char path[40];
	int i, fd;

	for(i = 0; i < Maxsessions; i++){
		snprint(path, sizeof path, "/srv/webterm.slot.%d", i);
		if((fd = create(path, OWRITE|ORCLOSE, 0600)) >= 0)
			return fd;
	}
	return -1;
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
		if(s->nbuf + n > s->cap){
			m = s->cap ? 2*s->cap : 64*1024;
			while(m < s->nbuf + n)
				m *= 2;
			if(m > Highwater)
				m = Highwater;
			if((s->buf = realloc(s->buf, m)) == nil){
				qunlock(&s->lk);
				break;
			}
			s->cap = m;
		}
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
				/*
				 * the frame goes into rcpu and into rcvd together, and only
				 * while this attachment is the session's: an attach takes
				 * uplk before it says "r rcvd", so a frame is in that count
				 * or the page sends it again - never both (the review's:
				 * the old attachment's frame went in after "r N")
				 */
				qlock(&s->uplk);
				if(s->gen != gen){
					qunlock(&s->uplk);
					free(p);
					goto replaced;
				}
				if(write(s->sfd, p, n) != n)
					sessionend(s);
				qlock(&s->lk);
				s->rcvd += n;
				ack = s->rcvd - s->acked >= Ackevery;
				if(ack)
					s->acked = s->rcvd;
				qunlock(&s->lk);
				qunlock(&s->uplk);
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
	replaced:
		detach(s, gen);
	}
}

/* attachments: "attach TOKEN SRVNAME N ORIGIN" on the ctl pipe (ORIGIN quoted) */
static void
sessionctl(Session *s, int ctl)
{
	char buf[512], path[128], *f[6];
	vlong have;
	int n, fd, first;

	while((n = read(ctl, buf, sizeof buf - 1)) > 0){
		buf[n] = 0;
		if(tokenize(buf, f, nelem(f)) != 5 || strcmp(f[0], "attach") != 0
		|| strcmp(f[1], s->token) != 0 || strchr(f[2], '/') != nil)
			continue;
		if(strcmp(f[4], s->origin) != 0){
			/* another origin with this session's token: no such session */
			snprint(path, sizeof path, "/srv/%s", f[2]);
			fd = open(path, ORDWR);
			remove(path);
			if(fd >= 0){
				sendframeto(fd, 1, (uchar*)"e", 1);
				sendframeto(fd, 8, nil, 0);
				close(fd);
			}
			continue;
		}
		snprint(path, sizeof path, "/srv/%s", f[2]);
		fd = open(path, ORDWR);
		remove(path);
		if(fd < 0)
			continue;
		have = strtoll(f[3], nil, 10);
		qlock(&s->uplk);	/* no frame of the old attachment half in: rcvd is what rcpu has */
		qlock(&s->lk);
		if(have < s->base || have > s->base + s->nbuf){
			qunlock(&s->lk);
			qunlock(&s->uplk);
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
		qunlock(&s->uplk);
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
		if(s->att < 0 && time(0) - s->since > Keep
		|| !s->authed && time(0) - s->start > Authwait){
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

/* attach this connection, from origin, to the session with token, having n bytes of it */
static void
attach(char *token, vlong n, char *origin)
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
	fprint(ctl, "attach %s %s.%d %lld %q", token, name, getpid(), n, origin);
	close(ctl);
	splice(p[0]);
}

/* GET /resume/TOKEN/N: attach to that session; does not return */
static void
resume(char *hdr, char *arg)
{
	char *f[3], *tok, *o;
	int i;

	if(getfields(arg, f, nelem(f), 1, "/") != 2 || strlen(f[0]) != 32)
		reply("404 Not Found");
	tok = f[0];
	for(i = 0; i < 32; i++)
		if(strchr("0123456789abcdef", tok[i]) == nil)
			reply("404 Not Found");
	o = header(hdr, "Origin");
	o = strdup(o != nil ? o : "");
	wsaccept(hdr);
	attach(tok, strtoll(f[1], nil, 10), o);
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
	char name[64], path[80], *origin, *script;
	int p[2], c[2], st[2], ap[2], fd, sl;

	if((sl = slot()) < 0)
		reply("503 Service Unavailable");
	USED(sl);	/* held, by this process and the session's, until they are gone */
	if(pipe(p) < 0 || pipe(c) < 0 || pipe(st) < 0 || pipe(ap) < 0)
		reply("500 Internal Server Error");
	origin = header(hdr, "Origin");
	origin = strdup(origin != nil ? origin : "");
	/* computepool: the app may use the pool (policy compute: a compute consumer) */
	script = smprint(appscript, app, allowed(app, "compute"));
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
		close(ap[0]);
		ai = auth_proxy(0, nil, "proto=p9any role=server");
		if(ai == nil)
			exits("auth_proxy");
		if(auth_chuid(ai, nil) < 0)
			exits("auth_chuid");
		auth_freeAI(ai);
		write(ap[1], "a", 1);	/* to the session: authenticated (its Authwait ends) */
		close(ap[1]);
		close(st[0]);
		switch(rfork(RFPROC|RFFDG|RFNOTEG)){
		case 0:
			close(0);
			close(1);
			close(2);
			statusproc(st[1]);
		}
		close(st[1]);
		execl("/bin/rc", "rc", "-c", script, nil);
		exits("exec");
	}
	close(p[1]);
	close(st[1]);
	close(ap[1]);
	ses.st = st[0];
	ses.start = time(0);

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
	ses.origin = origin;
	ses.att = -1;
	ses.since = time(0);
	ses.attached.l = &ses.lk;
	ses.room.l = &ses.lk;

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
		if(rfork(RFPROC|RFMEM) == 0){
			/* the rcpu child's word: authenticated, or gone without (then the session goes) */
			if(read(ap[0], name, 1) == 1){
				ses.authed = 1;
				exits(nil);
			}
			sessionend(&ses);
		}
		sessionctl(&ses, c[0]);
		sessionend(&ses);
	}
	close(p[0]);
	close(c[0]);
	close(st[0]);
	close(ap[0]);
	close(fd);
	attach(ses.token, 0, origin);
}

void
main(int argc, char **argv)
{
	char *hdr, *path, *e, *up, **s, *app;

	quotefmtinstall();
	ARGBEGIN{
	case 's':
		secure = 1;
		break;
	case 'r':
		fromhttpd = 1;
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

	/* one request: a WebSocket (rc-httpd serves everything else) */
	{
		hdr = fromhttpd ? httpdhdr() : readhdr();
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
			if(secure){
				/* the origin's app and what its policy allows */
				app = strdup(hostapp(hdr));
				/* the sessions are a browser page's: no page, no session */
				if((strcmp(path, "/rcpu") == 0 || strncmp(path, "/resume/", 8) == 0) && header(hdr, "Origin") == nil)
					reply("403 Forbidden");
				if(strcmp(path, "/rcpu") == 0 && allowed(app, "rcpu"))
					rcpu(hdr, app);
				if(strncmp(path, "/resume/", 8) == 0 && allowed(app, "rcpu"))
					resume(hdr, path+8);
				if(strcmp(path, "/17019") == 0 && allowed(app, "cpu"))
					websocket(hdr, path+1);
				if(strcmp(path, "/567") == 0 && allowed(app, "rcpu")
				|| strcmp(path, "/17030") == 0 && allowed(app, "cr")
				|| strcmp(path, "/5356") == 0 && allowed(app, "secstore")
				|| strcmp(path, "/17040") == 0 && allowed(app, "signup")
				|| strcmp(path, "/17041") == 0 && allowed(app, "login"))
					websocket(hdr, path+1);
				reply("403 Forbidden");
			}
			for(s = services; *s != nil; s++)
				if(strcmp(path+1, *s) == 0)
					websocket(hdr, *s);
			reply("404 Not Found");
		}
		reply("404 Not Found");
	}
}
