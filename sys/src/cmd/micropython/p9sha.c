/*
 * SHA-256 from libsec, for p9space.c: in a file of its own, apart from
 * MicroPython's headers (mp.h's and libsec.h's names).
 */
#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>

void
p9sha256(uchar *p, ulong n, uchar *digest)
{
	sha2_256(p, n, digest, nil);
}
