// gui-web's imports (monolith.h), an Emscripten library (--js-library).
// They run on the browser's main thread (__proxy) and hand the work to the
// page's web/monolith.js, Module.monolith: this file only moves data
// between wasm memory and JavaScript values.

addToLibrary({
	// the page may call drawterm's exports now
	js_ready__proxy: 'sync',
	js_ready__sig: 'v',
	js_ready: function() {
		Module.monolith.attach({
			input: (type, a, b, c) => { if(!runtimeExited) Module['_mo_input'](type, a, b, c); },
			netstate: (conn, gen, state) => { if(!runtimeExited) Module['_mo_netstate'](conn, gen, state); },
			netdata: (conn, gen, bytes) => {
				if(runtimeExited)
					return;
				var p = Module['_malloc'](bytes.length);
				HEAPU8.set(bytes, p);
				Module['_mo_netdata'](conn, gen, p, bytes.length);
				Module['_free'](p);
			},
		});
	},

	js_screensize__proxy: 'sync',
	js_screensize__sig: 'vpp',
	js_screensize: function(wp, hp) {
		var s = Module.monolith.size();
		HEAP32[wp >> 2] = s[0];
		HEAP32[hp >> 2] = s[1];
	},

	js_resize__proxy: 'sync',
	js_resize__sig: 'vii',
	js_resize: function(w, h) {
		Module.monolith.resize(w, h);
	},

	// rectangle x0,y0-x1,y1 of the XBGR32 screen (bytes R G B x) at base,
	// stride bytes a row, as ImageData (a copy: ImageData cannot view
	// shared memory), alpha set
	js_flush__proxy: 'sync',
	js_flush__sig: 'vpiiiii',
	js_flush: function(base, stride, x0, y0, x1, y1) {
		var w = x1 - x0, h = y1 - y0;
		if(w <= 0 || h <= 0)
			return;
		var img = new ImageData(w, h);
		var d = new Uint32Array(img.data.buffer);
		var s = HEAPU32, st = stride >> 2;
		var p = (base >> 2) + y0*st + x0;
		for(var y = 0, i = 0; y < h; y++, p += st)
			for(var x = 0; x < w; x++)
				d[i++] = s[p + x] | 0xff000000;
		Module.monolith.present(img, x0, y0);
	},

	js_cursor__proxy: 'sync',
	js_cursor__sig: 'vpii',
	js_cursor: function(rgba, hx, hy) {
		var img = new ImageData(16, 16);
		img.data.set(HEAPU8.subarray(rgba, rgba + 16*16*4));
		Module.monolith.cursor(img, hx, hy);
	},

	js_netopen__proxy: 'sync',
	js_netopen__sig: 'viip',
	js_netopen__deps: ['$UTF8ToString'],
	js_netopen: function(conn, gen, url) {
		Module.monolith.netopen(conn, gen, UTF8ToString(url));
	},

	// async: in order with the other calls, and drawterm does not wait
	js_netsend__proxy: 'async',
	js_netsend__sig: 'vipi',
	js_netsend: function(conn, p, n) {
		Module.monolith.netsend(conn, HEAPU8.slice(p, p + n));
		Module['_free'](p);
	},

	js_netclose__proxy: 'async',
	js_netclose__sig: 'vi',
	js_netclose: function(conn) {
		Module.monolith.netclose(conn);
	},
});
