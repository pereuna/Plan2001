/*
 * A Plan 9 program in the browser, on drawterm's kernel (Plan2001,
 * docs: the wasm app host): the program is linked into drawterm.wasm in
 * place of cpu.c, and its system calls are drawterm's (include/user.h:
 * open is sysopen and so on), so it draws on drawterm's own /dev/draw -
 * the browser's canvas - with no CPU server and no network.
 * The program's main is renamed wasmappmain (-Dmain=wasmappmain).
 */
#include <u.h>
#include <libc.h>

extern void	guimain(void);
extern void	wasmappmain(int, char**);

static char *appargv[] = { WASMAPP, nil };

void
cpubody(void)
{
	if(bind("#i", "/dev", MBEFORE) < 0)
		panic("bind #i: %r");
	if(bind("#m", "/dev", MBEFORE) < 0)
		panic("bind #m: %r");
	wasmappmain(1, appargv);
	exits(nil);
}

void
cpumain(int argc, char **argv)
{
	USED(argc);
	USED(argv);
	guimain();
}
