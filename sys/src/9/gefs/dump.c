#include "gefs.h"

char	spc[128];

static int
showkey(Fmt *fmt, Key *k)
{
	int n;

	/*
	 * dent: pqid[8] qid[8] -- a directory entry key.
	 * ptr:  off[8] hash[8] -- a key for an Dir block.
	 * dir:  fixed statbuf header, user ids
	 */
	if(k->nk == 0)
		return fmtprint(fmt, "\"\"");
	switch(k->k[0]){
	case Kdat:	/* qid[8] off[8] => ptr[16]:	pointer to data page */
		n = fmtprint(fmt, "dat qid:%llx off:%llx",
			UNPACK64(k->k+1), UNPACK64(k->k+9));
		break;
	case Kent:	/* pqid[8] name[n] => dir[n]:	serialized Dir */
		n = fmtprint(fmt, "ent dir:%llx, name:\"%.*s\"",
			UNPACK64(k->k+1), k->nk-11, k->k+11);
		break;
	case Klabel:	/* name[n] => tree[24]:	snapshot ref */
		n = fmtprint(fmt, "label name:\"%.*s\"", k->nk-1, k->k+1);
		break;
	case Ksnap:	/* name[n] => tree[24]:	snapshot root */
		n = fmtprint(fmt, "snap id:%lld", UNPACK64(k->k+1));
		break;
	case Kup:	/* qid[8] => pqid[8]:		parent dir */
		n = fmtprint(fmt, "up dir:%llx", UNPACK64(k->k+1));
		break;
	case Kdlist:
		n = fmtprint(fmt, "dlist gen:%lld, bgen:%lld",
			UNPACK64(k->k+1), UNPACK64(k->k+9));
		break;
	case Kconf:
		n = fmtprint(fmt, "Conf %.*s", k->nk-1, k->k+1);
		break;
	case Kobj:	/* Plan2001: store[8] [oid[8] ~ver[8] idx[4]] */
		if(k->nk == 1+8)
			n = fmtprint(fmt, "obj store:%llx header", UNPACK64(k->k+1));
		else if(k->nk == 1+8+8+8+4)
			n = fmtprint(fmt, "obj store:%llx oid:%llx ver:%lld idx:%d",
				UNPACK64(k->k+1), UNPACK64(k->k+9),
				~UNPACK64(k->k+17), UNPACK32(k->k+25));
		else
			n = fmtprint(fmt, "obj ??? %.*H", k->nk, k->k);
		break;
	default:
		n = fmtprint(fmt, "??? %.*H", k->nk, k->k);
		break;
	}
	return n;
}

/*
 * Plan2001: %M and %P may be used anywhere, without a volume: the Kent
 * value is decoded here without kv2dir's broke(), which would need one.
 * A corrupt value is shown as it lies; nothing is marked read-only.
 */
static void
showkvdir(Kvp *v, Xdir *d)
{
	char *p;

	memset(d, 0, sizeof *d);
	if(v->nk < 9 || v->nv != Xdirsz)
		return;
	d->name = v->k + 11;	/* packstr's length and its terminator */
	p = v->v;
	d->flag		= UNPACK64(p);	p += 8;
	d->qid.path	= UNPACK64(p);	p += 8;
	d->qid.vers	= UNPACK32(p);	p += 4;
	d->qid.type	= UNPACK8(p);	p += 1;
	d->mode		= UNPACK32(p);	p += 4;
	d->atime	= UNPACK64(p);	p += 8;
	d->mtime	= UNPACK64(p);	p += 8;
	d->length	= UNPACK64(p);	p += 8;
	d->uid		= UNPACK32(p);	p += 4;
	d->gid		= UNPACK32(p);	p += 4;
	d->muid		= UNPACK32(p);	p += 4;
}

static int
showval(Fmt *fmt, Kvp *v, int op, int flg)
{
	int n, ws;
	char *p;
	Tree t;
	Xdir d;

	n = 0;
	if(flg){
		assert(v->nv == Ptrsz+2);
		n = fmtprint(fmt, "(%B,%d)", unpackbp(v->v, v->nv), UNPACK16(v->v+Ptrsz));
		return n;
	}
	if(op == Odelete || op == Oclearb){
		n = fmtprint(fmt, "delete");
		return n;
	}
	switch(v->k[0]){
	case Kdat:	/* qid[8] off[8] => ptr[16]:	pointer to data page */
		switch(op){
		case Odelete:
		case Oclearb:
			n = 0;
			break;
		case Onop:
		case Oinsert:
			if(v->nv == Ptrsz)
				n = fmtprint(fmt, "ptr:%B", unpackbp(v->v, v->nv));
			else
				n = fmtprint(fmt, "BROKEN ptr %.*H", v->nk, v->k);
			break;
		}
		break;
	case Kent:	/* pqid[8] name[n] => dir[n]:	serialized Dir */
		switch(op){
		case Onop:
		case Oinsert:
			showkvdir(v, &d);
			n = fmtprint(fmt, "[qid=(%llux,%lud,%d), p=%luo, f=%llux, t=%lld,%lld, l=%lld, o=%d, g=%d m=%d]",
				d.qid.path, d.qid.vers, d.qid.type, d.mode,
				d.flag, d.atime, d.mtime, d.length,
				d.uid, d.gid, d.muid);
			break;
		case Odelete:
			n = fmtprint(fmt, "delete");
			break;
		case Owstat:
			p = v->v;
			ws = *p++;
			if(ws & Owsize){
				n += fmtprint(fmt, "size:%llx ", UNPACK64(p));
				p += 8;
			}
			if(ws & Owmode){
				n += fmtprint(fmt, "mode:%uo ", UNPACK32(p));
				p += 4;
			}
			if(ws & Owmtime){
				n += fmtprint(fmt, "mtime:%llx ", UNPACK64(p));
				p += 8;
			}
			if(ws & Owatime){
				n += fmtprint(fmt, "mtime:%llx ", UNPACK64(p));
				p += 8;
			}
			if(ws & Owuid){
				n += fmtprint(fmt, "uid:%d ", UNPACK32(p));
				p += 4;
			}
			if(ws & Owgid){
				n += fmtprint(fmt, "gid:%d ", UNPACK32(p));
				p += 4;
			}
			if(ws & Owmuid){
				n += fmtprint(fmt, "muid:%d ", UNPACK32(p));
				p += 4;
			}
			if(p != v->v + v->nv){
				print("gefs: v->nv: %d, sz=%d\n", v->nv, (int)(p - v->v));
				abort();
			}
			break;
		}
		break;
	case Ksnap:	/* name[n] => dent[16] ptr[16]:	snapshot root */
		switch(op){
		case Orelink:
		case Oreprev:
		case Oincref:
			n = fmtprint(fmt, "gen: %lld, dlbl: %d, dref: %d",
				UNPACK64(v->v), v->v[8], v->v[9]);
			break;
		case Onop:
		case Oinsert:
			if(unpacktree(nil, &t, v->v, v->nv) == nil)
				n = fmtprint(fmt, "corrupt tree");
			else
				n = fmtprint(fmt, "<tree %B [pred=%lld, succ=%lld, base=%lld, nref=%d, nlbl=%d]>",
					t.bp, t.pred, t.succ, t.base, t.nref, t.nlbl);
			break;
		default:
			n = fmtprint(fmt, "?? unknown op %d", op);
		}
		break;
	case Klabel:
		n = fmtprint(fmt, "snap id:%lld", UNPACK64(v->v+1));
		break;
	case Kup:	/* qid[8] => pqid[8]:		parent dir */
		n = fmtprint(fmt, "super dir:%llx, name:\"%.*s\")",
			UNPACK64(v->v+1), v->nv-11, v->v+11);
		break;
	case Kdlist:
		n = fmtprint(fmt, "hd:%B, tl:%B",
			unpackbp(v->v, v->nv),
			unpackbp(v->v+Ptrsz, v->nv-Ptrsz));
		break;
	case Kconf:
		n = fmtprint(fmt, "%.*s", v->nv, v->v);
		break;
	case Kobj:
		n = fmtprint(fmt, "%d bytes", v->nv);
		break;
	default:
		n = fmtprint(fmt, "??? %.*H", v->nv, v->v);
		break;
	}
	return n;

}

int
Bconv(Fmt *fmt)
{
	Bptr bp;

	bp = va_arg(fmt->args, Bptr);
	return fmtprint(fmt, "(%llx,%.16llux,%llx)", bp.addr, bp.hash, bp.gen);
}

int
Mconv(Fmt *fmt)
{
	char *opname[Nmsgtype] = {
	[Oinsert]	"Oinsert",
	[Odelete]	"Odelete",
	[Oclearb]	"Oclearb",
	[Oclobber]	"Oclobber",
	[Owstat]	"Owstat",
	[Orelink]	"Orelink",
	[Oreprev]	"Oreprev",
	[Oincref]	"Oincref",
	};
	Msg *m;
	int f, n;

	f = (fmt->flags & FmtSharp) != 0;
	m = va_arg(fmt->args, Msg*);
	if(m == nil)
		return fmtprint(fmt, "Msg{nil}");
	n = fmtprint(fmt, "Msg(%s, ", opname[m->op]);
	n += showkey(fmt, m);
	n += fmtprint(fmt, ") => (");
	n += showval(fmt, m, m->op, f);
	n += fmtprint(fmt, ")");
	return n;
}

int
Pconv(Fmt *fmt)
{
	Kvp *kv;
	int f, n;

	f = (fmt->flags & FmtSharp) != 0;
	kv = va_arg(fmt->args, Kvp*);
	if(kv == nil)
		return fmtprint(fmt, "Kvp{nil}");
	n = fmtprint(fmt, "Kvp(");
	n += showkey(fmt, kv);
	n += fmtprint(fmt, ") => (");
	n += showval(fmt, kv, Onop, f);
	n += fmtprint(fmt, ")");
	return n;
}

int
Kconv(Fmt *fmt)
{
	Key *k;
	int n;

	k = va_arg(fmt->args, Key*);
	if(k == nil)
		return fmtprint(fmt, "Key{nil}");
	n = fmtprint(fmt, "Key(");
	n += showkey(fmt, k);
	n += fmtprint(fmt, ")");
	return n;
}

int
Rconv(Fmt *fmt)
{
	Arange *r;

	r = va_arg(fmt->args, Arange*);
	if(r == nil)
		return fmtprint(fmt, "<Arange:nil>");
	else
		return fmtprint(fmt, "Arange(%lld+%lld)", r->off, r->len);
}

int
Qconv(Fmt *fmt)
{
	Qid q;

	q = va_arg(fmt->args, Qid);
	return fmtprint(fmt, "(%llx %ld %d)", q.path, q.vers, q.type);
}

static void
rshowblk(Gefs *fs, int fd, Blk *b, int indent, int recurse)
{
	Blk *c;
	int i;
	Bptr bp;
	Kvp kv;
	Msg m;

	if(indent > sizeof(spc)/4)
		indent = sizeof(spc)/4;
	if(b == nil){
		fprint(fd, "NIL\n");
		return;
	}
	fprint(fd, "%.*s[BLK]|{%B}\n", 4*indent, spc, b->bp);
	switch(b->type){
	case Tpivot:
		for(i = 0; i < b->nbuf; i++){
			getmsg(b, i, &m);
			fprint(fd, "%.*s[%03d]|%N\n", 4*indent, spc, i, &m);
		}
		/* wet floor */
	case Tleaf:
		for(i = 0; i < b->nval; i++){
			getval(b, i, &kv);
			if(b->type == Tpivot){
				fprint(fd, "%.*s[%03d]|%#P\n", 4*indent, spc, i, &kv);
				bp = unpackbp(kv.v, kv.nv);
				c = getblk(fs, bp, 0);
				if(recurse)
					rshowblk(fs, fd, c, indent + 1, 1);
				dropblk(c);
			}else{
				fprint(fd, "%.*s[%03d]|%P\n", 4*indent, spc, i, &kv);
			}
		}
		break;
	case Tarena:
		fprint(fd, "arena -- ");
		goto Show;
	case Tlog:
		fprint(fd, "log -- ");
		goto Show;
	case Tdlist:
		fprint(fd, "dlist -- ");
		goto Show;
	case Tdat:
		fprint(fd, "dat -- ");
	Show:
		for(i = 0; i < 32; i++){
			fprint(fd, "%x", b->buf[i] & 0xff);
			if(i % 4 == 3)
				fprint(fd, " ");
		}
		fprint(fd, "\n");
		break;
	}
}

void
showblk(Gefs *fs, int fd, Blk *b, char *m, int recurse)
{
	fprint(fd, "=== %s\n", m);
	rshowblk(fs, fd, b, 0, recurse);
}

void
showbp(Gefs *fs, int fd, Bptr bp, int recurse)
{
	Blk *b;

	b = getblk(fs, bp, GBnochk);
	rshowblk(fs, fd, b, 0, recurse);
	dropblk(b);
}

void
showtreeroot(int fd, Tree *t)
{
	Gefs *fs = t->fs;
	fprint(fd, "\tflag\t0x%x\n", t->flag);
	fprint(fd, "\tgen:\t%lld\n", t->gen);
	fprint(fd, "\tbase\t%lld\n", t->base);
	fprint(fd, "\tpred:\t%lld\n", t->pred);
	fprint(fd, "\tsucc:\t%lld\n", t->succ);
	fprint(fd, "\tnref:\t%d\n", t->nref);
	fprint(fd, "\tnlbl:\t%d\n", t->nlbl);
	fprint(fd, "\tht:\t%d\n", t->ht);
	fprint(fd, "\tbp:\t%B\n", t->bp);
}

void
initshow(void)
{
	int i;

	memset(spc, ' ', sizeof(spc));
	for(i = 0; i < sizeof(spc); i += 4)
		spc[i] = '|';
}
