// A compute resource (CR) in a browser tab (docs/cpu-server-design.md): the
// compute origin's page (compute.MACHINE: policy cr, docs/app-origins.md).
// Sharing is the user's choice: nothing is offered before "share spare
// compute", and stop (or closing the tab) leaves the pool.  The page is
// dull on purpose: black, a clock, "CR available" and a breathing dot.
//
// Two levels of scheduling: the CPU server (crsrv) gives this CR jobs, as
// many at once as its credits say; which web worker runs a job is decided
// here, from the CR's own queue.  The server sees one CR with its workers
// and credits, not the workers.  One CR per computer: the tab holding the
// Web Lock shares; another tab of this origin waits for it.
//
// Connects to crsrv through webterm (wss://HOST/17030): hello (key, type,
// api, workers, owner, credits), then the headers (include) and compile
// jobs (job), run in web workers with 6c.wasm (cr-worker.js), results back
// (result).  Messages: a 4-byte big-endian length, a header line (tab-
// separated fields), a body of files (name, NUL, 4-byte length, data).
//   ?workers=N   threads to share (default the CPU's threads less one)
//   ?autostart   share at once (tests; the user's choice is the button)
//   ?nolock      tests: tabs of one browser as separate computers
//   ?corrupt     tests: a bad CR (one byte of every object changed)
//   #key=KEY     crsrv -k
// Its owner is not the browser's to say: "-" until CRs authenticate.
'use strict';
(() => {
const q = new URLSearchParams(location.search);
const frag = new URLSearchParams(location.hash.slice(1));
const key = frag.get('key') || '';
const hc = navigator.hardwareConcurrency || 2;
const $ = id => document.getElementById(id);
const enc = new TextEncoder(), dec = new TextDecoder();
let nworkers = Math.max(1, Math.min(64, +q.get('workers') || hc - 1));
let ndone = 0, sharing = false, since = 0, shared = 0, release = null;

function log(t) {
	const l = $('log');
	l.textContent = (new Date().toISOString().slice(11, 19) + ' ' + t + '\n' + l.textContent).slice(0, 20000);
	console.log('cr: ' + t);
}
// the page is meant to be dull: a clock, one line, one breathing dot
function state(t, on) { $('state').textContent = t; $('dot').className = on ? 'on' : ''; }

$('threads').max = Math.max(1, hc);
$('threads').value = nworkers;
$('tval').textContent = `${nworkers} / ${hc}`;
$('threads').oninput = () => { nworkers = +$('threads').value; $('tval').textContent = `${nworkers} / ${hc}`; };
function stats() {
	if (!sharing) { $('sharing').textContent = $('jobs').textContent = $('time').textContent = ''; return; }
	const t = Math.floor((shared + (since ? Date.now() - since : 0)) / 1000);
	$('sharing').textContent = `${nworkers} threads ·`;
	$('jobs').textContent = `${ndone} jobs ·`;
	$('time').textContent = [t / 3600, t / 60 % 60, t % 60].map(x => String(Math.floor(x)).padStart(2, '0')).join(':');
}

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

// the local scheduler: the CR's queue and its workers, each with 6c.wasm
let wasm = null, include = null;
const workers = [], waiting = [];
function credits() { return nworkers + Math.max(1, Math.ceil(nworkers / 4)); }	// a little queue: no worker waits on the network
function busy() {}
function work(w) {
	if (waiting.length === 0 || !include) { w.busy = false; busy(); return; }
	w.busy = true;
	w.postMessage(waiting.shift());
	busy();
}
function startworkers() {
	for (let i = 0; i < nworkers; i++) {
		const w = new Worker('cr-worker.js');
		w.busy = false;
		w.postMessage({ type: 'init', wasm, include });
		w.onmessage = e => {
			const r = e.data;
			const files = [['log', enc.encode(r.log)]];
			if (r.ok && q.has('corrupt')) r.obj[r.obj.length >> 1] ^= 0xff;	// tests: a CR not to be trusted
			if (r.ok) files.push([r.out, r.obj]);
			send(msg(`result\t${r.id}\t${r.ok ? 'ok' : 'fail'}`, bundle(files)));
			ndone++;
			log(`job ${r.id} ${r.ok ? 'ok' : 'failed'} ${r.src} (${r.ms} ms)`);
			work(w);
		};
		workers.push(w);
	}
	busy();
}
function stopworkers() {
	for (const w of workers) w.terminate();
	workers.length = 0;
	waiting.length = 0;
	busy();
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
	if (!sharing) return;
	state('connecting', false);
	ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/17030`);
	ws.binaryType = 'arraybuffer';
	buf = new Uint8Array(0);
	ws.onopen = () => {
		send(msg(`hello\t${key}\tbrowser-cpu\twasm\t${nworkers}\t-\t${credits()}`));
		state('CR available', true);
		since = Date.now();
		log(`connected: ${nworkers} workers, ${credits()} credits`);
	};
	ws.onmessage = e => received(e.data);
	ws.onclose = () => {
		if (since) { shared += Date.now() - since; since = 0; }
		stopworkers();	// the server gives their jobs to another CR
		if (!sharing) return;
		state('reconnecting', false);
		log('disconnected');
		setTimeout(connect, 3000);
	};
}

function begin() {
	sharing = true;
	$('go').textContent = 'stop'; $('go').disabled = false;
	$('choose').style.visibility = 'hidden';
	(wasm ? Promise.resolve() : fetch('6c.wasm').then(r => { if (!r.ok) throw r.status; return r.arrayBuffer(); }).then(b => { wasm = b; }))
		.then(connect)
		.catch(e => { state('no 6c.wasm: ' + e, false); stop(); });
}
// one CR per computer: the tab holding the lock shares, another waits for it
let pending = false;
function start() {
	if (sharing || release || pending) return;
	pending = true;
	$('go').disabled = true;
	if (navigator.locks && !q.has('nolock')) {
		state('another tab shares; waiting', false);
		navigator.locks.request('plan2001-cr', () => new Promise(r => { release = r; pending = false; begin(); }));
	} else {
		release = () => {};
		pending = false;
		begin();
	}
}
function stop() {
	sharing = false;
	if (ws) { ws.onclose(); ws.onclose = null; ws.close(); ws = null; }
	if (release) { release(); release = null; }
	$('go').textContent = 'share spare compute'; $('go').disabled = false;
	$('choose').style.visibility = '';
	state('CR off', false);
	log('stopped');
}
$('go').onclick = () => sharing ? stop() : start();
// the clock, and the few numbers under it
function tick() {
	const d = new Date();
	$('clock').textContent = [d.getHours(), d.getMinutes(), d.getSeconds()].map(x => String(x).padStart(2, '0')).join(':');
	stats();
}
tick();
setInterval(tick, 1000);

if (q.has('autostart')) start();
window.cr = { get done() { return ndone; }, get workers() { return nworkers; }, get sharing() { return sharing; }, start, stop };
})();
