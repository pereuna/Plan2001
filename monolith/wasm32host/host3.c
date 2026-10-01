/*
 * Plan2001 wasm32 processes (3c, 3l; docs/wasm32.md) under drawterm's
 * kernel in the browser: docs/architecture.md, step A.  A process is a
 * kproc - a Worker - that runs the program's module; its one import,
 * plan9.syscall(number, args), comes here as h3syscall and becomes
 * drawterm's sys* calls.  The program's memory is its own: what a call
 * reads or writes there goes through h3in and h3out (copyin, copyout),
 * which the platform (host3js.c, JavaScript) does, as DMA.
 *
 * fork: 3l made the program's stack unwindable; the platform unwinds it
 * into the program's memory, which comes here as a copy for the child -
 * a new kproc - and both rewind.  exec: a program from the name space
 * (#! too).  The page loads build/wasm32/root into #U (/root/wasm32root,
 * bound on / and /bin); ?wasm=host3&prog=NAME&arg=... runs /bin/NAME.
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

/* the platform (host3js.c) */
extern	int	h3in(void*, ulong, int);
extern	int	h3out(ulong, void*, int);
extern	int	h3strlen(ulong, int);
extern	int	h3brk(ulong);
extern	void	h3stop(int);
extern	void	h3unwind(int);
extern	int	h3run(int, uchar*, int, int, char**, uchar*, int, ulong, vlong*, vlong*, int*, char*, int);
extern	void	h3log(char*, int, char*);
extern	void	h3trace(char*);

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

typedef struct H3 H3;
struct H3
{
	int	pid;
	H3	*parent;
	int	nowait;
	char	*name;

	/* children's ends, for await */
	Lock	lk;
	Rendez	r;
	char	*wait[Nwait];
	int	nwait;
	int	nchild;

	/* the program, and how h3run starts it next */
	Img	*img;
	int	argc;
	char	**argv;
	int	mode;
	uchar	*snap;
	long	nsnap;
	ulong	asptr;
	vlong	asret;

	Img	*eimg;	/* exec's */
	int	eargc;
	char	**eargv;
	char	exitmsg[ERRMAX];
};

/* each process a thread of its own */
static	__thread	H3	*h3;
static	__thread	vlong	h3ret;
static	Lock	pidlock;
static	int	pids;

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

static vlong
bad(void)
{
	werrstr("bad address in system call");
	return -1;
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

static int
havewait(void *v)
{
	H3 *p;

	p = v;
	return p->nwait > 0 || p->nchild == 0;
}

/* a child's end, for its parent's await: 'pid utime stime rtime msg' */
static void
h3end(H3 *p)
{
	H3 *q;
	char *w;

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

static long
h3await(ulong u, long n)
{
	H3 *p;
	char *w;
	long m;

	p = h3;
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
	if(m > n)
		m = n;
	if(h3out(u, w, m) < 0) {
		free(w);
		return bad();
	}
	free(w);
	return m;
}

static void	h3proc(void*);

/* fork: p unwound, snap its memory (ours now, malloc'd) */
static H3*
h3child(H3 *p, int flags, uchar *snap, long nsnap, ulong asptr)
{
	H3 *c;
	Fgrp *f;
	Rgrp *rg;

	c = mallocz(sizeof *c, 1);
	lock(&pidlock);
	c->pid = ++pids;
	unlock(&pidlock);
	c->parent = p;
	c->nowait = (flags & RFNOWAIT) != 0;
	c->name = strdup(p->name);
	incref(&p->img->ref);
	c->img = p->img;
	c->mode = Hchild;
	c->snap = snap;
	c->nsnap = nsnap;
	c->asptr = asptr;
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
h3exec(ulong uname, ulong uargv)
{
	char *name, **argv;
	ulong a;
	int argc;
	Img *img;

	if((name = ustr(uname)) == nil)
		return bad();
	argv = mallocz(Maxargs*sizeof(char*), 1);
	for(argc = 0; argc < Maxargs-1; argc++) {
		if(h3in(&a, uargv + 4*argc, 4) < 0) {
			freeargv(argc, argv);
			free(name);
			return bad();
		}
		if(a == 0)
			break;
		if((argv[argc] = ustr(a)) == nil) {
			freeargv(argc, argv);
			free(name);
			return bad();
		}
	}
	img = h3load(name, &argc, &argv, 0);
	dbg("%d %s exec %s: %s", h3->pid, h3->name, name, img != nil ? "ok" : "failed");
	free(name);
	if(img == nil) {
		freeargv(argc, argv);
		return -1;
	}
	h3->eimg = img;
	h3->eargc = argc;
	h3->eargv = argv;
	h3stop(Stopexec);
	return 0;
}

static vlong
h3rfork(int flags)
{
	Fgrp *f;
	Rgrp *rg;

	if(flags & RFMEM) {
		werrstr("rfork RFMEM not yet on wasm32");
		return -1;
	}
	if(flags & RFPROC) {
		/* the stack unwinds; h3proc makes the child */
		h3unwind(flags);
		return 0;
	}
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

static vlong
h3sys(int n, ulong a)
{
	ulong v[6];
	char *s, *s2, *e;
	uchar *buf;
	int fd[2];
	vlong r, off;

	if(h3in(v, a, sizeof v) < 0)
		return bad();
	s = s2 = nil;
	buf = nil;
	r = -1;
	switch(n) {
	default:
		werrstr("system call %d not here (wasm32)", n);
		break;

	case EXITS:
		s = ustr(v[0]);
		dbg("%d %s exits '%s'", h3->pid, h3->name, s != nil ? s : "");
		strecpy(h3->exitmsg, h3->exitmsg+sizeof h3->exitmsg, s != nil ? s : "");
		h3stop(Stopexit);
		r = 0;
		break;

	case EXEC:
		r = h3exec(v[0], v[1]);
		break;

	case RFORK:
		r = h3rfork(v[0]);
		break;

	case AWAIT:
		dbg("%d %s await (%d children)", h3->pid, h3->name, h3->nchild);
		r = h3await(v[0], v[1]);
		dbg("%d %s await: %lld", h3->pid, h3->name, r);
		break;

	case OPEN:
		if((s = ustr(v[0])) == nil)
			return bad();
		r = sysopen(s, v[1]);
		break;

	case CREATE:
		if((s = ustr(v[0])) == nil)
			return bad();
		r = syscreate(s, v[1], v[2]);
		break;

	case CLOSE:
		r = sysclose(v[0]);
		break;

	case DUP:
		r = sysdup(v[0], v[1]);
		break;

	case PREAD:
		if((long)v[2] < 0)
			return bad();
		buf = smalloc(v[2]+1);
		off = (uvlong)v[3] | (uvlong)v[4]<<32;
		r = syspread(v[0], buf, v[2], off);
		if(r > 0 && h3out(v[1], buf, r) < 0)
			r = bad();
		break;

	case PWRITE:
		if((long)v[2] < 0)
			return bad();
		buf = smalloc(v[2]+1);
		if(h3in(buf, v[1], v[2]) < 0) {
			r = bad();
			break;
		}
		off = (uvlong)v[3] | (uvlong)v[4]<<32;
		r = syspwrite(v[0], buf, v[2], off);
		break;

	case SEEK:
		off = (uvlong)v[1] | (uvlong)v[2]<<32;
		r = sysseek(v[0], off, v[3]);
		break;

	case FSTAT:
	case STAT:
		if((long)v[2] < 0)
			return bad();
		buf = smalloc(v[2]+1);
		if(n == FSTAT)
			r = sysfstat(v[0], buf, v[2]);
		else {
			if((s = ustr(v[0])) == nil)
				return bad();
			r = sysstat(s, buf, v[2]);
		}
		if(r > 0 && h3out(v[1], buf, r) < 0)
			r = bad();
		break;

	case FWSTAT:
	case WSTAT:
		if((long)v[2] < 0)
			return bad();
		buf = smalloc(v[2]+1);
		if(h3in(buf, v[1], v[2]) < 0) {
			r = bad();
			break;
		}
		if(n == FWSTAT)
			r = sysfwstat(v[0], buf, v[2]);
		else {
			if((s = ustr(v[0])) == nil)
				return bad();
			r = syswstat(s, buf, v[2]);
		}
		break;

	case BIND:
		s = ustr(v[0]);
		s2 = ustr(v[1]);
		if(s == nil || s2 == nil)
			return bad();
		r = sysbind(s, s2, v[2]);
		break;

	case MOUNT:
		s = ustr(v[2]);
		s2 = ustr(v[4]);
		if(s == nil)
			return bad();
		r = sysmount(v[0], v[1], s, v[3], s2);
		break;

	case UNMOUNT:
		s = ustr(v[0]);
		s2 = ustr(v[1]);
		if(s2 == nil)
			return bad();
		r = sysunmount(s, s2);
		break;

	case CHDIR:
		if((s = ustr(v[0])) == nil)
			return bad();
		r = syschdir(s);
		break;

	case REMOVE:
		if((s = ustr(v[0])) == nil)
			return bad();
		r = sysremove(s);
		break;

	case PIPE:
		r = syspipe(fd);
		if(r >= 0 && h3out(v[0], fd, sizeof fd) < 0)
			r = bad();
		break;

	case FD2PATH:
		if((long)v[2] <= 0)
			return bad();
		buf = smalloc(v[2]+1);
		r = fd2path(v[0], (char*)buf, v[2]);
		if(r >= 0 && h3out(v[1], buf, strlen((char*)buf)+1) < 0)
			r = bad();
		break;

	case ERRSTR:
		/* swap the program's buffer and ours */
		if((long)v[1] <= 0)
			return bad();
		if(v[1] > ERRMAX)
			v[1] = ERRMAX;
		buf = smalloc(v[1]+1);
		if(h3in(buf, v[0], v[1]) < 0)
			return bad();
		buf[v[1]-1] = 0;
		e = up->syserrstr;
		if(h3out(v[0], e, strlen(e)+1) < 0)
			return bad();
		strecpy(e, e+ERRMAX, (char*)buf);
		r = 0;
		break;

	case BRK_:
		r = h3brk(v[0]);
		if(r < 0)
			werrstr("no memory");
		break;

	case SLEEP:
		if((long)v[0] > 0)
			osmsleep(v[0]);
		r = 0;
		break;

	case _NSEC:
		r = nsec();
		break;

	case ALARM:
	case NOTIFY:
	case NOTED:
		r = 0;
		break;
	}
	free(s);
	free(s2);
	free(buf);
	return r;
}

void
h3syscall(int n, ulong a)
{
	ulong v[4];

	if(h3debug > 2 && h3in(v, a, sizeof v) == 0)
		dbg("%d %s sys %d (%lux %lux %lux %lux) ...", h3->pid, h3->name, n, v[0], v[1], v[2], v[3]);
	h3ret = h3sys(n, a);
	if(h3debug > 1 && n != EXITS && h3in(v, a, sizeof v) == 0)
		dbg("%d %s sys %d (%lux %lux %lux %lux) = %lld%s%s", h3->pid, h3->name, n,
			v[0], v[1], v[2], v[3], h3ret, h3ret < 0 ? " " : "", h3ret < 0 ? up->syserrstr : "");
}

static void
h3proc(void *v)
{
	H3 *p, *c;
	int r, out[5];
	char msg[ERRMAX];

	p = v;
	h3 = p;
	for(;;) {
		msg[0] = 0;
		r = h3run(p->mode, p->img->p, p->img->n, p->argc, p->argv,
			p->snap, p->nsnap, p->asptr, &p->asret, &h3ret, out, msg, sizeof msg);
		free(p->snap);
		p->snap = nil;
		if(r < 0) {
			print("%s: %s\n", p->name, msg);
			strecpy(p->exitmsg, p->exitmsg+sizeof p->exitmsg, msg);
			break;
		}
		if(out[0] == Hforked) {
			c = h3child(p, out[1], (uchar*)(uintptr)out[2], out[3], out[4]);
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
			free(p->name);
			p->name = strdup(p->argc > 0 ? p->argv[0] : "?");
			p->mode = Hnew;
			continue;
		}
		break;
	}
	dbg("%d %s ends '%s'", p->pid, p->name, p->exitmsg);
	/* as pexit: its files closed (the ends of its pipes), its groups let go */
	if(up->fgrp != nil) {
		closefgrp(up->fgrp);
		up->fgrp = nil;
	}
	if(up->rgrp != nil) {
		closergrp(up->rgrp);
		up->rgrp = nil;
	}
	if(p->parent == nil)
		h3log(p->name, p->exitmsg[0] != 0, p->exitmsg);
	h3end(p);
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
	/* the page's root: build/wasm32/root */
	bind("/root/wasm32root", "/", MAFTER);
	bind("/root/wasm32root/bin", "/bin", MAFTER);

	prog = getenv("WASM32PROG");
	if(prog == nil || *prog == 0)
		prog = "rc";
	if(getenv("WASM32DEBUG") != nil)
		h3debug = atoi(getenv("WASM32DEBUG"));
	p = mallocz(sizeof *p, 1);
	p->pid = ++pids;
	p->name = strdup(prog);
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
