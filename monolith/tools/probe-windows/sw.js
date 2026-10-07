const id = Math.random().toString(36).slice(2, 8);
let conns = 0;
const ports = [];
const res = { id, iso: self.crossOriginIsolated, sab: typeof SharedArrayBuffer, worker: typeof Worker, waitAsync: typeof Atomics.waitAsync };
let mem = null;
try { mem = new WebAssembly.Memory({ initial: 1, maximum: 4, shared: true }); res.sharedmem = 'ok'; } catch (e) { res.sharedmem = 'ERR ' + e; }
try { res.atomicswait = Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 5); } catch (e) { res.atomicswait = 'ERR ' + e; }
// a nested Worker (a proc's): the shared memory, Atomics both ways, OPFS's sync handle, a Worker of its own
const nested = new Promise((done) => {
	let w;
	try { w = new Worker(new URL('nested.js', import.meta.url), { type: 'module' }); } catch (e) { done({ ERR: String(e) }); return; }
	w.onerror = (e) => done({ ERR: 'onerror ' + (e.message || e) });
	w.onmessage = (e) => done(e.data);
	try { w.postMessage({ mem }); } catch (e) { done({ ERR: 'post ' + e }); }
	setTimeout(() => done({ ERR: 'timeout' }), 10000);
});
const waited = (async () => {
	if (!mem) return 'no mem';
	const a = new Int32Array(mem.buffer);
	const r = Atomics.waitAsync(a, 0, 0, 8000);
	return r.async ? await r.value : r.value;
})();
onconnect = async (e) => {
	const p = e.ports[0];
	conns++;
	ports.push(p);
	p.onmessage = async (m) => {
		if (m.data.sendmem) {
			try { p.postMessage({ mem }); p.postMessage({ sentmem: 'ok' }); } catch (x) { p.postMessage({ sentmem: 'ERR ' + x }); }
		} else if (m.data.hello !== undefined) {
			p.postMessage({ ...res, conns, hello: m.data.hello, nested: await nested, waited: await waited, word1: mem ? new Int32Array(mem.buffer)[1] : null });
		} else if (m.data.count) {
			p.postMessage({ id, conns, alive: ports.length });
		}
	};
	p.start();
};
