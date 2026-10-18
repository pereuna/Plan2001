#!/bin/bash
# MicroPython as the first user program (docs/mpinit.md) in QEMU: the
# kernel (tools/build.sh; /boot/mp and /boot/kbdfs in its image) with
# plan9.ini init=/boot/mp -B, console=0.  Over the serial console
# (tools/9run's relay) Python's REPL: the plan9 module (/boot, kbdfs's
# /dev, #c/user, a file in /env, spawn + wait), Ctrl-C as
# KeyboardInterrupt, Ctrl-D starting the REPL again.
#   tools/test-mpinit.sh    no root disk: an ESP with the kernel
#   ROOT=1      the gefs root too (tools/gefsdisk, root=-w PARTUUID=; the
#               image build/$TARGET/root.gefs, made if missing): rc from
#               Python (a command, a program built, rc -i on the
#               console), a file written on the root, fshalt, then
#               tools/gefsck checks the partition and reads the file back
#   NET=1       a network too (QEMU's user net, a free host port to the
#               guest's 80): sys/lib/python/httpd.py typed in (paste
#               mode), net() and serve(), then the pages over HTTP from
#               here, an idle connection not blocking the others, Ctrl-C
#   KEEP=1      leave QEMU running (console: tools/9run -d build/$TARGET/mpvm)
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
. "$here/target.sh"
b=$tbuild
vm=$b/mpvm
for f in "$b/$kernel" "$b/$loader"; do
	[ -s "$f" ] || { echo "test-mpinit: no $f (tools/build.sh)" >&2; exit 1; }
done
mkdir -p "$vm"
mode=noroot
if [ "${ROOT:-0}" = 1 ]; then
	mode=root
	disk=$b/mprootdisk.img
	CONSOLE=0 INIT='/boot/mp -B' INI= "$here/gefsdisk" "$disk"
	drive=(-drive file="$disk",format=raw,if=none,id=d0 -device virtio-blk-pci-non-transitional,drive=d0,bootindex=0)
else
	printf 'bootfile=/%s\nconsole=0\nuser=glenda\ninit=/boot/mp -B\n' "$kernel" >"$vm/plan9.ini"
	rm -f "$vm/esp.img"
	mformat -C -i "$vm/esp.img" -T 65536 -h 64 -s 32 ::
	mmd -i "$vm/esp.img" ::/EFI ::/EFI/BOOT
	mcopy -i "$vm/esp.img" "$b/$loader" "::/EFI/BOOT/$espname"
	mcopy -i "$vm/esp.img" "$b/$kernel" ::/
	mcopy -i "$vm/esp.img" "$vm/plan9.ini" ::/plan9.ini
	drive=(-drive file="$vm/esp.img",format=raw,if=none,id=esp "${espdev[@]}")
fi

nic=(-nic none)
port=0
if [ "${NET:-0}" = 1 ]; then
	port=$(python3 -I -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
	nic=(-netdev user,id=n0,hostfwd=tcp:127.0.0.1:$port-:80 -device virtio-net-pci-non-transitional,netdev=n0)
fi

q= relay=
stop() {
	[ -n "$q" ] && { kill $q 2>/dev/null || true; wait $q 2>/dev/null || true; }
	[ -n "$relay" ] && { kill $relay 2>/dev/null || true; wait $relay 2>/dev/null || true; }
	q= relay=
}
trap 'if [ "${KEEP:-0}" = 1 ] && [ -n "$q" ]; then echo "QEMU left running (pid $q), console: tools/9run -d $vm"; else stop; fi' EXIT
rm -f "$vm"/*.sock "$vm/serial.in"
: > "$vm/serial.log"
cp "$fwvars" "$vm/vars.fd"
"$qemu" -name plan2001-mpinit -nodefaults "${qemuargs[@]}" -m 2048 -smp 2 \
	-drive if=pflash,format=raw,readonly=on,file="$fwcode" \
	-drive if=pflash,format=raw,file="$vm/vars.fd" \
	"${drive[@]}" \
	"${nic[@]}" -display none \
	-chardev socket,id=ser,path="$vm/serial.sock",server=on,wait=off -serial chardev:ser \
	2>"$vm/qemu.log" &
q=$!
mkfifo "$vm/serial.in"
python3 "$here/9run" --relay "$vm" </dev/null >/dev/null 2>&1 &
relay=$!

# the REPL: each STEP a line typed (\r), or a control character; then
# the output up to the next prompt must match its regex
st=0
python3 -I - "$vm" "$mode" "$port" "$root/sys/lib/python/httpd.py" <<'EOF' || st=1
import os, re, sys, time, socket, urllib.request, urllib.error
vm, mode, port, httpd = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
log = os.path.join(vm, 'serial.log')
def text():
    with open(log, 'rb') as f:
        s = f.read().decode('utf-8', 'replace')
    return re.sub(r'\x1b\[[0-9;=?]*[a-zA-Z]', '', s).replace('\r', '')
def wait(pat, start, t):
    end = time.time() + t
    while time.time() < end:
        m = re.compile(pat, re.S).search(text(), start)
        if m:
            return m
        time.sleep(0.2)
    return None
def typ(s):
    with open(os.path.join(vm, 'serial.in'), 'w') as f:
        f.write(s)
fails = 0
def check(name, ok, out=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name)
    if not ok:
        fails += 1
        print('\t' + out[-600:].replace('\n', '\n\t'))
m = wait(r'>>> $', 0, 150)
t = text()
check('boot: the REPL' + (' on the gefs root' if mode == 'root' else ' without a root'), m is not None, t)
check('boot.py ran', 'boot: MicroPython from the kernel, %s\n' % ('root' if mode == 'root' else 'no root') in t, t)
if mode == 'root':
    check('the kernel mounted gefs', re.search(r'^gefs: .*read-write', t, re.M) is not None, t)
if m is None:
    sys.exit(1)
# a step: a line typed (\r) or a control character, the regex its output
# must match up to the prompt (None: none awaited), the prompt, seconds
steps = [
    ('import plan9 as p', r''),
    ("print(sorted(d[0] for d in p.dirread('/boot')))", r"\['kbdfs', 'mp'\]"),
    ("print([n in [d[0] for d in p.dirread('/dev')] for n in ('cons', 'consctl', 'kbmap')])", r'\[True, True, True\]'),
    ("f = p.open('#c/user'); print(p.read(f)); p.close(f)", r"b'glenda'"),
]
common, steps = steps, []
if mode == 'noroot':
    steps += [
        ("f = p.create('/env/mptest', p.OWRITE, 0o664); p.write(f, 'hello'); p.close(f)", r''),
        ("f = p.open('/env/mptest'); print(p.read(f)); p.close(f); p.remove('/env/mptest')", r"b'hello'"),
        ("pid = p.spawn(['/boot/mp', '-c', 'print(6*7)']); w = p.wait(); print('wait', w[0] == pid, repr(w[1]))", r"42.*wait True ''"),
        ("pid = p.spawn(['/boot/mp', '-c', 'raise ValueError']); print(p.wait()[1])", r'ValueError.*micropython'),
        ("print(f'{6*7}', '%d-%s' % (1, 'x'), 'äö'[1], len('äö'))", r'42 1-x ö 2'),
        ("p.open('/nonexistent')", r"OSError: .*does not exist"),
        ('while True: pass\r', None),
        ('\x03', r'KeyboardInterrupt'),
        ('\x04', r'MicroPython v1\.27\.0'),
        ("print('again')", r'again'),
    ]
else:
    def rc(cmd):
        return "p.spawn(['/bin/rc', '-c', %r]); print('status', repr(p.wait()[1]))" % cmd
    steps += [
        ("print(p.fd2path(p.open('/')), len(p.dirread('/amd64/bin')) > 50)", r"^/ True"),
        (rc('echo rc on $cputype; ls /bin/rc /bin/fshalt'), r"rc on amd64\n/bin/fshalt\n/bin/rc\nstatus ''"),
        (rc('ls /nonexistent'), r"does not exist.*status '.+'"),
        ("f = p.create('/usr/glenda/mproot', p.OWRITE, 0o664); p.write(f, 'from python on gefs\\n'); p.close(f)", r''),
        (rc('mkdir -p /tmp && cd /sys/src/cmd && 6c -FTVw cat.c && 6l -o /tmp/6.cat cat.6 && rm cat.6 && /tmp/6.cat /usr/glenda/mproot'),
            r"^from python on gefs\nstatus ''", '>>> $', 180),
        # rc on the console, cooked, while Python waits; Ctrl-D ends it
        ("p.spawn(['/bin/rc', '-i']); p.wait()", r'', '% $'),
        ('echo rc -i from python', r'^rc -i from python', '% $'),
        ('\x04', r"\(\d+, ''\)"),
        ("print('back')", r'^back'),
        (rc('fshalt'), r"done halting.*status ''", '>>> $', 240),
        # the root is gone; init and the REPL are the kernel's
        ("print(sorted(d[0] for d in p.dirread('/boot')))", r"\['kbdfs', 'mp'\]"),
    ]
def run(steps):
    for step in steps:
        line, pat = step[0], step[1]
        prompt = step[2] if len(step) > 2 else '>>> $'
        secs = step[3] if len(step) > 3 else 30
        start = len(text())
        typ(line if line in ('\x03', '\x04', '\x05') else line + '\r')
        if pat is None:
            time.sleep(1)
            continue
        m = wait(prompt, start + 1, secs)
        out = text()[start:]
        check(repr(line)[:70], m is not None and re.search(pat, out, re.M | re.S) is not None, out)
def get(path, t=10):
    try:
        with urllib.request.urlopen('http://127.0.0.1:%d%s' % (port, path), timeout=t) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()
    except OSError as e:
        return None, repr(e)
run(common)
if port:
    # httpd.py typed in as on a console: paste mode, a line at a time
    start = len(text())
    run([('\x05', r'paste mode', '=== $')])
    pasted = True
    for line in open(httpd).read().split('\n'):
        n = len(text())
        typ(line + '\n')
        if wait(r'=== $', n + 1, 10) is None:
            pasted = False
            break
    check('httpd.py pasted', pasted, text()[start:])
    run([('\x04', r'\A(?!.*Error)', '>>> $'),
         ('net()', r''),
         ('serve()', r'httpd: /net/tcp/\d+ port 80', r'port 80\n', 15)])
    st, body = get('/')
    check('GET / ', st == 200 and 'MicroPython as init' in body and 'pid' in body, body)
    st, body = get('/boot')
    check('GET /boot', st == 200 and '<li>mp ' in body and '<li>kbdfs ' in body, body)
    st, body = get('/env/cputype')
    check('GET /env/cputype', st == 200 and body == 'amd64\n', body)
    st, body = get('/env/nonexistent')
    check('GET /env/nonexistent: 500', st == 500 and 'does not exist' in body, body)
    idle = socket.create_connection(('127.0.0.1', port))
    st, body = get('/', 5)
    check('GET / with an idle connection open', st == 200, body)
    idle.close()
    sts = [get('/')[0] for i in range(20)]
    check('20 GETs', sts == [200] * 20, repr(sts))
    time.sleep(1)
    pids = set(re.findall(r'^httpd: 10\.0\.2\.2!\d+ GET \S+ 200 pid (\d+)', text(), re.M))
    check('each call in a process of its own', len(pids) >= 24 and '1' not in pids, repr(pids))
    run([('\x03', r'KeyboardInterrupt'),
         ('print(p.getpid())', r'^1$')])
run(steps)
if mode == 'root':
    check('gefs halted', wait(r'gefs: halted', 0, 30) is not None, text())
print('PASS: MicroPython init' if fails == 0 else 'FAIL: %d' % fails)
sys.exit(1 if fails else 0)
EOF
[ "$mode" = root ] || exit $st
stop

# the partition checked and read back in the build VM (userspace gefs)
part=$b/mprootpart.img
rm -f "$part"
mib=$(( $(stat -c %s "$disk") / 1048576 - 66 ))
dd if="$disk" of="$part" bs=1M skip=65 count=$mib conv=sparse status=none
echo "### gefsck"
"$here/gefsck" "$part" /usr/glenda/mproot > "$vm/out" 2>&1 || st=1
sed 's/^/	/' "$vm/out" | tail -5
grep -q '^ok' "$vm/out" || { echo "FAIL: gefsck: not ok"; st=1; }
grep -q '^from python on gefs' "$vm/out" || { echo "FAIL: gefsck: no /usr/glenda/mproot"; st=1; }
rm -f "$part"
[ $st = 0 ] && echo "PASS: MicroPython init on the gefs root" || echo FAIL
exit $st
