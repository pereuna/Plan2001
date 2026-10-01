#!/usr/bin/env node
// A wasm32 process (3c, 3l: objtype wasm32) under Node, with the least
// of a kernel: plan9.syscall(number, args) reads the arguments from the
// process's memory at args, as a trap would.  For tests (tools/test-3c).
//   node run3.mjs prog.wasm [args...]   exit status: 0, or 1 with exits("...")
import { readFileSync, writeSync } from 'node:fs';

const EXITS = 8, BRK_ = 24, PWRITE = 51, PREAD = 50, ERRSTR = 41;
const [file, ...args] = process.argv.slice(2);
let errstr = '';
let mem;
const dv = () => new DataView(mem.buffer);
const str = (p) => { const b = new Uint8Array(mem.buffer); let e = p; while (b[e]) e++; return Buffer.from(b.subarray(p, e)).toString(); };
class Exit { constructor(msg) { this.msg = msg; } }

function syscall(n, a) {
	const d = dv(), w = (i) => d.getInt32(a + 4*i, true);
	switch (n) {
	case PWRITE: {
		const fd = w(0), p = w(1) >>> 0, len = w(2);
		writeSync(fd, new Uint8Array(mem.buffer, p, len));
		return BigInt(len);
	}
	case EXITS:
		throw new Exit(w(0) ? str(w(0) >>> 0) : '');
	case BRK_: {
		const want = w(0) >>> 0, have = mem.buffer.byteLength;
		if (want > have)
			mem.grow(Math.ceil((want - have) / 65536));
		return 0n;
	}
	case ERRSTR: {
		// swap: the process's buffer gets ours, ours what it had
		const p = w(0) >>> 0, n = w(1), b = new Uint8Array(mem.buffer);
		const had = str(p), ours = Buffer.from(errstr);
		const m = Math.min(ours.length, n - 1);
		b.set(ours.subarray(0, m), p);
		b[p + m] = 0;
		errstr = had;
		return 0n;
	}
	}
	process.stderr.write(`run3: system call ${n} not here\n`);
	return -1n;
}

const { instance } = await WebAssembly.instantiate(readFileSync(file), { plan9: { syscall: (n, a) => syscall(n, a) } });
mem = instance.exports.memory;
// argc, argv[0] ... nil at sp; the strings above them, as Plan 9's kernel does
{
	const sp = instance.exports.sp, argv = [file.replace(/.*\//, '').replace(/\.wasm$/, ''), ...args];
	const b = new Uint8Array(mem.buffer), d = dv();
	let top = sp.value;
	const ptrs = argv.map((a) => { const e = Buffer.from(a + '\0'); top -= e.length; b.set(e, top); return top; });
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
