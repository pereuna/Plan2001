#!/usr/bin/env node
// A wasm32 process (3c, 3l: objtype wasm32) under Node, with the least
// of a kernel: plan9.syscall(number, args) reads the arguments from the
// process's memory at args, as a trap would.  For tests (tools/test-3c).
//   node run3.mjs prog.wasm [args...]   exit status: 0, or 1 with exits("...")
import { readFileSync, writeSync } from 'node:fs';

const EXITS = 8, BRK_ = 24, PWRITE = 51, PREAD = 50, ERRSTR = 41;
const [file] = process.argv.slice(2);
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
	case ERRSTR:
		return 0n;
	}
	process.stderr.write(`run3: system call ${n} not here\n`);
	return -1n;
}

const { instance } = await WebAssembly.instantiate(readFileSync(file), { plan9: { syscall: (n, a) => syscall(n, a) } });
mem = instance.exports.memory;
try {
	instance.exports._start();
} catch (e) {
	if (e instanceof Exit) {
		if (e.msg) { process.stderr.write(`exits: ${e.msg}\n`); process.exit(1); }
		process.exit(0);
	}
	throw e;
}
