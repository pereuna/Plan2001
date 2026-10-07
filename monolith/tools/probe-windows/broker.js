// the machine's tab registers; a tab that comes later gets a MessagePort to it (no shared memory: messages)
let machine = null;
onconnect = (e) => {
	const p = e.ports[0];
	p.onmessage = (m) => {
		if (m.data.machine) machine = p;
		else if (m.data.term) {
			if (!machine) { p.postMessage({ nomachine: 1 }); return; }
			const c = new MessageChannel();
			machine.postMessage({ port: c.port1 }, [c.port1]);
			p.postMessage({ port: c.port2 }, [c.port2]);
		}
	};
	p.start();
};
