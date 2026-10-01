/*
 * wasm32: the platform's functions (platform.js) - the kernel calls them
 * as any C function, its arguments in memory; 3l -k imports them
 */
void	eiaout(void*, int);		/* #t/eia0: bytes to the page */
void	eiaring(void*);			/* and from it: the page writes into this ring (uartwasm32.c) */
void	platnewproc(void (*)(void*), void*, void*);	/* fn(arg) on a Worker of its own, its stack's top */
int	platwait(long*, long, long);	/* Atomics.wait(addr, val, ms; -1 for ever): 0 woken, 1 timed out, 2 not val */
int	platwake(long*, int);		/* Atomics.notify(addr, n) */
vlong	platnsec(void);			/* nanoseconds since 1970 */
void	platrandom(void*, ulong);	/* crypto.getRandomValues */
void	platlog(char*);			/* early, before #t/eia0: on the page's console (KLOG) */
void	plathalt(char*);			/* the machine stops: the page says so */

/* the program on this proc's Worker: its own module and memory (trap.c, sysproc.c) */
int	platexec(void*, long, void*, long, int);	/* the next program: its module's bytes, argv's strings, argc; -1 not a module */
_Noreturn void	platuser(vlong (*)(int, ulong));	/* run it, and each one exec makes next; its system calls to the function */
int	platcopyin(void*, ulong, long);	/* from the program's memory: -1 not there */
int	platcopyout(ulong, void*, long);	/* to it */
long	platustrlen(ulong, long);		/* a string's length there, at most the second; -1 none */
int	platbrk(ulong);			/* its memory to the address at least: -1 can not */
void	platfork(Proc*, void (*)(Proc*), ulong);	/* it unwinds, its memory the child's, ready(child), both rewind (pid, 0) */
long	platbootfs(void*, long);	/* the files the page gave: its size, the archive into the buffer */
long	platbootargs(void*, long);	/* what init runs: argv's strings, each with its 0 */
