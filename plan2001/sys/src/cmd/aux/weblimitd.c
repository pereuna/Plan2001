/*
 * weblimitd - the web accounts' processes and memory on Plan2001's CPU
 * server (docs/webauthn.md): 9front has no quotas, so this watches /proc
 * as the host owner (cpustart) and, for the members of the file server's
 * group web (signupd's), a scan a second:
 *
 *	a user's processes over -p	all of them killed (a fork bomb: rc
 *					puts each & in a note group of its
 *					own, so no group of them is enough)
 *	a user's memory over -m (KB)	its biggest process killed (procs
 *					sharing memory - the same program,
 *					the same size - counted once)
 *	all of theirs over -t (KB)	the biggest of them killed
 *	priority			lowered to -r (9front's normal is 10)
 *
 * A kill is the proc's ctl's: it can not be caught.  What it does goes to fd 1 and, if the machine keeps one,
 * /sys/log/weblimit.  The host owner's processes are never touched.
 * The users file is read again when it changes.
 *
 *	aux/weblimitd [-1n] [-g group | -u user] [-a users] [-p procs]
 *		[-m kb] [-t kb] [-r pri] [-i ms] [-P proc]
 *
 * defaults web, /adm/users, 32, 49152, 262144, 7, 1000, /proc; -1 one scan
 * (a test's), -n says what it would do and does not, -u one user instead
 * of a group (the host owner too).
 */
#include <u.h>
#include <libc.h>
#include <bio.h>

enum {
	Nuser	= 64,
	Nproc	= 4096,
};

typedef struct Pr Pr;
struct Pr
{
	ulong	pid;
	ulong	mem;		/* KB */
	int	basepri;
	int	shared;		/* the same program and memory as one before it: rfork(RFMEM)'s, counted once */
	char	user[28];
	char	text[28];
};

static char *group = "web";
static char *oneuser;
static char *users = "/adm/users";
static char *procdir = "/proc";
static ulong maxproc = 32;
static ulong maxmem = 48*1024;
static ulong totalmem = 256*1024;
static int pri = 7;
static char *eve;
static int dryrun;

static char *members[Nuser];
static int nmembers;
static long usersmtime = -1;
static Pr procs[Nproc];
static int nprocs;

static void
say(char *fmt, ...)
{
	char buf[256];
	va_list arg;

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	if(dryrun){	/* what it would do: not the log's */
		print("%s (-n: not done)\n", buf);
		return;
	}
	print("%s\n", buf);
	if(access("/sys/log/weblimit", AEXIST) == 0)
		syslog(0, "weblimit", "%s", buf);
}

/* the group's members, if users changed (id:name:leader:members) */
static void
readusers(void)
{
	Biobuf *b;
	Dir *d;
	char *l, *f[4], *m[Nuser];
	int i, n;

	if(oneuser != nil)
		return;
	if((d = dirstat(users)) == nil){
		if(usersmtime != -2)
			say("%s: %r: nobody limited", users);
		usersmtime = -2;
		nmembers = 0;
		return;
	}
	if(d->mtime == usersmtime){
		free(d);
		return;
	}
	usersmtime = d->mtime;
	free(d);
	if((b = Bopen(users, OREAD)) == nil)
		return;
	for(i = 0; i < nmembers; i++)
		free(members[i]);
	nmembers = 0;
	while((l = Brdstr(b, '\n', 1)) != nil){
		if(getfields(l, f, nelem(f), 0, ":") == 4 && strcmp(f[1], group) == 0){
			n = getfields(f[3], m, nelem(m), 0, ",");
			for(i = 0; i < n && nmembers < Nuser; i++)
				if(*m[i] != 0)
					members[nmembers++] = strdup(m[i]);
		}
		free(l);
	}
	Bterm(b);
}

static int
limited(char *user)
{
	int i;

	if(oneuser != nil)
		return strcmp(user, oneuser) == 0;
	if(eve != nil && strcmp(user, eve) == 0)
		return 0;
	for(i = 0; i < nmembers; i++)
		if(strcmp(members[i], user) == 0)
			return 1;
	return 0;
}

static char*
readfile(char *fmt, ...)
{
	static char buf[512];
	char *p;
	va_list arg;
	int fd, n;

	va_start(arg, fmt);
	p = vsmprint(fmt, arg);
	va_end(arg);
	fd = open(p, OREAD);
	free(p);
	if(fd < 0)
		return nil;
	n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if(n <= 0)
		return nil;
	buf[n] = 0;
	return buf;
}

static int
ctl(ulong pid, char *msg)
{
	char *p;
	int fd, r;

	p = smprint("%s/%lud/ctl", procdir, pid);
	fd = open(p, OWRITE);
	free(p);
	if(fd < 0)
		return -1;
	if(dryrun){
		close(fd);
		return 0;
	}
	r = write(fd, msg, strlen(msg));
	close(fd);
	return r < 0 ? -1 : 0;
}

/* /proc's limited processes: status's columns (text 27, user 27, state 11, 9 numbers: the 7th memory) */
static void
scanprocs(void)
{
	Dir *d;
	char *s, *f[16], *e;
	long i, n;
	int nf, j;
	Pr *p;

	nprocs = 0;
	if((i = open(procdir, OREAD)) < 0)
		sysfatal("%s: %r", procdir);
	n = dirreadall(i, &d);
	close(i);
	for(i = 0; i < n && nprocs < Nproc; i++){
		p = &procs[nprocs];
		p->pid = strtoul(d[i].name, &e, 10);
		if(*e != 0 || p->pid == getpid() || (s = readfile("%s/%s/status", procdir, d[i].name)) == nil || strlen(s) < 68)
			continue;
		memmove(p->text, s, 27);
		p->text[27] = 0;
		if((e = strchr(p->text, ' ')) != nil)
			*e = 0;
		memmove(p->user, s+28, 27);
		p->user[27] = 0;
		if((e = strchr(p->user, ' ')) != nil)
			*e = 0;
		if(!limited(p->user))
			continue;
		nf = tokenize(s+68, f, nelem(f));
		p->mem = nf > 6 ? strtoul(f[6], nil, 10) : 0;
		p->basepri = nf > 7 ? atoi(f[7]) : 0;
		p->shared = 0;
		for(j = 0; j < nprocs; j++)
			if(procs[j].mem == p->mem && strcmp(procs[j].user, p->user) == 0 && strcmp(procs[j].text, p->text) == 0){
				p->shared = 1;
				break;
			}
		nprocs++;
	}
	free(d);
}

/* all of the user's processes */
static void
killuser(char *who)
{
	char user[28];
	int i, k;

	strecpy(user, user+sizeof user, who);	/* who is a proc's, cleared when it is killed */
	k = 0;
	for(i = 0; i < nprocs; i++)
		if(strcmp(procs[i].user, user) == 0 && ctl(procs[i].pid, "kill") == 0){
			procs[i].mem = 0;
			procs[i].user[0] = 0;	/* gone: not counted again */
			k++;
		}
	say("%s: killed (%d procs)", user, k);
}

static void
killproc(Pr *p, char *why)
{
	say("%s: %s: proc %lud killed (%lud KB)", p->user, why, p->pid, p->mem);
	if(ctl(p->pid, "kill") == 0){
		p->mem = 0;
		p->user[0] = 0;
	}
}

static void
scan(void)
{
	char *u, done[Nproc];
	ulong n, mem, total;
	int i, j, big;
	char buf[32];

	readusers();
	scanprocs();
	memset(done, 0, sizeof done);
	for(i = 0; i < nprocs; i++){
		if(done[i] || procs[i].user[0] == 0)
			continue;
		u = procs[i].user;
		/* the user's procs and memory, its biggest note group and process */
		n = mem = 0;
		big = -1;
		for(j = i; j < nprocs; j++)
			if(strcmp(procs[j].user, u) == 0){
				done[j] = 1;
				n++;
				if(!procs[j].shared)
					mem += procs[j].mem;
				if(big < 0 || procs[j].mem > procs[big].mem)
					big = j;
			}
		if(n > maxproc){
			say("%s: %lud procs > %lud", u, n, maxproc);
			killuser(u);
			continue;
		}
		if(mem > maxmem && big >= 0){
			snprint(buf, sizeof buf, "%lud KB > %lud", mem, maxmem);
			killproc(&procs[big], buf);
		}
	}
	/* all of theirs */
	total = 0;
	big = -1;
	for(i = 0; i < nprocs; i++)
		if(procs[i].user[0] != 0){
			if(!procs[i].shared)
				total += procs[i].mem;
			if(big < 0 || procs[i].mem > procs[big].mem)
				big = i;
		}
	if(total > totalmem && big >= 0){
		snprint(buf, sizeof buf, "all %lud KB > %lud", total, totalmem);
		killproc(&procs[big], buf);
	}
	/* their priority (9front: the host owner may lower it) */
	if(pri > 0){
		snprint(buf, sizeof buf, "pri %d", pri);
		for(i = 0; i < nprocs; i++)
			if(procs[i].user[0] != 0 && procs[i].basepri > pri)
				ctl(procs[i].pid, buf);
	}
}

static void
usage(void)
{
	fprint(2, "usage: aux/weblimitd [-1n] [-g group | -u user] [-a users] [-p procs] [-m kb] [-t kb] [-r pri] [-i ms] [-P proc]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int once, ms;
	char *s;

	once = 0;
	ms = 1000;
	ARGBEGIN{
	case '1':
		once = 1;
		break;
	case 'n':
		dryrun = 1;
		break;
	case 'g':
		group = EARGF(usage());
		break;
	case 'u':
		oneuser = EARGF(usage());
		break;
	case 'a':
		users = EARGF(usage());
		break;
	case 'p':
		maxproc = strtoul(EARGF(usage()), nil, 10);
		break;
	case 'm':
		maxmem = strtoul(EARGF(usage()), nil, 10);
		break;
	case 't':
		totalmem = strtoul(EARGF(usage()), nil, 10);
		break;
	case 'r':
		pri = atoi(EARGF(usage()));
		break;
	case 'i':
		ms = atoi(EARGF(usage()));
		break;
	case 'P':
		procdir = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND
	if(argc != 0)
		usage();
	if((s = readfile("/dev/hostowner")) != nil)
		eve = strdup(s);
	if(once){
		scan();
		exits(nil);
	}
	say("watching group %s: %lud procs, %lud KB each, %lud KB all, pri %d", oneuser != nil ? oneuser : group, maxproc, maxmem, totalmem, pri);
	for(;;){
		scan();
		sleep(ms);
	}
}
