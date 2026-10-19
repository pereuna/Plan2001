/*
 * ktest: Plan2001's kernel under load, from a root it mounted itself
 * (the root's /init runs it when plan9.ini has ktest=...).  Each test
 * runs in its own process, under an alarm; a note or an exit string is
 * a failure.  The file system tests work in -d dir (default /tmp/ktest,
 * on the root: the kernel's gefs, devgefs); the others stress memory and
 * processes, and look at what Plan2001 changed against 9front.  ktest
 * refuses a directory that is not the kernel's gefs (a ramfs on /tmp)
 * unless -u.
 *	ktest [-u] [-d dir] [-s scale] [-t secs] [all | fs | mem | proc | mix | k | test...]
 * Prints KTEST name OK ms / KTEST name FAIL why, then
 * KTEST-DONE ok N fail M.  fsfill (fills the file system) only by name.
 */
#include <u.h>
#include <libc.h>

typedef struct Test Test;
struct Test {
	char	*name;
	char	*group;
	void	(*fn)(void);
};

char	*dir = "/tmp/ktest";
int	anyfs;		/* -u: the fs tests on any file server (a userspace gefs) */
int	scale = 1;
int	secs = 300;
char	buf[64*1024];

char*	readstr(char*);

#pragma varargck argpos fail 1

void
fail(char *fmt, ...)
{
	char s[ERRMAX+128];
	va_list arg;

	va_start(arg, fmt);
	vsnprint(s, sizeof s, fmt, arg);
	va_end(arg);
	exits(s);
}

/* the word at offset o of a file made with seed */
uvlong
pat(uvlong seed, vlong o)
{
	uvlong x;

	x = (o/8 + 1) * 0x9E3779B97F4A7C15ULL ^ seed;
	x ^= x >> 29;
	x *= 0xBF58476D1CE4E5B9ULL;
	return x ^ x >> 32;
}

void
fillpat(uchar *p, long n, uvlong seed, vlong off)
{
	long i;
	uvlong w;

	for(i = 0; i < n; i += 8){
		w = pat(seed, off+i);
		memmove(p+i, &w, n-i < 8 ? n-i : 8);
	}
}

int
checkpat(uchar *p, long n, uvlong seed, vlong off)
{
	uchar q[8];
	long i;
	uvlong w;

	for(i = 0; i < n; i += 8){
		w = pat(seed, off+i);
		memmove(q, &w, 8);
		if(memcmp(p+i, q, n-i < 8 ? n-i : 8) != 0)
			return -1;
	}
	return 0;
}

char*
path(char *fmt, ...)
{
	static char s[8192];
	char t[8000];
	va_list arg;

	va_start(arg, fmt);
	vsnprint(t, sizeof t, fmt, arg);
	va_end(arg);
	snprint(s, sizeof s, "%s/%s", dir, t);
	return s;
}

void
writefile(char *f, uvlong seed, vlong size)
{
	int fd;
	vlong o;
	long n;

	if((fd = create(f, OWRITE, 0664)) < 0)
		fail("create %s: %r", f);
	for(o = 0; o < size; o += n){
		n = size-o > sizeof buf ? sizeof buf : size-o;
		fillpat((uchar*)buf, n, seed, o);
		if(write(fd, buf, n) != n)
			fail("write %s at %lld: %r", f, o);
	}
	close(fd);
}

void
checkfile(char *f, uvlong seed, vlong size)
{
	Dir *d;
	int fd;
	vlong o;
	long n;

	if((d = dirstat(f)) == nil)
		fail("stat %s: %r", f);
	if(d->length != size)
		fail("%s: length %lld, not %lld", f, d->length, size);
	free(d);
	if((fd = open(f, OREAD)) < 0)
		fail("open %s: %r", f);
	for(o = 0; o < size; o += n){
		n = size-o > sizeof buf ? sizeof buf : size-o;
		if(readn(fd, buf, n) != n)
			fail("read %s at %lld: %r", f, o);
		if(checkpat((uchar*)buf, n, seed, o) < 0)
			fail("%s: wrong data at %lld", f, o);
	}
	close(fd);
}

void
mkdirp(char *d)
{
	int fd;

	if(access(d, AEXIST) == 0)
		return;
	if((fd = create(d, OREAD, DMDIR|0775)) < 0)
		fail("mkdir %s: %r", d);
	close(fd);
}

/* d and all in it */
void
rmtree(char *d)
{
	Dir *e;
	int fd, i, n;
	char *s;

	/* all of it read and closed first: no descriptor per level */
	if((fd = open(d, OREAD)) >= 0){
		n = dirreadall(fd, &e);
		close(fd);
		for(i = 0; i < n; i++){
			s = smprint("%s/%s", d, e[i].name);
			if(e[i].mode & DMDIR)
				rmtree(s);
			else if(remove(s) < 0)
				fail("remove %s: %r", s);
			free(s);
		}
		if(n > 0)
			free(e);
	}
	remove(d);
}

int
countdir(char *d)
{
	Dir *e;
	int fd, n, t;

	if((fd = open(d, OREAD)) < 0)
		fail("open %s: %r", d);
	t = 0;
	while((n = dirread(fd, &e)) > 0){
		t += n;
		free(e);
	}
	if(n < 0)
		fail("dirread %s after %d: %r", d, t);
	close(fd);
	return t;
}

/* run fn in n processes, each with its index; wait for all */
void
procs(int n, void (*fn)(int))
{
	Waitmsg *w;
	int i;

	for(i = 0; i < n; i++)
		switch(rfork(RFPROC|RFFDG)){
		case -1:
			fail("fork: %r");
		case 0:
			(*fn)(i);
			exits(nil);
		}
	for(i = 0; i < n; i++){
		if((w = wait()) == nil)
			fail("wait: %r");
		if(w->msg[0])
			fail("child: %s", w->msg);
		free(w);
	}
}

/*
 * file system: the kernel's gefs (the root), written
 */
void
fsbig(void)
{
	char *f;
	vlong size, o;
	int fd, i;
	uvlong seed;

	size = 64LL*1024*1024*scale;
	f = path("big");
	writefile(f, 1, size);
	checkfile(f, 1, size);
	/* rewrites at random places, unaligned */
	if((fd = open(f, ORDWR)) < 0)
		fail("open %s: %r", f);
	srand(2001);
	for(i = 0; i < 2000; i++){
		o = (vlong)nrand(size/512) * 512 + nrand(63)*8;	/* the pattern's words */
		if(o + 1000 > size)
			continue;
		seed = 1;
		fillpat((uchar*)buf, 1000, seed, o);
		if(pwrite(fd, buf, 1000, o) != 1000)
			fail("pwrite at %lld: %r", o);
	}
	close(fd);
	checkfile(f, 1, size);
	if(remove(f) < 0)
		fail("remove %s: %r", f);
}

/*
 * one write bigger than a 9P message (64 KiB): all of it, not
 * a short count (ar wrote a big member at once and took one for an error)
 */
void
fsbigio(void)
{
	char *f;
	uchar *b, *r;
	long n;
	int fd;

	n = 1024*1024 + 13;
	b = malloc(n);
	r = malloc(n);
	if(b == nil || r == nil)
		fail("malloc: %r");
	fillpat(b, n, 7, 0);
	f = path("bigio");
	if((fd = create(f, ORDWR, 0666)) < 0)
		fail("create %s: %r", f);
	if(write(fd, b, n) != n)
		fail("write %ld at once: %r", n);
	if(pwrite(fd, b, n, 0) != n)
		fail("pwrite %ld at once: %r", n);
	if(seek(fd, 0, 0) != 0 || readn(fd, r, n) != n)
		fail("read back: %r");
	if(memcmp(b, r, n) != 0)
		fail("read back: not as written");
	close(fd);
	if(remove(f) < 0)
		fail("remove %s: %r", f);
	free(b);
	free(r);
}

void
fsmany(void)
{
	int i, n, fd;
	char *d;
	long r;

	n = 3000*scale;
	d = strdup(path("many"));	/* path's buffer is reused */
	mkdirp(d);
	for(i = 0; i < n; i++){
		if((fd = create(path("many/f%d", i), OWRITE, 0664)) < 0)
			fail("create f%d: %r", i);
		fprint(fd, "%d\n", i);
		close(fd);
	}
	if(countdir(d) != n)
		fail("%d entries, not %d", countdir(d), n);
	for(i = 0; i < n; i += 2)
		if(remove(path("many/f%d", i)) < 0)
			fail("remove f%d: %r", i);
	if(countdir(d) != n/2)
		fail("%d entries after removing half, not %d", countdir(d), n/2);
	for(i = 1; i < n; i += 2){
		if((fd = open(path("many/f%d", i), OREAD)) < 0)
			fail("open f%d: %r", i);
		r = read(fd, buf, 32);
		buf[r < 0 ? 0 : r] = 0;
		close(fd);
		if(atoi(buf) != i)
			fail("f%d holds %s", i, buf);
	}
	rmtree(d);
	if(access(d, AEXIST) == 0)
		fail("%s still there", d);
	free(d);
}

void
fsdeep(void)
{
	char *p, *e;
	int i, n, fd;

	n = 150;
	p = malloc(8192);
	e = seprint(p, p+8192, "%s/deep", dir);
	mkdirp(p);
	for(i = 0; i < n; i++){
		e = seprint(e, p+8192, "/d%d", i);
		mkdirp(p);
	}
	seprint(e, p+8192, "/file");
	/* one walk of n+3 names: the kernel's stack, the file server's walk */
	writefile(p, 7, 10000);
	checkfile(p, 7, 10000);
	if((fd = open(p, OREAD)) < 0)
		fail("open deep: %r");
	close(fd);
	if(chdir(p) == 0)
		fail("chdir to a file worked");
	*e = 0;
	if(chdir(p) < 0)
		fail("chdir deep: %r");
	for(i = 0; i < n; i++)
		if(chdir("..") < 0)
			fail("chdir ..: %r");
	if(chdir(dir) < 0)
		fail("chdir %s: %r", dir);
	rmtree(path("deep"));
	free(p);
}

void
fsrename(void)
{
	Dir nd;
	int i, n, k;
	char a[64], b[64];

	n = 50;
	mkdirp(path("ren"));
	for(i = 0; i < n; i++)
		writefile(path("ren/a%d", i), i, 4000+i);
	/* round and round: a -> b -> a, by wstat */
	for(k = 0; k < 20; k++)
		for(i = 0; i < n; i++){
			snprint(a, sizeof a, k%2 ? "b%d" : "a%d", i);
			snprint(b, sizeof b, k%2 ? "a%d" : "b%d", i);
			nulldir(&nd);
			nd.name = b;
			if(dirwstat(path("ren/%s", a), &nd) < 0)
				fail("rename %s to %s: %r", a, b);
		}
	for(i = 0; i < n; i++)
		checkfile(path("ren/a%d", i), i, 4000+i);
	if(countdir(path("ren")) != n)
		fail("%d entries, not %d", countdir(path("ren")), n);
	/* onto an existing name: refused */
	nulldir(&nd);
	nd.name = "a1";
	if(dirwstat(path("ren/a0"), &nd) == 0)
		fail("rename onto a1 worked");
	rmtree(path("ren"));
}

/*
 * renames of a file held open: its dent (the file server's) stays
 * cached across them, so a stale name in it shows (gefs answered such
 * a rename done without doing it)
 */
void
fsrenopen(void)
{
	Dir nd;
	int fd, k;
	char *a, *b, *f, *t;

	mkdirp(path("renopen"));
	a = strdup(path("renopen/a"));
	b = strdup(path("renopen/b"));
	writefile(a, 7, 5000);
	if((fd = open(a, OREAD)) < 0)
		fail("open: %r");
	for(k = 0; k < 100; k++){
		f = k%2 ? b : a;
		t = k%2 ? a : b;
		nulldir(&nd);
		nd.name = strrchr(t, '/')+1;
		if(dirwstat(f, &nd) < 0)
			fail("rename %d: %r", k);
		if(access(t, AEXIST) < 0)
			fail("rename %d done, but %s: %r", k, t);
		if(access(f, AEXIST) == 0)
			fail("rename %d done, but %s still there", k, f);
	}
	close(fd);
	checkfile(a, 7, 5000);
	rmtree(path("renopen"));
	free(a);
	free(b);
}

void
concw(int k)
{
	int i;

	for(i = 0; i < 100*scale; i++){
		writefile(path("conc/w%d.%d", k, i), k*1000+i, 16*1024+i*7);
		if(i % 10 == 0)
			checkfile(path("conc/w%d.%d", k, i), k*1000+i, 16*1024+i*7);
	}
	for(i = 0; i < 100*scale; i++)
		checkfile(path("conc/w%d.%d", k, i), k*1000+i, 16*1024+i*7);
}

void
concr(int)
{
	int i;

	for(i = 0; i < 5; i++)
		checkfile(path("conc/shared"), 42, 8*1024*1024);
}

void
both(int k)
{
	if(k < 8)
		concw(k);
	else
		concr(k);
}

void
fsconc(void)
{
	mkdirp(path("conc"));
	writefile(path("conc/shared"), 42, 8*1024*1024);
	procs(12, both);
	if(countdir(path("conc")) != 8*100*scale + 1)
		fail("%d entries", countdir(path("conc")));
	rmtree(path("conc"));
}

void
fssparse(void)
{
	char *f;
	int fd, i;
	Dir *d;

	f = path("sparse");
	if((fd = create(f, ORDWR, 0664)) < 0)
		fail("create: %r");
	if(pwrite(fd, "end", 3, 1000000) != 3)
		fail("pwrite past the end: %r");
	close(fd);
	if((d = dirstat(f)) == nil || d->length != 1000003)
		fail("length %lld, not 1000003", d ? d->length : -1);
	free(d);
	if((fd = open(f, OREAD)) < 0)
		fail("open: %r");
	for(i = 0; i < 1000000; i += sizeof buf){
		long n = 1000000-i > sizeof buf ? sizeof buf : 1000000-i;
		if(readn(fd, buf, n) != n)
			fail("read: %r");
		while(--n >= 0)
			if(buf[n] != 0)
				fail("not zero at %ld", i+n);
	}
	if(readn(fd, buf, 3) != 3 || memcmp(buf, "end", 3) != 0)
		fail("the end is not there");
	close(fd);
	remove(f);
}

enum { Arec = 64, Anrec = 500 };

void
appender(int k)
{
	int fd, i;
	char r[Arec];

	if((fd = open(path("append"), OWRITE)) < 0)
		fail("open: %r");
	for(i = 0; i < Anrec; i++){
		memset(r, 'a'+k, Arec);
		snprint(r, Arec, "%d %d ", k, i);
		r[Arec-1] = '\n';
		if(write(fd, r, Arec) != Arec)
			fail("append: %r");
	}
	close(fd);
}

void
fsappend(void)
{
	int fd, i, k, seen[4];
	Dir *d;
	char r[Arec];

	if((fd = create(path("append"), OWRITE, DMAPPEND|0664)) < 0)
		fail("create: %r");
	close(fd);
	if((d = dirstat(path("append"))) == nil || !(d->mode & DMAPPEND))
		fail("not append-only");
	free(d);
	procs(4, appender);
	if((d = dirstat(path("append"))) == nil || d->length != 4*Anrec*Arec)
		fail("length %lld, not %d", d ? d->length : -1, 4*Anrec*Arec);
	free(d);
	memset(seen, 0, sizeof seen);
	if((fd = open(path("append"), OREAD)) < 0)
		fail("open: %r");
	for(i = 0; i < 4*Anrec; i++){
		if(readn(fd, r, Arec) != Arec)
			fail("read: %r");
		k = atoi(r);
		if(k < 0 || k > 3 || r[Arec-2] != 'a'+k || r[Arec-1] != '\n')
			fail("record %d mixed: %.20s", i, r);
		if(atoi(strchr(r, ' ')+1) != seen[k]++)
			fail("records of %d out of order", k);
	}
	close(fd);
	remove(path("append"));
}

void
fstrunc(void)
{
	Dir nd, *d;
	char *f;
	int fd, i;

	f = path("trunc");
	writefile(f, 9, 1000000);
	nulldir(&nd);
	nd.length = 100000;
	if(dirwstat(f, &nd) < 0)
		fail("truncate: %r");
	checkfile(f, 9, 100000);
	nd.length = 200000;
	if(dirwstat(f, &nd) < 0)
		fail("extend: %r");
	if((d = dirstat(f)) == nil || d->length != 200000)
		fail("length after extend");
	free(d);
	if((fd = open(f, OREAD)) < 0 || pread(fd, buf, 60000, 100000) != 60000)
		fail("read the extension: %r");
	close(fd);
	for(i = 0; i < 60000; i++)
		if(buf[i] != 0)
			fail("extension not zero at %d", 100000+i);
	/* OTRUNC */
	if((fd = open(f, OWRITE|OTRUNC)) < 0)
		fail("OTRUNC: %r");
	close(fd);
	if((d = dirstat(f)) == nil || d->length != 0)
		fail("not empty after OTRUNC");
	free(d);
	remove(f);
}

void
fswstat(void)
{
	Dir nd, *d;
	char *f;
	int fd;

	f = strdup(path("wstat"));
	writefile(f, 3, 100);
	nulldir(&nd);
	nd.mode = 0600;
	nd.mtime = 1000000000;
	if(dirwstat(f, &nd) < 0)
		fail("wstat: %r");
	if((d = dirstat(f)) == nil)
		fail("stat: %r");
	if((d->mode&0777) != 0600 || d->mtime != 1000000000)
		fail("mode %luo mtime %lud", d->mode&0777, d->mtime);
	free(d);
	nulldir(&nd);
	nd.mode = DMDIR|0600;
	if(dirwstat(f, &nd) == 0)
		fail("a file made a directory");
	/* ORCLOSE */
	if((fd = create(path("rclose"), OWRITE|ORCLOSE, 0664)) < 0)
		fail("create ORCLOSE: %r");
	write(fd, "x", 1);
	if(access(path("rclose"), AEXIST) < 0)
		fail("ORCLOSE file not there while open");
	close(fd);
	if(access(path("rclose"), AEXIST) == 0)
		fail("ORCLOSE file there after close");
	/* create in a file, remove of a full directory */
	if(create(path("wstat/x"), OWRITE, 0664) >= 0)
		fail("create in a file worked");
	mkdirp(path("full"));
	writefile(path("full/x"), 1, 10);
	if(remove(path("full")) == 0)
		fail("removed a full directory");
	rmtree(path("full"));
	remove(f);
	free(f);
}

/* the image cache: the binary rewritten in place, run again */
char*
runout(char *cmd, char *arg)
{
	static char out[256];
	int p[2], n;

	pipe(p);
	switch(fork()){
	case -1:
		fail("fork: %r");
	case 0:
		dup(p[1], 1);
		close(p[0]);
		close(p[1]);
		execl(cmd, cmd, arg, nil);
		fail("exec %s: %r", cmd);
	}
	close(p[1]);
	n = readn(p[0], out, sizeof out - 1);
	close(p[0]);
	waitpid();
	out[n < 0 ? 0 : n] = 0;
	if(n > 0 && out[n-1] == '\n')
		out[n-1] = 0;
	return out;
}

void
copy(char *from, char *to)
{
	int f, t;
	long n;

	if((f = open(from, OREAD)) < 0)
		fail("copy %s: %r", from);
	if((t = create(to, OWRITE, 0775)) < 0)
		fail("copy to %s: %r", to);
	while((n = read(f, buf, sizeof buf)) > 0)
		if(write(t, buf, n) != n)
			fail("copy: %r");
	close(f);
	close(t);
}

void
fsexec(void)
{
	char *x, *o;
	int i;

	x = strdup(path("x"));
	for(i = 0; i < 20; i++){
		copy("/bin/echo", x);
		if(strcmp(o = runout(x, "a/b"), "a/b") != 0)
			fail("echo gave %s", o);
		copy("/bin/basename", x);	/* the same second, mostly */
		if(strcmp(o = runout(x, "a/b"), "b") != 0)
			fail("old image run: basename gave %s", o);
	}
	remove(x);
	free(x);
}

/* OTRUNC needs write permission, ORCLOSE the directory's: as none */
/*
 * A file server checks its attach's user: the root is attached as the
 * host owner, so none mounts it anew (an fd of #s/gefs.root opened
 * before) and is checked there.
 */
void
fsperm(void)
{
	Waitmsg *w;
	int fd, sfd;
	char f[256];

	mkdirp(path("perm"));
	writefile(path("perm/f"), 21, 5000);
	if((sfd = open("#s/gefs.root", ORDWR)) < 0)
		fail("#s/gefs.root: %r");
	snprint(f, sizeof f, "/n/ktestnone%s", path("perm/f"));
	switch(fork()){
	case 0:
		if((fd = open("#c/user", OWRITE)) < 0 || write(fd, "none", 4) != 4)
			fail("becoming none: %r");
		close(fd);
		if(rfork(RFNAMEG) < 0 || mount(sfd, -1, "/n/ktestnone", MREPL|MCREATE, "main") < 0)
			fail("none's mount: %r");
		if((fd = open(f, OREAD)) < 0)
			fail("none cannot read: %r");
		close(fd);
		if(open(f, OREAD|OTRUNC) >= 0)
			fail("none truncated a file it cannot write");
		if((fd = open(f, OREAD|ORCLOSE)) >= 0){
			close(fd);
			fail("none opened ORCLOSE in a directory it cannot write");
		}
		if(remove(f) == 0)
			fail("none removed it");
		exits(nil);
	case -1:
		fail("fork: %r");
	}
	close(sfd);
	if((w = wait()) == nil || w->msg[0])
		fail("%s", w ? w->msg : "wait");
	checkfile(path("perm/f"), 21, 5000);
	rmtree(path("perm"));
}

/* a removed file's open fd: errors, not the next file on its inode */
void
fsstale(void)
{
	int fd, wfd, i, n;
	char *f;

	f = strdup(path("stale"));
	writefile(f, 31, 50000);
	if((fd = open(f, OREAD)) < 0)
		fail("open: %r");
	if((wfd = open(f, OWRITE)) < 0)
		fail("open for writing: %r");
	if(remove(f) < 0)
		fail("remove: %r");
	/* new files, one of them likely on the freed inode */
	for(i = 0; i < 20; i++)
		writefile(path("stale%d", i), 40+i, 50000);
	n = pread(fd, buf, 1000, 0);
	if(n > 0 && checkpat((uchar*)buf, n, 31, 0) < 0)
		fail("read the next file's data through a removed one's fd");
	if(n >= 0)
		fail("read of a removed file worked (%d)", n);
	if(pwrite(wfd, "x", 1, 0) == 1)
		fail("write to a removed file worked");
	close(fd);
	close(wfd);
	for(i = 0; i < 20; i++){
		checkfile(path("stale%d", i), 40+i, 50000);
		remove(path("stale%d", i));
	}
	free(f);
}

/* a write far past the end: zeros, a chunk at a time, stopped by a note */
void
fsgrow(void)
{
	char *f;
	int fd, pid, i;
	vlong o;
	Waitmsg *w;
	vlong t0;

	f = strdup(path("grow"));
	if((fd = create(f, ORDWR, 0664)) < 0)
		fail("create: %r");
	if(pwrite(fd, "end", 3, 20*1024*1024) != 3)
		fail("write at 20 MB: %r");
	for(o = 0; o < 20*1024*1024; o += 1024*1024+4097){
		if(pread(fd, buf, 4096, o) != 4096)
			fail("read at %lld: %r", o);
		for(i = 0; i < 4096; i++)
			if(buf[i] != 0)
				fail("not zero at %lld", o+i);
	}
	close(fd);
	remove(f);
	switch(pid = fork()){
	case 0:
		if((fd = create(f, OWRITE, 0664)) < 0)
			fail("create: %r");
		pwrite(fd, "x", 1, 2LL*1024*1024*1024);
		exits("not stopped");
	case -1:
		fail("fork: %r");
	}
	sleep(1000);
	t0 = nsec();
	postnote(PNPROC, pid, "ktest");
	if((w = wait()) == nil)
		fail("wait: %r");
	if((nsec()-t0)/1000000 > 5000)
		fail("stopped only after %lld ms", (nsec()-t0)/1000000);
	free(w);
	remove(f);
	free(f);
}

/* sync through the adm tree's ctl (gefs's sync: the disk's cache flushed too) */
void
fssync(void)
{
	int fd;

	writefile(path("sync"), 5, 300000);
	if(mount(open("#s/gefs.root", ORDWR), -1, "/n/ktestadm", MREPL|MCREATE, "adm") < 0)
		fail("mount #s/gefs.root adm: %r");
	if((fd = open("/n/ktestadm/ctl", OWRITE)) < 0)
		fail("adm ctl: %r");
	if(fprint(fd, "sync") < 0)
		fail("sync: %r");
	close(fd);
	unmount(nil, "/n/ktestadm");
	checkfile(path("sync"), 5, 300000);
	remove(path("sync"));
}

/*
 * object stores (DMOBJ, the kernel gefs' obj.c, docs/gefs-obj.md):
 * records written, versions read back as a stream
 */
#ifndef DMOBJ
#define DMOBJ	0x02000000
#define QTOBJ	0x02
#endif

typedef struct Oread Oread;
struct Oread {
	uvlong	n;
	uvlong	k;
	char	user[256];
	int	nobj;
	uvlong	oid[64];
	uvlong	ver[64];
	vlong	len[64];
	uchar	*data[64];
};

uchar	obuf[256*1024];
char	*ostep = "";	/* fsobj's step, in its failures */

uchar*
orec(uchar *p, int t, uvlong oid, vlong len)
{
	*p++ = t;
	if(t == 'p' || t == 'd' || t == 'v' || t == 's'){
		p[0] = oid>>56; p[1] = oid>>48; p[2] = oid>>40; p[3] = oid>>32;
		p[4] = oid>>24; p[5] = oid>>16; p[6] = oid>>8; p[7] = oid;
		p += 8;
	}
	if(t == 'p'){
		p[0] = len>>56; p[1] = len>>48; p[2] = len>>40; p[3] = len>>32;
		p[4] = len>>24; p[5] = len>>16; p[6] = len>>8; p[7] = len;
		p += 8;
	}
	return p;
}

void
owrite(int fd, uchar *b, uchar *e)
{
	if(write(fd, b, e-b) != e-b)
		fail("%s: object write: %r", ostep);
}

void
oput(int fd, uvlong oid, uvlong seed, vlong len)
{
	uchar *p;

	p = orec(obuf, 'p', oid, len);
	fillpat(p, len, seed, 0);
	owrite(fd, obuf, p+len);
}

void
ocommit(int fd, int sync)
{
	uchar b[2];

	b[0] = 'c';
	b[1] = sync;
	owrite(fd, b, b+2);
}

uvlong
get64(uchar *p)
{
	return (uvlong)p[0]<<56 | (uvlong)p[1]<<48 | (uvlong)p[2]<<40 | (uvlong)p[3]<<32
		| (uvlong)p[4]<<24 | p[5]<<16 | p[6]<<8 | p[7];
}

/* the whole stream from offset 0, in reads of chunk bytes */
void
oread(int fd, Oread *r, int chunk)
{
	vlong off, n, tot;
	uchar *p, *e;
	int i;

	for(i = 0; i < r->nobj; i++)
		free(r->data[i]);
	memset(r, 0, sizeof *r);
	tot = 0;
	for(off = 0;; off += n){
		if(tot + chunk > sizeof obuf)
			fail("%s: object stream over %d bytes", ostep, (int)sizeof obuf);
		if((n = pread(fd, obuf+tot, chunk, off)) < 0)
			fail("%s: object read at %lld: %r", ostep, off);
		if(n == 0)
			break;
		tot += n;
	}
	p = obuf;
	e = obuf+tot;
	if(e-p < 21 || *p != 'H')
		fail("%s: object stream: no header", ostep);
	r->n = get64(p+1);
	r->k = get64(p+9);
	i = p[17]<<24 | p[18]<<16 | p[19]<<8 | p[20];
	memmove(r->user, p+21, i);
	r->user[i] = 0;
	p += 21+i;
	while(p < e){
		if(*p != 'o' || e-p < 25 || r->nobj == nelem(r->oid))
			fail("%s: object stream: bad record at %lld", ostep, (vlong)(p-obuf));
		i = r->nobj++;
		r->oid[i] = get64(p+1);
		r->ver[i] = get64(p+9);
		r->len[i] = get64(p+17);
		p += 25;
		if(r->len[i] > e-p)
			fail("%s: object stream: object %llud short", ostep, r->oid[i]);
		r->data[i] = malloc(r->len[i]+1);
		memmove(r->data[i], p, r->len[i]);
		p += r->len[i];
	}
}

/* oid's object in r: its version and pattern, or ver 0: not there */
void
ohas(Oread *r, uvlong oid, uvlong ver, uvlong seed, vlong len)
{
	uchar *b;
	int i;

	for(i = 0; i < r->nobj; i++)
		if(r->oid[i] == oid)
			break;
	if(ver == 0){
		if(i < r->nobj)
			fail("%s: object %llud there (version %llud)", ostep, oid, r->ver[i]);
		return;
	}
	if(i == r->nobj)
		fail("%s: object %llud missing (N %llud)", ostep, oid, r->n);
	if(r->ver[i] != ver || r->len[i] != len)
		fail("%s: object %llud: version %llud len %lld, not %llud %lld",
			ostep, oid, r->ver[i], r->len[i], ver, len);
	b = malloc(len+1);
	fillpat(b, len, seed, 0);
	if(memcmp(b, r->data[i], len) != 0)
		fail("%s: object %llud: data differs", ostep, oid);
	free(b);
}

void
fsobj(void)
{
	static Oread r;
	uchar b[64], *p;
	char *f, e[ERRMAX];
	int fd, fd2, i;
	Dir *d;

	if(anyfs)
		return;	/* the kernel gefs' */
	f = strdup(path("obj"));
	if((fd = create(f, ORDWR, DMOBJ|0600)) < 0)
		fail("create DMOBJ: %r");
	if((d = dirfstat(fd)) == nil)
		fail("stat: %r");
	if((d->qid.type & QTOBJ) == 0 || (d->mode & DMOBJ) == 0)
		fail("qid.type %ux mode %ulx: not an object store", d->qid.type, d->mode);
	free(d);

	/* empty: the header, N 0 */
	ostep = "new";
	oread(fd, &r, 8192);
	if(r.n != 0 || r.nobj != 0)
		fail("new store: N %llud, %d objects", r.n, r.nobj);

	/* version 1: three objects, one over many chunks, one empty */
	ostep = "v1";
	oput(fd, 1, 11, 5);
	oput(fd, 2, 12, 1200);
	oput(fd, 3, 13, 0);
	p = b;
	*p++ = 'h'; *p++ = 0; *p++ = 0; *p++ = 0; *p++ = 2; *p++ = 'u'; *p++ = '1';
	owrite(fd, b, p);
	ocommit(fd, 1);
	oread(fd, &r, 8192);
	if(r.n != 1 || r.k != 1 || strcmp(r.user, "u1") != 0)
		fail("version 1: N %llud K %llud user %s", r.n, r.k, r.user);
	ohas(&r, 1, 1, 11, 5);
	ohas(&r, 2, 1, 12, 1200);
	ohas(&r, 3, 1, 13, 0);

	/* the same in reads of 7 bytes */
	ostep = "small reads";
	oread(fd, &r, 7);
	ohas(&r, 2, 1, 12, 1200);

	/* version 2: 2 shorter, 1 deleted; 3 stays version 1 */
	ostep = "v2";
	oput(fd, 2, 22, 10);
	owrite(fd, b, orec(b, 'd', 1, 0));
	ocommit(fd, 0);
	oread(fd, &r, 8192);
	if(r.n != 2 || strcmp(r.user, "u1") != 0)
		fail("version 2: N %llud user %s", r.n, r.user);
	ohas(&r, 1, 0, 0, 0);
	ohas(&r, 2, 2, 22, 10);
	ohas(&r, 3, 1, 13, 0);

	/* version 1 still kept */
	ostep = "v1 kept";
	owrite(fd, b, orec(b, 'v', 1, 0));
	oread(fd, &r, 8192);
	ohas(&r, 1, 1, 11, 5);
	ohas(&r, 2, 1, 12, 1200);

	/* one object by 's' */
	ostep = "'s'";
	owrite(fd, b, orec(b, 's', 3, 0));
	oread(fd, &r, 8192);
	if(r.nobj != 1)
		fail("'s' 3: %d objects", r.nobj);
	ohas(&r, 3, 1, 13, 0);

	/* uncommitted: unseen, by this fid and another; then aborted */
	ostep = "uncommitted";
	if((fd2 = open(f, OREAD)) < 0)
		fail("open: %r");
	oput(fd, 9, 99, 3000);
	oread(fd2, &r, 8192);
	ohas(&r, 9, 0, 0, 0);
	oread(fd, &r, 8192);
	ohas(&r, 9, 0, 0, 0);
	b[0] = 'a';
	owrite(fd, b, b+1);
	oput(fd, 4, 44, 4);
	ocommit(fd, 0);
	oread(fd2, &r, 8192);
	if(r.n != 3)
		fail("after the abort: N %llud, not 3", r.n);
	ohas(&r, 9, 0, 0, 0);
	ohas(&r, 4, 3, 44, 4);

	/* a second writer: busy; a clunked transaction: aborted */
	ostep = "busy, clunk";
	close(fd2);
	if((fd2 = open(f, ORDWR)) < 0)
		fail("open: %r");
	oput(fd, 5, 55, 5);
	p = orec(obuf, 'p', 6, 1);
	*p++ = 'x';
	if(write(fd2, obuf, p-obuf) >= 0)
		fail("two transactions in a store");
	close(fd);
	sleep(200);	/* the sweeper's AOobjabort */
	oput(fd2, 6, 66, 6);
	ocommit(fd2, 1);
	oread(fd2, &r, 8192);
	if(r.n != 4)
		fail("after the clunk: N %llud, not 4", r.n);
	ohas(&r, 5, 0, 0, 0);
	ohas(&r, 6, 4, 66, 6);
	fd = fd2;

	/* versions K..N kept: 3 */
	ostep = "K..N";
	for(i = 0; i < 3; i++){
		oput(fd, 2, 100+i, 100+i);
		ocommit(fd, 0);
	}
	oread(fd, &r, 8192);
	if(r.n != 7 || r.k != 5)
		fail("N %llud K %llud, not 7 5", r.n, r.k);
	ohas(&r, 2, 7, 102, 102);
	owrite(fd, b, orec(b, 'v', 5, 0));
	oread(fd, &r, 8192);
	ohas(&r, 2, 5, 100, 100);
	owrite(fd, b, orec(b, 'v', 4, 0));
	if(pread(fd, obuf, 8192, 0) >= 0)
		fail("version 4 read, K is 5");

	/* an unknown record: an error, the transaction out */
	ostep = "bad record";
	oput(fd, 7, 77, 7);
	b[0] = 'z';
	if(write(fd, b, 1) >= 0)
		fail("record 'z' taken");
	oread(fd, &r, 8192);
	ohas(&r, 7, 0, 0, 0);

	/* many objects, and one of 100 KB */
	ostep = "many";
	for(i = 0; i < 40; i++)
		oput(fd, 1000+i, i, 50*i);
	oput(fd, 2000, 7, 100*1024);
	ocommit(fd, 1);
	oread(fd, &r, 8192);
	for(i = 0; i < 40; i++)
		ohas(&r, 1000+i, 8, i, 50*i);
	ohas(&r, 2000, 8, 7, 100*1024);

	/*
	 * V7's recovery pin (docs/v7-upgrade.md 4.2, docs/gefs-obj.md 9):
	 * the user part is 88 bytes, env[64] next[8] fmt 2 pad[7] pin[8],
	 * and K never passes the pin, nor goes back below what is pruned.
	 * Now N 8, K 6: the pin is 8.
	 */
	ostep = "pin";
	oread(fd, &r, 8192);
	if(r.n != 8)
		fail("before the pin: N %llud, not 8", r.n);
	memset(obuf, 0, 5+88);
	obuf[0] = 'h'; obuf[4] = 88;
	obuf[5+72] = 2;
	obuf[5+87] = 8;			/* the pin, big-endian at 80..87 */
	owrite(fd, obuf, obuf+5+88);
	oput(fd, 1, 900, 9);
	ocommit(fd, 0);
	oread(fd, &r, 8192);
	if(r.n != 9 || r.k != 7)
		fail("pinned: N %llud K %llud, not 9 7", r.n, r.k);
	for(i = 0; i < 3; i++){
		oput(fd, 1, 910+i, 9);
		ocommit(fd, 0);
		oread(fd, &r, 8192);
		if(r.n != 10+i || r.k != 8)
			fail("commit %d: N %llud K %llud, not %d 8", i, r.n, r.k, 10+i);
	}
	owrite(fd, b, orec(b, 'v', 8, 0));
	oread(fd, &r, 8192);
	ohas(&r, 1000, 8, 0, 0);	/* the pinned version reads */
	memset(obuf, 0, 5+88);
	obuf[0] = 'h'; obuf[4] = 88;
	obuf[5+72] = 2;			/* fmt 2, the pin 0 now */
	owrite(fd, obuf, obuf+5+88);
	oput(fd, 1, 920, 9);
	ocommit(fd, 0);
	oread(fd, &r, 8192);
	if(r.n != 13 || r.k != 11)
		fail("pin gone: N %llud K %llud, not 13 11", r.n, r.k);
	owrite(fd, b, orec(b, 'v', 8, 0));
	if(pread(fd, obuf, 8192, 0) >= 0)
		fail("version 8 read, K is 11");

	/* no truncation of a store */
	close(fd);
	if((fd = open(f, OWRITE|OTRUNC)) >= 0)
		fail("OTRUNC of a store worked");
	rerrstr(e, sizeof e);

	/* removed and made again: empty */
	ostep = "remade";
	if(remove(f) < 0)
		fail("remove: %r");
	if((fd = create(f, ORDWR, DMOBJ|0600)) < 0)
		fail("create again: %r");
	oread(fd, &r, 8192);
	if(r.n != 0 || r.nobj != 0)
		fail("store made again: N %llud, %d objects", r.n, r.nobj);
	close(fd);
	remove(f);
	free(f);
}

void
fsfill(void)
{
	int fd, i;
	vlong t;
	char e[ERRMAX];

	if((fd = create(path("fill"), OWRITE, 0664)) < 0)
		fail("create: %r");
	memset(buf, 'f', sizeof buf);
	for(t = 0; write(fd, buf, sizeof buf) == sizeof buf; t += sizeof buf)
		;
	rerrstr(e, sizeof e);
	close(fd);
	print("KTEST fsfill: %lld MB written, then: %s\n", t>>20, e);
	if(remove(path("fill")) < 0)
		fail("remove: %r");
	/* usable again */
	for(i = 0; i < 10; i++)
		writefile(path("after%d", i), i, 100000);
	for(i = 0; i < 10; i++){
		checkfile(path("after%d", i), i, 100000);
		remove(path("after%d", i));
	}
}

/*
 * memory and processes
 */
vlong
usermem(void)
{
	char s[1024], *f[64], *p;
	int fd, n, i, nf;
	vlong v;

	/* 9front /dev/swap: "N pagesize", "used/total user" (pages) */
	if((fd = open("/dev/swap", OREAD)) < 0)
		fail("/dev/swap: %r");
	n = read(fd, s, sizeof s - 1);
	close(fd);
	if(n <= 0)
		fail("read /dev/swap: %r");
	s[n] = 0;
	nf = tokenize(s, f, nelem(f));
	v = 0;
	for(i = 0; i+1 < nf; i++)
		if(strcmp(f[i+1], "user") == 0 && (p = strchr(f[i], '/')) != nil)
			v = strtoll(p+1, nil, 10) - strtoll(f[i], nil, 10);	/* free pages */
	for(i = 0; i+1 < nf; i++)
		if(strcmp(f[i+1], "pagesize") == 0)
			v *= strtoll(f[i], nil, 10);
	return v;
}

void
memalloc(void)
{
	vlong free, want, got, i;
	uchar *p[1024];
	int n, k;
	long chunk;

	free = usermem();
	want = free / 2;
	/* one segment (bss) of it: 9front's segments are at most a few GB */
	if(want > 1536LL*1024*1024)
		want = 1536LL*1024*1024;
	chunk = 16*1024*1024;
	print("KTEST memalloc: %lld MB free for users, taking %lld MB\n", free>>20, want>>20);
	n = 0;
	for(got = 0; got < want && n < nelem(p); got += chunk){
		if((p[n] = sbrk(chunk)) == (void*)-1)
			fail("sbrk after %lld MB: %r", got>>20);
		for(i = 0; i < chunk; i += 4096)
			*(uvlong*)(p[n]+i) = pat(n, i);
		n++;
	}
	for(k = 0; k < n; k++)
		for(i = 0; i < chunk; i += 4096)
			if(*(uvlong*)(p[k]+i) != pat(k, i))
				fail("page %lld of chunk %d lost", i/4096, k);
}

void
memchild(int k)
{
	uchar *p;
	long n, i;

	n = 8*1024*1024;
	if((p = malloc(n)) == nil)
		fail("malloc: %r");
	fillpat(p, n, k, 0);
	sleep(10);
	for(i = 0; i < n; i += 64*1024)
		if(checkpat(p+i, 64*1024, k, i) < 0)
			fail("child %d: memory changed", k);
	free(p);
}

void
memprocs(void)
{
	int r;

	for(r = 0; r < 4; r++)
		procs(48, memchild);
}

int
recurse(int n)
{
	volatile char frame[1024];

	frame[0] = n;
	if(n == 0)
		return 0;
	return recurse(n-1) + frame[0] - n;
}

void
memstack(void)
{
	Waitmsg *w;

	/* 12 MB of the 16 MB stack: fine */
	switch(fork()){
	case 0:
		recurse(12*1024);
		exits(nil);
	case -1:
		fail("fork: %r");
	}
	if((w = wait()) == nil || w->msg[0])
		fail("12 MB of stack: %s", w ? w->msg : "?");
	free(w);
	/* past it: a note for the child, nothing else */
	switch(fork()){
	case 0:
		recurse(40*1024);
		exits("survived");
	case -1:
		fail("fork: %r");
	}
	if((w = wait()) == nil)
		fail("wait: %r");
	if(strstr(w->msg, "survived") || w->msg[0] == 0)
		fail("40 MB of stack did not fault: %s", w->msg);
	free(w);
}

void
memseg(void)
{
	uchar *p;
	long n, i;
	Waitmsg *w;

	n = 16*1024*1024;
	if((p = segattach(0, "shared", nil, n)) == (void*)-1)
		fail("segattach: %r");
	switch(rfork(RFPROC|RFFDG)){
	case 0:
		fillpat(p, n, 77, 0);
		exits(nil);
	case -1:
		fail("fork: %r");
	}
	if((w = wait()) == nil || w->msg[0])
		fail("child: %s", w ? w->msg : "?");
	for(i = 0; i < n; i += 64*1024)
		if(checkpat(p+i, 64*1024, 77, i) < 0)
			fail("shared segment: not the child's at %ld", i);
	if(segdetach(p) < 0)
		fail("segdetach: %r");
}

void
memcow(void)
{
	uchar *p;
	long n, i;
	Waitmsg *w;

	n = 64*1024*1024;
	if((p = malloc(n)) == nil)
		fail("malloc: %r");
	fillpat(p, n, 5, 0);
	switch(fork()){
	case 0:
		fillpat(p, n, 6, 0);
		for(i = 0; i < n; i += 64*1024)
			if(checkpat(p+i, 64*1024, 6, i) < 0)
				exits("child sees the old");
		exits(nil);
	case -1:
		fail("fork: %r");
	}
	if((w = wait()) == nil || w->msg[0])
		fail("child: %s", w ? w->msg : "?");
	for(i = 0; i < n; i += 64*1024)
		if(checkpat(p+i, 64*1024, 5, i) < 0)
			fail("the child's writes in the parent at %ld", i);
	free(p);
}

void
procpipe(void)
{
	int p[2];
	vlong total, o;
	long n;
	Waitmsg *w;

	total = 128LL*1024*1024;
	if(pipe(p) < 0)
		fail("pipe: %r");
	switch(fork()){
	case 0:
		close(p[0]);
		for(o = 0; o < total; o += n){
			n = 8192 + (o/8192)%7*1000;
			if(n > total-o)
				n = total-o;
			fillpat((uchar*)buf, n, 11, o);
			if(write(p[1], buf, n) != n)
				exits("write");
		}
		exits(nil);
	case -1:
		fail("fork: %r");
	}
	close(p[1]);
	for(o = 0; (n = read(p[0], buf, sizeof buf)) > 0; o += n)
		if(checkpat((uchar*)buf, n&~7, 11, o) < 0 && (o&7) == 0)
			fail("pipe data wrong at %lld", o);
	close(p[0]);
	if(o != total)
		fail("%lld bytes through, not %lld", o, total);
	if((w = wait()) == nil || w->msg[0])
		fail("writer: %s", w ? w->msg : "?");
}

int
gotnote(void*, char *s)
{
	if(strcmp(s, "ktest") == 0)
		exits(nil);
	return 0;
}

void
procnote(void)
{
	int pid, i;
	Waitmsg *w;

	for(i = 0; i < 50; i++){
		switch(pid = fork()){
		case 0:
			atnotify(gotnote, 1);
			sleep(60000);
			exits("not interrupted");
		case -1:
			fail("fork: %r");
		}
		sleep(5);
		if(postnote(PNPROC, pid, "ktest") < 0)
			fail("postnote: %r");
		if((w = wait()) == nil || w->msg[0])
			fail("noted child: %s", w ? w->msg : "?");
		free(w);
	}
}

void
procstorm(void)
{
	int i, n;
	Waitmsg *w;

	n = 0;
	for(i = 0; i < 500*scale; i++){
		switch(rfork(RFPROC|RFFDG|RFNOTEG)){
		case 0:
			exits(nil);
		case -1:
			fail("fork %d: %r", i);
		}
		n++;
		if(n >= 100){
			while(n > 0){
				if((w = wait()) == nil)
					fail("wait: %r");
				free(w);
				n--;
			}
		}
	}
	while(n-- > 0)
		free(wait());
}

/*
 * all at once: writers, memory, pipes and forks, on all processors
 */
void
mixone(int k)
{
	switch(k%4){
	case 0:
		writefile(path("mix%d", k), k, 24*1024*1024);
		checkfile(path("mix%d", k), k, 24*1024*1024);
		remove(path("mix%d", k));
		break;
	case 1:
		memchild(k);
		memchild(k+100);
		break;
	case 2:
		procpipe();
		break;
	case 3:
		procstorm();
		concr(k);
		break;
	}
}

void
stressmix(void)
{
	mkdirp(path("conc"));
	writefile(path("conc/shared"), 42, 8*1024*1024);
	procs(16, mixone);
	remove(path("conc/shared"));
}

/*
 * what Plan2001 changed against 9front
 */
char*
readstr(char *f)
{
	static char s[1024];
	int fd, n;

	if((fd = open(f, OREAD)) < 0)
		fail("%s: %r", f);
	n = read(fd, s, sizeof s - 1);
	close(fd);
	if(n < 0)
		fail("read %s: %r", f);
	s[n] = 0;
	return s;
}

void
ktime(void)
{
	vlong t0, t1;
	long now;

	/* the RTC from UEFI (BootInfo) */
	now = time(nil);
	if(now < 1790000000)
		fail("time %ld: before 2026", now);
	/* the clock: nsec against a sleep */
	t0 = nsec();
	sleep(2000);
	t1 = nsec();
	if(t1 - t0 < 1900000000LL || t1 - t0 > 2300000000LL)
		fail("sleep(2000) took %lld ms", (t1-t0)/1000000);
}

void
krand(void)
{
	uchar a[4096], b[4096];
	int cnt[256], i, fd;

	/* the EFI RNG seed mixed into the kernel's (BootInfo) */
	if((fd = open("/dev/random", OREAD)) < 0)
		fail("/dev/random: %r");
	if(readn(fd, a, sizeof a) != sizeof a || readn(fd, b, sizeof b) != sizeof b)
		fail("read: %r");
	close(fd);
	if(memcmp(a, b, sizeof a) == 0)
		fail("the same twice");
	memset(cnt, 0, sizeof cnt);
	for(i = 0; i < sizeof a; i++)
		cnt[a[i]]++;
	for(i = 0; i < 256; i++)
		if(cnt[i] > 64)
			fail("byte %d %d times in 4096", i, cnt[i]);
}

void
kenv(void)
{
	char *s;

	/* rootmount: the host owner from user= */
	if(strcmp(readstr("/dev/hostowner"), "glenda") != 0)
		fail("hostowner %s", readstr("/dev/hostowner"));
	if(strcmp(readstr("/dev/user"), "glenda") != 0)
		fail("user %s", readstr("/dev/user"));
	s = getenv("cputype");
	if(s == nil || strcmp(s, "amd64") != 0)
		fail("cputype %s", s);
}

void
kroot(void)
{
	Dir *d;

	/* the root before #/: its /rc, not #/'s empty one */
	if(access("/rc/lib/rcmain", AREAD) < 0)
		fail("/rc/lib/rcmain: %r");
	if(access("/init", AEXEC) < 0)
		fail("/init: %r");
	/* #/'s mount points it lacks */
	if((d = dirstat("/proc")) == nil)
		fail("/proc: %r");
	free(d);
	/* the kernel's gefs: its 9P and its console, for fshalt */
	if(access("#s/gefs.root", AEXIST) < 0)
		fail("#s/gefs.root: %r");
	if(access("#s/gefs.root.cmd", AEXIST) < 0)
		fail("#s/gefs.root.cmd: %r");
}

void
kwalk(void)
{
	char *p, *e;
	int i, fd;

	/* 600 names: ".." and back, on the root, one open */
	p = malloc(16384);
	e = seprint(p, p+16384, "%s", dir);
	for(i = 0; i < 300; i++)
		e = seprint(e, p+16384, "/../%s", strrchr(dir, '/')+1);
	if((fd = open(p, OREAD)) < 0)
		fail("open of 600 names: %r");
	close(fd);
	/*
	 * a union of 30 bound in turn on /mnt, first replaced by an empty
	 * directory of ours (mntgen there makes any name a directory)
	 */
	if(rfork(RFNAMEG) < 0)
		fail("rfork: %r");
	mkdirp(path("union"));
	if(bind(path("union"), "/mnt", MREPL) < 0)
		fail("bind on /mnt: %r");
	for(i = 0; i < 30; i++)
		if(bind("/amd64/bin", "/mnt", MAFTER) < 0)
			fail("bind %d: %r", i);
	if(access("/mnt/rc", AEXEC) < 0)
		fail("rc in the union: %r");
	free(p);
}

Test tests[] = {
	"fsbig",	"fs",	fsbig,
	"fsbigio",	"fs",	fsbigio,
	"fsmany",	"fs",	fsmany,
	"fsdeep",	"fs",	fsdeep,
	"fsrename",	"fs",	fsrename,
	"fsrenopen",	"fs",	fsrenopen,
	"fsconc",	"fs",	fsconc,
	"fssparse",	"fs",	fssparse,
	"fsappend",	"fs",	fsappend,
	"fstrunc",	"fs",	fstrunc,
	"fswstat",	"fs",	fswstat,
	"fsexec",	"fs",	fsexec,
	"fsperm",	"fs",	fsperm,
	"fsstale",	"fs",	fsstale,
	"fsgrow",	"fs",	fsgrow,
	"fssync",	"fs",	fssync,
	"fsobj",	"fs",	fsobj,
	"fsfill",	"fill",	fsfill,
	"memalloc",	"mem",	memalloc,
	"memprocs",	"mem",	memprocs,
	"memstack",	"mem",	memstack,
	"memseg",	"mem",	memseg,
	"memcow",	"mem",	memcow,
	"procpipe",	"proc",	procpipe,
	"procnote",	"proc",	procnote,
	"procstorm",	"proc",	procstorm,
	"stressmix",	"mix",	stressmix,
	"ktime",	"k",	ktime,
	"krand",	"k",	krand,
	"kenv",	"k",	kenv,
	"kroot",	"k",	kroot,
	"kwalk",	"k",	kwalk,
};

int
wanted(Test *t, int argc, char **argv)
{
	int i;

	if(argc == 0)
		return strcmp(t->group, "fill") != 0;
	for(i = 0; i < argc; i++)
		if(strcmp(argv[i], t->name) == 0 || strcmp(argv[i], t->group) == 0
		|| strcmp(argv[i], "all") == 0 && strcmp(t->group, "fill") != 0)
			return 1;
	return 0;
}

int
alarmed(void*, char *s)
{
	if(strcmp(s, "alarm") == 0)
		exits("timed out");
	return 0;
}

void
usage(void)
{
	fprint(2, "usage: ktest [-u] [-d dir] [-s scale] [-t secs] [all|fs|mem|proc|mix|k|fill|test...]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	Test *t;
	Waitmsg *w;
	Dir *d;
	int nok, nfail;
	vlong t0;

	ARGBEGIN{
	case 'u':
		anyfs = 1;
		break;
	case 'd':
		dir = EARGF(usage());
		break;
	case 's':
		scale = atoi(EARGF(usage()));
		break;
	case 't':
		secs = atoi(EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND

	rmtree(dir);
	mkdirp(dir);
	/* the file system tests are the kernel's gefs' (a mount of its pipe) */
	if((d = dirstat(dir)) == nil)
		sysfatal("%s: %r", dir);
	if(!anyfs && (d->type != 'M' || access("#s/gefs.root", AEXIST) < 0))
		sysfatal("%s is not on the kernel's gefs but #%C", dir, d->type);
	free(d);
	nok = nfail = 0;
	for(t = tests; t < tests+nelem(tests); t++){
		if(!wanted(t, argc, argv))
			continue;
		t0 = nsec();
		switch(rfork(RFPROC|RFFDG|RFNOTEG)){
		case -1:
			sysfatal("fork: %r");
		case 0:
			atnotify(alarmed, 1);
			alarm(secs*1000);
			(*t->fn)();
			exits(nil);
		}
		if((w = wait()) == nil)
			sysfatal("wait: %r");
		if(w->msg[0] == 0){
			print("KTEST %s OK %lldms\n", t->name, (nsec()-t0)/1000000);
			nok++;
		}else{
			print("KTEST %s FAIL %s\n", t->name, w->msg);
			nfail++;
		}
		free(w);
		/* what a failed one left does not fill the disk for the next */
		rmtree(dir);
		mkdirp(dir);
	}
	rmtree(dir);
	print("KTEST-DONE ok %d fail %d\n", nok, nfail);
	exits(nfail ? "fail" : nil);
}
