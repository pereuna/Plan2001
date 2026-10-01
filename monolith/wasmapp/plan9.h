/*
 * What 9front's libc.h has and drawterm's lib.h does not, for
 * Plan 9 programs on drawterm's kernel (included before anything else,
 * wasmapp.mk -include).
 */
#include <time.h>	/* first: localtime is renamed below, not in it */
#ifndef PI
#define	PI	3.14159265358979323846
#endif
typedef struct Tm Tm;
#define	localtime	p9localtime
extern	Tm*	p9localtime(long);
