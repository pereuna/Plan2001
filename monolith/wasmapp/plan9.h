/*
 * What 9front's libc.h and draw.h have and drawterm's lib.h does not, for
 * Plan 9 programs on drawterm's kernel (included before anything else,
 * wasmapp.mk -include).
 */
#include <time.h>	/* first: localtime is renamed below, not in it */
#ifndef PI
#define	PI	3.14159265358979323846
#endif
typedef struct Image Image;
extern	Image	*screen;	/* libdraw's: initdraw, getwindow */
typedef struct Tm Tm;
#define	localtime	p9localtime
extern	Tm*	p9localtime(long);
/* libdraw's display lock for getwindow: one at a time is enough here */
#define	RWLock	QLock
#define	rlock	qlock
#define	runlock	qunlock
#define	wlock	qlock
#define	wunlock	qunlock
/* access(2) modes */
#ifndef AREAD
#define	AEXIST	0
#define	AEXEC	1
#define	AWRITE	2
#define	AREAD	4
#endif
