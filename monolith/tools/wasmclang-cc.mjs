// One Plan 9 program's C compiled by clang.wasm (YoWASP's LLVM, run by
// Node as a browser would run it: no disk, a virtual file system) for
// drawterm's kernel (wasmapp/): tools/test-wasmclang runs it.
//   node wasmclang-cc.mjs YOWASP DEPS BUILD SRC OUT
// DEPS: the headers emcc -M names; the Emscripten sysroot's go in as
// sysroot/, clang's own builtin headers are clang.wasm's.
import { readFileSync, writeFileSync } from 'node:fs';
const [yowasp, deps, build, src, out] = process.argv.slice(2);
const { runClang } = await import(yowasp + '/gen/bundle.js');
const SR = '/usr/share/emscripten/cache/sysroot/';
const files = {};
let n = 0, bytes = 0;
function put(vpath, real) {
	const parts = vpath.split('/');
	let d = files;
	for (const p of parts.slice(0, -1)) d = d[p] ??= {};
	const b = readFileSync(real);
	n++;
	bytes += b.length;
	d[parts.at(-1)] = b;
}
for (const f of readFileSync(deps, 'utf8').split('\n').filter(Boolean)) {
	if (f.startsWith(SR)) put('sysroot/' + f.slice(SR.length), f);
	else if (f.startsWith('/usr/lib/llvm')) continue;
	else put(f.replace(/^\.\//, ''), build + '/' + f);
}
// emcc's own clang flags (emcc -v), and the app's (wasmapp.mk)
const args = ['clang', '--target=wasm32-unknown-emscripten', '-nostdlibinc',
	'-isystem', 'sysroot/include/compat',
	'-isystem', 'sysroot/include/wasm32-emscripten', '-isystem', 'sysroot/include',
	'-D__EMSCRIPTEN_SHARED_MEMORY__=1', '-DEMSCRIPTEN', '-pthread', '-matomics', '-mbulk-memory',
	'-fignore-exceptions', '-fvisibility=hidden', '-mllvm', '-enable-emscripten-sjlj',
	'-include', 'wasmapp/plan9.h', '-I.', '-Iinclude', '-Ikern', '-w', '-O2',
	'-Dlinux', '-D_THREAD_SAFE', '-DPTHREAD', '-D_REENTRANT', '-D__builtin_return_address(x)=0',
	'-Dmain=wasmappmain', '-c', src, '-o', 'out.o'];
const t = Date.now();
const res = await runClang(args, files, {
	stdout: b => b && process.stdout.write(b), stderr: b => b && process.stderr.write(b)
});
if (!res['out.o'])
	throw new Error('no object');
writeFileSync(out, res['out.o']);
console.log(`wasmclang-cc: ${src}: ${n} files, ${bytes} bytes in; ${res['out.o'].length} bytes out; ${Date.now() - t} ms`);
