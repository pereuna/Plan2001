#!/bin/bash
# MicroPython as the first user program (docs/mpinit.md) in QEMU, with no
# root disk: an ESP with the kernel (tools/build.sh; /boot/mp and
# /boot/kbdfs in its image) and plan9.ini init=/boot/mp -B, console=0.
# Over the serial console (tools/9run's relay) Python's REPL: the plan9
# module (/boot, kbdfs's /dev, #c/user, a file in /env, spawn + wait),
# Ctrl-C as KeyboardInterrupt, Ctrl-D starting the REPL again.
#   tools/test-mpinit.sh
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
printf 'bootfile=/%s\nconsole=0\nuser=glenda\ninit=/boot/mp -B\n' "$kernel" >"$vm/plan9.ini"
rm -f "$vm/esp.img"
mformat -C -i "$vm/esp.img" -T 65536 -h 64 -s 32 ::
mmd -i "$vm/esp.img" ::/EFI ::/EFI/BOOT
mcopy -i "$vm/esp.img" "$b/$loader" "::/EFI/BOOT/$espname"
mcopy -i "$vm/esp.img" "$b/$kernel" ::/
mcopy -i "$vm/esp.img" "$vm/plan9.ini" ::/plan9.ini

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
"$qemu" -name plan2001-mpinit -nodefaults "${qemuargs[@]}" -m 1024 -smp 2 \
	-drive if=pflash,format=raw,readonly=on,file="$fwcode" \
	-drive if=pflash,format=raw,file="$vm/vars.fd" \
	-drive file="$vm/esp.img",format=raw,if=none,id=esp "${espdev[@]}" \
	-nic none -display none \
	-chardev socket,id=ser,path="$vm/serial.sock",server=on,wait=off -serial chardev:ser \
	2>"$vm/qemu.log" &
q=$!
mkfifo "$vm/serial.in"
python3 "$here/9run" --relay "$vm" </dev/null >/dev/null 2>&1 &
relay=$!

# the REPL: each STEP a line typed (\r), or a control character; then
# the output up to the next prompt must match its regex
python3 -I - "$vm" <<'EOF'
import os, re, sys, time
vm = sys.argv[1]
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
m = wait(r'>>> $', 0, 120)
t = text()
check('boot: the REPL without a root', m is not None, t)
check('boot.py ran', 'boot: MicroPython from the kernel, no root' in t, t)
if m is None:
    sys.exit(1)
steps = [
    ('import plan9 as p', r''),
    ("print(sorted(d[0] for d in p.dirread('/boot')))", r"\['kbdfs', 'mp'\]"),
    ("print([n in [d[0] for d in p.dirread('/dev')] for n in ('cons', 'consctl', 'kbmap')])", r'\[True, True, True\]'),
    ("f = p.open('#c/user'); print(p.read(f)); p.close(f)", r"b'glenda'"),
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
for line, pat in steps:
    start = len(text())
    typ(line if line in ('\x03', '\x04') else line + '\r')
    if pat is None:
        time.sleep(1)
        continue
    m = wait(r'>>> $', start + 1, 30)
    out = text()[start:]
    check(repr(line)[:70], m is not None and re.search(pat, out, re.M | re.S) is not None, out)
print('PASS: MicroPython init' if fails == 0 else 'FAIL: %d' % fails)
sys.exit(1 if fails else 0)
EOF
