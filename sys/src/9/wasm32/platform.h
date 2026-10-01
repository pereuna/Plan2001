/*
 * wasm32: the platform's functions (platform.js) - the kernel calls them
 * as any C function, its arguments in memory; 3l -k imports them
 */
void	eiaout(void*, int);		/* #t/eia0: bytes to the page */
int	eiain(void*, int);		/* and from it, what has come */
void	newproc(void (*)(void*), void*, void*);	/* fn(arg) on a Worker of its own, its stack's top */
int	pwait(long*, long, long);	/* Atomics.wait(addr, val, ms; -1 for ever): 0 woken, 1 timed out, 2 not val */
int	pwake(long*, int);		/* Atomics.notify(addr, n) */
vlong	pnsec(void);			/* nanoseconds since 1970 */
void	phalt(char*);			/* the machine stops: the page says so */
