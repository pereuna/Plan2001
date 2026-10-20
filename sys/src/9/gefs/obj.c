#include "gefs.h"

/*
 * Plan2001: object stores (docs/gefs-obj.md).  A store is a file made
 * with DMOBJ; its objects are versioned keys of the file's tree:
 *
 *	Kobj store[8]				=> N[8] K[8] W[8] user[]
 *	Kobj store[8] oid[8] ~ver[8] idx[4]	=> chunk (idx 0: len[4] first)
 *
 * N is the published version, K the oldest one kept, W the last begun.
 * What shows is, for each oid, its newest version <= N, unless that is
 * a tombstone (len ~0).  A transaction writes version W = N+1 as the
 * records come (unseen, as > N), and its commit publishes it with the
 * header: one message, so in one change of the tree's root.  A failed
 * or abandoned transaction's keys are taken out (versions > N); after a
 * crash, W > N says some may be left, and the next begin sweeps them.
 *
 * Writes are a stream of records (the fid's transaction):
 *	'p' oid[8] len[8] data	put		'd' oid[8]	delete
 *	'h' len[4] data		header's user part
 *	'c' flags[1]		commit (flags&1: sync)	'a'	abort
 *	'v' ver[8]		the next read's version	's' oid[8]	its object
 * Reads are a stream from offset 0 (a cursor in the fid, as readdir's):
 *	'H' N[8] K[8] len[4] user, then 'o' oid[8] ver[8] len[8] data
 */

enum {
	Objhdrksz	= 1+8,
	Objoidksz	= 1+8+8,
	Objksz		= 1+8+8+8+4,
	Objchunk	= 480,		/* < Inlmax */
	Objuser		= 256,		/* the header's user part */
	Objkeep		= 3,		/* versions kept: K..N */
	Objtxmax	= 64*MiB,	/* a transaction's data, an object's */
	Objtomb		= 0xffffffff,	/* len of a tombstone */
	Objbatch	= 32,		/* messages with values to an upsert */
	Objclob		= 256,		/* clobbers to an upsert: > Bufspc */
	Objyield	= 4,		/* upserts between flushtx's yields */
	Objroom		= 16,		/* blocks a batch of values needs, at most */
	Objrhdr		= 1+8+8+8,	/* 'o' oid ver len */
};

typedef struct Ohdr Ohdr;
typedef struct Oent Oent;
typedef struct Otx Otx;

struct Ohdr {
	uvlong	n;
	uvlong	k;
	uvlong	w;
	int	nuser;
	char	user[Objuser];
};

struct Oent {
	uvlong	oid;
	u32int	nidx;
};

struct Otx {
	Gefs	*fs;		/* Plan2001: its volume */
	Otx	*next;		/* the active ones, under mutlk */
	Mount	*mnt;		/* DMOBJ: the file's mount, nil for native O */
	Tree	*tree;		/* native O: the volume's snapshot tree */
	vlong	store;

	/* the transaction: ver != 0 */
	uvlong	ver;
	Amsg	*abort;		/* for the clunk (it must not fail) */
	Oent	*ent;
	int	nent;
	int	aent;
	vlong	bytes;
	int	newuser;
	int	nuser;
	char	user[Objuser];

	/* the write stream's record */
	char	rec[1+8+8];
	int	nrec;
	uvlong	oid;
	vlong	left;		/* put's data still to come */
	u32int	idx;
	char	chunk[Objchunk];
	int	nchunk;
	int	inuser;		/* 'h' data still to come */

	/* messages for the next upsert */
	int	id;		/* the worker's epoch (yield), -1 none */
	int	nflush;		/* upserts since the last yield */
	Msg	mb[Objclob];
	char	kb[Objclob][Objksz];
	char	vb[Objbatch][Objchunk];
	int	nm;

	/* the read stream */
	uvlong	selver;		/* 'v', 0: N */
	uvlong	seloid;		/* 's' */
	int	sel;
	Scan	*scan;
	vlong	roff;		/* the next offset expected */
	uvlong	rver;
	uvlong	roid;		/* the object being read */
	uvlong	rcver;		/* its version */
	u32int	ridx;		/* its next chunk */
	int	rstate;
	vlong	rleft;
	char	pend[Objrhdr+Objchunk];
	int	npend;
	int	hdrsent;
};

enum {
	Rseek,		/* its newest version <= rver not yet seen */
	Rdata,		/* its chunks */
	Rskip,		/* done with it */
};

/*
 * Plan2001 Stage 3: the engine's tree.  A DMOBJ transaction writes its
 * file's mount (upsert's snapshot handling); a native O transaction
 * writes the O volume's snapshot tree directly, with no mount or file.
 */
static Tree*
txroot(Otx *tx)
{
	if(tx->mnt != nil)
		return agetp(&tx->mnt->root);
	return tx->tree;
}

static void
txupsert(Otx *tx, Msg *m, int nm)
{
	if(tx->mnt != nil)
		upsert(tx->mnt, m, nm);
	else{
		if(agetl(&tx->fs->rdonly))
			error(Erdonly);
		btupsert(tx->tree, m, nm);
	}
}

static int
txdrain(Otx *tx, Key *k, Kvp *next, char *buf, int nbuf)
{
	if(tx->mnt != nil)
		return drain(tx->mnt, k, next, buf, nbuf);
	if(agetl(&tx->fs->rdonly))
		error(Erdonly);
	return btdrain(tx->tree, k, next, buf, nbuf);
}

/* Plan2001: a volume's stores with a transaction, under its mutlk (Gefs.otxs) */

static void
packkey(char *k, vlong store, uvlong oid, uvlong ver, u32int idx)
{
	k[0] = Kobj;
	PACK64(k+1, store);
	PACK64(k+9, oid);
	PACK64(k+17, ~ver);
	PACK32(k+25, idx);
}

static int
gethdr(Tree *t, vlong store, Ohdr *h)
{
	char kb[Objhdrksz], buf[Kvmax];
	Key k;
	Kvp kv;

	kb[0] = Kobj;
	PACK64(kb+1, store);
	k.k = kb;
	k.nk = sizeof(kb);
	memset(h, 0, sizeof(*h));
	if(!btlookup(t, &k, &kv, buf, sizeof(buf)))
		return 0;
	if(kv.nv < 24 || kv.nv > 24+Objuser)
		error(Ecorrupt);
	h->n = UNPACK64(kv.v);
	h->k = UNPACK64(kv.v+8);
	h->w = UNPACK64(kv.v+16);
	h->nuser = kv.nv - 24;
	memmove(h->user, kv.v+24, h->nuser);
	return 1;
}

static void
puthdr(Otx *tx, Ohdr *h)
{
	char kb[Objhdrksz], vb[24+Objuser];
	Msg m;

	kb[0] = Kobj;
	PACK64(kb+1, tx->store);
	PACK64(vb, h->n);
	PACK64(vb+8, h->k);
	PACK64(vb+16, h->w);
	memmove(vb+24, h->user, h->nuser);
	m.op = Oinsert;
	m.k = kb;
	m.nk = sizeof(kb);
	m.v = vb;
	m.nv = 24 + h->nuser;
	txupsert(tx, &m, 1);
}

/*
 * Plan2001: a long run of upserts (a store removed, an abort, the
 * history swept) lets go of mutlk and its epoch every Objyield
 * batches, as AOclear's batches do: the blocks it freed leave limbo.
 * Held through it all, the Bfree pool and the cache ran out with
 * mutlk held, and gefs stopped.  No scan may be entered here.
 */
static void
flushtx(Otx *tx)
{
	Gefs *fs = tx->fs;
	int i;

	if(tx->nm == 0)
		return;
	/*
	 * Plan2001: values stop where data does (blkalloc's Tdat
	 * limit, twice the reserve): the chunks are in the leaves, and
	 * a full disk's commit took the tree's reserve (read-only, T5.9)
	 */
	for(i = 0; i < tx->nm; i++)
		if(tx->mb[i].nv > 0){
			if(!dataroom(fs, Objroom*Blksz))
				error(Efull);
			break;
		}
	txupsert(tx, tx->mb, tx->nm);
	tx->nm = 0;
	if(tx->id >= 0 && ++tx->nflush >= Objyield){
		tx->nflush = 0;
		epochend(fs, tx->id);
		qunlock(&fs->mutlk);
		epochclean(fs);
		qlock(&fs->mutlk);
		epochstart(fs, tx->id);
	}
}

static void
addmsg(Otx *tx, int op, char *k, int nk, char *v, int nv)
{
	Msg *m;

	/*
	 * Plan2001: clobbers go in batches over a pivot's buffer room,
	 * so they reach the leaves: in a pivot's buffer they kept the
	 * removed values in the leaves (T5.4: a removed 40 MiB store
	 * kept ~20 MiB of the tree)
	 */
	if(tx->nm == Objclob || (nv > 0 && tx->nm >= Objbatch))
		flushtx(tx);
	m = &tx->mb[tx->nm];
	memmove(tx->kb[tx->nm], k, nk);
	if(nv > 0)
		memmove(tx->vb[tx->nm], v, nv);
	m->op = op;
	m->k = tx->kb[tx->nm];
	m->nk = nk;
	m->v = nv > 0 ? tx->vb[tx->nm] : nil;	/* vb: Objbatch only */
	m->nv = nv;
	tx->nm++;
}

/*
 * The keys of store under pfx for which drop() says so, out: in
 * batches, each from a scan of its own (as readdir's: entered and left
 * again, so the tree may change between them).  Called with mutlk.
 */
static void
sweep(Otx *tx, char *pfx, int npfx, int (*drop)(char*, int, void*), void *a)
{
	Gefs *fs = tx->fs;
	char last[Keymax];
	int nlast, more, n;
	Scan *s;

	s = emalloc(sizeof(Scan), 1);
	if(waserror()){
		free(s);
		nexterror();
	}
	nlast = 0;
	do{
		btnewscan(fs, s, pfx, npfx);
		if(nlast > 0){
			memmove(s->kvbuf, last, nlast);
			s->kv.k = s->kvbuf;
			s->kv.nk = nlast;
			s->first = 0;
		}
		btenter(txroot(tx), s);
		more = 0;
		n = 0;
		while(btnext(s, &s->kv)){
			if(drop(s->kv.k, s->kv.nk, a)){
				addmsg(tx, Oclobber, s->kv.k, s->kv.nk, nil, 0);
				n++;
			}
			if(n == Objclob-1 || tx->nm == Objclob){
				memmove(last, s->kv.k, s->kv.nk);
				nlast = s->kv.nk;
				more = 1;
				break;
			}
		}
		btexit(s);
		flushtx(tx);
	}while(more);
	poperror();
	free(s);
}

static uvlong
keyver(char *k)
{
	return ~UNPACK64(k+17);
}

static int
dropall(char*, int, void*)
{
	return 1;
}

static int
dropunpub(char *k, int nk, void *a)
{
	return nk == Objksz && keyver(k) > *(uvlong*)a;
}

typedef struct Oold Oold;
struct Oold {
	uvlong	k;
	uvlong	keep;	/* the newest version <= k, 0: not yet seen */
};

/* an oid's versions older than its newest <= K: seen by none kept */
static int
dropold(char *k, int nk, void *a)
{
	Oold *o;
	uvlong v;

	if(nk != Objksz)
		return 0;
	o = a;
	v = keyver(k);
	if(o->keep == 0){
		if(v <= o->k)
			o->keep = v;
		return 0;
	}
	return v < o->keep;
}

int	objfail;	/* the next commit's history sweep fails */

/*
 * Plan2001 V7 (docs/v7-upgrade.md 4.2): the user part's recovery pin.
 * The header's user part is the file's, and only its writer knows it:
 * env[64] next[8] (V5, V6), or env[64] next[8] fmt[1] pad[7] pin[8]
 * (V7, fmt 2).  The pin is a published version that must stay readable
 * until the next upgrade publishes another: K never passes it.
 */
enum {
	Objuserfmt	= 72,	/* the format byte's offset */
	Objuserpin	= 80,	/* the pin's */
	Objuserfmtv7	= 2,
	Objuserlenv7	= 88,
};

static uvlong
objpin(Ohdr *h)
{
	if(h->nuser < Objuserlenv7 || h->user[Objuserfmt] != Objuserfmtv7)
		return 0;
	return UNPACK64(h->user+Objuserpin);
}

static Otx*
gettx(Fid *f)
{
	Otx *tx;

	if((tx = f->aux) == nil){
		tx = emalloc(sizeof(Otx), 1);
		tx->fs = f->fs;
		tx->mnt = f->mnt;
		tx->tree = nil;
		tx->store = f->qpath;
		tx->id = -1;
		f->aux = tx;
	}
	return tx;
}

static void
begin(Otx *tx)
{
	Gefs *fs = tx->fs;
	Ohdr h;
	Otx *o;

	if(tx->ver != 0)
		return;
	for(o = fs->otxs; o != nil; o = o->next)
		if(o->store == tx->store)
			error("object store busy");
	if(tx->abort == nil)
		tx->abort = emalloc(sizeof(Amsg), 1);
	gethdr(txroot(tx), tx->store, &h);
	if(h.w > h.n){
		/* a transaction cut short: its keys out */
		char pfx[Objhdrksz];

		pfx[0] = Kobj;
		PACK64(pfx+1, tx->store);
		sweep(tx, pfx, sizeof(pfx), dropunpub, &h.n);
	}
	h.w = h.n + 1;
	puthdr(tx, &h);
	tx->ver = h.w;
	tx->nent = 0;
	tx->bytes = 0;
	tx->newuser = 0;
	tx->next = fs->otxs;
	fs->otxs = tx;
}

static void
endtx(Otx *tx)
{
	Gefs *fs = tx->fs;
	Otx **p;

	for(p = (Otx**)&fs->otxs; *p != nil; p = &(*p)->next)
		if(*p == tx){
			*p = tx->next;
			break;
		}
	tx->ver = 0;
	tx->nent = 0;
	tx->nm = 0;
	tx->nrec = 0;
	tx->left = 0;
	tx->inuser = 0;
	free(tx->ent);
	tx->ent = nil;
	tx->aent = 0;
}

static void
addent(Otx *tx, uvlong oid, u32int nidx)
{
	Oent *e;

	if(tx->nent == tx->aent){
		tx->aent = tx->aent ? 2*tx->aent : 256;
		e = realloc(tx->ent, tx->aent*sizeof(Oent));
		if(e == nil)
			error(Enomem);
		tx->ent = e;
	}
	tx->ent[tx->nent].oid = oid;
	tx->ent[tx->nent].nidx = nidx;
	tx->nent++;
}

/* the transaction's keys out; called with mutlk */
static void
abort1(Otx *tx)
{
	char k[Objksz];
	Ohdr h;
	Oent *e;
	u32int i;

	if(tx->ver == 0)
		return;
	if(waserror()){
		/* left to the next begin: W > N */
		endtx(tx);
		nexterror();
	}
	tx->nm = 0;
	for(e = tx->ent; e < tx->ent+tx->nent; e++)
		for(i = 0; i < e->nidx; i++){
			packkey(k, tx->store, e->oid, tx->ver, i);
			addmsg(tx, Oclobber, k, Objksz, nil, 0);
		}
	flushtx(tx);
	gethdr(txroot(tx), tx->store, &h);
	h.w = h.n;
	puthdr(tx, &h);
	poperror();
	endtx(tx);
}

static void
putchunk(Otx *tx)
{
	char k[Objksz];

	packkey(k, tx->store, tx->oid, tx->ver, tx->idx);
	addmsg(tx, Oinsert, k, Objksz, tx->chunk, tx->nchunk);
	tx->idx++;
	tx->nchunk = 0;
}

static void
commit(Otx *tx)
{
	char pfx[Objoidksz];
	Ohdr h;
	Oent *e;
	Oold o;

	if(tx->ver == 0)
		return;
	uvlong k0, pin;

	flushtx(tx);
	gethdr(txroot(tx), tx->store, &h);
	if(h.w != tx->ver)
		error(Ecorrupt);
	h.n = tx->ver;
	/* the prospective user part first: its pin decides K */
	k0 = h.k;
	if(tx->newuser){
		h.nuser = tx->nuser;
		memmove(h.user, tx->user, tx->nuser);
	}
	if(h.n >= Objkeep && h.k < h.n - Objkeep + 1)
		h.k = h.n - Objkeep + 1;
	pin = objpin(&h);
	if(pin != 0 && pin < h.k && pin >= k0)
		h.k = pin;	/* the recovery point stays: K <= pin <= N */
	if(h.k < k0)
		h.k = k0;	/* and nothing already pruned comes back */
	if(h.k == 0)
		h.k = 1;
	/* published: one message */
	puthdr(tx, &h);

	/*
	 * then the versions none kept sees.  Published, the
	 * transaction is no more: a failure here aborts nothing
	 * (abort1 clobbered the version just published) and is no
	 * failure of the commit; what is left is swept at the
	 * object's next commit
	 */
	tx->ver = 0;
	if(waserror()){
		print("gefs: store %llx: history not swept: %s\n", tx->store, errmsg());
		tx->nm = 0;
	}else{
		if(objfail){	/* the console's objfail (T5.12) */
			objfail = 0;
			error("objfail: history sweep");
		}
		pfx[0] = Kobj;
		PACK64(pfx+1, tx->store);
		for(e = tx->ent; e < tx->ent+tx->nent; e++){
			PACK64(pfx+9, e->oid);
			o.k = h.k;
			o.keep = 0;
			sweep(tx, pfx, sizeof(pfx), dropold, &o);
		}
		poperror();
	}
	endtx(tx);
}

/*
 * One Twrite's records, in the mutator (mutlk).  Returns 1 if the
 * reply waits for a sync (*ao, which answers m).
 */
int
objwrite(Fmsg *m, Fid *f, Amsg **ao, int id)
{
	Gefs *fs = m->conn->fs;
	char *p, *e;
	int n, need, sync;
	Otx *tx;

	tx = gettx(f);
	tx->id = id;
	sync = 0;
	if(waserror()){
		if(!waserror()){
			abort1(tx);
			poperror();
		}
		endtx(tx);
		nexterror();
	}
	p = m->data;
	e = p + m->count;
	while(p < e){
		if(tx->left > 0){
			n = Objchunk - tx->nchunk;
			if(n > tx->left)
				n = tx->left;
			if(n > e - p)
				n = e - p;
			memmove(tx->chunk+tx->nchunk, p, n);
			tx->nchunk += n;
			tx->left -= n;
			p += n;
			if(tx->nchunk == Objchunk || tx->left == 0)
				putchunk(tx);
			if(tx->left == 0)
				tx->ent[tx->nent-1].nidx = tx->idx;
			continue;
		}
		if(tx->inuser > 0){
			n = tx->inuser;
			if(n > e - p)
				n = e - p;
			memmove(tx->user+tx->nuser, p, n);
			tx->nuser += n;
			tx->inuser -= n;
			p += n;
			continue;
		}
		if(tx->nrec == 0)
			tx->rec[tx->nrec++] = *p++;
		switch(tx->rec[0]){
		case 'p':	need = 1+8+8;	break;
		case 'd':
		case 'v':
		case 's':	need = 1+8;	break;
		case 'h':	need = 1+4;	break;
		case 'c':	need = 1+1;	break;
		case 'a':	need = 1;	break;
		default:	error("object record: unknown type");
		}
		n = need - tx->nrec;
		if(n > e - p)
			n = e - p;
		memmove(tx->rec+tx->nrec, p, n);
		tx->nrec += n;
		p += n;
		if(tx->nrec < need)
			break;
		tx->nrec = 0;
		switch(tx->rec[0]){
		case 'p':
			begin(tx);
			tx->oid = UNPACK64(tx->rec+1);
			tx->left = UNPACK64(tx->rec+9);
			if(tx->left < 0 || tx->left > Objtxmax
			|| tx->bytes + tx->left > Objtxmax)
				error("object transaction too big");
			if(!dataroom(fs, 2*tx->left + Blksz))
				error(Efull);
			tx->bytes += tx->left;
			tx->idx = 0;
			PACK32(tx->chunk, (u32int)tx->left);
			tx->nchunk = 4;
			addent(tx, tx->oid, 1);
			if(tx->left == 0)
				putchunk(tx);
			break;
		case 'd':
			begin(tx);
			tx->oid = UNPACK64(tx->rec+1);
			tx->idx = 0;
			PACK32(tx->chunk, Objtomb);
			tx->nchunk = 4;
			putchunk(tx);
			addent(tx, tx->oid, 1);
			break;
		case 'h':
			begin(tx);
			tx->inuser = UNPACK32(tx->rec+1);
			if(tx->inuser < 0 || tx->inuser > Objuser)
				error("object header too big");
			tx->nuser = 0;
			tx->newuser = 1;
			break;
		case 'c':
			commit(tx);
			sync = tx->rec[1] & 1;
			break;
		case 'a':
			abort1(tx);
			break;
		case 'v':
			tx->selver = UNPACK64(tx->rec+1);
			break;
		case 's':
			tx->seloid = UNPACK64(tx->rec+1);
			tx->sel = 1;
			break;
		}
	}
	flushtx(tx);
	poperror();
	if(sync){
		*ao = emalloc(sizeof(Amsg), 1);
		(*ao)->op = AOsync;
		(*ao)->m = m;
		return 1;
	}
	return 0;
}

/*
 * One Tread of the stream, in a reader (no mutlk; the fid locked).
 */
static int
emit(Otx *tx, char **pp, char *e, char *s, int n)
{
	int k;

	k = e - *pp;
	if(k > n)
		k = n;
	memmove(*pp, s, k);
	*pp += k;
	if(k < n){
		memmove(tx->pend, s+k, n-k);
		tx->npend = n-k;
		return 0;
	}
	return 1;
}

void
objread(Fmsg *m, Fid *f, Fcall *r)
{
	Gefs *fs = m->conn->fs;
	char pfx[Objoidksz], b[Objrhdr+Objchunk], *p, *e;
	uvlong oid, v;
	u32int len;
	int n, room;
	Ohdr h;
	Tree *t;
	Otx *tx;

	wlock(f);
	if(waserror()){
		wunlock(f);
		nexterror();
	}
	tx = gettx(f);
	t = txroot(tx);
	p = r->data;
	e = p + m->count;
	if(m->offset == 0){
		gethdr(t, tx->store, &h);
		v = tx->selver ? tx->selver : h.n;
		tx->selver = 0;		/* 'v' and 's': this read's, even if it fails */
		if(v > h.n || v < h.k && v != 0){
			tx->sel = 0;
			error("object version not kept");
		}
		if(tx->scan == nil)
			tx->scan = emalloc(sizeof(Scan), 1);
		pfx[0] = Kobj;
		PACK64(pfx+1, tx->store);
		if(tx->sel){
			PACK64(pfx+9, tx->seloid);
			btnewscan(fs, tx->scan, pfx, Objoidksz);
		}else
			btnewscan(fs, tx->scan, pfx, Objhdrksz);
		tx->sel = 0;
		tx->rver = v;
		tx->roff = 0;
		tx->roid = ~0ULL;
		tx->rstate = Rskip;
		tx->npend = 0;
		/* the header first */
		b[0] = 'H';
		PACK64(b+1, v);
		PACK64(b+9, h.k);
		PACK32(b+17, h.nuser);
		memmove(b+21, h.user, h.nuser);
		emit(tx, &p, e, b, 21+h.nuser);
	}else{
		if(tx->scan == nil || m->offset != tx->roff)
			error(Edscan);
		if(tx->npend > 0){
			n = tx->npend;
			tx->npend = 0;
			memmove(b, tx->pend, n);
			emit(tx, &p, e, b, n);
		}
	}
	if(tx->npend == 0 && !tx->scan->donescan){
		btenter(t, tx->scan);
		if(waserror()){
			btexit(tx->scan);
			nexterror();
		}
		while(p < e && btnext(tx->scan, &tx->scan->kv)){
			if(tx->scan->kv.nk != Objksz)
				continue;
			oid = UNPACK64(tx->scan->kv.k+9);
			v = keyver(tx->scan->kv.k);
			if(oid != tx->roid){
				tx->roid = oid;
				tx->rstate = Rseek;
			}
			if(tx->rstate == Rskip || v > tx->rver)
				continue;
			if(tx->rstate == Rseek){
				if(UNPACK32(tx->scan->kv.k+25) != 0
				|| tx->scan->kv.nv < 4)
					continue;	/* left over */
				len = UNPACK32(tx->scan->kv.v);
				tx->rstate = Rskip;
				if(len == Objtomb)
					continue;
				b[0] = 'o';
				PACK64(b+1, oid);
				PACK64(b+9, v);
				PACK64(b+17, (uvlong)len);
				n = tx->scan->kv.nv - 4;
				if(n > len)
					n = len;
				memmove(b+Objrhdr, tx->scan->kv.v+4, n);
				tx->rleft = len - n;
				if(tx->rleft > 0){
					tx->rstate = Rdata;
					tx->ridx = 1;
					tx->rcver = v;
				}
				if(!emit(tx, &p, e, b, Objrhdr+n))
					break;
				continue;
			}
			/* Rdata */
			if(v != tx->rcver || UNPACK32(tx->scan->kv.k+25) != tx->ridx)
				error(Ecorrupt);
			n = tx->scan->kv.nv;
			if(n > tx->rleft)
				n = tx->rleft;
			tx->rleft -= n;
			tx->ridx++;
			if(tx->rleft == 0)
				tx->rstate = Rskip;
			if(!emit(tx, &p, e, tx->scan->kv.v, n))
				break;
		}
		btexit(tx->scan);
		poperror();
	}
	/* a version cleaned away under the scan: the reader starts over */
	gethdr(t, tx->store, &h);
	if(tx->rver != 0 && tx->rver < h.k)
		error("object version not kept");
	room = p - r->data;
	r->count = room;
	tx->roff += room;
	wunlock(f);
	poperror();
}

/*
 * The fid's end (must not raise error()): a transaction left open goes
 * to the sweeper as AOobjabort, in the Amsg its begin made
 */
void
objclunk(Fid *f, Amsg **ao)
{
	Otx *tx;

	if(!(f->dent->qid.type & QTOBJ) || (tx = f->aux) == nil)
		return;	/* aux is others' too (ctl) */
	f->aux = nil;
	free(tx->scan);
	tx->scan = nil;
	if(tx->ver == 0 || *ao != nil){
		/* not a transaction (nor in fs->otxs), or rclose: no store */
		free(tx->abort);
		free(tx->ent);
		free(tx);
		return;
	}
	*ao = tx->abort;
	tx->abort = nil;
	memset(*ao, 0, sizeof(Amsg));
	(*ao)->op = AOobjabort;
	(*ao)->mnt = f->mnt;
	aincl(&f->mnt->ref, 1);
	(*ao)->otx = tx;
}

/*
 * Plan2001: a removed store's pending mark, Kobj ~0 qpath: put by
 * fsremove in the dirent's delete, clobbered when objsweep has swept
 * the store's keys.  A crash between leaves the mark, and objresume
 * sweeps on at the next start.
 */
int
objpending(char *k, vlong qpath)
{
	k[0] = Kobj;
	PACK64(k+1, ~0ULL);
	PACK64(k+9, qpath);
	return Objoidksz;
}

/* the sweeper's, once at the start: the removals a crash cut short */
void
objresume(Gefs *fs, int id)
{
	char pfx[Objhdrksz];
	vlong *q, *nq;
	int n, a, i;
	Mount *mnt;
	Amsg am;
	Scan s;

	if(waserror()){
		fprint(2, "gefs: removals not resumed: %s\n", errmsg());
		return;
	}
	mnt = getmount(fs, "main");
	poperror();
	q = nil;
	n = a = 0;
	epochstart(fs, id);
	if(waserror()){
		epochend(fs, id);
		fprint(2, "gefs: removals not resumed: %s\n", errmsg());
		free(q);
		clunkmount(mnt);
		return;
	}
	pfx[0] = Kobj;
	PACK64(pfx+1, ~0ULL);
	btnewscan(fs, &s, pfx, sizeof(pfx));
	btenter(agetp(&mnt->root), &s);
	while(btnext(&s, &s.kv)){
		if(s.kv.nk != Objoidksz)
			continue;
		if(n == a){
			a = a ? 2*a : 16;
			if((nq = realloc(q, a*sizeof(vlong))) == nil){
				btexit(&s);
				error(Enomem);
			}
			q = nq;
		}
		q[n++] = UNPACK64(s.kv.k+9);
	}
	btexit(&s);
	epochend(fs, id);
	poperror();
	for(i = 0; i < n; i++){
		print("gefs: store %llx: its removal resumed\n", q[i]);
		memset(&am, 0, sizeof(am));
		am.op = AOobjclear;
		am.mnt = mnt;
		am.qpath = q[i];
		if(waserror()){
			fprint(2, "gefs: store %llx: %s\n", q[i], errmsg());
			continue;
		}
		objsweep(fs, id, &am);
		poperror();
	}
	free(q);
	clunkmount(mnt);
}

/*
 * Plan2001: the clobbers of a removed range down to its leaves, leaf
 * by leaf.  They waited in the pivots' buffers, and the leaves kept
 * the removed values: the tree grew by ~20 MiB per removed 40 MiB
 * store (T5.4).
 */
static void
drainrange(Otx *tx, char *pfx, int npfx)
{
	Gefs *fs = tx->fs;
	char kb[Keymax], nb[Keymax+Inlmax];
	Key k;
	Kvp next;

	memmove(kb, pfx, npfx);
	k.k = kb;
	k.nk = npfx;
	while(txdrain(tx, &k, &next, nb, sizeof(nb))){
		if(next.nk < npfx || memcmp(next.k, pfx, npfx) != 0)
			break;
		memmove(kb, next.k, next.nk);
		k.nk = next.nk;
		if(tx->id >= 0 && ++tx->nflush >= Objyield){
			tx->nflush = 0;
			epochend(fs, tx->id);
			qunlock(&fs->mutlk);
			epochclean(fs);
			qlock(&fs->mutlk);
			epochstart(fs, tx->id);
		}
	}
}

/* the sweeper's: a fid's transaction, or a removed store's keys */
void
objsweep(Gefs *fs, int id, Amsg *am)
{
	char pfx[Objhdrksz], pk[Objoidksz];
	Otx *tx;

	qlock(&fs->mutlk);
	epochstart(fs, id);
	if(waserror()){
		epochend(fs, id);
		qunlock(&fs->mutlk);
		if(am->otx != nil){
			tx = am->otx;
			endtx(tx);
			free(tx->abort);
			free(tx);
			am->otx = nil;
		}
		nexterror();
	}
	if(am->op == AOobjabort){
		tx = am->otx;
		tx->id = id;
		tx->mnt = am->mnt;
		tx->tree = nil;
		abort1(tx);
		free(tx->abort);
		free(tx);
		am->otx = nil;
	}else{
		tx = emalloc(sizeof(Otx), 1);
		tx->fs = fs;
		tx->mnt = am->mnt;
		tx->id = id;
		if(waserror()){
			free(tx);
			nexterror();
		}
		pfx[0] = Kobj;
		PACK64(pfx+1, am->qpath);
		sweep(tx, pfx, sizeof(pfx), dropall, nil);
		/* swept: the removal's pending mark out */
		addmsg(tx, Oclobber, pk, objpending(pk, am->qpath), nil, 0);
		flushtx(tx);
		drainrange(tx, pfx, sizeof(pfx));
		poperror();
		free(tx);
	}
	epochend(fs, id);
	qunlock(&fs->mutlk);
	poperror();
}

/* the fid's transaction out now: its store's removal, in the mutator */
void
objabortfid(Fid *f, int id)
{
	Otx *tx;

	if(!(f->dent->qid.type & QTOBJ) || (tx = f->aux) == nil || tx->ver == 0)
		return;
	tx->id = id;
	abort1(tx);
}

/*
 * Plan2001 Stage 3: the native O object API (docs/o-volume-format.md).
 * A Space is the catalog's stable SpaceID; its objects are the same
 * Kobj records the DMOBJ adapter uses, in the O volume's snapshot tree.
 * There is no Fid, qid, mount, file or path: these calls enter the
 * engine directly.  The handle's lock is the PoC's single writer.
 */

Ospace*
ospaceopen(Gefs *fs, char *name)
{
	Ospace *sp;
	uvlong sid;

	if(!(fs->flag & Ovolflag))
		error("O: not an O volume");
	sid = ospaceid(fs, name);
	sp = emalloc(sizeof(Ospace), 1);
	sp->fs = fs;
	sp->sid = sid;
	return sp;
}

static Otx*
ospacetx(Ospace *sp)
{
	Otx *tx;

	if((tx = sp->tx) != nil)
		return tx;
	tx = emalloc(sizeof(Otx), 1);
	tx->fs = sp->fs;
	tx->mnt = nil;
	tx->tree = &sp->fs->snap;
	tx->store = sp->sid;
	tx->id = -1;
	sp->tx = tx;
	return tx;
}

static void
ospaceputdata(Otx *tx, uvlong oid, void *data, long n)
{
	char *p;
	long k;

	begin(tx);
	if(n < 0 || n > Objtxmax || tx->bytes + n > Objtxmax)
		error("object transaction too big");
	if(!dataroom(tx->fs, 2*n + Blksz))
		error(Efull);
	tx->oid = oid;
	tx->left = 0;
	tx->bytes += n;
	tx->idx = 0;
	PACK32(tx->chunk, (u32int)n);
	tx->nchunk = 4;
	p = data;
	do{
		k = Objchunk - tx->nchunk;
		if(k > n)
			k = n;
		if(k > 0){
			memmove(tx->chunk+tx->nchunk, p, k);
			p += k;
			n -= k;
			tx->nchunk += k;
		}
		if(tx->nchunk == Objchunk || n == 0)
			putchunk(tx);
	}while(n > 0);
	addent(tx, oid, tx->idx);
}

void
ospaceput(Ospace *sp, uvlong oid, void *data, long n)
{
	Otx *tx;

	qlock(&sp->lk);
	if(waserror()){
		qunlock(&sp->lk);
		nexterror();
	}
	tx = ospacetx(sp);
	qlock(&sp->fs->mutlk);
	if(waserror()){
		if(!waserror()){
			abort1(tx);
			poperror();
		}
		qunlock(&sp->fs->mutlk);
		nexterror();
	}
	ospaceputdata(tx, oid, data, n);
	flushtx(tx);
	qunlock(&sp->fs->mutlk);
	poperror();
	qunlock(&sp->lk);
	poperror();
}

uvlong
ospacecommit(Ospace *sp)
{
	Gefs *fs = sp->fs;
	Otx *tx;
	Ohdr h;
	uvlong v;

	qlock(&sp->lk);
	if(waserror()){
		qunlock(&sp->lk);
		nexterror();
	}
	if((tx = sp->tx) == nil || tx->ver == 0){
		gethdr(&fs->snap, sp->sid, &h);
		v = h.n;
	}else{
		qlock(&fs->mutlk);
		if(waserror()){
			if(!waserror()){
				abort1(tx);
				poperror();
			}
			qunlock(&fs->mutlk);
			nexterror();
		}
		v = tx->ver;
		commit(tx);
		qunlock(&fs->mutlk);
		poperror();
		gesync(fs, 0);
	}
	qunlock(&sp->lk);
	poperror();
	return v;
}

/*
 * The newest published version of oid, or -1 if there is none.  A
 * tombstone is not an object.  The whole value must fit in buf: the PoC
 * has no streaming read without a Fid.
 */
long
ospaceget(Ospace *sp, uvlong oid, void *buf, long nbuf, uvlong *ver)
{
	Gefs *fs;
	Tree *t;
	Scan *s;
	Ohdr h;
	char pfx[Objoidksz];
	uvlong v, sel;
	u32int idx, len;
	long n, total;
	int found, tomb, want;

	if(ver != nil)
		*ver = 0;
	fs = sp->fs;
	qlock(&sp->lk);
	if(waserror()){
		qunlock(&sp->lk);
		nexterror();
	}
	t = &fs->snap;
	if(!gethdr(t, sp->sid, &h) || h.n == 0){
		qunlock(&sp->lk);
		poperror();
		return -1;
	}
	s = emalloc(sizeof(Scan), 1);
	if(waserror()){
		free(s);
		nexterror();
	}
	pfx[0] = Kobj;
	PACK64(pfx+1, sp->sid);
	PACK64(pfx+9, oid);
	btnewscan(fs, s, pfx, Objoidksz);
	btenter(t, s);
	if(waserror()){
		btexit(s);
		nexterror();
	}
	found = tomb = 0;
	total = 0;
	sel = 0;
	len = 0;
	want = 0;
	while(btnext(s, &s->kv)){
		if(s->kv.nk != Objksz)
			continue;
		v = keyver(s->kv.k);
		if(v > h.n)
			continue;
		idx = UNPACK32(s->kv.k+25);
		if(!found){
			if(idx != 0 || s->kv.nv < 4)
				continue;
			len = UNPACK32(s->kv.v);
			if(len == Objtomb){
				tomb = 1;
				break;
			}
			/* Plan2001 Stage 4: a size query (buf nil, nbuf 0) */
			if(buf == nil && nbuf == 0){
				sel = v;
				found = 1;
				total = len;
				break;
			}
			if((long)len > nbuf)
				error("object larger than buffer");
			sel = v;
			found = 1;
			want = 1;
			n = s->kv.nv - 4;
			if(n > len)
				n = len;
			if(n > 0)
				memmove(buf, s->kv.v+4, n);
			total = n;
			if(total == len)
				break;
			continue;
		}
		if(v != sel)
			break;
		if(idx != want)
			error(Ecorrupt);
		n = s->kv.nv;
		if(n > len - total)
			n = len - total;
		if(n > 0)
			memmove((char*)buf+total, s->kv.v, n);
		total += n;
		want++;
		if(total == len)
			break;
	}
	btexit(s);
	poperror();
	free(s);
	poperror();
	qunlock(&sp->lk);
	poperror();
	if(tomb)
		return -1;
	if(!found)
		return -1;
	if(total != len)
		error(Ecorrupt);
	if(ver != nil)
		*ver = sel;
	return total;
}

void
ospaceclose(Ospace *sp)
{
	Gefs *fs;
	Otx *tx;

	if(sp == nil)
		return;
	fs = sp->fs;
	qlock(&sp->lk);
	tx = sp->tx;
	sp->tx = nil;
	if(tx != nil && tx->ver != 0){
		qlock(&fs->mutlk);
		if(waserror()){
			endtx(tx);
			qunlock(&fs->mutlk);
			print("O: space %llud: transaction aborted late\n", sp->sid);
		}else{
			abort1(tx);
			qunlock(&fs->mutlk);
			poperror();
		}
	}
	if(tx != nil){
		free(tx->scan);
		free(tx->abort);
		free(tx);
	}
	qunlock(&sp->lk);
	free(sp);
}
