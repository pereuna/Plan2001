/*
 * host3's devices: drawterm's (kern/devtab.c) and #t, the wasm32 platform's
 * serial port (devuart3.c), and #d (devdup3.c); linked before libkern, in its place
 */
#include "u.h"
#include "lib.h"
#include "dat.h"
#include "fns.h"
#include "error.h"

extern Dev consdevtab;
extern Dev rootdevtab;
extern Dev pipedevtab;
extern Dev ssldevtab;
extern Dev tlsdevtab;
extern Dev mousedevtab;
extern Dev drawdevtab;
extern Dev ipdevtab;
extern Dev fsdevtab;
extern Dev mntdevtab;
extern Dev lfddevtab;
extern Dev audiodevtab;
extern Dev kbddevtab;
extern Dev cmddevtab;
extern Dev envdevtab;
extern Dev uartdevtab;
extern Dev dupdevtab;
#ifdef NINEPTERM
extern Dev jobsdevtab;
#endif

Dev *devtab[] = {
	&rootdevtab,
	&consdevtab,
	&pipedevtab,
	&ssldevtab,
	&tlsdevtab,
	&mousedevtab,
	&drawdevtab,
	&ipdevtab,
	&fsdevtab,
	&mntdevtab,
	&lfddevtab,
	&audiodevtab,
	&kbddevtab,
	&cmddevtab,
	&envdevtab,
	&uartdevtab,
	&dupdevtab,
#ifdef NINEPTERM
	&jobsdevtab,
#endif
	0
};

