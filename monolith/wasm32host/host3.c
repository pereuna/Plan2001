/*
 * Plan2001 wasm32 processes (3c, 3l; docs/wasm32.md) under drawterm's
 * kernel in the browser: docs/architecture.md, step A.  A process is a
 * kproc - a Worker - that runs the program's module; its one import,
 * plan9.syscall(number, args), comes here as h3syscall and becomes
 * drawterm's sys* calls.  The program's memory is its own: what a call
 * reads or writes there goes through h3in and h3out (copyin, copyout),
 * which the platform (host3js.c, JavaScript) does, as DMA.
 *
 * The page: ?wasm=host3&prog=NAME runs w3/NAME.wasm (build/wasm32/bin)
 * with drawterm's console as its 0, 1, 2 and #i, #m at /dev.
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

/* 9front's /sys/src/libc/9syscall/sys.h */
enum {
	SYSR1 = 0, _ERRSTR, BIND, CHDIR, CLOSE, DUP, ALARM, EXEC, EXITS, _FSESSION,
	FAUTH, _FSTAT, SEGBRK, _MOUNT, OPEN, _READ, OSEEK, SLEEP, _STAT, RFORK,
	_WRITE, PIPE, CREATE, FD2PATH, BRK_, REMOVE, _WSTAT, _FWSTAT, NOTIFY, NOTED,
	SEGATTACH, SEGDETACH, SEGFREE, SEGFLUSH, RENDEZVOUS, UNMOUNT, _WAIT,
	SEMACQUIRE, SEMRELEASE, SEEK, FVERSION, ERRSTR, STAT, FSTAT, WSTAT, FWSTAT,
	MOUNT, AWAIT, PREAD = 50, PWRITE, TSEMACQUIRE, _NSEC,
};

/* the platform (host3js.c) */
extern	int	h3in(void*, ulong, int);
extern	int	h3out(ulong, void*, int);
extern	int	h3strlen(ulong, int);
extern	int	h3brk(ulong);
extern	void	h3exit(char*);
extern	int	h3run(char*, int, char**, vlong*, char*, int);
extern	void	h3log(char*, int, char*);

/* each process a thread of its own */
static	__thread	vlong	h3ret;
static	__thread	char	h3exitmsg[ERRMAX];

/* the program's string at u, in our memory (free it) */
static	char	*h3bin = "w3/";

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

static vlong
h3sys(int n, ulong a)
{
	ulong v[6];
	char *s, *s2, *s3, *e;
	uchar *buf;
	int fd[2];
	vlong r, off;

	if(h3in(v, a, sizeof v) < 0)
		return bad();
	s = s2 = s3 = nil;
	buf = nil;
	r = -1;
	switch(n) {
	default:
		werrstr("system call %d not here (wasm32)", n);
		break;

	case EXITS:
		s = ustr(v[0]);
		strecpy(h3exitmsg, h3exitmsg+sizeof h3exitmsg, s != nil ? s : "");
		h3exit(h3exitmsg);
		r = 0;
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

	case RFORK:
		/* the name space and the like: one proc of its own already */
		if(v[0] & (1<<4)) {	/* RFPROC */
			werrstr("rfork RFPROC not yet on wasm32");
			break;
		}
		r = 0;
		break;

	case ALARM:
	case NOTIFY:
	case NOTED:
		r = 0;
		break;

	case AWAIT:
		werrstr("no living children");
		break;
	}
	free(s);
	free(s2);
	free(s3);
	free(buf);
	return r;
}

void
h3syscall(int n, ulong a)
{
	h3ret = h3sys(n, a);
}

static void
h3proc(void *v)
{
	char *prog, *url, *argv[2];
	int r;

	prog = v;
	url = smprint("%s%s.wasm", h3bin, prog);
	argv[0] = prog;
	argv[1] = nil;
	h3exitmsg[0] = 0;
	r = h3run(url, 1, argv, &h3ret, h3exitmsg, sizeof h3exitmsg);
	print("\n%s: %s\n", prog, r < 0 ? h3exitmsg : r ? h3exitmsg : "exited");
	h3log(prog, r != 0, h3exitmsg);
	free(url);
}

void
cpubody(void)
{
	char *prog;

	if(bind("#i", "/dev", MBEFORE) < 0)
		panic("bind #i: %r");
	if(bind("#m", "/dev", MBEFORE) < 0)
		panic("bind #m: %r");
	if(getenv("WASM32BIN") != nil)
		h3bin = strdup(getenv("WASM32BIN"));
	prog = getenv("WASM32PROG");
	if(prog == nil || *prog == 0)
		prog = "hello";
	kproc(prog, h3proc, strdup(prog));
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
