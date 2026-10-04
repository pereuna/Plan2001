/*
 * signupd - a Plan2001 account (docs/webauthn.md): a line from the machine's
 * auth/passkey signup, through webterm's /17040 (policy word signup), on
 * fd 0, its answer (ok, or error WHY) on fd 1; aux/listen's
 * service.auth/tcp17040, as the host owner (keyfs, /adm).
 *
 *	signup NAME AESKEY SECHI SECFILE CREDID PUBKEY WRAP [INVITE]
 *
 * None of it is the password: AESKEY the auth server's key (hex, the
 * password's passtokey), SECHI secstore's verifier (PAK's Hi, base64),
 * SECFILE secstore's factotum file (base64, encrypted with the password
 * by the client), CREDID the passkey's id, PUBKEY its public key (SPKI)
 * and WRAP the name and password encrypted with its PRF output (base64url).
 * The account: a user in keyfs (its aeskey), a secstore account (who/NAME,
 * store/NAME/factotum), the passkey (webauthn/CREDID: whose, its key and
 * wrap) and, with -c, a home on the file server (newuser on its console;
 * ok once usr/NAME, which it makes, is there).
 * With an invites file one of its lines is needed, and used up.  What it
 * had made is undone if a later step fails.
 *
 *	aux/signupd [-k keys] [-s secstore] [-w webauthn] [-c fscons [-u usr]] [-i invites]
 *
 * defaults /mnt/keys, /adm/secstore, /adm/webauthn, none, webauthn/invites
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <authsrv.h>

enum {
	Nline	= 16*1024,
};

static char *keys = "/mnt/keys";
static char *secdir = "/adm/secstore";
static char *wadir = "/adm/webauthn";
static char *fscons;
static char *usrdir = "/usr";
static char *invites;
static char *reserved[] = {
	"glenda", "bootes", "adm", "sys", "none", "upas", "nobody", "root",
	"admin", "secstore", "webauthn", "cpu", "auth", "fs", "plan2001", nil,
};

static void
answer(char *fmt, ...)
{
	char buf[256];
	va_list arg;

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	fprint(1, "%s\n", buf);
	if(access("/sys/log/signupd", AEXIST) == 0)	/* its log, if the machine keeps one (not a test's) */
	syslog(0, "signupd", "%s", buf);
	exits(buf[0] == 'o' ? nil : buf);
}

static int
validname(char *s)
{
	char **r;
	int n;

	n = strlen(s);
	if(n < 2 || n > 27 || s[0] < 'a' || s[0] > 'z' || strspn(s, "abcdefghijklmnopqrstuvwxyz0123456789") != n)
		return 0;
	for(r = reserved; *r != nil; r++)
		if(strcmp(*r, s) == 0)
			return 0;
	return 1;
}

/* base64 (secstore's), base64url (WebAuthn's), hex: only their characters */
static int
charset(char *s, char *set, int min, int max)
{
	int n;

	n = strlen(s);
	return n >= min && n <= max && strspn(s, set) == n;
}

#define B64	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/="
#define B64U	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"

static int
exists(char *fmt, ...)
{
	char *p;
	va_list arg;
	Dir *d;

	va_start(arg, fmt);
	p = vsmprint(fmt, arg);
	va_end(arg);
	d = dirstat(p);
	free(p);
	free(d);
	return d != nil;
}

/* the file written whole, made if it is not there (keyfs's are, a directory's are not) */
static int
put(char *path, void *a, long n, int perm)
{
	int fd;

	if((fd = open(path, OWRITE|OTRUNC)) < 0 && (fd = create(path, OWRITE, perm)) < 0)
		return -1;
	if(write(fd, a, n) != n){
		close(fd);
		return -1;
	}
	return close(fd);
}

static int
mkdir(char *path, int perm)
{
	int fd;

	if((fd = create(path, OREAD, DMDIR|perm)) < 0)
		return -1;
	return close(fd);
}

/* an invite, used up: its line out of the file */
static int
invite(char *code)
{
	Biobuf *b;
	char *l, *rest, *s;
	int found, n;

	if(invites == nil || !exists("%s", invites))
		return 0;
	if(code == nil || !charset(code, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-", 4, 64))
		return -1;
	if((b = Bopen(invites, OREAD)) == nil)
		return -1;
	rest = mallocz(1, 1);
	found = 0;
	while((l = Brdstr(b, '\n', 1)) != nil){
		if(!found && strcmp(l, code) == 0)
			found = 1;
		else{
			s = smprint("%s%s\n", rest, l);
			free(rest);
			rest = s;
		}
		free(l);
	}
	Bterm(b);
	n = strlen(rest);
	if(found && put(invites, rest, n, 0600) < 0)
		found = 0;
	free(rest);
	return found ? 0 : -1;
}

/* what was made, undone */
static char *made[8];
static int nmade;

static void
undo(void)
{
	while(nmade > 0)
		remove(made[--nmade]);
}

static void
fail(char *why)
{
	undo();
	answer("error %s", why);
}

void
main(int argc, char **argv)
{
	char line[Nline], *f[10], *name, *p, *q;
	uchar aes[AESKEYLEN];
	long n, m;
	int nf, nsec;
	uchar *sec;

	ARGBEGIN{
	case 'k':
		keys = EARGF(sysfatal("usage"));
		break;
	case 's':
		secdir = EARGF(sysfatal("usage"));
		break;
	case 'w':
		wadir = EARGF(sysfatal("usage"));
		break;
	case 'c':
		fscons = EARGF(sysfatal("usage"));
		break;
	case 'i':
		invites = EARGF(sysfatal("usage"));
		break;
	case 'u':
		usrdir = EARGF(sysfatal("usage"));
		break;
	}ARGEND
	if(invites == nil)
		invites = smprint("%s/invites", wadir);

	/* a line, at most Nline, from the connection */
	for(n = 0; n < Nline-1 && (m = read(0, line+n, Nline-1-n)) > 0; n += m)
		if(memchr(line+n, '\n', m) != nil){
			n += m;
			break;
		}
	line[n] = 0;
	if((p = strchr(line, '\n')) == nil)
		answer("error no line");
	*p = 0;
	nf = tokenize(line, f, nelem(f));
	if((nf != 8 && nf != 9) || strcmp(f[0], "signup") != 0)
		answer("error bad request");
	name = f[1];
	if(!validname(name))
		answer("error bad name");
	if(!charset(f[2], "0123456789abcdefABCDEF", 2*AESKEYLEN, 2*AESKEYLEN) || dec16(aes, sizeof aes, f[2], 2*AESKEYLEN) != AESKEYLEN)
		answer("error bad key");
	if(!charset(f[3], B64, 16, 1024) || !charset(f[4], B64, 16, 8192)
	|| !charset(f[5], B64U, 16, 1024) || !charset(f[6], B64U, 16, 1024) || !charset(f[7], B64U, 16, 2048))
		answer("error bad request");
	if(exists("%s/%s", keys, name) || exists("%s/who/%s", secdir, name) || exists("%s/%s", wadir, f[5]))
		answer("error name taken");
	sec = malloc(strlen(f[4]));
	if(sec == nil || (nsec = dec64(sec, strlen(f[4]), f[4], strlen(f[4]))) < 32)
		answer("error bad request");
	if(invite(nf == 9 ? f[8] : nil) < 0)
		answer("error invite needed");

	/* the auth server's user (keyfs: the directory makes it) */
	p = smprint("%s/%s", keys, name);
	if(mkdir(p, 0700) < 0)
		fail("keyfs");
	made[nmade++] = p;
	q = smprint("%s/aeskey", p);
	if(put(q, aes, AESKEYLEN, 0600) < 0)
		fail("keyfs aeskey");
	made[nmade++] = q;	/* undone before its directory (keyfs: the user's whole) */
	memset(aes, 0, sizeof aes);

	/* secstore's account and its factotum file */
	p = smprint("%s/who/%s", secdir, name);
	q = smprint("exp\t0\nPAK-Hi\t%s\n", f[3]);
	if(put(p, q, strlen(q), 0600) < 0)
		fail("secstore account");
	made[nmade++] = p;
	p = smprint("%s/store/%s", secdir, name);
	if(mkdir(p, 0700) < 0)
		fail("secstore store");
	q = smprint("%s/factotum", p);
	if(put(q, sec, nsec, 0600) < 0){
		remove(p);
		fail("secstore file");
	}
	made[nmade++] = p;
	made[nmade++] = q;

	/* the passkey */
	p = smprint("%s/%s", wadir, f[5]);
	q = smprint("name\t%s\npubkey\t%s\nwrap\t%s\ncreated\t%ld\n", name, f[6], f[7], time(0));
	if(put(p, q, strlen(q), 0600) < 0)
		fail("passkey");
	made[nmade++] = p;

	/* a home on the file server */
	if(fscons != nil && *fscons){
		/* a line: the file server's console reads its commands a line at a time (cwfs's Brdline) */
		q = smprint("newuser %s\n", name);
		if((nf = open(fscons, OWRITE)) < 0 || write(nf, q, strlen(q)) != strlen(q))
			fail("newuser");
		close(nf);
		/* the console says nothing back: done when its home is there (cwfs's newuser makes /usr/NAME) */
		for(nf = 0; nf < 100 && !exists("%s/%s", usrdir, name); nf++)
			sleep(200);
		if(nf == 100)
			fail("newuser: no home made");
	}
	answer("ok");
}
