#!/usr/bin/env node
// A wasm32 process (3c, 3l: objtype wasm32) under Node, with the least
// of a kernel: plan9.syscall(number, args) reads the arguments from the
// process's memory at args, as a trap would; files are the host's.  fork
// (rfork RFPROC without RFMEM): the stack unwound into memory (3l), the
// memory copied to a child - a worker thread - and both rewound.  For
// tests (tools/test-3c).
//   node run3.mjs prog.wasm [args...]   exit status: 0, or 1 with exits("...")
import * as fs from 'node:fs';
import { Worker, isMainThread, workerData } from 'node:worker_threads';
import { fileURLToPath } from 'node:url';

const SYS = { CLOSE: 4, EXITS: 8, OPEN: 14, RFORK: 19, CREATE: 22, FD2PATH: 23, BRK_: 24, REMOVE: 25,
	SEEK: 39, ERRSTR: 41, AWAIT: 47, PREAD: 50, PWRITE: 51, CHDIR: 3, _NSEC: 53 };
const OREAD = 0, OWRITE = 1, ORDWR = 2, OTRUNC = 16, ORCLOSE = 64, OEXCL = 0x1000, DMDIR = 0x80000000;
const RFPROC = 1<<4, RFMEM = 1<<5, RFNOWAIT = 1<<6;
class Exit { constructor(msg) { this.msg = msg; } }

// the process table: pids, and each child's end (status: 0 running, 1 ended)
const PIDS = 64, MSG = 256;
function newtable() { return { shared: new SharedArrayBuffer(4 + PIDS * (8 + MSG)) }; }

function run(bytes, args, st) {
	// st: { pid, ppid, table, fds, snap, asptr }
	const tab = new Int32Array(st.table, 0, 1 + PIDS * 2), tmsg = new Uint8Array(st.table);
	let mem, inst, errstr = '', fork = null;
	const dv = () => new DataView(mem.buffer);
	const u8 = () => new Uint8Array(mem.buffer);
	const str = (p) => { const b = u8(); let e = p; while (b[e]) e++; return Buffer.from(b.subarray(p, e)).toString(); };
	const fds = new Map(st.fds.map(([i, f]) => [i, { ...f }]));
	const newfd = (f) => { let i = 0; while (fds.has(i)) i++; fds.set(i, f); return i; };
	const err = (e) => { errstr = e.code ? `${e.code.toLowerCase()} ${e.path ?? ''}`.trim() : String(e.message ?? e); return -1n; };
	const children = [];

	function syscall(n, a) {
		const d = dv(), w = (i) => d.getInt32(a + 4*i, true), v = (i) => d.getBigInt64(a + 4*i, true);
		try {
			switch (n) {
			case SYS.PWRITE:
			case SYS.PREAD: {
				const f = fds.get(w(0)), p = w(1) >>> 0, len = w(2), o = v(3);
				if (!f) { errstr = 'fd out of range or not open'; return -1n; }
				const pos = o >= 0n ? Number(o) : f.off;
				const buf = new Uint8Array(mem.buffer, p, len);
				const r = n === SYS.PWRITE ? fs.writeSync(f.h, buf, 0, len, pos) : fs.readSync(f.h, buf, 0, len, pos);
				if (o < 0n && f.off !== null) f.off += r;
				return BigInt(r);
			}
			case SYS.OPEN:
			case SYS.CREATE: {
				const path = str(w(0) >>> 0), mode = w(1), perm = n === SYS.CREATE ? w(2) >>> 0 : 0;
				if (n === SYS.CREATE && (perm & DMDIR)) { fs.mkdirSync(path); return BigInt(newfd({ h: fs.openSync(path, 'r'), off: 0, path })); }
				const rw = mode & 3;
				let flags = rw === OWRITE ? 'r+' : rw === ORDWR ? 'r+' : 'r';
				if (n === SYS.CREATE) flags = (mode & OEXCL) ? 'wx+' : 'w+';
				else if ((mode & OTRUNC) && rw !== OREAD) flags = 'w+';
				const h = fs.openSync(path, flags, n === SYS.CREATE ? (perm & 0o777) : undefined);
				return BigInt(newfd({ h, off: 0, path, rclose: mode & ORCLOSE }));
			}
			case SYS.CLOSE: {
				const f = fds.get(w(0));
				if (!f) { errstr = 'fd out of range or not open'; return -1n; }
				if (f.h > 2) fs.closeSync(f.h);
				if (f.rclose) fs.rmSync(f.path, { force: true });
				fds.delete(w(0));
				return 0n;
			}
			case SYS.SEEK: {
				const f = fds.get(w(0)), o = v(1), type = w(3);
				if (!f || f.off === null) { errstr = 'seek on a stream'; return -1n; }
				const base = type === 0 ? 0 : type === 1 ? f.off : fs.fstatSync(f.h).size;
				f.off = base + Number(o);
				return BigInt(f.off);
			}
			case SYS.REMOVE:
				fs.rmSync(str(w(0) >>> 0));
				return 0n;
			case SYS.FD2PATH: {
				const f = fds.get(w(0)), p = w(1) >>> 0, len = w(2);
				if (!f) { errstr = 'fd out of range or not open'; return -1n; }
				const s = Buffer.from(f.path), m = Math.min(s.length, len - 1);
				u8().set(s.subarray(0, m), p);
				u8()[p + m] = 0;
				return 0n;
			}
			case SYS.CHDIR:
				process.chdir(str(w(0) >>> 0));
				return 0n;
			case SYS._NSEC:
				return process.hrtime.bigint();
			case SYS.RFORK: {
				const flags = w(0);
				if (!(flags & RFPROC))
					return 0n;
				if (flags & RFMEM) { errstr = 'rfork RFMEM not in run3'; return -1n; }
				// unwind: _trap returns, every frame saves itself, _start returns
				fork = { flags };
				inst.exports.asstate.value = 1;
				return 0n;
			}
			case SYS.AWAIT: {
				// a child's end: 'pid utime stime rtime msg', as Plan 9's
				const p = w(0) >>> 0, len = w(1);
				const mine = children.filter((c) => !c.reaped);
				if (mine.length === 0) { errstr = 'no living children'; return -1n; }
				for (;;) {
					const c = mine.find((c) => Atomics.load(tab, 1 + 2*c.slot) === 1);
					if (c) {
						c.reaped = true;
						const m = Buffer.from(tmsg.subarray(4 + PIDS*8 + c.slot*MSG, 4 + PIDS*8 + (c.slot+1)*MSG)).toString().replace(/\0.*$/s, '');
						const q = m === '' ? "''" : /[ \t\n'`=]/.test(m) ? "'" + m.replace(/'/g, "''") + "'" : m;
						const s = Buffer.from(`${c.pid} 0 0 0 ${q}`), k = Math.min(s.length, len);
						u8().set(s.subarray(0, k), p);
						return BigInt(k);
					}
					Atomics.wait(tab, 0, Atomics.load(tab, 0), 1000);
				}
			}
			case SYS.EXITS:
				throw new Exit(w(0) ? str(w(0) >>> 0) : '');
			case SYS.BRK_: {
				const want = w(0) >>> 0, have = mem.buffer.byteLength;
				if (want > have)
					mem.grow(Math.ceil((want - have) / 65536));
				return 0n;
			}
			case SYS.ERRSTR: {
				// swap: the process's buffer gets ours, ours what it had
				const p = w(0) >>> 0, len = w(1), b = u8();
				const had = str(p), ours = Buffer.from(errstr);
				const m = Math.min(ours.length, len - 1);
				b.set(ours.subarray(0, m), p);
				b[p + m] = 0;
				errstr = had;
				return 0n;
			}
			}
		} catch (e) {
			if (e instanceof Exit) throw e;
			return err(e);
		}
		process.stderr.write(`run3: system call ${n} not here\n`);
		errstr = 'system call not here';
		return -1n;
	}

	inst = new WebAssembly.Instance(new WebAssembly.Module(bytes), { plan9: { syscall: (n, a) => syscall(n, a) } });
	mem = inst.exports.memory;
	const x = inst.exports;
	if (st.snap) {
		// a child: the parent's memory, rewound with 0
		if (st.snap.byteLength > mem.buffer.byteLength)
			mem.grow(Math.ceil((st.snap.byteLength - mem.buffer.byteLength) / 65536));
		u8().set(new Uint8Array(st.snap));
		x.asptr.value = st.asptr;
		x.asstate.value = 2;
		x.asret.value = 0n;
	} else {
		// argc, argv[0] ... nil at sp; the strings above them, as Plan 9's kernel does
		const b = u8(), d = dv();
		let top = x.sp.value;
		const ptrs = args.map((s) => { const e = Buffer.from(s + '\0'); top -= e.length; b.set(e, top); return top; });
		top &= ~7;
		top -= 4 * (ptrs.length + 2);
		top &= ~7;
		d.setInt32(top, ptrs.length, true);
		ptrs.forEach((p, i) => d.setInt32(top + 4 + 4*i, p, true));
		d.setInt32(top + 4 + 4*ptrs.length, 0, true);
		x.sp.value = top;
	}
	try {
		for (;;) {
			x._start();
			if (!fork)
				return '';
			// unwound: the child gets a copy of everything, then both rewind
			const slot = Atomics.add(tab, 0, 1) % PIDS;	// tab[0] also the wakeup word
			const pid = st.pid * 10 + children.length + 1;
			Atomics.store(tab, 1 + 2*slot, 0);
			Atomics.store(tab, 2 + 2*slot, pid);
			const snap = new SharedArrayBuffer(mem.buffer.byteLength);
			new Uint8Array(snap).set(u8());
			new Worker(fileURLToPath(import.meta.url), { workerData: { bytes, args,
				st: { pid, ppid: st.pid, table: st.table, slot, fds: [...fds], snap, asptr: x.asptr.value } } });
			children.push({ pid, slot });
			fork = null;
			x.asstate.value = 2;
			x.asret.value = BigInt(pid);
		}
	} catch (e) {
		if (e instanceof Exit)
			return e.msg;
		throw e;
	}
}

function ended(st, msg) {
	if (st.slot === undefined)
		return;
	const tab = new Int32Array(st.table, 0, 1 + PIDS * 2), b = new Uint8Array(st.table);
	const m = Buffer.from(msg).subarray(0, MSG - 1), o = 4 + PIDS*8 + st.slot*MSG;
	b.fill(0, o, o + MSG);
	b.set(m, o);
	Atomics.store(tab, 1 + 2*st.slot, 1);
	Atomics.add(tab, 0, 1);
	Atomics.notify(tab, 0);
}

if (isMainThread) {
	const [file, ...args] = process.argv.slice(2);
	const bytes = fs.readFileSync(file);
	const argv = [file.replace(/.*\//, '').replace(/\.wasm$/, ''), ...args];
	const fds = [[0, { h: 0, off: null, path: '/dev/stdin' }], [1, { h: 1, off: null, path: '/dev/stdout' }], [2, { h: 2, off: null, path: '/dev/stderr' }]];
	const msg = run(bytes, argv, { pid: 1, ppid: 0, table: newtable().shared, fds });
	if (msg) { process.stderr.write(`exits: ${msg}\n`); process.exit(1); }
	process.exit(0);
} else {
	const { bytes, args, st } = workerData;
	let msg;
	try {
		msg = run(bytes, args, st);
	} catch (e) {
		msg = 'run3: ' + e;
		process.stderr.write(msg + '\n');
	}
	ended(st, msg);
}
