#include "../port/portfns.h"
#include "platform.h"

/*
 * wasm32: what port/ wants of the machine and portfns.h does not declare
 */
void	coherence(void);		/* 3l's: atomic.fence */
int	tas(void*);
int	cmpswap(long*, long, long);
void	idlehands(void);
void	procsave(Proc*);
void	procrestore(Proc*);
void	procsetup(Proc*);
void	procfork(Proc*);
void	evenaddr(uintptr);
void	validalign(uintptr, unsigned);
#define	kmapinval()
#define	kmap(p)		((KMap*)(p)->pa)
#define	kunmap(k)	USED(k)
void	splx(int);
void	cycles(uvlong*);
void	mmuinit(void);
_Noreturn void	clockinit(void);
int	userureg(Ureg*);
char*	getconf(char*);
