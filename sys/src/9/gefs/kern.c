/*
 * Plan2001: what gefs has from libc and from its main.c, in the kernel
 * (gefs.h).  gefsserve starts it on a device as kprocs (main.c's
 * procs: the mutator, the readers, the sweeper, the syncers, the
 * tasks and the console) and gives the kernel the other end of the
 * pipe it serves 9P on, for mntattach.  Each call makes a GE instance
 * of its own (docs/ge-fs-o.md): volumes share the engine's code, not
 * its state, so two of them run at the same time on different
 * partitions.
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

int	debug;
int	permissive;
int	usereserve;
char	*reamuser;
int	noneid		= 0;
int	nogroupid	= 9999;
int	admid		= -1;

static void	gwritetrace(Gefs*, vlong);

enum {
	Nmsgerr	= 256,	/* Nfd is the volume's (gdat.h) */
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
 * size), the connection and the console, each a Chan of the kernel's.
 * They are the volume's (Gefs.k, gdat.h), not one global table.
 */

/*
 * libc's %r is an errstr system call: in the kernel it is up->errstr,
 * put into the format as text
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
broke(Gefs *fs, char *fmt, ...)
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
_babort(Gefs *fs, vlong addr, char *msg)
{
	gwritetrace(fs, addr);
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
gfprint(Gefs *fs, int fd, char *fmt, ...)
{
	char buf[512];
	va_list arg;
	int n;

	va_start(arg, fmt);
	vfmtr(buf, sizeof buf, fmt, arg);
	va_end(arg);
	n = strlen(buf);
	if(fd > 2 && fd < Nfd && fs->k.fd[fd].c != nil)
		return gwrite(fs, fd, buf, n);
	print("%s", buf);
	return n;
}

/* main.c's trace (plan9.ini gefstrace=MiB), by pid */
void
_trace(Gefs *fs, char *msg, Bptr bp, vlong v0, vlong v1)
{
	Trace *t;
	ulong idx;

	idx = aincl(&fs->traceidx, 1);
	t = &fs->trace[(idx-1) % fs->ntrace];
	strecpy(t->msg, t->msg+sizeof(t->msg), msg);
	t->tid = up != nil ? up->pid : 0;
	t->qgen = agetv(&fs->qgen);
	t->bp = bp;
	t->v0 = v0;
	t->v1 = v1;
}

/* main.c's writetrace: what the trace has of addr (or all, -1), oldest first */
static void
gwritetrace(Gefs *fs, vlong addr)
{
	Trace *t;
	long ti;
	int i;

	if(fs == nil || fs->trace == nil)
		return;
	ti = agetl(&fs->traceidx);
	for(i = 0; i < fs->ntrace; i++){
		t = &fs->trace[(ti+i) % fs->ntrace];
		if(t->msg[0] == 0)
			continue;
		if(addr != -1 && t->bp.addr != -1 && t->bp.addr != addr && t->v0 != addr)
			continue;
		if(addr != -1 && t->bp.addr == -1 && t->v0 != addr)
			continue;
		print("[%d@%d] %s", t->tid, t->qgen, t->msg);
		if(t->bp.addr != -1)
			print(" %B", t->bp);
		if(t->v0 != -1)
			print(" %llx", t->v0);
		if(t->v1 != -1)
			print(" %llx", t->v1);
		print("\n");
	}
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
newgfd(Gefs *fs, Kchan *c, char *name, vlong off, vlong size)
{
	int i;

	qlock(&fs->k);
	for(i = 3; i < Nfd; i++)
		if(fs->k.fd[i].c == nil){
			fs->k.fd[i].c = c;
			fs->k.fd[i].name = name;
			fs->k.fd[i].off = off;
			fs->k.fd[i].size = size;
			qunlock(&fs->k);
			return i;
		}
	qunlock(&fs->k);
	error("gefs: no fds");
}

static Gfd*
getgfd(Gefs *fs, int fd)
{
	if(fd < 3 || fd >= Nfd || fs->k.fd[fd].c == nil){
		werrstr("bad fd %d", fd);
		return nil;
	}
	return &fs->k.fd[fd];
}

/* only the device's name opens: it is set up before (gefsserve) */
int
gopen(Gefs *fs, char *name, int)
{
	int i;

	for(i = 3; i < Nfd; i++)
		if(fs->k.fd[i].name != nil && strcmp(fs->k.fd[i].name, name) == 0)
			return i;
	werrstr("%s: not gefs's device", name);
	return -1;
}

int
gclose(Gefs *fs, int fd)
{
	Gfd *f;
	Kchan *c;

	if((f = getgfd(fs, fd)) == nil)
		return -1;
	qlock(&fs->k);
	c = f->c;
	f->c = nil;
	f->name = nil;
	qunlock(&fs->k);
	if(!waserror()){
		cclose(c);
		poperror();
	}
	return 0;
}

int
ghangup(Gefs *fs, int)
{
	USED(fs);
	return 0;
}

static long
rw(Gefs *fs, int fd, void *buf, long n, vlong off, int write)
{
	Gfd *f;
	Kchan *c;

	if((f = getgfd(fs, fd)) == nil)
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
gread(Gefs *fs, int fd, void *buf, long n)
{
	return rw(fs, fd, buf, n, 0, 0);
}

long
greadn(Gefs *fs, int fd, void *buf, long n)
{
	long m, t;

	for(t = 0; t < n; t += m){
		m = gread(fs, fd, (char*)buf+t, n-t);
		if(m <= 0)
			return t > 0 ? t : m;
	}
	return t;
}

long
gwrite(Gefs *fs, int fd, void *buf, long n)
{
	return rw(fs, fd, buf, n, 0, 1);
}

long
gpread(Gefs *fs, int fd, void *buf, long n, vlong off)
{
	return rw(fs, fd, buf, n, off, 0);
}

long
gpwrite(Gefs *fs, int fd, void *buf, long n, vlong off)
{
	return rw(fs, fd, buf, n, off, 1);
}

Dir*
gdirfstat(Gefs *fs, int fd)
{
	Gfd *f;
	Dir *d;

	if((f = getgfd(fs, fd)) == nil)
		return nil;
	if((d = mallocz(sizeof(Dir), 1)) == nil)
		return nil;
	d->length = f->size;
	return d;
}

/*
 * The disk's volatile write cache to the medium, after each wrwait in
 * gefs's sync (the arena heads, the superblocks, the arena tails):
 * SCSI's synchronize cache through #S's raw file (the drivers make it
 * ATA's flush cache, NVMe's flush, virtio's).  A failure is reported
 * once.
 */
void
gflushdev(Gefs *fs)
{
	uchar cmd[10];
	char st[32];
	Kchan *c;
	long n;

	if(fs->k.raw == nil)
		return;
	if(waserror()){
		if(!fs->k.flushwarned)
			print("gefs: %s: cache flush: %s\n", fs->k.raw, up->errstr);
		fs->k.flushwarned = 1;
		return;
	}
	c = namec(fs->k.raw, Aopen, ORDWR, 0);
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
	fs->k.nflush++;
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
gefshalted(Gefs *fs)
{
	Kchan *c;

	print("gefs: halted\n");
	fs->k.halted = 1;
	if(fs->k.srv == nil || waserror())
		return;
	c = namec(fs->k.srv, Aremove, 0, 0);
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

/*
 * main.c's, with the blocks from the kernel's heap.  FS makes the
 * volume (kern.c's gefsserve): every GE object it creates is bound to
 * it -- the tree, the dlists and the blocks carry their volume, so the
 * engine's functions find it in the data they were given.
 */
static void
initfs(Gefs *fs, vlong cachesz, int rdonly)
{
	Bfree *f, *h;
	Blk *b;
	usize i;

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
		f->fs = fs;
		f->next = h;
		h = f;
	}
	fs->bfree = h;

	for(i = 0; i < fs->cmax; i++){
		if((b = mallocz(sizeof(Blk), 1)) == nil)
			sysfatal("cache: %lld blocks: %r", fs->cmax);
		b->bp = Zb;
		b->fs = fs;
		b->magic = Magic;
		lrutop(b);
	}
}

/* main.c's launch: a kproc running f(id, arg) */
typedef struct Gproc Gproc;
struct Gproc {
	Gefs	*fs;		/* the volume the proc serves */
	void	(*f)(Gefs*, int, void*);
	void	*arg;
	int	id;
};

static void
gproc(void *a)
{
	Gproc p;

	p = *(Gproc*)a;
	free(a);
	(*p.f)(p.fs, p.id, p.arg);
	pexit("", 1);
}

static void
launch(Gefs *fs, void (*f)(Gefs*, int, void*), void *arg, int id, char *text)
{
	Gproc *p;
	char name[KNAMELEN];

	p = smalloc(sizeof *p);
	p->fs = fs;
	p->f = f;
	p->arg = arg;
	p->id = id;
	if(id >= 0)
		snprint(name, sizeof name, "gefs%d%s.%d", fs->id, text, id);
	else
		snprint(name, sizeof name, "gefs%d%s", fs->id, text);
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

/*
 * Plan2001: the cache budget is the kernel's, not a volume's
 * (docs/ge-fs-o.md 3): the instances' caches together must fit in it.
 * The first volume gets the whole budget, as before (plan9.ini's
 * gefscache=, or an eighth of memory); a later one gets what is left,
 * and is refused if that is less than Cachemin, instead of taking the
 * heap past the budget.  A machine that runs several volumes sets
 * gefscache= for all of them.
 */
enum {
	Cachemin	= 16,	/* MiB; also the least gefscache= may say */
};

static struct {
	QLock;
	int	set;
	int	budget;	/* MiB, all the volumes together */
	int	used;
} cachebud;

static int
cachemb(void)
{
	char *s;
	uvlong mem;
	int mb, budget;

	qlock(&cachebud);
	if(!cachebud.set){
		budget = 0;
		if((s = getconf("gefscache")) != nil)
			budget = strtol(s, nil, 0);
		if(budget < Cachemin){
			mem = (uvlong)conf.npage*BY2PG;
			budget = mem/8/MiB;
			if(budget < Cachemin)
				budget = Cachemin;
			if(budget > 512)
				budget = 512;
		}
		cachebud.budget = budget;
		cachebud.set = 1;
	}
	mb = cachebud.budget - cachebud.used;
	if(mb < Cachemin){
		qunlock(&cachebud);
		return -1;
	}
	cachebud.used += mb;
	qunlock(&cachebud);
	return mb;
}

/*
 * Plan2001: open a GE volume (docs/PoC-experimental-demo.md 1).  This
 * is the storage engine's context: its device, cache, superblock,
 * arenas, logs and recovery.  It loads no FS mount or user metadata and
 * starts no 9P server; gefsserve adds those for the legacy root.
 */
void*
geopen(char *dev, Kchan *c, vlong off, vlong size, int rw)
{
	static int nvol;	/* instances made, for their names */
	Gefs *fs;
	char *d;
	int mb;

	/* main.c's sanity checks */
	assert(4*Kpmax < Pivspc);
	assert(2*Msgmax < Bufspc);
	assert(Treesz < Inlmax);

	/*
	 * Plan2001: this call's GE instance (docs/ge-fs-o.md).  Nothing
	 * below is one volume's alone any more: the engine's state is in
	 * fs, and the objects it makes carry it.
	 */
	if((fs = mallocz(sizeof(Gefs), 1)) == nil)
		sysfatal("malloc: %r");
	fs->id = ++nvol;
	fs->snap.fs = fs;	/* the volume's own trees and dlists */
	fs->snapmnt = nil;
	fs->snapdl.fs = fs;
	fs->dropdl.fs = fs;
	fs->dropdl.hd = Zb;
	fs->dropdl.tl = Zb;
	fs->k.raw = nil;
	if((d = getconf("gefsdebug")) != nil)
		debug = strtol(d, nil, 0);
	if((mb = cachemb()) < 0){
		free(fs);
		error("gefs: no cache left for another volume (set gefscache=)");
	}
	initfs(fs, (vlong)mb*MiB, !rw);
	if((d = getconf("gefstrace")) != nil && strtol(d, nil, 0) > 0){
		fs->ntrace = strtol(d, nil, 0)*MiB/sizeof(Trace);
		if((fs->trace = mallocz(fs->ntrace*sizeof(Trace), 1)) == nil)
			fs->ntrace = 0;
	}
	fmtinstall('B', Bconv);
	fmtinstall('N', Mconv);
	fmtinstall('P', Pconv);
	fmtinstall('K', Kconv);
	fmtinstall('R', Rconv);
	fmtinstall('J', Qconv);
	initshow();

	d = nil;
	kstrdup(&d, dev);
	fs->k.raw = rawfile(d);
	newgfd(fs, c, d, off, size);
	loadge(fs, d);
	return fs;
}

/*
 * Plan2001: the engine's kprocs (mutator, sweeper, tasks, readers,
 * syncers), without FS's console and 9P server.  A native O volume
 * needs these and nothing from FS (docs/PoC-experimental-demo.md 1).
 */
void
gestart(void *v)
{
	Gefs *fs;
	int i, nproc;

	fs = v;
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
		qinit(fs, &fs->syncq[i]);
	if((fs->rdchan = malloc(fs->nreaders*sizeof(Chan*))) == nil)
		sysfatal("malloc: %r");
	for(i = 0; i < fs->nreaders; i++)
		fs->rdchan[i] = mkchan(32);
	for(i = 0; i < fs->narena; i++){
		fs->arenas[i].fs = fs;
		fs->arenas[i].sync = &fs->syncq[i%fs->nsyncers];
	}

	launch(fs, runmutate, nil, aincl(&fs->nworker, 1), "mutate");
	launch(fs, runsweep, nil, aincl(&fs->nworker, 1), "sweep");
	launch(fs, runtasks, nil, -1, "tasks");
	for(i = 0; i < fs->nreaders; i++)
		launch(fs, runread, fs->rdchan[i], aincl(&fs->nworker, 1), "read");
	for(i = 0; i < fs->nsyncers; i++)
		launch(fs, runsync, &fs->syncq[i], aincl(&fs->nworker, 1), "sync");
}

/*
 * Plan2001 stage 1 gate: a raw GE key/value record, read and written
 * back on the volume opened by geopen, before FS's loadfs.  The key is
 * GE's config namespace in the snapshot tree, not an FS file or path.
 */
void
geprobe(void *v)
{
	char kbuf[Keymax], vbuf[8], xbuf[Kvmax];
	Gefs *fs;
	Key k;
	Kvp kv;
	Msg m;
	uvlong seq;

	fs = v;
	kbuf[0] = Kconf;
	strecpy(kbuf+1, kbuf+sizeof(kbuf), "getest.seq");
	k.k = kbuf;
	k.nk = 1+strlen("getest.seq");
	seq = 0;
	if(btlookup(&fs->snap, &k, &kv, xbuf, sizeof(xbuf))){
		if(kv.nv != 8)
			error("getest: bad value length");
		seq = UNPACK64(kv.v);
	}
	print("getest: raw GE key getest.seq = %llud\n", seq);
	seq++;
	PACK64(vbuf, seq);
	m.op = Oinsert;
	m.k = kbuf;
	m.nk = k.nk;
	m.v = vbuf;
	m.nv = sizeof(vbuf);
	qlock(&fs->mutlk);
	if(waserror()){
		qunlock(&fs->mutlk);
		nexterror();
	}
	btupsert(&fs->snap, &m, 1);
	qunlock(&fs->mutlk);
	poperror();
	if(!btlookup(&fs->snap, &k, &kv, xbuf, sizeof(xbuf)) || kv.nv != 8 || UNPACK64(kv.v) != seq)
		error("getest: raw GE write did not read back");
	gesync(fs, 0);
	print("getest: raw GE key getest.seq written %llud, synced: PASS\n", seq);
}

/*
 * gefs on dev (its partition at off, size long: c, opened), written
 * if rw: its procs started, serving 9P on a pipe.  The other end of the
 * pipe, for mntattach.  Its console is posted as srv.
 */
Kchan*
gefsserve(char *dev, Kchan *c, vlong off, vlong size, int rw, char *srv, void **vol)
{
	Kchan *s0, *s1, *c0, *c1;
	Gefs *fs;
	int sfd, cfd;
	int mb;
	Conn *conn;

	fs = geopen(dev, c, off, size, rw);
	mb = fs->cmax*Blksz/MiB;

	mkpipe(&c0, &c1);
	cfd = newgfd(fs, c1, nil, 0, 0);
	launch(fs, runcons, (void*)(uintptr)cfd, aincl(&fs->nworker, 1), "cons");
	gestart(fs);
	loadfs(fs);

	mkpipe(&s0, &s1);
	sfd = newgfd(fs, s1, nil, 0, 0);
	if((conn = newconn(fs, sfd, sfd, -1)) == nil)
		sysfatal("newconn: %r");
	launch(fs, runfs, conn, -1, "srv");
	/* c0 is kept: the console reads on with no srv (or after halt) */
	if(srv != nil && !waserror()){
		gefspost(c0, srv);
		kstrdup(&fs->k.srv, srv);
		poperror();
	}
	print("gefs%d: %s@%lld, %lld MiB, cache %d MiB, %ld readers, %s\n",
		fs->id, dev, off, size/MiB, mb, fs->nreaders, rw ? "read-write" : "read-only");
	if(vol != nil)
		*vol = fs;
	return s0;
}

/*
 * At reboot (devgefs's shutdown): halted as the console's halt does
 * it (the sweeper syncs), waited for a while.
 */
void
gefsshutdown(void *v)
{
	Gefs *fs = v;
	Amsg *a;
	int i;

	if(fs == nil || fs->admchan == nil || agetl(&fs->rdonly) || fs->k.halting++)
		return;
	if(waserror())
		return;
	a = emalloc(sizeof(Amsg), 1);
	a->op = AOhalt;
	chsend(fs->admchan, a);
	for(i = 0; i < 100 && !fs->k.halted; i++)
		tsleep(&up->sleep, return0, nil, 100);
	poperror();
	if(!fs->k.halted)
		print("gefs%d: not halted\n", fs->id);
}
