/*
 * crsrv - the compute pool (docs/cpu-server-design.md): compute resources
 * (CRs) connect over TCP (tcp!*!17030; a browser's through webterm's
 * WebSocket /17030) and run jobs; users give jobs through the file system
 * crsrv posts (/srv/compute): the server's view is /global/compute, and an
 * app's namespace file mounts it on /compute for its processes
 * (docs/app-origins.md):
 *
 *	cc	open, write a job, read its result (rcc)
 *	status	crs N workers N credits N queued N running N done N verified N
 *		mismatch N (two results differed) differed N (failed for it)
 *	ID/	one per connected CR, its ID given here (6 hex digits): type,
 *		api, workers, credits, owner, state, jobs
 *
 * Scheduling is in two levels.  crsrv gives a job to a CR, never to a
 * worker: a CR says how many jobs it will take at once (credits: its
 * local queue included) and crsrv keeps no more than that on it, the CR
 * with the most free credits first.  How the CR runs them - which web
 * worker, thread, GPU - is its own business: its workers are only shown.
 *
 * A CR is untrusted and its result unverified (docs/cpu-server-design.md):
 * with -v duplicate a job runs on two CRs and is done when their results
 * agree (else it fails); with -v 2of3 a third CR decides when two differ.
 * A result is compared without its log.  A job is named by what it is:
 * its ID is a hash of its command and input files (and a number that
 * tells runs apart).  A CR that goes away gives its jobs back to the queue.
 *
 * Messages on a CR connection: a 4-byte big-endian length, a header line
 * (fields separated by tabs) and a body.
 *	from the CR	hello KEY TYPE API WORKERS OWNER [CREDITS]
 *			credits N	(a new number, any time)
 *			result ID ok|fail	body: the output files and "log"
 *	to the CR	include		body: /sys/include and /$objtype/include
 *			job ID CWD ARG...	body: the input files
 * A body is files: a name, NUL, a 4-byte big-endian length, the data.
 * A job written to cc is the same message: "cc CWD ARG..." and the input
 * files; its result, "ok" or "fail" and the output files and log.
 */
#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <mp.h>
#include <libsec.h>

enum {
	Maxmsg	= 64*1024*1024,
	Maxruns	= 3,		/* on this many CRs at most (2of3) */
	Maxcredits = 1024,
};

enum {
	Vnone,
	Vduplicate,
	V2of3,
};

typedef struct Job Job;
typedef struct Run Run;
typedef struct Cr Cr;
typedef struct Fjob Fjob;

struct Job {
	int	seq;
	char	jid[17];	/* hash of the command and the inputs */
	char	*hdr;		/* CWD\tARG... */
	uchar	*in;		/* the input files */
	long	nin;
	uchar	*out;		/* the result: status line and files */
	long	nout;
	int	done;
	int	gone;		/* its fid is clunked */
	int	queued;
	int	nrun;		/* runs on CRs now */
	int	nres;		/* results in */
	uchar	*res[Maxruns];	/* their results, */
	long	nresb[Maxruns];
	uchar	hash[Maxruns][SHA1dlen];	/* compared by these */
	Cr	*rescr[Maxruns];	/* the CRs that gave them */
	Req	*r;		/* a read waiting for the result */
	Job	*next;		/* queue */
};

/* a job on a CR */
struct Run {
	Job	*job;
	Cr	*cr;
	Run	*next;
};

struct Cr {
	char	id[8];
	int	fd;
	char	*type;
	char	*api;
	char	*owner;
	int	workers;	/* shown only: the CR schedules its own */
	int	credits;	/* jobs it takes at once */
	int	out;		/* jobs on it now */
	int	njobs;
	int	dead;
	File	*dir;
	File	*files[7];
	QLock	wlk;
	Cr	*next;
};

struct Fjob {
	uchar	*buf;
	long	n;
	Job	*job;
};

static QLock lk;
static Cr *crs;
static Job *queue;	/* wanting (more) runs */
static Run *runs;	/* on CRs */
static int ncr, ndone, nverified, ndiffered, nmismatch;
static int verify = Vnone;
static uchar *include;
static long ninclude;
static char *key;
static Tree *tree;
static File *ccfile, *statusfile;
static char *crfiles[] = { "type", "api", "workers", "credits", "owner", "state", "jobs" };

static void
put32(uchar *p, ulong v)
{
	p[0] = v>>24;
	p[1] = v>>16;
	p[2] = v>>8;
	p[3] = v;
}

static ulong
get32(uchar *p)
{
	return p[0]<<24 | p[1]<<16 | p[2]<<8 | p[3];
}

/* a body: add name and data */
static void
addfile(uchar **b, long *n, char *name, uchar *data, long len)
{
	long l;

	l = strlen(name) + 1;
	*b = erealloc9p(*b, *n + l + 4 + len);
	memmove(*b + *n, name, l);
	put32(*b + *n + l, len);
	memmove(*b + *n + l + 4, data, len);
	*n += l + 4 + len;
}

static uchar*
readall(int fd, long *np)
{
	uchar *b;
	long n, m;

	b = nil;
	n = 0;
	for(;;){
		b = erealloc9p(b, n + 8192);
		if((m = read(fd, b+n, 8192)) <= 0)
			break;
		n += m;
	}
	*np = n;
	return b;
}

/* the headers, for every CR: dir and dir's subdirectories */
static void
addtree(char *dir)
{
	Dir *d;
	char *p;
	uchar *data;
	long i, nd, n;
	int fd;

	if((fd = open(dir, OREAD)) < 0)
		return;
	nd = dirreadall(fd, &d);
	close(fd);
	for(i = 0; i < nd; i++){
		p = smprint("%s/%s", dir, d[i].name);
		if(d[i].mode & DMDIR)
			addtree(p);
		else if((fd = open(p, OREAD)) >= 0){
			data = readall(fd, &n);
			close(fd);
			addfile(&include, &ninclude, p, data, n);
			free(data);
		}
		free(p);
	}
	free(d);
}

static int
sendmsg(Cr *c, char *hdr, uchar *body, long n)
{
	uchar len[4];
	long h;
	int r;

	h = strlen(hdr);
	put32(len, h + 1 + n);
	qlock(&c->wlk);
	r = 0;
	if(write(c->fd, len, 4) != 4 || write(c->fd, hdr, h) != h
	|| write(c->fd, "\n", 1) != 1 || (n > 0 && write(c->fd, body, n) != n))
		r = -1;
	qunlock(&c->wlk);
	return r;
}

/* one message: header (NUL-terminated in place of \n) and body; nil at EOF */
static uchar*
recvmsg(int fd, long *np, uchar **bodyp, long *nbodyp)
{
	uchar len[4], *m, *e;
	long n;

	if(readn(fd, len, 4) != 4)
		return nil;
	n = get32(len);
	if(n <= 0 || n > Maxmsg)
		return nil;
	m = emalloc9p(n + 1);
	if(readn(fd, m, n) != n){
		free(m);
		return nil;
	}
	m[n] = 0;
	if((e = memchr(m, '\n', n)) == nil)
		e = m + n;
	*e = 0;
	*np = n;
	*bodyp = e + 1 < m + n ? e + 1 : m + n;
	*nbodyp = m + n - *bodyp;
	return m;
}

static void
unqueue(Job *j)
{
	Job **l;

	for(l = &queue; *l != nil; l = &(*l)->next)
		if(*l == j){
			*l = j->next;
			j->next = nil;
			break;
		}
	j->queued = 0;
}

static void
enqueue(Job *j, int front)
{
	Job **l;

	if(j->queued)
		return;
	j->queued = 1;
	if(front){
		j->next = queue;
		queue = j;
		return;
	}
	for(l = &queue; *l != nil; l = &(*l)->next)
		;
	j->next = nil;
	*l = j;
}

static void
freejob(Job *j)
{
	int i;

	free(j->hdr);
	free(j->in);
	free(j->out);
	for(i = 0; i < j->nres; i++)
		free(j->res[i]);
	free(j);
}

/* runs it still wants: the policy's, less those going and done */
static int
wanted(Job *j)
{
	int want;

	if(j->done || j->gone)
		return 0;
	switch(verify){
	case Vduplicate:
		want = 2;
		break;
	case V2of3:
		want = j->nres == 2 && memcmp(j->hash[0], j->hash[1], SHA1dlen) != 0 ? 3 : 2;
		break;
	default:
		want = 1;
	}
	return want - j->nrun - j->nres;
}

/* whether c runs j now or gave a result for it: another CR must verify */
static int
hasrun(Job *j, Cr *c)
{
	Run *u;
	int i;

	for(u = runs; u != nil; u = u->next)
		if(u->job == j && u->cr == c)
			return 1;
	for(i = 0; i < j->nres; i++)
		if(j->rescr[i] == c)
			return 1;
	return 0;
}

/* the CR with the most free credits, not already on j */
static Cr*
bestcr(Job *j)
{
	Cr *c, *best;
	int free, bfree;

	best = nil;
	bfree = 0;
	for(c = crs; c != nil; c = c->next){
		if(c->dead || (free = c->credits - c->out) <= bfree || hasrun(j, c))
			continue;
		best = c;
		bfree = free;
	}
	return best;
}

/* give queued jobs to CRs with free credits */
static void
dispatch(void)
{
	Cr *c;
	Job *j, *jn;
	Run *u;
	char *hdr;

	for(;;){
		qlock(&lk);
		c = nil;
		for(j = queue; j != nil; j = jn){
			jn = j->next;
			if(wanted(j) <= 0){
				unqueue(j);
				continue;
			}
			if((c = bestcr(j)) != nil)
				break;
		}
		if(j == nil){
			qunlock(&lk);
			return;
		}
		u = emalloc9p(sizeof *u);
		u->job = j;
		u->cr = c;
		u->next = runs;
		runs = u;
		j->nrun++;
		c->out++;
		if(wanted(j) <= 0)
			unqueue(j);
		hdr = smprint("job\t%s.%d\t%s", j->jid, j->seq, j->hdr);
		qunlock(&lk);
		sendmsg(c, hdr, j->in, j->nin);	/* a failure ends the CR's reader */
		free(hdr);
	}
}

/* j's result is out (reached its reader, or kept for the read to come) */
static void
finish(Job *j, uchar *out, long nout)
{
	Req *r;

	j->out = emalloc9p(nout);
	memmove(j->out, out, nout);
	j->nout = nout;
	j->done = 1;
	ndone++;
	if((r = j->r) != nil){
		j->r = nil;
		readbuf(r, j->out, j->nout);
		respond(r, nil);
	}
}

/* a result's hash: its status and files, not its log */
static void
reshash(uchar *m, long n, uchar *h)
{
	DigestState *s;
	uchar *p, *e, *z;
	long l;

	e = m + n;
	p = memchr(m, '\n', n);
	p = p == nil ? e : p + 1;
	s = sha1(m, p - m, nil, nil);
	while(p < e && (z = memchr(p, 0, e - p)) != nil && z + 5 <= e){
		l = get32(z + 1);
		if(z + 5 + l > e)
			break;
		if(strcmp((char*)p, "log") != 0)
			s = sha1(p, z + 5 + l - p, nil, s);
		p = z + 5 + l;
	}
	sha1(nil, 0, h, s);
}

/* a result for j, from one of its runs: done when the policy says */
static void
result(Job *j, Cr *c, uchar *out, long nout)
{
	int i, k;

	if(j->done || j->nres >= Maxruns)
		return;
	j->res[j->nres] = emalloc9p(nout);
	memmove(j->res[j->nres], out, nout);
	j->nresb[j->nres] = nout;
	j->rescr[j->nres] = c;
	reshash(out, nout, j->hash[j->nres]);
	j->nres++;
	if(j->nres == 2 && memcmp(j->hash[0], j->hash[1], SHA1dlen) != 0)
		nmismatch++;
	if(verify == Vnone){
		finish(j, out, nout);
		return;
	}
	/* two alike are enough */
	for(i = 0; i < j->nres; i++)
		for(k = i+1; k < j->nres; k++)
			if(memcmp(j->hash[i], j->hash[k], SHA1dlen) == 0){
				nverified++;
				finish(j, j->res[i], j->nresb[i]);
				return;
			}
	if(j->nres == (verify == Vduplicate ? 2 : 3)){
		ndiffered++;
		finish(j, (uchar*)"fail\nverify: the CRs' results differ\n", 37);
		return;
	}
	if(wanted(j) > 0)
		enqueue(j, 1);
}

/* a CR's ID: the server's, not the CR's to say */
static void
crid(Cr *c)
{
	Cr *o;
	uchar b[3];

	for(;;){
		genrandom(b, sizeof b);
		snprint(c->id, sizeof c->id, "%.2ux%.2ux%.2ux", b[0], b[1], b[2]);
		for(o = crs; o != nil; o = o->next)
			if(strcmp(o->id, c->id) == 0)
				break;
		if(o == nil && strcmp(c->id, "status") != 0)
			return;
	}
}

static void
crfilesnew(Cr *c)
{
	int i;

	c->dir = createfile(tree->root, c->id, "crsrv", DMDIR|0555, c);
	for(i = 0; i < nelem(crfiles); i++)
		c->files[i] = createfile(c->dir, crfiles[i], "crsrv", 0444, c);
}

static void
crfilesgone(Cr *c)
{
	int i;

	for(i = 0; i < nelem(crfiles); i++)
		if(c->files[i] != nil){
			removefile(c->files[i]);
			closefile(c->files[i]);
		}
	if(c->dir != nil){
		removefile(c->dir);
		closefile(c->dir);
	}
}

static int
credits(char *s, int workers)
{
	int n;

	n = s != nil ? atoi(s) : workers;
	if(n < 1)
		n = 1;
	if(n > Maxcredits)
		n = Maxcredits;
	return n;
}

/* a CR's connection: hello, then results and credits, until it goes */
static void
crproc(void *a)
{
	Cr *c, **l;
	Job *j;
	Run *u, **ul;
	uchar *m, *body;
	char *f[8], *p;
	long n, nbody;
	int nf, seq;

	c = a;
	j = nil;
	m = recvmsg(c->fd, &n, &body, &nbody);
	if(m == nil || (nf = getfields((char*)m, f, nelem(f), 0, "\t")) < 6
	|| strcmp(f[0], "hello") != 0 || (key != nil && strcmp(f[1], key) != 0)){
		free(m);
		close(c->fd);
		free(c);
		return;
	}
	c->type = estrdup9p(f[2]);
	c->api = estrdup9p(f[3]);
	c->workers = atoi(f[4]);
	if(c->workers < 1)
		c->workers = 1;
	c->owner = estrdup9p(f[5]);
	c->credits = credits(nf > 6 ? f[6] : nil, c->workers);
	free(m);
	if(sendmsg(c, "include", include, ninclude) < 0)
		goto gone;
	qlock(&lk);
	crid(c);
	threadsetname("cr %s", c->id);
	crfilesnew(c);
	c->next = crs;
	crs = c;
	ncr++;
	qunlock(&lk);
	dispatch();

	while((m = recvmsg(c->fd, &n, &body, &nbody)) != nil){
		nf = getfields((char*)m, f, nelem(f), 0, "\t");
		if(nf >= 2 && strcmp(f[0], "credits") == 0){
			qlock(&lk);
			c->credits = credits(f[1], c->workers);
			qunlock(&lk);
			dispatch();
		}else if(nf >= 3 && strcmp(f[0], "result") == 0){
			/* ID is JID.SEQ: SEQ tells the job */
			seq = (p = strrchr(f[1], '.')) != nil ? atoi(p+1) : -1;
			qlock(&lk);
			for(ul = &runs; (u = *ul) != nil; ul = &u->next)
				if(u->cr == c && u->job->seq == seq)
					break;
			if(u != nil)
				j = u->job;
			if(u != nil){
				*ul = u->next;
				free(u);
				c->out--;
				c->njobs++;
				j->nrun--;
				if(j->gone){
					if(j->nrun == 0 && !j->queued)
						freejob(j);
				}else{
					/* "ok\n" or "fail\n" and the files */
					n = strlen(f[2]);
					f[2][n] = '\n';
					result(j, c, (uchar*)f[2], n + 1 + nbody);
				}
			}
			qunlock(&lk);
			dispatch();
		}
		free(m);
	}

gone:
	qlock(&lk);
	c->dead = 1;
	/* its jobs back to the front of the queue, for another CR */
	for(ul = &runs; (u = *ul) != nil;){
		if(u->cr != c){
			ul = &u->next;
			continue;
		}
		*ul = u->next;
		j = u->job;
		free(u);
		j->nrun--;
		if(j->gone){
			if(j->nrun == 0 && !j->queued)
				freejob(j);
		}else if(wanted(j) > 0)
			enqueue(j, 1);
	}
	for(l = &crs; *l != nil; l = &(*l)->next)
		if(*l == c){
			*l = c->next;
			ncr--;
			break;
		}
	if(c->dir != nil)
		crfilesgone(c);
	qunlock(&lk);
	close(c->fd);
	dispatch();
}

static void
listenproc(void *a)
{
	char adir[40], ldir[40];
	int afd, lfd, dfd;
	Cr *c;

	threadsetname("listen %s", (char*)a);
	if((afd = announce(a, adir)) < 0)
		sysfatal("announce %s: %r", (char*)a);
	USED(afd);	/* kept open: the announcement */
	for(;;){
		if((lfd = listen(adir, ldir)) < 0)
			sysfatal("listen: %r");
		dfd = accept(lfd, ldir);
		close(lfd);
		if(dfd < 0)
			continue;
		c = emalloc9p(sizeof *c);
		c->fd = dfd;
		proccreate(crproc, c, 32*1024);
	}
}

static char*
crtext(Cr *c, char *name)
{
	if(strcmp(name, "type") == 0)
		return smprint("%s\n", c->type);
	if(strcmp(name, "api") == 0)
		return smprint("%s\n", c->api);
	if(strcmp(name, "workers") == 0)
		return smprint("%d\n", c->workers);
	if(strcmp(name, "credits") == 0)
		return smprint("%d\n", c->credits);
	if(strcmp(name, "owner") == 0)
		return smprint("%s\n", c->owner);
	if(strcmp(name, "state") == 0)
		return smprint("%s\n", c->out < c->credits ? "ready" : "full");
	if(strcmp(name, "jobs") == 0)
		return smprint("%d\n", c->njobs);
	return estrdup9p("");
}

static void
fsopen(Req *r)
{
	if(r->fid->file == ccfile)
		r->fid->aux = emalloc9p(sizeof(Fjob));
	respond(r, nil);
}

static void
fsread(Req *r)
{
	Fjob *fj;
	Job *j, *q;
	Run *u;
	Cr *c;
	char *s;
	int nw, ncred, nrun, nq;

	qlock(&lk);
	if(r->fid->file == ccfile){
		fj = r->fid->aux;
		if(fj == nil || (j = fj->job) == nil){
			qunlock(&lk);
			respond(r, "no job written");
			return;
		}
		if(j->done){
			readbuf(r, j->out, j->nout);
			qunlock(&lk);
			respond(r, nil);
			return;
		}
		if(j->r != nil){
			qunlock(&lk);
			respond(r, "one read at a time");
			return;
		}
		j->r = r;	/* finish or fsflush responds */
		qunlock(&lk);
		return;
	}
	if(r->fid->file == statusfile){
		nw = ncred = 0;
		for(c = crs; c != nil; c = c->next){
			nw += c->workers;
			ncred += c->credits;
		}
		nrun = 0;
		for(u = runs; u != nil; u = u->next)
			nrun++;
		for(q = queue, nq = 0; q != nil; q = q->next)
			nq++;
		s = smprint("crs %d workers %d credits %d queued %d running %d done %d verified %d mismatch %d differed %d\n",
			ncr, nw, ncred, nq, nrun, ndone, nverified, nmismatch, ndiffered);
	}else
		s = crtext(r->fid->file->aux, r->fid->file->name);
	qunlock(&lk);
	readstr(r, s);
	free(s);
	respond(r, nil);
}

static int jobid;

/* what the job is: a hash of its command and inputs */
static void
jobname(Job *j)
{
	DigestState *s;
	uchar h[SHA1dlen];
	int i;

	s = sha1((uchar*)j->hdr, strlen(j->hdr)+1, nil, nil);
	sha1(j->in, j->nin, h, s);
	for(i = 0; i < 8; i++)
		snprint(j->jid+2*i, 3, "%.2ux", h[i]);
}

static void
fswrite(Req *r)
{
	Fjob *fj;
	Job *j;
	uchar *e;
	long want;

	fj = r->fid->aux;
	if(r->fid->file != ccfile || fj == nil){
		respond(r, "permission denied");
		return;
	}
	if(fj->job != nil){
		respond(r, "job already written");
		return;
	}
	if(fj->n + r->ifcall.count > Maxmsg + 4){
		respond(r, "job too big");
		return;
	}
	fj->buf = erealloc9p(fj->buf, fj->n + r->ifcall.count);
	memmove(fj->buf + fj->n, r->ifcall.data, r->ifcall.count);
	fj->n += r->ifcall.count;
	r->ofcall.count = r->ifcall.count;
	if(fj->n < 4){
		respond(r, nil);
		return;
	}
	want = get32(fj->buf);
	if(fj->n < 4 + want){
		respond(r, nil);
		return;
	}
	/* "cc\tCWD\tARG...\n" and the input files */
	if(fj->n != 4 + want || want < 4 || memcmp(fj->buf+4, "cc\t", 3) != 0
	|| (e = memchr(fj->buf+4, '\n', want)) == nil){
		respond(r, "bad job");
		return;
	}
	j = emalloc9p(sizeof *j);
	j->hdr = emalloc9p(e - (fj->buf+7) + 1);
	memmove(j->hdr, fj->buf+7, e - (fj->buf+7));
	j->nin = fj->buf + 4 + want - (e+1);
	j->in = emalloc9p(j->nin + 1);
	memmove(j->in, e+1, j->nin);
	free(fj->buf);
	fj->buf = nil;
	jobname(j);
	qlock(&lk);
	j->seq = ++jobid;
	fj->job = j;
	enqueue(j, 0);
	qunlock(&lk);
	respond(r, nil);
	dispatch();
}

static void
fsflush(Req *r)
{
	Req *o;
	Job *j;
	Run *u;
	int found;

	o = r->oldreq;
	qlock(&lk);
	found = 0;
	for(u = runs; u != nil; u = u->next)
		if(u->job->r == o){
			u->job->r = nil;
			found = 1;
		}
	for(j = queue; j != nil; j = j->next)
		if(j->r == o){
			j->r = nil;
			found = 1;
		}
	qunlock(&lk);
	if(found)
		respond(o, "interrupted");
	respond(r, nil);
}

static void
fsdestroyfid(Fid *fid)
{
	Fjob *fj;
	Job *j;

	if(fid->file != ccfile || (fj = fid->aux) == nil)
		return;
	free(fj->buf);
	qlock(&lk);
	if((j = fj->job) != nil){
		j->r = nil;
		j->gone = 1;
		if(j->queued)
			unqueue(j);
		if(j->nrun == 0)
			freejob(j);
		/* else its last CR's result frees it */
	}
	qunlock(&lk);
	free(fj);
}

static Srv fs = {
	.open	= fsopen,
	.read	= fsread,
	.write	= fswrite,
	.flush	= fsflush,
	.destroyfid	= fsdestroyfid,
};

static void
usage(void)
{
	fprint(2, "usage: crsrv [-k key] [-a addr] [-s srvname] [-v none|duplicate|2of3]\n");
	threadexitsall("usage");
}

void
threadmain(int argc, char **argv)
{
	char *addr, *srvname, *objtype, *d, *p;

	addr = "tcp!*!17030";
	srvname = "compute";
	ARGBEGIN{
	case 'k':
		key = EARGF(usage());
		break;
	case 'a':
		addr = EARGF(usage());
		break;
	case 's':
		srvname = EARGF(usage());
		break;
	case 'v':
		p = EARGF(usage());
		if(strcmp(p, "none") == 0)
			verify = Vnone;
		else if(strcmp(p, "duplicate") == 0)
			verify = Vduplicate;
		else if(strcmp(p, "2of3") == 0)
			verify = V2of3;
		else
			usage();
		break;
	default:
		usage();
	}ARGEND
	if(argc != 0)
		usage();

	if((objtype = getenv("objtype")) == nil)
		objtype = "amd64";
	addtree("/sys/include");
	d = smprint("/%s/include", objtype);
	addtree(d);
	free(d);

	tree = fs.tree = alloctree("crsrv", "crsrv", DMDIR|0555, nil);
	ccfile = createfile(tree->root, "cc", "crsrv", 0666, nil);
	statusfile = createfile(tree->root, "status", "crsrv", 0444, nil);
	proccreate(listenproc, addr, 32*1024);
	threadpostsrv(&fs, srvname);
	threadexits(nil);
}
