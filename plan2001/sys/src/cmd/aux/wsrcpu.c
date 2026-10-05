#include <u.h>
#include <libc.h>
#include <auth.h>

/*
 * aux/wsrcpu - rcpu's connection for an app's origin (docs/app-origins.md,
 * docs/architecture.md D7): webterm's own rcpu session, GET /rcpu on the
 * page's https origin.  As tlsclient -a, but no TLS: the WebSocket is wss
 * already, and webterm, not the client, says what runs (the app's
 * namespace and image).  Dial, authenticate (p9any, factotum), then run
 * cmd with the connection as its 0 and 1 - rcpu's sendscript and client,
 * through /boot/rconnect.app.
 *
 *	aux/wsrcpu [-k keyspec] dialstring cmd [arg...]
 * dialstring tcp!MACHINE!rcpuws on the wasm32 machine (devwsnet: /rcpu).
 */
static void
usage(void)
{
	fprint(2, "usage: aux/wsrcpu [-k keyspec] dialstring cmd [arg...]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	AuthInfo *ai;
	char *keyspec;
	int fd;

	keyspec = "";
	ARGBEGIN{
	case 'k':
		keyspec = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND
	if(argc < 2)
		usage();
	if((fd = dial(argv[0], nil, nil, nil)) < 0)
		sysfatal("dial %s: %r", argv[0]);
	ai = auth_proxy(fd, auth_getkey, "proto=p9any role=client %s", keyspec);
	if(ai == nil)
		sysfatal("auth_proxy: %r");
	auth_freeAI(ai);
	dup(fd, 0);
	dup(fd, 1);
	if(fd > 1)
		close(fd);
	exec(argv[1], argv+1);
	sysfatal("exec %s: %r", argv[1]);
}
