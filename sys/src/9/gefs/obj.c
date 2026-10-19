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
	Objbatch	= 32,		/* messages to an upsert */
	Objyield	= 16,		/* upserts between flushtx's yields */
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
	Otx	*next;		/* the active ones, under mutlk */
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
	Msg	mb[Objbatch];
	char	kb[Objbatch][Objksz];
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

static Otx *otxs;	/* stores with a transaction, under mutlk */

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
puthdr(Mount *mnt, vlong store, Ohdr *h)
{
	char kb[Objhdrksz], vb[24+Objuser];
	Msg m;

	kb[0] = Kobj;
	PACK64(kb+1, store);
	PACK64(vb, h->n);
	PACK64(vb+8, h->k);
	PACK64(vb+16, h->w);
	memmove(vb+24, h->user, h->nuser);
	m.op = Oinsert;
	m.k = kb;
	m.nk = sizeof(kb);
	m.v = vb;
	m.nv = 24 + h->nuser;
	upsert(mnt, &m, 1);
}

/*
 * Plan2001: a long run of upserts (a store removed, an abort, the
 * history swept) lets go of mutlk and its epoch every Objyield
 * batches, as AOclear's batches do: the blocks it freed leave limbo.
 * Held through it all, the Bfree pool and the cache ran out with
 * mutlk held, and gefs stopped.  No scan may be entered here.
 */
static void
flushtx(Mount *mnt, Otx *tx)
{
	if(tx->nm == 0)
		return;
	upsert(mnt, tx->mb, tx->nm);
	tx->nm = 0;
	if(tx->id >= 0 && ++tx->nflush >= Objyield){
		tx->nflush = 0;
		epochend(tx->id);
		qunlock(&fs->mutlk);
		epochclean();
		qlock(&fs->mutlk);
		epochstart(tx->id);
	}
}

static void
addmsg(Mount *mnt, Otx *tx, int op, char *k, int nk, char *v, int nv)
{
	Msg *m;

	if(tx->nm == Objbatch)
		flushtx(mnt, tx);
	m = &tx->mb[tx->nm];
	memmove(tx->kb[tx->nm], k, nk);
	if(nv > 0)
		memmove(tx->vb[tx->nm], v, nv);
	m->op = op;
	m->k = tx->kb[tx->nm];
	m->nk = nk;
	m->v = tx->vb[tx->nm];
	m->nv = nv;
	tx->nm++;
}

/*
 * The keys of store under pfx for which drop() says so, out: in
 * batches, each from a scan of its own (as readdir's: entered and left
 * again, so the tree may change between them).  Called with mutlk.
 */
static void
sweep(Mount *mnt, Otx *tx, char *pfx, int npfx, int (*drop)(char*, int, void*), void *a)
{
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
		btnewscan(s, pfx, npfx);
		if(nlast > 0){
			memmove(s->kvbuf, last, nlast);
			s->kv.k = s->kvbuf;
			s->kv.nk = nlast;
			s->first = 0;
		}
		btenter(agetp(&mnt->root), s);
		more = 0;
		n = 0;
		while(btnext(s, &s->kv)){
			if(drop(s->kv.k, s->kv.nk, a)){
				addmsg(mnt, tx, Oclobber, s->kv.k, s->kv.nk, nil, 0);
				n++;
			}
			if(n == Objbatch-1 || tx->nm == Objbatch){
				memmove(last, s->kv.k, s->kv.nk);
				nlast = s->kv.nk;
				more = 1;
				break;
			}
		}
		btexit(s);
		flushtx(mnt, tx);
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

static Otx*
gettx(Fid *f)
{
	Otx *tx;

	if((tx = f->aux) == nil){
		tx = emalloc(sizeof(Otx), 1);
		tx->store = f->qpath;
		tx->id = -1;
		f->aux = tx;
	}
	return tx;
}

static void
begin(Mount *mnt, Otx *tx)
{
	Ohdr h;
	Otx *o;

	if(tx->ver != 0)
		return;
	for(o = otxs; o != nil; o = o->next)
		if(o->store == tx->store)
			error("object store busy");
	if(tx->abort == nil)
		tx->abort = emalloc(sizeof(Amsg), 1);
	gethdr(agetp(&mnt->root), tx->store, &h);
	if(h.w > h.n){
		/* a transaction cut short: its keys out */
		char pfx[Objhdrksz];

		pfx[0] = Kobj;
		PACK64(pfx+1, tx->store);
		sweep(mnt, tx, pfx, sizeof(pfx), dropunpub, &h.n);
	}
	h.w = h.n + 1;
	puthdr(mnt, tx->store, &h);
	tx->ver = h.w;
	tx->nent = 0;
	tx->bytes = 0;
	tx->newuser = 0;
	tx->next = otxs;
	otxs = tx;
}

static void
endtx(Otx *tx)
{
	Otx **p;

	for(p = &otxs; *p != nil; p = &(*p)->next)
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
abort1(Mount *mnt, Otx *tx)
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
			addmsg(mnt, tx, Oclobber, k, Objksz, nil, 0);
		}
	flushtx(mnt, tx);
	gethdr(agetp(&mnt->root), tx->store, &h);
	h.w = h.n;
	puthdr(mnt, tx->store, &h);
	poperror();
	endtx(tx);
}

static void
putchunk(Mount *mnt, Otx *tx)
{
	char k[Objksz];

	packkey(k, tx->store, tx->oid, tx->ver, tx->idx);
	addmsg(mnt, tx, Oinsert, k, Objksz, tx->chunk, tx->nchunk);
	tx->idx++;
	tx->nchunk = 0;
}

static void
commit(Mount *mnt, Otx *tx)
{
	char pfx[Objoidksz];
	Ohdr h;
	Oent *e;
	Oold o;

	if(tx->ver == 0)
		return;
	flushtx(mnt, tx);
	gethdr(agetp(&mnt->root), tx->store, &h);
	if(h.w != tx->ver)
		error(Ecorrupt);
	h.n = tx->ver;
	if(h.n >= Objkeep && h.k < h.n - Objkeep + 1)
		h.k = h.n - Objkeep + 1;
	if(h.k == 0)
		h.k = 1;
	if(tx->newuser){
		h.nuser = tx->nuser;
		memmove(h.user, tx->user, tx->nuser);
	}
	/* published: one message */
	puthdr(mnt, tx->store, &h);

	/* then the versions none kept sees */
	pfx[0] = Kobj;
	PACK64(pfx+1, tx->store);
	for(e = tx->ent; e < tx->ent+tx->nent; e++){
		PACK64(pfx+9, e->oid);
		o.k = h.k;
		o.keep = 0;
		sweep(mnt, tx, pfx, sizeof(pfx), dropold, &o);
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
	char *p, *e;
	int n, need, sync;
	Mount *mnt;
	Otx *tx;

	tx = gettx(f);
	tx->id = id;
	mnt = f->mnt;
	sync = 0;
	if(waserror()){
		if(!waserror()){
			abort1(mnt, tx);
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
				putchunk(mnt, tx);
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
			begin(mnt, tx);
			tx->oid = UNPACK64(tx->rec+1);
			tx->left = UNPACK64(tx->rec+9);
			if(tx->left < 0 || tx->left > Objtxmax
			|| tx->bytes + tx->left > Objtxmax)
				error("object transaction too big");
			if(!dataroom(2*tx->left + Blksz))
				error(Efull);
			tx->bytes += tx->left;
			tx->idx = 0;
			PACK32(tx->chunk, (u32int)tx->left);
			tx->nchunk = 4;
			addent(tx, tx->oid, 1);
			if(tx->left == 0)
				putchunk(mnt, tx);
			break;
		case 'd':
			begin(mnt, tx);
			tx->oid = UNPACK64(tx->rec+1);
			tx->idx = 0;
			PACK32(tx->chunk, Objtomb);
			tx->nchunk = 4;
			putchunk(mnt, tx);
			addent(tx, tx->oid, 1);
			break;
		case 'h':
			begin(mnt, tx);
			tx->inuser = UNPACK32(tx->rec+1);
			if(tx->inuser < 0 || tx->inuser > Objuser)
				error("object header too big");
			tx->nuser = 0;
			tx->newuser = 1;
			break;
		case 'c':
			commit(mnt, tx);
			sync = tx->rec[1] & 1;
			break;
		case 'a':
			abort1(mnt, tx);
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
	flushtx(mnt, tx);
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
	t = agetp(&f->mnt->root);
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
			btnewscan(tx->scan, pfx, Objoidksz);
		}else
			btnewscan(tx->scan, pfx, Objhdrksz);
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
		/* not a transaction (nor in otxs), or rclose: no store */
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

/* the sweeper's: a fid's transaction, or a removed store's keys */
void
objsweep(int id, Amsg *am)
{
	char pfx[Objhdrksz];
	Otx *tx;

	qlock(&fs->mutlk);
	epochstart(id);
	if(waserror()){
		epochend(id);
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
		abort1(am->mnt, tx);
		free(tx->abort);
		free(tx);
		am->otx = nil;
	}else{
		tx = emalloc(sizeof(Otx), 1);
		tx->id = id;
		if(waserror()){
			free(tx);
			nexterror();
		}
		pfx[0] = Kobj;
		PACK64(pfx+1, am->qpath);
		sweep(am->mnt, tx, pfx, sizeof(pfx), dropall, nil);
		poperror();
		free(tx);
	}
	epochend(id);
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
	abort1(f->mnt, tx);
}
