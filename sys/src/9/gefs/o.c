/*
 * Plan2001: the native O volume (docs/o-volume-format.md).  Stage 2 is
 * only the profile, the flat catalog and plain GE records: no object
 * transactions, FS mounts, 9P or path resolution.  O records are Ko
 * keys in the volume's snapshot tree; opening an existing volume only
 * reads it and never formats.
 */
#include "gefs.h"

static void
okey(char *kbuf, char *name, Key *k)
{
	if(strlen(name) > Keymax-1)
		error("O: record name too long");
	kbuf[0] = Ko;
	memmove(kbuf+1, name, strlen(name));
	k->k = kbuf;
	k->nk = 1+strlen(name);
}

uvlong
ospaceid(Gefs *fs, char *name)
{
	char kbuf[Keymax], xbuf[Kvmax];
	Key k;
	Kvp kv;

	okey(kbuf, name, &k);
	if(!btlookup(&fs->snap, &k, &kv, xbuf, sizeof(xbuf)))
		error("O: no space '%s'", name);
	if(kv.nv != 8)
		error("O: bad space id length for '%s'", name);
	return UNPACK64(kv.v);
}

static int
orecget(Gefs *fs, char *name, void *val, int nval)
{
	char kbuf[Keymax], xbuf[Kvmax];
	Key k;
	Kvp kv;

	okey(kbuf, name, &k);
	if(!btlookup(&fs->snap, &k, &kv, xbuf, sizeof(xbuf)))
		return -1;
	if(kv.nv > nval)
		error("O: record '%s' too long", name);
	memmove(val, kv.v, kv.nv);
	return kv.nv;
}

static void
orecput(Gefs *fs, char *name, void *val, int nval)
{
	char kbuf[Keymax];
	Key k;
	Msg m;

	okey(kbuf, name, &k);
	m.op = Oinsert;
	m.k = k.k;
	m.nk = k.nk;
	m.v = val;
	m.nv = nval;
	qlock(&fs->mutlk);
	if(waserror()){
		qunlock(&fs->mutlk);
		nexterror();
	}
	btupsert(&fs->snap, &m, 1);
	qunlock(&fs->mutlk);
	poperror();
}

/*
 * Open an existing O volume: GE's blocks/tree/recovery, the O profile
 * and version, and the init Space's catalog entry.  An FS volume is
 * rejected here, before any write.
 */
void*
oopen(char *dev, Kchan *c, vlong off, vlong size, int rw)
{
	Gefs *fs;
	uvlong v;

	fs = geopen(dev, c, off, size, rw);
	if(!(fs->flag & Ovolflag))
		error("O: not an O volume (legacy FS profile)");
	v = fs->flag & Ovolvermask;
	if(v != Ovolvers)
		error("O: unsupported volume version %llud", v);
	if(ospaceid(fs, "init") != OspaceInit)
		error("O: init Space id mismatch");
	return fs;
}

/* Stage 2's QEMU gate: a persistent plain O record, no FS involved. */
void
oprobe(void *v)
{
	char vbuf[8];
	Gefs *fs;
	uvlong id, seq;
	int n;

	fs = v;
	id = ospaceid(fs, "init");
	print("otest: O profile version %llud, space init = %llud\n",
		fs->flag & Ovolvermask, id);
	seq = 0;
	if((n = orecget(fs, "test.value", vbuf, sizeof(vbuf))) >= 0){
		if(n != 8)
			error("O: bad test.value length");
		seq = UNPACK64(vbuf);
	}
	print("otest: value = %llud\n", seq);
	seq++;
	PACK64(vbuf, seq);
	orecput(fs, "test.value", vbuf, sizeof(vbuf));
	if((n = orecget(fs, "test.value", vbuf, sizeof(vbuf))) != 8 || UNPACK64(vbuf) != seq)
		error("O: test.value did not read back");
	gesync(fs, 0);
	print("otest: value written %llud, synced: PASS\n", seq);
}

/*
 * Plan2001 Stage 3's gate (docs/PoC-experimental-demo.md 3): the native
 * object API on the init Space.  The first boot puts two objects with
 * stable OIDs and commits them; the next boot fetches both by OID after
 * a commit and reboot.  No FS, 9P, Fid, qid or path is involved.
 */
enum {
	OobjA	= 0x4f424a0000000001ULL,
	OobjB	= 0x4f424a0000000002ULL,
};

static char objaval[] = "native O object A";
static char objbval[] = "native O object B";

void
oobjprobe(void *v)
{
	char a[64], b[64];
	long na, nb;
	uvlong va, vb, ver;
	Ospace *sp;

	sp = ospaceopen(v, "init");
	if(waserror()){
		ospaceclose(sp);
		nexterror();
	}
	na = ospaceget(sp, OobjA, a, sizeof(a), &va);
	nb = ospaceget(sp, OobjB, b, sizeof(b), &vb);
	if(na < 0 && nb < 0){
		print("oobj: space init has no objects\n");
		ospaceput(sp, OobjA, objaval, sizeof(objaval)-1);
		ospaceput(sp, OobjB, objbval, sizeof(objbval)-1);
		ver = ospacecommit(sp);
		print("oobj: A and B committed at version %llud\n", ver);
		na = ospaceget(sp, OobjA, a, sizeof(a), &va);
		nb = ospaceget(sp, OobjB, b, sizeof(b), &vb);
		if(na != sizeof(objaval)-1 || memcmp(a, objaval, na) != 0)
			error("oobj: A did not read back");
		if(nb != sizeof(objbval)-1 || memcmp(b, objbval, nb) != 0)
			error("oobj: B did not read back");
		print("oobj: put A and B, commit, readback: PASS\n");
	}else{
		if(na != sizeof(objaval)-1 || memcmp(a, objaval, na) != 0)
			error("oobj: A not restored");
		if(nb != sizeof(objbval)-1 || memcmp(b, objbval, nb) != 0)
			error("oobj: B not restored");
		print("oobj: restored A (version %llud) and B (version %llud): PASS\n",
			va, vb);
	}
	ospaceclose(sp);
	poperror();
}

/* Plan2001 Stage 4: the port's syscall boundary.  These keep Ospace and
 * Gefs opaque to devgefs.c, whose 6c signatures for an incomplete
 * struct would otherwise differ from this package's. */
void*
onativeopen(void *v, char *name)
{
	return ospaceopen(v, name);
}

long
onativeget(void *sp, uvlong oid, void *buf, long n, uvlong *ver)
{
	return ospaceget(sp, oid, buf, n, ver);
}

void
onativeput(void *sp, uvlong oid, void *buf, long n)
{
	ospaceput(sp, oid, buf, n);
}

uvlong
onativecommit(void *sp)
{
	return ospacecommit(sp);
}

void
onativeclose(void *sp)
{
	ospaceclose(sp);
}
