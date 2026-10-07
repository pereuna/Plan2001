onmessage = async (e) => {
	const out = { iso: self.crossOriginIsolated };
	try {
		const a = new Int32Array(e.data.mem.buffer);
		a[1] = 4242;
		Atomics.store(a, 0, 1);
		out.notified = Atomics.notify(a, 0);
	} catch (x) { out.mem = 'ERR ' + x; }
	try {
		const d = await navigator.storage.getDirectory();
		const f = await d.getFileHandle('sdW0', { create: true });
		const h = await f.createSyncAccessHandle();
		h.write(new Uint8Array([1, 2, 3]), { at: 0 });
		out.opfs = 'sync handle ok, size ' + h.getSize();
		h.close();
	} catch (x) { out.opfs = 'ERR ' + x; }
	try {
		const w = new Worker(new URL('leaf.js', import.meta.url), { type: 'module' });
		out.grandchild = await new Promise((done) => { w.onmessage = (m) => done(m.data); w.onerror = (m) => done('ERR ' + m.message); setTimeout(() => done('timeout'), 5000); });
	} catch (x) { out.grandchild = 'ERR ' + x; }
	postMessage(out);
};
