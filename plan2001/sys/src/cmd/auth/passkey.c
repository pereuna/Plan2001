/*
 * passkey - a Plan2001 account and its passkey (docs/webauthn.md):
 * the passkey opens the account's password, which gets factotum's keys
 * from secstore; Plan 9's authentication is as it was.
 *
 *	auth/passkey [-s server] signup NAME [INVITE]
 *		the password asked twice; a passkey made (#W, the page's
 *		WebAuthn: its PRF output); to signupd what the server keeps,
 *		none of it the password: the auth server's key (passtokey),
 *		secstore's verifier (PAK's Hi), secstore's factotum file with
 *		the dp9ik key - encrypted with the password as secstore -p
 *		would - and the passkey's id, public key and wrap: the name
 *		and the password, AES-GCM with a key from the PRF output
 *	auth/passkey [-s server] [-c file] login
 *		the passkey asked for (any of this site's) with passkeyd's
 *		challenge, its signature to passkeyd for its wrap, opened: the name and the password on standard output,
 *		a line each, for aux/seckeys -I.  With -c the wrap is kept in
 *		file (the machine's disk) and taken from there when passkeyd
 *		can not be reached: the wrap is nothing without the passkey
 *
 * server: signupd's and passkeyd's host (tcp!HOST!signup, !passkey),
 * else $cpu; a path is a file to talk to instead (a test's pipe to the
 * service).  The rp is the page's host's ($cpu) unless $passkeyrp.
 */
#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>
#include <authsrv.h>

enum {
	Nline	= 8192,
	Nwrap	= 512,
	CHK	= 16,
};

static char *server, *rp;
static char prfsalt[] = "plan2001 passkey v1";
static char wrapinfo[] = "plan2001 passkey wrap v1";

static void*
emalloc(ulong n)
{
	void *p;

	if((p = mallocz(n, 1)) == nil)
		sysfatal("out of memory");
	return p;
}

static char*
estrdup(char *s)
{
	char *t;

	t = emalloc(strlen(s)+1);
	strcpy(t, s);
	return t;
}

static void
usage(void)
{
	fprint(2, "usage: %s [-s server] signup name [invite]\n       %s [-s server] [-c file] login\n", argv0, argv0);
	exits("usage");
}

/* base64url, no padding (WebAuthn's) */
static char*
b64u(uchar *p, int n)
{
	char *s, *t;

	s = emalloc(2*n + 8);
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

static int
unb64u(char *s, uchar *p, int n)
{
	char *t, *u;
	int m;

	m = strlen(s);
	t = emalloc(m + 4);
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
b64(uchar *p, int n)
{
	char *s;

	s = emalloc(2*n + 8);
	enc64(s, 2*n + 8, p, n);
	return s;
}

/* the page's WebAuthn (#W): the request's answer, nil and %r an error */
static char*
webauthn(char *fmt, ...)
{
	char req[1024], *ans;
	va_list arg;
	int fd, n;

	va_start(arg, fmt);
	vsnprint(req, sizeof req, fmt, arg);
	va_end(arg);
	if((fd = open("#W/webauthn", ORDWR)) < 0)
		return nil;
	if(write(fd, req, strlen(req)) < 0){
		close(fd);
		return nil;
	}
	ans = emalloc(Nline);
	n = read(fd, ans, Nline-1);
	close(fd);
	if(n <= 0){
		free(ans);
		werrstr("no answer");
		return nil;
	}
	ans[n] = 0;
	if(strncmp(ans, "ok ", 3) != 0){
		werrstr("%s", ans);
		free(ans);
		return nil;
	}
	return ans;
}

/* the answer's NAME=VALUE, alloc'd; nil none */
static char*
field(char *ans, char *name)
{
	char *f[32], *s, *v;
	int i, n, l;

	s = estrdup(ans);
	n = tokenize(s, f, nelem(f));
	l = strlen(name);
	v = nil;
	for(i = 1; i < n; i++)
		if(strncmp(f[i], name, l) == 0 && f[i][l] == '='){
			v = estrdup(f[i]+l+1);
			break;
		}
	free(s);
	return v;
}

/* the PRF output (32 bytes) in the answer, -1 none */
static int
prf(char *ans, uchar key[32])
{
	char *p;
	int n;

	if((p = field(ans, "prf")) == nil || strcmp(p, "none") == 0){
		free(p);
		return -1;
	}
	n = unb64u(p, key, 32);
	free(p);
	return n == 32 ? 0 : -1;
}

/* the wrap's key from the PRF output */
static void
wrapkey(uchar prfout[32], uchar key[32])
{
	hkdf_x(nil, 0, (uchar*)wrapinfo, strlen(wrapinfo), prfout, 32, key, 32, hmac_sha2_256, SHA2_256dlen);
}

/* name and password, AES-256-GCM: iv[12] | ciphertext | tag[16] */
static int
wrap(uchar prfout[32], char *name, char *pass, uchar *w, int nw)
{
	uchar key[32];
	AESGCMstate s;
	int n;

	n = snprint((char*)w+12, nw-12-16, "%s\n%s", name, pass);
	if(n >= nw-12-16-1)
		return -1;
	wrapkey(prfout, key);
	genrandom(w, 12);
	setupAESGCMstate(&s, key, 32, w, 12);
	aesgcm_encrypt(w+12, n, (uchar*)prfsalt, strlen(prfsalt), w+12+n, &s);
	memset(key, 0, sizeof key);
	memset(&s, 0, sizeof s);
	return 12 + n + 16;
}

static int
unwrap(uchar prfout[32], uchar *w, int n, char **name, char **pass)
{
	uchar key[32];
	AESGCMstate s;
	char *nl;
	int r;

	if(n < 12+16+3)
		return -1;
	wrapkey(prfout, key);
	setupAESGCMstate(&s, key, 32, w, 12);
	r = aesgcm_decrypt(w+12, n-12-16, (uchar*)prfsalt, strlen(prfsalt), w+n-16, &s);
	memset(key, 0, sizeof key);
	memset(&s, 0, sizeof s);
	if(r != 0)
		return -1;
	w[n-16] = 0;
	if((nl = strchr((char*)w+12, '\n')) == nil)
		return -1;
	*nl = 0;
	*name = (char*)w+12;
	*pass = nl+1;
	return 0;
}

/*
 * secstore's verifier, as its pak.c's PAK_Hi: H = (the hmac_sha1s of
 * "secstore", the name and sha1(password))^r mod p, Hi its inverse
 */
static char*
pakhi(char *name, char *pass)
{
	static char *P = "C41CFBE4D4846F67A3DF7DE9921A49D3B42DC33728427AB159CEC8CBB"
		"DB12B5F0C244F1A734AEB9840804EA3C25036AD1B61AFF3ABBC247CD4B384224567A86"
		"3A6F020E7EE9795554BCD08ABAD7321AF27E1E92E3DB1C6E7E94FAAE590AE9C48F96D9"
		"3D178E809401ABE8A534A1EC44359733475A36A70C7B425125062B1142D";
	static char *R = "DF310F4E54A5FEC5D86D3E14863921E834113E060F90052AD332B3241"
		"CEF2497EFA0303D6344F7C819691A0F9C4A773815AF8EAECFB7EC1D98F039F17A32A7E"
		"887D97251A927D093F44A55577F4D70444AEBD06B9B45695EC23962B175F266895C67D"
		"21C4656848614D888A4";
	uchar passhash[SHA1dlen], buf[7*SHA1dlen], *cp, key[1];
	mpint *p, *r, *H, *Hi;
	int i, n;
	char *s;

	sha1((uchar*)pass, strlen(pass), passhash, nil);
	n = 8 + strlen(name) + SHA1dlen;
	cp = emalloc(n);
	memmove(cp, "secstore", 8);
	memmove(cp+8, name, strlen(name));
	memmove(cp+8+strlen(name), passhash, SHA1dlen);
	for(i = 0; i < 7; i++){
		key[0] = 'A'+i;
		hmac_sha1(cp, n, key, 1, buf+i*SHA1dlen, nil);
	}
	memset(cp, 0, n);
	free(cp);
	p = strtomp(P, nil, 16, nil);
	r = strtomp(R, nil, 16, nil);
	H = betomp(buf, sizeof buf, nil);
	mpmod(H, p, H);
	mpexp(H, r, p, H);
	Hi = mpnew(0);
	mpinvert(H, p, Hi);
	s = mptoa(Hi, 64, nil, 0);
	mpfree(p);
	mpfree(r);
	mpfree(H);
	mpfree(Hi);
	return s;
}

/* secstore's file, as its putfile makes it: AES-CBC(IV | file | X*16) with the IV, the key sha1("aescbc file", password) */
static uchar*
secfile(char *file, char *pass, int *np)
{
	uchar skey[SHA1dlen], *b;
	AESstate aes;
	DigestState *ds;
	int n;

	n = strlen(file);
	b = emalloc(AESbsize + n + CHK);
	genrandom(b, AESbsize);
	ds = sha1((uchar*)"aescbc file", 11, nil, nil);
	sha1((uchar*)pass, strlen(pass), skey, ds);
	setupAESstate(&aes, skey, AESbsize, b);
	memset(skey, 0, sizeof skey);
	memmove(b+AESbsize, file, n);
	memset(b+AESbsize+n, 'X', CHK);
	aesCBCencrypt(b, AESbsize+n+CHK, &aes);
	memset(&aes, 0, sizeof aes);
	*np = AESbsize + n + CHK;
	return b;
}

/* the service's connection: dialled - or, server a path, that file (a test's pipe to the service) */
static int
connect(char *service)
{
	if(server[0] == '/')
		return open(server, ORDWR);
	return dial(netmkaddr(server, "tcp", service), nil, nil, nil);
}

/* a line to the connection, its answer's line (alloc'd); nil and %r an error */
static char*
talk(int fd, char *service, char *line)
{
	char *ans;
	int n;

	if(write(fd, line, strlen(line)) != strlen(line))
		return nil;
	ans = emalloc(Nline);
	for(n = 0; n < Nline-1 && read(fd, ans+n, 1) == 1 && ans[n] != '\n'; n++)
		;
	ans[n] = 0;
	if(strncmp(ans, "ok", 2) != 0 || ans[2] != 0 && ans[2] != ' '){
		werrstr("%s: %s", service, n > 0 ? ans : "no answer");
		free(ans);
		return nil;
	}
	return ans;
}

static char*
call(char *service, char *line)
{
	char *ans;
	int fd;

	if((fd = connect(service)) < 0)
		return nil;
	ans = talk(fd, service, line);
	close(fd);
	return ans;
}

static int
validname(char *s)
{
	int n;

	n = strlen(s);
	if(n < 2 || n > 27 || s[0] < 'a' || s[0] > 'z' || strspn(s, "abcdefghijklmnopqrstuvwxyz0123456789") != n)
		return 0;
	return 1;
}

static void
signup(char *name, char *code)
{
	char *pass, *again, *ans, *id, *pk, *hi, *file, *line, *user, *salt;
	uchar prfout[32], w[Nwrap], *sf;
	Authkey ak;
	int nw, nsf;

	if(!validname(name))
		sysfatal("a name is a-z, then a-z and 0-9, 2 to 27 of them");
	pass = readcons("account password", nil, 1);
	if(pass == nil || strlen(pass) < 10)
		sysfatal("a password is at least 10 characters");
	again = readcons("account password again", nil, 1);
	if(again == nil || strcmp(again, pass) != 0)
		sysfatal("the passwords differ");
	memset(again, 0, strlen(again));

	user = b64u((uchar*)name, strlen(name));
	salt = b64u((uchar*)prfsalt, strlen(prfsalt));
	if((ans = webauthn("create rp=%s user=%s name=%s salt=%s", rp, user, name, salt)) == nil)
		sysfatal("passkey: %r");
	id = field(ans, "id");
	pk = field(ans, "pubkey");
	if(id == nil || pk == nil)
		sysfatal("passkey: no id or public key");
	if(prf(ans, prfout) < 0){
		/* PRF at creation not given (the authenticator's or browser's): get it once */
		free(ans);
		if((ans = webauthn("get rp=%s salt=%s allow=%s", rp, salt, id)) == nil)
			sysfatal("passkey: %r");
		if(prf(ans, prfout) < 0)
			sysfatal("passkey: no PRF here: the password only (aux/seckeys)");
	}
	free(ans);
	if((nw = wrap(prfout, name, pass, w, sizeof w)) < 0)
		sysfatal("password too long");
	memset(prfout, 0, sizeof prfout);

	passtokey(&ak, pass);
	hi = pakhi(name, pass);
	file = smprint("key proto=dp9ik dom=plan2001 user=%s !password=%s\n", name, pass);
	sf = secfile(file, pass, &nsf);
	memset(file, 0, strlen(file));
	memset(pass, 0, strlen(pass));
	line = smprint("signup %s %.*H %s %s %s %s %s%s%s\n", name, AESKEYLEN, ak.aes, hi,
		b64(sf, nsf), id, pk, b64u(w, nw), code ? " " : "", code ? code : "");
	memset(&ak, 0, sizeof ak);
	if((ans = call("signup", line)) == nil)
		sysfatal("%r");
	print("%s: account %s made, its passkey and password\n", argv0, name);
	exits(nil);
}

static void
login(char *cache)
{
	char *ans, *id, *u, *salt, *name, *pass, *ws, *f[4], *chal, *line;
	uchar prfout[32], w[Nwrap], uname[64];
	int nw, fd, nu, conn;

	/* passkeyd's challenge, which the passkey signs (none without passkeyd: the disk's wrap) */
	chal = nil;
	if((conn = connect("passkey")) >= 0){
		if((ans = talk(conn, "passkey", "challenge\n")) != nil && tokenize(ans, f, nelem(f)) == 2)
			chal = estrdup(f[1]);
		else{
			close(conn);
			conn = -1;
		}
	}
	salt = b64u((uchar*)prfsalt, strlen(prfsalt));
	if(chal != nil)
		ans = webauthn("get rp=%s salt=%s challenge=%s", rp, salt, chal);
	else
		ans = webauthn("get rp=%s salt=%s", rp, salt);
	if(ans == nil)
		sysfatal("passkey: %r");
	id = field(ans, "id");
	u = field(ans, "user");
	if(id == nil || prf(ans, prfout) < 0)
		sysfatal("passkey: no PRF here: the password instead");
	nu = u != nil ? unb64u(u, uname, sizeof uname - 1) : 0;
	uname[nu > 0 ? nu : 0] = 0;

	nw = -1;
	ws = nil;
	if(conn >= 0){
		/* the assertion for passkeyd: its wrap only for the passkey's signature on its challenge */
		line = smprint("wrap %s %s %s %s\n", id, field(ans, "auth"), field(ans, "client"), field(ans, "sig"));
		free(ans);
		if((ans = talk(conn, "passkey", line)) == nil)
			sysfatal("%r");
		close(conn);
		if(tokenize(ans, f, nelem(f)) == 3 && (nw = unb64u(f[2], w, sizeof w)) > 0)
			ws = estrdup(f[2]);
		free(ans);
	}else if(cache != nil && (fd = open(cache, OREAD)) >= 0){
		/* no passkeyd: the wrap kept on the disk, if it is this passkey's */
		ans = emalloc(Nline);
		nw = read(fd, ans, Nline-1);
		close(fd);
		ans[nw > 0 ? nw : 0] = 0;
		nw = -1;
		if(tokenize(ans, f, nelem(f)) == 2 && strcmp(f[0], id) == 0)
			nw = unb64u(f[1], w, sizeof w);
		free(ans);
	}else
		sysfatal("%r");
	if(nw <= 0)
		sysfatal("no wrap for this passkey");
	if(unwrap(prfout, w, nw, &name, &pass) < 0)
		sysfatal("the wrap does not open with this passkey");
	memset(prfout, 0, sizeof prfout);
	if(nu > 0 && strcmp((char*)uname, name) != 0)
		sysfatal("the passkey's user is not the wrap's");
	if(ws != nil && cache != nil && (fd = create(cache, OWRITE, 0600)) >= 0){
		fprint(fd, "%s %s\n", id, ws);
		close(fd);
	}
	print("%s\n%s\n", name, pass);
	memset(pass, 0, strlen(pass));
	exits(nil);
}

void
main(int argc, char **argv)
{
	char *cache;

	cache = nil;
	ARGBEGIN{
	case 's':
		server = EARGF(usage());
		break;
	case 'c':
		cache = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND
	fmtinstall('H', encodefmt);
	if(server == nil && (server = getenv("cpu")) == nil)
		sysfatal("no server: -s or $cpu");
	if((rp = getenv("passkeyrp")) == nil && (rp = getenv("cpu")) == nil)
		sysfatal("no rp: $passkeyrp or $cpu");
	if((argc == 2 || argc == 3) && strcmp(argv[0], "signup") == 0)
		signup(argv[1], argc == 3 ? argv[2] : nil);
	if(argc == 1 && strcmp(argv[0], "login") == 0)
		login(cache);
	usage();
}
