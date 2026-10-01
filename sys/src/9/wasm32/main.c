#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
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
	conf.ialloc = 1*MiB;
	conf.pipeqsize = 32*1024;
	conf.nuart = 1;
	conf.copymode = 0;
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

		memset(&r, 0, sizeof r);
		t = platnsec();
		tsleep(&r, return0, nil, 100);
		t = (platnsec() - t) / 1000000;
		print("tsleep 100 ms: %s\n", t >= 90 && t < 1000 ? "ok" : "wrong");
	}
	USED(fd);
	plathalt("C2a done");
	for(;;)
		tsleep(&up->sleep, return0, nil, 1000000);
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
	procinit0();
	chandevreset();
	print("Plan2001 wasm32: %lud pages free\n", conf.npage);
	/* main's Worker has no up; the work is a proc's */
	{
		Proc *p;

		p = newproc();
		p->mach = mallocz(sizeof(Mach), 1);
		kstrdup(&p->text, "init");
		kstrdup(&p->user, eve);
		kprocchild(p, (void(*)(void))initproc);
		pidalloc(p);
		ready(p);
	}
	clockinit();
}
