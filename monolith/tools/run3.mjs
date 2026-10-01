#!/usr/bin/env node
// A wasm32 process (3c, 3l: objtype wasm32) under Node, with the least
// of a kernel: plan9.syscall(number, args) reads the arguments from the
// process's memory at args, as a trap would; files are the host's.  For
// tests (tools/test-3c).
//   node run3.mjs prog.wasm [args...]   exit status: 0, or 1 with exits("...")
import * as fs from 'node:fs';

const SYS = { CLOSE: 4, EXITS: 8, OPEN: 14, CREATE: 22, FD2PATH: 23, BRK_: 24, REMOVE: 25,
	SEEK: 39, ERRSTR: 41, PREAD: 50, PWRITE: 51, CHDIR: 3, _NSEC: 53 };
const OREAD = 0, OWRITE = 1, ORDWR = 2, OEXEC = 3, OTRUNC = 16, ORCLOSE = 64, OEXCL = 0x1000, DMDIR = 0x80000000;
const [file, ...args] = process.argv.slice(2);
let mem, errstr = '';
const dv = () => new DataView(mem.buffer);
const u8 = () => new Uint8Array(mem.buffer);
const str = (p) => { const b = u8(); let e = p; while (b[e]) e++; return Buffer.from(b.subarray(p, e)).toString(); };
class Exit { constructor(msg) { this.msg = msg; } }

// file descriptors: ours, with their offsets (Node has no lseek)
const fds = new Map([[0, { h: 0, off: null, path: '/dev/stdin' }], [1, { h: 1, off: null, path: '/dev/stdout' }], [2, { h: 2, off: null, path: '/dev/stderr' }]]);
const newfd = (f) => { let i = 0; while (fds.has(i)) i++; fds.set(i, f); return i; };
function err(e) { errstr = e.code ? `${e.code.toLowerCase()} ${e.path ?? ''}`.trim() : String(e.message ?? e); return -1n; }

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

const { instance } = await WebAssembly.instantiate(fs.readFileSync(file), { plan9: { syscall: (n, a) => syscall(n, a) } });
mem = instance.exports.memory;
// argc, argv[0] ... nil at sp; the strings above them, as Plan 9's kernel does
{
	const sp = instance.exports.sp, argv = [file.replace(/.*\//, '').replace(/\.wasm$/, ''), ...args];
	const b = u8(), d = dv();
	let top = sp.value;
	const ptrs = argv.map((s) => { const e = Buffer.from(s + '\0'); top -= e.length; b.set(e, top); return top; });
	top &= ~7;
	top -= 4 * (ptrs.length + 2);
	top &= ~7;
	d.setInt32(top, ptrs.length, true);
	ptrs.forEach((p, i) => d.setInt32(top + 4 + 4*i, p, true));
	d.setInt32(top + 4 + 4*ptrs.length, 0, true);
	sp.value = top;
}
try {
	instance.exports._start();
} catch (e) {
	if (e instanceof Exit) {
		if (e.msg) { process.stderr.write(`exits: ${e.msg}\n`); process.exit(1); }
		process.exit(0);
	}
	throw e;
}
