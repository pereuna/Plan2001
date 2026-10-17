/*
 * Plan2001: what gefs has from libc and from its main.c, in the kernel
 * (gefs.h).  gefsserve starts it on a device as kprocs (main.c's
 * procs: the mutator, the readers, the sweeper, the syncers, the
 * tasks and the console) and gives the kernel the other end of the
 * pipe it serves 9P on, for mntattach.  One gefs at a time.
 */
#include "gefs.h"

/* the kernel's, below: its spin locks, sleep, error and Chan I/O */
#undef	Lock
#undef	lock
#undef	unlock
#undef	canlock
#undef	error
#undef	sleep
#undef	open
#undef	close
#undef	read
#undef	write

Gefs	*fs;
int	debug;
int	permissive;
int	usereserve;
char	*reamuser;
int	noneid		= 0;
int	nogroupid	= 9999;
int	admid		= -1;
Blk	*blkbuf;

enum {
	Nfd	= 16,
	Nmsgerr	= 256,
};

/*
 * libc's atomics: the read a load, the rest with one of Nalock spin
 * locks (by the address), which are barriers too
 */
enum {
	Nalock	= 64,
};
static Lock alock[Nalock];

#define	ALOCK(p)	(&alock[((uintptr)(p)>>3) % Nalock])

long
agetl(Along *a)
{
	return *(volatile long*)&a->v;
}

vlong
agetv(Avlong *a)
{
	return *(volatile vlong*)&a->v;
}

void*
agetp(Aptr *a)
{
	return *(void* volatile*)&a->v;
}

long
aswapl(Along *a, long v)
{
	Lock *l;
	long o;

	l = ALOCK(a);
	lock(l);
	o = a->v;
	a->v = v;
	unlock(l);
	return o;
}

vlong
aswapv(Avlong *a, vlong v)
{
	Lock *l;
	vlong o;

	l = ALOCK(a);
	lock(l);
	o = a->v;
	a->v = v;
	unlock(l);
	return o;
}

void*
aswapp(Aptr *a, void *v)
{
	Lock *l;
	void *o;

	l = ALOCK(a);
	lock(l);
	o = a->v;
	a->v = v;
	unlock(l);
	return o;
}

/* the new value, as libc's */
long
aincl(Along *a, long d)
{
	Lock *l;
	long v;

	l = ALOCK(a);
	lock(l);
	v = a->v += d;
	unlock(l);
	return v;
}

vlong
aincv(Avlong *a, vlong d)
{
	Lock *l;
	vlong v;

	l = ALOCK(a);
	lock(l);
	v = a->v += d;
	unlock(l);
	return v;
}

int
acasl(Along *a, long o, long n)
{
	Lock *l;
	int r;

	l = ALOCK(a);
	lock(l);
	r = a->v == o;
	if(r)
		a->v = n;
	unlock(l);
	return r;
}

int
acasv(Avlong *a, vlong o, vlong n)
{
	Lock *l;
	int r;

	l = ALOCK(a);
	lock(l);
	r = a->v == o;
	if(r)
		a->v = n;
	unlock(l);
	return r;
}

int
acasp(Aptr *a, void *o, void *n)
{
	Lock *l;
	int r;

	l = ALOCK(a);
	lock(l);
	r = a->v == o;
	if(r)
		a->v = n;
	unlock(l);
	return r;
}

/*
 * gefs's file descriptors: the device (a partition of it: off and
 * size), the connection and the console, each a Chan of the kernel's
 */
typedef struct Gfd Gfd;
struct Gfd {
	Kchan	*c;
	char	*name;	/* gopen's, for the device */
	vlong	off;
	vlong	size;
};

static struct {
	QLock;
	Gfd	fd[Nfd];
	char	*raw;	/* the disk's raw file, for its cache flush */
	int	flushwarned;
	ulong	nflush;
	char	*srv;	/* the console's srv file */
	int	halting;
	int	halted;
} g;

/*
 * libc's %r is an errstr system call: in the kernel it is up->errstr,
 * put into the format as text (as ext4glue.c's werrstr)
 */
static void
vfmtr(char *buf, int nbuf, char *fmt, va_list arg)
{
	char nfmt[ERRMAX+256], *p, *f, *e, *s;

	p = fmt;
	f = nfmt;
	e = nfmt + sizeof nfmt - 1;
	while(*p != 0 && f < e){
		if(p[0] == '%' && p[1] == 'r'){
			for(s = up->errstr; *s != 0 && f < e-1; s++){
				if(*s == '%')
					*f++ = '%';
				*f++ = *s;
			}
			p += 2;
			continue;
		}
		if(p[0] == '%' && p[1] == '%' && f < e-1){
			*f++ = *p++;
			*f++ = *p++;
			continue;
		}
		*f++ = *p++;
	}
	*f = 0;
	vsnprint(buf, nbuf, nfmt, arg);
}

_Noreturn void
gerror(char *fmt, ...)
{
	char buf[ERRMAX];
	va_list arg;

	va_start(arg, fmt);
	vfmtr(buf, sizeof buf, fmt, arg);
	va_end(arg);
	error(buf);
}

_Noreturn void
broke(char *fmt, ...)
{
	char buf[ERRMAX];
	va_list arg;

	aincl(&fs->rdonly, 1);
	va_start(arg, fmt);
	vfmtr(buf, sizeof buf, fmt, arg);
	va_end(arg);
	print("gefs: %s\n", buf);
	error(buf);
}

_Noreturn void
gsysfatal(char *fmt, ...)
{
	char buf[ERRMAX];
	va_list arg;

	va_start(arg, fmt);
	vfmtr(buf, sizeof buf, fmt, arg);
	va_end(arg);
	panic("gefs: %s", buf);
}

_Noreturn void
_babort(vlong addr, char *msg)
{
	panic("gefs: assert failed for [%llx]: %s", addr, msg);
}

void
gwerrstr(char *fmt, ...)
{
	char buf[ERRMAX];
	va_list arg;

	va_start(arg, fmt);
	vfmtr(buf, sizeof buf, fmt, arg);
	va_end(arg);
	kstrcpy(up->errstr, buf, ERRMAX);
}

/* 2 (and any fd not in the table) is the kernel's console */
int
gfprint(int fd, char *fmt, ...)
{
	char buf[512];
	va_list arg;
	int n;

	va_start(arg, fmt);
	vfmtr(buf, sizeof buf, fmt, arg);
	va_end(arg);
	n = strlen(buf);
	if(fd > 2 && fd < Nfd && g.fd[fd].c != nil)
		return gwrite(fd, buf, n);
	print("%s", buf);
	return n;
}

void
_trace(char*, Bptr, vlong, vlong)
{
}

void*
emalloc(usize sz, int zero)
{
	void *p;

	if((p = mallocz(sz, zero)) == nil)
		gerror(Enomem);
	setmalloctag(p, getcallerpc(&sz));
	return p;
}

void
gsleep(long ms)
{
	if(ms <= 0)
		yield();
	else
		tsleep(&up->sleep, return0, nil, ms);
}

/* the kernel has wlock but not this */
int
canwlock(RWLock *q)
{
	lock(&q->use);
	if(q->readers == 0 && q->writer == 0){
		q->wpc = getcallerpc(&q);
		q->writer = 1;
		unlock(&q->use);
		return 1;
	}
	unlock(&q->use);
	return 0;
}

/*
 * libc's Rendez: a sleeper is queued before r->l is let go, so no
 * wakeup is missed; the waker wakes it with r->wl held, so it is still
 * there (its Gsleeper on its stack) when it is woken.
 */
static int
woken(void *a)
{
	return ((Gsleeper*)a)->woken;
}

void
rsleep(Grendez *r)
{
	Gsleeper s;

	memset(&s, 0, sizeof s);
	lock(&r->wl);
	if(r->tail != nil)
		r->tail->next = &s;
	else
		r->head = &s;
	r->tail = &s;
	unlock(&r->wl);
	qunlock(r->l);
	while(!s.woken){
		if(waserror())
			continue;	/* a note: still queued, sleep on */
		sleep(&s, woken, &s);
		poperror();
	}
	/* the waker took it off the queue; waiting for it to let go of wl */
	lock(&r->wl);
	unlock(&r->wl);
	qlock(r->l);
}

static int
wake1(Grendez *r)
{
	Gsleeper *s;

	if((s = r->head) == nil)
		return 0;
	r->head = s->next;
	if(r->head == nil)
		r->tail = nil;
	s->next = nil;
	s->woken = 1;
	wakeup(s);
	return 1;
}

int
rwakeup(Grendez *r)
{
	int n;

	lock(&r->wl);
	n = wake1(r);
	unlock(&r->wl);
	return n;
}

int
rwakeupall(Grendez *r)
{
	int n;

	lock(&r->wl);
	for(n = 0; wake1(r); n++)
		;
	unlock(&r->wl);
	return n;
}

/* a Chan in the table: its fd */
static int
newgfd(Kchan *c, char *name, vlong off, vlong size)
{
	int i;

	qlock(&g);
	for(i = 3; i < Nfd; i++)
		if(g.fd[i].c == nil){
			g.fd[i].c = c;
			g.fd[i].name = name;
			g.fd[i].off = off;
			g.fd[i].size = size;
			qunlock(&g);
			return i;
		}
	qunlock(&g);
	error("gefs: no fds");
}

static Gfd*
getgfd(int fd)
{
	if(fd < 3 || fd >= Nfd || g.fd[fd].c == nil){
		werrstr("bad fd %d", fd);
		return nil;
	}
	return &g.fd[fd];
}

/* only the device's name opens: it is set up before (gefsserve) */
int
gopen(char *name, int)
{
	int i;

	for(i = 3; i < Nfd; i++)
		if(g.fd[i].name != nil && strcmp(g.fd[i].name, name) == 0)
			return i;
	werrstr("%s: not gefs's device", name);
	return -1;
}

int
gclose(int fd)
{
	Gfd *f;
	Kchan *c;

	if((f = getgfd(fd)) == nil)
		return -1;
	qlock(&g);
	c = f->c;
	f->c = nil;
	f->name = nil;
	qunlock(&g);
	if(!waserror()){
		cclose(c);
		poperror();
	}
	return 0;
}

int
ghangup(int)
{
	return 0;
}

static long
rw(int fd, void *buf, long n, vlong off, int write)
{
	Gfd *f;
	Kchan *c;

	if((f = getgfd(fd)) == nil)
		return -1;
	c = f->c;
	if(f->size > 0){
		if(off < 0 || off >= f->size)
			return 0;
		if(n > f->size - off)
			n = f->size - off;
		off += f->off;
	}
	if(waserror())
		return -1;
	if(write)
		n = devtab[c->type]->write(c, buf, n, off);
	else
		n = devtab[c->type]->read(c, buf, n, off);
	poperror();
	return n;
}

long
gread(int fd, void *buf, long n)
{
	return rw(fd, buf, n, 0, 0);
}

long
greadn(int fd, void *buf, long n)
{
	long m, t;

	for(t = 0; t < n; t += m){
		m = gread(fd, (char*)buf+t, n-t);
		if(m <= 0)
			return t > 0 ? t : m;
	}
	return t;
}

long
gwrite(int fd, void *buf, long n)
{
	return rw(fd, buf, n, 0, 1);
}

long
gpread(int fd, void *buf, long n, vlong off)
{
	return rw(fd, buf, n, off, 0);
}

long
gpwrite(int fd, void *buf, long n, vlong off)
{
	return rw(fd, buf, n, off, 1);
}

Dir*
gdirfstat(int fd)
{
	Gfd *f;
	Dir *d;

	if((f = getgfd(fd)) == nil)
		return nil;
	if((d = mallocz(sizeof(Dir), 1)) == nil)
		return nil;
	d->length = f->size;
	return d;
}

/*
 * The disk's volatile write cache to the medium, after each wrwait in
 * gefs's sync (the arena heads, the superblocks, the arena tails):
 * SCSI's synchronize cache through #S's raw file, as devext4's
 * flushdev.  A failure is reported once.
 */
void
gflushdev(void)
{
	uchar cmd[10];
	char st[32];
	Kchan *c;
	long n;

	if(g.raw == nil)
		return;
	if(waserror()){
		if(!g.flushwarned)
			print("gefs: %s: cache flush: %s\n", g.raw, up->errstr);
		g.flushwarned = 1;
		return;
	}
	c = namec(g.raw, Aopen, ORDWR, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	memset(cmd, 0, sizeof cmd);
	cmd[0] = 0x35;
	devtab[c->type]->write(c, cmd, sizeof cmd, 0);
	devtab[c->type]->read(c, st, 0, 0);	/* no data */
	n = devtab[c->type]->read(c, st, sizeof st - 1, 0);
	st[n < 0 ? 0 : n] = 0;
	if(strtol(st, nil, 0) != 0)
		error("status not 0");
	poperror();
	cclose(c);
	poperror();
	g.nflush++;
}

/* #S's raw file of the disk dev is on (#S/sdN0/data, ...); nil if none */
static char*
rawfile(char *dev)
{
	char *s, *e, *r;
	int n;

	if(strncmp(dev, "#S/", 3) == 0)
		s = dev+3;
	else if(strncmp(dev, "/dev/sd", 7) == 0)
		s = dev+5;
	else
		return nil;
	if(strncmp(s, "sd", 2) != 0 || (e = strchr(s, '/')) == nil)
		return nil;
	n = e - s;
	r = smalloc(3+n+5);
	memmove(r, "#S/", 3);
	memmove(r+3, s, n);
	strcpy(r+3+n, "/raw");
	return r;
}

/* halted (the console's halt): read-only, synced; fshalt waits for this */
void
gefshalted(void)
{
	Kchan *c;

	print("gefs: halted\n");
	g.halted = 1;
	if(g.srv == nil || waserror())
		return;
	c = namec(g.srv, Aremove, 0, 0);
	if(waserror()){
		c->type = 0;	/* as sysremove: remove clunked it */
		cclose(c);
		nexterror();
	}
	devtab[c->type]->remove(c);
	poperror();
	c->type = 0;
	cclose(c);
	poperror();
}

/* main.c's, with the blocks from the kernel's heap */
static void
initfs(vlong cachesz, int rdonly)
{
	Bfree *f, *h;
	Blk *b;
	usize i;

	if((fs = mallocz(sizeof(Gefs), 1)) == nil)
		sysfatal("malloc: %r");
	aswapl(&fs->rdonly, rdonly);
	fs->lrurz.l = &fs->lrulk;
	fs->syncrz.l = &fs->synclk;
	fs->bfreerz.l = &fs->bfreelk;
	fs->noauth = 1;
	fs->cmax = cachesz/Blksz;
	if((fs->bcache = mallocz(fs->cmax*sizeof(Bucket), 1)) == nil)
		sysfatal("malloc: %r");

	fs->dlcount = 0;
	fs->dlcmax = fs->cmax/10;
	if(fs->dlcmax < 4)
		fs->dlcmax = 4;
	if(fs->dlcmax > 512)
		fs->dlcmax = 512;
	if((fs->dlcache = mallocz(fs->dlcmax*sizeof(Dlist*), 1)) == nil)
		sysfatal("malloc: %r");

	h = nil;
	for(i = 0; i < fs->cmax; i++){
		if((f = mallocz(sizeof(Bfree), 1)) == nil)
			sysfatal("malloc: %r");
		f->bp = Zb;
		f->next = h;
		h = f;
	}
	fs->bfree = h;

	for(i = 0; i < fs->cmax; i++){
		if((b = mallocz(sizeof(Blk), 1)) == nil)
			sysfatal("cache: %lld blocks: %r", fs->cmax);
		b->bp = Zb;
		b->magic = Magic;
		lrutop(b);
	}
}

/* main.c's launch: a kproc running f(id, arg) */
typedef struct Gproc Gproc;
struct Gproc {
	void	(*f)(int, void*);
	void	*arg;
	int	id;
};

static void
gproc(void *a)
{
	Gproc p;

	p = *(Gproc*)a;
	free(a);
	(*p.f)(p.id, p.arg);
	pexit("", 1);
}

static void
launch(void (*f)(int, void*), void *arg, int id, char *text)
{
	Gproc *p;
	char name[KNAMELEN];

	p = smalloc(sizeof *p);
	p->f = f;
	p->arg = arg;
	p->id = id;
	if(id >= 0)
		snprint(name, sizeof name, "gefs%s.%d", text, id);
	else
		snprint(name, sizeof name, "gefs%s", text);
	kproc(name, gproc, p);
}

/* a pipe's ends: name ("data" or "data1") of the pipe d, opened */
static Kchan*
pipeend(Kchan *d, char *name)
{
	Walkqid *wq;
	Kchan *c;

	wq = devtab[d->type]->walk(d, nil, &name, 1);
	if(wq == nil || wq->clone == nil){
		free(wq);
		error(Enonexist);
	}
	c = wq->clone;
	free(wq);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	c = devtab[c->type]->open(c, ORDWR);
	poperror();
	return c;
}

static void
mkpipe(Kchan **a, Kchan **b)
{
	Kchan *d;

	d = namec("#|", Atodir, 0, 0);
	if(waserror()){
		cclose(d);
		nexterror();
	}
	*a = pipeend(d, "data");
	if(waserror()){
		cclose(*a);
		nexterror();
	}
	*b = pipeend(d, "data1");
	poperror();
	poperror();
	cclose(d);
}

/* c as the srv file srv (#s/...), eve's: as gefs's /srv/gefs and gefs.cmd */
void
gefspost(Kchan *c, char *srv)
{
	Kchan *s;
	char buf[16];
	int fd;

	incref(c);
	if((fd = newfd(c, 0)) < 0){
		cclose(c);
		error(Enofd);
	}
	if(waserror()){
		fdclose(fd, 0);
		nexterror();
	}
	s = namec(srv, Acreate, OWRITE, 0600);
	if(waserror()){
		cclose(s);
		nexterror();
	}
	snprint(buf, sizeof buf, "%d", fd);
	devtab[s->type]->write(s, buf, strlen(buf), 0);
	poperror();
	cclose(s);
	poperror();
	fdclose(fd, 0);
}

static int
cachemb(void)
{
	char *s;
	uvlong mem;
	int mb;

	if((s = getconf("gefscache")) != nil && (mb = strtol(s, nil, 0)) >= 16)
		return mb;
	mem = (uvlong)conf.npage*BY2PG;
	mb = mem/8/MiB;
	if(mb < 16)
		mb = 16;
	if(mb > 512)
		mb = 512;
	return mb;
}

/*
 * gefs on dev (its partition at off, size long: c, opened), written
 * if rw: loaded, its procs started, serving 9P on a pipe.  The other
 * end of the pipe, for mntattach.  Its console is posted as srv.
 */
Kchan*
gefsserve(char *dev, Kchan *c, vlong off, vlong size, int rw, char *srv)
{
	static int once;
	Kchan *s0, *s1, *c0, *c1;
	char *d;
	int i, nproc, sfd, cfd;
	Conn *conn;

	if(once++)
		error("gefs: one at a time");
	/* main.c's sanity checks */
	assert(4*Kpmax < Pivspc);
	assert(2*Msgmax < Bufspc);
	assert(Treesz < Inlmax);

	if((d = getconf("gefsdebug")) != nil)
		debug = strtol(d, nil, 0);
	initfs((vlong)cachemb()*MiB, !rw);
	fmtinstall('B', Bconv);
	fmtinstall('N', Mconv);
	fmtinstall('P', Pconv);
	fmtinstall('K', Kconv);
	fmtinstall('R', Rconv);
	fmtinstall('J', Qconv);
	initshow();

	d = nil;
	kstrdup(&d, dev);
	dev = d;
	g.raw = rawfile(dev);
	newgfd(c, dev, off, size);
	loadfs(dev);

	nproc = conf.nmach*2;
	if(nproc < 2)
		nproc = 2;
	if(nproc > 8)
		nproc = 8;
	fs->wrchan = mkchan(32);
	fs->admchan = mkchan(32);
	fs->nsyncers = 1;
	fs->nreaders = nproc/2;
	if(fs->nsyncers > fs->narena)
		fs->nsyncers = fs->narena;
	for(i = 0; i < fs->nsyncers; i++)
		qinit(&fs->syncq[i]);
	if((fs->rdchan = malloc(fs->nreaders*sizeof(Chan*))) == nil)
		sysfatal("malloc: %r");
	for(i = 0; i < fs->nreaders; i++)
		fs->rdchan[i] = mkchan(32);
	for(i = 0; i < fs->narena; i++)
		fs->arenas[i].sync = &fs->syncq[i%fs->nsyncers];

	mkpipe(&s0, &s1);
	mkpipe(&c0, &c1);
	sfd = newgfd(s1, nil, 0, 0);
	cfd = newgfd(c1, nil, 0, 0);
	launch(runcons, (void*)(uintptr)cfd, aincl(&fs->nworker, 1), "cons");
	launch(runmutate, nil, aincl(&fs->nworker, 1), "mutate");
	launch(runsweep, nil, aincl(&fs->nworker, 1), "sweep");
	launch(runtasks, nil, -1, "tasks");
	for(i = 0; i < fs->nreaders; i++)
		launch(runread, fs->rdchan[i], aincl(&fs->nworker, 1), "read");
	for(i = 0; i < fs->nsyncers; i++)
		launch(runsync, &fs->syncq[i], aincl(&fs->nworker, 1), "sync");
	if((conn = newconn(sfd, sfd, -1)) == nil)
		sysfatal("newconn: %r");
	launch(runfs, conn, -1, "srv");
	/* c0 is kept: the console reads on with no srv (or after halt) */
	if(srv != nil && !waserror()){
		gefspost(c0, srv);
		kstrdup(&g.srv, srv);
		poperror();
	}
	print("gefs: %s@%lld, %lld MiB, cache %d MiB, %ld readers, %s\n",
		dev, off, size/MiB, cachemb(), fs->nreaders, rw ? "read-write" : "read-only");
	return s0;
}

/*
 * At reboot (devgefs's shutdown): halted as the console's halt does
 * it (the sweeper syncs), waited for a while.
 */
void
gefsshutdown(void)
{
	Amsg *a;
	int i;

	if(fs == nil || fs->admchan == nil || agetl(&fs->rdonly) || g.halting++)
		return;
	if(waserror())
		return;
	a = emalloc(sizeof(Amsg), 1);
	a->op = AOhalt;
	chsend(fs->admchan, a);
	for(i = 0; i < 100 && !g.halted; i++)
		tsleep(&up->sleep, return0, nil, 100);
	poperror();
	if(!g.halted)
		print("gefs: not halted\n");
}
