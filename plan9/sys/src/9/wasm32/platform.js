// Plan2001's wasm32 machine: the platform, its firmware (docs/architecture.md,
// phase C).  The kernel (9wasm32.wasm, 3c and 3l -k) imports its memory -
// shared, one for all its Workers - and the functions of platform.h, which
// it calls as any C function: their arguments are in memory at SP, results
// go back in RET (wasm32's calling convention).  Each Worker is a CPU: the
// first runs main, each platnewproc another (a kproc).  The page (kernel.html)
// is the machine's front: it makes the Workers and is #t/eia0's other end.
// As the machine's firmware, boot() hands the kernel what it is as any
// loader does: a BootInfo blob (Plan2001 Boot ABI v1, plan9/sys/include/
// bootinfo.h; docs/boot-abi-wasm32.md), its address _start's argument.
//
//   import { boot } from './platform.js'; boot('9wasm32.wasm', { eia(bytes), halt(why) })
// For tests, window.monolith: eia0bytes, eia0out, eia0b64, eia0in.

const PAGES = 1024, MAXPAGES = 16384;	// 64 MB, the kernel's (its image, its heap); 3l -k's maximum

/*
 * The BootInfo blob (bootinfo.h; docs/boot-abi.md): the same data as the
 * UEFI loader's for amd64 and arm64.  3c lays the structs out as 6c does
 * (u64int 8-aligned, the size a multiple of 8): BootInfo 224 bytes,
 * BootMem 24.  The page puts, above the kernel's 64 MB, the blob, the
 * root's archive and the framebuffer, each page-aligned, and the memory
 * map says so: Conventional [0, 64 MB) - the kernel's module, its data
 * and stack at the bottom, the kernel keeps [0, end) - LoaderData the
 * blob and the archive, Reserved the framebuffer.
 */
const BI = { magic: 0x49423250, version: 1, arch: 4, size: 224, memsize: 24, pg: 4096 };
const BootMemLoaderData = 2, BootMemConventional = 7, BootMemReserved = 0;
const pground = (n) => Math.ceil(n / BI.pg) * BI.pg;

// init's argv as plan9.ini's init=: tokenize's quoting (rc's), a word with ' or blanks in quotes
function initconf(args) {
	if (!args || args.length === 0) return '';
	for (const a of args)
		if (/[\n\r\0]/.test(a)) throw new Error('platform: init\'s argument with a newline: ' + JSON.stringify(a));
	const q = (a) => a !== '' && !/[\s']/.test(a) ? a : "'" + a.replace(/'/g, "''") + "'";
	return 'init=' + args.map(q).join(' ') + '\n';
}

/*
 * front: its config (plan9.ini lines), args (init=), fs (the root's
 * archive), screen ({ w, h }): the machine's memory, the blob in it -
 * { mem, pa (the blob's address), fb ({ addr, stride, w, h }, or null) }
 */
function firmware(front) {
	const config = new TextEncoder().encode(initconf(front.args) + (front.config ?? '') + '\0');
	const heap = PAGES * 65536;
	const rd = front.fs ?? null;
	const sc = front.screen ?? { w: 1024, h: 768 };
	const fbw = sc.w | 0, fbh = sc.h | 0;
	const fbbytes = fbw > 0 && fbh > 0 ? fbw * fbh * 4 : 0;

	/* the blob: header, config, memory map (nmap entries at most) */
	const nmap = 4;
	const configoff = 256, mmapoff = configoff + Math.ceil(config.length / 8) * 8;
	const blobsize = pground(mmapoff + nmap * BI.memsize);
	const pa = heap;
	const rdbase = rd && rd.length ? pa + blobsize : 0;
	const fbbase = fbbytes ? pground(pa + blobsize + (rd ? rd.length : 0)) : 0;
	const top = fbbase ? fbbase + pground(fbbytes) : pground(pa + blobsize + (rd ? rd.length : 0));
	const pages = Math.ceil(top / 65536);
	if (pages > MAXPAGES) throw new Error('platform: the machine wants ' + top + ' bytes, more than wasm32 has');
	const mem = new WebAssembly.Memory({ initial: pages, maximum: MAXPAGES, shared: true });

	const map = [[0, heap, BootMemConventional], [pa, blobsize, BootMemLoaderData]];
	if (rdbase) map.push([rdbase, pground(rd.length), BootMemLoaderData]);
	if (fbbase) map.push([fbbase, pground(fbbytes), BootMemReserved]);

	const u8 = new Uint8Array(mem.buffer), d = new DataView(mem.buffer);
	const w32 = (o, v) => d.setUint32(pa + o, v, true);
	const w64 = (o, v) => d.setBigUint64(pa + o, BigInt(v), true);
	w32(0, BI.magic); w32(4, BI.version); w32(8, BI.size); w32(12, blobsize); w32(16, 0);
	w32(20, configoff); w32(24, config.length);
	w32(28, configoff + config.length); w32(32, 0);		/* logoff, loglen: no loader log */
	w32(36, mmapoff); w32(40, map.length); w32(44, BI.memsize);
	w64(48, 0);						/* acpi: none */
	w64(56, 0);						/* tscfreq: amd64's */
	w64(64, Math.floor(Date.now() / 1000));		/* epoch */
	const seed = new Uint8Array(64);			/* getRandomValues takes no shared memory */
	crypto.getRandomValues(seed);
	u8.set(seed, pa + 72); w32(136, seed.length);
	if (fbbase) {
		w64(144, fbbase); w64(152, fbbytes); w32(160, fbw); w32(164, fbh); w32(168, fbw); w32(172, 32);
		u8.set(new TextEncoder().encode('x8r8g8b8'), pa + 176);
	}
	w32(192, front.badarch || BI.arch);	/* a test's: a blob for another ISA */
	w32(196, configoff + config.length); w32(200, 0);	/* fdtoff, fdtlen: no device tree */
	w64(208, rdbase); w64(216, rdbase ? rd.length : 0);
	u8.set(config, pa + configoff);
	map.forEach(([b, n, t], i) => {
		const o = mmapoff + i * BI.memsize;
		w64(o, b); w64(o + 8, n); w32(o + 16, t); w32(o + 20, 0);
	});
	if (rdbase) u8.set(rd, rdbase);
	return { mem, pa, fb: fbbase ? { addr: fbbase, stride: fbw * 4, w: fbw, h: fbh } : null };
}

// exec's: the program's frames unwind to platuser's loop, which starts the next
const EXEC = { exec: true };
// noted's: the program's notify handler's frames unwind to its caller (unote)
const NOTED = { noted: true };
// a program's fault: it ends with the note, as a trap would on Plan 9 - the machine goes on
class Trap extends Error {}

// a kernel function, fn(words...), from JS on this Worker: below the kernel's SP
function kcall(env, fn, ...w) {
	const x = env.x, k = x.sp.value, top = (k - 8 - 4*w.length) & ~7, d = new DataView(env.mem.buffer);
	w.forEach((v, i) => d.setUint32(top + 4*i, v, true));
	x.sp.value = top;
	x.table.get(fn)();
	x.sp.value = k;
}

// rfork(RFMEM): the memory's last proc is gone - the Worker leaves the kernel and ends
const MEMDONE = { memdone: true };

// the kernel's Ufns (platform.h): its functions for the program's Worker
function ufns(env, a) {
	const d = new DataView(env.mem.buffer), w = (i) => d.getUint32(a + 4*i, true);
	return { syscall: w(0), sysprep: w(1), sysdone: w(2), sysfin: w(3), sysret: w(4), coend: w(5), memend: w(6) };
}

// a note (trap.c, platnote): the program's handler(ureg, msg) below its SP,
// until noted (NOTED), it returns, or it jumps out (notejmp)
function unote(env) {
	const nt = env.note, u = env.user, x = u.x, sp = x.sp.value;
	env.note = null;
	const b = new Uint8Array(u.mem.buffer), d = new DataView(u.mem.buffer);
	let top = sp - 64 - (nt.msg.length + 1);
	b.set(nt.msg, top);
	b[top + nt.msg.length] = 0;
	const msg = top;
	top = (top - 8) & ~7;
	const ureg = top;
	d.setUint32(ureg, 0, true);	/* pc: wasm32 has none to give */
	d.setUint32(ureg + 4, sp, true);
	top -= 16;
	d.setUint32(top, ureg, true);
	d.setUint32(top + 4, msg, true);
	x.sp.value = top;
	if (!(nt.handler > 0 && nt.handler < x.table.length)) {
		/* not a function of this program's: as if it had no handler */
		console.log(`KLOG platform: a note's handler ${nt.handler} is not in this program (table ${x.table.length}): ${new TextDecoder().decode(nt.msg)}`);
		x.sp.value = sp;
		kcall(env, nt.done, nt.p);
		throw new Trap(new TextDecoder().decode(nt.msg));
	}
	try {
		x.table.get(nt.handler)();
	} catch (e) {
		if (e !== NOTED) {
			kcall(env, nt.done, nt.p);
			throw e;
		}
	}
	x.sp.value = sp;
	kcall(env, nt.done, nt.p);
}

// the program on this Worker (platuser), and each one exec makes next
function runuser(env, K, host, pid) {
	for (;;) {
		const job = env.exec;
		env.exec = null;
		env.note = null;	/* the program before's, not this one's */
		if (!job) throw new Error('platuser: no program');
		runprog(env, K, host, pid, job);
	}
}

const QUANTUM = 20000, SLICE = 10, FOREVER = 0x3fffffff;

/*
 * a program: its module, its memory its own.  job: { module, args, argc }
 * from exec, or a fork's child's { module, snap, asptr, base, top, fn }
 * (its memory, its context rewound with 0).  Its procs (rfork RFMEM, the
 * cos) take turns here; while there is one, its calls go to the kernel
 * as they come (syscall), and with more, each is prepared here, done by
 * the proc's helper if it blocks (sysprep, sysdone, sysfin), and the
 * next proc that can go, goes.  Contexts (libthread's threads): a proc's
 * stacks it switches between itself - _ctxnew(fn, arg, stk, n),
 * _ctxswitch(id), _ctxself, _ctxfree (calls 100-103, libc/wasm32/ctx.c).
 * Returns on exec.
 */
function runprog(env, K, host, pid, job) {
	const h = { multi: false, cos: [], cur: 0, ctxs: [], regions: new Map(), memdone: 0,
		block: 0, preempt: 0, ctxswitch: undefined, slice: 0 };
	let x = null;
	const inst = new WebAssembly.Instance(job.module, { plan9: { syscall: (n, a) => sys(n, a >>> 0) } });
	x = inst.exports;
	const mem = x.memory, tbl = x.table;
	env.user = { x, mem };
	const m32 = () => new DataView(mem.buffer);
	const kw = () => env.x.retw.value;

	/* a call's end on the program's side: exec, noted, a note */
	const after = (c, r) => {
		if (env.exec) throw EXEC;
		if (env.noted) { env.noted = false; throw NOTED; }
		if (x.asstate.value === 1) {	/* unwinding (fork): a note waits */
			if (env.note) { c.note = env.note; env.note = null; }
			return r;
		}
		if (!env.note && c.note) { env.note = c.note; c.note = null; }
		if (env.note) unote(env);
		return r;
	};
	const sys = (n, a) => {
		const c = h.cos[h.cur];
		/*
		 * a note that came while the proc was blocked (its call finished
		 * by its helper, it went on rewound: no after() for that call)
		 * goes to its handler now, before its next call - else a proc
		 * that blocks again (exportfs's slaves in rendezvous) never has it
		 */
		if (h.multi && c.note && x.asstate.value === 0) {
			env.note = c.note;
			c.note = null;
			unote(env);
			if (x.asstate.value === 1) return 0n;	/* the handler blocked, or its noted(NDFLT) ends the proc */
		}
		if (n >= 100 && n <= 103)
			return ctxcall(c, n, a);
		if (n === 105) {
			yieldcall(c);
			if (h.preempt) return 0n;
		}
		if (!h.multi) {
			kcall(env, K.syscall, n, a);
			return after(c, env.x.retv.value);
		}
		kcall(env, K.sysprep, c.p, n, a);
		if (kw() === 0) { h.block = 1; x.asstate.value = 1; return 0n; }	/* its helper has it: the next goes */
		kcall(env, K.sysfin, c.p);
		if (kw() === 1) { h.block = 1; x.asstate.value = 1; return 0n; }	/* a note ends it, on its helper */
		kcall(env, K.sysret, c.p);
		return after(c, env.x.retv.value);
	};

	/* the contexts' calls: 100 new, 101 switch, 102 free, 103 self */
	const ctxcall = (c, n, a) => {
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
			/* a context is the memory's; one not started goes with the first proc to switch to it */
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

	/* 3l's preemption point (call 105): another proc goes if this one has had its slice */
	const done = (q) => { kcall(env, K.sysdone, q.p); return kw(); };
	const yieldcall = (c) => {
		x.preempt.value = h.multi ? QUANTUM : FOREVER;
		if (!h.multi || performance.now() - h.slice < SLICE) return;
		if (!h.cos.some((q) => q !== c && (q.state == 'ready' || q.state == 'blocked' && done(q) != 0))) return;
		h.preempt = 1;
		x.asstate.value = 1;
	};

	/*
	 * A context's region: its stack [.., top) and saved frames [base, asptr).
	 * rfork(RFMEM) procs share them at the same addresses, as Plan 9's
	 * private stack segments: memory holds one's, the owner's; the others'
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
		const ms = minspOf(t), b = new Uint8Array(mem.buffer);
		t.saved = { ms, stack: b.slice(ms, t.top), recs: b.slice(t.base, t.asptr) };
	};
	const claim = (t) => {
		const key = t.base + ':' + t.top, r = h.regions.get(key);
		if (!r) { h.regions.set(key, { owner: t }); return; }
		if (r.owner === t) return;
		if (r.owner && !r.owner.dead && r.owner.started) save(r.owner);
		if (t.saved) {
			const b = new Uint8Array(mem.buffer);
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
	const setpid = (v) => { if (pp && ppn >= 52) m32().setUint32(pp + 48, v, true); };	/* Tos.pid */
	const ppsave = () => pp ? new Uint8Array(mem.buffer, pp, ppn).slice() : null;
	const ppload = (v) => { if (pp && v) new Uint8Array(mem.buffer).set(v, pp); };

	let entry = x._start;
	if (job.snap) {
		if (job.snap.length > mem.buffer.byteLength)
			mem.grow(Math.ceil((job.snap.length - mem.buffer.byteLength) / 65536));
		new Uint8Array(mem.buffer).set(job.snap);
		h.ctxs = [{ fn: job.fn, base: job.base || x.asbase.value, top: job.top || x.stacktop.value, asptr: job.asptr, started: true }];
		h.cos = [{ p: host, state: 'run', ret: 0n, cx: 0 }];
		setpid(pid);
		entry = resume(h.cos[0]);
	} else {
		// argc, argv[0] ... nil at sp; the strings above them, as Plan 9's kernel does
		const b = new Uint8Array(mem.buffer), d = m32();
		let top = x.sp.value;
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
		ptrs.forEach((q, i) => d.setInt32(top + 4 + 4*i, q, true));
		d.setInt32(top + 4 + 4*ptrs.length, 0, true);
		x.sp.value = top;
		setpid(pid);
		h.ctxs = [{ fn: 0, base: x.asbase.value, top: x.stacktop.value, asptr: 0, started: true }];
		h.cos = [{ p: host, state: 'run', ret: 0n, cx: 0 }];
	}

	/* an rfork(RFMEM) child of c: its stack and saved frames, at the same addresses, kept until it goes */
	const mkchild = (c, rf) => {
		const t = h.ctxs[c.cx];
		const ct = { fn: t.fn, base: t.base, top: t.top, asptr: t.asptr, started: true };
		claim(t);
		save(ct);
		const priv = ppsave();
		if (priv && priv.length >= 52) new DataView(priv.buffer).setUint32(48, rf.pid, true);
		h.ctxs.push(ct);
		return { p: rf.p, state: 'ready', ret: 0n, cx: h.ctxs.length - 1, priv };
	};

	for (;;) {
		h.block = 0;
		h.preempt = 0;
		h.ctxswitch = undefined;
		env.fork = null;
		env.rfmem = null;
		h.slice = performance.now();
		x.preempt.value = h.multi ? QUANTUM : FOREVER;
		let trap = null;
		try {
			entry();
		} catch (e) {
			if (e === EXEC) return;
			/* the program's fault (WebAssembly's trap, a bad table index, its stack): it ends, not the machine */
			if (!(e instanceof Trap || e instanceof WebAssembly.RuntimeError || e instanceof RangeError || e instanceof TypeError))
				throw e;
			trap = e instanceof Trap ? e.message : 'sys: trap: ' + e.message;
			console.log('KLOG platform: ' + trap);
		}
		let c = h.cos[h.cur];
		if (trap !== null) {
			/* exits(trap): its message at the top of its context's stack, which it has no more use for */
			const t = h.ctxs[c.cx], m = new TextEncoder().encode(trap.slice(0, 120) + '\0');
			const z = (t.top - 256) & ~7;
			new Uint8Array(mem.buffer).set(m, z);
			x.sp.value = z - 64;
			m32().setUint32(z - 8, z, true);
			x.asstate.value = 0;
			sys(8, z - 8);	/* one proc: pexit, no return; with more, its helper has it (h.block) */
			if (!h.block) throw new Error('platform: exits returned');
		}
		if (h.ctxswitch !== undefined) {
			/* another context of the same proc */
			h.ctxs[c.cx].asptr = x.asptr.value;
			c.cx = h.ctxswitch;
			c.ret = 0n;
			entry = resume(c);
			continue;
		}
		if (env.fork) {
			/* fork: a copy of the memory goes with the child's Worker (ready, platnewproc); c goes on */
			const f = env.fork, t = h.ctxs[c.cx];
			t.asptr = x.asptr.value;
			env.forkimage = { module: job.module, snap: new Uint8Array(mem.buffer).slice(),
				asptr: t.asptr, base: t.base, top: t.top, fn: t.fn };
			kcall(env, f.ready, f.p);
			env.forkimage = null;
			c.ret = kw() < 0 ? -1n : BigInt(f.pid);
			entry = resume(c);
			continue;
		}
		if (env.rfmem) {
			/*
			 * rfork(RFMEM): the child a proc of this memory, its calls on its
			 * helper, c's too from now - or, if a Worker could not be made,
			 * neither, and c as it was (sysproc.c rfmemstart)
			 */
			const rf = env.rfmem;
			h.ctxs[c.cx].asptr = x.asptr.value;
			const ch = mkchild(c, rf);
			kcall(env, rf.start, rf.p);
			if (kw() < 0) {
				h.ctxs.pop();
				c.ret = -1n;
			} else {
				if (!h.multi) {
					h.multi = true;
					h.memdone = rf.memdone;
					h.umem = rf.umem;
				}
				h.cos.push(ch);
				c.ret = BigInt(rf.pid);
			}
			c.state = 'ready';
		} else if (h.block) {
			h.ctxs[c.cx].asptr = x.asptr.value;
			c.state = 'blocked';
		} else if (h.preempt) {
			/* its slice is up: another goes, it is ready again */
			h.ctxs[c.cx].asptr = x.asptr.value;
			c.state = 'ready';
			c.ret = 0n;
		} else {
			/* main or a context's function returned: exits(nil) */
			const z = (x.sp.value - 8) & ~7;
			m32().setUint32(z, 0, true);
			x.asstate.value = 0;
			sys(8, z);
			if (!h.block) throw new Error('platform: exits returned');
			c.state = 'blocked';
		}
		/* the next to go: round robin, after what the helpers finished */
		let next = -1;
		for (;;) {
			const i32 = new Int32Array(env.mem.buffer);
			const seen = Atomics.load(i32, h.memdone >> 2);
			for (const q of h.cos) {
				if (q.state != 'blocked')
					continue;
				const st = done(q);
				if (st === 2) {
					q.state = 'done';
					h.ctxs[q.cx].dead = true;
					if (q.p !== host) kcall(env, K.coend, q.p);
				} else if (st === 1) {
					claim(h.ctxs[q.cx]);	/* its results go to its stack: in memory first */
					kcall(env, K.sysfin, q.p);
					if (kw() === 1) continue;
					kcall(env, K.sysret, q.p);
					q.ret = env.x.retv.value;
					if (env.note) { q.note = env.note; env.note = null; }
					q.state = 'ready';
				}
			}
			if (h.cos.every((q) => q.state == 'done')) {
				kcall(env, K.coend, host);
				kcall(env, K.memend, h.umem);
				throw MEMDONE;
			}
			for (let i = 1; i <= h.cos.length; i++) {
				const j = (h.cur + i) % h.cos.length;
				if (h.cos[j].state == 'ready') { next = j; break; }
			}
			if (next >= 0) break;
			Atomics.wait(i32, h.memdone >> 2, seen);
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
}

// the kernel's imports on a Worker: env { mem, x (its exports, once there), post, user, exec }
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
			env.post({ spawn: { fn: arg(0), arg: arg(1), sp: arg(2), up: arg(3), what: iarg(4), user } }, user ? [user.snap.buffer] : []);
		},
		platnote: () => {
			const k = new Uint8Array(env.mem.buffer);
			let e = arg(1);
			while (k[e]) e++;
			env.note = { handler: arg(0), msg: k.slice(arg(1), e), done: arg(2), p: arg(3) };
		},
		platnoted: () => { env.noted = true; },
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
		platuser: () => runuser(env, ufns(env, arg(0)), arg(1), arg(2)),
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
		platrfmem: () => {
			env.rfmem = { p: arg(0), start: arg(1), pid: arg(2), umem: arg(3), memdone: arg(4) };
			env.user.x.asstate.value = 1;	/* unwind when the call returns */
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
		platflush: () => env.post({ flush: [iarg(0), iarg(1), iarg(2), iarg(3)] }),
		platcursor: () => {
			const k = new Uint8Array(env.mem.buffer);
			env.post({ cursor: { x: iarg(0), y: iarg(1), clr: k.slice(arg(2), arg(2) + 32), set: k.slice(arg(3), arg(3) + 32) } });
		},
		platmousering: () => env.post({ mring: arg(0) }),
		platnetopen: () => env.post({ netopen: { id: iarg(0), gen: arg(1), path: str(arg(2)), ring: arg(3), st: arg(4) } }),
		platnetsend: () => {
			const b = new Uint8Array(env.mem.buffer).slice(arg(2), arg(2) + iarg(3));
			env.post({ netsend: { id: iarg(0), gen: arg(1), b } }, [b.buffer]);
		},
		platnetclose: () => env.post({ netclose: { id: iarg(0), gen: arg(1) } }),
		platkbdring: () => env.post({ kring: arg(0) }),
	};
	// a function the kernel wants and the platform has not: say which
	const platform = new Proxy(fns, { get: (o, k) => k === 'memory' ? env.mem : o[k] ?? (() => { throw new Error('platform: ' + String(k) + ' not here'); }) });
	return { plan9: { syscall: () => -1n }, platform };
}

// on a Worker: the kernel, and what this CPU runs
function cpu({ module, mem, role, fn, arg, sp, user }) {
	const env = { mem, x: null, post: (m, t) => postMessage(m, t ?? []), user: null, exec: null, fork: null, forkimage: null,
		rfmem: null, note: null, noted: false };
	if (user) env.exec = user;	/* a fork's child */
	try {
		const inst = new WebAssembly.Instance(module, imports(env));
		env.x = inst.exports;
		if (role === 'boot') {
			env.x._init();		// the kernel's data, once
			/* the entry ABI (docs/boot-abi-wasm32.md): _start(BootInfo's address), as a C call - at SP */
			const top = (env.x.stacktop.value - 16) & ~7;
			new DataView(mem.buffer).setUint32(top, arg, true);
			env.x.sp.value = top;
			env.x._start();		// main(pa)
		} else {
			const top = (sp - 16) & ~7;
			new DataView(mem.buffer).setUint32(top, arg, true);
			env.x.sp.value = top;
			env.x.table.get(fn)();
			// the proc is Dead and this Worker out of the kernel: a Worker less on its Proc and KSTACK (newproc)
			const gone = env.x.retw.value;
			if (gone) {
				const i32 = new Int32Array(mem.buffer);
				Atomics.sub(i32, gone >> 2, 1);
				Atomics.notify(i32, gone >> 2);
			}
			close();
		}
	} catch (e) {
		if (e === MEMDONE) {	/* rfork(RFMEM): the memory's procs are gone (runprog) */
			close();
			return;
		}
		postMessage({ halt: 'platform: ' + e + (e.stack ? ' ' + e.stack : '') });
	}
}

if (typeof WorkerGlobalScope !== 'undefined' && self instanceof WorkerGlobalScope)
	self.onmessage = (e) => cpu(e.data);

// on the page: the machine
// front: { eia(bytes), halt(why), fs (the root's archive, devrootfs.c), args (init's argv: plan9.ini's init=),
//	config (more plan9.ini lines), canvas, screen ({ w, h }, 1024x768 without: the framebuffer, shown on
//	the canvas; its pointer the mouse) - what the firmware puts in BootInfo (firmware()),
//	ws (the machine's webterm: ws://host:port, the network's WebSockets),
//	failfork, failhelper, failrfmem (a test's: the nth fork's child, helper, rfork(RFMEM) child gets no Worker) }
export async function boot(url, front = {}) {
	const module = await WebAssembly.compileStreaming(fetch(url));
	const { mem, pa, fb } = firmware(front);
	const eia = [];
	let ring = 0;		/* #t/eia0's input: the kernel's ring */
	let kring = 0;		/* #b/kbd's (devkbd.c) */
	const kbdq = [];	/* its messages the ring has had no room for yet */
	let kbdtimer = 0;
	const kbdpump = () => {
		kbdtimer = 0;
		if (!kring) { if (kbdq.length) kbdtimer = setTimeout(kbdpump, 20); return; }
		const i32 = new Int32Array(mem.buffer), u8 = new Uint8Array(mem.buffer), N = 4096;
		let w = Atomics.load(i32, (kring + 4) >> 2) >>> 0;	/* modulo 2^32, N a power of 2 */
		const w0 = w;
		while (kbdq.length && ((w - (Atomics.load(i32, kring >> 2) >>> 0)) >>> 0) + kbdq[0].length <= N)
			for (const c of kbdq.shift()) u8[kring + 8 + (w++ & (N - 1))] = c;
		if (w !== w0) {
			Atomics.store(i32, (kring + 4) >> 2, w | 0);
			Atomics.notify(i32, (kring + 4) >> 2);
		}
		if (kbdq.length) kbdtimer = setTimeout(kbdpump, 5);	/* the ring is full: when the kernel has read */
	};
	const me = import.meta.url;
	// a proc's Worker the page could not make: -1 in its word (procspawn waits on it)
	const failed = (job, why) => {
		console.log('KLOG platform: no Worker for a proc: ' + why);
		if (!job.up) return false;
		const i32 = new Int32Array(mem.buffer);
		if (Atomics.compareExchange(i32, job.up >> 2, 0, -1) !== 0) return false;
		Atomics.notify(i32, job.up >> 2);
		return true;
	};
	/*
	 * the screen (screen.c): the kernel's framebuffer, XRGB32 in the shared
	 * memory, drawn on front.canvas where it changed, once a frame; the
	 * canvas's pointer the mouse, into the kernel's ring
	 */
	const screen = { f: null, dirty: null, mring: 0, img: null, flushes: 0 };
	const canvas = front.canvas;
	screen.fb = (fb) => {
		screen.f = fb;
		if (!canvas) return;
		canvas.width = fb.w;
		canvas.height = fb.h;
		screen.img = canvas.getContext('2d').createImageData(fb.w, fb.h);
	};
	const paint = () => {
		const r = screen.dirty, f = screen.f;
		screen.dirty = null;
		if (!r || !f || !canvas) return;
		const src = new Uint8Array(mem.buffer), dst = screen.img.data;
		for (let y = r[1]; y < r[3]; y++) {
			let s = f.addr + y*f.stride + r[0]*4, d = (y*f.w + r[0])*4;
			for (let x = r[0]; x < r[2]; x++, s += 4, d += 4) {
				dst[d] = src[s + 2];
				dst[d + 1] = src[s + 1];
				dst[d + 2] = src[s];
				dst[d + 3] = 255;
			}
		}
		canvas.getContext('2d').putImageData(screen.img, 0, 0, r[0], r[1], r[2] - r[0], r[3] - r[1]);
		screen.flushes++;
	};
	screen.flush = (r) => {
		const d = screen.dirty;
		screen.dirty = d ? [Math.min(d[0], r[0]), Math.min(d[1], r[1]), Math.max(d[2], r[2]), Math.max(d[3], r[3])] : r;
		if (!d) requestAnimationFrame(paint);
	};
	/* Plan 9's cursor (16x16: set black, clr white) as the canvas's */
	screen.cursor = (c) => {
		if (!canvas) return;
		const cc = document.createElement('canvas');
		cc.width = cc.height = 16;
		const g = cc.getContext('2d'), im = g.createImageData(16, 16);
		for (let i = 0; i < 256; i++) {
			const byte = i >> 3, bit = 0x80 >> (i & 7);
			const set = c.set[byte] & bit, clr = c.clr[byte] & bit;
			if (set || clr) {
				const v = set ? 0 : 255;
				im.data.set([v, v, v, 255], 4*i);
			}
		}
		g.putImageData(im, 0, 0);
		const hx = Math.min(15, Math.max(0, -c.x)), hy = Math.min(15, Math.max(0, -c.y));
		canvas.style.cursor = `url(${cc.toDataURL()}) ${hx} ${hy}, auto`;
	};
	/* the mouse: x, y, buttons (1 2 4; the wheel 8 16), msec into the kernel's ring */
	let buttons = 0;
	/*
	 * Events the ring has no room for wait here, in order: a move after a
	 * waiting move with the same buttons takes its place (the pointer is
	 * where it is now), a button or the wheel is never lost - a lost up
	 * would leave a button down
	 */
	const mouseq = [];
	let mousetimer = 0;
	const mousepump = () => {
		mousetimer = 0;
		if (!screen.mring) return;
		const i32 = new Int32Array(mem.buffer), r = screen.mring >> 2;
		let w = Atomics.load(i32, r) >>> 0;	/* modulo 2^32 */
		const w0 = w;
		while (mouseq.length && ((w - (Atomics.load(i32, r + 1) >>> 0)) >>> 0) < 64) {
			const [x, y, b, ms] = mouseq.shift(), e = r + 2 + 4*(w & 63);
			i32[e] = x; i32[e + 1] = y; i32[e + 2] = b; i32[e + 3] = ms;
			w++;
		}
		if (w !== w0) {
			Atomics.store(i32, r, w | 0);
			Atomics.notify(i32, r);
		}
		if (mouseq.length) mousetimer = setTimeout(mousepump, 5);
	};
	const mouse = (x, y, b, move) => {
		if (!screen.mring) return;
		const last = mouseq[mouseq.length - 1];
		if (move && last && last.move && last[2] === b)
			mouseq[mouseq.length - 1] = Object.assign([x, y, b, performance.now() | 0], { move });
		else
			mouseq.push(Object.assign([x, y, b, performance.now() | 0], { move }));
		if (!mousetimer) mousepump();
	};
	if (canvas) {
		const at = (e) => {
			const b = canvas.getBoundingClientRect();
			return [Math.round((e.clientX - b.left) * canvas.width / b.width), Math.round((e.clientY - b.top) * canvas.height / b.height)];
		};
		const bits = (e) => (e.buttons & 1 ? 1 : 0) | (e.buttons & 4 ? 2 : 0) | (e.buttons & 2 ? 4 : 0);
		canvas.addEventListener('pointermove', (e) => {
			const b = bits(e), moved = b === buttons;	/* buttons change on a move too (chords) */
			buttons = b;
			mouse(...at(e), buttons, moved);
		});
		canvas.addEventListener('pointerdown', (e) => { canvas.setPointerCapture(e.pointerId); buttons = bits(e); mouse(...at(e), buttons); e.preventDefault(); });
		canvas.addEventListener('pointerup', (e) => { buttons = bits(e); mouse(...at(e), buttons); });
		canvas.addEventListener('wheel', (e) => {
			const p = at(e), w = e.deltaY < 0 ? 8 : 16;
			mouse(...p, buttons | w);
			mouse(...p, buttons);
			e.preventDefault();
		}, { passive: false });
		canvas.addEventListener('contextmenu', (e) => e.preventDefault());
	}

	/*
	 * the network (devwsnet.c): conversation n a WebSocket to front.ws +
	 * path (the machine's webterm); what it brings into the kernel's ring
	 * (r, w, closed, b[64K]), the kernel woken; past a full ring the
	 * connection fails rather than lose bytes, as drawterm's wsock.c
	 */
	const net = { ws: new Map() };	/* n -> { ws, gen }: the conversation's WebSocket, which gen of n it is */
	const NRING = 64*1024;		/* a power of 2: the counters run on modulo 2^32, the index masked */
	net.open = ({ id, gen, path, ring, st }) => {
		net.close({ id });	/* an older gen's, if its close has not come yet */
		const i32 = new Int32Array(mem.buffer);
		const word = (a, v) => { Atomics.store(i32, a >> 2, v); Atomics.notify(i32, a >> 2); };
		/* the ring empty: only this thread writes it, and the kernel reads it only once this gen is open
		   (front.ringstart, a test's: the counters start there - across 2^31 and 2^32) */
		Atomics.store(i32, ring >> 2, (front.ringstart ?? 0) | 0);
		Atomics.store(i32, (ring + 4) >> 2, (front.ringstart ?? 0) | 0);
		Atomics.store(i32, (ring + 8) >> 2, 0);
		let ws, opened = false;
		try {
			ws = new WebSocket((front.ws ?? '') + path);
		} catch (e) {
			word(st, -1);
			return;
		}
		ws.binaryType = 'arraybuffer';
		net.ws.set(id, { ws, gen });
		const mine = () => net.ws.get(id)?.ws === ws;
		const end = (why) => {
			if (!mine()) return;
			net.ws.delete(id);
			if (!opened) word(st, -1);
			else { Atomics.store(i32, (ring + 8) >> 2, why); Atomics.notify(i32, (ring + 4) >> 2); }
		};
		ws.onopen = () => { if (!mine()) return; opened = true; word(st, 1); };
		ws.onclose = () => end(1);
		ws.onerror = () => end(2);
		ws.onmessage = (e) => {
			if (!mine()) return;
			const b = typeof e.data === 'string' ? new TextEncoder().encode(e.data) : new Uint8Array(e.data);
			const u8 = new Uint8Array(mem.buffer);
			let w = Atomics.load(i32, (ring + 4) >> 2) >>> 0;
			if (((w - (Atomics.load(i32, ring >> 2) >>> 0)) >>> 0) + b.length > NRING) {
				end(3);
				ws.close();
				return;
			}
			for (const c of b) u8[ring + 12 + (w++ & (NRING - 1))] = c;
			Atomics.store(i32, (ring + 4) >> 2, w | 0);
			Atomics.notify(i32, (ring + 4) >> 2);
		};
	};
	net.send = ({ id, gen, b }) => { const c = net.ws.get(id); if (c && c.gen === gen && c.ws.readyState === 1) c.ws.send(b); };
	net.close = ({ id, gen }) => {	/* gen undefined: whichever */
		const c = net.ws.get(id);
		if (!c || gen !== undefined && c.gen !== gen) return;
		net.ws.delete(id);
		c.ws.onopen = c.ws.onclose = c.ws.onerror = c.ws.onmessage = null;
		c.ws.close();
	};

	/* a test's: the nth Worker of a kind is not made - a fork's child, a helper, an rfork(RFMEM) child's (platform.h) */
	const fails = { 1: front.failfork, 2: front.failhelper, 3: front.failrfmem }, made = { 1: 0, 2: 0, 3: 0 };
	const spawn = (job) => {
		if (fails[job.what] && ++made[job.what] === fails[job.what]) {
			failed(job, 'the test fails Worker ' + made[job.what] + ' of kind ' + job.what);
			return;
		}
		let w;
		try {
			w = new Worker(me, { type: 'module' });
		} catch (e) {
			if (!failed(job, e)) console.log('KERNEL-HALT platform: worker: ' + e);
			return;
		}
		w.onmessage = (e) => {
			const m = e.data;
			if (m.eia) { eia.push(m.eia); front.eia?.(m.eia); }
			if (m.spawn) spawn({ module, mem, role: 'proc', ...m.spawn });
			if (m.log !== undefined) console.log('KLOG ' + m.log);
			if (m.ring !== undefined) ring = m.ring;
			if (m.flush) screen.flush(m.flush);
			if (m.cursor) screen.cursor(m.cursor);
			if (m.mring !== undefined) screen.mring = m.mring;
			if (m.netopen) net.open(m.netopen);
			if (m.netsend) net.send(m.netsend);
			if (m.netclose) net.close(m.netclose);
			if (m.kring !== undefined) kring = m.kring;
			if (m.halt !== undefined) { console.log('KERNEL-HALT ' + m.halt); front.halt?.(m.halt); }
		};
		w.onerror = (e) => { if (!failed(job, e.message)) console.log('KERNEL-HALT platform: worker: ' + e.message); };
		w.postMessage(job, job.user ? [job.user.snap.buffer] : []);
	};
	window.monolith = {
		get flushes() { return screen.flushes; },
		eia0bytes: () => { const b = new Uint8Array(eia.reduce((n, c) => n + c.length, 0)); let o = 0; for (const c of eia) { b.set(c, o); o += c.length; } return b; },
		eia0out: () => new TextDecoder().decode(window.monolith.eia0bytes()),
		eia0b64: () => { let s = ''; for (const c of window.monolith.eia0bytes()) s += String.fromCharCode(c); return btoa(s); },
		eia0in: (x) => {
			if (!ring) return -1;
			const b = typeof x === 'string' ? new TextEncoder().encode(x) : new Uint8Array(x);
			const i32 = new Int32Array(mem.buffer), u8 = new Uint8Array(mem.buffer), N = 8192;
			let w = Atomics.load(i32, (ring + 4) >> 2) >>> 0, n = 0;	/* modulo 2^32, N a power of 2 */
			for (const c of b) {
				if (((w - (Atomics.load(i32, ring >> 2) >>> 0)) >>> 0) >= N) break;
				u8[ring + 8 + (w & (N - 1))] = c;
				w++;
				n++;
			}
			Atomics.store(i32, (ring + 4) >> 2, w | 0);
			Atomics.notify(i32, (ring + 4) >> 2);
			return n;
		},
		/* #b/kbd's messages (devkbd.c): r and a rune down, R up, c typed, each with its 0 */
		/*
		 * #b/kbd's messages (devkbd.c): r and a rune down, R up, c typed,
		 * each with its 0.  None is lost: what the ring has no room for
		 * waits here, in order, and goes in as the kernel reads
		 */
		kbd: (s) => {
			kbdq.push(new TextEncoder().encode(s));
			kbdpump();
			return 1;
		},
		eia0inb64: (b) => window.monolith.eia0in(Uint8Array.from(atob(b), (c) => c.charCodeAt(0))),
	};
	if (fb) screen.fb(fb);
	spawn({ module, mem, role: 'boot', arg: pa });
}
