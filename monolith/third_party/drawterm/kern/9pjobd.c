/*
 * 9pjobd - the CPU server's side of a 9pterm session (9pterm -M,
 * kern/devjobs.c).  9pterm carries this source and the session compiles
 * it into $home/bin/$cputype; it holds one file open, CTL (9pterm's
 * /dev/jobs/ctl): a read gives a job, and the job's output and status go
 * back as writes to it - no file to open per job.
 *
 *	read:	'J' id[4] len[4] command		(one job, an rc command)
 *	write:	'o'|'e'|'s' id[4] len[4] data	(stdout, stderr, status)
 *
 * Each job runs in its own process and note group: rc -c, stdin /dev/null.
 */
#include <u.h>
#include <libc.h>

enum {
	Hdr	= 9,
	Maxcmd	= 1024*1024,
	Iosize	= 8000,
};

int ctl;

void
put32(uchar *p, ulong v)
{
	p[0] = v>>24;
	p[1] = v>>16;
	p[2] = v>>8;
	p[3] = v;
}

ulong
get32(uchar *p)
{
	return p[0]<<24 | p[1]<<16 | p[2]<<8 | p[3];
}

void
frame(int type, int id, void *data, long n)
{
	uchar buf[Hdr+Iosize];

	buf[0] = type;
	put32(buf+1, id);
	put32(buf+5, n);
	memmove(buf+Hdr, data, n);
	write(ctl, buf, Hdr+n);
}

void
relay(int fd, int type, int id)
{
	uchar buf[Iosize];
	long n;

	while((n = read(fd, buf, sizeof buf)) > 0)
		frame(type, id, buf, n);
}

void
job(int id, char *cmd)
{
	int out[2], err[2], rcpid, n, null;
	char *status;
	Waitmsg *w;

	if(pipe(out) < 0 || pipe(err) < 0){
		frame('s', id, "9pjobd: pipe", 12);
		exits(nil);
	}
	switch(rcpid = rfork(RFPROC|RFFDG|RFNOTEG|RFENVG)){
	case -1:
		frame('s', id, "9pjobd: fork", 12);
		exits(nil);
	case 0:
		close(ctl);
		null = open("/dev/null", OREAD);
		dup(null, 0);
		dup(out[1], 1);
		dup(err[1], 2);
		close(null);
		close(out[0]);
		close(out[1]);
		close(err[0]);
		close(err[1]);
		execl("/bin/rc", "rc", "-c", cmd, nil);
		exits("exec rc");
	}
	close(out[1]);
	close(err[1]);
	switch(rfork(RFPROC|RFFDG)){
	case 0:
		close(out[0]);
		relay(err[0], 'e', id);
		exits(nil);
	}
	close(err[0]);
	relay(out[0], 'o', id);
	close(out[0]);
	status = strdup("9pjobd: lost");
	for(n = 0; n < 2 && (w = wait()) != nil; n++){
		if(w->pid == rcpid){
			free(status);
			status = strdup(w->msg);
		}
		free(w);
	}
	frame('s', id, status, strlen(status));
	exits(nil);
}

void
main(int argc, char **argv)
{
	static uchar buf[Hdr+Maxcmd+1];
	long n, len;
	int id;

	if(argc != 2){
		fprint(2, "usage: 9pjobd ctl\n");
		exits("usage");
	}
	if((ctl = open(argv[1], ORDWR)) < 0)
		sysfatal("%s: %r", argv[1]);
	for(;;){
		n = read(ctl, buf, sizeof buf - 1);
		if(n <= 0)
			exits(nil);
		if(n < Hdr || buf[0] != 'J')
			continue;
		id = get32(buf+1);
		len = get32(buf+5);
		if(len != n-Hdr)
			continue;
		buf[n] = 0;
		switch(rfork(RFPROC|RFFDG|RFNOWAIT|RFNOTEG)){
		case 0:
			job(id, (char*)buf+Hdr);
		}
	}
}
