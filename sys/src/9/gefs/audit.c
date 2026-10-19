/* Plan2001: address ownership, shared with the offline user-space checker. */
#ifndef GEFS_AUDIT_USER
#include "gefs.h"
#endif

int auditverbose;

void
auditreplayed(Arena *ar, vlong head, vlong tail, int offset, vlong gen, vlong na, vlong nf)
{
	if(auditverbose)
		fprint(2, "audit replay: arena=%llx head=%llx tail=%llx offset=%d stopgen=%lld qgen=%lld allocbytes=%lld freebytes=%lld used=%lld\n", ar->h0->bp.addr, head, tail, offset, gen, agetv(&fs->qgen), na, nf, ar->used);
}

enum {
	Atree=1, Adata=2, Alog=4, Adead=8, Aheader=16, Afree=32,
	Aphysical=64, Aactive=128, Treclaim=0x1000,
	Aowned=Atree|Adata|Alog|Adead|Aheader,
};
typedef struct Audit Audit;
struct Audit {
	uchar *map;
	vlong n;
	uint *chain;
	Bptr *seen;
	uint sequence;
	int fd;
};

static vlong
aindex(Audit *a, vlong addr)
{
	if(addr < 0 || addr%Blksz != 0 || addr/Blksz >= a->n)
		error("audit: invalid address %llx", addr);
	return addr/Blksz;
}

static void
amark(Audit *a, vlong addr, int kind)
{
	vlong i;

	i = aindex(a, addr);
	if((a->map[i] & Afree) && (kind & Aowned))
		error("audit: free block owned at %llx (kind %d)", addr, kind);
	a->map[i] |= kind;
}

/* Physical values can outlive their logical deletion in a buffered tree.
 * Report these references separately; they are not proof of ownership. */
static void
aphysical(Audit *a, Kvp *kv)
{
	Bptr bp;

	if(kv->nk == Offksz && kv->k[0] == Kdat && kv->nv == Ptrsz){
		bp = unpackbp(kv->v, kv->nv);
		amark(a, bp.addr, Aphysical);
	}
}

static void
atree(Audit *a, Bptr bp, int height)
{
	Blk *b;
	Kvp kv;
	Msg m;
	int i, fill;
	vlong idx;

	idx = aindex(a, bp.addr);
	if(a->map[idx] & Aactive)
		error("audit: tree cycle at %llx", bp.addr);
	/* Shared subtrees counted once, irrespective of snapshot pred. */
	if(a->map[idx] & Atree){
		if(a->seen[idx].hash != bp.hash || a->seen[idx].gen != bp.gen)
			error("audit: conflicting tree pointer at %llx", bp.addr);
		return;
	}
	if(height < 1 || height > 64)
		error("audit: invalid height %d", height);
	amark(a, bp.addr, Atree|Aactive);
	a->seen[idx] = bp;
	b = getblk(bp, 0);
	if(waserror()){
		dropblk(b);
		nexterror();
	}
	if((height == 1 && b->type != Tleaf) || (height > 1 && b->type != Tpivot))
		error("audit: tree type/height mismatch at %llx", bp.addr);
	for(i=0; i<b->nval; i++){
		getval(b, i, &kv);
		if(b->type == Tpivot){
			if(kv.nv != Pptrsz)
				error("audit: short child pointer");
			atree(a, getptr(&kv, &fill), height-1);
		}else
			aphysical(a, &kv);
	}
	for(i=0; i<b->nbuf; i++){
		getmsg(b, i, &m);
		aphysical(a, &m.Kvp);
	}
	a->map[idx] &= ~Aactive;
	dropblk(b);
	poperror();
}

static void
achain(Audit *a, Bptr bp, Bptr tail, int type)
{
	Blk *b;
	Bptr next;
	char *p;
	vlong limit;

	limit = a->n;
	a->sequence++;
	while(bp.addr != -1){
		if(--limit < 0)
			error("audit: chain cycle");
		if(a->chain[aindex(a, bp.addr)] == a->sequence)
			error("audit: chain cycle at %llx", bp.addr);
		a->chain[aindex(a, bp.addr)] = a->sequence;
		amark(a, bp.addr, Alog);
		b = getblk(bp, 0);
		if(waserror()){
			dropblk(b);
			nexterror();
		}
		if(b->type != (type == Treclaim ? Tdlist : type) || b->logsz < 0 || b->logsz > Logspc)
			error("audit: invalid log at %llx", bp.addr);
		if(type == Tdlist || type == Treclaim){
			if(b->logsz%8 != 0)
				error("audit: short deadlist entry");
			for(p=b->data; p<b->data+b->logsz; p+=8){
				vlong addr;

				addr = UNPACK64(p);
				if(type != Treclaim || (a->map[aindex(a, addr)] & Afree) == 0)
					amark(a, addr, Adead);
			}
		}
		next = b->logp;
		dropblk(b);
		poperror();
		if(bp.addr == tail.addr)
			return;
		bp = next;
	}
	if(tail.addr != -1)
		error("audit: truncated chain");
}

static void
adata(Audit *a, Tree *t)
{
	Scan s;
	char pfx[1];
	Bptr bp;
	Blk *b;

	atree(a, t->bp, t->ht);
	pfx[0] = Kdat;
	btnewscan(&s, pfx, 1);
	btenter(t, &s);
	if(waserror()){
		btexit(&s);
		nexterror();
	}
	while(btnext(&s, &s.kv)){
		if(s.kv.nk != Offksz || s.kv.nv != Ptrsz)
			error("audit: invalid data pointer");
		bp = unpackbp(s.kv.v, s.kv.nv);
		amark(a, bp.addr, Adata);
		b = getblk(bp, GBraw);
		dropblk(b);
	}
	btexit(&s);
	poperror();
}

#ifdef GEFS_AUDIT_USER
/* Export overlapping ownership sets for before/after address comparisons. */
static void
aprintmap(Audit *a)
{
	static char *names[] = { "tree", "data", "log", "dead", "header", "free", "physical" };
	vlong i, start;
	int k;

	for(k=0; k<nelem(names); k++){
		start=-1;
		for(i=0; i<=a->n; i++){
			if(i<a->n && (a->map[i] & (1<<k))){
				if(start<0) start=i;
			}else if(start>=0){
				fprint(a->fd, "audit owned-range: kind=%s start=%llx end=%llx blocks=%lld\n", names[k], start*Blksz, i*Blksz, i-start);
				start=-1;
			}
		}
	}
}
#endif

/* Caller holds mutlk.  Offline has no writers, limbo or sync workers. */
int
auditfs(int fd, int offline)
{
	Audit a;
	Arena *ar;
	Arange *r;
	Tree t;
	Mount *mnt;
	Scan s;
	Dlist dl;
	char pfx[1];
	vlong addr, base, end, i, j, total, nf, no, nu, phys, start;
	int k, complete;
	vlong kinds[5];

	memset(&a, 0, sizeof a);
	a.fd = fd;
	a.n = fs->sb1->bp.addr/Blksz + 1;
	a.map = emalloc(a.n, 1);
	a.chain = emalloc(a.n*sizeof(uint), 1);
	a.seen = emalloc(a.n*sizeof(Bptr), 1);
	if(waserror()){
		fprint(fd, "audit error: %s\naudit complete: no\n", errmsg());
		free(a.seen);
		free(a.chain);
		free(a.map);
		return 0;
	}
	/* Freeze each arena's free ranges while copying them. */
	for(k=0; k<fs->narena; k++){
		ar = &fs->arenas[k];
		qlock(ar);
		if(waserror()){
			qunlock(ar);
			nexterror();
		}
		for(r=(Arange*)avlmin(ar->free); r!=nil; r=(Arange*)avlnext(r)){
			base=ar->h0->bp.addr+2*Blksz;
			if(r->len <= 0 || r->len%Blksz || r->off < base || r->off > base+ar->size || r->len > base+ar->size-r->off)
				error("audit: invalid free range");
			for(addr=r->off; addr<r->off+r->len; addr+=Blksz){
				i=aindex(&a, addr);
				if(a.map[i] & Afree)
					error("audit: duplicate free block");
				a.map[i] |= Afree;
			}
		}
		qunlock(ar);
		poperror();
	}
	amark(&a, fs->sb0->bp.addr, Aheader);
	amark(&a, fs->sb1->bp.addr, Aheader);
	for(k=0; k<fs->narena; k++){
		ar=&fs->arenas[k];
		amark(&a, ar->h0->bp.addr, Aheader);
		amark(&a, ar->h1->bp.addr, Aheader);
		/* Use replay's logical tail, never a disk chain beyond LogSync. */
		achain(&a, ar->loghd, ar->logtl->bp, Tlog);
	}
	atree(&a, fs->snap.bp, fs->snap.ht);
	achain(&a, fs->snapdl.hd, fs->snapdl.tl, Tdlist);
	achain(&a, fs->reclaimdl.hd, fs->reclaimdl.tl, Treclaim);
	achain(&a, fs->dropdl.hd, fs->dropdl.tl, Tdlist);
	pfx[0]=Kdlist;
	btnewscan(&s, pfx, 1);
	btenter(&fs->snap, &s);
	if(waserror()){
		btexit(&s);
		nexterror();
	}
	while(btnext(&s, &s.kv)){
		if(s.kv.nk != Dlksz || s.kv.nv != Dlvsz)
			error("audit: invalid deadlist value");
		kv2dlist(&s.kv, &dl);
		achain(&a, dl.hd, dl.tl, Tdlist);
	}
	btexit(&s);
	poperror();
	pfx[0]=Ksnap;
	btnewscan(&s, pfx, 1);
	btenter(&fs->snap, &s);
	if(waserror()){
		btexit(&s);
		nexterror();
	}
	while(btnext(&s, &s.kv)){
		if(s.kv.nv != Treesz)
			error("audit: invalid snapshot value");
		unpacktree(&t, s.kv.v, s.kv.nv);
		adata(&a, &t);
	}
	btexit(&s);
	poperror();
	for(mnt=agetp(&fs->mounts); mnt!=nil; mnt=mnt->next)
		adata(&a, agetp(&mnt->root));
	complete = offline;
	if(!offline)
		fprint(fd, "audit pending: limbo=%ld syncing=%ld (live queues not frozen)\n", agetl(&fs->nlimbo), fs->syncing);
	total=nf=no=nu=phys=0;
	for(k=0; k<fs->narena; k++){
		ar=&fs->arenas[k];
		base=ar->h0->bp.addr+2*Blksz;
		end=base+ar->size;
		start=-1;
		for(addr=base; addr<=end; addr+=Blksz){
			j=0;
			if(addr<end){
				i=aindex(&a, addr);
				total++;
				if(a.map[i] & Afree) nf++;
				else if(a.map[i] & Aowned) no++;
				else{
					nu++;
					j=1;
					if(a.map[i] & Aphysical) phys++;
				}
			}
			if(j && start<0) start=addr;
			if(!j && start>=0){
				fprint(fd, "audit range: start=%llx end=%llx blocks=%lld\n", start, addr, (addr-start)/Blksz);
				start=-1;
			}
		}
	}
	memset(kinds, 0, sizeof kinds);
	for(i=0; i<a.n; i++)
		for(k=0; k<5; k++)
			if(a.map[i] & (1<<k)) kinds[k]++;
#ifdef GEFS_AUDIT_USER
	if(offline) aprintmap(&a);
#endif
	fprint(fd, "audit owners: tree=%lld data=%lld log=%lld dead=%lld header=%lld (may overlap)\n", kinds[0], kinds[1], kinds[2], kinds[3], kinds[4]);
	fprint(fd, "audit summary: complete=%d total=%lld free=%lld owned=%lld unaccounted=%lld physicalonly=%lld\n", complete, total, nf, no, nu, phys);
	fprint(fd, "audit complete: %s\n", complete ? "yes" : "no (live)");
	free(a.seen);
	free(a.chain);
	free(a.map);
	poperror();
	return 1;
}

#ifndef GEFS_AUDIT_USER
/* Test-only stop points, explicitly armed after boot. */
int auditautosync = 1;
static Along astop;
static QLock afreelk;
static int *afreearena;
static vlong *afreeaddr;
static int afreeactive;
static vlong afreecount;
static uvlong afreesum, afreexor;
#define AuditFreeMax 32768
static char *aphases[] = { "none", "log-chain", "log-flush", "arena-heads", "supers", "arena-tails", "cleanup" };

void
auditfreebegin(void)
{
	if(agetl(&astop) == 0)
		return;
	qlock(&afreelk);
	free(afreearena);
	free(afreeaddr);
	afreearena = calloc(fs->narena, sizeof(int));
	afreeaddr = calloc(AuditFreeMax, sizeof(vlong));
	afreesum = afreexor = 0;
	afreecount = 0;
	afreeactive = afreearena != nil && afreeaddr != nil;
	qunlock(&afreelk);
}

void
auditfreearena(Arena *a)
{
	int i;

	qlock(&afreelk);
	if(afreeactive && (i = a - fs->arenas) >= 0 && i < fs->narena)
		afreearena[i] = 1;
	qunlock(&afreelk);
}

void
auditqfree(Arena *a, vlong addr)
{
	int i;

	qlock(&afreelk);
	i = a - fs->arenas;
	if(afreeactive && i >= 0 && i < fs->narena && afreearena[i]){
		if(afreecount < AuditFreeMax)
			afreeaddr[afreecount] = addr;
		afreecount++;
		afreesum += addr;
		afreexor ^= addr;
	}
	qunlock(&afreelk);
}

void
auditfreeend(void)
{
	vlong i;

	qlock(&afreelk);
	if(afreeactive){
		afreeactive = 0;
		fprint(2, "audit qfree: blocks=%lld bytes=%lld sum=%llx xor=%llx\n",
			afreecount, afreecount*Blksz, afreesum, afreexor);
		if(afreecount <= AuditFreeMax)
			for(i = 0; i < afreecount; i++)
				fprint(2, "audit qfree-address: %llx\n", afreeaddr[i]);
	}
	qunlock(&afreelk);
}

int
auditarm(char *phase)
{
	int i;

	for(i=0; i<nelem(aphases); i++)
		if(strcmp(phase, aphases[i]) == 0){
			aswapl(&astop, i);
			return 1;
		}
	return 0;
}

void
audithook(char *phase)
{
	long i;

	i=agetl(&astop);
	if(i>0 && strcmp(phase, aphases[i])==0 && acasl(&astop, i, 0)){
		fprint(2, "audit stopped: phase=%s qgen=%lld\n", phase, agetv(&fs->qgen));
		for(;;)
			sleep(1000);
	}
}
#endif
