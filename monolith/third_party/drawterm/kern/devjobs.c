/*
 * #J: 9pterm's session jobs (9pterm -M, docs/9pterm.md).  A session
 * authenticates once; the CPU server runs a loop over /mnt/term/dev/jobs
 * (this device, through the rcpu connection's 9P), and local clients hand
 * in commands through a Unix socket without authenticating again:
 *
 *	jobs/new	read: the next job's number (waits for one)
 *	jobs/N/cmd	the command, an rc script
 *	jobs/N/out	written: to the client's stdout
 *	jobs/N/err	written: to the client's stderr
 *	jobs/N/status	written: the command's status; the job ends
 *	jobs/ctl	the jobs in one file, for 9pjobd (kern/9pjobd.c): a read
 *			gives a job ('J' id len command), writes return its
 *			output and status ('o'|'e'|'s' id len data)
 *	jobs/src	9pjobd's source, which the session compiles
 *
 * Socket frames, both ways: a type byte, a 4-byte big-endian length, data.
 *	client:	C command, Q (the session's state), X (end the session)
 *	9pterm:	o stdout, e stderr, s status (last)
 * A client that goes away before its job has ended (9pterm -S -T, ^C) has
 * the job killed: 9pjobd tells each job's process ('p' frame) and gets
 * 'K' id pid to kill its note group.  -I secs: the session ends when no
 * job has run for secs.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include "u.h"
#include "lib.h"
#include "dat.h"
#include "fns.h"
#include "error.h"

#undef listen
#undef accept
#undef bind
#undef atexit

#ifdef NINEPTERM

enum {
	Qroot,
	Qjobs,
	Qnew,
	Qctl,
	Qsrc,
	Qjobdir,
	Qcmd,
	Qout,
	Qerr,
	Qstatus,

	Maxcmd	= 1024*1024,
};

static char jobdsrc[] =
#include "9pjobd.h"
;

#define KIND(p)	((int)((p) & 0xFF))
#define JID(p)	((int)((p) >> 8))
#define QID(id, k)	(((vlong)(id) << 8) | (k))

typedef struct Job Job;
struct Job {
	int	id;
	char	*cmd;
	long	ncmd;
	int	fd;		/* the client's socket */
	int	given;		/* its number has been read from new */
	int	ended;
	int	rpid;		/* its process on the server (9pjobd's 'p') */
	int	killwant;	/* its client went away */
	int	killsent;
	QLock	wlk;		/* frames to fd */
	Job	*next;
};

static Lock jl;		/* jobs, npending */
static Job *jobs;
static int npending;
static int nkill;		/* jobs to kill (killwant, rpid, not killsent) */
static int nextid = 1;
static Rendez newr;
static long lastact;		/* a job began or ended (-I) */
static char *sockpath;
static char *sessdesc;
static long sesssince;

static Job*
lookjob(int id)
{
	Job *j;

	lock(&jl);
	for(j = jobs; j != nil; j = j->next)
		if(j->id == id)
			break;
	unlock(&jl);
	return j;
}

static void
put32(uchar *p, ulong v)
{
	p[0] = v>>24;
	p[1] = v>>16;
	p[2] = v>>8;
	p[3] = v;
}

static int
frame(Job *j, int type, void *data, long n)
{
	uchar h[5];
	int r;

	h[0] = type;
	put32(h+1, n);
	qlock(&j->wlk);
	r = 0;
	/* no SIGPIPE when the client has gone */
	if(j->fd < 0 || send(j->fd, h, 5, MSG_NOSIGNAL) != 5
	|| (n > 0 && send(j->fd, data, n, MSG_NOSIGNAL) != n))
		r = -1;
	qunlock(&j->wlk);
	return r;
}

static void
endjob(Job *j)
{
	Job **l;

	lock(&jl);
	j->ended = 1;
	for(l = &jobs; *l != nil; l = &(*l)->next)
		if(*l == j){
			*l = j->next;
			break;
		}
	lastact = time(0);
	unlock(&jl);
	qlock(&j->wlk);
	if(j->fd >= 0)
		close(j->fd);
	j->fd = -1;
	qunlock(&j->wlk);
	/* j is not freed: a chan may still name it; its number is not reused */
}

static int
jobsgen(Chan *c, char *name, Dirtab *tab, int ntab, int s, Dir *dp)
{
	Qid q;
	Job *j;
	int k, id, i;
	char buf[16];
	static char *jf[] = { "cmd", "out", "err", "status" };

	USED(name);
	USED(tab);
	USED(ntab);
	k = KIND(c->qid.path);
	id = JID(c->qid.path);
	/* a file's entries are its directory's (devstat, devopen look there) */
	if(k == Qnew || k == Qctl || k == Qsrc)
		k = Qjobs;
	else if(k >= Qcmd)
		k = Qjobdir;
	if(s == DEVDOTDOT){
		if(k == Qjobdir){
			mkqid(&q, QID(0, Qjobs), 0, QTDIR);
			devdir(c, q, "jobs", 0, eve, DMDIR|0555, dp);
		}else{
			mkqid(&q, QID(0, Qroot), 0, QTDIR);
			devdir(c, q, "#J", 0, eve, DMDIR|0555, dp);
		}
		return 1;
	}
	switch(k){
	case Qroot:
		if(s != 0)
			return -1;
		mkqid(&q, QID(0, Qjobs), 0, QTDIR);
		devdir(c, q, "jobs", 0, eve, DMDIR|0555, dp);
		return 1;
	case Qjobs:
		if(s == 0){
			mkqid(&q, QID(0, Qnew), 0, QTFILE);
			devdir(c, q, "new", 0, eve, 0444, dp);
			return 1;
		}
		if(s == 1){
			mkqid(&q, QID(0, Qctl), 0, QTFILE);
			devdir(c, q, "ctl", 0, eve, 0666, dp);
			return 1;
		}
		if(s == 2){
			mkqid(&q, QID(0, Qsrc), 0, QTFILE);
			devdir(c, q, "src", sizeof jobdsrc - 1, eve, 0444, dp);
			return 1;
		}
		lock(&jl);
		for(i = 3, j = jobs; j != nil && i < s; j = j->next)
			i++;
		id = j != nil ? j->id : 0;
		unlock(&jl);
		if(j == nil)
			return -1;
		snprint(buf, sizeof buf, "%d", id);
		mkqid(&q, QID(id, Qjobdir), 0, QTDIR);
		devdir(c, q, buf, 0, eve, DMDIR|0555, dp);
		return 1;
	case Qjobdir:
		if(s < 0 || s >= nelem(jf))
			return -1;
		mkqid(&q, QID(id, Qcmd+s), 0, QTFILE);
		devdir(c, q, jf[s], 0, eve, s == 0 ? 0444 : 0222, dp);
		return 1;
	}
	return -1;
}

static Chan*
jobsattach(char *spec)
{
	Chan *c;

	c = devattach('J', spec);
	mkqid(&c->qid, QID(0, Qroot), 0, QTDIR);
	return c;
}

static Walkqid*
jobswalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, 0, 0, jobsgen);
}

static int
jobsstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, 0, 0, jobsgen);
}

static Chan*
jobsopen(Chan *c, int omode)
{
	int k;

	k = KIND(c->qid.path);
	if(k >= Qcmd && lookjob(JID(c->qid.path)) == nil)
		error(Enonexist);
	c = devopen(c, omode, 0, 0, jobsgen);
	c->aux = nil;
	return c;
}

static void
jobsclose(Chan *c)
{
	if(KIND(c->qid.path) == Qnew && (c->flag & COPEN))
		free(c->aux);
}

static int
havepending(void *v)
{
	USED(v);
	return npending > 0;
}

static int
havework(void *v)
{
	USED(v);
	return npending > 0 || nkill > 0;
}

/*
 * The next job not yet given out, waiting for one; with kills, a job to
 * kill first (*kill set).  A job whose client has gone is not given out.
 */
static Job*
takework(int *kill)
{
	Job *j, *dead;

	for(;;){
		sleep(&newr, kill != nil ? havework : havepending, nil);
		dead = nil;
		lock(&jl);
		j = nil;
		if(kill != nil && nkill > 0)
			for(j = jobs; j != nil; j = j->next)
				if(j->killwant && j->rpid && !j->killsent){
					j->killsent = 1;
					nkill--;
					*kill = 1;
					break;
				}
		if(j == nil){
			for(j = jobs; j != nil; j = j->next)
				if(!j->given)
					break;
			if(j != nil){
				j->given = 1;
				npending--;
				if(j->killwant){
					dead = j;
					j = nil;
				}else if(kill != nil)
					*kill = 0;
			}
		}
		unlock(&jl);
		if(dead != nil)
			endjob(dead);
		if(j != nil)
			return j;
	}
}

static Job*
takejob(void)
{
	return takework(nil);
}

static long
jobsread(Chan *c, void *va, long n, vlong off)
{
	Job *j;
	char buf[32];
	long m;
	int kill;

	switch(KIND(c->qid.path)){
	case Qroot:
	case Qjobs:
	case Qjobdir:
		return devdirread(c, va, n, 0, 0, jobsgen);
	case Qnew:
		if(off == 0 && c->aux == nil){
			j = takejob();
			snprint(buf, sizeof buf, "%d\n", j->id);
			c->aux = strdup(buf);
		}
		return readstr(off, va, n, c->aux != nil ? c->aux : "");
	case Qsrc:
		return readstr(off, va, n, jobdsrc);
	case Qctl:
		j = takework(&kill);
		if(kill){
			((uchar*)va)[0] = 'K';
			put32((uchar*)va+1, j->id);
			put32((uchar*)va+5, j->rpid);
			return 9;
		}
		if(n < 9 + j->ncmd)
			error("ctl: read buffer too small for the job");
		((uchar*)va)[0] = 'J';
		put32((uchar*)va+1, j->id);
		put32((uchar*)va+5, j->ncmd);
		memmove((uchar*)va+9, j->cmd, j->ncmd);
		return 9 + j->ncmd;
	case Qcmd:
		if((j = lookjob(JID(c->qid.path))) == nil)
			error(Enonexist);
		if(off >= j->ncmd)
			return 0;
		m = j->ncmd - off;
		if(m > n)
			m = n;
		memmove(va, j->cmd + off, m);
		return m;
	}
	return 0;
}

/* 9pjobd's frames: 'o'|'e'|'s' id[4] len[4] data, one or more */
static long
ctlwrite(void *va, long n)
{
	uchar *p, *e;
	ulong id, len;
	Job *j;

	for(p = va, e = p + n; e - p >= 9; p += 9 + len){
		id = p[1]<<24 | p[2]<<16 | p[3]<<8 | p[4];
		len = p[5]<<24 | p[6]<<16 | p[7]<<8 | p[8];
		if(len > e - p - 9)
			error("ctl: short frame");
		if((j = lookjob(id)) == nil)
			continue;
		switch(p[0]){
		case 'p':	/* the job's process, to kill it by */
			if(len >= 4){
				lock(&jl);
				j->rpid = p[9]<<24 | p[10]<<16 | p[11]<<8 | p[12];
				if(j->killwant && !j->killsent)
					nkill++;
				unlock(&jl);
				wakeup(&newr);
			}
			break;
		case 'o':
		case 'e':
			frame(j, p[0], p+9, len);
			break;
		case 's':
			frame(j, 's', p+9, len);
			endjob(j);
			break;
		}
	}
	return n;
}

static long
jobswrite(Chan *c, void *va, long n, vlong off)
{
	Job *j;
	int k;

	USED(off);
	k = KIND(c->qid.path);
	if(k == Qctl)
		return ctlwrite(va, n);
	if(k != Qout && k != Qerr && k != Qstatus)
		error(Eperm);
	if((j = lookjob(JID(c->qid.path))) == nil)
		error(Enonexist);
	if(frame(j, k == Qout ? 'o' : k == Qerr ? 'e' : 's', va, n) < 0 && k != Qstatus)
		error("the client went away");
	if(k == Qstatus)
		endjob(j);
	return n;
}

Dev jobsdevtab = {
	'J',
	"jobs",

	devreset,
	devinit,
	devshutdown,
	jobsattach,
	jobswalk,
	jobsstat,
	jobsopen,
	devcreate,
	jobsclose,
	jobsread,
	devbread,
	jobswrite,
	devbwrite,
	devremove,
	devwstat,
};

/*
 * The socket: each connection hands in one command (frame C) and gets
 * its output and status as frames; the job is the connection's.
 */
static int
sockreadn(int fd, void *va, long n)
{
	char *p;
	long m, t;

	p = va;
	for(t = 0; t < n; t += m)
		if((m = read(fd, p+t, n-t)) <= 0)
			return -1;
	return 0;
}

/* a job's client: when it goes before the job ends, the job is killed */
static void
watchproc(void *v)
{
	Job *j;
	char c;

	j = v;
	while(recv(j->fd, &c, 1, 0) > 0)
		;
	lock(&jl);
	if(!j->ended && !j->killwant){
		j->killwant = 1;
		if(j->rpid)
			nkill++;
	}
	unlock(&jl);
	wakeup(&newr);
}

static void
reply(int fd, char *text)
{
	uchar h[5];
	long n;

	n = strlen(text);
	h[0] = 'o';
	put32(h+1, n);
	send(fd, h, 5, MSG_NOSIGNAL);
	send(fd, text, n, MSG_NOSIGNAL);
	h[0] = 's';
	put32(h+1, 0);
	send(fd, h, 5, MSG_NOSIGNAL);
}

static void
sessionstate(int fd)
{
	char buf[512];
	Job *j;
	int nj;

	lock(&jl);
	nj = 0;
	for(j = jobs; j != nil; j = j->next)
		nj++;
	snprint(buf, sizeof buf, "session %s\nsocket %s\nup %lds\njobs %d running, %d done\nidle %lds\n",
		sessdesc != nil ? sessdesc : "?", sockpath, time(0) - sesssince,
		nj, nextid - 1 - nj, nj > 0 ? 0 : time(0) - lastact);
	unlock(&jl);
	reply(fd, buf);
}

static void
sessionend(void)
{
	if(sockpath != nil)
		unlink(sockpath);
}

static void
idleproc(void *v)
{
	long idle;
	int busy;

	idle = (long)(uintptr)v;
	for(;;){
		osmsleep(1000);
		lock(&jl);
		busy = jobs != nil;
		unlock(&jl);
		if(!busy && time(0) - lastact > idle){
			fprint(2, "9pterm: session idle %lds: ending\n", idle);
			exit(0);
		}
	}
}

static void
acceptproc(void *v)
{
	int lfd, fd;
	uchar h[5];
	ulong len;
	Job *j;

	lfd = (int)(uintptr)v;
	for(;;){
		if((fd = accept(lfd, nil, nil)) < 0)
			continue;
		if(sockreadn(fd, h, 5) < 0){
			close(fd);
			continue;
		}
		if(h[0] == 'Q'){
			sessionstate(fd);
			close(fd);
			continue;
		}
		if(h[0] == 'X'){
			reply(fd, "session ending\n");
			close(fd);
			fprint(2, "9pterm: session ended by -X\n");
			exit(0);
		}
		if(h[0] != 'C'){
			close(fd);
			continue;
		}
		len = h[1]<<24 | h[2]<<16 | h[3]<<8 | h[4];
		if(len > Maxcmd){
			close(fd);
			continue;
		}
		j = mallocz(sizeof *j, 1);
		j->cmd = malloc(len+2);
		if(sockreadn(fd, j->cmd, len) < 0){
			close(fd);
			free(j->cmd);
			free(j);
			continue;
		}
		if(len == 0 || j->cmd[len-1] != '\n')
			j->cmd[len++] = '\n';	/* rc wants the script's last line ended */
		j->cmd[len] = 0;
		j->ncmd = len;
		j->fd = fd;
		lock(&jl);
		j->id = nextid++;
		j->next = jobs;
		jobs = j;
		npending++;
		lastact = time(0);
		unlock(&jl);
		kproc("jobwatch", watchproc, j);
		wakeup(&newr);
	}
}

/*
 * 9pterm -M: the socket, once the session is up (cpu.c), described by
 * desc; the session ends after idle seconds without a job (0: never).
 * -1 on failure.
 */
int
jobslisten(char *path, int idle, char *desc)
{
	struct sockaddr_un sa;
	int fd;

	if(strlen(path) >= sizeof sa.sun_path)
		return -1;
	unlink(path);
	if((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return -1;
	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	strcpy(sa.sun_path, path);
	umask(077);
	if(bind(fd, (struct sockaddr*)&sa, sizeof sa) < 0 || listen(fd, 16) < 0){
		close(fd);
		return -1;
	}
	chmod(path, 0600);
	sockpath = strdup(path);
	sessdesc = strdup(desc);
	sesssince = lastact = time(0);
	atexit(sessionend);	/* the system's: the socket goes with the process */
	kproc("jobs", acceptproc, (void*)(uintptr)fd);
	if(idle > 0)
		kproc("idle", idleproc, (void*)(uintptr)idle);
	return 0;
}

#endif
