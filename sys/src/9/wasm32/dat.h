/*
 * wasm32: the machine's types for port/ (docs/architecture.md, phase C).
 * A CPU is a Worker, a Proc runs on one of its own; no MMU, no FPU state
 * to save (WebAssembly keeps it), no interrupts (the platform's events are
 * kprocs' wakeups).
 */
typedef struct Conf	Conf;
typedef struct Confmem	Confmem;
typedef struct FPsave	FPsave;
typedef struct PFPU	PFPU;
typedef struct Label	Label;
typedef struct Mach	Mach;
typedef struct Page	Page;
typedef struct PMMU	PMMU;
typedef struct Proc	Proc;
typedef vlong		Tval;
typedef struct Ureg	Ureg;

#pragma incomplete Ureg

#define MAXSYSARG	5	/* for mount(fd, afd, mpt, flag, arg) */
#define AOUT_MAGIC	(0)	/* no a.out: WebAssembly modules */

struct Label		/* setlabel, gotolabel: 3l's setjmp, longjmp */
{
	uintptr	sp;
	uintptr	pc;
};

struct FPsave
{
	int	unused;
};

struct PFPU
{
	int	fpstate;
};

enum
{
	FPinit,
	FPactive,
	FPinactive,
	FPnotify = 0x100,
};

struct Confmem
{
	uintptr	base;
	ulong	npage;
	uintptr	kbase;
	uintptr	klimit;
};

struct Conf
{
	ulong	nmach;
	ulong	nproc;
	ulong	monitor;
	ulong	npage;
	ulong	upages;
	ulong	nimage;
	ulong	nswap;
	int	nswppo;
	ulong	copymode;
	ulong	ialloc;
	ulong	pipeqsize;
	int	nuart;
	Confmem	mem[4];
};

#define NCOLOR 1
struct PMMU
{
	long	workergone;	/* 1: no Worker is on its KSTACK any more (proc.c, platform.js) */
};

#define	inittxtflush(p)
#define	settxtflush(p,c)

#include "../port/portdat.h"

struct Mach
{
	int	machno;			/* 0: to port/, one CPU (proc.c) */
	uintptr	splpc;
	int	splhigh;		/* no interrupts here, but port/ keeps the level (ilock) */
	Proc*	proc;			/* current process on this processor */
	PMach;
	uintptr	stack[1];
};

typedef void KMap;
#define	VA(k)		((void*)k)

struct
{
	char	machs[MAXMACH];
	int	exiting;
}active;

extern Mach*	machp[MAXMACH];	/* machp[0]: mach0, the boot Worker's, the clock's; other Workers' m are not here */
#define	MACHP(n)	(machp[n])

extern register Mach* m;	/* registers 2, 3: each Worker's own (3c, 3l) */
extern register Proc* up;
