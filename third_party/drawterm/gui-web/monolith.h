/*
 * The boundary between drawterm.wasm and the page.  The imports js_* are
 * gui-web/library.js, run on the browser's main thread, which hands them
 * to the page's web/monolith.js (Module.monolith); the exports mo_* are
 * what the page calls.  Plain C types only: no drawterm or Emscripten
 * headers, so both sides' files can include it.
 */

enum {	/* mo_input */
	Evmouse = 1,	/* x, y, buttons */
	Evkey,		/* rune, down */
	Evresize,	/* width, height */
};

enum {	/* mo_netstate */
	Netopen = 3,
	Netclosed = 4,
};

/* imports: the page */
void	js_ready(void);		/* the page may call the exports now */
void	js_screensize(int *w, int *h);
void	js_resize(int w, int h);
void	js_flush(void *base, int stride, int x0, int y0, int x1, int y1);	/* XBGR32 */
void	js_cursor(unsigned char *rgba, int hx, int hy);	/* 16x16 */
void	js_netopen(int conn, int gen, char *url);	/* a WebSocket */
void	js_netsend(int conn, void *p, int n);	/* frees p */
void	js_netclose(int conn);

/* exports: from the page */
void	mo_input(int type, int a, int b, int c);
void	mo_netstate(int conn, int gen, int state);
void	mo_netdata(int conn, int gen, unsigned char *p, int n);

/* events.c, for web.c */
void	mo_nextinput(int *type, int *a, int *b, int *c);
