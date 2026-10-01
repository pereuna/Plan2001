// Plan2001's wasm32 machine: the platform, its firmware (docs/architecture.md,
// phase C).  The kernel (9wasm32.wasm, 3c and 3l -k) imports its memory -
// shared, one for all its Workers - and the functions of platform.h, which
// it calls as any C function: their arguments are in memory at SP, results
// go back in RET (wasm32's calling convention).  Each Worker is a CPU: the
// first runs main, each platnewproc another (a kproc).  The page (kernel.html)
// is the machine's front: it makes the Workers and is #t/eia0's other end.
//
//   import { boot } from './platform.js'; boot('9wasm32.wasm', { eia(bytes), halt(why) })
// For tests, window.monolith: eia0bytes, eia0out, eia0b64, eia0in.

const PAGES = 1024, MAXPAGES = 16384;	// 64 MB to start; 3l -k's maximum

// exec's: the program's frames unwind to platuser's loop, which starts the next
const EXEC = { exec: true };

// a kernel function, fn(words...), from JS on this Worker: below the kernel's SP
function kcall(env, fn, ...w) {
	const x = env.x, k = x.sp.value, top = (k - 8 - 4*w.length) & ~7, d = new DataView(env.mem.buffer);
	w.forEach((v, i) => d.setUint32(top + 4*i, v, true));
	x.sp.value = top;
	x.table.get(fn)();
	x.sp.value = k;
}

// the program on this Worker (platuser): its module, its memory its own;
// plan9.syscall(n, a) is the kernel's syscall(n, a) in this Worker's instance
function usys(env, fn, n, a) {
	kcall(env, fn, n, a);
	if (env.exec) throw EXEC;
	return env.x.retv.value;
}

// env.exec: the program to start - { module, args, argc } from exec,
// { module, snap, asptr } a fork's child's (its memory, rewound with 0)
function runuser(env, fn) {
	for (;;) {
		const job = env.exec;
		env.exec = null;
		if (!job) throw new Error('platuser: no program');
		const inst = new WebAssembly.Instance(job.module, { plan9: { syscall: (n, a) => usys(env, fn, n, a) } });
		const u = { x: inst.exports, mem: inst.exports.memory };
		env.user = u;
		if (job.snap) {
			if (job.snap.length > u.mem.buffer.byteLength)
				u.mem.grow(Math.ceil((job.snap.length - u.mem.buffer.byteLength) / 65536));
			new Uint8Array(u.mem.buffer).set(job.snap);
			u.x.asptr.value = job.asptr;
			u.x.asstate.value = 2;
			u.x.asret.value = 0n;
		} else {
			// argc, argv[0] ... nil at sp; the strings above them, as Plan 9's kernel does
			const b = new Uint8Array(u.mem.buffer), d = new DataView(u.mem.buffer);
			let top = u.x.sp.value;
			const ptrs = [];
			for (let i = 0, o = 0; i < job.argc; i++) {
				let e = o;
				while (job.args[e]) e++;
				top -= e - o + 1;
				b.set(job.args.subarray(o, e + 1), top);
				ptrs.push(top);
				o = e + 1;
			}
			top &= ~7;
			top -= 4 * (ptrs.length + 2);
			top &= ~7;
			d.setInt32(top, ptrs.length, true);
			ptrs.forEach((p, i) => d.setInt32(top + 4 + 4*i, p, true));
			d.setInt32(top + 4 + 4*ptrs.length, 0, true);
			u.x.sp.value = top;
		}
		try {
			for (;;) {
				u.x._start();
				const f = env.fork;
				if (!f) {
					// returned without exits: exits(nil)
					const z = (u.x.sp.value - 8) & ~7;
					new DataView(u.mem.buffer).setUint32(z, 0, true);
					usys(env, fn, 8, z);
				}
				// fork: unwound - a copy of the memory goes with the child's Worker (ready, platnewproc)
				env.fork = null;
				env.forkimage = { module: job.module, snap: new Uint8Array(u.mem.buffer).slice(), asptr: u.x.asptr.value };
				kcall(env, f.ready, f.p);
				env.forkimage = null;
				u.x.asstate.value = 2;
				u.x.asret.value = BigInt(f.pid);
			}
		} catch (e) {
			if (e !== EXEC) throw e;
		}
	}
}

// the kernel's imports on a Worker: env { mem, x (its exports, once there), post, user, exec, boot }
function imports(env) {
	const dv = () => new DataView(env.mem.buffer);
	const i32 = () => new Int32Array(env.mem.buffer);
	const arg = (i) => dv().getUint32(env.x.sp.value + 4*i, true);
	const iarg = (i) => dv().getInt32(env.x.sp.value + 4*i, true);
	const str = (p) => { const b = new Uint8Array(env.mem.buffer); let e = p; while (b[e]) e++; return new TextDecoder().decode(b.slice(p, e)); };
	const fns = {
		eiaout: () => env.post({ eia: new Uint8Array(env.mem.buffer).slice(arg(0), arg(0) + iarg(1)) }),
		eiaring: () => env.post({ ring: arg(0) }),	// uartwasm32.c's: r, w, b[8192]
		platnewproc: () => {
			const user = env.forkimage;
			env.forkimage = null;
			env.post({ spawn: { fn: arg(0), arg: arg(1), sp: arg(2), user } }, user ? [user.snap.buffer] : []);
		},
		platwait: () => {
			const r = Atomics.wait(i32(), arg(0) >> 2, iarg(1), iarg(2) < 0 ? Infinity : iarg(2));
			env.x.retw.value = r === 'ok' ? 0 : r === 'timed-out' ? 1 : 2;
		},
		platwake: () => { env.x.retw.value = Atomics.notify(i32(), arg(0) >> 2, iarg(1)); },
		platnsec: () => { env.x.retv.value = BigInt(Math.round(performance.timeOrigin * 1e6 + performance.now() * 1e6)); },
		platrandom: () => {
			const b = new Uint8Array(iarg(1));	/* getRandomValues takes no shared memory */
			for (let o = 0; o < b.length; o += 65536) crypto.getRandomValues(b.subarray(o, Math.min(o + 65536, b.length)));
			new Uint8Array(env.mem.buffer).set(b, arg(0));
		},
		platlog: () => env.post({ log: str(arg(0)) }),
		plathalt: () => env.post({ halt: arg(0) ? str(arg(0)) : '' }),
		platexec: () => {
			const k = new Uint8Array(env.mem.buffer);
			try {
				const module = new WebAssembly.Module(k.slice(arg(0), arg(0) + iarg(1)));
				env.exec = { module, args: k.slice(arg(2), arg(2) + iarg(3)), argc: iarg(4) };
				env.x.retw.value = 0;
			} catch (e) {
				env.x.retw.value = -1;
			}
		},
		platuser: () => runuser(env, arg(0)),
		platcopyin: () => {
			const u = env.user, a = arg(1), n = iarg(2);
			if (!u || n < 0 || a + n > u.mem.buffer.byteLength) { env.x.retw.value = -1; return; }
			new Uint8Array(env.mem.buffer).set(new Uint8Array(u.mem.buffer, a, n), arg(0));
			env.x.retw.value = 0;
		},
		platcopyout: () => {
			const u = env.user, a = arg(0), n = iarg(2);
			if (!u || n < 0 || a + n > u.mem.buffer.byteLength) { env.x.retw.value = -1; return; }
			new Uint8Array(u.mem.buffer).set(new Uint8Array(env.mem.buffer, arg(1), n), a);
			env.x.retw.value = 0;
		},
		platustrlen: () => {
			const u = env.user, a = arg(0), max = iarg(1);
			if (!u) { env.x.retw.value = -1; return; }
			const b = new Uint8Array(u.mem.buffer), e = Math.min(b.length, a + max);
			let i = a;
			while (i < e && b[i]) i++;
			env.x.retw.value = i < e ? i - a : -1;
		},
		platfork: () => {
			env.fork = { p: arg(0), ready: arg(1), pid: arg(2) };
			env.user.x.asstate.value = 1;	/* unwind when the call returns */
		},
		platbrk: () => {
			const u = env.user, want = arg(0), have = u.mem.buffer.byteLength;
			try {
				if (want > have) u.mem.grow(Math.ceil((want - have) / 65536));
				env.x.retw.value = 0;
			} catch (e) {
				env.x.retw.value = -1;
			}
		},
		platbootfs: () => {
			const a = env.boot?.fs ?? new Uint8Array(0);
			if (arg(0)) new Uint8Array(env.mem.buffer).set(a.subarray(0, iarg(1)), arg(0));
			env.x.retw.value = a.length;
		},
		platbootargs: () => {	/* as platbootfs: its size, then into the buffer */
			const a = new TextEncoder().encode((env.boot?.args ?? []).map((s) => s + '\0').join(''));
			if (arg(0)) new Uint8Array(env.mem.buffer).set(a.subarray(0, iarg(1)), arg(0));
			env.x.retw.value = a.length;
		},
	};
	// a function the kernel wants and the platform has not: say which
	const platform = new Proxy(fns, { get: (o, k) => k === 'memory' ? env.mem : o[k] ?? (() => { throw new Error('platform: ' + String(k) + ' not here'); }) });
	return { plan9: { syscall: () => -1n }, platform };
}

// on a Worker: the kernel, and what this CPU runs
function cpu({ module, mem, role, fn, arg, sp, boot, user }) {
	const env = { mem, x: null, post: (m, t) => postMessage(m, t ?? []), user: null, exec: null, fork: null, forkimage: null, boot };
	if (user) env.exec = user;	/* a fork's child */
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
			// the proc is Dead and this Worker out of the kernel: its Proc and KSTACK are free (newproc)
			const gone = env.x.retw.value;
			if (gone) {
				const i32 = new Int32Array(mem.buffer);
				Atomics.store(i32, gone >> 2, 1);
				Atomics.notify(i32, gone >> 2);
			}
			close();
		}
	} catch (e) {
		postMessage({ halt: 'platform: ' + e + (e.stack ? ' ' + e.stack : '') });
	}
}

if (typeof WorkerGlobalScope !== 'undefined' && self instanceof WorkerGlobalScope)
	self.onmessage = (e) => cpu(e.data);

// on the page: the machine
// front: { eia(bytes), halt(why), fs (the root's archive, rootfs.c: the boot Worker's), args (init's argv: every Worker's) }
export async function boot(url, front = {}) {
	const module = await WebAssembly.compileStreaming(fetch(url));
	const mem = new WebAssembly.Memory({ initial: PAGES, maximum: MAXPAGES, shared: true });
	const eia = [];
	let ring = 0;		/* #t/eia0's input: the kernel's ring */
	const me = import.meta.url;
	const spawn = (job) => {
		const w = new Worker(me, { type: 'module' });
		w.onmessage = (e) => {
			const m = e.data;
			if (m.eia) { eia.push(m.eia); front.eia?.(m.eia); }
			if (m.spawn) spawn({ module, mem, role: 'proc', boot: { args: front.args }, ...m.spawn });
			if (m.log !== undefined) console.log('KLOG ' + m.log);
			if (m.ring !== undefined) ring = m.ring;
			if (m.halt !== undefined) { console.log('KERNEL-HALT ' + m.halt); front.halt?.(m.halt); }
		};
		w.onerror = (e) => console.log('KERNEL-HALT platform: worker: ' + e.message);
		w.postMessage(job, job.user ? [job.user.snap.buffer] : []);
	};
	window.monolith = {
		eia0bytes: () => { const b = new Uint8Array(eia.reduce((n, c) => n + c.length, 0)); let o = 0; for (const c of eia) { b.set(c, o); o += c.length; } return b; },
		eia0out: () => new TextDecoder().decode(window.monolith.eia0bytes()),
		eia0b64: () => { let s = ''; for (const c of window.monolith.eia0bytes()) s += String.fromCharCode(c); return btoa(s); },
		eia0in: (x) => {
			if (!ring) return -1;
			const b = typeof x === 'string' ? new TextEncoder().encode(x) : new Uint8Array(x);
			const i32 = new Int32Array(mem.buffer), u8 = new Uint8Array(mem.buffer), N = 8192;
			let w = Atomics.load(i32, (ring + 4) >> 2), n = 0;
			for (const c of b) {
				if (w - Atomics.load(i32, ring >> 2) >= N) break;
				u8[ring + 8 + (w % N)] = c;
				w++;
				n++;
			}
			Atomics.store(i32, (ring + 4) >> 2, w);
			Atomics.notify(i32, (ring + 4) >> 2);
			return n;
		},
		eia0inb64: (b) => window.monolith.eia0in(Uint8Array.from(atob(b), (c) => c.charCodeAt(0))),
	};
	spawn({ module, mem, role: 'boot', boot: { fs: front.fs, args: front.args } });
}
