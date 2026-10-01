/*
 * host3's platform side (host3.c is the kernel's): the program's module
 * and memory, in JavaScript - copyin and copyout as DMA, brk, the module
 * run in this Worker, kept there between runs (fork rewinds it), and its
 * procs (rfork RFMEM) taking turns on it.  Not with drawterm's headers:
 * they define long.
 *
 * Contexts (libthread's threads): a proc's stacks it switches between
 * itself - _ctxnew(fn, arg, stk, n) one on stk (saved frames at its
 * bottom), _ctxswitch(id) to it: the current unwound, it rewound (or
 * started: fn(arg)), _ctxself, _ctxfree.  System calls 100-103, the
 * platform's own (libc/wasm32/ctx.c).
 */
#include <emscripten.h>

typedef long long vlong;

EM_JS(int, h3in, (void *k, unsigned u, int n), {
	const h = globalThis.h3;
	if (n < 0 || u + n > h.mem.buffer.byteLength) return -1;
	new Uint8Array(wasmMemory.buffer).set(new Uint8Array(h.mem.buffer, u, n), k);
	return 0;
});

EM_JS(int, h3out, (unsigned u, const void *k, int n), {
	const h = globalThis.h3;
	if (n < 0 || u + n > h.mem.buffer.byteLength) return -1;
	new Uint8Array(h.mem.buffer).set(new Uint8Array(wasmMemory.buffer, k, n), u);
	return 0;
});

/* a string's length in the program's memory, -1 if none within max */
EM_JS(int, h3strlen, (unsigned u, int max), {
	const b = new Uint8Array(globalThis.h3.mem.buffer);
	for (let i = 0; i < max && u + i < b.length; i++)
		if (b[u + i] == 0) return i;
	return -1;
});

EM_JS(int, h3brk, (unsigned want), {
	const m = globalThis.h3.mem, have = m.buffer.byteLength;
	if (want > have) {
		try { m.grow(Math.ceil((want - have) / 65536)); } catch (e) { return -1; }
	}
	return 0;
});

/* after this system call the proc stops: exits, exec */
EM_JS(void, h3stop, (int why), {
	globalThis.h3.stop = why;
});

/* fork: after this system call the stack unwinds (3l) */
EM_JS(void, h3unwind, (int flags), {
	const h = globalThis.h3;
	h.fork = flags;
	h.inst.exports.asstate.value = 1;
});

/*
 * run the program (p, the process's first proc): mode 0 a new one (img,
 * argc, argv at its sp as Plan 9's kernel puts them), 1 a forked child
 * (img, the parent's memory snap, rewound with asret from asptr; asbase,
 * stacktop the forking proc's, 0 the program's own), 2 the one here, its
 * current proc rewound with asret.  Then out[0]: 0 it exited, 1 it
 * unwound for fork (out[1] the flags, out[2] its memory in ours -
 * malloc'd - out[3] its size, out[4] asptr, out[5] the proc, out[6]
 * asbase, out[7] stacktop, out[8] its context's function), 2 exec.  rfork(RFMEM) procs are scheduled
 * here: a call that blocks goes to the proc's helper (host3.c h3start)
 * and the next proc that can go, goes.
 */
EM_JS(int, h3run, (void *p, int mode, unsigned char *img, int nimg, int argc, char **argv,
	unsigned char *snap, int nsnap, unsigned asptr, unsigned asbase, unsigned stacktop, unsigned ctxfn,
	vlong *asret, int *out, char *msg, int nmsg), {
	const fail = (t) => { stringToUTF8(String(t), msg, nmsg); return -1; };
	const Stop = globalThis.h3stopobj ??= { stop: true };
	const RFMEM = 1<<5;
	const k32 = () => new DataView(wasmMemory.buffer);
	const ret64 = (c) => k32().getBigInt64(c, true);
	let h = globalThis.h3;
	try {
		if (mode != 2) {
			const bytes = new Uint8Array(wasmMemory.buffer, img, nimg).slice();
			h = globalThis.h3 = { stop: 0, fork: 0, block: 0, mem: null, inst: null, multi: false, cos: [], cur: 0, ctxs: [], regions: new Map() };
			h.inst = new WebAssembly.Instance(new WebAssembly.Module(bytes), { plan9: { syscall: (n, a) => {
				const c = h.cos[h.cur];
				a >>>= 0;
				if (n >= 100 && n <= 103)
					return ctxcall(c, n, a);
				if (n == 105)
					return yieldcall(c);
				if (!h.multi) {
					_h3sys1(c.ptr, n, a);
					if (h.stop) throw Stop;
					return ret64(c.ptr);
				}
				const pending = _h3start(c.ptr, n, a);
				if (h.stop) throw Stop;
				if (h.fork || !pending) return ret64(c.ptr);
				h.block = 1;			/* to its helper: unwind, the next goes */
				h.inst.exports.asstate.value = 1;
				return 0n;
			} } });
			h.mem = h.inst.exports.memory;
		}
		const x = h.inst.exports;
		const m32 = () => new DataView(h.mem.buffer);
		const tbl = x.table;
		const jt = (t) => {	/* ?debug=4: the scheduler's steps, on the page's console */
			if (!globalThis.h3jdebug) return;
			const n = lengthBytesUTF8(t) + 1, q = _malloc(n);
			stringToUTF8(t, q, n);
			_h3trace(q);
			_free(q);
		};
		/* the contexts' calls (libc/wasm32/ctx.c): 100 new, 101 switch, 102 free, 103 self */
		globalThis.h3ctxcall = (c, n, a) => {
			const d = m32(), arg = (i) => d.getUint32(a + 4*i, true);
			switch (n) {
			case 100: {
				const fn = arg(0), farg = arg(1), stk = arg(2), size = arg(3);
				let id = h.ctxs.findIndex((t) => t == null);
				if (id < 0) id = h.ctxs.length;
				h.ctxs[id] = { fn, arg: farg, base: stk, top: (stk + size) & ~15, asptr: stk, started: false };
				return BigInt(id);
			}
			case 101: {
				/* a context: the memory's; one not started goes with the first proc to switch to it */
				const id = arg(0);
				if (id == c.cx) return 0n;
				if (!h.ctxs[id] || h.cos.some((q) => q !== c && q.cx == id && q.state != 'done')) return -1n;
				h.ctxswitch = id;
				x.asstate.value = 1;
				return 0n;
			}
			case 102:
				if (h.ctxs[arg(0)] && !h.cos.some((q) => q.cx == arg(0) && q.state != 'done')) {
					h.ctxs[arg(0)].dead = true;
					h.ctxs[arg(0)] = null;
				}
				return 0n;
			case 103:
				return BigInt(c.cx);
			}
			return -1n;
		};
		var ctxcall = globalThis.h3ctxcall;
		/*
		 * preemption (3l's _yield, call 105): after QUANTUM branches back; if
		 * the proc has had its slice and another can go, it unwinds here
		 */
		const QUANTUM = 20000, SLICE = 10;
		globalThis.h3yieldcall = (c) => {
			x.preempt.value = h.multi ? QUANTUM : 0x3fffffff;
			if (!h.multi || performance.now() - h.slice < SLICE) return 0n;
			const i32 = new Int32Array(wasmMemory.buffer);
			if (!h.cos.some((q) => q !== c && (q.state == 'ready' || q.state == 'blocked' && Atomics.load(i32, (q.ptr + 12) >> 2))))
				return 0n;
			h.preempt = 1;
			x.asstate.value = 1;
			return 0n;
		};
		var yieldcall = globalThis.h3yieldcall;
		/*
		 * A context's region: its stack [.., top) and saved frames [base, asptr).
		 * rfork(RFMEM) procs share them at the same addresses, as Plan 9's
		 * private stack segments: memory holds one's, the owner; the others'
		 * are kept here (t.saved) and swapped in when they go on.
		 */
		const minspOf = (t) => {
			const d = m32();
			let ms = t.top;
			for (let q = t.asptr; q > t.base; ) {
				const size = d.getInt32(q - 4, true), rec = q - size;
				ms = Math.min(ms, d.getUint32(rec + 4, true));
				q = rec;
			}
			return ms;
		};
		const save = (t) => {
			const ms = minspOf(t), b = new Uint8Array(h.mem.buffer);
			t.saved = { ms, stack: b.slice(ms, t.top), recs: b.slice(t.base, t.asptr) };
		};
		const claim = (t) => {
			const key = t.base + ':' + t.top, r = h.regions.get(key);
			if (!r) { h.regions.set(key, { owner: t }); return; }
			if (r.owner === t) return;
			if (r.owner && !r.owner.dead && r.owner.started) save(r.owner);
			if (t.saved) {
				const b = new Uint8Array(h.mem.buffer);
				b.set(t.saved.stack, t.saved.ms);
				b.set(t.saved.recs, t.base);
				t.saved = null;
			}
			r.owner = t;
		};
		/* go on with c's current context: rewound, or started; the function to call */
		const resume = (c) => {
			const t = h.ctxs[c.cx];
			claim(t);
			jt(`resume pid ${k32().getInt32(c.ptr + 8, true)} ctx ${c.cx} ${t.started ? 'rewind' : 'start'} fn ${t.fn} asptr ${t.asptr} base ${t.base} top ${t.top}`);
			if (!t.started) {
				t.started = true;
				x.asstate.value = 0;
				const sp = (t.top - 16) & ~7;
				m32().setUint32(sp, t.arg, true);
				x.sp.value = sp;
				x.asptr.value = t.base;
				return tbl.get(t.fn);
			}
			x.asptr.value = t.asptr;
			x.asstate.value = 2;
			x.asret.value = c.ret;
			return t.fn ? tbl.get(t.fn) : x._start;
		};
		/* libc's per-proc region (_perproc: _tos, privalloc's): swapped as procs take turns */
		const pp = x.perproc ? x.perproc.value : 0, ppn = x.perprocsize ? x.perprocsize.value : 0;
		const setpid = (pid) => { if (pp && ppn >= 52) m32().setUint32(pp + 48, pid, true); };	/* Tos.pid */
		const ppsave = () => pp ? new Uint8Array(h.mem.buffer, pp, ppn).slice() : null;
		const ppload = (v) => { if (pp && v) new Uint8Array(h.mem.buffer).set(v, pp); };
		let entry = x._start;
		if (mode == 0) {
			const b = new Uint8Array(h.mem.buffer), d = m32(), k = k32();
			let top = x.sp.value;
			const ptrs = [];
			for (let i = 0; i < argc; i++) {
				const s = UTF8ToString(k.getUint32(argv + 4*i, true)), e = new TextEncoder().encode(s + '\0');
				top -= e.length;
				b.set(e, top);
				ptrs.push(top);
			}
			top &= ~7;
			top -= 4 * (argc + 2);
			top &= ~7;
			d.setInt32(top, argc, true);
			ptrs.forEach((q, i) => d.setInt32(top + 4 + 4*i, q, true));
			d.setInt32(top + 4 + 4*argc, 0, true);
			x.sp.value = top;
			setpid(k.getInt32(p + 8, true));
			h.ctxs = [{ fn: 0, base: x.asbase.value, top: x.stacktop.value, asptr: 0, started: true }];
			h.cos = [{ ptr: p, state: 'run', ret: 0n, cx: 0 }];
		} else if (mode == 1) {
			if (nsnap > h.mem.buffer.byteLength)
				h.mem.grow(Math.ceil((nsnap - h.mem.buffer.byteLength) / 65536));
			new Uint8Array(h.mem.buffer).set(new Uint8Array(wasmMemory.buffer, snap, nsnap));
			h.ctxs = [{ fn: ctxfn, base: asbase || x.asbase.value, top: stacktop || x.stacktop.value, asptr, started: true }];
			h.cos = [{ ptr: p, state: 'run', ret: ret64(asret), cx: 0 }];
			setpid(k32().getInt32(p + 8, true));
			entry = resume(h.cos[0]);
		} else {
			const c = h.cos[h.cur];
			c.ret = ret64(asret);
			entry = resume(c);
		}

		/* an rfork(RFMEM) child of c: its stack and saved frames, at the same addresses, kept until it goes */
		const mkchild = (c, flags) => {
			const t = h.ctxs[c.cx];
			const ct = { fn: t.fn, base: t.base, top: t.top, asptr: t.asptr, started: true };
			claim(t);	/* c's is in memory: it owns the region (the first run did not resume) */
			save(ct);	/* the child's copy of it */
			const ptr = _h3newco(c.ptr, flags);
			const priv = ppsave();
			if (priv && priv.length >= 52) new DataView(priv.buffer).setUint32(48, k32().getInt32(ptr + 8, true), true);
			h.ctxs.push(ct);
			return { ptr, state: 'ready', ret: 0n, cx: h.ctxs.length - 1, priv };
		};

		const o = (i, v) => k32().setInt32(out + 4*i, v, true);
		for (;;) {
			h.stop = 0;
			h.fork = 0;
			h.block = 0;
			h.preempt = 0;
			h.ctxswitch = undefined;
			h.slice = performance.now();
			x.preempt.value = h.multi ? QUANTUM : 0x3fffffff;
			try {
				entry();
			} catch (e) {
				if (e !== Stop) throw e;
			}
			let c = h.cos[h.cur];
			jt(`back: stop ${h.stop} fork ${h.fork} block ${h.block} ctxswitch ${h.ctxswitch} asptr ${x.asptr.value} asstate ${x.asstate.value}`);
			if (h.stop == 2) { o(0, 2); return 0; }
			if (h.ctxswitch !== undefined && !h.stop) {
				/* another context of the same proc */
				h.ctxs[c.cx].asptr = x.asptr.value;
				c.cx = h.ctxswitch;
				c.ret = 0n;
				entry = resume(c);
				continue;
			}
			if (h.fork && !h.stop) {
				h.ctxs[c.cx].asptr = x.asptr.value;
				if (h.fork & RFMEM) {
					if (!h.multi) {
						h.multi = true;
						_h3mkhelper(c.ptr);
					}
					const ch = mkchild(c, h.fork);
					if (ch) {
						h.cos.push(ch);
						c.ret = BigInt(k32().getInt32(ch.ptr + 8, true));
					} else
						c.ret = -1n;
					c.state = 'ready';
				} else {
					/* the whole memory to a child process: host3.c h3proc */
					const n = h.mem.buffer.byteLength, q = _malloc(n);
					if (!q) return fail('no memory for fork');
					new Uint8Array(wasmMemory.buffer).set(new Uint8Array(h.mem.buffer), q);
					const t = h.ctxs[c.cx];
					o(0, 1); o(1, h.fork); o(2, q); o(3, n); o(4, t.asptr); o(5, c.ptr); o(6, t.base); o(7, t.top); o(8, t.fn);
					return 0;
				}
			} else if (h.block && !h.stop) {
				h.ctxs[c.cx].asptr = x.asptr.value;
				c.state = 'blocked';
			} else if (h.preempt && !h.stop) {
				/* its slice is up: another goes, it is ready again */
				h.ctxs[c.cx].asptr = x.asptr.value;
				c.state = 'ready';
				c.ret = 0n;
			} else {
				/* exits, or main returned */
				if (!h.multi) { o(0, 0); return 0; }
				c.state = 'done';
				h.ctxs[c.cx].dead = true;
				if (c.ptr != p) _h3coend(c.ptr);
				if (h.cos.every((q) => q.state == 'done')) { o(0, 0); return 0; }
			}
			/* the next to go: round robin, after what its helper finished */
			let next = -1;
			for (;;) {
				const i32 = new Int32Array(wasmMemory.buffer);
				for (const q of h.cos)
					if (q.state == 'blocked' && Atomics.load(i32, (q.ptr + 12) >> 2)) {
						claim(h.ctxs[q.cx]);	/* its results go to its stack: in memory first */
						_h3finish(q.ptr);
						q.ret = ret64(q.ptr);
						q.state = 'ready';
					}
				for (let i = 1; i <= h.cos.length; i++) {
					const j = (h.cur + i) % h.cos.length;
					if (h.cos[j].state == 'ready') { next = j; break; }
				}
				if (next >= 0) break;
				jt('wait for a helper');
				_h3waitdone(p);
			}
			if (next != h.cur) {
				const was = h.cos[h.cur];
				if (was.state != 'done') was.priv = ppsave();
				ppload(h.cos[next].priv);
			}
			h.cur = next;
			c = h.cos[next];
			c.state = 'run';
			entry = resume(c);
		}
	} catch (e) {
		return fail(e + (e.stack ? ' ' + e.stack : ''));
	}
});

/* a process's end, on the page's console (tests: tools/test-wasmapp) */
void
h3log(const char *prog, int bad, const char *msg)
{
	MAIN_THREAD_EM_ASM({ console.log('HOST3-EXIT ' + UTF8ToString($0) + ' ' + ($1 ? UTF8ToString($2) : 'ok')); },
		prog, bad, msg);
}

/*
 * #t/eia0 (devuart3.c): the page's side - bytes, as a serial line: what
 * the kernel writes is kept as it came (h3eia0out, Uint8Arrays), what
 * the page sends goes into the kernel's ring (r, w, b[8192]) as bytes.
 * Text is the reader's business: window.monolith.eia0out() decodes the
 * whole stream as UTF-8, eia0bytes() is the bytes.
 */
void
eiaplatform(void *ring)
{
	MAIN_THREAD_EM_ASM({ ((ring) => {	/* in parentheses: a macro's argument */
		const N = 8192;
		globalThis.h3eia0out = [];
		globalThis.h3eia0in = (x) => {
			const b = typeof x === 'string' ? new TextEncoder().encode(x) : new Uint8Array(x);
			const i32 = new Int32Array(wasmMemory.buffer), u8 = new Uint8Array(wasmMemory.buffer);
			let w = Atomics.load(i32, (ring + 4) >> 2), n = 0;
			for (const c of b) {
				if (w - Atomics.load(i32, ring >> 2) >= N) break;	/* full */
				u8[ring + 8 + (w % N)] = c;
				w++;
				n++;
			}
			Atomics.store(i32, (ring + 4) >> 2, w);
			return n;
		};
	})($0); }, ring);
}

void
eiaout(void *p, int n)
{
	MAIN_THREAD_EM_ASM({ ((p, n) => {
		globalThis.h3eia0out.push(new Uint8Array(wasmMemory.buffer).slice(p, p + n));
	})($0, $1); }, p, n);
}

/* the scheduler's tracing (?debug=4): every Worker */
EM_JS(void, h3jdebug, (void), {
	globalThis.h3jdebug = 1;
});

/* host3's own tracing (?debug=1), on the page's console */
void
h3trace(const char *s)
{
	MAIN_THREAD_EM_ASM({ console.log('HOST3 ' + UTF8ToString($0)); }, s);
}
