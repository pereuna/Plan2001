/*
 * The page's input for drawterm: mo_input (main thread) queues it, and
 * web.c's kproc takes it with mo_nextinput.  js_ready at start tells the
 * page it may call the exports.
 */
#include <pthread.h>
#include <emscripten.h>
#include "monolith.h"

enum { Nev = 1024 };

typedef struct Ev Ev;
struct Ev {
	int	type;
	int	a;
	int	b;
	int	c;
};

static Ev evq[Nev];
static unsigned evr, evw;
static pthread_mutex_t evlk = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t evcv = PTHREAD_COND_INITIALIZER;

__attribute__((constructor)) static void
ready(void)
{
	js_ready();
}

EMSCRIPTEN_KEEPALIVE void
mo_input(int type, int a, int b, int c)
{
	pthread_mutex_lock(&evlk);
	if(evw - evr < Nev){
		evq[evw++ % Nev] = (Ev){type, a, b, c};
		pthread_cond_signal(&evcv);
	}
	pthread_mutex_unlock(&evlk);
}

void
mo_nextinput(int *type, int *a, int *b, int *c)
{
	Ev e;

	pthread_mutex_lock(&evlk);
	while(evr == evw)
		pthread_cond_wait(&evcv, &evlk);
	e = evq[evr++ % Nev];
	pthread_mutex_unlock(&evlk);
	*type = e.type;
	*a = e.a;
	*b = e.b;
	*c = e.c;
}
