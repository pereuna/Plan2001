// Plan2001's wasm32 machine: the platform, its firmware (docs/architecture.md,
// phase C).  The kernel (9wasm32.wasm, 3c and 3l -k) imports its memory -
// shared, one for all its Workers - and the functions of platform.h, which
// it calls as any C function: their arguments are in memory at SP, results
// go back in RET (wasm32's calling convention).  Each Worker is a CPU: the
// first runs main, each newproc another (a kproc).  The page (kernel.html)
// is the machine's front: it makes the Workers and is #t/eia0's other end.
//
//   import { boot } from './platform.js'; boot('9wasm32.wasm', { eia(bytes), halt(why) })
// For tests, window.monolith: eia0bytes, eia0out, eia0b64, eia0in.

const PAGES = 1024, MAXPAGES = 16384;	// 64 MB to start; 3l -k's maximum

// the kernel's imports on a Worker: env { mem, x (its exports, once there), post }
function imports(env) {
	const dv = () => new DataView(env.mem.buffer);
	const i32 = () => new Int32Array(env.mem.buffer);
	const arg = (i) => dv().getUint32(env.x.sp.value + 4*i, true);
	const iarg = (i) => dv().getInt32(env.x.sp.value + 4*i, true);
	const str = (p) => { const b = new Uint8Array(env.mem.buffer); let e = p; while (b[e]) e++; return new TextDecoder().decode(b.slice(p, e)); };
	const fns = {
		eiaout: () => env.post({ eia: new Uint8Array(env.mem.buffer).slice(arg(0), arg(0) + iarg(1)) }),
		eiain: () => { env.x.retw.value = 0; },	// C2: the page's ring
		newproc: () => env.post({ spawn: { fn: arg(0), arg: arg(1), sp: arg(2) } }),
		pwait: () => {
			const r = Atomics.wait(i32(), arg(0) >> 2, iarg(1), iarg(2) < 0 ? Infinity : iarg(2));
			env.x.retw.value = r === 'ok' ? 0 : r === 'timed-out' ? 1 : 2;
		},
		pwake: () => { env.x.retw.value = Atomics.notify(i32(), arg(0) >> 2, iarg(1)); },
		pnsec: () => { env.x.retv.value = BigInt(Math.round(performance.timeOrigin * 1e6 + performance.now() * 1e6)); },
		phalt: () => env.post({ halt: arg(0) ? str(arg(0)) : '' }),
	};
	// a function the kernel wants and the platform has not: say which
	const platform = new Proxy(fns, { get: (o, k) => k === 'memory' ? env.mem : o[k] ?? (() => { throw new Error('platform: ' + String(k) + ' not here'); }) });
	return { plan9: { syscall: () => -1n }, platform };
}

// on a Worker: the kernel, and what this CPU runs
function cpu({ module, mem, role, fn, arg, sp }) {
	const env = { mem, x: null, post: (m) => postMessage(m) };
	try {
		const inst = new WebAssembly.Instance(module, imports(env));
		env.x = inst.exports;
		if (role === 'boot') {
			env.x._init();		// the kernel's data, once
			env.x._start();		// main
		} else {
			const top = (sp - 16) & ~7;
			new DataView(mem.buffer).setUint32(top, arg, true);
			env.x.sp.value = top;
			env.x.table.get(fn)();
		}
	} catch (e) {
		postMessage({ halt: 'platform: ' + e + (e.stack ? ' ' + e.stack : '') });
	}
}

if (typeof WorkerGlobalScope !== 'undefined' && self instanceof WorkerGlobalScope)
	self.onmessage = (e) => cpu(e.data);

// on the page: the machine
export async function boot(url, front = {}) {
	const module = await WebAssembly.compileStreaming(fetch(url));
	const mem = new WebAssembly.Memory({ initial: PAGES, maximum: MAXPAGES, shared: true });
	const eia = [];
	const me = import.meta.url;
	const spawn = (job) => {
		const w = new Worker(me, { type: 'module' });
		w.onmessage = (e) => {
			const m = e.data;
			if (m.eia) { eia.push(m.eia); front.eia?.(m.eia); }
			if (m.spawn) spawn({ module, mem, role: 'proc', ...m.spawn });
			if (m.halt !== undefined) { console.log('KERNEL-HALT ' + m.halt); front.halt?.(m.halt); }
		};
		w.onerror = (e) => console.log('KERNEL-HALT platform: worker: ' + e.message);
		w.postMessage(job);
	};
	window.monolith = {
		eia0bytes: () => { const b = new Uint8Array(eia.reduce((n, c) => n + c.length, 0)); let o = 0; for (const c of eia) { b.set(c, o); o += c.length; } return b; },
		eia0out: () => new TextDecoder().decode(window.monolith.eia0bytes()),
		eia0b64: () => { let s = ''; for (const c of window.monolith.eia0bytes()) s += String.fromCharCode(c); return btoa(s); },
		eia0in: () => -1,	// C2
	};
	spawn({ module, mem, role: 'boot' });
}
