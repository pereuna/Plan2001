// A compute resource (CR) in a browser tab (docs/cpu-server-design.md):
// connects to the CPU server's crsrv through webterm (wss://HOST/17030),
// says what it offers (hello), takes the headers (include) and compile
// jobs (job), runs them in web workers with 6c.wasm (cr-worker.js) and
// sends the results (result).  Messages: a 4-byte big-endian length, a
// header line (tab-separated fields), a body of files (name, NUL, 4-byte
// length, data).  ?workers=N (default the CPU's threads), ?owner=NAME,
// #key=KEY (crsrv -k).
'use strict';
(() => {
const q = new URLSearchParams(location.search);
const frag = new URLSearchParams(location.hash.slice(1));
const key = frag.get('key') || '';
const owner = (q.get('owner') || 'browser').replace(/\s/g, '_');
const nworkers = Math.max(1, Math.min(64, +q.get('workers') || navigator.hardwareConcurrency || 4));
const $ = id => document.getElementById(id);
const enc = new TextEncoder(), dec = new TextDecoder();
let ndone = 0;
function log(t) {
	const l = $('log');
	l.textContent = (new Date().toISOString().slice(11, 19) + ' ' + t + '\n' + l.textContent).slice(0, 20000);
	console.log('cr: ' + t);
}
function state(t, on) { $('state').textContent = t; $('state').className = on ? 'on' : 'off'; }

function msg(hdr, body) {
	const h = enc.encode(hdr + '\n');
	const m = new Uint8Array(4 + h.length + (body ? body.length : 0));
	new DataView(m.buffer).setUint32(0, h.length + (body ? body.length : 0));
	m.set(h, 4);
	if (body) m.set(body, 4 + h.length);
	return m;
}
function bundle(files) {
	let n = 0;
	const parts = files.map(([name, data]) => { const b = enc.encode(name); n += b.length + 5 + data.length; return [b, data]; });
	const out = new Uint8Array(n);
	const dv = new DataView(out.buffer);
	let o = 0;
	for (const [b, data] of parts) {
		out.set(b, o); o += b.length; out[o++] = 0;
		dv.setUint32(o, data.length); o += 4;
		out.set(data, o); o += data.length;
	}
	return out;
}

// the workers, each with 6c.wasm; a job waits for a free one
let wasm = null, include = null;
const workers = [], waiting = [];
function work(w) {
	if (waiting.length === 0 || !include) { w.busy = false; return; }
	w.busy = true;
	w.postMessage(waiting.shift());
	$('busy').textContent = workers.filter(x => x.busy).length;
}
function startworkers() {
	for (let i = 0; i < nworkers; i++) {
		const w = new Worker('cr-worker.js');
		w.busy = false;
		w.postMessage({ type: 'init', wasm, include });
		w.onmessage = e => {
			const r = e.data;
			const files = [['log', enc.encode(r.log)]];
			if (r.ok) files.push([r.out, r.obj]);
			send(msg(`result\t${r.id}\t${r.ok ? 'ok' : 'fail'}`, bundle(files)));
			ndone++;
			$('jobs').textContent = ndone;
			log(`job ${r.id} ${r.ok ? 'ok' : 'failed'} ${r.src} (${r.ms} ms)`);
			work(w);
			$('busy').textContent = workers.filter(x => x.busy).length;
		};
		workers.push(w);
	}
	$('workers').textContent = nworkers;
}

let ws = null, buf = new Uint8Array(0);
function send(m) {
	if (!ws || ws.readyState !== 1) return;
	for (let o = 0; o < m.length; o += 256 * 1024)	// webterm takes frames up to 1 MB
		ws.send(m.subarray(o, Math.min(m.length, o + 256 * 1024)));
}
function received(data) {
	const b = new Uint8Array(buf.length + data.byteLength);
	b.set(buf); b.set(new Uint8Array(data), buf.length);
	buf = b;
	while (buf.length >= 4) {
		const n = new DataView(buf.buffer, buf.byteOffset).getUint32(0);
		if (buf.length < 4 + n) break;
		const m = buf.subarray(4, 4 + n);
		buf = buf.slice(4 + n);
		const nl = m.indexOf(10);
		const f = dec.decode(m.subarray(0, nl < 0 ? m.length : nl)).split('\t');
		const body = nl < 0 ? new Uint8Array(0) : m.slice(nl + 1);
		if (f[0] === 'include') {
			include = body;
			log(`headers: ${(body.length / 1024).toFixed(0)} KB`);
			if (workers.length === 0) startworkers();
			else for (const w of workers) w.postMessage({ type: 'init', wasm, include });
			for (const w of workers) if (!w.busy) work(w);
		} else if (f[0] === 'job') {
			const [, id, cwd, ...args] = f;
			waiting.push({ type: 'job', id, cwd, args, files: body });
			const w = workers.find(x => !x.busy);
			if (w) work(w);
		}
	}
}

function connect() {
	state('connecting', false);
	ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/17030`);
	ws.binaryType = 'arraybuffer';
	buf = new Uint8Array(0);
	ws.onopen = () => {
		send(msg(`hello\t${key}\tbrowser-cpu\twasm\t${nworkers}\t${owner}`));
		state('in the pool', true);
		log(`connected, ${nworkers} workers`);
	};
	ws.onmessage = e => received(e.data);
	ws.onclose = () => {
		state('disconnected, retrying', false);
		log('disconnected');
		waiting.length = 0;
		setTimeout(connect, 3000);
	};
}

fetch('6c.wasm').then(r => r.arrayBuffer()).then(b => { wasm = b; connect(); })
	.catch(e => { state('no 6c.wasm: ' + e, false); });
window.cr = { get done() { return ndone; }, get workers() { return nworkers; } };
})();
