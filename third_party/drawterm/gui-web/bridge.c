/* No drawterm headers here: their type macros clash with Emscripten's. */
#undef main	/* Make.emscripten: -Dmain=drawtermmain etc. */
#undef getsockname
#undef getpeername
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
