/*
 * host3's platform side (host3.c is the kernel's): the program's module
 * and memory, in JavaScript - copyin and copyout as DMA, brk, the module
 * run in this Worker.  Not with drawterm's headers: they define long.
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

EM_JS(void, h3exit, (const char *msg), {
	globalThis.h3.exit = msg ? UTF8ToString(msg) : '';
});

/*
 * run url's module with argv: argc, argv[0] ... nil at its sp, as
 * Plan 9's kernel puts them; returns when it exits
 */
EM_JS(int, h3run, (const char *curl, int argc, char **argv, vlong *ret, char *msg, int nmsg), {
	const fail = (t) => { stringToUTF8('host3: ' + url + ': ' + t, msg, nmsg); return -1; };
	const url = UTF8ToString(curl);
	let bytes;
	try {
		const x = new XMLHttpRequest();
		x.open('GET', url, false);
		x.responseType = 'arraybuffer';
		x.send();
		if (x.status != 200) return fail('http ' + x.status);
		bytes = x.response;
	} catch (e) { return fail(e); }
	const h = globalThis.h3 = { exit: undefined, mem: null };
	class Exit {}
	let inst;
	try {
		inst = new WebAssembly.Instance(new WebAssembly.Module(bytes), { plan9: { syscall: (n, a) => {
			_h3syscall1(n, a >>> 0);
			if (h.exit !== undefined) throw new Exit();
			const d = new DataView(wasmMemory.buffer);
			return d.getBigInt64(ret, true);
		} } });
	} catch (e) { return fail(e); }
	h.mem = inst.exports.memory;
	const sp = inst.exports.sp, b = new Uint8Array(h.mem.buffer), d = new DataView(h.mem.buffer);
	let top = sp.value;
	const ptrs = [];
	for (let i = 0; i < argc; i++) {
		const s = UTF8ToString(HEAPU32[(argv >> 2) + i]), e = new TextEncoder().encode(s + '\0');
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
	sp.value = top;
	try {
		inst.exports._start();
	} catch (e) {
		if (!(e instanceof Exit)) return fail(e + (e.stack ? ' ' + e.stack : ''));
	}
	return h.exit ? 1 : 0;
});


/* a process's end, on the page's console (tests: tools/test-wasmapp) */
void
h3log(const char *prog, int bad, const char *msg)
{
	MAIN_THREAD_EM_ASM({ console.log('HOST3-EXIT ' + UTF8ToString($0) + ' ' + ($1 ? UTF8ToString($2) : 'ok')); },
		prog, bad, msg);
}

/* the kernel's (host3.c) */
extern void h3syscall(int, unsigned);

EMSCRIPTEN_KEEPALIVE void
h3syscall1(int n, unsigned a)
{
	h3syscall(n, a);
}
