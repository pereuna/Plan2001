#include <u.h>
#include <libc.h>

/*
 * WebRTC between two wasm32 machines (tools/test-9wasm32 rtc): devwsnet.c's
 * announce, listen and NAME.rtc!port over the pages' DataChannel.
 *	rtc -l port cmd [arg...]	announce tcp!*!port, take one call, cmd with it as fd 0 and 1
 *	rtc -m addr mtpt		dial addr, mount the 9P server it is on mtpt
 */
void
main(int argc, char **argv)
{
	char addr[64], adir[40], ldir[40];
	int afd, lfd, dfd, fd;

	if(argc >= 4 && strcmp(argv[1], "-l") == 0){
		snprint(addr, sizeof addr, "tcp!*!%s", argv[2]);
		if((afd = announce(addr, adir)) < 0)
			sysfatal("announce %s: %r", addr);
		print("rtc: announced %s\n", addr);
		if((lfd = listen(adir, ldir)) < 0)
			sysfatal("listen: %r");
		if((dfd = accept(lfd, ldir)) < 0)
			sysfatal("accept: %r");
		print("rtc: a call\n");
		dup(dfd, 0);
		dup(dfd, 1);
		close(dfd);
		close(lfd);
		close(afd);
		exec(argv[3], argv+3);
		sysfatal("exec %s: %r", argv[3]);
	}
	if(argc == 4 && strcmp(argv[1], "-m") == 0){
		if((fd = dial(argv[2], nil, nil, nil)) < 0)
			sysfatal("dial %s: %r", argv[2]);
		print("rtc: connected\n");
		if(mount(fd, -1, argv[3], MREPL, "") < 0)
			sysfatal("mount: %r");
		print("rtc: mounted %s\n", argv[3]);
		exits(nil);
	}
	fprint(2, "usage: rtc -l port cmd [arg...] | rtc -m addr mtpt\n");
	exits("usage");
}
