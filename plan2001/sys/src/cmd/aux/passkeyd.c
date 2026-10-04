/*
 * passkeyd - a passkey's wrap (docs/webauthn.md): a line from the machine's
 * auth/passkey login, through webterm's /17041 (policy word login), on fd 0,
 * the answer on fd 1; aux/listen's service.auth/tcp17041, as the host owner.
 *
 *	wrap CREDID	->	ok NAME WRAP | error WHY
 *
 * The wrap is the name and the password encrypted with the passkey's PRF
 * output: nothing without the passkey, so it is given to whoever names its
 * id (random, 16 bytes or more).  Checking the passkey's signature first is
 * docs/webauthn.md's phase 4.
 *
 *	aux/passkeyd [-w webauthn]	default /adm/webauthn
 */
#include <u.h>
#include <libc.h>
#include <bio.h>

static char *wadir = "/adm/webauthn";

static void
answer(char *fmt, ...)
{
	char buf[4096];
	va_list arg;

	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, fmt, arg);
	va_end(arg);
	fprint(1, "%s\n", buf);
	exits(buf[0] == 'o' ? nil : "error");
}

void
main(int argc, char **argv)
{
	char line[512], *f[3], *p, *l, *name, *wrap;
	Biobuf *b;
	long n, m;

	ARGBEGIN{
	case 'w':
		wadir = EARGF(sysfatal("usage"));
		break;
	}ARGEND
	for(n = 0; n < sizeof line-1 && (m = read(0, line+n, sizeof line-1-n)) > 0; n += m)
		if(memchr(line+n, '\n', m) != nil){
			n += m;
			break;
		}
	line[n] = 0;
	if((p = strchr(line, '\n')) == nil)
		answer("error no line");
	*p = 0;
	if(tokenize(line, f, nelem(f)) != 2 || strcmp(f[0], "wrap") != 0)
		answer("error bad request");
	n = strlen(f[1]);
	if(n < 16 || n > 1024 || strspn(f[1], "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") != n)
		answer("error no such passkey");
	p = smprint("%s/%s", wadir, f[1]);
	if((b = Bopen(p, OREAD)) == nil)
		answer("error no such passkey");
	name = wrap = nil;
	while((l = Brdstr(b, '\n', 1)) != nil){
		if(strncmp(l, "name\t", 5) == 0)
			name = strdup(l+5);
		else if(strncmp(l, "wrap\t", 5) == 0)
			wrap = strdup(l+5);
		free(l);
	}
	Bterm(b);
	if(name == nil || wrap == nil)
		answer("error no such passkey");
	answer("ok %s %s", name, wrap);
}
