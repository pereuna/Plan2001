// Monolith's page side of the boundary with drawterm.wasm (see
// third_party/drawterm/gui-web/monolith.h).  drawterm calls these through
// gui-web/library.js as Module.monolith.*, on the main thread; drawterm's
// own exports arrive in attach().  The page decides what the screen is (a
// 2D canvas here; WebGPU later is a change to this file) and carries
// drawterm's connections as WebSockets.
'use strict';

/* opts: canvas, size() -> [w, h], onready(), onflush(n), log(text) */
function Monolith(opts) {
	const ctx = opts.canvas.getContext('2d', { alpha: false });
	const cursorcanvas = document.createElement('canvas');
	cursorcanvas.width = cursorcanvas.height = 16;
	const socks = {};	/* conn -> WebSocket */
	let wasm = null, flushes = 0;
	const log = opts.log || (() => {});

	return {
		/* drawterm's exports: input, netstate, netdata */
		attach(exports) {
			wasm = exports;
			if (opts.onready) opts.onready();
		},
		get ready() { return wasm !== null; },
		get flushes() { return flushes; },

		/* from the page: mouse (1: x, y, buttons), key (2: rune, down), resize (3: w, h) */
		input(type, a, b, c) {
			if (wasm) wasm.input(type, a, b, c);
		},

		/* the screen */
		size: () => opts.size(),
		resize(w, h) {
			const c = opts.canvas;
			c.width = w; c.height = h;
			c.style.width = w + 'px'; c.style.height = h + 'px';
		},
		present(img, x, y) {
			ctx.putImageData(img, x, y);
			flushes++;
			if (opts.onflush) opts.onflush(flushes);
		},
		cursor(img, hx, hy) {
			cursorcanvas.getContext('2d').putImageData(img, 0, 0);
			hx = Math.min(15, Math.max(0, hx)); hy = Math.min(15, Math.max(0, hy));
			opts.canvas.style.cursor = `url(${cursorcanvas.toDataURL()}) ${hx} ${hy}, default`;
		},

		/* drawterm's connections: one WebSocket each; gen tells an old
		   socket's late events from the new one's in the same slot */
		netopen(conn, gen, url) {
			const ws = new WebSocket(url);
			ws.binaryType = 'arraybuffer';
			socks[conn] = ws;
			ws.onopen = () => wasm && wasm.netstate(conn, gen, 3);
			ws.onclose = e => {
				if (socks[conn] === ws) delete socks[conn];
				log(`connection ${url} closed (${e.code})`);
				if (wasm) wasm.netstate(conn, gen, 4);
			};
			ws.onmessage = e => wasm && wasm.netdata(conn, gen, new Uint8Array(e.data));
		},
		netsend(conn, bytes) {
			const ws = socks[conn];
			if (ws && ws.readyState === 1) ws.send(bytes);
		},
		netclose(conn) {
			const ws = socks[conn];
			if (ws) { delete socks[conn]; ws.close(); }
		},
	};
}
