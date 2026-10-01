/*
 * #t: the wasm32 platform's serial port, eia0 - a byte pipe to the page
 * (host3js.c: eiaout to it, what it sends in eia0's ring).  Tests talk to
 * a program, rc say, through it as through a machine's serial console
 * (tools/test-wasmapp SERIAL=1): text, not screenshots.  The kernel's
 * part is this; the platform's carries over to sys/src/9/wasm32.
 */
#include	"u.h"
#include	"lib.h"
#include	"dat.h"
#include	"fns.h"
#include	"error.h"

enum
{
	Qdir,
	Qdata,
	Qctl,
	Qstat,

	Nring	= 8192,
};

/* what the page sends: it writes w, we read r */
typedef struct Ring Ring;
struct Ring
{
	int	r;
	int	w;
	uchar	b[Nring];
};

extern	void	eiaplatform(Ring*);	/* host3js.c */
extern	void	eiaout(void*, int);

static	Ring	*rx;
static	QLock	eiarlock;

static Dirtab uartdir[] =
{
	".",		{Qdir, 0, QTDIR},	0,	DMDIR|0555,
	"eia0",		{Qdata},		0,	0666,
	"eia0ctl",	{Qctl},			0,	0666,
	"eia0status",	{Qstat},		0,	0444,
};

static void
uartinit(void)
{
	rx = mallocz(sizeof *rx, 1);
	eiaplatform(rx);
}

static Chan*
uartattach(char *spec)
{
	return devattach('t', spec);
}

static Walkqid*
uartwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, uartdir, nelem(uartdir), devgen);
}

static int
uartstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, uartdir, nelem(uartdir), devgen);
}

static Chan*
uartopen(Chan *c, int omode)
{
	return devopen(c, omode, uartdir, nelem(uartdir), devgen);
}

static void
uartclose(Chan *c)
{
	USED(c);
}

static long
uartread(Chan *c, void *va, long n, vlong off)
{
	uchar *p;
	long m;
	int w;

	switch((long)c->qid.path) {
	case Qdir:
		return devdirread(c, va, n, uartdir, nelem(uartdir), devgen);
	case Qstat:
		return readstr(off, va, n, "b115200 c0 d0 e0 l8 m0 p0 r0 s1 i0\n");
	case Qctl:
		return 0;
	case Qdata:
		/* what the page sent, at least a byte: wait for it */
		qlock(&eiarlock);
		while((w = __atomic_load_n(&rx->w, __ATOMIC_SEQ_CST)) == rx->r)
			osmsleep(5);
		p = va;
		for(m = 0; m < n && rx->r != w; m++) {
			p[m] = rx->b[rx->r % Nring];
			rx->r++;
		}
		__atomic_store_n(&rx->r, rx->r, __ATOMIC_SEQ_CST);
		qunlock(&eiarlock);
		return m;
	}
	error(Egreg);
	return -1;
}

static long
uartwrite(Chan *c, void *va, long n, vlong off)
{
	USED(off);
	switch((long)c->qid.path) {
	case Qctl:
		return n;	/* b115200, and the like: a pipe has no speed */
	case Qdata:
		eiaout(va, n);
		return n;
	}
	error(Eperm);
	return -1;
}

Dev uartdevtab = {
	't',
	"uart",

	devreset,
	uartinit,
	devshutdown,
	uartattach,
	uartwalk,
	uartstat,
	uartopen,
	devcreate,
	uartclose,
	uartread,
	devbread,
	uartwrite,
	devbwrite,
	devremove,
	devwstat,
};
