#include <u.h>
#include <libc.h>
#include "platform.h"

/*
 * Plan2001's wasm32 kernel (docs/architecture.md, phase C): the machine
 * starts here, on the platform's first Worker (platform.js).  C1: the
 * platform's primitives - #t/eia0 out, labels (setlabel, gotolabel: 3l's
 * WebAssembly exceptions), a kproc on a Worker of its own, sleep and
 * wakeup (Atomics) - before port/ comes in (C2).
 */
typedef struct Label Label;
struct Label
{
	ulong	sp;
	ulong	pc;
};
extern	int	setlabel(Label*);
extern	void	gotolabel(Label*);

static void
kprint(char *fmt, ...)
{
	char buf[256], *e;
	va_list arg;

	va_start(arg, fmt);
	e = vseprint(buf, buf+sizeof buf, fmt, arg);
	va_end(arg);
	eiaout(buf, e - buf);
}

static	long	ready;
static	long	forever;
static	char	kstack[64*1024];
static	Label	errlab;

static void
kproc1(void *a)
{
	kprint("kproc %s: on a Worker of its own\n", (char*)a);
	ready = 1;
	pwake(&ready, 1);
	for(;;)
		pwait(&forever, 0, -1);
}

static int
deep(int n)
{
	if(n == 0)
		gotolabel(&errlab);
	return deep(n-1) + 1;
}

void
main(void)
{
	vlong t;

	kprint("Plan2001 wasm32 kernel\n");

	if(setlabel(&errlab))
		kprint("gotolabel: back at setlabel\n");
	else {
		kprint("setlabel\n");
		deep(10);
	}

	newproc(kproc1, "one", kstack+sizeof kstack);
	while(ready == 0)
		pwait(&ready, 0, 1000);
	kprint("woken by kproc one\n");

	t = pnsec();
	kprint("time %s\n", t > 1000000000LL*1000000000LL ? "since 2001" : "wrong");
	phalt("C1 done");
}
