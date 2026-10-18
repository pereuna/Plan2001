#!/bin/bash
# The persistent object environment (docs/object-model.md) in QEMU: the
# gefs root (tools/gefsdisk, root=-w PARTUUID=) with plan9.ini
# init=/boot/mp -B, console=0, booted again and again on the same disk.
# Each boot is driven over the serial console in Python's raw REPL (as
# tools/test-mpinit.sh); a crash is QEMU killed (no fshalt, no sync).
# At the end tools/gefsck checks the partition in the build VM.
#   PHASE=0     the phases' tests up to this one (docs/object-model.md 7)
#   CRASH=3     crashes in the crash tests
#   ONLY=n      only phase n's tests (while developing it)
#   HW=1        instead: /sys/lib/python/test/space/hw.py (spacehw, the
#               tests on real hardware) as there, QEMU killed where it
#               says to cut the power
# V0 (sync and exclusive use):
#   T0.1  a file written, plan9.sync, QEMU killed as soon as it returns:
#         the next boot has it (CRASH times, 1 MiB each, checksummed)
#   T0.2  DMEXCL: a second open fails, in this process and another;
#         close or the holder's death frees it; OCEXEC: no fd after exec
#   T0.3  plan9.sync's time: a small change and 1 MiB (median of 10)
# V1 (a module's definition, the Environment, mp -E):
#   T1.1  space.define: the source's SHA-256; the .mpy made once
#   T1.2  two Environments, calc's two versions: each its own result
#   T1.3  a calc.py in the working directory does not count (sys.path)
#   T1.4  an unknown name, a package: ImportError; builtins work; a
#         module loaded once; a failed top level not in sys.modules
#   T1.5  a wrong runtime, a corrupt Environment: refused
# V2 (a Space, mp -S: the whole image, committed, restored):
#   T2.1  counter.increment() in three boots (killed): 1, 2, 3; a REPL
#         statement's a = 10 kept
#   T2.2  a module's top level runs in the first boot only
#   T2.3  shared references and a cycle kept as they were
#   T2.4  classes, instances, methods, a closure, defaults,
#         staticmethod, classmethod; no class body run again
#   T2.5  a generator: the commit fails with its type and path, the
#         version before stays
#   T2.6  __transient__ names None, the others kept
#   T2.7  a failed import leaves no instance
#   T2.8  two Spaces, one Environment: two counters
#   T2.9  a second mp -S: space locked; an rfork child's changes lost
#   T2.10 a dict keyed by instances with their own __hash__
# V3 (rests and crashes):
#   T3.1  n += 1; plan9.sleep(100) in a loop, killed CRASH times: n
#         grows from boot to boot
#   T3.2  with space.atomic(): a += 1; plan9.sleep(50); b += 1, killed:
#         always a == b
#   T3.3  killed amid t<N+1>'s write ($spaceslow): v<N> used, t gone
#   T3.4  a byte of v<N> changed: v<N-1> used, v<N> renamed bad<N>
#   T3.5  the disk full (a 136 MiB gefs of its own): CommitError, the
#         version before stays
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
echo "test-space: PHASE=$phase${ONLY:+ ONLY=$ONLY} CRASH=${CRASH:-3}; limits: a boot 150 s, a step 30-120 s, all about $(( ${CRASH:-3} + 2 )) min (QEMU, KVM)"

st=0
QEMU=$qemu QEMUARGS="${qemuargs[*]}" FWCODE=$fwcode FWVARS=$fwvars \
HWPY=${HW:+$root/sys/lib/python/test/space/hw.py} \
python3 -I - "$vm" "$disk" "$here/9run" "$phase" "${CRASH:-3}" "${ONLY:-}" <<'EOF' || st=1
import os, re, sys, time, shutil, subprocess, hashlib
vm, disk, run9, phase, ncrash, only = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5]), sys.argv[6]
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

def v1():
    src = {
        'calc1': 'def f(x):\n    return x * 2\n',
        'calc2': 'def f(x):\n    return x * 3\n',
        'user2': 'import calc\n',
        'bad': 'x = 1 / 0\n',
    }
    if not boot():
        return
    raw('import plan9 as p, space', r'\A\Z')
    code = "try:\n    p.create('/usr/glenda/src', p.OREAD, p.DMDIR | 0o775)\nexcept OSError:\n    pass\n"
    for n, t in src.items():
        code += "with open('/usr/glenda/src/%s.py', 'w') as f:\n    f.write(%r)\n" % (n, t)
    code += "with open('/usr/glenda/calc.py', 'w') as f:\n    f.write(\"def f(x):\\n    return 'shadow'\\n\")\n"
    # a child mp: its output, then its status
    code += "def run(src, env=None):\n    a = ['/boot/mp'] + (['-E', env] if env else []) + ['-c', src]\n    p.spawn(a)\n    print('status', repr(p.wait()[1]))\n"
    raw(code, r'\A\Z', name='V1 sources')
    raw('print(space.runtime)', r'^mp-1\.27\.0-mpy6\.3$', name='space.runtime')
    want = {n: hashlib.sha256(t.encode()).hexdigest() for n, t in src.items()}
    out = raw("h = {n: space.define('/usr/glenda/src/%s.py' % n) for n in ('calc1', 'calc2', 'user2', 'bad')}\nprint(sorted(h.items()))",
        re.escape(repr(sorted(want.items()))), secs=60, name="T1.1 define: the source's SHA-256")
    # T1.1: the .mpy made once: a second define changes nothing
    raw("m = '/usr/glenda/lib/mod/%s.mpy' % h['calc1']\nst, b = p.stat(m), open(m, 'rb').read()\n"
        "print(space.define('/usr/glenda/src/calc1.py') == h['calc1'], p.stat(m) == st, open(m, 'rb').read() == b, len(b) > 0)\n"
        "print(open('/usr/glenda/lib/mod/%s.py' % h['calc1']).read() == open('/usr/glenda/src/calc1.py').read())",
        r'^True True True True\nTrue$', name='T1.1 the .mpy made once, the source kept')
    raw("e1 = space.mkenv({'calc': h['calc1']})\ne2 = space.mkenv({'calc': h['calc2']})\n"
        "e3 = space.mkenv({'calc': h['calc1'], 'user2': h['user2'], 'bad': h['bad']})\n"
        "print(e1 != e2, e1 == space.mkenv({'calc': h['calc1']}), open('/usr/glenda/lib/env/' + e1).read())",
        r'^True True runtime mp-1\.27\.0-mpy6\.3\ncalc [0-9a-f]{64}$', name='mkenv: named by its text')
    raw("run(\"import calc; print('f', calc.f(10))\", e1)\nrun(\"import calc; print('f', calc.f(10))\", e2)",
        r"^f 20\nstatus ''\nf 30\nstatus ''$", secs=60, name='T1.2 two Environments, two versions')
    raw("p.chdir('/usr/glenda')\nrun(\"import calc; print('f', calc.f(10))\", e1)\nrun(\"import calc; print('f', calc.f(10))\")",
        r"^f 20\nstatus ''\nf shadow\nstatus ''$", secs=60, name='T1.3 the working directory does not count (without -E it does)')
    raw("run('import nope', e1)", r"ImportError: no module named 'nope' in the Environment\nstatus '.+'$", secs=30, name='T1.4 an unknown name')
    raw("run('import a.b', e1)", r"ImportError: packages not supported\nstatus '.+'$", secs=30, name='T1.4 a package')
    raw("run('import json, plan9, space; print(json.dumps([1]), space.env() == %r)' % e1, e1)",
        r"^\[1\] True\nstatus ''$", secs=30, name='T1.4 builtin modules')
    raw("run('import calc, user2; print(user2.calc is calc)', e3)", r"^True\nstatus ''$", secs=30, name='T1.4 a module loaded once')
    raw("run('import sys\\ntry:\\n    import bad\\nexcept ZeroDivisionError:\\n    print(\"bad\" in sys.modules)', e3)",
        r"^False\nstatus ''$", secs=30, name='T1.4 a failed top level not in sys.modules')
    raw("t = 'runtime mp-0.0.0-mpy0.0\\ncalc %s\\n' % h['calc1']\nx = space.hash(t)\n"
        "with open('/usr/glenda/lib/env/' + x, 'w') as f:\n    f.write(t)\nrun('print(1)', x)",
        r"runtime mp-0\.0\.0-mpy0\.0, this mp is mp-1\.27\.0-mpy6\.3\nstatus '.+'$", secs=30, name='T1.5 a wrong runtime')
    raw("x = 'f' * 64\nwith open('/usr/glenda/lib/env/' + x, 'w') as f:\n    f.write(open('/usr/glenda/lib/env/' + e1).read())\nrun('print(1)', x)",
        r"corrupt.*\nstatus '.+'$", secs=30, name='T1.5 a corrupt Environment')
    raw("run('print(1)', 'x;y')", r"not an Environment\nstatus '.+'$", secs=30, name='T1.5 not an Environment')
    halt()

def v2():
    src = {
        'counter': "print('init counter')\nn = 0\ndef increment():\n    global n\n    n += 1\n    return n\n",
        'shapes': "print('init shapes')\nclass Base:\n    print('class body Base')\n    kind = 'base'\n"
            "    def __init__(self, x):\n        self.x = x\n    def get(self):\n        return self.x\n"
            "    @staticmethod\n    def twice(v):\n        return 2 * v\n"
            "    @classmethod\n    def make(cls, v):\n        return cls(v)\n"
            "class Sub(Base):\n    def get(self):\n        return self.x + 100\n"
            "def adder(k):\n    def add(v):\n        return v + k\n    return add\n"
            "def scale(v, f=3, *, off=1):\n    return v * f + off\n",
        'keyed': "class K:\n    def __init__(self, a):\n        self.a = a\n"
            "    def __hash__(self):\n        return hash(self.a)\n"
            "    def __eq__(self, o):\n        return isinstance(o, K) and o.a == self.a\n",
        'badtop': "x = 1 / 0\n",
    }
    if not boot():
        return
    raw('import plan9 as p, space', r'\A\Z')
    code = "try:\n    p.create('/usr/glenda/src2', p.OREAD, p.DMDIR | 0o775)\nexcept OSError:\n    pass\n"
    for n, t in src.items():
        code += "with open('/usr/glenda/src2/%s.py', 'w') as f:\n    f.write(%r)\n" % (n, t)
    # a child mp in a Space: its output, then its status
    code += ("def run(src, sp='t2'):\n    pid = p.spawn(['/boot/mp', '-S', sp, '-c', src])\n"
        "    while True:\n        w = p.wait()\n        if w[0] == pid:\n            break\n    print('status', repr(w[1]))\n")
    raw(code, r'\A\Z', name='V2 sources')
    raw("e = space.mkenv({n: space.define('/usr/glenda/src2/%s.py' % n) for n in ('counter', 'shapes', 'keyed', 'badtop')})\n"
        "space.create('t2', e)\nspace.create('t2b', e)\nprint(sorted(p.dirread('/usr/glenda/space/t2')[i][0] for i in range(2)))",
        r"^\['lock', 'v0000000000000000'\]$", secs=60, name='space.create')
    raw("space.create('t2', e)", r'\A\Z', r'space t2 exists', name='space.create: one there')
    raw("run(\"import counter; print('c', counter.increment())\")", r"^init counter\nc 1\nstatus ''$", secs=30, name='T2.1 boot 1: 1')
    raw("run(\"import shapes, keyed\\nb = shapes.Base(5)\\ns = shapes.Sub.make(7)\\nadd = shapes.adder(10)\\n"
        "m = s.get\\nsc = shapes.scale\\nl = []\\nd = {'x': l, 'y': l}\\nl.append(d)\\nbig = 2 ** 100\\n"
        "fl = 1.5\\nfs = frozenset([1, 2])\\nby = bytearray(b'ab')\\n__transient__ = ('tmp',)\\ntmp = [1]\\nkeep = 2\\n"
        "kd = {keyed.K(1): 'one', keyed.K((2, 3)): 'two'}\\nprint('made')\")",
        r"^init shapes\nclass body Base\nmade\nstatus ''$", secs=30, name='T2.3-4, 6, 10: the objects made')
    raw("p.spawn(['/bin/rc', '-c', \"echo 'r = 10' | /boot/mp -S t2 >/dev/null\"]); print('status', repr(p.wait()[1]))\n"
        "p.spawn(['/bin/rc', '-c', \"echo 'print(r)' | /boot/mp -S t2\"]); print('status', repr(p.wait()[1]))",
        r"^status ''\n.*>>> print\(r\)\s*10\s.*status ''$", secs=30, name='T2.1 a REPL statement')
    crash()
    if not boot():
        return
    raw('import plan9 as p, space\n' + code[code.index('def run'):], r'\A\Z', name='V2 run()')
    raw("run(\"import counter; print('c', counter.increment())\")", r"^c 2\nstatus ''$", secs=30, name='T2.1-2 boot 2: 2, no top level')
    raw("run(\"print(b.get(), s.get(), m(), add(5), sc(2), sc(2, 4, off=0), shapes.Base.twice(4), shapes.Sub.make(1).get(), "
        "type(s) is shapes.Sub, isinstance(s, shapes.Base), b.kind)\\n"
        "print(d['x'] is d['y'], l[0] is d, big, fl, sorted(fs), type(fs) is frozenset, by)\\nprint(tmp, keep, r)\\n"
        "print(kd[keyed.K(1)], kd[keyed.K((2, 3))], len(kd))\")",
        r"^5 107 107 15 7 8 8 101 True True base\nTrue True 1267650600228229401496703205376 1\.5 \[1, 2\] True bytearray\(b'ab'\)\n"
        r"None 2 10\none two 2\nstatus ''$", secs=30, name='T2.3-4, 6, 10: restored, no class body')
    crash()
    if not boot():
        return
    raw('import plan9 as p, space\n' + code[code.index('def run'):], r'\A\Z', name='V2 run()')
    raw("run(\"import counter; print('c', counter.increment())\")", r"^c 3\nstatus ''$", secs=30, name='T2.1 boot 3: 3')
    raw("run('x = (i for i in range(3))')",
        r"space: commit failed: CommitError: not persistent: generator at __main__\.x\nstatus '.+'$", secs=30, name='T2.5 a generator')
    raw("run(\"print('x' in globals(), counter.n)\")", r"^False 3\nstatus ''$", secs=30, name='T2.5 the version before')
    raw("run('try:\\n    import badtop\\nexcept ZeroDivisionError:\\n    print(\"failed\")')\n"
        "run(\"import sys; print('badtop' in sys.modules, 'badtop' in globals())\")",
        r"^failed\nstatus ''\nFalse False\nstatus ''$", secs=30, name='T2.7 a failed import')
    raw("run(\"import counter; print('c', counter.increment())\", 't2b')\n"
        "run(\"import counter; print('c', counter.increment())\", 't2b')\n"
        "run(\"print('c', counter.increment())\")",
        r"^init counter\nc 1\nstatus ''\nc 2\nstatus ''\nc 4\nstatus ''$", secs=60, name='T2.8 two Spaces')
    raw("sl = p.spawn(['/boot/mp', '-S', 't2', '-c', 'import plan9\\nplan9.sleep(5000)'])\np.sleep(1500)\n"
        "run('print(1)')\nwhile p.wait()[0] != sl:\n    pass",
        r"space t2 locked\nstatus '.+'$", secs=30, name='T2.9 a second mp -S')
    raw("run(\"import plan9\\nif plan9.rfork(16 | plan9.RFFDG) == 0:\\n    child = 1\\n    raise SystemExit\\n"
        "plan9.wait()\\nprint('parent')\")\nrun(\"print(globals().get('child'))\")",
        r"^parent\nstatus ''\nNone\nstatus ''$", secs=30, name="T2.9 an rfork child's changes lost")
    out = raw("print(sorted(x[0] for x in p.dirread('/usr/glenda/space/t2')))", r"^\['lock'(, 'v\d{16}'){3}\]$",
        name='three versions kept')
    halt()

def v3():
    loop1 = "import plan9\nif 'n' not in globals():\n    n = 0\nwhile True:\n    n += 1\n    plan9.sleep(100)"
    loop2 = ("import plan9, space\nspace.interval = 0.3\nif 'a' not in globals():\n    a = b = 0\n"
        "while True:\n    with space.atomic():\n        a += 1\n        plan9.sleep(50)\n        b += 1\n    plan9.sleep(10)")
    code = ("import plan9 as p, space\ne = space.mkenv({})\n"
        "def run(src, sp):\n    pid = p.spawn(['/boot/mp', '-S', sp, '-c', src])\n"
        "    while True:\n        w = p.wait()\n        if w[0] == pid:\n            break\n    print('status', repr(w[1]))\n"
        "def bg(src, sp):\n    p.spawn(['/boot/mp', '-S', sp, '-c', src])\n")
    if not boot():
        return
    raw(code, r'\A\Z', secs=30, name='V3 run()')
    raw("for n in ('t3a', 't3b', 't3c', 't3d'):\n    space.create(n, e)", r'\A\Z', secs=30, name='V3 spaces')
    n0 = a0 = 0
    for i in range(1, ncrash + 2):
        if i > 1:
            if not boot():
                return
            raw(code, r'\A\Z', secs=30, name='V3 run()')
            out = raw("run(\"print('n', n)\", 't3a')", r"^n \d+\nstatus ''$", secs=30, name='T3.1 boot %d: n' % nboot)
            n = int(out.split()[1]) if out else -1
            check('T3.1 boot %d: n grew: %d > %d' % (nboot, n, n0), n > n0)
            n0 = n
            out = raw("run(\"print('ab', a, b)\", 't3b')", r"^ab \d+ \d+\nstatus ''$", secs=30, name='T3.2 boot %d: a, b' % nboot)
            a, b = (int(x) for x in out.split()[1:3]) if out else (-1, -2)
            check('T3.2 boot %d: a == b == %d, grew from %d' % (nboot, a, a0), a == b and a > a0)
            a0 = a
        if i == ncrash + 1:
            break
        raw("bg(%r, 't3a')\nbg(%r, 't3b')\np.sleep(3000)" % (loop1, loop2), r'\A\Z', secs=30, name='V3 loops for 3 s')
        crash()
    # T3.3: killed amid t<2>'s write, after the autosync (5 s) has it
    raw("run('x = 1', 't3c')\nwith open('/env/spaceslow', 'w') as f:\n    f.write('30000')\n"
        "bg('x = 2', 't3c')\np.sleep(8000)\nprint([d[0] for d in p.dirread('/usr/glenda/space/t3c') if d[0][0] in 'tv'])",
        r"^status ''\n\[.*'t0000000000000002'.*\]$", secs=30, name='T3.3 t2 half written')
    crash()
    if not boot():
        return
    raw(code, r'\A\Z', secs=30, name='V3 run()')
    raw("run('print(x)', 't3c')\nprint([d[0] for d in p.dirread('/usr/glenda/space/t3c') if d[0][0] == 't'])",
        r"^1\nstatus ''\n\[\]$", secs=30, name='T3.3 v1 used, t gone')
    # T3.4: v2 corrupt
    raw("run('x = 1', 't3d')\nrun('x = 2', 't3d')\nfd = p.open('/usr/glenda/space/t3d/v0000000000000002', p.ORDWR)\n"
        "p.seek(fd, 80, 0)\nc = p.read(fd, 1)\np.seek(fd, 80, 0)\np.write(fd, bytes([c[0] ^ 1]))\np.close(fd)\n"
        "run('print(x)', 't3d')\nprint(sorted(d[0] for d in p.dirread('/usr/glenda/space/t3d')))",
        r"^status ''\nstatus ''\nspace: t3d/v0000000000000002: its SHA-256: renamed bad0000000000000002\n1\nstatus ''\n"
        r"\['bad0000000000000002', 'lock', 'v0000000000000000', 'v0000000000000001'\]$", secs=30, name='T3.4 v1 used, v2 bad')
    # T3.5: a gefs of its own over /usr/glenda/space, filled
    raw("p.spawn(['/bin/rc', '-c', 'cd /usr/glenda && dd -if /dev/zero -of small.img -bs 1048576 -count 136 >[2]/dev/null && "
        "gefs -r glenda -f small.img >/tmp/ream.log >[2=1] && gefs -A -n small -f small.img >[2]/dev/null && "
        "mount -c /srv/small /usr/glenda/space && echo mounted'])\np.wait()",
        r"^mounted$", secs=120, name='T3.5 a small gefs')
    raw("space.create('t3e', e)\nrun('x = 1', 't3e')\n"
        "p.spawn(['/bin/rc', '-c', 'dd -if /dev/zero -of /usr/glenda/space/fill -bs 1048576 -count 200 >[2]/dev/null; echo filled'])\np.wait()",
        r"^status ''\nfilled$", secs=120, name='T3.5 filled')
    raw("run(\"import space, plan9\\nspace.interval = 0\\ny = 'z' * 1000000\\ntry:\\n    plan9.sleep(1)\\n"
        "except space.CommitError as e:\\n    print('CE', e)\\ndel y\", 't3e')",
        r"^CE .+\nspace: commit failed: CommitError: .+\nstatus '.+'$", secs=60, name='T3.5 CommitError at a rest and the end')
    raw("run(\"print(x, 'y' in globals(), 'space' in globals())\", 't3e')", r"^1 False False\nstatus ''$", secs=30,
        name='T3.5 the version before')
    raw("p.spawn(['/bin/rc', '-c', 'unmount /usr/glenda/space; echo halt >>/srv/small.cmd; "
        "for(i in 1 2 3 4 5 6 7 8 9 10) if(test -e /srv/small.cmd) sleep 1; rm -f /usr/glenda/small.img; echo gone'])\np.wait()",
        r"gone$", secs=60, name='T3.5 the small gefs halted')
    halt()

def vhw():
    """hw.py as on real hardware: written to the root, run at each boot"""
    src = open(os.environ['HWPY']).read()
    path = '/sys/lib/python/test/space/hw.py'
    for i in range(10):
        if not boot():
            return
        if i == 0:
            raw("import plan9 as p\nfor d in ('/sys/lib/python', '/sys/lib/python/test', '/sys/lib/python/test/space'):\n"
                "    try:\n        p.close(p.create(d, p.OREAD, p.DMDIR | 0o775))\n    except OSError:\n        pass\n"
                "with open(%r, 'w') as f:\n    f.write(%r)" % (path, src), r'\A\Z', secs=120, name='hw.py written')
        start = len(text())
        typ("import plan9 as p\np.spawn(['/boot/mp', %r])\x04" % path)
        m = wait(r'CUT THE POWER NOW \((\d) of (\d)\)|spacehw: done: (\d+) FAIL|Traceback', start, 600)
        out = text()[start:]
        print('\t' + '\n\t'.join(l for l in out.split('\n') if l.startswith(('ok ', 'FAIL', 'info', 'spacehw'))), flush=True)
        if m is None or m.group(0) == 'Traceback':
            check('hw.py stage %d' % i, False, out)
            return
        if m.group(3) is not None:
            check('hw.py: %s FAIL' % m.group(3), m.group(3) == '0', out)
            wait(r'done halting', start, 120)
            stop()
            return
        crash()

phases = [v0, v1, v2, v3]
if os.environ.get('HWPY'):
    phases, phase, only = [vhw], 0, ''

try:
    for i, f in enumerate(phases):
        if (only == '' and i <= phase) or only == str(i):
            f()
finally:
    stop()
print(("PASS: space PHASE=%d" % phase if not only else "PASS: space ONLY=" + only) if fails == 0 else "FAIL: %d" % fails)
sys.exit(1 if fails else 0)
EOF
[ $st = 0 ] || { echo FAIL; exit 1; }

[ -z "$ONLY" ] || [ "$ONLY" = 0 ] || { echo "PASS: space ONLY=$ONLY"; exit 0; }
[ -z "$HW" ] || { echo "PASS: space HW=1"; exit 0; }

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
