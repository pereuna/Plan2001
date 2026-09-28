// A compute resource's worker (cr.js): 9front's 6c as WebAssembly
// (tools/build-cc wasm), a fresh instance for each job, with the server's
// headers and the job's files at their Plan 9 paths and the job's
// directory as the current one, so that the object is the same as the
// server's own 6c would make.
'use strict';
importScripts('6c.js');
let wasm = null, include = [];
const dec = new TextDecoder();

function files(b) {
	const out = [], dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
	for (let o = 0; o < b.length;) {
		const z = b.indexOf(0, o);
		if (z < 0 || z + 5 > b.length) break;
		const name = dec.decode(b.subarray(o, z));
		const n = dv.getUint32(z + 1);
		out.push([name, b.subarray(z + 5, z + 5 + n)]);
		o = z + 5 + n;
	}
	return out;
}
function dirname(p) { return p.slice(0, p.lastIndexOf('/')) || '/'; }

onmessage = async e => {
	const m = e.data;
	if (m.type === 'init') {
		wasm = m.wasm;
		include = files(m.include);
		return;
	}
	const t0 = performance.now();
	let log = '';
	const mod = await Cc6({ wasmBinary: wasm, print: t => { log += t + '\n'; }, printErr: t => { log += t + '\n'; } });
	const FS = mod.FS;
	for (const [name, data] of include.concat(files(m.files))) {
		FS.mkdirTree(dirname(name));
		FS.writeFile(name, data);
	}
	FS.mkdirTree(m.cwd);
	FS.chdir(m.cwd);
	const out = m.args[m.args.indexOf('-o') + 1];
	FS.mkdirTree(dirname(out));
	let status = 0;
	try { mod.callMain(m.args); } catch (x) { status = x && x.status !== undefined ? x.status : 1; if (!(x && x.status !== undefined)) log += String(x) + '\n'; }
	let obj = null;
	try { obj = FS.readFile(out); } catch (x) {}
	const ok = status === 0 && obj !== null;
	postMessage({ id: m.id, ok, out, obj: ok ? obj : null, log, src: m.args[m.args.length - 1], ms: Math.round(performance.now() - t0) },
		ok ? [obj.buffer] : []);
};
