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
