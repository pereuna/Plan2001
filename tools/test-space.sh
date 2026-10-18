#!/bin/bash
# The persistent object environment (docs/object-model.md) in QEMU: the
# gefs root (tools/gefsdisk, root=-w PARTUUID=) with plan9.ini
# init=/boot/mp -B, console=0, booted again and again on the same disk.
# Each boot is driven over the serial console in Python's raw REPL (as
# tools/test-mpinit.sh); a crash is QEMU killed (no fshalt, no sync).
# At the end tools/gefsck checks the partition in the build VM.
#   PHASE=0     the phases' tests up to this one (docs/object-model.md 7)
#   CRASH=3     crashes in the crash tests
# V0 (sync and exclusive use):
#   T0.1  a file written, plan9.sync, QEMU killed as soon as it returns:
#         the next boot has it (CRASH times, 1 MiB each, checksummed)
#   T0.2  DMEXCL: a second open fails, in this process and another;
#         close or the holder's death frees it; OCEXEC: no fd after exec
#   T0.3  plan9.sync's time: a small change and 1 MiB (median of 10)
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$(dirname "$here")
. "$here/target.sh"
b=$tbuild
vm=$b/spacevm
phase=${PHASE:-0}
for f in "$b/$kernel" "$b/$loader"; do
	[ -s "$f" ] || { echo "test-space: no $f (tools/build.sh)" >&2; exit 1; }
done
mkdir -p "$vm"
disk=$b/spacedisk.img
CONSOLE=0 INIT='/boot/mp -B' INI= "$here/gefsdisk" "$disk"
echo "test-space: PHASE=$phase CRASH=${CRASH:-3}; limits: a boot 150 s, a step 30-120 s, all about $(( ${CRASH:-3} + 2 )) min (QEMU, KVM)"

st=0
QEMU=$qemu QEMUARGS="${qemuargs[*]}" FWCODE=$fwcode FWVARS=$fwvars \
python3 -I - "$vm" "$disk" "$here/9run" "$phase" "${CRASH:-3}" <<'EOF' || st=1
import os, re, sys, time, shutil, subprocess, hashlib
vm, disk, run9, phase, ncrash = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
log = os.path.join(vm, 'serial.log')
q = relay = None

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
        time.sleep(0.1)
    return None
def typ(s):
    with open(os.path.join(vm, 'serial.in'), 'w') as f:
        f.write(s)
fails = 0
def check(name, ok, out=''):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name, flush=True)
    if not ok:
        fails += 1
        print('\t' + out[-600:].replace('\n', '\n\t'), flush=True)
    return ok

def stop():
    global q, relay
    for p in (q, relay):
        if p:
            p.kill()
            p.wait()
    q = relay = None
nboot = 0
def boot():
    """QEMU on the disk, the relay, the REPL, then the raw REPL"""
    global q, relay, nboot
    nboot += 1
    for f in ('serial.sock', 'serial.in'):
        try:
            os.remove(os.path.join(vm, f))
        except FileNotFoundError:
            pass
    open(log, 'w').close()
    shutil.copy(os.environ['FWVARS'], os.path.join(vm, 'vars.fd'))
    q = subprocess.Popen([os.environ['QEMU'], '-name', 'plan2001-space', '-nodefaults'] +
        os.environ['QEMUARGS'].split() + ['-m', '2048', '-smp', '2',
        '-drive', 'if=pflash,format=raw,readonly=on,file=' + os.environ['FWCODE'],
        '-drive', 'if=pflash,format=raw,file=' + os.path.join(vm, 'vars.fd'),
        '-drive', 'file=%s,format=raw,if=none,id=d0' % disk,
        '-device', 'virtio-blk-pci-non-transitional,drive=d0,bootindex=0',
        '-nic', 'none', '-display', 'none',
        '-chardev', 'socket,id=ser,path=%s,server=on,wait=off' % os.path.join(vm, 'serial.sock'),
        '-serial', 'chardev:ser'], stderr=open(os.path.join(vm, 'qemu.log'), 'w'))
    os.mkfifo(os.path.join(vm, 'serial.in'))
    relay = subprocess.Popen(['python3', run9, '--relay', vm], stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    m = wait(r'>>> $', 0, 150)
    if not check('boot %d: the REPL on the gefs root' % nboot, m is not None and
            re.search(r'^gefs: .*read-write', text(), re.M) is not None, text()):
        return False
    start = len(text())
    typ('\x01')
    return check('boot %d: the raw REPL' % nboot, wait(r'raw REPL; CTRL-B to exit\n>$', start, 10) is not None, text()[start:])
def crash():
    print('=== boot %d: QEMU killed' % nboot, flush=True)
    stop()
def halt():
    raw("p.spawn(['/bin/rc', '-c', 'fshalt']); print('status', repr(p.wait()[1]))", r"done halting.*status ''", None, 240)
    check('gefs halted', wait(r'gefs: halted', 0, 30) is not None, text())
    stop()

rawre = re.compile(r'OK(.*?)\x04(.*?)\x04>', re.S)
def raw(code, pat, err=None, secs=30, name=None):
    """a raw REPL step: its output and error must match; the output"""
    start = len(text())
    typ(code + '\x04')
    m = wait(rawre.pattern, start, secs)
    name = name or repr(code)[:70]
    if m is None:
        check(name, False, text()[start:])
        return None
    out, e = m.group(1).strip('\n'), m.group(2)
    ok = re.search(pat, out, re.M | re.S) is not None
    ok = ok and (not e if err is None else re.search(err, e, re.S) is not None)
    check(name, ok, 'output: %r\nerror: %r' % (out, e))
    return out

def blob(i):
    """1 MiB the guest makes and the host checks: line i repeated"""
    line = ('crash %d synced ' % i).ljust(63, '.') + '\n'
    return line * (1048576 // 64)

def v0():
    # T0.1: sync then killed at once
    for i in range(1, ncrash + 1):
        if not boot():
            return
        raw('import plan9 as p', r'\A\Z')
        if i > 1:
            prev = blob(i - 1)
            raw("print(open('/usr/glenda/synced%d').read() == %r * %d)" % (i - 1, prev[:64], len(prev) // 64),
                r'^True$', name='T0.1 boot %d: synced%d after the crash' % (i, i - 1))
        raw("s = %r * %d\nwith open('/usr/glenda/synced%d', 'w') as f:\n    f.write(s)\np.sync('/usr/glenda/synced%d')\nprint('synced', %d)"
            % (blob(i)[:64], 1048576 // 64, i, i, i), r'^synced %d$' % i, secs=60, name='T0.1 boot %d: write, sync' % i)
        # an unsynced file too, killed within a second: may be lost
        typ("f = open('/usr/glenda/unsynced%d', 'w'); f.write('x'); f.close(); print('written')\x04" % i)
        time.sleep(0.5)
        crash()
    if not boot():
        return
    raw('import plan9 as p', r'\A\Z')
    prev = blob(ncrash)
    raw("print(open('/usr/glenda/synced%d').read() == %r * %d)" % (ncrash, prev[:64], len(prev) // 64),
        r'^True$', name='T0.1 boot %d: synced%d after the crash' % (nboot, ncrash))
    out = raw("print(sum(1 for d in p.dirread('/usr/glenda') if d[0].startswith('unsynced')))", r'^\d+$')
    print('\tinfo: unsynced files that survived a kill 0.5 s after: %s of %d' % (out, ncrash), flush=True)

    # T0.2: DMEXCL and OCEXEC
    raw("try:\n    p.remove('/usr/glenda/lock')\nexcept OSError:\n    pass\n"
        "lk = p.create('/usr/glenda/lock', p.ORDWR | p.OCEXEC, p.DMEXCL | 0o600)\nprint(p.stat('/usr/glenda/lock')[1] & p.DMEXCL != 0)",
        r'^True$', name='T0.2 create DMEXCL')
    raw("p.open('/usr/glenda/lock', p.ORDWR)", r'\A\Z', r'OSError: .*(exclusive|in use|locked)',
        name='T0.2 a second open in this process fails')
    raw("pid = p.spawn(['/boot/mp', '-c', \"import plan9 as p\\ntry:\\n    p.open('/usr/glenda/lock', p.ORDWR)\\n    print('opened')\\nexcept OSError as e:\\n    print('child:', e)\"]); p.wait()",
        r'^child: .*(exclusive|in use|locked)', name='T0.2 another process fails')
    raw("pid = p.spawn(['/boot/mp', '-c', \"import plan9 as p\\ntry:\\n    p.fd2path(%d)\\n    print('fd there')\\nexcept OSError:\\n    print('no fd')\" % lk]); p.wait()",
        r'^no fd$', name='T0.2 OCEXEC: no fd after exec')
    raw("p.close(lk)\npid = p.spawn(['/boot/mp', '-c', \"import plan9 as p\\np.open('/usr/glenda/lock', p.ORDWR)\\nprint('opened')\"]); p.wait()",
        r'^opened$', name='T0.2 open after close')
    raw("pid = p.spawn(['/boot/mp', '-c', \"import plan9 as p\\nf = p.open('/usr/glenda/lock', p.ORDWR)\\nprint('holding')\\np.sleep(600000)\"])\np.sleep(1000)\n"
        "try:\n    p.open('/usr/glenda/lock', p.ORDWR)\n    print('opened')\nexcept OSError:\n    print('held')\n"
        "f = p.open('/proc/%d/ctl' % pid, p.OWRITE); p.write(f, 'kill'); p.close(f); p.wait()\n"
        "lk = p.open('/usr/glenda/lock', p.ORDWR); print('freed'); p.close(lk)",
        r'holding\nheld\nfreed$', name="T0.2 the holder's death frees it")

    # T0.3: sync's time (ms, median of 10)
    out = raw("bt = p.open('#c/bintime')\ndef ns():\n    p.seek(bt, 0, 0)\n    return int.from_bytes(p.read(bt, 8), 'big')\n"
        "def t(n):\n    r = []\n    for i in range(10):\n        with open('/usr/glenda/synct', 'w') as f:\n            f.write('y' * n + str(i))\n"
        "        a = ns(); p.sync('/usr/glenda/synct'); r.append((ns() - a) // 1000000)\n    return sorted(r)[5]\n"
        "print('sync ms', t(10), t(1048576))", r'^sync ms \d+ \d+$', secs=120, name='T0.3 sync time')
    if out:
        a = out.split()
        print('\tinfo: plan9.sync median: small %s ms, 1 MiB %s ms (QEMU)' % (a[2], a[3]), flush=True)
    halt()

try:
    if phase >= 0:
        v0()
finally:
    stop()
print('PASS: space PHASE=%d' % phase if fails == 0 else 'FAIL: %d' % fails)
sys.exit(1 if fails else 0)
EOF
[ $st = 0 ] || { echo FAIL; exit 1; }

# the partition checked and read back in the build VM (userspace gefs)
part=$b/spacepart.img
rm -f "$part"
mib=$(( $(stat -c %s "$disk") / 1048576 - 66 ))
dd if="$disk" of="$part" bs=1M skip=65 count=$mib conv=sparse status=none
echo "### gefsck"
n=${CRASH:-3}
"$here/gefsck" "$part" /usr/glenda/synced$n > "$vm/out" 2>&1 || st=1
sed 's/^/	/' "$vm/out" | tail -3
grep -q '^ok' "$vm/out" || { echo "FAIL: gefsck: not ok"; st=1; }
grep -q "^crash $n synced" "$vm/out" || { echo "FAIL: gefsck: no /usr/glenda/synced$n"; st=1; }
rm -f "$part"
[ $st = 0 ] && echo "PASS: space PHASE=$phase" || echo FAIL
exit $st
