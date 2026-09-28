/*
 * drawterm's TCP connections as WebSockets: each connect() opens
 * MONOLITH_WS/PORT (Plan2001's webterm: /17019 rcpu, /567 auth), and the
 * bytes stream both ways.  The page owns the WebSockets (js_netopen ...,
 * monolith.h); recv() waits on a buffer its mo_netdata fills, send()
 * hands the bytes over and returns.  The
 * address in connect() is not used: the WebSocket server is the machine.
 *
 * Over wss the rcpu connection (port 17019) is MONOLITH_WS/rcpu instead:
 * webterm authenticates and runs rcpu without its TLS-PSK, the WebSocket's
 * TLS being enough, and cpu.c's tlsClient (-DtlsClient=monolithtlsclient
 * for cpu.c only, in web.c) leaves that connection as it is.
 *
 * drawterm's calls come here by renaming (Make.emscripten: -Dsocket=wssocket
 * ...); the rest of drawterm's file descriptors pass through.  No drawterm
 * headers: their type macros clash with Emscripten's.
 */
#undef socket
#undef connect
#undef send
#undef recv
#undef close
#undef setsockopt
#undef getsockname
#undef getpeername
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <emscripten.h>
#include "monolith.h"

enum {
	Nconn	= 64,
	/* received, not yet read by drawterm: a WebSocket cannot be paused,
	   so past this the connection fails rather than grow or lose data */
	Maxbuf	= 64*1024*1024,
	/* sent, not yet acknowledged by the far end (resumable links only,
	   mo_netacked): send() waits past this, so drawterm is held back
	   instead of the page's resend buffer growing */
	Maxunacked	= 8*1024*1024,
	Fdbase	= 0x4000,	/* far from Emscripten's own descriptors */

	Free	= 0,
	New,
	Connecting,
	Open = Netopen,
	Closed = Netclosed,
};

typedef struct Conn Conn;
struct Conn {
	int	state;
	int	gen;		/* events from an older WebSocket in this slot are stale */
	int	port;
	unsigned char	*buf;
	size_t	rp;
	size_t	wp;
	size_t	cap;
	int	err;		/* the connection failed here (errno), not closed */
	unsigned int	sent;	/* bytes sent (mod 2^32) */
	unsigned int	acked;	/* of them acknowledged, once acking */
	int	acking;
};

static Conn conns[Nconn];
static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

static Conn*
conn(int fd)
{
	if(fd < Fdbase || fd >= Fdbase+Nconn || conns[fd-Fdbase].state == Free)
		return NULL;
	return &conns[fd-Fdbase];
}

/* from the page, main thread: the WebSocket opened (Netopen) or closed (Netclosed) */
EMSCRIPTEN_KEEPALIVE void
mo_netstate(int i, int gen, int state)
{
	pthread_mutex_lock(&lk);
	if(conns[i].gen == gen && conns[i].state != Free)
		conns[i].state = state;
	pthread_cond_broadcast(&cv);
	pthread_mutex_unlock(&lk);
}

/* from the page, main thread: n bytes at p arrived */
EMSCRIPTEN_KEEPALIVE void
mo_netdata(int i, int gen, unsigned char *p, int n)
{
	Conn *c;
	unsigned char *nb;
	size_t need;

	pthread_mutex_lock(&lk);
	c = &conns[i];
	if(c->gen != gen || c->state == Free)
		goto out;
	if(c->rp == c->wp)
		c->rp = c->wp = 0;
	need = c->wp - c->rp + n;
	if(c->rp > 0 && c->wp + n > c->cap){
		memmove(c->buf, c->buf + c->rp, c->wp - c->rp);
		c->wp -= c->rp;
		c->rp = 0;
	}
	if(need > c->cap){
		nb = NULL;
		if(need <= Maxbuf)
			nb = realloc(c->buf, need*2 < Maxbuf ? need*2 : Maxbuf);
		if(nb == NULL){
			/* dropping these bytes would corrupt the stream: fail it */
			c->err = need > Maxbuf ? ENOBUFS : ENOMEM;
			c->state = Closed;
			pthread_cond_broadcast(&cv);
			pthread_mutex_unlock(&lk);
			js_netclose(i);
			return;
		}
		c->buf = nb;
		c->cap = need*2 < Maxbuf ? need*2 : Maxbuf;
	}
	memcpy(c->buf + c->wp, p, n);
	c->wp += n;
	pthread_cond_broadcast(&cv);
out:
	pthread_mutex_unlock(&lk);
}

/* from the page, main thread: the far end has n bytes of what we sent */
EMSCRIPTEN_KEEPALIVE void
mo_netacked(int i, int gen, unsigned int n)
{
	pthread_mutex_lock(&lk);
	if(conns[i].gen == gen && conns[i].state != Free){
		conns[i].acked = n;
		conns[i].acking = 1;
		pthread_cond_broadcast(&cv);
	}
	pthread_mutex_unlock(&lk);
}

int
wssocket(int domain, int type, int protocol)
{
	int i;

	if((type & 0xF) != SOCK_STREAM){
		errno = EPROTONOSUPPORT;
		return -1;
	}
	pthread_mutex_lock(&lk);
	for(i = 0; i < Nconn; i++)
		if(conns[i].state == Free){
			conns[i].state = New;
			conns[i].gen++;
			conns[i].rp = conns[i].wp = 0;
			conns[i].err = 0;
			conns[i].sent = conns[i].acked = 0;
			conns[i].acking = 0;
			pthread_mutex_unlock(&lk);
			return Fdbase + i;
		}
	pthread_mutex_unlock(&lk);
	errno = EMFILE;
	return -1;
}

int
wsconnect(int fd, const struct sockaddr *a, socklen_t alen)
{
	Conn *c;
	char *base, path[16], url[512];
	int i, gen, ok;

	if((c = conn(fd)) == NULL)
		return connect(fd, a, alen);
	if(a->sa_family != AF_INET && a->sa_family != AF_INET6){
		errno = EAFNOSUPPORT;
		return -1;
	}
	i = c - conns;
	pthread_mutex_lock(&lk);
	c->port = ntohs(((struct sockaddr_in*)a)->sin_port);
	c->state = Connecting;
	gen = c->gen;
	pthread_mutex_unlock(&lk);
	base = getenv("MONOLITH_WS");
	if(base == NULL)
		base = "ws://127.0.0.1:8081";
	if(c->port == 17019 && wsrcpuplain())
		strcpy(path, "rcpu");
	else
		snprintf(path, sizeof path, "%d", c->port);
	snprintf(url, sizeof url, "%s/%s", base, path);
	js_netopen(i, gen, url);
	pthread_mutex_lock(&lk);
	while(c->gen == gen && c->state == Connecting)
		pthread_cond_wait(&cv, &lk);
	ok = c->gen == gen && c->state == Open;
	pthread_mutex_unlock(&lk);
	if(!ok){
		errno = ECONNREFUSED;
		return -1;
	}
	return 0;
}

ssize_t
wsrecv(int fd, void *p, size_t n, int flags)
{
	Conn *c;
	size_t m;
	int gen;

	if((c = conn(fd)) == NULL)
		return recv(fd, p, n, flags);
	pthread_mutex_lock(&lk);
	gen = c->gen;
	while(c->gen == gen && c->rp == c->wp && c->state == Open)
		pthread_cond_wait(&cv, &lk);
	m = 0;
	if(c->gen == gen && c->err){
		errno = c->err;
		pthread_mutex_unlock(&lk);
		return -1;
	}
	if(c->gen == gen){
		m = c->wp - c->rp;
		if(m > n)
			m = n;
		memcpy(p, c->buf + c->rp, m);
		c->rp += m;
	}
	pthread_mutex_unlock(&lk);
	return m;	/* 0: closed */
}

ssize_t
wssend(int fd, const void *p, size_t n, int flags)
{
	Conn *c;
	void *q;

	int gen;

	if((c = conn(fd)) == NULL)
		return send(fd, p, n, flags);
	if(n == 0)
		return 0;
	if((q = malloc(n)) == NULL){
		errno = ENOMEM;
		return -1;
	}
	memcpy(q, p, n);
	pthread_mutex_lock(&lk);
	gen = c->gen;
	/* backpressure: wait for acknowledgements past Maxunacked */
	while(c->gen == gen && c->state == Open && c->acking && c->sent - c->acked > Maxunacked)
		pthread_cond_wait(&cv, &lk);
	if(c->gen != gen || c->state != Open){
		pthread_mutex_unlock(&lk);
		free(q);
		errno = c->err ? c->err : EPIPE;
		return -1;
	}
	c->sent += n;
	/* in order with the other calls to the main thread, and with sent */
	js_netsend(c - conns, q, n);
	pthread_mutex_unlock(&lk);
	return n;
}

int
wsclose(int fd)
{
	Conn *c;
	int i;

	if((c = conn(fd)) == NULL)
		return close(fd);
	i = c - conns;
	js_netclose(i);
	pthread_mutex_lock(&lk);
	c->state = Free;
	c->gen++;
	pthread_cond_broadcast(&cv);
	pthread_mutex_unlock(&lk);
	return 0;
}

int
wssetsockopt(int fd, int level, int name, const void *v, socklen_t len)
{
	if(conn(fd) == NULL)
		return setsockopt(fd, level, name, v, len);
	return 0;
}

static int
fakeaddr(Conn *c, struct sockaddr *a, socklen_t *alen, int port)
{
	struct sockaddr_in sin;

	memset(&sin, 0, sizeof sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sin.sin_port = htons(port);
	memcpy(a, &sin, *alen < sizeof sin ? *alen : sizeof sin);
	*alen = sizeof sin;
	return 0;
}

int
wsgetsockname(int fd, struct sockaddr *a, socklen_t *alen)
{
	Conn *c;

	if((c = conn(fd)) == NULL)
		return getsockname(fd, a, alen);
	return fakeaddr(c, a, alen, 0);
}

int
wsgetpeername(int fd, struct sockaddr *a, socklen_t *alen)
{
	Conn *c;

	if((c = conn(fd)) == NULL)
		return getpeername(fd, a, alen);
	return fakeaddr(c, a, alen, c->port);
}

/* rcpu goes to MONOLITH_WS/rcpu, without TLS-PSK (web.c's tlsClient) */
int
wsrcpuplain(void)
{
	char *base;

	base = getenv("MONOLITH_WS");
	return base != NULL && strncmp(base, "wss:", 4) == 0;
}
