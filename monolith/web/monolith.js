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
	const socks = {};	/* conn -> WebSocket or Link */
	let wasm = null, flushes = 0, resumes = 0;
	const log = opts.log || (() => {});

	/*
	 * A connection that survives its WebSocket: Plan2001's webterm keeps
	 * the rcpu session and gives a token ('s TOKEN').  Both sides count
	 * the bytes they have received and acknowledge them ('a N'); what is
	 * sent stays here until acknowledged.  When the WebSocket drops, a new
	 * one to /resume/TOKEN/N (N: bytes we have) gets 'r M' (bytes the
	 * session has), and we send again what it lacks.  drawterm sees one
	 * connection throughout; it is closed only when resuming fails for
	 * Giveup ms.  What is kept here is bounded: acknowledgements go to
	 * drawterm (netacked), which stops sending past its limit.
	 */
	const Ackevery = 16384, Giveup = 10 * 60 * 1000;
	class Link {
		constructor(conn, gen, url) {
			this.conn = conn; this.gen = gen;
			this.origin = url.replace(/\/rcpu$/, '');
			this.token = null;
			this.rcvd = 0; this.acked = 0;	/* from the session */
			this.sent = []; this.sentbase = 0;	/* not acknowledged: chunks from sentbase */
			this.ws = null; this.live = false; this.opened = false;
			this.closing = false; this.lost = 0; this.retry = null; this.tries = 0;
			this.acktimer = setInterval(() => this.ack(false), 2000);
			this.connect(url);
		}
		connect(url) {
			const ws = new WebSocket(url);
			ws.binaryType = 'arraybuffer';
			this.ws = ws;
			ws.onmessage = e => this.message(ws, e.data);
			ws.onclose = () => this.closed(ws);
		}
		message(ws, d) {
			if (ws !== this.ws) return;
			if (typeof d === 'string') {
				const [op, arg] = d.split(' ');
				if (op === 's') this.token = arg;
				else if (op === 'e') this.ended = true;	/* no session to resume */
				else if (op === 'a') this.trim(+arg);
				else if (op === 'r') {
					this.trim(+arg);
					for (const c of this.sent) ws.send(c);
					this.live = true; this.lost = 0; this.tries = 0;
					if (!this.opened) { this.opened = true; if (wasm) wasm.netstate(this.conn, this.gen, 3); }
					else { resumes++; log('resumed'); }
				}
				return;
			}
			const b = new Uint8Array(d);
			this.rcvd += b.length;
			if (wasm) wasm.netdata(this.conn, this.gen, b);
			if (this.rcvd - this.acked >= Ackevery) this.ack(true);
		}
		ack(now) {
			if (this.live && this.rcvd !== this.acked && this.ws.readyState === 1) {
				this.acked = this.rcvd;
				this.ws.send('a ' + this.rcvd);
			}
		}
		trim(n) {
			while (this.sent.length && this.sentbase + this.sent[0].length <= n) {
				this.sentbase += this.sent[0].length;
				this.sent.shift();
			}
			if (this.sent.length && this.sentbase < n) {
				this.sent[0] = this.sent[0].slice(n - this.sentbase);
				this.sentbase = n;
			}
			/* drawterm waits once too much is unacknowledged (wsock.c) */
			if (wasm && wasm.netacked) wasm.netacked(this.conn, this.gen, this.sentbase % 2 ** 32);
		}
		send(bytes) {
			this.sent.push(bytes);
			if (this.live && this.ws.readyState === 1) this.ws.send(bytes);
		}
		closed(ws) {
			if (ws !== this.ws) return;
			this.live = false;
			if (this.closing) return;
			if (!this.token || this.ended) { this.finish(); return; }
			if (!this.lost) { this.lost = Date.now(); log('connection lost, resuming'); }
			if (Date.now() - this.lost > Giveup) { this.finish(); return; }
			const wait = Math.min(10000, 500 * 2 ** this.tries++);
			clearTimeout(this.retry);
			this.retry = setTimeout(() => this.resume(), wait);
		}
		resume() {
			if (this.closing || this.live) return;
			if (this.ws && this.ws.readyState <= 1) return;	/* one at a time */
			this.connect(`${this.origin}/resume/${this.token}/${this.rcvd}`);
		}
		wake() {
			if (this.closing || this.live || !this.token) return;
			this.tries = 0;
			clearTimeout(this.retry);
			this.resume();
		}
		drop() {
			if (this.ws) this.ws.close();
		}
		finish() {
			clearInterval(this.acktimer);
			clearTimeout(this.retry);
			log('connection closed');
			if (socks[this.conn] === this) delete socks[this.conn];
			if (wasm) wasm.netstate(this.conn, this.gen, 4);
		}
		close() {
			this.closing = true;
			clearInterval(this.acktimer);
			clearTimeout(this.retry);
			if (this.ws && this.ws.readyState === 1) this.ws.send('x');
			if (this.ws) this.ws.close();
		}
	}

	return {
		/* drawterm's exports: input, netstate, netdata */
		attach(exports) {
			wasm = exports;
			if (opts.onready) opts.onready();
		},
		get ready() { return wasm !== null; },
		get flushes() { return flushes; },
		get resumes() { return resumes; },
		/* tests: the resumable sessions' tokens */
		get tokens() { return Object.values(socks).filter(l => l instanceof Link && l.token).map(l => l.token); },

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
		   socket's late events from the new one's in the same slot.  rcpu
		   over wss (url .../rcpu) is a resumable link (below). */
		netopen(conn, gen, url) {
			if (/^wss:.*\/rcpu$/.test(url)) {
				socks[conn] = new Link(conn, gen, url);
				return;
			}
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
			if (ws instanceof Link) ws.send(bytes);
			else if (ws && ws.readyState === 1) ws.send(bytes);
		},
		netclose(conn) {
			const ws = socks[conn];
			if (!ws) return;
			delete socks[conn];
			if (ws instanceof Link) ws.close();
			else ws.close();
		},

		/* tests: drop the WebSockets as a phone's browser does */
		dropnet() {
			for (const k in socks)
				if (socks[k] instanceof Link) socks[k].drop();
		},
		/* the page is back (visible, online): resume at once */
		wake() {
			for (const k in socks)
				if (socks[k] instanceof Link) socks[k].wake();
		},
	};
}
