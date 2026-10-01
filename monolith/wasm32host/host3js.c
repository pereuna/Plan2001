/*
 * host3's platform side (host3.c is the kernel's): the program's module
 * and memory, in JavaScript - copyin and copyout as DMA, brk, the module
 * run in this Worker, kept there between runs (fork rewinds it).  Not
 * with drawterm's headers: they define long.
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

/* after this system call the program stops: exits, exec */
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
 * run the program: mode 0 a new one (img, argc, argv at its sp as Plan
 * 9's kernel puts them), 1 a forked child (img, the parent's memory
 * snap, rewound with asret), 2 the one here, rewound with asret.  Then
 * out[0]: 0 it exited, 1 it unwound for fork (out[1] the flags, out[2]
 * its memory in ours - malloc'd - out[3] its size, out[4] asptr), 2 exec
 */
EM_JS(int, h3run, (int mode, unsigned char *img, int nimg, int argc, char **argv,
	unsigned char *snap, int nsnap, unsigned asptr, vlong *asret, vlong *ret, int *out, char *msg, int nmsg), {
	const fail = (t) => { stringToUTF8(String(t), msg, nmsg); return -1; };
	/* one for the Worker: a forked process goes on with the closure made when it started */
	const Stop = globalThis.h3stop ??= { stop: true };
	let h = globalThis.h3;
	try {
		if (mode != 2) {
			const bytes = new Uint8Array(wasmMemory.buffer, img, nimg).slice();
			h = globalThis.h3 = { stop: 0, fork: 0, mem: null, inst: null };
			h.inst = new WebAssembly.Instance(new WebAssembly.Module(bytes), { plan9: { syscall: (n, a) => {
				_h3syscall1(n, a >>> 0);
				if (h.stop) throw Stop;
				return new DataView(wasmMemory.buffer).getBigInt64(ret, true);
			} } });
			h.mem = h.inst.exports.memory;
		}
		const x = h.inst.exports;
		if (mode == 0) {
			const b = new Uint8Array(h.mem.buffer), d = new DataView(h.mem.buffer), k = new DataView(wasmMemory.buffer);
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
			ptrs.forEach((p, i) => d.setInt32(top + 4 + 4*i, p, true));
			d.setInt32(top + 4 + 4*argc, 0, true);
			x.sp.value = top;
		} else {
			if (mode == 1) {
				if (nsnap > h.mem.buffer.byteLength)
					h.mem.grow(Math.ceil((nsnap - h.mem.buffer.byteLength) / 65536));
				new Uint8Array(h.mem.buffer).set(new Uint8Array(wasmMemory.buffer, snap, nsnap));
				x.asptr.value = asptr;
			}
			x.asstate.value = 2;
			x.asret.value = new DataView(wasmMemory.buffer).getBigInt64(asret, true);
		}
		h.stop = 0;
		h.fork = 0;
		try {
			x._start();
		} catch (e) {
			if (e !== Stop) throw e;
		}
		const o = (i, v) => new DataView(wasmMemory.buffer).setInt32(out + 4*i, v, true);
		if (h.stop == 2) { o(0, 2); return 0; }
		if (h.fork && !h.stop) {
			const n = h.mem.buffer.byteLength, p = _malloc(n);
			if (!p) return fail('no memory for fork');
			new Uint8Array(wasmMemory.buffer).set(new Uint8Array(h.mem.buffer), p);
			o(0, 1); o(1, h.fork); o(2, p); o(3, n); o(4, x.asptr.value);
			return 0;
		}
		o(0, 0);
		return 0;
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

/* the kernel's (host3.c) */
extern void h3syscall(int, unsigned);

EMSCRIPTEN_KEEPALIVE void
h3syscall1(int n, unsigned a)
{
	h3syscall(n, a);
}
