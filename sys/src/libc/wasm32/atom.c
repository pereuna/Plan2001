#include <u.h>
#include <libc.h>

/*
 * wasm32: one thread for now
 */
long
ainc(long *p)
{
	return ++*p;
}

long
adec(long *p)
{
	return --*p;
}

int
cas(int *p, int ov, int nv)
{
	if(*p != ov)
		return 0;
	*p = nv;
	return 1;
}

int
casp(void **p, void *ov, void *nv)
{
	if(*p != ov)
		return 0;
	*p = nv;
	return 1;
}

int
casl(ulong *p, ulong ov, ulong nv)
{
	if(*p != ov)
		return 0;
	*p = nv;
	return 1;
}

void
coherence(void)
{
}
