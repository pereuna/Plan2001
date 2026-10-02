#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	<pool.h>
#include	"../port/error.h"

/*
 * Plan2001's wasm32 kernel (docs/architecture.md, phase C): 9front's
 * port/ on the platform's Workers.  main runs on the first: memory,
 * allocation, procs, devices, the clock, then init - a kproc for now
 * (C2a); user processes come with sysproc (C2b).
 */
Conf	conf;
Mach	mach0;
char	*eve = "glenda";
uchar	*sp;	/* user stack of init proc, unused */

void
confinit(void)
{
	uintptr base, top;

	base = PGROUND((uintptr)end);
	top = 64*MiB;			/* platform.js: the memory's first size */
	conf.nmach = 1;
	conf.nproc = 512;
	conf.mem[0].base = base;
	conf.mem[0].npage = (top - base) / BY2PG;
	conf.npage = conf.mem[0].npage;
	conf.upages = 0;
	/*
	 * the kernel's heap is all of it: programs' memories are their own,
	 * and what passes through the kernel (an exec's module, the screen)
	 * is here.  9front's pools start at 4 and 16 MB; pc64's confinit sets
	 * them from the machine's memory too
	 */
	conf.ialloc = (top - base) / 2;
	mainmem->maxsize = top - base;
	imagmem->maxsize = top - base;
	conf.pipeqsize = 32*1024;
	conf.nuart = 1;
	conf.monitor = 1;	/* the page's screen (screen.c) */
	conf.copymode = 0;
}

Proc	*initp;		/* its end is the machine's (sysexits) */

static uintptr
kcall(uintptr (*f)(va_list), ...)
{
	va_list a;
	uintptr r;

	va_start(a, f);
	r = (*f)(a);
	va_end(a);
	return r;
}

/*
 * C2b: init becomes a program, what the page says (platbootargs): its
 * root #R, the page's files; #c, #t (eia0) in /dev, #e, #s; its files
 * 0, 1, 2 #t/eia0.  Without one: the end of C2a's test.
 */
static void
inituser(void)
{
	char *args, *e, *z, **argv;
	long n;
	int argc;
	Chan *c;

	n = platbootargs(nil, 0);
	if(n <= 0){
		plathalt("C2a done");
		for(;;)
			tsleep(&up->sleep, return0, nil, 1000000);
	}
	/* the page's: each string must end in its 0 */
	args = smalloc(n);
	platbootargs(args, n);
	argv = smalloc((n+1)*sizeof(char*));
	argc = 0;
	for(e = args+n; args < e; args = z+1){
		if((z = memchr(args, 0, e-args)) == nil){
			print("inituser: init's arguments: no 0 at the end\n");
			plathalt("inituser error");
			for(;;)
				tsleep(&up->sleep, return0, nil, 1000000);
		}
		argv[argc++] = args;
	}
	argv[argc] = nil;

	if(waserror()){
		print("inituser: %s\n", up->errstr);
		plathalt("inituser error");
		for(;;)
			tsleep(&up->sleep, return0, nil, 1000000);
	}
	c = namec("#R", Atodir, 0, 0);
	pathclose(c->path);
	c->path = newpath("/");
	cclose(up->slash);
	cclose(up->dot);
	up->slash = c;
	up->dot = cclone(c);
	kcall(sysbind, "#c", "/dev", MAFTER);
	kcall(sysbind, "#t", "/dev", MAFTER);
	kcall(sysbind, "#e", "/env", MREPL|MCREATE);
	kcall(sysbind, "#s", "/srv", MREPL|MCREATE);
	kcall(sysbind, "#p", "/proc", MREPL);
	kcall(sysbind, "#d", "/fd", MREPL);
	kcall(sysbind, "#i", "/dev", MAFTER);
	kcall(sysbind, "#m", "/dev", MAFTER);
	kcall(sysbind, "#b", "/dev", MAFTER);
	kcall(sysbind, "/rc/bin", "/bin", MAFTER);	/* as /lib/namespace: rc's own commands after the machine's */
	kcall(sysopen, "/dev/eia0", OREAD);
	kcall(sysopen, "/dev/eia0", OWRITE);
	kcall(sysopen, "/dev/eia0", OWRITE);
	ksetenv("cputype", "wasm32", 0);
	ksetenv("objtype", "wasm32", 0);
	ksetenv("terminal", "wasm32 browser", 0);
	ksetenv("service", "terminal", 0);
	poperror();
	kproc("alarm", alarmkproc, 0);
	initp = up;
	touser(argv, argc);
}

/*
 * C2a: port/ at work - the console (devcons on #t/eia0), a pipe,
 * #e, errors, sleep with a timeout, the time
 */
static void
initproc(void*)
{
	int fd, p[2];
	char buf[64];
	long n;
	Chan *c;

	up->nerrlab = 0;
	chandevinit();
	print("Plan2001 wasm32 kernel: 9front's port/\n");

	if(waserror()){
		print("initproc: %s\n", up->errstr);
		plathalt("initproc error");
		return;
	}
	/* a name space: #c, #e, #| */
	up->pgrp = newpgrp();
	up->fgrp = dupfgrp(nil);
	up->egrp = smalloc(sizeof(Egrp));
	up->egrp->ref = 1;
	up->rgrp = newrgrp();
	c = namec("#/", Atodir, 0, 0);
	up->slash = c;
	incref(c);
	up->dot = c;
	poperror();

	/* the console, as a file: #c/cons (devcons on #t/eia0) */
	c = namec("#c/cons", Aopen, OWRITE, 0);
	devtab[c->type]->write(c, "written to #c/cons\n", 19, 0);
	cclose(c);

	/* a pipe (#|): written at one end, read at the other */
	c = namec("#|/data", Aopen, ORDWR, 0);
	devtab[c->type]->write(c, "through a pipe", 14, 0);
	cclose(c);
	USED(p);

	/* #e: an environment variable */
	c = namec("#e/sysname", Acreate, OWRITE, 0666);
	devtab[c->type]->write(c, "wasm32", 6, 0);
	cclose(c);
	c = namec("#e/sysname", Aopen, OREAD, 0);
	n = devtab[c->type]->read(c, buf, sizeof buf - 1, 0);
	buf[n] = 0;
	cclose(c);
	print("#e/sysname: %s\n", buf);

	/* an error, caught */
	if(waserror())
		print("caught: %s\n", up->errstr);
	else {
		namec("#c/nonexistent", Aopen, OREAD, 0);
		poperror();
	}

	/* the clock: tsleep's timer */
	{
		Rendez r;
		uvlong t;
		ulong ticks;

		memset(&r, 0, sizeof r);
		t = platnsec();
		ticks = MACHP(0)->ticks;
		tsleep(&r, return0, nil, 100);
		t = (platnsec() - t) / 1000000;
		ticks = MACHP(0)->ticks - ticks;
		print("tsleep 100 ms: %s\n", t >= 90 && t < 1000 ? "ok" : "wrong");
		print("ticks in it: %s\n", ticks >= 5 && ticks <= 100 ? "ok" : "wrong");
	}
	USED(fd);
	inituser();
}

void
main(void)
{
	m = &mach0;
	machp[0] = m;
	m->machno = 0;
	MACHP(0)->ticks = 0;
	active.machs[0] = 1;
	quotefmtinstall();
	confinit();
	xinit();
	printinit();
	timersinit();
	todset(platnsec(), 0, 0);	/* the time: the platform's */
	procinit0();
	screeninit();
	chandevreset();
	mouseinput();
	print("Plan2001 wasm32: %lud pages free\n", conf.npage);
	/* main's Worker has no up: it is CPU 0, the clock (clock.c); the work is a proc's */
	{
		Proc *p;

		p = newproc();
		kstrdup(&p->text, "init");
		kstrdup(&p->user, eve);
		kprocchild(p, (void(*)(void))initproc);
		pidalloc(p);
		ready(p);
	}
	clockinit();
}
