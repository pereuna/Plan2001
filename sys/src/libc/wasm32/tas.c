#include <u.h>
#include <libc.h>

/*
 * wasm32: one thread for now (threads: atomics on shared memory)
 */
int
_tas(int *p)
{
	int v;

	v = *p;
	*p = 0xdeadead;
	return v;
}
