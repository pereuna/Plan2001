/*
 * host3's platform side (host3.c is the kernel's): the program's module
 * and memory, in JavaScript - copyin and copyout as DMA, brk, the module
 * run in this Worker, kept there between runs (fork rewinds it), and its
 * procs (rfork RFMEM) taking turns on it.  Not with drawterm's headers:
 * they define long.
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
 * asbase, out[7] stacktop), 2 exec.  rfork(RFMEM) procs are scheduled
 * here: a call that blocks goes to the proc's helper (host3.c h3start)
 * and the next proc that can go, goes.
 */
EM_JS(int, h3run, (void *p, int mode, unsigned char *img, int nimg, int argc, char **argv,
	unsigned char *snap, int nsnap, unsigned asptr, unsigned asbase, unsigned stacktop,
	vlong *asret, int *out, char *msg, int nmsg), {
	const fail = (t) => { stringToUTF8(String(t), msg, nmsg); return -1; };
	const Stop = globalThis.h3stopobj ??= { stop: true };
	const RFMEM = 1<<5, TASAREA = 32*1024;
	const k32 = () => new DataView(wasmMemory.buffer);
	const ret64 = (c) => k32().getBigInt64(c, true);
	let h = globalThis.h3;
	try {
		if (mode != 2) {
			const bytes = new Uint8Array(wasmMemory.buffer, img, nimg).slice();
			h = globalThis.h3 = { stop: 0, fork: 0, block: 0, mem: null, inst: null, multi: false, cos: [], cur: 0, slots: [] };
			h.inst = new WebAssembly.Instance(new WebAssembly.Module(bytes), { plan9: { syscall: (n, a) => {
				const c = h.cos[h.cur];
				a >>>= 0;
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
			h.cos = [{ ptr: p, slot: -1, base: x.asbase.value, top: x.stacktop.value, asptr: 0, state: 'run', ret: 0n }];
		} else if (mode == 1) {
			if (nsnap > h.mem.buffer.byteLength)
				h.mem.grow(Math.ceil((nsnap - h.mem.buffer.byteLength) / 65536));
			new Uint8Array(h.mem.buffer).set(new Uint8Array(wasmMemory.buffer, snap, nsnap));
			h.cos = [{ ptr: p, slot: -1, base: asbase || x.asbase.value, top: stacktop || x.stacktop.value,
				asptr: asptr, state: 'run', ret: ret64(asret) }];
			x.asptr.value = asptr;
			x.asstate.value = 2;
			x.asret.value = ret64(asret);
		} else {
			const c = h.cos[h.cur];
			x.asptr.value = c.asptr;
			x.asstate.value = 2;
			x.asret.value = ret64(asret);
		}

		/* an rfork(RFMEM) child of c: its stack and saved frames a copy in a slot of its own */
		const mkchild = (c, flags) => {
			let s = 0;
			while (h.slots[s]) s++;
			if (s >= x.ntslot.value) return null;
			const d = m32(), b = new Uint8Array(h.mem.buffer);
			const slot = x.tzone.value + s * x.tslot.value;
			const top = slot + x.tslot.value - TASAREA, abase = top;
			/* the stack in use: from the innermost saved frame's SP to c's top */
			let minsp = c.top;
			for (let q = c.asptr; q > c.base; ) {
				const size = d.getInt32(q - 4, true), rec = q - size;
				minsp = Math.min(minsp, d.getUint32(rec + 4, true));
				q = rec;
			}
			const delta = top - c.top;
			if (minsp < c.top - (x.tslot.value - TASAREA)) return null;	/* too deep for a slot */
			b.copyWithin(minsp + delta, minsp, c.top);
			b.copyWithin(abase, c.base, c.asptr);
			const asp = abase + (c.asptr - c.base);
			/* SPs and what looks like a stack address in i32 locals: moved (docs/wasm32.md) */
			for (let q = asp; q > abase; ) {
				const size = d.getInt32(q - 4, true), rec = q - size, ni = d.getInt32(q - 8, true);
				for (let i = 0; i < ni; i++) {
					const v = d.getUint32(rec + 4 + 4*i, true);
					if (v >= minsp && v < c.top)
						d.setUint32(rec + 4 + 4*i, v + delta, true);
				}
				q = rec;
			}
			const ptr = _h3newco(c.ptr, flags);
			h.slots[s] = 1;
			return { ptr, slot: s, base: abase, top, asptr: asp, state: 'ready', ret: 0n };
		};

		const o = (i, v) => k32().setInt32(out + 4*i, v, true);
		for (;;) {
			h.stop = 0;
			h.fork = 0;
			h.block = 0;
			try {
				x._start();
			} catch (e) {
				if (e !== Stop) throw e;
			}
			let c = h.cos[h.cur];
			if (h.stop == 2) { o(0, 2); return 0; }
			if (h.fork && !h.stop) {
				c.asptr = x.asptr.value;
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
					o(0, 1); o(1, h.fork); o(2, q); o(3, n); o(4, c.asptr); o(5, c.ptr); o(6, c.base); o(7, c.top);
					return 0;
				}
			} else if (h.block && !h.stop) {
				c.asptr = x.asptr.value;
				c.state = 'blocked';
			} else {
				/* exits, or main returned */
				if (!h.multi) { o(0, 0); return 0; }
				c.state = 'done';
				if (c.slot >= 0) h.slots[c.slot] = 0;
				if (c.ptr != p) _h3coend(c.ptr);
				if (h.cos.every((q) => q.state == 'done')) { o(0, 0); return 0; }
			}
			/* the next to go: round robin, after what its helper finished */
			let next = -1;
			for (;;) {
				const i32 = new Int32Array(wasmMemory.buffer);
				for (const q of h.cos)
					if (q.state == 'blocked' && Atomics.load(i32, (q.ptr + 12) >> 2)) {
						_h3finish(q.ptr);
						q.ret = ret64(q.ptr);
						q.state = 'ready';
					}
				for (let i = 1; i <= h.cos.length; i++) {
					const j = (h.cur + i) % h.cos.length;
					if (h.cos[j].state == 'ready') { next = j; break; }
				}
				if (next >= 0) break;
				_h3waitdone(p);
			}
			h.cur = next;
			c = h.cos[next];
			c.state = 'run';
			x.asptr.value = c.asptr;
			x.asstate.value = 2;
			x.asret.value = c.ret;
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

/* host3's own tracing (?debug=1), on the page's console */
void
h3trace(const char *s)
{
	MAIN_THREAD_EM_ASM({ console.log('HOST3 ' + UTF8ToString($0)); }, s);
}
