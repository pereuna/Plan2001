#include "gefs.h"

/*
 * Plan2001: the blocks check reaches (trees, data, logs, deadlists
 * and their entries), by address: a block neither free nor reached
 * is leaked.  Meaningful after a sync, with nothing in flight.
 */
static uchar	*reach;
static vlong	nreach;
enum { Rtree, Rdata, Rlog, Rdead, Nr };
static vlong	nreached[Nr];
static char	*rname[Nr] = { "tree", "data", "log", "dead" };
static vlong	nleaf, nleaf0, nleaf1, nkv, npiv, nmsg, nclob;	/* the tree's shape */

static void
mark(vlong addr, int kind)
{
	vlong i;

	i = addr/Blksz;
	if(reach != nil && i >= 0 && i < nreach && !(reach[i/8] & 1<<(i%8))){
		reach[i/8] |= 1<<(i%8);
		nreached[kind]++;
	}
}

static int
isfree(vlong bp)
{
	Arange *r, q;
	Arena *a;

	q.off = bp;
	q.len = Blksz;

	a = getarena(bp);
	r = (Arange*)avllookup(a->free, &q, -1);
	if(r == nil)
		return 0;
	return bp < (r->off + r->len);
}

static int
checktree(int fd, Blk *b, int h, vlong pred, Kvp *lo, Kvp *hi)
{
	Kvp x, y;
	Msg mx, my;
	int i, r, fill;
	Blk *c;
	int fail;
	Bptr bp;

	fail = 0;
	mark(b->bp.addr, Rtree);
	if(b->type == Tleaf){
		nleaf++;
		nkv += b->nval;
		if(b->nval == 0)
			nleaf0++;
		else if(b->nval < 4)
			nleaf1++;
	}else{
		npiv++;
		nmsg += b->nbuf;
		for(i = 0; i < b->nbuf; i++){
			getmsg(b, i, &mx);
			if(mx.op == Oclobber)
				nclob++;
		}
	}
	if(h < 0){
		fprint(fd, "node too deep (loop?\n");
		fail++;
		return fail;
	} 
	if(b->type == Tleaf){
		if(h != 0){
			fprint(fd, "unbalanced leaf\n");
			fail++;
		}
		if(h != 0 && b->nval < 2 && debug){
			fprint(fd, "warning: underfilled leaf %B\n", b->bp);
			fail++;
		}
	}
	if(b->type == Tpivot && b->nval < 2 && debug)
		fprint(fd, "warning: underfilled pivot %B\n", b->bp);
	getval(b, 0, &x);
	if(lo && keycmp(lo, &x) > 0){
		fprint(fd, "out of range keys %P != %P\n", lo, &x);
		showblk(fd, b, "out of range", 1);
		fail++;
	}
	for(i = 1; i < b->nval; i++){
		getval(b, i, &y);
		if(hi && keycmp(&y, hi) >= 0){
			fprint(fd, "out of range keys %P >= %P\n", &y, hi);
			fail++;
		}
		if(b->type == Tpivot){
			bp = getptr(&x, &fill);
			/* Plan2001: the free list against every pointer, older ones too */
			if(isfree(bp.addr)){
				fprint(fd, "freed block in use: %B\n", bp);
				fail++;
			}
			if(bp.gen <= pred)
				goto Skip;
			if((c = getblk(bp, 0)) == nil){
				fprint(fd, "corrupt block: %B\n", bp);
				fail++;
				continue;
			}
			if(blkfill(c) != fill){
				fprint(fd, "mismatched block fill\n");
				fail++;
			}
			if(checktree(fd, c, h - 1, pred, &x, &y))
				fail++;
			dropblk(c);
		}
Skip:
		r = keycmp(&x, &y);
		switch(r){
		case -1:
			break;
		case 0:
			fprint(fd, "duplicate keys %P, %P\n", &x, &y);
			fail++;
			break;
		case 1:
			fprint(fd, "misordered keys %P, %P\n", &x, &y);
			fail++;
			break;
		}
		x = y;
	}
	if(b->type == Tpivot){
		getval(b, b->nval-1, &y);
		bp = getptr(&x, &fill);
		if(isfree(bp.addr)){	/* Plan2001: the last child was not looked at */
			fprint(fd, "freed block in use: %B\n", bp);
			fail++;
		}
		if(bp.gen > pred){
			if((c = getblk(bp, 0)) == nil){
				fprint(fd, "corrupt block: %B\n", bp);
				fail++;
			}
			if(c != nil && checktree(fd, c, h - 1, pred, &y, nil))
				fail++;
			dropblk(c);
		}
		if(b->nbuf > 0){
			getmsg(b, 0, &mx);
			if(hi && keycmp(&mx, hi) >= 0){
				fprint(fd, "out of range messages %P != %N\n", hi, &mx);
				fail++;
			}
		}
		for(i = 1; i < b->nbuf; i++){
			getmsg(b, i, &my);
			switch(my.op){
			case Owstat:		/* kvp dirent */
				if((my.v[0] & ~(Owsize|Owmode|Owmtime|Owatime|Owuid|Owgid|Owmuid)) != 0){
					fprint(fd, "invalid stat op %x\n", my.v[0]);
					fail++;
				}
				break;
			default:
				if(my.op <= 0 || my.op >= Nmsgtype){
					fprint(fd, "invalid message op %d\n", my.op);
					fail++;
				}
				break;
			}
			if(hi && keycmp(&y, hi) > 0){
				fprint(fd, "out of range keys %P >= %P\n", &y, hi);
				fail++;
			}
			if(keycmp(&mx, &my) == 1){
				fprint(fd, "misordered keys %P, %P\n", &x, &y);
				fail++;
				break;
			}
			mx = my;
		}

	}
	return fail;
}

static int
checklog(int fd, Bptr hd, Bptr tl)
{
	Bptr pb, bp, nb;
	Blk *b;
	
	pb = Zb;
	for(bp = hd; bp.addr != -1; bp = nb){
		if(waserror()){
			fprint(fd, "error loading %B\n", bp);
			return 0;
		}
		pb = bp;
		mark(bp.addr, Rlog);
		if(isfree(bp.addr)){	/* Plan2001 */
			fprint(fd, "freed log block in use: %B\n", bp);
			poperror();
			return 0;
		}
		b = getblk(bp, 0);
		nb = b->logp;
		dropblk(b);
		poperror();
		if(bp.addr == tl.addr)
			break;
	}
	if(tl.addr != -1 && pb.addr != tl.addr){
		fprint(fd, "truncated chain %B\n", hd);
		return 0;
	}
	return 1;
}

static int
checkfree(int fd)
{
	Arena *a;
	Arange *r, *n;
	int i, ok;

	ok = 1;
	for(i = 0; i < fs->narena; i++){
		a = &fs->arenas[i];
		qlock(a);
		r = (Arange*)avlmin(a->free);
		for(n = (Arange*)avlnext(r); n != nil; n = (Arange*)avlnext(n)){
			if(r->off >= n->off){
				fprint(fd, "misordered length %llx >= %llx\n", r->off, n->off);
				ok = 0;
			}
			if(r->off+r->len >= n->off){
				fprint(fd, "overlaping range %llx+%llx >= %llx\n", r->off, r->len, n->off);
				ok = 0;
			}
			r = n;
		}
		if(!checklog(fd, a->loghd, Zb)){
			fprint(fd, "arena %d: broken freelist\n", i);
			ok = 0;
		}
		qunlock(a);
	}
	return ok;
}

/* Plan2001: a deadlist's entries, the killed blocks, are not free yet */
static void
markdl(Bptr hd, Bptr tl)
{
	Bptr bp;
	Blk *b;
	char *p;
	int done;

	done = 0;
	for(bp = hd; bp.addr != -1 && !done; bp = b->logp){
		b = getblk(bp, 0);
		for(p = b->data; p != b->data+b->logsz; p += 8)
			mark(UNPACK64(p), Rdead);
		done = (bp.addr == tl.addr);
		dropblk(b);
	}
}

static int
checkdlist(int fd)
{
	char pfx[1];
	Dlist dl;
	Scan s;
	int ok;

	ok = 1;
	if(!checklog(fd, fs->snapdl.hd, fs->snapdl.tl)){
		fprint(fd, "bad snap dlist (%B, %B): %s\n", fs->snapdl.hd, fs->snapdl.tl, errmsg());
		ok = 0;
	}else
		markdl(fs->snapdl.hd, fs->snapdl.tl);
	pfx[0] = Kdlist;
	btnewscan(&s, pfx, 1);
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		kv2dlist(&s.kv, &dl);
		if(!checklog(fd, dl.hd, dl.tl)){
			fprint(fd, "bad dlist %P: %s\n", &s.kv, errmsg());
			ok = 0;
		}else
			markdl(dl.hd, dl.tl);
	}
	btexit(&s);
	return ok;
}

static int
checkdata(int fd, Tree *t, vlong pred)
{
	char pfx[1];
	Bptr bp;
	Scan s;
	Blk *b;

	pfx[0] = Kdat;
	btnewscan(&s, pfx, 1);
	btenter(t, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		if(waserror()){
			btexit(&s);
			nexterror();
		}
		bp = unpackbp(s.kv.v, s.kv.nv);
		mark(bp.addr, Rdata);
		if(isfree(bp.addr)){	/* Plan2001: older data too */
			fprint(fd, "free block in use: %B\n", bp);
			error("free block in use");
		}
		if(bp.gen > pred){
			b = getblk(bp, GBraw);
			dropblk(b);
		}
		poperror();
	}
	btexit(&s);
	return 0;
}

static vlong
countlbl(int fd, vlong id)
{
	char pfx[1];
	int nref;
	Scan s;

	nref = 0;
	pfx[0] = Klabel;
	btnewscan(&s, pfx, 1);
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		if(s.kv.nv >= 9 && UNPACK64(s.kv.v+1) == id){
			fprint(fd, "\tlabel %.*s => %lld\n", (int)(s.kv.nk-1), s.kv.k+1, UNPACK64(s.kv.v+1));
			nref++;
		}
	}
	btexit(&s);
	return nref;
}

static vlong
countref(vlong id)
{
	char pfx[1];
	Tree t;
	int nref;
	Scan s;

	nref = 0;
	pfx[0] = Ksnap;
	btnewscan(&s, pfx, 1);
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		unpacktree(&t, s.kv.v, s.kv.nv);
		if(t.pred == -1 && t.base == id)
			nref++;
	}
	btexit(&s);
	return nref;
}

/* Plan2001: the blocks neither free nor reached */
static int
checkleak(int fd)
{
	vlong addr, base, n, i;
	Arena *a;
	Blk *b;
	int j;

	n = 0;
	for(j = 0; j < fs->narena; j++){
		a = &fs->arenas[j];
		base = a->h0->bp.addr + 2*Blksz;
		for(addr = base; addr < base + a->size; addr += Blksz){
			i = addr/Blksz;
			if(i >= nreach || (reach[i/8] & 1<<(i%8)) || isfree(addr))
				continue;
			if(n++ < 10){
				if(waserror()){
					fprint(fd, "leaked block %llx: unreadable\n", addr);
					continue;
				}
				b = getblk((Bptr){addr, -1, -1}, GBnochk);
				fprint(fd, "leaked block %llx: type %d\n", addr, b->type);
				dropblk(b);
				poperror();
			}
		}
	}
	fprint(fd, "leaked blocks: %lld (%lld KiB)\n", n, n*Blksz/1024);
	for(j = 0; j < Nr; j++)
		fprint(fd, "reached %s: %lld KiB\n", rname[j], nreached[j]*Blksz/1024);
	fprint(fd, "reached leaves %lld (empty %lld, < 4 %lld) kvs %lld; pivots %lld msgs %lld clobbers %lld\n",
		nleaf, nleaf0, nleaf1, nkv, npiv, nmsg, nclob);
	return n == 0;
}

int
checkfs(int fd)
{
	int ok, height, nref, nlbl;
	vlong gen;
	char pfx[1];
	Arena *a;
	Tree *t;
	Scan s;
	Blk *b;

	ok = 1;
	assert(!canqlock(&fs->mutlk));
	a = &fs->arenas[fs->narena-1];
	nreach = (a->h0->bp.addr + a->size + 2*Blksz)/Blksz + 1;
	reach = emalloc((nreach+7)/8, 1);
	memset(nreached, 0, sizeof(nreached));
	nleaf = nleaf0 = nleaf1 = nkv = npiv = nmsg = nclob = 0;
	mark(fs->sb0->bp.addr, Rlog);
	mark(fs->sb1->bp.addr, Rlog);
	if(waserror()){
		fprint(fd, "error checking %s\n", errmsg());
		free(reach);
		reach = nil;
		return 0;
	}
	fprint(fd, "checking freelist\n");
	if(!checkfree(fd))
		ok = 0;
	fprint(fd, "checking deadlist\n");
	if(!checkdlist(fd))
		ok = 0;
	fprint(fd, "checking snap tree: %B\n", fs->snap.bp);
	if((b = getroot(&fs->snap, &height)) != nil){
		if(checktree(fd, b, height-1, -1, nil, 0))
			ok = 0;
		dropblk(b);
	}
	pfx[0] = Ksnap;
	btnewscan(&s, pfx, 1);
	btenter(&fs->snap, &s);
	while(1){
		if(!btnext(&s, &s.kv))
			break;
		gen = UNPACK64(s.kv.k+1);
		if(waserror()){
			fprint(fd, "moving on: %s\n", errmsg());
			ok = 0;
			continue;
		}
		if((t = opentree(gen)) == nil){
			fprint(fd, "invalid snap id %lld\n", gen);
			ok = 0;
			poperror();
			continue;
		}
		fprint(fd, "checking snap %lld: %B\n", gen, t->bp);
		if(waserror()){
			closesnap(t);
			nexterror();
		}
		nref = countref(gen);
		nlbl = countlbl(fd, gen);
		fprint(fd, "\tnref %d nlbl %d\n", nref, nlbl);
		if(t->nref < nref || t->nlbl < nlbl){
			fprint(fd, "mismatched refs: (%d, %d) != (%d, %d)\n", t->nref, nref, t->nlbl, nlbl);
			ok = 0;
		}
		b = getroot(t, &height);
		if(waserror()){
			dropblk(b);
			nexterror();
		}
		if(checktree(fd, b, height-1, t->pred, nil, 0))
			ok = 0;
		if(checkdata(fd, t, t->pred))
			ok = 0;
		dropblk(b);
		poperror();
		closesnap(t);
		poperror();
		poperror();
	}
	btexit(&s);
	/* Plan2001: reported, not failed: in-flight frees look leaked */
	checkleak(fd);
	free(reach);
	reach = nil;
	poperror();
	if(ok)
		fprint(fd, "ok\n");
	else
		fprint(fd, "not ok\n");
	return ok;
}
