#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"
#include	"../../libc/9syscall/sys.h"

/*
 * wasm32's system calls (docs/architecture.md, phase C).  A program is
 * a module of its own on its proc's Worker, its memory its own; its
 * plan9.syscall(n, a) comes here (platform.js, platuser), a the address
 * of its arguments - 4-byte words, a vlong two - in its memory.  The
 * arguments are copied into the kernel and so are the strings and
 * buffers they point at, so that 9front's sys* (sysfile.c) and the
 * devices see the kernel's addresses only; what a call wrote goes back
 * after it.
 *
 * A call's args say what its words are:
 *	i	a word
 *	v	a vlong: two words
 *	s	a string the call reads; S the same or nil
 *	r	a buffer the call reads, and the next word its length
 *	w	a buffer the call writes, its length next: as much copied out
 *		as the call returns
 *	W	a buffer the call reads and writes, its length next: all of it
 *		copied out
 *	p	an int[2] the call writes (pipe)
 */
enum
{
	YIELD	= 105,	/* 3l's preemption point */
	Nwords	= 8,
	Nbufs	= 4,
	Maxbuf	= 16*1024*1024,
	Maxstr	= 8192,
};

typedef uintptr Syscall(va_list);
Syscall	sysbind, syschdir, sysclose, sysdup, sysalarm, sysexec, sysexits,
	sysfauth, sysopen, syssleep, sysrfork, syspipe, syscreate, sysfd2path,
	sysbrk_, sysremove, sysnotify, sysnoted, sysunmount, sysfversion,
	syserrstr, sysstat, sysfstat, syswstat, sysfwstat, sysmount, sysawait,
	syspread, syspwrite, sysrendezvous, sysyield;
vlong	sysseekv(va_list);
vlong	sysnsecv(va_list);

typedef struct Sys Sys;
struct Sys
{
	uintptr	(*f)(va_list);
	vlong	(*fv)(va_list);		/* calls with a vlong result */
	char	*args;
};

static Sys systab[] =
{
[BIND]		sysbind, nil, "ssi",
[CHDIR]		syschdir, nil, "s",
[CLOSE]		sysclose, nil, "i",
[DUP]		sysdup, nil, "ii",
[ALARM]		sysalarm, nil, "i",
[EXEC]		sysexec, nil, "si",
[EXITS]		sysexits, nil, "S",
[FAUTH]		sysfauth, nil, "is",
[OPEN]		sysopen, nil, "si",
[SLEEP]		syssleep, nil, "i",
[RFORK]		sysrfork, nil, "i",
[PIPE]		syspipe, nil, "p",
[CREATE]	syscreate, nil, "sii",
[FD2PATH]	sysfd2path, nil, "iW",
[BRK_]		sysbrk_, nil, "i",
[REMOVE]	sysremove, nil, "s",
[NOTIFY]	sysnotify, nil, "i",
[NOTED]		sysnoted, nil, "i",
[UNMOUNT]	sysunmount, nil, "Ss",
[SEEK]		nil, sysseekv, "ivi",
[FVERSION]	sysfversion, nil, "iiW",
[ERRSTR]	syserrstr, nil, "W",
[STAT]		sysstat, nil, "sw",
[FSTAT]		sysfstat, nil, "iw",
[WSTAT]		syswstat, nil, "sr",
[FWSTAT]	sysfwstat, nil, "ir",
[MOUNT]		sysmount, nil, "iisiS",
[AWAIT]		sysawait, nil, "w",
[PREAD]		syspread, nil, "iwv",
[PWRITE]	syspwrite, nil, "irv",
[_NSEC]		nil, sysnsecv, "",
[RENDEZVOUS]	sysrendezvous, nil, "ii",
[YIELD]		sysyield, nil, "",
};

typedef struct Call Call;
struct Call
{
	ulong	u[Nwords];	/* the program's words */
	ulong	k[Nwords];	/* the kernel's: the va_list */
	struct {
		void	*k;
		ulong	u;
		long	n;
		int	out;	/* w, W, p: copied out */
		int	upto;	/* w: as much as the call returns */
	} b[Nbufs];
	int	nb;
};

static int
nwords(char *a)
{
	int n;

	for(n = 0; *a != 0; a++)
		n += (*a == 'v' || *a == 'r' || *a == 'w' || *a == 'W') ? 2 : 1;
	return n;
}

static void*
callbuf(Call *c, ulong u, long n, int in, int out, int upto)
{
	void *k;

	if(n < 0 || n > Maxbuf)
		error(Etoobig);
	if(c->nb == Nbufs)
		panic("callbuf");
	k = malloc(n+1);
	if(k == nil)
		error(Enomem);
	c->b[c->nb].k = k;
	c->b[c->nb].u = u;
	c->b[c->nb].n = n;
	c->b[c->nb].out = out;
	c->b[c->nb].upto = upto;
	c->nb++;
	if(in && platcopyin(k, u, n) < 0)
		error(Ebadarg);
	((char*)k)[n] = 0;
	return k;
}

static char*
callstr(Call *c, ulong u)
{
	long n;

	n = platustrlen(u, Maxstr);
	if(n < 0)
		error(Ebadarg);
	return callbuf(c, u, n, 1, 0, 0);
}

/* the program's words to the kernel's */
static void
marshal(Call *c, char *a)
{
	int i;

	for(i = 0; *a != 0; a++){
		switch(*a){
		case 'i':
			c->k[i] = c->u[i];
			i++;
			break;
		case 'v':
			c->k[i] = c->u[i];
			c->k[i+1] = c->u[i+1];
			i += 2;
			break;
		case 'S':
			if(c->u[i] == 0){
				c->k[i++] = 0;
				break;
			}
			/* fall through */
		case 's':
			c->k[i] = (ulong)callstr(c, c->u[i]);
			i++;
			break;
		case 'r':
		case 'w':
		case 'W':
			c->k[i] = (ulong)callbuf(c, c->u[i], c->u[i+1], *a != 'w', *a != 'r', *a == 'w');
			c->k[i+1] = c->u[i+1];
			i += 2;
			break;
		case 'p':
			c->k[i] = (ulong)callbuf(c, c->u[i], 2*sizeof(int), 0, 1, 0);
			i++;
			break;
		}
	}
}

/* what the call wrote, back to the program */
static void
unmarshal(Call *c, vlong r)
{
	int i;
	long n;

	for(i = 0; i < c->nb; i++){
		if(!c->b[i].out)
			continue;
		n = c->b[i].n;
		if(c->b[i].upto && r < n)
			n = r;
		if(n > 0 && platcopyout(c->b[i].u, c->b[i].k, n) < 0)
			error(Ebadarg);
	}
}

/* the program's handler is done: noted, or it jumped out (notejmp) */
static void
notedone(void)
{
	up->notified = 0;
}

/*
 * a note for the program, when its call returns: its handler gets it
 * (platform.js); popnote ends the proc if it has none or is in it
 */
static void
usernote(void)
{
	char *msg;

	if(up->nnote == 0)
		return;
	qlock(&up->debug);
	msg = popnote(nil);
	if(msg == nil){
		qunlock(&up->debug);
		return;
	}
	platnote(up->notify, msg, notedone);
	qunlock(&up->debug);
}

/*
 * the program's call (platform.js calls it on the proc's Worker, in
 * the kernel's instance): -1 and the proc's syserrstr on an error
 */
vlong
syscall(int n, ulong a)
{
	Sys *s;
	Call c;
	vlong r;
	char *e;
	int i, nerrlab;

	memset(&c, 0, sizeof c);
	up->insyscall = 1;
	up->scallnr = n;
	nerrlab = up->nerrlab;
	r = -1;
	if(!waserror()){
		if(n < 0 || n >= nelem(systab) || (s = &systab[n])->args == nil){
			pprint("bad sys call number %d\n", n);
			error(Ebadarg);
		}
		if(platcopyin(c.u, a, nwords(s->args)*BY2WD) < 0)
			error(Ebadarg);
		marshal(&c, s->args);
		if(s->fv != nil)
			r = (*s->fv)((va_list)c.k);
		else
			r = (long)(*s->f)((va_list)c.k);
		unmarshal(&c, r);
		poperror();
	}else{
		/* the error is the call's (errstr): 9front's syscall does the same */
		e = up->syserrstr;
		up->syserrstr = up->errstr;
		up->errstr = e;
		r = -1;
	}
	for(i = 0; i < c.nb; i++)
		free(c.b[i].k);
	if(up->nerrlab != nerrlab){
		print("bad errstack [%d]: %d extra\n", n, up->nerrlab - nerrlab);
		up->nerrlab = nerrlab;
	}
	up->insyscall = 0;
	usernote();
	return r;
}
