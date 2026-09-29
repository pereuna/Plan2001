/*
 * rcc - 6c in the compute pool (crsrv, docs/cpu-server-design.md):
 * rcc [6c options] file.c ... compiles each file on a compute resource
 * and writes its object as 6c would, whole or not at all (NAME.tmp, then
 * renamed).  The job carries the source and the headers of its own
 * directory, the current one and the -I ones; the CR has /sys/include and
 * /$objtype/include from crsrv, and compiles in a directory of the same
 * name as this one, so the object is the same as 6c's here.  With no
 * CR in /compute - the pool as the process's namespace has it (its app's
 * namespace file, docs/app-origins.md): none there, none to use - rcc
 * runs 6c.
 *	NPROC=8 mk 'CC=rcc'
 */
#include <u.h>
#include <libc.h>

static char *opts[64];	/* 6c's options, as given */
static int nopts;
static char *incs[16];	/* -I */
static int nincs;
static char *outname;	/* -o */
static char *objtype;

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

static void*
erealloc(void *p, ulong n)
{
	if((p = realloc(p, n)) == nil)
		sysfatal("out of memory");
	return p;
}

static void
addfile(uchar **b, long *n, char *name, uchar *data, long len)
{
	long l;

	l = strlen(name) + 1;
	*b = erealloc(*b, *n + l + 4 + len);
	memmove(*b + *n, name, l);
	put32(*b + *n + l, len);
	memmove(*b + *n + l + 4, data, len);
	*n += l + 4 + len;
}

static uchar*
readfile(char *name, long *np)
{
	uchar *b;
	long n, m;
	int fd;

	if((fd = open(name, OREAD)) < 0)
		return nil;
	b = nil;
	n = 0;
	for(;;){
		b = erealloc(b, n + 8192);
		if((m = read(fd, b+n, 8192)) <= 0)
			break;
		n += m;
	}
	close(fd);
	*np = n;
	return b;
}

/* the *.h files of dir (absolute) into the body, once each */
static char *added[256];
static int nadded;

static void
addheaders(uchar **b, long *n, char *dir)
{
	Dir *d;
	long i, nd, len, l;
	char *p;
	uchar *data;
	int fd;

	for(i = 0; i < nadded; i++)
		if(strcmp(added[i], dir) == 0)
			return;
	if(nadded < nelem(added))
		added[nadded++] = strdup(dir);
	if((fd = open(dir, OREAD)) < 0)
		return;
	nd = dirreadall(fd, &d);
	close(fd);
	for(i = 0; i < nd; i++){
		l = strlen(d[i].name);
		if((d[i].mode & DMDIR) || l < 3 || strcmp(d[i].name+l-2, ".h") != 0)
			continue;
		p = smprint("%s/%s", dir, d[i].name);
		if((data = readfile(p, &len)) != nil){
			addfile(b, n, p, data, len);
			free(data);
		}
		free(p);
	}
	free(d);
}

static char*
absolute(char *cwd, char *p)
{
	if(p[0] == '/')
		return strdup(p);
	return cleanname(smprint("%s/%s", cwd, p));
}

static int
pooled(void)
{
	char buf[128], *f[2];
	int fd, n;

	if((fd = open("/compute/status", OREAD)) < 0)
		return 0;
	n = read(fd, buf, sizeof buf - 1);
	close(fd);
	if(n <= 0)
		return 0;
	buf[n] = 0;
	return tokenize(buf, f, 2) == 2 && strcmp(f[0], "crs") == 0 && atoi(f[1]) > 0;
}

/* 6c here, for all the arguments */
static void
local(char **argv)
{
	char *cc;

	cc = smprint("/bin/%sc", strcmp(objtype, "amd64") == 0 ? "6" : "?");
	argv[0] = "6c";
	exec(cc, argv);
	sysfatal("exec %s: %r", cc);
}

/* the job for src; 0 if its object is written */
static int
job(char *cwd, char *src)
{
	uchar *b, *msg, *res, *data, *p, *e;
	long n, nres, len, l, m;
	char *out, *hdr, *tmp, *s, *dir, *name;
	int fd, i, ok, iounit;
	Dir nd;

	out = outname;
	if(out == nil){
		s = strrchr(src, '/');
		s = strdup(s != nil ? s+1 : src);
		if((l = strlen(s)) > 2 && strcmp(s+l-2, ".c") == 0)
			s[l-2] = 0;
		out = smprint("%s.6", s);
	}
	out = absolute(cwd, out);

	/* "cc CWD ARG...": 6c's options, -o the object, the source as given */
	hdr = smprint("cc\t%s", cwd);
	for(i = 0; i < nopts; i++){
		s = smprint("%s\t%s", hdr, opts[i]);
		free(hdr);
		hdr = s;
	}
	s = smprint("%s\t-o\t%s\t%s\n", hdr, out, src);
	free(hdr);
	hdr = s;

	b = nil;
	n = 0;
	name = absolute(cwd, src);
	if((data = readfile(name, &len)) == nil){
		fprint(2, "rcc: %s: %r\n", src);
		return -1;
	}
	addfile(&b, &n, name, data, len);
	free(data);
	dir = strdup(name);
	*strrchr(dir, '/') = 0;
	nadded = 0;
	addheaders(&b, &n, dir);
	addheaders(&b, &n, cwd);
	for(i = 0; i < nincs; i++)
		addheaders(&b, &n, absolute(cwd, incs[i]));

	l = strlen(hdr);
	msg = erealloc(nil, 4 + l + n);
	put32(msg, l + n);
	memmove(msg+4, hdr, l);
	memmove(msg+4+l, b, n);
	free(b);

	if((fd = open("/compute/cc", ORDWR)) < 0){
		fprint(2, "rcc: /compute/cc: %r\n");
		return -1;
	}
	iounit = 8192;
	for(p = msg, e = msg+4+l+n; p < e; p += m){
		m = e - p;
		if(m > iounit)
			m = iounit;
		if(write(fd, p, m) != m){
			fprint(2, "rcc: writing the job: %r\n");
			return -1;
		}
	}
	free(msg);
	seek(fd, 0, 0);	/* the result is read from its start, not after the job */
	res = nil;
	nres = 0;
	for(;;){
		res = erealloc(res, nres + 8192);
		if((m = read(fd, res+nres, 8192)) <= 0)
			break;
		nres += m;
	}
	close(fd);
	if(m < 0 || nres < 3){
		fprint(2, "rcc: the result (%ld bytes, read %ld): %r\n", nres, m);
		return -1;
	}

	/* "ok\n" or "fail\n", then the files: log, the object */
	ok = memcmp(res, "ok\n", 3) == 0;
	p = (uchar*)memchr(res, '\n', nres) + 1;
	e = res + nres;
	while(p < e){
		s = (char*)p;
		if((p = memchr(p, 0, e-p)) == nil)
			break;
		p++;
		if(e - p < 4)
			break;
		len = get32(p);
		p += 4;
		if(len > e - p)
			break;
		if(strcmp(s, "log") == 0)
			write(1, p, len);
		else if(ok && strcmp(s, out) == 0){
			/* whole or not at all: NAME.tmp, renamed */
			tmp = smprint("%s.tmp", out);
			if((fd = create(tmp, OWRITE, 0664)) < 0 || write(fd, p, len) != len){
				fprint(2, "rcc: %s: %r\n", tmp);
				remove(tmp);
				return -1;
			}
			close(fd);
			nulldir(&nd);
			nd.name = strrchr(out, '/') + 1;
			remove(out);
			if(dirwstat(tmp, &nd) < 0){
				fprint(2, "rcc: rename %s: %r\n", tmp);
				remove(tmp);
				return -1;
			}
			free(tmp);
		}
		p += len;
	}
	free(res);
	return ok ? 0 : -1;
}

void
main(int argc, char **argv)
{
	char cwd[512], **av;
	int i, bad, nsrc;

	av = argv;
	if((objtype = getenv("objtype")) == nil)
		objtype = "amd64";
	argv++, argc--;
	for(; argc > 0 && argv[0][0] == '-'; argv++, argc--){
		if(strcmp(argv[0], "-o") == 0 || strcmp(argv[0], "-I") == 0 || strcmp(argv[0], "-D") == 0){
			if(argc < 2)
				sysfatal("usage: rcc [6c options] file.c ...");
			if(argv[0][1] == 'o')
				outname = argv[1];
			else{
				if(argv[0][1] == 'I' && nincs < nelem(incs))
					incs[nincs++] = argv[1];
				if(nopts+2 > nelem(opts))
					sysfatal("too many options");
				opts[nopts++] = argv[0];
				opts[nopts++] = argv[1];
			}
			argv++, argc--;
			continue;
		}
		if(argv[0][1] == 'I' && nincs < nelem(incs))
			incs[nincs++] = argv[0]+2;
		if(argv[0][1] == 'o' && argv[0][2] != 0){
			outname = argv[0]+2;
			continue;
		}
		if(nopts >= nelem(opts))
			sysfatal("too many options");
		opts[nopts++] = argv[0];
	}
	nsrc = argc;
	if(nsrc == 0 || (outname != nil && nsrc > 1) || strcmp(objtype, "amd64") != 0 || !pooled())
		local(av);
	if(getwd(cwd, sizeof cwd) == nil)
		sysfatal("getwd: %r");
	bad = 0;
	for(i = 0; i < nsrc; i++)
		if(job(cwd, argv[i]) < 0)
			bad++;
	exits(bad ? "errors" : nil);
}
