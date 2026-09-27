/* No drawterm headers here: their type macros clash with Emscripten's. */
#undef main	/* Make.emscripten: -Dmain=drawtermmain etc. */
#undef getsockname
#undef getpeername
#undef recv
#undef socket
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <emscripten/posix_socket.h>

/*
 * Sockets go through Emscripten's websocket_to_posix_proxy (-lwebsocket.js,
 * -sPROXY_POSIX_SOCKETS): open the bridge before drawterm's main runs.
 * This is the program's main; drawterm's own is renamed drawtermmain.
 * It runs in a pthread (-sPROXY_TO_PTHREAD), so it can wait for the
 * bridge WebSocket to open.
 */
int drawtermmain(int, char**);

int
main(int argc, char **argv)
{
	char *url;
	unsigned short state;
	int i;

	url = getenv("MONOLITH_BRIDGE");
	if(url == NULL)
		url = "ws://127.0.0.1:8081";
	EMSCRIPTEN_WEBSOCKET_T ws = emscripten_init_websocket_to_posix_socket_bridge(url);
	for(i = 0; i < 100; i++){
		state = 0;
		emscripten_websocket_get_ready_state(ws, &state);
		if(state == 1)
			return drawtermmain(argc, argv);
		if(state > 1)
			break;
		usleep(100*1000);
	}
	fprintf(stderr, "drawterm: no socket bridge at %s\n", url);
	return 1;
}

/*
 * Emscripten 3.1.69's proxied getsockname and getpeername send
 * sizeof(d) + *address_len - 256 bytes to the proxy: 0 bytes (and a closed
 * bridge) unless *address_len is 256.  drawterm calls these (compiled with
 * -Dgetsockname=monolithgetsockname ...) with a 256-byte buffer.
 */
enum { Sockaddrmax = 256 };

static int
fixaddr(int r, unsigned char *buf, socklen_t n, struct sockaddr *a, socklen_t *alen)
{
	if(r == 0){
		memcpy(a, buf, n < *alen ? n : *alen);
		*alen = n;
	}
	return r;
}

int
monolithgetsockname(int fd, struct sockaddr *a, socklen_t *alen)
{
	unsigned char buf[Sockaddrmax];
	socklen_t n = Sockaddrmax;

	return fixaddr(getsockname(fd, (struct sockaddr*)buf, &n), buf, n, a, alen);
}

int
monolithgetpeername(int fd, struct sockaddr *a, socklen_t *alen)
{
	unsigned char buf[Sockaddrmax];
	socklen_t n = Sockaddrmax;

	return fixaddr(getpeername(fd, (struct sockaddr*)buf, &n), buf, n, a, alen);
}

/*
 * Every call through the bridge is a round trip to the proxy, and drawterm
 * reads the auth strings a byte at a time (cpu.c readstr): over a phone
 * link that is minutes of round trips, and the server gives up.  So recv
 * (drawterm's, -Drecv=monolithrecv) reads what the socket has, up to
 * Rbufsize, and hands it out from a buffer; socket() (-Dsocket=...) empties
 * the buffer of a reused descriptor.
 */
enum { Nrbuf = 256, Rbufsize = 64*1024 };

typedef struct Rbuf Rbuf;
struct Rbuf {
	pthread_mutex_t	lk;
	int	rp;
	int	wp;
	unsigned char	data[Rbufsize];
};

static Rbuf *rbufs[Nrbuf];
static pthread_mutex_t rbufslk = PTHREAD_MUTEX_INITIALIZER;

static Rbuf*
getrbuf(int fd)
{
	Rbuf *b;

	if(fd < 0 || fd >= Nrbuf)
		return NULL;
	pthread_mutex_lock(&rbufslk);
	b = rbufs[fd];
	if(b == NULL && (b = calloc(1, sizeof *b)) != NULL){
		pthread_mutex_init(&b->lk, NULL);
		rbufs[fd] = b;
	}
	pthread_mutex_unlock(&rbufslk);
	return b;
}

ssize_t
monolithrecv(int fd, void *p, size_t n, int flags)
{
	Rbuf *b;
	ssize_t r;
	size_t m;

	if(flags != 0 || (b = getrbuf(fd)) == NULL)
		return recv(fd, p, n, flags);
	pthread_mutex_lock(&b->lk);
	if(b->rp == b->wp){
		r = recv(fd, b->data, Rbufsize, 0);
		if(r <= 0){
			pthread_mutex_unlock(&b->lk);
			return r;
		}
		b->rp = 0;
		b->wp = r;
	}
	m = b->wp - b->rp;
	if(m > n)
		m = n;
	memcpy(p, b->data + b->rp, m);
	b->rp += m;
	pthread_mutex_unlock(&b->lk);
	return m;
}

int
monolithsocket(int domain, int type, int protocol)
{
	Rbuf *b;
	int fd;

	fd = socket(domain, type, protocol);
	if((b = getrbuf(fd)) != NULL){
		pthread_mutex_lock(&b->lk);
		b->rp = b->wp = 0;
		pthread_mutex_unlock(&b->lk);
	}
	return fd;
}
