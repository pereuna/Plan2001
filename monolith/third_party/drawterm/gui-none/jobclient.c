/*
 * 9pterm -S SOCKET -c CMD: a command through a session (9pterm -M,
 * kern/devjobs.c) - no authentication, no kernel: the command to the
 * socket, its stdout, stderr and status back.  Exit 0 for an empty
 * status, 1 otherwise (the status on stderr), 2 when the session cannot
 * be reached or ends, 124 after secs (0: no limit).  System headers only.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int
readfull(int fd, void *va, long n, long deadline)
{
	char *p;
	long m, t;
	struct pollfd pf;
	int ms;

	p = va;
	for(t = 0; t < n; t += m){
		if(deadline){
			ms = (deadline - time(0)) * 1000;
			if(ms <= 0)
				return -2;
			pf.fd = fd;
			pf.events = POLLIN;
			if(poll(&pf, 1, ms) == 0)
				return -2;
		}
		if((m = read(fd, p+t, n-t)) <= 0)
			return -1;
	}
	return 0;
}

int
ninepclient(char *sock, char *cmd, int secs)
{
	struct sockaddr_un sa;
	unsigned char h[5];
	unsigned long len;
	long n, deadline;
	char *buf;
	int fd, r;

	if(strlen(sock) >= sizeof sa.sun_path){
		fprintf(stderr, "9pterm: %s: name too long\n", sock);
		return 2;
	}
	if((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0){
		perror("9pterm: socket");
		return 2;
	}
	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	strcpy(sa.sun_path, sock);
	if(connect(fd, (struct sockaddr*)&sa, sizeof sa) < 0){
		fprintf(stderr, "9pterm: no session at %s: ", sock);
		perror("");
		return 2;
	}
	n = strlen(cmd);
	h[0] = 'C';
	h[1] = n>>24; h[2] = n>>16; h[3] = n>>8; h[4] = n;
	if(write(fd, h, 5) != 5 || write(fd, cmd, n) != n){
		perror("9pterm: sending the command");
		return 2;
	}
	deadline = secs > 0 ? time(0) + secs : 0;
	for(;;){
		if((r = readfull(fd, h, 5, deadline)) < 0)
			break;
		len = (unsigned long)h[1]<<24 | h[2]<<16 | h[3]<<8 | h[4];
		if(len > 64*1024*1024 || (buf = malloc(len+1)) == NULL){
			r = -1;
			break;
		}
		if((r = readfull(fd, buf, len, deadline)) < 0){
			free(buf);
			break;
		}
		buf[len] = 0;
		switch(h[0]){
		case 'o':
			write(1, buf, len);
			break;
		case 'e':
			write(2, buf, len);
			break;
		case 's':
			if(len == 0)
				return 0;
			fprintf(stderr, "%s\n", buf);
			return 1;
		}
		free(buf);
	}
	if(r == -2){
		fprintf(stderr, "9pterm: timeout: the command ran %d s\n", secs);
		return 124;
	}
	fprintf(stderr, "9pterm: the session ended before the command did\n");
	return 2;
}
