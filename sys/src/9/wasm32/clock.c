#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

/*
 * wasm32: the clock is a kproc - no interrupts.  It ticks HZ times a
 * second (MACHP(0)->ticks) and runs the timers (portclock.c's
 * timerintr), sooner when timerset asks for an earlier one.
 */
static	long	clockword;		/* timerset wakes the clock with it */
static	uvlong	nextwhen;

void
timerset(Tval when)
{
	nextwhen = when;
	clockword++;
	platwake(&clockword, 1);
}

static void
clockproc(void*)
{
	uvlong now, tick;
	long ms, seen;

	tick = platnsec();
	for(;;){
		seen = clockword;
		now = platnsec();
		ms = (tick + 1000000000ULL/HZ - now) / 1000000;
		if(nextwhen != 0 && nextwhen > now && (nextwhen - now)/1000000 < ms)
			ms = (nextwhen - now)/1000000;
		if(ms > 0)
			platwait(&clockword, seen, ms);
		now = platnsec();
		while(now >= tick + 1000000000ULL/HZ){
			tick += 1000000000ULL/HZ;
			MACHP(0)->ticks++;
			m->ticks = MACHP(0)->ticks;
		}
		timerintr(nil, 0);
	}
}

void
clockinit(void)
{
	kproc("clock", clockproc, nil);
}
