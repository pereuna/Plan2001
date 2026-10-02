#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

/*
 * #I: wasm32's network, /net/tcp over WebSockets (docs/architecture.md,
 * D2).  A browser opens no TCP connections: a connection is a WebSocket
 * to the machine's webterm (sys/src/cmd/webterm.c), which carries a
 * service's bytes unchanged - GET /17019 rcpu, /567 auth - as drawterm's
 * wsock.c does.  The address's host is not used: the WebSocket server is
 * the machine.  The page owns the WebSockets (a Worker waits in
 * Atomics.wait and would not hear them): what comes in it writes into
 * the conversation's ring here, what goes out goes to it as a message.
 *
 *	/net/tcp/clone, /net/tcp/n/{ctl,data,local,remote,status}
 *	ctl: connect host!port, hangup
 *	/net/cs: a connection server, as ndb/cs answers - net or tcp, a
 *	host as it is, a service by the names below or its number
 */
enum
{
	Nconv	= 64,
	Nring	= 64*1024,

	Qtop	= 1,
	Qcs,
	Qtcp,
	Qclone,
	Qconv,
	Qctl,
	Qdata,
	Qlocal,
	Qremote,
	Qstatus,
};

#define	QID(c, t)	(((c)+1)<<8 | (t))
#define	QTYPE(q)	((ulong)(q).path & 0xFF)
#define	QCONV(q)	(((ulong)(q).path >> 8) - 1)

typedef struct Ring Ring;
struct Ring
{
	long	r;
	long	w;		/* the page's: bytes written */
	long	closed;		/* the page's: 1 the WebSocket closed, 2 it failed, 3 the ring was full */
	uchar	b[Nring];
};

typedef struct Conv Conv;
struct Conv
{
	QLock;
	int	ref;		/* open channels */
	int	used;
	long	st;		/* the page's: 0 opening, 1 open, -1 it could not */
	char	raddr[64];
	Ring	*in;
	int	open;		/* a WebSocket, the page's */
};

static	Conv	convs[Nconv];
static	Lock	convlock;

static char *names[] = {
	[Qctl]		"ctl",
	[Qdata]		"data",
	[Qlocal]	"local",
	[Qremote]	"remote",
	[Qstatus]	"status",
};

/* port names wasm32 knows without a cs: /lib/ndb/common's */
static struct {
	char	*name;
	char	*port;
} services[] = {
	"rcpu",		"17019",
	"ticket",	"567",
	"exportfs",	"17007",
};

static int
wsgen(Chan *c, char*, Dirtab*, int, int s, Dir *dp)
{
	Qid q;
	char buf[16];
	int n, t;

	t = QTYPE(c->qid);
	if(s == DEVDOTDOT){
		switch(t){
		case Qtop:
		case Qtcp:
			mkqid(&q, Qtop, 0, QTDIR);
			devdir(c, q, "#I", 0, eve, DMDIR|0555, dp);
			return 1;
		}
		mkqid(&q, Qtcp, 0, QTDIR);
		devdir(c, q, "tcp", 0, eve, DMDIR|0555, dp);
		return 1;
	}
	switch(t){
	case Qtop:
		if(s == 0){
			mkqid(&q, Qtcp, 0, QTDIR);
			devdir(c, q, "tcp", 0, eve, DMDIR|0555, dp);
			return 1;
		}
		if(s > 1)
			return -1;
		mkqid(&q, Qcs, 0, QTFILE);
		devdir(c, q, "cs", 0, eve, 0666, dp);
		return 1;
	case Qtcp:
		if(s == 0){
			mkqid(&q, Qclone, 0, QTFILE);
			devdir(c, q, "clone", 0, eve, 0666, dp);
			return 1;
		}
		n = s-1;
		if(n >= Nconv)
			return -1;
		if(!convs[n].used)
			return 0;
		snprint(buf, sizeof buf, "%d", n);
		mkqid(&q, QID(n, Qconv), 0, QTDIR);
		devdir(c, q, buf, 0, eve, DMDIR|0555, dp);
		return 1;
	default:
		n = QCONV(c->qid);
		if(s >= Qstatus-Qctl+1)
			return -1;
		mkqid(&q, QID(n, Qctl+s), 0, QTFILE);
		devdir(c, q, names[Qctl+s], 0, eve, 0666, dp);
		return 1;
	}
}

static Chan*
wsattach(char *spec)
{
	Chan *c;

	c = devattach('I', spec);
	mkqid(&c->qid, Qtop, 0, QTDIR);
	return c;
}

static Walkqid*
wswalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, wsgen);
}

static int
wsstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, nil, 0, wsgen);
}

static Conv*
newconv(void)
{
	Conv *cv;
	int i;

	lock(&convlock);
	for(i = 0; i < Nconv; i++){
		cv = &convs[i];
		if(!cv->used && cv->ref == 0){
			cv->used = 1;
			cv->ref = 1;
			unlock(&convlock);
			cv->st = 0;
			cv->open = 0;
			cv->raddr[0] = 0;
			if(cv->in == nil)
				cv->in = mallocz(sizeof(Ring), 1);
			if(cv->in == nil){
				cv->used = 0;
				cv->ref = 0;
				error(Enomem);
			}
			memset(cv->in, 0, sizeof(Ring));
			return cv;
		}
	}
	unlock(&convlock);
	error(Enodev);
}

static Chan*
wsopen(Chan *c, int omode)
{
	Conv *cv;
	int t;

	t = QTYPE(c->qid);
	switch(t){
	case Qcs:
		c->aux = nil;
		break;
	case Qclone:
		cv = newconv();
		mkqid(&c->qid, QID(cv-convs, Qctl), 0, QTFILE);
		break;
	case Qctl:
	case Qdata:
	case Qlocal:
	case Qremote:
	case Qstatus:
		cv = &convs[QCONV(c->qid)];
		lock(&convlock);
		if(!cv->used){
			unlock(&convlock);
			error(Ehungup);
		}
		cv->ref++;
		unlock(&convlock);
		break;
	default:
		if(omode != OREAD)
			error(Eperm);
		break;
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
wsclose(Chan *c)
{
	Conv *cv;
	int t;

	t = QTYPE(c->qid);
	if(t == Qcs && (c->flag & COPEN)){
		free(c->aux);
		c->aux = nil;
	}
	if((c->flag & COPEN) == 0 || t < Qctl)
		return;
	cv = &convs[QCONV(c->qid)];
	lock(&convlock);
	if(--cv->ref == 0){
		if(cv->open)
			platnetclose(cv-convs);
		cv->open = 0;
		cv->used = 0;
	}
	unlock(&convlock);
}

/* wait for the word to be other than v, a second at a time: a note interrupts */
static void
netwait(long *w, long v)
{
	while(*w == v){
		if(up->notepending)
			error(Eintr);
		platwait(w, v, 1000);
	}
}

/* a service's port: a name /lib/ndb/common gives, or its number; nil if neither */
static char*
service(char *p)
{
	int i;

	for(i = 0; i < nelem(services); i++)
		if(strcmp(p, services[i].name) == 0)
			return services[i].port;
	for(i = 0; p[i] != 0; i++)
		if(p[i] < '0' || p[i] > '9')
			return nil;
	return i > 0 ? p : nil;
}

/* cs: net!host!service or tcp!host!service, the clone file and the address */
static char*
csquery(char *q)
{
	char *f[3], *p;
	int n;

	n = getfields(q, f, nelem(f), 0, "!");
	if(n != 3 || strcmp(f[0], "net") != 0 && strcmp(f[0], "tcp") != 0)
		error("cs: no translation (only tcp on wasm32)");
	if((p = service(f[2])) == nil)
		error("cs: no such service");
	n = strlen(f[1]) + strlen(p) + 32;
	q = malloc(n);
	if(q == nil)
		error(Enomem);
	snprint(q, n, "/net/tcp/clone %s!%s", f[1], p);
	return q;
}

static void
connect(Conv *cv, char *addr)
{
	char path[32], *p;

	if(cv->open)
		error("already connected");
	if((p = strrchr(addr, '!')) == nil)
		error("bad address: host!port");
	if((p = service(p+1)) == nil)
		error("bad port");
	snprint(path, sizeof path, "/%s", p);
	strecpy(cv->raddr, cv->raddr+sizeof cv->raddr, addr);
	cv->st = 0;
	cv->open = 1;
	platnetopen(cv-convs, path, cv->in, &cv->st);
	netwait(&cv->st, 0);
	if(cv->st < 0){
		cv->open = 0;
		error("connection refused");
	}
}

static long
wsread(Chan *c, void *a, long n, vlong off)
{
	Conv *cv;
	Ring *r;
	char buf[128];
	long m, i, w;

	if(c->qid.type & QTDIR)
		return devdirread(c, a, n, nil, 0, wsgen);
	if(QTYPE(c->qid) == Qcs){
		if(c->aux == nil)
			return 0;
		return readstr(off, a, n, c->aux);
	}
	cv = &convs[QCONV(c->qid)];
	switch(QTYPE(c->qid)){
	case Qctl:
		snprint(buf, sizeof buf, "%ld", (long)(cv-convs));
		return readstr(off, a, n, buf);
	case Qlocal:
		return readstr(off, a, n, "::!0\n");
	case Qremote:
		snprint(buf, sizeof buf, "%s\n", cv->raddr);
		return readstr(off, a, n, buf);
	case Qstatus:
		snprint(buf, sizeof buf, "%s\n", !cv->open ? "Closed" : cv->st == 0 ? "Syn_sent" :
			cv->in->closed ? "Closed" : "Established");
		return readstr(off, a, n, buf);
	case Qdata:
		r = cv->in;
		for(;;){
			w = r->w;
			if(r->r != w)
				break;
			if(r->closed || !cv->open)
				return 0;
			netwait(&r->w, w);
		}
		m = w - r->r;
		if(m > n)
			m = n;
		for(i = 0; i < m; i++)
			((uchar*)a)[i] = r->b[(r->r + i) % Nring];
		coherence();
		r->r += m;
		return m;
	}
	error(Egreg);
	return 0;
}

static long
wswrite(Chan *c, void *a, long n, vlong)
{
	Conv *cv;
	Cmdbuf *cb;

	if(QTYPE(c->qid) == Qcs){
		char q[128];

		if(n >= sizeof q)
			error(Etoobig);
		memmove(q, a, n);
		q[n] = 0;
		free(c->aux);
		c->aux = nil;
		c->aux = csquery(q);
		return n;
	}
	cv = &convs[QCONV(c->qid)];
	switch(QTYPE(c->qid)){
	case Qctl:
		cb = parsecmd(a, n);
		if(waserror()){
			free(cb);
			nexterror();
		}
		if(cb->nf == 2 && strcmp(cb->f[0], "connect") == 0)
			connect(cv, cb->f[1]);
		else if(cb->nf >= 1 && strcmp(cb->f[0], "hangup") == 0){
			if(cv->open)
				platnetclose(cv-convs);
			cv->open = 0;
		}else
			error(Ebadctl);
		poperror();
		free(cb);
		return n;
	case Qdata:
		if(!cv->open || cv->st != 1 || cv->in->closed)
			error(Ehungup);
		platnetsend(cv-convs, a, n);
		return n;
	}
	error(Eperm);
	return 0;
}

Dev wsnetdevtab = {
	'I',
	"wsnet",

	devreset,
	devinit,
	devshutdown,
	wsattach,
	wswalk,
	wsstat,
	wsopen,
	devcreate,
	wsclose,
	wsread,
	devbread,
	wswrite,
	devbwrite,
	devremove,
	devwstat,
};
