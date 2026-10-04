/*
 * passkeyd - a passkey's wrap (docs/webauthn.md), to whoever shows the
 * passkey: lines from the machine's auth/passkey login, through webterm's
 * /17041 (policy word login), on fd 0, the answers on fd 1; aux/listen's
 * service.auth/tcp17041, as the host owner.  One connection:
 *
 *	challenge			->	ok CHALLENGE
 *	wrap CREDID AUTH CLIENT SIG	->	ok NAME WRAP | error WHY
 *
 * CHALLENGE is 32 random bytes, this connection's.  AUTH, CLIENT and SIG
 * are the passkey's assertion for it (authenticatorData, clientDataJSON,
 * signature; base64url): clientDataJSON's type webauthn.get, its
 * challenge this one and its origin one of -o's, not cross-origin;
 * authenticatorData's rpIdHash sha256 of -r, its user present, its
 * counter above the one kept (if either is not 0); the signature
 * (ES256, the public key signupd kept) over authenticatorData and
 * sha256(clientDataJSON).  Then the wrap - the name and the password
 * encrypted with the passkey's PRF output - and the counter kept.
 *
 *	aux/passkeyd -r rpid -o origin [-o origin ...] [-w webauthn]
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mp.h>
#include <libsec.h>

enum {
	Nline	= 16*1024,
	Nchal	= 32,
};

static char *wadir = "/adm/webauthn";
static char *rp;
static char *origins[16];
static int norigins;

static void
reply(char *fmt, ...)
{
	char buf[4096];
	va_list arg;

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	fprint(1, "%s\n", buf);
}

static void
fail(char *why)
{
	reply("error %s", why);
	if(access("/sys/log/passkeyd", AEXIST) == 0)	/* its log, if the machine keeps one (not a test's) */
	syslog(0, "passkeyd", "%s", why);
	exits(why);
}

/* a line from the connection: nil at its end */
static char*
line(void)
{
	static char buf[Nline];
	static long n;
	char *p, *l;
	long m;

	for(;;){
		if((p = memchr(buf, '\n', n)) != nil){
			*p = 0;
			l = strdup(buf);
			n -= p+1 - buf;
			memmove(buf, p+1, n);
			return l;
		}
		if(n >= sizeof buf - 1 || (m = read(0, buf+n, sizeof buf - 1 - n)) <= 0)
			return nil;
		n += m;
	}
}

static int
unb64u(char *s, uchar *p, int n)
{
	char *t, *u;
	int m;

	m = strlen(s);
	if(strspn(s, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") != m)
		return -1;
	t = malloc(m + 4);
	strcpy(t, s);
	for(u = t; *u; u++)
		if(*u == '-')
			*u = '+';
		else if(*u == '_')
			*u = '/';
	while(m % 4)
		t[m++] = '=';
	t[m] = 0;
	m = dec64(p, n, t, m);
	free(t);
	return m;
}

static char*
b64u(uchar *p, int n)
{
	char *s, *t;

	s = malloc(2*n + 8);
	enc64(s, 2*n + 8, p, n);
	for(t = s; *t; t++)
		if(*t == '+')
			*t = '-';
		else if(*t == '/')
			*t = '_';
		else if(*t == '='){
			*t = 0;
			break;
		}
	return s;
}

/* clientDataJSON's "key":"value" (the browsers' serialization: no spaces, values without escapes we take) */
static char*
jsonstr(char *json, char *key)
{
	char *k, *p, *e;

	k = smprint("\"%s\":\"", key);
	p = strstr(json, k);
	if(p == nil){
		free(k);
		return nil;
	}
	p += strlen(k);
	free(k);
	if((e = strchr(p, '"')) == nil)
		return nil;
	return smprint("%.*s", (int)(e-p), p);
}

typedef struct Rec Rec;
struct Rec
{
	char	*name;
	char	*pubkey;
	char	*wrap;
	ulong	count;
	char	*rest;	/* its other lines, kept */
};

static Rec*
readrec(char *path)
{
	Biobuf *b;
	char *l, *o, *s, *v;
	Rec *r;

	if((b = Bopen(path, OREAD)) == nil)
		return nil;
	r = mallocz(sizeof *r, 1);
	r->rest = strdup("");
	while((l = Brdstr(b, '\n', 1)) != nil){
		/* KEY, a tab or blanks, VALUE (a hand-edited record has blanks) */
		o = strdup(l);
		v = l + strcspn(l, " \t");
		if(*v != 0)
			*v++ = 0;
		v += strspn(v, " \t");
		if(strcmp(l, "name") == 0)
			r->name = strdup(v);
		else if(strcmp(l, "pubkey") == 0)
			r->pubkey = strdup(v);
		else if(strcmp(l, "wrap") == 0)
			r->wrap = strdup(v);
		else if(strcmp(l, "count") == 0)
			r->count = strtoul(v, nil, 10);
		else{
			s = smprint("%s%s\n", r->rest, o);	/* kept as it was */
			free(r->rest);
			r->rest = s;
		}
		free(o);
		free(l);
	}
	Bterm(b);
	if(r->name == nil || r->pubkey == nil || r->wrap == nil)
		return nil;
	return r;
}

static int
writerec(char *path, Rec *r)
{
	char *s;
	int fd, n;

	s = smprint("name\t%s\npubkey\t%s\nwrap\t%s\ncount\t%lud\n%s", r->name, r->pubkey, r->wrap, r->count, r->rest);
	n = strlen(s);
	if((fd = open(path, OWRITE|OTRUNC)) < 0)
		return -1;
	if(write(fd, s, n) != n){
		close(fd);
		return -1;
	}
	return close(fd);
}

/* the assertion's signature, ES256 with the SPKI key (P-256: its last 65 bytes the point) */
static int
verify(char *spki, uchar *auth, int nauth, uchar *client, int nclient, uchar *sig, int nsig)
{
	uchar key[256], h[SHA2_256dlen], d[SHA2_256dlen];
	ECdomain dom;
	ECpub *pub;
	DigestState *ds;
	char *e;
	int n;

	n = unb64u(spki, key, sizeof key);
	if(n != 91 || key[26] != 4)
		return -1;
	ecdominit(&dom, secp256r1);
	if((pub = ecdecodepub(&dom, key+26, 65)) == nil){
		ecdomfree(&dom);
		return -1;
	}
	sha2_256(client, nclient, h, nil);
	ds = sha2_256(auth, nauth, nil, nil);
	sha2_256(h, sizeof h, d, ds);
	e = X509ecdsaverifydigest(sig, nsig, d, sizeof d, &dom, pub);
	ecpubfree(pub);
	ecdomfree(&dom);
	return e == nil ? 0 : -1;
}

void
main(int argc, char **argv)
{
	char *l, *f[6], *path, *s;
	uchar chal[Nchal], auth[1024], client[4096], sig[256], h[SHA2_256dlen];
	int nauth, nclient, nsig, i, n;
	ulong count;
	Rec *r;

	ARGBEGIN{
	case 'w':
		wadir = EARGF(sysfatal("usage"));
		break;
	case 'r':
		rp = EARGF(sysfatal("usage"));
		break;
	case 'o':
		if(norigins < nelem(origins))
			origins[norigins++] = EARGF(sysfatal("usage"));
		break;
	}ARGEND
	if(rp == nil || norigins == 0)
		sysfatal("usage: aux/passkeyd -r rpid -o origin ... [-w webauthn]");

	if((l = line()) == nil || strcmp(l, "challenge") != 0)
		fail("bad request");
	genrandom(chal, sizeof chal);
	reply("ok %s", b64u(chal, sizeof chal));

	if((l = line()) == nil || tokenize(l, f, nelem(f)) != 5 || strcmp(f[0], "wrap") != 0)
		fail("bad request");
	n = strlen(f[1]);
	if(n < 16 || n > 1024 || strspn(f[1], "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") != n)
		fail("no such passkey");
	path = smprint("%s/%s", wadir, f[1]);
	if((r = readrec(path)) == nil)
		fail("no such passkey");
	nauth = unb64u(f[2], auth, sizeof auth);
	nclient = unb64u(f[3], client, sizeof client - 1);
	nsig = unb64u(f[4], sig, sizeof sig);
	if(nauth < 37 || nclient <= 0 || nsig <= 0)
		fail("bad assertion");
	client[nclient] = 0;

	/* clientDataJSON: what was asked, from where */
	if((s = jsonstr((char*)client, "type")) == nil || strcmp(s, "webauthn.get") != 0)
		fail("not an assertion");
	if((s = jsonstr((char*)client, "challenge")) == nil || strcmp(s, b64u(chal, sizeof chal)) != 0)
		fail("not this challenge");
	if(strstr((char*)client, "\"crossOrigin\":true") != nil)
		fail("cross-origin");
	if((s = jsonstr((char*)client, "origin")) == nil)
		fail("no origin");
	for(i = 0; i < norigins; i++)
		if(strcmp(s, origins[i]) == 0)
			break;
	if(i == norigins)
		fail("not this site's origin");

	/* authenticatorData: rpIdHash[32] flags[1] counter[4] */
	sha2_256((uchar*)rp, strlen(rp), h, nil);
	if(memcmp(auth, h, sizeof h) != 0)
		fail("not this site's passkey");
	if((auth[32] & 0x01) == 0)
		fail("no user present");
	count = (ulong)auth[33]<<24 | auth[34]<<16 | auth[35]<<8 | auth[36];
	if((count != 0 || r->count != 0) && count <= r->count)
		fail("counter not above the last: a copied passkey?");

	if(verify(r->pubkey, auth, nauth, client, nclient, sig, nsig) < 0)
		fail("bad signature");
	r->count = count;
	if(writerec(path, r) < 0)
		fail("can not keep the counter");
	reply("ok %s %s", r->name, r->wrap);
	exits(nil);
}
