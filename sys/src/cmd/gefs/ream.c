#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <avl.h>

#include "dat.h"
#include "fns.h"

static void
initadm(Blk *r, Blk *u, int nu)
{
	char *p, kbuf[Keymax], vbuf[Inlmax];
	Kvp kv;
	Xdir d;

	/* nb: values must be inserted in key order */
	kv.k = kbuf;
	kv.nk = Offksz;
	kv.v = vbuf;
	kv.nv = Ptrsz;
	kbuf[0] = Kdat;
	PACK64(kbuf+1, (uvlong)Qadmuser);
	PACK64(kbuf+9, 0ULL);
	packbp(kv.v, kv.nv, &u->bp);
	setval(r, &kv);

	/* stupid quirk, we sort by length and then alphabet.. */
	fillxdir(&d, Qctl, "ctl", QTFILE, 0664, 0);
	dir2kv(Qadmroot, &d, &kv, vbuf, sizeof(vbuf));
	setval(r, &kv);

	fillxdir(&d, Qadmuser, "users", QTFILE, 0664, nu);
	dir2kv(Qadmroot, &d, &kv, vbuf, sizeof(vbuf));
	setval(r, &kv);

	fillxdir(&d, Qstatus, "status", QTFILE, 0664, 0);
	dir2kv(Qadmroot, &d, &kv, vbuf, sizeof(vbuf));
	setval(r, &kv);

	fillxdir(&d, Qadmroot, "", QTDIR, DMDIR|0775, 0);
	dir2kv(-1, &d, &kv, vbuf, sizeof(vbuf));
	setval(r, &kv);

	p = packsuper(kbuf, sizeof(kbuf), Qadmroot);
	kv.k = kbuf;
	kv.nk = p - kbuf;
	p = packdkey(vbuf, sizeof(vbuf), -1, "");
	kv.v = vbuf;
	kv.nv = p - vbuf;
	setval(r, &kv);
}

static void
initroot(Blk *r)
{
	char *p, kbuf[Keymax], vbuf[Inlmax];
	Kvp kv;
	Xdir d;

	/* nb: values must be inserted in key order */
	fillxdir(&d, Qmainroot, "", QTDIR, DMDIR|0775, 0);
	dir2kv(-1, &d, &kv, vbuf, sizeof(vbuf));
	setval(r, &kv);

	p = packsuper(kbuf, sizeof(kbuf), Qmainroot);
	kv.k = kbuf;
	kv.nk = p - kbuf;
	p = packdkey(vbuf, sizeof(vbuf), -1, "");
	kv.v = vbuf;
	kv.nv = p - vbuf;
	setval(r, &kv);
}

static void
initsnap(Blk *s, Blk *r, Blk *a)
{
	char *p, *e, buf[Kvmax];
	Tree t;
	Kvp kv;

	lbl2kv("adm", 1, Lmut, &kv, buf, sizeof(buf));
	setval(s, &kv);
	lbl2kv("empty", 0, 0, &kv, buf, sizeof(buf));
	setval(s, &kv);
	lbl2kv("main", 2, Lmut, &kv, buf, sizeof(buf));
	setval(s, &kv);

	p = buf;
	e = p + sizeof(buf);

	/* empty */
	kv.k = p;
	p = packsnap(buf, e - p, 0);
	kv.nk = p - kv.k;
	kv.v = p;
	memset(&t, 0, sizeof(Tree));
	t.flag = 0;
	t.nref = 2;
	t.nlbl = 1;
	t.ht = 1;
	t.gen = fs->nextgen++;
	t.base = -1;
	t.pred = -1;
	t.succ = -1;
	t.bp = r->bp;
	p = packtree(p, e - p, &t);
	kv.nv = p - kv.v;
	setval(s, &kv);

	p = buf;
	e = p + sizeof(buf);

	/* adm */
	kv.k = p;
	p = packsnap(p, e - p, 1);
	kv.nk = p - kv.k;
	kv.v = p;
	memset(&t, 0, sizeof(Tree));
	t.nref = 0;
	t.nlbl = 1;
	t.ht = 1;
	t.gen = fs->nextgen++;
	t.base = 0;
	t.pred = -1;
	t.succ = -1;
	t.bp = a->bp;
	p = packtree(p, e - p, &t);
	kv.nv = p - kv.v;
	setval(s, &kv);

	p = buf;
	e = p + sizeof(buf);

	/* main */
	kv.k = p;
	p = packsnap(buf, e - p, 2);
	kv.nk = p - kv.k;
	kv.v = p;
	memset(&t, 0, sizeof(Tree));
	t.nref = 0;
	t.nlbl = 1;
	t.ht = 1;
	t.gen = fs->nextgen++;
	t.base = 0;
	t.pred = -1;
	t.succ = -1;
	t.bp = r->bp;
	p = packtree(p, e - p, &t);
	kv.nv = p - kv.v;
	setval(s, &kv);
}

static void
initarena(Arena *a, uvlong hdaddr, vlong asz)
{
	Blk *b, *h0, *h1;
	uvlong addr;
	char *p;

	b = cachepluck();

	addr = hdaddr+2*Blksz;	/* leave room for arena hdr */

	a->loghd.addr = -1;
	a->loghd.hash = -1;
	a->loghd.gen = -1;

	memset(b->buf, 0, sizeof(b->buf));
	b->type = Tlog;
	b->bp.addr = addr;
	b->logsz = 0;
	b->logp = (Bptr){-1, -1, -1};
	b->data = b->buf + Loghdsz;
	setflag(b, Bdirty, 0);

	p = b->buf + Loghdsz;
	b->logp = (Bptr){-1, -1, -1};
	PACK64(p, addr|LogFree);	p += 8;	/* addr */
	PACK64(p, asz-2*Blksz);		p += 8;	/* len */
	PACK64(p, b->bp.addr|LogAlloc);	p += 8;	/* addr */
	PACK64(p, Blksz);		p += 8;	/* len */
	PACK64(p, (uvlong)LogSync);	p += 8;	/* barrier */
	b->logsz = p - b->data;
	finalize(b);
	syncblk(b);
	dropblk(b);

	a->loghd = b->bp;
	a->loghd.gen = -1;
	a->size = asz;
	a->used = Blksz;

	h0 = cachepluck();
	h1 = cachepluck();

	memset(h0->buf, 0, sizeof(h0->buf));
	h0->type = Tarena;
	h0->bp.addr = hdaddr;
	h0->data = h0->buf+2;
	packarena(h0->data, Arenasz, a);
	finalize(h0);
	syncblk(h0);
	a->h0 = h0;

	memset(h1->buf, 0, sizeof(h1->buf));
	h1->type = Tarena;
	h1->bp.addr = hdaddr+Blksz;
	h1->data = h1->buf+2;
	packarena(h1->data, Arenasz, a);
	finalize(h1);
	syncblk(h1);
	a->h1 = h1;
}

void
reamfs(char *dev)
{
	Blk *sb0, *sb1, *tb, *mb, *ab, *ub;
	vlong sz, asz, off;
	Mount *mnt, *adm;
	Arena *a;
	Tree *r;
	char *utab;
	Dir *d;
	int i;

	if(waserror())
		sysfatal("ream %s: %s", dev, errmsg());
	if((fs->fd = open(dev, ORDWR)) == -1)
		sysfatal("open %s: %r", dev);
	if((d = dirfstat(fs->fd)) == nil)
		sysfatal("ream: %r");
	sz = d->length;
	free(d);

	print("reaming %s\n", dev);
	if(sz < 128*MiB+Blksz)
		sysfatal("ream: disk too small");
	mnt = emalloc(sizeof(Mount), 1);
	aswapp(&mnt->root, mallocz(sizeof(Tree), 1));
	adm = mallocz(sizeof(Mount), 1);
	aswapp(&adm->root, mallocz(sizeof(Tree), 1));

	sz = sz - sz%Blksz - 2*Blksz;
	fs->narena = (sz + 4096ULL*GiB - 1) / (4096ULL*GiB);
	if(fs->narena < 8)
		fs->narena = 8;
	if(fs->narena >= 32)
		fs->narena = 32;
	fs->arenas = emalloc(fs->narena*sizeof(Arena), 1);


	off = Blksz;
	asz = sz/fs->narena;
	asz = asz - (asz % Blksz) - 2*Blksz;

	sb0 = cachepluck();
	sb1 = cachepluck();
	sb0->bp = (Bptr){0, -1, -1};
	sb1->bp = (Bptr){sz+Blksz, -1, -1};

	fs->arenabp = emalloc(fs->narena * sizeof(Bptr), 1);
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		print("\tarena %d: %lld blocks at %llx\n", i, asz/Blksz, off);
		initarena(a, off, asz);
		fs->arenabp[i] = a->h0->bp;
		off += asz+2*Blksz;

	}
	
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		loadarena(a, a->h0->bp);
		loadlog(a, a->loghd);
	}

	if((mb = newblk(agetp(&mnt->root), Tleaf)) == nil)
		sysfatal("ream: allocate root: %r");
	holdblk(mb);
	initroot(mb);
	finalize(mb);
	syncblk(mb);

	r = agetp(&mnt->root);
	r->ht = 1;
	r->bp = mb->bp;

	r = agetp(&adm->root);
	if((ab = newblk(r, Tleaf)) == nil)
		sysfatal("ream: allocate root: %r");
	if((ub = newdblk(r, 0, 1)) == nil)
		sysfatal("ream: allocate root: %r");
	holdblk(ab);
	holdblk(ub);
	utab = smprint(
		"-1:adm::%s\n"
		"0:none::\n"
		"1:%s:%s:\n",
		reamuser, reamuser, reamuser);
	memcpy(ub->data, utab, strlen(utab));
	finalize(ub);
	syncblk(ub);
	initadm(ab, ub, strlen(utab));
	finalize(ab);
	syncblk(ab);

	r->ht = 1;
	r->bp = ab->bp;

	/*
	 * Now that we have a completely empty fs, give it
	 * a single snap block that the tree will insert
	 * into, and take a snapshot as the initial state.
	 */
	if((tb = newblk(agetp(&mnt->root), Tleaf)) == nil)
		sysfatal("ream: allocate snaps: %r");
	holdblk(tb);
	initsnap(tb, mb, ab);
	finalize(tb);
	syncblk(tb);

	fs->snap.bp = tb->bp;
	fs->snap.ht = 1;
	fs->snapdl.hd.addr = -1;
	fs->snapdl.hd.hash = -1;
	fs->snapdl.tl.addr = -1;
	fs->snapdl.tl.hash = -1;

	dropblk(mb);
	dropblk(ab);
	dropblk(ub);
	dropblk(tb);
	fs->nextqid = Nreamqid;

	/*
	 * We need to write back all of the arenas
	 * with the updated free lists
	 */
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		finalize(a->logtl);
		syncblk(a->logtl);
		packarena(a->h0->data, Blksz, a);
		finalize(a->h0);
		syncblk(a->h0);
		packarena(a->h1->data, Blksz, a);
		finalize(a->h1);
		syncblk(a->h1);
		fs->arenabp[i] = a->h0->bp;
		dropblk(a->h0);
		dropblk(a->h1);
	}

	dropblk(mb);
	dropblk(ab);
	dropblk(ub);
	dropblk(tb);

	/*
	 * Finally, write back the superblock and backup
	 * superblock.
	 */
	packsb(sb0->buf, Blksz, fs);
	packsb(sb1->buf, Blksz, fs);
	finalize(sb0);
	finalize(sb1);
	syncblk(sb0);
	syncblk(sb1);
	dropblk(sb0);
	dropblk(sb1);
	free(mnt);
	poperror();
}


/*
 * Plan2001 Stage 4: seed a fresh O volume with one Environment and its
 * immutable code objects.  Files are host preparation inputs only: the
 * finished image's runtime reads O objects by OID.  A single catalog
 * leaf holds the records; a demo's manifest and code must fit in it.
 */
enum {
	OID_ENV_HOST	= 0xfffffffffffffffcULL,
	OID_FIRST_HOST	= 2,
	Seedmax		= 128*KiB,
};

typedef struct Seedrec Seedrec;
struct Seedrec {
	char	k[Keymax];
	int	nk;
	char	v[Inlmax];
	int	nv;
};

extern char *seeddir;

static int
seedcmp(const void *a, const void *b)
{
	Seedrec *x = (Seedrec*)a, *y = (Seedrec*)b;
	Key kx, ky;

	kx.k = x->k;
	kx.nk = x->nk;
	ky.k = y->k;
	ky.nk = y->nk;
	return keycmp(&kx, &ky);
}

static void
seedadd(Seedrec **r, int *n, int *a, char *k, int nk, char *v, int nv)
{
	Seedrec *x;

	if(nk > Keymax || nv > Inlmax)
		sysfatal("seed: record too big");
	if(*n == *a){
		*a = *a ? 2*(*a) : 32;
		*r = realloc(*r, *a * sizeof(Seedrec));
		if(*r == nil)
			sysfatal("seed: malloc: %r");
	}
	x = &(*r)[(*n)++];
	memmove(x->k, k, nk);
	x->nk = nk;
	memmove(x->v, v, nv);
	x->nv = nv;
}

static void
seedobj(Seedrec **r, int *n, int *a, uvlong oid, uchar *data, long len)
{
	enum { Seedchunk = 480 };
	char k[Keymax], v[Inlmax];
	long off, m;
	u32int idx;

	off = 0;
	idx = 0;
	do{
		k[0] = Kobj;
		PACK64(k+1, OspaceInit);
		PACK64(k+9, oid);
		PACK64(k+17, ~1ULL);
		PACK32(k+25, idx);
		m = 0;
		if(idx == 0){
			PACK32(v, (u32int)len);
			m = 4;
		}
		if(len - off > Seedchunk - m)
			m += Seedchunk - m;
		else
			m += len - off;
		if(m > (idx == 0 ? 4 : 0)){
			if(idx == 0)
				memmove(v+4, data+off, m-4);
			else
				memmove(v, data+off, m);
		}
		seedadd(r, n, a, k, 29, v, m);
		off += m - (idx == 0 ? 4 : 0);
		idx++;
	}while(off < len);
}

static int
seedread(char *path, uchar **bp, long *np)
{
	Dir *d;
	uchar *p;
	long n, m, r;
	int fd;

	r = 0;

	if((fd = open(path, OREAD)) < 0)
		return -1;
	if((d = dirfstat(fd)) == nil){
		close(fd);
		return -1;
	}
	n = d->length;
	free(d);
	p = emalloc(n+1, 1);
	m = 0;
	while(m < n){
		r = read(fd, p+m, n-m);
		if(r <= 0)
			break;
		m += r;
	}
	close(fd);
	if(r < 0){
		free(p);
		return -1;
	}
	p[m] = 0;
	*bp = p;
	*np = m;
	return 0;
}

static void
oseed(Blk *tb)
{
	char *env, *p, *e, *q, path[512];
	char name[128], def[65], mpy[65], oidhex[17], k[Keymax], v[Inlmax], hex[65];
	uchar *data, *eb, *hb;
	long n, en, hn, tot;
	Seedrec *r;
	uvlong oid;
	int nr, ar, off, i;

	r = nil;
	nr = ar = 0;
	/* the catalog entry: init -> 1 */
	k[0] = Ko;
	strecpy(k+1, k+sizeof k, "init");
	PACK64(v, OspaceInit);
	seedadd(&r, &nr, &ar, k, 1+4, v, 8);

	snprint(path, sizeof path, "%s/env", seeddir);
	if(seedread(path, &eb, &en) < 0)
		sysfatal("seed: %s: %r", path);
	snprint(path, sizeof path, "%s/envhex", seeddir);
	if(seedread(path, &hb, &hn) < 0)
		sysfatal("seed: %s: %r", path);
	while(hn > 0 && (hb[hn-1] == '\n' || hb[hn-1] == '\r' || hb[hn-1] == ' '))
		hn--;
	if(hn != 64)
		sysfatal("seed: envhex: not 64 characters");
	seedobj(&r, &nr, &ar, OID_ENV_HOST, eb, en);

	/* a line: name definition mpy code-oid */
	env = (char*)eb;
	for(p = env; *p != 0;){
		if((e = strchr(p, '\n')) == nil)
			e = p + strlen(p);
		if(strncmp(p, "runtime ", 8) == 0){
			p = *e ? e+1 : e;
			continue;
		}
		q = p;
		off = 0;
#define FIELD(dst, max) do{ \
	off = 0; \
	while(q < e && *q != ' ' && off < (max)-1) dst[off++] = *q++; \
	dst[off] = 0; \
	if(q < e && *q == ' ') q++; \
}while(0)
		FIELD(name, sizeof name);
		FIELD(def, sizeof def);
		FIELD(mpy, sizeof mpy);
		FIELD(oidhex, sizeof oidhex);
#undef FIELD
		if(strlen(def) != 64 || strlen(mpy) != 64 || strlen(oidhex) != 16)
			sysfatal("seed: bad manifest line");
		oid = strtoull(oidhex, nil, 16);
		snprint(path, sizeof path, "%s/mod/%s.mpy", seeddir, def);
		if(seedread(path, &data, &n) < 0)
			sysfatal("seed: %s: %r", path);
		seedobj(&r, &nr, &ar, oid, data, n);
		free(data);
		p = *e ? e+1 : e;
	}
	/* the header: version 1, then Environment and next OID */
	memmove(hex, hb, 64);
	hex[64] = 0;
	k[0] = Kobj;
	PACK64(k+1, OspaceInit);
	PACK64(v, 1);
	PACK64(v+8, 1);
	PACK64(v+16, 1);
	memmove(v+24, hb, 64);
	PACK64(v+24+64, OID_FIRST_HOST);
	seedadd(&r, &nr, &ar, k, 9, v, 24+72);

	free(eb);
	free(hb);
	qsort(r, nr, sizeof(Seedrec), seedcmp);
	tot = 0;
	for(i = 0; i < nr; i++)
		tot += 2 + r[i].nk + 2 + r[i].nv;
	if(tot > Leafspc)
		sysfatal("seed: %ld bytes do not fit one catalog leaf (%ld)", tot, (long)Leafspc);
	for(i = 0; i < nr; i++){
		Kvp kv;

		kv.k = r[i].k;
		kv.nk = r[i].nk;
		kv.v = r[i].v;
		kv.nv = r[i].nv;
		setval(tb, &kv);
	}
	free(r);
	print("seed: %d records, %ld bytes, Environment %s\n", nr, tot, hex);
}

/*
 * Plan2001: format a native O volume (docs/o-volume-format.md).  Reuse
 * GE's arenas, logs and an ordinary leaf for the flat catalog: there is
 * no FS root, adm tree or user table.  The superblock flag carries the
 * profile and PoC version; opening never calls this.
 */
void
reamo(char *dev)
{
	Blk *sb0, *sb1, *tb;
	vlong sz, asz, off;
	Arena *a;
	char kbuf[Keymax], vbuf[Inlmax];
	Kvp kv;
	Dir *d;
	int i;

	if(waserror())
		sysfatal("ream O %s: %s", dev, errmsg());
	if((fs->fd = open(dev, ORDWR)) == -1)
		sysfatal("open %s: %r", dev);
	if((d = dirfstat(fs->fd)) == nil)
		sysfatal("ream O: %r");
	sz = d->length;
	free(d);

	print("reaming O volume %s\n", dev);
	if(sz < 128*MiB+Blksz)
		sysfatal("ream O: disk too small");
	sz = sz - sz%Blksz - 2*Blksz;
	fs->narena = (sz + 4096ULL*GiB - 1) / (4096ULL*GiB);
	if(fs->narena < 8)
		fs->narena = 8;
	if(fs->narena >= 32)
		fs->narena = 32;
	fs->arenas = emalloc(fs->narena*sizeof(Arena), 1);

	off = Blksz;
	asz = sz/fs->narena;
	asz = asz - (asz % Blksz) - 2*Blksz;

	sb0 = cachepluck();
	sb1 = cachepluck();
	sb0->bp = (Bptr){0, -1, -1};
	sb1->bp = (Bptr){sz+Blksz, -1, -1};

	fs->arenabp = emalloc(fs->narena * sizeof(Bptr), 1);
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		print("\tarena %d: %lld blocks at %llx\n", i, asz/Blksz, off);
		initarena(a, off, asz);
		fs->arenabp[i] = a->h0->bp;
		off += asz+2*Blksz;
	}
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		loadarena(a, a->h0->bp);
		loadlog(a, a->loghd);
	}

	/*
	 * The catalog is one GE leaf in the snapshot tree: Ko "init" =>
	 * an 8-byte SpaceID.  It is flat: no parent, slash or directory.
	 * Stage 4's -i adds the Environment and code objects to it.
	 */
	if((tb = newblk(&fs->snap, Tleaf)) == nil)
		sysfatal("ream O: allocate catalog: %r");
	holdblk(tb);
	if(seeddir != nil)
		oseed(tb);
	else{
		kbuf[0] = Ko;
		strecpy(kbuf+1, kbuf+sizeof(kbuf), "init");
		PACK64(vbuf, OspaceInit);
		kv.k = kbuf;
		kv.nk = 1+strlen("init");
		kv.v = vbuf;
		kv.nv = 8;
		setval(tb, &kv);
	}
	finalize(tb);
	syncblk(tb);

	fs->snap.ht = 1;
	fs->snap.bp = tb->bp;
	fs->snapdl.hd.addr = -1;
	fs->snapdl.hd.hash = -1;
	fs->snapdl.tl.addr = -1;
	fs->snapdl.tl.hash = -1;
	fs->flag = Ovolflag | Ovolvers;
	fs->nextqid = 0;
	dropblk(tb);

	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		finalize(a->logtl);
		syncblk(a->logtl);
		packarena(a->h0->data, Blksz, a);
		finalize(a->h0);
		syncblk(a->h0);
		packarena(a->h1->data, Blksz, a);
		finalize(a->h1);
		syncblk(a->h1);
		fs->arenabp[i] = a->h0->bp;
		dropblk(a->h0);
		dropblk(a->h1);
	}

	packsb(sb0->buf, Blksz, fs);
	packsb(sb1->buf, Blksz, fs);
	finalize(sb0);
	finalize(sb1);
	syncblk(sb0);
	syncblk(sb1);
	dropblk(sb0);
	dropblk(sb1);
	poperror();
}

void
growfs(char *dev)
{
	vlong oldsz, newsz, asz, off, eb;
	int i, narena;
	Arena *a;
	Dir *d;

	if(waserror())
		sysfatal("grow %s: %s", dev, errmsg());
	if((d = dirfstat(fs->fd)) == nil)
		sysfatal("ream: %r");
	a = &fs->arenas[fs->narena-1];
	oldsz = a->h0->bp.addr + a->size + 2*Blksz;
	newsz = d->length - d->length%Blksz - 2*Blksz;
	if(newsz - oldsz < 64*MiB)
		sysfatal("new arenas too small (%lld < %lld), not growing", newsz - oldsz, 64*MiB);
	asz = (newsz - oldsz)/4;
	asz = asz - asz % Blksz - 2*Blksz;
	narena = fs->narena + 4;
	assert(oldsz % Blksz == 0);
	if((fs->arenas = realloc(fs->arenas, narena*sizeof(Arena))) == nil)
		error(Enomem);
	if((fs->arenabp = realloc(fs->arenabp, narena*sizeof(Bptr))) == nil)
		error(Enomem);

	off = oldsz;
	for(i = fs->narena; i < narena; i++){
		a = &fs->arenas[i];
		print("\tnew arena %d: adding %lld blocks at %llx\n", i, asz/Blksz, off);
		initarena(&fs->arenas[i], off, asz);
		loadarena(a, a->h0->bp);
		loadlog(a, a->loghd);
		a = &fs->arenas[i];
		packarena(a->h0->data, Blksz, a);
		packarena(a->h1->data, Blksz, a);
		finalize(a->h0);
		finalize(a->h1);
		syncblk(a->h0);
		syncblk(a->h1);

		fs->arenabp[i] = a->h0->bp;
		off += asz+2*Blksz;
	}
	fs->narena = narena;
	packsb(fs->sb0->buf, Blksz, fs);
	finalize(fs->sb0);
	syncblk(fs->sb0);
	/*
	 * We're being a bit tricksy here: because we're on a bigger
	 * partition, we don't know where the end is; just load the
	 * first block, and patch the address in to the right place
	 * when we write it back.
	 */
	eb = d->length;
	eb = eb - (eb%Blksz) - Blksz;
	fs->sb0->bp = (Bptr){eb, -1, -1};
	packsb(fs->sb0->buf, Blksz, fs);
	finalize(fs->sb0);
	syncblk(fs->sb0);
	free(d);
	poperror();
}
