/*
 * crsrv - the compute pool (docs/cpu-server-design.md), a first test:
 * compute resources (CRs) connect over TCP (tcp!*!17030; a browser's
 * through webterm's WebSocket /17030) and run compile jobs; users give
 * jobs through the file system crsrv posts (/srv/compute): the server's
 * view is /global/compute, and an app's namespace file mounts it on
 * /compute for its processes (docs/app-origins.md):
 *
 *	cc	open, write a job, read its result (rcc)
 *	status	crs N workers N queued N running N done N
 *	N/	one per connected CR: type, api, workers, owner, state, jobs
 *
 * Messages on a CR connection: a 4-byte big-endian length, a header line
 * (fields separated by tabs) and a body.
 *	from the CR	hello KEY TYPE API WORKERS OWNER
 *			result ID ok|fail	body: the output files and "log"
 *	to the CR	include		body: /sys/include and /$objtype/include
 *			job ID CWD ARG...	body: the input files
 * A body is files: a name, NUL, a 4-byte big-endian length, the data.
 * A job written to cc is the same message: "cc CWD ARG..." and the input
 * files; its result, "ok" or "fail" and the output files and log.  A
 * result is whole or nothing: the job of a CR that goes away is given to
 * another.
 */
#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

enum {
	Maxmsg	= 64*1024*1024,
};

typedef struct Job Job;
typedef struct Cr Cr;
typedef struct Fjob Fjob;

struct Job {
	int	id;
	char	*hdr;		/* CWD\tARG... */
	uchar	*in;		/* the input files */
	long	nin;
	uchar	*out;		/* the result: status line and files */
	long	nout;
	int	done;
	int	gone;		/* its fid is clunked */
	Cr	*cr;		/* running on */
	Req	*r;		/* a read waiting for the result */
	Job	*next;		/* queue, or all jobs */
};

struct Cr {
	int	id;
	int	fd;
	char	*type;
	char	*api;
	char	*owner;
	int	workers;
	int	busy;
	int	njobs;
	int	dead;
	File	*dir;
	File	*files[6];
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
static Job *queue;	/* waiting for a CR */
static Job *running;	/* on CRs */
static int ncr, njob, ndone;
static uchar *include;
static long ninclude;
static char *key;
static Tree *tree;
static File *ccfile, *statusfile;
static char *crfiles[] = { "type", "api", "workers", "owner", "state", "jobs" };

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
unlink(Job **l, Job *j)
{
	for(; *l != nil; l = &(*l)->next)
		if(*l == j){
			*l = j->next;
			j->next = nil;
			return;
		}
}

static void
freejob(Job *j)
{
	free(j->hdr);
	free(j->in);
	free(j->out);
	free(j);
}

/* give queued jobs to CRs with a free worker */
static void
dispatch(void)
{
	Cr *c;
	Job *j;
	char *hdr;

	for(;;){
		qlock(&lk);
		j = queue;
		for(c = crs; c != nil; c = c->next)
			if(!c->dead && c->busy < c->workers)
				break;
		if(j == nil || c == nil){
			qunlock(&lk);
			return;
		}
		queue = j->next;
		j->next = running;
		running = j;
		j->cr = c;
		c->busy++;
		hdr = smprint("job\t%d\t%s", j->id, j->hdr);
		qunlock(&lk);
		sendmsg(c, hdr, j->in, j->nin);	/* a failure ends the CR's reader */
		free(hdr);
	}
}

/* the result of j reached its reader, or there is none to wait for */
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

static void
crfilesnew(Cr *c)
{
	char name[16];
	int i;

	snprint(name, sizeof name, "%d", c->id);
	c->dir = createfile(tree->root, name, "crsrv", DMDIR|0555, c);
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

/* a CR's connection: hello, then results, until it goes */
static void
crproc(void *a)
{
	Cr *c, **l;
	Job *j, *jn;
	uchar *m, *body;
	char *f[8];
	long n, nbody;
	int nf, id;

	c = a;
	threadsetname("cr %d", c->id);
	m = recvmsg(c->fd, &n, &body, &nbody);
	if(m == nil || (nf = getfields((char*)m, f, nelem(f), 0, "\t")) < 6
	|| strcmp(f[0], "hello") != 0 || (key != nil && strcmp(f[1], key) != 0)){
		free(m);
		close(c->fd);
		free(c);
		return;
	}
	USED(nf);
	c->type = estrdup9p(f[2]);
	c->api = estrdup9p(f[3]);
	c->workers = atoi(f[4]);
	if(c->workers < 1)
		c->workers = 1;
	c->owner = estrdup9p(f[5]);
	free(m);
	if(sendmsg(c, "include", include, ninclude) < 0)
		goto gone;
	qlock(&lk);
	crfilesnew(c);
	c->next = crs;
	crs = c;
	ncr++;
	qunlock(&lk);
	dispatch();

	while((m = recvmsg(c->fd, &n, &body, &nbody)) != nil){
		nf = getfields((char*)m, f, nelem(f), 0, "\t");
		if(nf >= 3 && strcmp(f[0], "result") == 0){
			id = atoi(f[1]);
			qlock(&lk);
			for(j = running; j != nil; j = j->next)
				if(j->id == id && j->cr == c)
					break;
			if(j != nil){
				unlink(&running, j);
				j->cr = nil;
				c->busy--;
				c->njobs++;
				if(j->gone)
					freejob(j);
				else{
					/* "ok\n" or "fail\n" and the files */
					n = strlen(f[2]);
					f[2][n] = '\n';
					finish(j, (uchar*)f[2], n + 1 + nbody);
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
	/* its jobs to the front of the queue, for another CR */
	for(j = running; j != nil; j = jn){
		jn = j->next;
		if(j->cr == c){
			unlink(&running, j);
			j->cr = nil;
			if(j->gone)
				freejob(j);
			else{
				j->next = queue;
				queue = j;
			}
		}
	}
	for(l = &crs; *l != nil; l = &(*l)->next)
		if(*l == c){
			*l = c->next;
			ncr--;
			break;
		}
	crfilesgone(c);
	qunlock(&lk);
	close(c->fd);
	dispatch();
}

static void
listenproc(void *a)
{
	char adir[40], ldir[40];
	int afd, lfd, dfd, id;
	Cr *c;

	threadsetname("listen %s", (char*)a);
	if((afd = announce(a, adir)) < 0)
		sysfatal("announce %s: %r", (char*)a);
	USED(afd);	/* kept open: the announcement */
	for(id = 1;; id++){
		if((lfd = listen(adir, ldir)) < 0)
			sysfatal("listen: %r");
		dfd = accept(lfd, ldir);
		close(lfd);
		if(dfd < 0)
			continue;
		c = emalloc9p(sizeof *c);
		c->id = id;
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
	if(strcmp(name, "owner") == 0)
		return smprint("%s\n", c->owner);
	if(strcmp(name, "state") == 0)
		return smprint("%s\n", c->busy > 0 ? "busy" : "idle");
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
	Cr *c;
	char *s;
	int nw, nrun;

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
		nw = 0;
		for(c = crs; c != nil; c = c->next)
			nw += c->workers;
		nrun = 0;
		for(q = running; q != nil; q = q->next)
			nrun++;
		for(q = queue, njob = 0; q != nil; q = q->next)
			njob++;
		s = smprint("crs %d workers %d queued %d running %d done %d\n", ncr, nw, njob, nrun, ndone);
	}else
		s = crtext(r->fid->file->aux, r->fid->file->name);
	qunlock(&lk);
	readstr(r, s);
	free(s);
	respond(r, nil);
}

static int jobid;

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
	qlock(&lk);
	j->id = ++jobid;
	fj->job = j;
	/* to the end of the queue */
	if(queue == nil)
		queue = j;
	else{
		Job *q;
		for(q = queue; q->next != nil; q = q->next)
			;
		q->next = j;
	}
	qunlock(&lk);
	respond(r, nil);
	dispatch();
}

static void
fsflush(Req *r)
{
	Req *o;
	Job *j;
	int found;

	o = r->oldreq;
	qlock(&lk);
	found = 0;
	for(j = running; j != nil; j = j->next)
		if(j->r == o){
			j->r = nil;
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
		if(j->done)
			freejob(j);
		else if(j->cr == nil){
			unlink(&queue, j);
			freejob(j);
		}else
			j->gone = 1;	/* its CR's result frees it */
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
	fprint(2, "usage: crsrv [-k key] [-a addr] [-s srvname]\n");
	threadexitsall("usage");
}

void
threadmain(int argc, char **argv)
{
	char *addr, *srvname, *objtype, *d;

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
