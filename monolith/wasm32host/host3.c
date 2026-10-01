/*
 * Plan2001 wasm32 processes (3c, 3l; docs/wasm32.md) under drawterm's
 * kernel in the browser: docs/architecture.md, step A.  A process is a
 * kproc - a Worker - that runs the program's module; its one import,
 * plan9.syscall(number, args), comes here and becomes drawterm's sys*
 * calls.  The program's memory is its own: what a call reads or writes
 * there goes through h3in and h3out (copyin, copyout), which the
 * platform (host3js.c, JavaScript) does, as DMA.
 *
 * fork: 3l made the program's stack unwindable; the platform unwinds it
 * into the program's memory, which comes here as a copy for the child -
 * a new kproc - and both rewind.  exec: a program from the name space
 * (#! too).  rfork(RFMEM): the procs of one memory take turns on its
 * Worker, as on a uniprocessor - the platform switches by unwinding one
 * and rewinding another - and their system calls run on a helper kproc
 * each, so one blocked does not stop the others.  A system call is
 * prep (its arguments copied in, on the Worker), doreq (the kernel's
 * part, on the Worker or a helper) and fin (its results copied out).
 *
 * The page loads build/wasm32/root into #U (/root/wasm32root, bound on /
 * and /bin); ?wasm=host3&prog=NAME&arg=... runs /bin/NAME.
 */
#include	"u.h"
#include	"lib.h"
#include	"dat.h"
#include	"fns.h"
#include	"error.h"
#include	"user.h"

#undef	getenv
extern	char*	getenv(const char*);
extern	void	guimain(void);
extern	long	_sysfd2path(int, char*, uint);

typedef struct H3 H3;

/* the platform (host3js.c) */
extern	int	h3in(void*, ulong, int);
extern	int	h3out(ulong, void*, int);
extern	int	h3strlen(ulong, int);
extern	int	h3brk(ulong);
extern	void	h3stop(int);
extern	void	h3unwind(int);
extern	int	h3run(H3*, int, uchar*, int, int, char**, uchar*, int, ulong, ulong, ulong, ulong, vlong*, int*, char*, int);
extern	void	h3log(char*, int, char*);
extern	void	h3trace(char*);
extern	void	h3jdebug(void);

/* 9front's /sys/src/libc/9syscall/sys.h */
enum {
	SYSR1 = 0, _ERRSTR, BIND, CHDIR, CLOSE, DUP, ALARM, EXEC, EXITS, _FSESSION,
	FAUTH, _FSTAT, SEGBRK, _MOUNT, OPEN, _READ, OSEEK, SLEEP, _STAT, RFORK,
	_WRITE, PIPE, CREATE, FD2PATH, BRK_, REMOVE, _WSTAT, _FWSTAT, NOTIFY, NOTED,
	SEGATTACH, SEGDETACH, SEGFREE, SEGFLUSH, RENDEZVOUS, UNMOUNT, _WAIT,
	SEMACQUIRE, SEMRELEASE, SEEK, FVERSION, ERRSTR, STAT, FSTAT, WSTAT, FWSTAT,
	MOUNT, AWAIT, PREAD = 50, PWRITE, TSEMACQUIRE, _NSEC,
};

/* libc.h's rfork flags */
enum {
	RFNAMEG = 1<<0, RFENVG = 1<<1, RFFDG = 1<<2, RFNOTEG = 1<<3, RFPROC = 1<<4,
	RFMEM = 1<<5, RFNOWAIT = 1<<6, RFCNAMEG = 1<<10, RFCENVG = 1<<11,
	RFCFDG = 1<<12, RFREND = 1<<13, RFNOMNT = 1<<14,
};

enum {
	Hnew = 0,	/* h3run's modes */
	Hchild,
	Hgoon,

	Hexited = 0,	/* and why it returned */
	Hforked,
	Hexec,

	Stopexit = 1,	/* h3stop */
	Stopexec = 2,

	Nwait = 64,
	Maxargs = 1024,
};

typedef struct Img Img;
struct Img
{
	Ref	ref;
	uchar	*p;
	long	n;
};

/* one system call: its arguments here, in our memory */
typedef struct Req Req;
struct Req
{
	int	n;
	ulong	v[6];
	char	*s;
	char	*s2;
	uchar	*buf;	/* to or from the program */
	long	nbuf;
	ulong	ubuf;	/* where in the program: copied there if out */
	int	out;
	int	fd[2];
	vlong	r;
	char	err[ERRMAX];
};

/*
 * a proc: a process's first, and each rfork(RFMEM) one of its memory
 */
struct H3
{
	/* host3js.c reads these: first, in this order */
	vlong	ret;	/* its system call's result */
	int	pid;
	int	done;	/* its helper finished the call */

	H3	*proc;	/* the first of its memory: the process */
	H3	*parent;
	int	nowait;
	int	ended;
	char	*name;
	char	*prog;	/* the first process's: what the page ran */
	char	err[ERRMAX];
	char	exitmsg[ERRMAX];

	/* children's ends, for await */
	Lock	lk;
	Rendez	r;
	char	*wait[Nwait];
	int	nwait;
	int	nchild;

	/* its helper kproc */
	int	helper;
	Rendez	hr;
	Req	*req;
	int	pending;
	int	quit;

	/* the process's: the program, and how h3run starts it next */
	Img	*img;
	int	argc;
	char	**argv;
	int	mode;
	uchar	*snap;
	long	nsnap;
	ulong	asptr;
	ulong	asbase;
	ulong	stacktop;
	ulong	ctxfn;	/* a forked child's: its context's function, 0 _start */
	vlong	asret;
	Img	*eimg;	/* exec's */
	int	eargc;
	char	**eargv;
	Lock	dlk;	/* helpers' ends */
	Rendez	dr;
	int	ndone;
};

static	Lock	pidlock;
static	int	pids;
static	int	h3debug;

static void
dbg(char *fmt, ...)
{
	char buf[256];
	va_list arg;

	if(!h3debug)
		return;
	va_start(arg, fmt);
	vseprint(buf, buf+sizeof buf, fmt, arg);
	va_end(arg);
	h3trace(buf);
}

static int
newpid(void)
{
	int p;

	lock(&pidlock);
	p = ++pids;
	unlock(&pidlock);
	return p;
}

static void
freeargv(int argc, char **argv)
{
	int i;

	for(i = 0; i < argc; i++)
		free(argv[i]);
	free(argv);
}

static void
imgfree(Img *m)
{
	if(m != nil && decref(&m->ref) == 0) {
		free(m->p);
		free(m);
	}
}

/*
 * a program from the name space: WebAssembly, or #!interpreter [arg],
 * whose program it is with argv made so
 */
static Img*
h3load(char *name, int *argcp, char ***argvp, int depth)
{
	int fd, n, i, argc;
	long m;
	char *p, *e, *interp, *arg, **argv;
	uchar *b;
	Img *img;

	fd = sysopen(name, OREAD);
	if(fd < 0)
		return nil;
	m = 65536;
	b = malloc(m);
	n = 0;
	for(;;) {
		if(n == m) {
			m *= 2;
			b = realloc(b, m);
		}
		i = sysread(fd, b+n, m-n);
		if(i <= 0)
			break;
		n += i;
	}
	sysclose(fd);
	if(n >= 4 && memcmp(b, "\0asm", 4) == 0) {
		img = mallocz(sizeof *img, 1);
		img->ref.ref = 1;
		img->p = b;
		img->n = n;
		return img;
	}
	if(n < 2 || b[0] != '#' || b[1] != '!' || depth > 0) {
		free(b);
		werrstr("exec header invalid");
		return nil;
	}
	/* #!interp [arg] */
	p = (char*)b+2;
	e = memchr(p, '\n', n-2);
	if(e == nil)
		e = (char*)b+n;
	*e = 0;
	while(*p == ' ' || *p == '\t')
		p++;
	interp = p;
	while(*p && *p != ' ' && *p != '\t')
		p++;
	arg = nil;
	if(*p) {
		*p++ = 0;
		while(*p == ' ' || *p == '\t')
			p++;
		if(*p)
			arg = p;
	}
	interp = strdup(interp);
	arg = arg != nil ? strdup(arg) : nil;
	free(b);
	argc = *argcp;
	argv = mallocz((argc+3)*sizeof(char*), 1);
	n = 0;
	argv[n++] = strdup(interp);
	if(arg != nil)
		argv[n++] = arg;
	argv[n++] = strdup(name);
	for(i = 1; i < argc; i++)
		argv[n++] = (*argvp)[i];
	if(argc > 0)
		free((*argvp)[0]);
	free(*argvp);
	*argvp = argv;
	*argcp = n;
	img = h3load(interp, argcp, argvp, depth+1);
	free(interp);
	return img;
}

/* the program's string at u, in our memory (free it) */
static char*
ustr(ulong u)
{
	int n;
	char *s;

	if(u == 0)
		return nil;
	n = h3strlen(u, 4096);
	if(n < 0)
		return nil;
	s = smalloc(n+1);
	h3in(s, u, n+1);
	return s;
}

static long
fd2path(int fd, char *buf, uint nbuf)
{
	long r;

	if(waserror()) {
		werrstr("%s", up->errstr);
		return -1;
	}
	r = _sysfd2path(fd, buf, nbuf);
	poperror();
	return r;
}

/* a child's end, for its parent's await: 'pid utime stime rtime msg' */
static void
h3end(H3 *p)
{
	H3 *q;
	char *w;

	if(p->ended)
		return;
	p->ended = 1;
	dbg("%d %s ends '%s'", p->pid, p->name, p->exitmsg);
	q = p->parent;
	if(q == nil || p->nowait)
		return;
	w = smprint("%d 0 0 0 %q", p->pid, p->exitmsg);
	lock(&q->lk);
	if(q->nwait < Nwait)
		q->wait[q->nwait++] = w;
	else
		free(w);
	q->nchild--;
	unlock(&q->lk);
	wakeup(&q->r);
}

static int
havewait(void *v)
{
	H3 *p;

	p = v;
	return p->nwait > 0 || p->nchild == 0;
}

static vlong
h3await(H3 *p, Req *q)
{
	char *w;
	long m;

	for(;;) {
		lock(&p->lk);
		if(p->nwait > 0)
			break;
		if(p->nchild == 0) {
			unlock(&p->lk);
			werrstr("no living children");
			return -1;
		}
		unlock(&p->lk);
		ksleep(&p->r, havewait, p);	/* the kernel's sleep: user.h makes sleep osmsleep */
	}
	w = p->wait[0];
	memmove(p->wait, p->wait+1, (p->nwait-1)*sizeof(char*));
	p->nwait--;
	unlock(&p->lk);
	m = strlen(w);
	if(m > q->nbuf)
		m = q->nbuf;
	memmove(q->buf, w, m);
	free(w);
	return m;
}

/*
 * prep: a call's arguments, strings and buffers copied in; -1 if they are
 * not in the program's memory
 */
static int
needbuf(Req *q, ulong u, long n, int in)
{
	if(n < 0)
		return -1;
	q->buf = mallocz(n+1, 1);
	q->nbuf = n;
	q->ubuf = u;
	q->out = !in;
	if(in && h3in(q->buf, u, n) < 0)
		return -1;
	return 0;
}

static int
needstr(char **s, ulong u)
{
	*s = ustr(u);
	return *s == nil ? -1 : 0;
}

static int
prep(Req *q, int n, ulong a)
{
	ulong *v;

	q->n = n;
	v = q->v;
	if(h3in(v, a, sizeof q->v) < 0)
		return -1;
	switch(n) {
	case OPEN:
	case CREATE:
	case CHDIR:
	case REMOVE:
		return needstr(&q->s, v[0]);
	case BIND:
		return needstr(&q->s, v[0]) < 0 || needstr(&q->s2, v[1]) < 0 ? -1 : 0;
	case MOUNT:
		q->s2 = ustr(v[4]);
		return needstr(&q->s, v[2]);
	case UNMOUNT:
		q->s = ustr(v[0]);
		return needstr(&q->s2, v[1]);
	case PREAD:
	case FSTAT:
	case FD2PATH:
		return needbuf(q, v[1], v[2], 0);
	case STAT:
		return needstr(&q->s, v[0]) < 0 ? -1 : needbuf(q, v[1], v[2], 0);
	case PWRITE:
	case FWSTAT:
		return needbuf(q, v[1], v[2], 1);
	case WSTAT:
		return needstr(&q->s, v[0]) < 0 ? -1 : needbuf(q, v[1], v[2], 1);
	case AWAIT:
		return needbuf(q, v[0], v[1], 0);
	case PIPE:
		return needbuf(q, v[0], 2*sizeof(int), 0);
	}
	return 0;
}

/* the kernel's part: on the program's Worker, or its proc's helper */
static void
doreq(H3 *c, Req *q)
{
	ulong *v;
	vlong off;

	v = q->v;
	off = (uvlong)v[3] | (uvlong)v[4]<<32;
	switch(q->n) {
	default:
		werrstr("system call %d not here (wasm32)", q->n);
		q->r = -1;
		break;
	case OPEN:	q->r = sysopen(q->s, v[1]); break;
	case CREATE:	q->r = syscreate(q->s, v[1], v[2]); break;
	case CLOSE:	q->r = sysclose(v[0]); break;
	case DUP:	q->r = sysdup(v[0], v[1]); break;
	case PREAD:	q->r = syspread(v[0], q->buf, q->nbuf, off); break;
	case PWRITE:	q->r = syspwrite(v[0], q->buf, q->nbuf, off); break;
	case SEEK:	q->r = sysseek(v[0], (uvlong)v[1] | (uvlong)v[2]<<32, v[3]); break;
	case FSTAT:	q->r = sysfstat(v[0], q->buf, q->nbuf); break;
	case STAT:	q->r = sysstat(q->s, q->buf, q->nbuf); break;
	case FWSTAT:	q->r = sysfwstat(v[0], q->buf, q->nbuf); break;
	case WSTAT:	q->r = syswstat(q->s, q->buf, q->nbuf); break;
	case BIND:	q->r = sysbind(q->s, q->s2, v[2]); break;
	case MOUNT:	q->r = sysmount(v[0], v[1], q->s, v[3], q->s2); break;
	case UNMOUNT:	q->r = sysunmount(q->s, q->s2); break;
	case CHDIR:	q->r = syschdir(q->s); break;
	case REMOVE:	q->r = sysremove(q->s); break;
	case AWAIT:	q->r = h3await(c, q); break;
	case PIPE:
		q->r = syspipe(q->fd);
		if(q->r >= 0)
			memmove(q->buf, q->fd, sizeof q->fd);
		break;
	case FD2PATH:
		q->r = fd2path(v[0], (char*)q->buf, q->nbuf);
		break;
	case SLEEP:
		if((long)v[0] > 0)
			osmsleep(v[0]);
		q->r = 0;
		break;
	case RENDEZVOUS:
		/* tags and values: the program's numbers, not addresses here */
		q->r = (ulong)(uintptr)sysrendezvous((void*)(uintptr)v[0], (void*)(uintptr)v[1]);
		if(q->r == (ulong)~0)
			q->r = ~0;	/* (void*)~0: interrupted */
		break;
	}
	if(q->r < 0)
		strecpy(q->err, q->err+ERRMAX, up->syserrstr);
}

/* fin: the results copied out, the error kept as the proc's errstr */
static void
fin(H3 *c, Req *q)
{
	long n;

	if(q->r >= 0 && q->out) {
		n = q->nbuf;
		if(q->n == PREAD || q->n == FSTAT || q->n == STAT || q->n == AWAIT)
			n = q->r;
		else if(q->n == FD2PATH)
			n = strlen((char*)q->buf)+1;
		if(n > 0 && h3out(q->ubuf, q->buf, n) < 0) {
			q->r = -1;
			strecpy(q->err, q->err+ERRMAX, "bad address in system call");
		}
	}
	if(q->r < 0)
		strecpy(c->err, c->err+ERRMAX, q->err);
	c->ret = q->r;
	free(q->s);
	free(q->s2);
	free(q->buf);
	free(q);
}

static void	h3proc(void*);

/* fork: p unwound, snap its memory (ours now, malloc'd) */
static H3*
h3child(H3 *p, int flags, uchar *snap, long nsnap, ulong asptr, ulong asbase, ulong stacktop, ulong ctxfn)
{
	H3 *c;
	Fgrp *f;
	Rgrp *rg;

	c = mallocz(sizeof *c, 1);
	c->pid = newpid();
	c->proc = c;
	c->parent = p;
	c->nowait = (flags & RFNOWAIT) != 0;
	c->name = strdup(p->name);
	incref(&p->img->ref);
	c->img = p->img;
	c->mode = Hchild;
	c->snap = snap;
	c->nsnap = nsnap;
	c->asptr = asptr;
	c->asbase = asbase;
	c->stacktop = stacktop;
	c->ctxfn = ctxfn;
	c->asret = 0;
	if(!c->nowait) {
		lock(&p->lk);
		p->nchild++;
		unlock(&p->lk);
	}
	/* what kproc gives the child: ours, or copies, as the flags say */
	f = up->fgrp;
	rg = up->rgrp;
	if(flags & RFFDG)
		up->fgrp = dupfgrp(f);
	else if(flags & RFCFDG)
		up->fgrp = dupfgrp(nil);
	if(flags & RFREND)
		up->rgrp = newrgrp();
	kproc(c->name, h3proc, c);
	if(up->fgrp != f) {
		closefgrp(up->fgrp);
		up->fgrp = f;
	}
	if(up->rgrp != rg) {
		closergrp(up->rgrp);
		up->rgrp = rg;
	}
	return c;
}

static vlong
h3exec(H3 *c, ulong uname, ulong uargv)
{
	char *name, **argv;
	ulong a;
	int argc;
	Img *img;

	if(c->proc != c || c->helper) {
		werrstr("exec from an rfork(RFMEM) proc: not yet on wasm32");
		return -1;
	}
	if((name = ustr(uname)) == nil) {
		werrstr("bad address in system call");
		return -1;
	}
	argv = mallocz(Maxargs*sizeof(char*), 1);
	for(argc = 0; argc < Maxargs-1; argc++) {
		if(h3in(&a, uargv + 4*argc, 4) < 0 || a != 0 && (argv[argc] = ustr(a)) == nil) {
			freeargv(argc, argv);
			free(name);
			werrstr("bad address in system call");
			return -1;
		}
		if(a == 0)
			break;
	}
	img = h3load(name, &argc, &argv, 0);
	dbg("%d %s exec %s: %s", c->pid, c->name, name, img != nil ? "ok" : "failed");
	free(name);
	if(img == nil) {
		freeargv(argc, argv);
		return -1;
	}
	c->eimg = img;
	c->eargc = argc;
	c->eargv = argv;
	h3stop(Stopexec);
	return 0;
}

/* rfork without RFPROC: the calling proc's groups */
static vlong
h3rfork(H3 *c, int flags)
{
	Fgrp *f;
	Rgrp *rg;

	if(flags & RFPROC) {
		/* the stack unwinds; the platform makes the child (RFMEM) or h3proc does */
		h3unwind(flags);
		return 0;
	}
	USED(c);
	if(flags & (RFFDG|RFCFDG)) {
		f = up->fgrp;
		up->fgrp = dupfgrp((flags & RFFDG) ? f : nil);
		closefgrp(f);
	}
	if(flags & RFREND) {
		rg = up->rgrp;
		up->rgrp = newrgrp();
		closergrp(rg);
	}
	/* RFNAMEG, RFENVG, RFNOTEG: one name space and environment yet */
	return 0;
}

/* calls the program's Worker does itself, even with helpers */
static int
local(int n)
{
	switch(n) {
	case EXITS:
	case EXEC:
	case RFORK:
	case BRK_:
	case ERRSTR:
	case _NSEC:
	case ALARM:
	case NOTIFY:
	case NOTED:
		return 1;
	}
	return 0;
}

static void
dolocal(H3 *c, Req *q)
{
	ulong *v;
	char *s, buf[ERRMAX];

	v = q->v;
	q->r = 0;
	switch(q->n) {
	case EXITS:
		s = ustr(v[0]);
		strecpy(c->exitmsg, c->exitmsg+sizeof c->exitmsg, s != nil ? s : "");
		dbg("%d %s exits '%s'", c->pid, c->name, c->exitmsg);
		free(s);
		h3stop(Stopexit);
		break;
	case EXEC:
		q->r = h3exec(c, v[0], v[1]);
		break;
	case RFORK:
		q->r = h3rfork(c, v[0]);
		break;
	case BRK_:
		q->r = h3brk(v[0]);
		if(q->r < 0)
			werrstr("no memory");
		break;
	case ERRSTR:
		/* swap the program's buffer and its proc's errstr */
		if((long)v[1] <= 0) {
			q->r = -1;
			werrstr("bad errstr buffer");
			break;
		}
		if(v[1] > ERRMAX)
			v[1] = ERRMAX;
		if(h3in(buf, v[0], v[1]) < 0 || h3out(v[0], c->err, strlen(c->err)+1) < 0) {
			q->r = -1;
			werrstr("bad address in system call");
			break;
		}
		buf[v[1]-1] = 0;
		strecpy(c->err, c->err+ERRMAX, buf);
		break;
	case _NSEC:
		q->r = nsec();
		break;
	}
	if(q->r < 0)
		strecpy(q->err, q->err+ERRMAX, up->syserrstr);
}

static void
trace(H3 *c, Req *q, char *when)
{
	if(h3debug > 2 || h3debug > 1 && *when == '=')
		dbg("%d %s sys %d (%lux %lux %lux %lux) %s %lld%s%s", c->pid, c->name, q->n,
			q->v[0], q->v[1], q->v[2], q->v[3], when, q->r,
			q->r < 0 && *when == '=' ? " " : "", q->r < 0 && *when == '=' ? q->err : "");
}

/*
 * a system call, all of it here: one proc in the memory
 */
void
h3sys1(H3 *c, int n, ulong a)
{
	Req *q;

	q = mallocz(sizeof *q, 1);
	if(prep(q, n, a) < 0) {
		q->r = -1;
		strecpy(q->err, q->err+ERRMAX, "bad address in system call");
	} else if(local(n))
		dolocal(c, q);
	else {
		trace(c, q, "...");
		doreq(c, q);
	}
	trace(c, q, "=");
	fin(c, q);
}

/*
 * procs taking turns: a call that is not local goes to c's helper; 1 if
 * it is pending there (h3finish when c->done)
 */
static int
havereq(void *v)
{
	H3 *c;

	c = v;
	return c->pending || c->quit;
}

static void
h3helper(void *v)
{
	H3 *c, *p;
	Req *q;

	c = v;
	p = c->proc;
	for(;;) {
		ksleep(&c->hr, havereq, c);
		if(c->quit)
			break;
		q = c->req;
		c->pending = 0;
		trace(c, q, "...");
		doreq(c, q);
		__atomic_store_n(&c->done, 1, __ATOMIC_SEQ_CST);
		lock(&p->dlk);
		p->ndone++;
		unlock(&p->dlk);
		wakeup(&p->dr);
	}
	if(up->fgrp != nil) {
		closefgrp(up->fgrp);
		up->fgrp = nil;
	}
}

/* c's helper, from the program's Worker; flags for an rfork(RFMEM) child's */
static void
mkhelper(H3 *c, int flags)
{
	Fgrp *f;
	Rgrp *rg;

	if(c->helper)
		return;
	c->helper = 1;
	f = up->fgrp;
	rg = up->rgrp;
	if(flags & RFFDG)
		up->fgrp = dupfgrp(f);
	else if(flags & RFCFDG)
		up->fgrp = dupfgrp(nil);
	if(flags & RFREND)
		up->rgrp = newrgrp();
	kproc(c->name, h3helper, c);
	if(up->fgrp != f) {
		closefgrp(up->fgrp);
		up->fgrp = f;
	}
	if(up->rgrp != rg) {
		closergrp(up->rgrp);
		up->rgrp = rg;
	}
}

void
h3mkhelper(H3 *c)
{
	mkhelper(c, 0);
}

int
h3start(H3 *c, int n, ulong a)
{
	Req *q;

	q = mallocz(sizeof *q, 1);
	if(prep(q, n, a) < 0) {
		q->r = -1;
		strecpy(q->err, q->err+ERRMAX, "bad address in system call");
	} else if(local(n))
		dolocal(c, q);
	else {
		mkhelper(c, 0);
		c->req = q;
		c->done = 0;
		c->pending = 1;
		wakeup(&c->hr);
		return 1;
	}
	trace(c, q, "=");
	fin(c, q);
	return 0;
}

void
h3finish(H3 *c)
{
	trace(c, c->req, "=");
	fin(c, c->req);
	c->req = nil;
	c->done = 0;
}

static int
havedone(void *v)
{
	H3 *p;

	p = v;
	return p->ndone > 0;
}

/* until a helper of p's memory finishes */
void
h3waitdone(H3 *p)
{
	ksleep(&p->dr, havedone, p);
	lock(&p->dlk);
	p->ndone = 0;
	unlock(&p->dlk);
}

/* rfork(RFMEM|RFPROC): a proc for the child, sharing parent's memory */
H3*
h3newco(H3 *parent, int flags)
{
	H3 *c;

	c = mallocz(sizeof *c, 1);
	c->pid = newpid();
	c->proc = parent->proc;
	c->parent = parent;
	c->nowait = (flags & RFNOWAIT) != 0;
	c->name = strdup(parent->name);
	if(!c->nowait) {
		lock(&parent->lk);
		parent->nchild++;
		unlock(&parent->lk);
	}
	mkhelper(parent, 0);
	mkhelper(c, flags);
	dbg("%d %s rfork RFMEM %#x: child %d", parent->pid, parent->name, flags, c->pid);
	return c;
}

/* an rfork(RFMEM) proc's end; not the process's */
void
h3coend(H3 *c)
{
	h3end(c);
	c->quit = 1;
	wakeup(&c->hr);
}

static void
h3proc(void *v)
{
	H3 *p, *c;
	int r, out[9];
	char msg[ERRMAX];

	p = v;
	for(;;) {
		msg[0] = 0;
		r = h3run(p, p->mode, p->img->p, p->img->n, p->argc, p->argv,
			p->snap, p->nsnap, p->asptr, p->asbase, p->stacktop, p->ctxfn, &p->asret, out, msg, sizeof msg);
		free(p->snap);
		p->snap = nil;
		if(r < 0) {
			print("%s: %s\n", p->name, msg);
			strecpy(p->exitmsg, p->exitmsg+sizeof p->exitmsg, msg);
			break;
		}
		if(out[0] == Hforked) {
			/* out[5]: the proc that forked (an rfork(RFMEM) one perhaps), 6, 7 its stack's */
			c = h3child((H3*)(uintptr)out[5], out[1], (uchar*)(uintptr)out[2], out[3], out[4], out[6], out[7], out[8]);
			dbg("%d %s fork %#x: child %d, %d bytes", p->pid, p->name, out[1], c->pid, out[3]);
			p->asret = c->pid;
			p->mode = Hgoon;
			continue;
		}
		if(out[0] == Hexec) {
			imgfree(p->img);
			freeargv(p->argc, p->argv);
			p->img = p->eimg;
			p->argc = p->eargc;
			p->argv = p->eargv;
			p->eimg = nil;
			if(p->name != p->prog)
				free(p->name);
			p->name = strdup(p->argc > 0 ? p->argv[0] : "?");
			p->mode = Hnew;
			continue;
		}
		break;
	}
	if(p->parent == nil)
		h3log(p->prog, p->exitmsg[0] != 0, p->exitmsg);
	h3end(p);
	/* as pexit: its files closed (the ends of its pipes), its groups let go */
	if(up->fgrp != nil) {
		closefgrp(up->fgrp);
		up->fgrp = nil;
	}
	if(up->rgrp != nil) {
		closergrp(up->rgrp);
		up->rgrp = nil;
	}
	imgfree(p->img);
}

void
cpubody(void)
{
	char *prog, *args, *a, *e, *path;
	H3 *p;
	int n;

	if(bind("#i", "/dev", MBEFORE) < 0)
		panic("bind #i: %r");
	if(bind("#m", "/dev", MBEFORE) < 0)
		panic("bind #m: %r");
	/* ?console=eia0: the programs' 0, 1, 2 the serial port, not the screen */
	if((a = getenv("WASM32CONSOLE")) != nil && strcmp(a, "eia0") == 0) {
		n = sysopen("#t/eia0", ORDWR);
		if(n < 0)
			panic("open #t/eia0: %r");
		sysdup(n, 0);
		sysdup(n, 1);
		sysdup(n, 2);
		sysclose(n);
	}
	/* the page's root: build/wasm32/root */
	bind("/root/wasm32root", "/", MAFTER);
	bind("/root/wasm32root/bin", "/bin", MAFTER);

	prog = getenv("WASM32PROG");
	if(prog == nil || *prog == 0)
		prog = "rc";
	if(getenv("WASM32DEBUG") != nil)
		h3debug = atoi(getenv("WASM32DEBUG"));
	if(h3debug > 3)
		h3jdebug();
	p = mallocz(sizeof *p, 1);
	p->pid = newpid();
	p->proc = p;
	p->name = strdup(prog);
	p->prog = p->name;
	/* arguments: WASM32ARGV, separated by \x1f */
	p->argv = mallocz(Maxargs*sizeof(char*), 1);
	p->argv[p->argc++] = strdup(prog);
	args = getenv("WASM32ARGV");
	if(args != nil && *args) {
		args = strdup(args);
		for(a = args; a != nil && p->argc < Maxargs-1; a = e) {
			e = strchr(a, 0x1f);
			if(e != nil)
				*e++ = 0;
			p->argv[p->argc++] = strdup(a);
		}
		free(args);
	}
	path = smprint("/bin/%s", prog);
	n = p->argc;
	p->img = h3load(path, &n, &p->argv, 0);
	p->argc = n;
	free(path);
	if(p->img == nil) {
		print("host3: /bin/%s: %r\n", prog);
		h3log(prog, 1, "not found");
		for(;;)
			osmsleep(1000000);
	}
	p->mode = Hnew;
	kproc(prog, h3proc, p);
	for(;;)
		osmsleep(1000000);
}

void
cpumain(int argc, char **argv)
{
	USED(argc);
	USED(argv);
	guimain();
}
