/*
 * gui-web's browser side: the event queue that the page fills and the
 * calls into the page (Module.monolith*, web/index.html) for the canvas
 * and the cursor.  No drawterm headers here: their type macros clash with
 * Emscripten's.  web.c is the drawterm side.
 */
#include <pthread.h>
#include <emscripten.h>

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

/* called by the page (Module._webpush) on the browser's main thread */
EMSCRIPTEN_KEEPALIVE void
webpush(int type, int a, int b, int c)
{
	pthread_mutex_lock(&evlk);
	if(evw - evr < Nev){
		evq[evw++ % Nev] = (Ev){type, a, b, c};
		pthread_cond_signal(&evcv);
	}
	pthread_mutex_unlock(&evlk);
}

void
webnext(int *type, int *a, int *b, int *c)
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

void
webscreensize(int *w, int *h)
{
	*w = MAIN_THREAD_EM_ASM_INT({ return Module.monolithSize()[0]; });
	*h = MAIN_THREAD_EM_ASM_INT({ return Module.monolithSize()[1]; });
}

void
webresize(int w, int h)
{
	MAIN_THREAD_EM_ASM({ Module.monolithResize($0, $1); }, w, h);
}

/*
 * Copy rectangle x0,y0-x1,y1 of the screen (XBGR32: bytes R G B x, base is
 * pixel 0,0, stride in bytes) into the canvas.  ImageData cannot view
 * shared memory, so it is a copy, with alpha set.  (No top-level commas
 * in EM_ASM code: they split the macro's arguments.)
 */
void
webflush(void *base, int stride, int x0, int y0, int x1, int y1)
{
	MAIN_THREAD_EM_ASM({
		var w = $4 - $2;
		var h = $5 - $3;
		if(w <= 0 || h <= 0)
			return;
		var img = new ImageData(w, h);
		var d = new Uint32Array(img.data.buffer);
		var s = HEAPU32;
		var stride = $1 >> 2;
		var p = ($0 >> 2) + $3*stride + $2;
		for(var y = 0, i = 0; y < h; y++, p += stride)
			for(var x = 0; x < w; x++)
				d[i++] = s[p + x] | 0xff000000;
		Module.monolithFlush(img, $2, $3);
	}, base, stride, x0, y0, x1, y1);
}

/* 16x16 RGBA cursor image, hot spot hx,hy */
void
webcursor(unsigned char *rgba, int hx, int hy)
{
	MAIN_THREAD_EM_ASM({
		var img = new ImageData(16, 16);
		img.data.set(HEAPU8.subarray($0, $0 + 16*16*4));
		Module.monolithCursor(img, $1, $2);
	}, rgba, hx, hy);
}
