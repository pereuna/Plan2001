#!/bin/bash
# The persistent object environment (docs/object-model.md) in QEMU: the
# gefs root (tools/gefsdisk, root=-w PARTUUID=) with plan9.ini
# init=/boot/mp -B, console=0, booted again and again on the same disk.
# Each boot is driven over the serial console in Python's raw REPL (as
# tools/test-mpinit.sh); a crash is QEMU killed (no fshalt, no sync).
# At the end tools/gefsck checks the partition in the build VM (ONLY too).
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
#   T3.3  killed amid version N+1's write ($spaceslow): N used, then
#         N+2 committed
#   T3.4  (V2-V4: a version file's byte changed; the store has none)
#   T3.5  (the disk full: in V5 with T5.9)
# V4 (graph: relations, the identifier, #tags; sys/lib/python/graph.py):
#   T4.1-T4.11 as docs/object-model.md 7; T4.4 and T4.6 over a boot
# V5 (the store, docs/gefs-obj.md 5; $spacecheck on in all phases'
# boots, SPACECHECK=0 off):
#   T5.1  one change of 10 000 objects: a few records written
#   T5.2  space.oid: kept over versions and the boot
#   T5.4  a 40 MiB store made and removed 3 times: no growth (adm status)
#   T5.5  a REPL statement's commit in ~30 000 objects (ms, < 200)
#   T5.6  a reader of the store amid 100 commits: whole versions
#   T5.7  a bad record amid a commit ($spacefail): N kept, then N+1
#   T5.8  1 MiB bytes kept
#   T5.10 open(name, v): version v kept; one older refused
#   T5.13 killed amid a removed store's sweep: the boot sweeps on (no
#         store keys left of the removed ones: gefs console show objs)
#   T5.12 objfail (gefs console): the history sweep after the header's
#         publication fails: the version stays, the next commit works
#   T5.11 a V4 v<N> (made here) taken in, killed before its first
#         commit too: then committed, v<N> gone
#   T3.5, T5.9 the root filled (dd, ~4 GiB, ~9 min): gefs read-write;
#         CommitError (full) at a rest and the end, the version before
#         kept; the fill removed: commits again, and after the boot
# V6 (space.view: another Space read, docs/object-model.md V6):
#   T6.1  views amid 100 atomic commits: la[0] == lb[0] in each
#   T6.2  none (its own mount): a view; the store 0660: refused
#   T6.3  a foreign object in a commit: CommitError; an int of it kept
#   T6.4  graph.py's Space as a view: search, links, its own classes
#   T6.5  version=v; one not kept refused; the writer's lock no matter
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
HWPY=${HW:+$root/sys/lib/python/test/space/hw.py} GRAPHPY=$root/sys/lib/python/graph.py \
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
    if os.path.exists(log):
        shutil.copy(log, os.path.join(vm, 'serial%d.log' % (nboot - 1)))
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
    if not check('boot %d: the raw REPL' % nboot, wait(r'raw REPL; CTRL-B to exit\n>$', start, 10) is not None, text()[start:]):
        return False
    if os.environ.get('SPACECHECK', '1') != '0':
        raw("with open('/env/spacecheck', 'w') as f:\n    f.write('1')", r'\A\Z', name='boot %d: $spacecheck' % nboot)
    return True
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
        r"^\['lock', 'obj'\]$", secs=60, name='space.create')
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
    raw("print(sorted(x[0] for x in p.dirread('/usr/glenda/space/t2')))", r"^\['lock', 'obj'\]$",
        name='the store only')
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
    # T3.3: killed amid version 3's write, after the autosync (5 s) has it
    raw("run('x = 1', 't3c')\nwith open('/env/spaceslow', 'w') as f:\n    f.write('30000')\n"
        "bg('x = 2', 't3c')\np.sleep(8000)\nprint(sorted(d[0] for d in p.dirread('/usr/glenda/space/t3c')))",
        r"^status ''\n\['lock', 'obj'\]$", secs=30, name='T3.3 version 3 half written')
    crash()
    if not boot():
        return
    raw(code, r'\A\Z', secs=30, name='V3 run()')
    raw("run('print(x)', 't3c')\nrun('x = 3', 't3c')\nrun('print(x)', 't3c')",
        r"^1\nstatus ''\nstatus ''\n3\nstatus ''$", secs=30, name='T3.3 version 2 used, then 3')
    halt()

def v4():
    graphpy = open(os.environ['GRAPHPY']).read()
    photos = ("import graph, plan9\nclass Project(graph.Node):\n    def __init__(self, name=''):\n        self.name = name\n"
        "class Photo(graph.Node):\n    project = graph.link(Project, inverse='photos')\n"
        "    people = graph.links('Person', inverse='photos')\n    def __init__(self, n=0):\n        self.n = n\n"
        "class Person(graph.Node):\n    def __init__(self, name=''):\n        self.name = name\n"
        "def ms():\n    f = plan9.open('#c/bintime')\n    b = plan9.read(f, 8)\n    plan9.close(f)\n"
        "    return int.from_bytes(b, 'big') // 1000000\n")
    code = ("import plan9 as p, space\n"
        "def ms():\n    f = p.open('#c/bintime')\n    b = p.read(f, 8)\n    p.close(f)\n    return int.from_bytes(b, 'big') // 1000000\n"
        "def run(src, sp='t4'):\n    t = ms()\n    pid = p.spawn(['/boot/mp', '-S', sp, '-c', src])\n"
        "    while True:\n        w = p.wait()\n        if w[0] == pid:\n            break\n    print('status', repr(w[1]))\n    return ms() - t\n")
    if not boot():
        return
    raw(code + "try:\n    p.create('/usr/glenda/src4', p.OREAD, p.DMDIR | 0o775)\nexcept OSError:\n    pass\n"
        "with open('/usr/glenda/src4/graph.py', 'w') as f:\n    f.write(%r)\n"
        "with open('/usr/glenda/src4/photos.py', 'w') as f:\n    f.write(%r)\n"
        "with open('/usr/glenda/koe.txt', 'w') as f:\n    f.write('hello koe\\n')\n"
        "e = space.mkenv({n: space.define('/usr/glenda/src4/%%s.py' %% n) for n in ('graph', 'photos')})\n"
        "space.create('t4', e)" % (graphpy, photos), r'\A\Z', secs=120, name='V4 sources, the Space')
    raw("run(\"import graph, photos\\np = photos.Project('a')\\nq = photos.Project('b')\\nph = photos.Photo(1)\\n"
        "ph.project = p\\nprint(ph in p.photos, ph.project is p)\\nph.project = q\\n"
        "print(ph in p.photos, ph in q.photos, len(p.photos))\")",
        r"^True True\nFalse True 0\nstatus ''$", secs=30, name='T4.1 link and its inverse')
    raw("run(\"m = photos.Person('matti')\\nph.people.add(m)\\nprint(m in ph.people, ph in m.photos)\\n"
        "ph.people.discard(m)\\nprint(m in ph.people, ph in m.photos)\\n"
        "try:\\n    p.photos = 1\\nexcept AttributeError:\\n    print('ro')\\n"
        "try:\\n    p.photos.add(ph)\\nexcept AttributeError:\\n    print('ro2')\\nph.people.add(m)\")",
        r"^True True\nFalse False\nro\nro2\nstatus ''$", secs=30, name='T4.2 links, many to many; the inverse read only')
    out = raw("t = run(\"ph.project = p\\nt0 = photos.ms()\\nbig = [photos.Photo(i) for i in range(10000)]\\n"
        "for x in big:\\n    x.project = q\\nt1 = photos.ms()\\nr = graph.find(photos.Photo, project=p)\\n"
        "t2 = photos.ms()\\nr2 = graph.find(photos.Photo, project=q)\\nt3 = photos.ms()\\n"
        "r3 = graph.find(photos.Photo, n=5)\\nt4 = photos.ms()\\n"
        "print(len(r), r[0] is ph, len(r2), len(r3), 'make', t1 - t0, 'find', t2 - t1, t3 - t2, 'scan', t4 - t3)\\n"
        "del r, r2, r3\")\nprint('run', t)",
        r"^1 True 10000 1 make \d+ find \d+ \d+ scan \d+\nstatus ''\nrun \d+$", secs=120, name='T4.3 find, 10 000 photos')
    if out:
        print('\tinfo: T4.3 (ms): ' + out.replace('\n', '; '), flush=True)
    raw("run(\"d = photos.Photo(-1)\\nd.project = p\\nd.people.add(m)\\ngraph.delete(d)\\n"
        "print(d in p.photos, d in m.photos, d in graph.find(photos.Photo), graph.byid(d._id))\\n"
        "try:\\n    ph.project = m\\nexcept TypeError:\\n    print('TypeError')\\ndel d\")",
        r"^False False False None\nTypeError\nstatus ''$", secs=60, name='T4.5 delete; a link to what is not its target')
    raw("run(\"a1 = photos.Project('x')\\na2 = photos.Project('y')\\nprint(a2._id == a1._id + 1)\\n"
        "graph.delete(a2)\\nlast = a2._id\\ndel a1, a2\")",
        r"^True\nstatus ''$", secs=60, name='T4.6 the next identifier')
    raw("run(\"graph._next = 102939\\nhn = graph.Text('kaviot')\\nprint(graph.line(hn))\\n"
        "graph.tag(hn, '#kaviot #mirkku #11.12.2026 #Liisa #Hanna #Puuilo')\\n"
        "print(graph.parse('#0102939 #kaviot #mirkku #11.12.2026 #Liisa #Hanna #Puuilo'))\\n"
        "print(graph.parse('#0102939 #2026'))\\nprint(graph.parse(graph.line(hn)) == (hn._id, graph.tags(hn)[1:]))\\n"
        "try:\\n    graph.parse('#102939 #x')\\nexcept ValueError:\\n    print('not an identifier')\")",
        r"^#0102939\n\(102939, \['kaviot', 'mirkku', '11\.12\.2026', 'Liisa', 'Hanna', 'Puuilo'\]\)\n"
        r"\(102939, \['2026'\]\)\nTrue\nnot an identifier\nstatus ''$", secs=60, name='T4.7 parse and line')
    raw("run(\"graph.tag(ph, '#Mirkku #Kaviot #\u00c4iti')\\n"
        "print(graph.search('#Mirkku #kaviot') == graph.search('#mirkku #KAVIOT'), ph in graph.search('#\u00e4iti'), "
        "hn in graph.search('#mirkku #KAVIOT'), len(graph.search('#mirkku #kaviot')))\")",
        r"^True True True 2\nstatus ''$", secs=60, name='T4.8 case folded, \u00e4 too')
    raw("run(\"o2 = graph.Text('other')\\ngraph.tag(o2, '#0102939')\\ns = graph.search('#0102939')\\n"
        "print(len(s), hn in s, o2 in s)\\ntry:\\n    graph.untag(hn, '#0102939')\\nexcept ValueError:\\n    print('kept')\\n"
        "print(graph.tags(hn)[0])\\ntry:\\n    graph.search(' ')\\nexcept ValueError:\\n    print('empty')\\ndel s\")",
        r"^2 True True\nkept\n0102939\nempty\nstatus ''$", secs=60, name='T4.9 the identifier is a tag')
    raw("run(\"k = graph.importfile('/usr/glenda/koe.txt')\\nprint(graph.search('#koe.txt') == [k], k.text.strip())\")",
        r"^True hello koe\nstatus ''$", secs=60, name='T4.10 koe.txt a Text')
    out = raw("t = run(\"tg = [graph.Text(str(i)) for i in range(10000)]\\nfor i in range(10000):\\n"
        "    graph.tag(tg[i], '#a%d #b%d #c%d' % (i % 2, i % 3, i % 5))\\nt0 = photos.ms()\\n"
        "r = graph.search('#a0 #b0 #c0')\\nt1 = photos.ms()\\nprint(len(r), 'search', t1 - t0)\\ndel r\")\nprint('run', t)",
        r"^334 search \d+\nstatus ''\nrun \d+$", secs=180, name='T4.11 10 000 objects, three tags')
    if out:
        print('\tinfo: T4.11 (ms): ' + out.replace('\n', '; '), flush=True)
    out = raw("with open('/env/spacetime', 'w') as f:\n    f.write('1')\ntry:\n    p.remove('/env/spacecheck')\nexcept OSError:\n    pass\n"
        "print('open', run('pass'), 'commit', run('x4 = 1'))\np.remove('/env/spacetime')",
        r"open \d+ commit \d+$", secs=120, name='V4: open and commit times')
    if out:
        print('\tinfo: the Space of ~30 000 objects (ms): ' + out.replace('\n', '; '), flush=True)
    crash()
    if not boot():
        return
    raw(code, r'\A\Z', name='V4 run()')
    raw("run(\"print(ph.project is p, ph in p.photos, m in ph.people, ph in m.photos, len(graph.find(photos.Photo, project=q)), "
        "graph.search('#koe.txt') == [k], k.text.strip(), len(graph.search('#a0 #b0 #c0')), graph.byid(102939) is hn)\")",
        r"^True True True True 10000 True hello koe 334 True\nstatus ''$", secs=120, name='T4.4 after the boot')
    raw("run(\"a3 = photos.Project('z')\\nprint(a3._id > last, a3._id > hn._id)\")", r"^True True\nstatus ''$", secs=120,
        name='T4.6 identifiers not reused after delete and boot')
    halt()

def v5():
    code = ("import plan9 as p, space\n"
        "def run(src, sp='t5', args=None):\n    pid = p.spawn(['/boot/mp'] + (['-S', sp] if args is None else args) + ['-c', src])\n"
        "    while True:\n        w = p.wait()\n        if w[0] == pid:\n            break\n    print('status', repr(w[1]))\n"
        "def env(n, v):\n    if v is None:\n        try:\n            p.remove('/env/' + n)\n        except OSError:\n            pass\n"
        "    else:\n        with open('/env/' + n, 'w') as f:\n            f.write(v)\n")
    if not boot():
        return
    raw(code + "e = space.mkenv({})\nfor n in ('t5', 't5b', 't5c'):\n    space.create(n, e)", r'\A\Z', secs=30, name='V5 spaces')
    # T5.10: versions 2-5; 3-5 kept
    raw("for i in range(1, 5):\n    run('x = %d' % i)\n"
        "run(\"import space\\nspace.open('t5', 4)\\nprint('v4', x, space.info()[1])\", args=[])\n"
        "run(\"import space\\nspace.open('t5', 3)\\nprint('v3', x)\", args=[])\n"
        "run(\"import space\\nspace.open('t5', 2)\", args=[])\n"
        "run('print(x)')",
        r"^(status ''\n){4}v4 3 4\nstatus ''\nv3 2\nstatus ''\n.*not kept.*\nstatus '.+'\n4\nstatus ''$", secs=60,
        name='T5.10 versions 3-5 kept, 2 refused')
    # T5.8: 1 MiB bytes
    raw("run('y = bytes(range(256)) * 4096')\nrun('print(len(y), y == bytes(range(256)) * 4096)\\ndel y')",
        r"^status ''\n1048576 True\nstatus ''$", secs=60, name='T5.8 1 MiB bytes kept')
    # T5.1: one change in 10 000 objects
    out = raw("run(\"big = [{'i': i} for i in range(10000)]\")\nenv('spacetime', '1')\n"
        "run(\"big[5000]['i'] = -1\")\nenv('spacetime', None)\nrun(\"print(big[5000], big[4999])\")",
        r"^status ''\n.*spacetime: commit: \d+ encoded, \d+ records, \d+ bytes \d+ ms\n.*status ''\n\{'i': -1\} \{'i': 4999\}\nstatus ''$",
        secs=120, name='T5.1 one change of 10 000 objects')
    m = re.search(r'commit: (\d+) encoded, (\d+) records, (\d+) bytes', out or '')
    if m:
        print('\tinfo: T5.1: ' + m.group(0), flush=True)
        check('T5.1 a few objects and records: %s' % m.group(0), int(m.group(1)) < 20 and int(m.group(2)) <= 3)
    # T5.5: ~30 000 objects, a statement's commit, no check
    out = raw("run(\"big2 = [{'i': i} for i in range(20000)]\")\nenv('spacecheck', None)\nenv('spacetime', '1')\n"
        "run('z = 1')\nenv('spacetime', None)\nenv('spacecheck', '1')",
        r"^status ''\n.*spacetime: commit \d+ ms\n.*status ''$", secs=120, name='T5.5 ~30 000 objects, z = 1')
    m = re.findall(r'spacetime: (restore|commit) (\d+) ms', out or '')
    if m:
        print('\tinfo: T5.5 (ms): ' + ', '.join('%s %s' % x for x in m), flush=True)
        c = [int(x[1]) for x in m if x[0] == 'commit']
        check('T5.5 the commit under 200 ms: %s' % c, c and c[-1] < 200)
    # T5.6: a reader amid 100 commits
    writer = ("import plan9, space\nspace.interval = 0\nla = [0]\nlb = [0]\nfor i in range(100):\n"
        "    la[0] = i\n    lb[0] = i\n    plan9.sleep(10)\nprint('w done')")
    out = raw("def rd():\n    fd = p.open('/usr/glenda/space/t5b/obj', p.OREAD)\n    c = []\n    while True:\n"
        "        b = p.read(fd, 65536)\n        if not b:\n            break\n        c.append(b)\n    p.close(fd)\n"
        "    b = b''.join(c)\n    g = lambda o, n: int.from_bytes(b[o:o + n], 'big')\n"
        "    if b[0] != 72:\n        return None\n    n = g(1, 8)\n    o = 21 + g(17, 4)\n    mx = 0\n    oids = set()\n"
        "    while o < len(b):\n        if b[o] != 111 or g(o + 9, 8) > n or g(o + 1, 8) in oids:\n            return None\n"
        "        oids.add(g(o + 1, 8))\n        mx = max(mx, g(o + 9, 8))\n        o += 25 + g(o + 17, 8)\n"
        "    return n if o == len(b) and mx == n else None\n"
        "pid = p.spawn(['/boot/mp', '-S', 't5b', '-c', %r])\nseen = set()\nbad = reads = 0\nn = 0\n"
        "while n < 100 and reads < 3000:\n    n = rd()\n    reads += 1\n    if n is None:\n        bad += 1\n        n = 0\n"
        "    seen.add(n)\nwhile p.wait()[0] != pid:\n    pass\nprint('reads', reads, 'versions', len(seen), 'bad', bad)" % writer,
        r"w done\n.*reads \d+ versions \d+ bad 0$", secs=120, name='T5.6 a reader amid 100 commits')
    if out:
        print('\tinfo: T5.6: ' + out.split('\n')[-1], flush=True)
    # T5.7: a bad record amid a commit
    raw("run('x = 1', 't5c')\nenv('spacefail', '1')\nrun('x = 2', 't5c')\nenv('spacefail', None)\n"
        "run('print(x)', 't5c')\nrun('x = 3', 't5c')\nrun(\"import space\\nprint(x, space.info()[1])\", 't5c')",
        r"^status ''\nspace: commit failed: CommitError: .+\nstatus '.+'\n1\nstatus ''\nstatus ''\n3 3\nstatus ''$", secs=60,
        name='T5.7 a failed commit: N kept, then N+1')
    # T5.12: the history sweep fails after the header's publication:
    # the version published stays (it was clobbered by the abort)
    raw("fd = p.open('#s/gefs.root.cmd', p.OWRITE)\np.write(fd, b'objfail\\n')\np.close(fd)\np.sleep(200)\n"
        "run('x = 5', 't5c')\nrun(\"import space\\nprint(x, space.info()[1])\", 't5c')\nrun('x = 6', 't5c')\nrun('print(x)', 't5c')",
        r"history not swept: objfail.*^5 \d+\nstatus ''\nstatus ''\n6\nstatus ''$", secs=60,
        name='T5.12 a failed history sweep: the published version kept')
    # T5.2: OIDs kept over versions (and the boot, below)
    out = raw("run(\"o5 = [1]\\nd5 = {'a': o5}\")\n"
        "run(\"import space\\nprint('oid', space.oid(o5), space.oid(d5), space.oid(1), space.oid([]))\\no5.append(2)\")\n"
        "run(\"import space\\nprint('oid', space.oid(o5), space.oid(d5))\")",
        r"^status ''\noid (\d+) (\d+) None None\nstatus ''\noid \1 \2\nstatus ''$", secs=60, name='T5.2 OIDs kept over versions')
    oids = re.search(r'oid (\d+) (\d+)', out or '')
    # T5.4: a removed store's keys freed (the usage back; gefsck at the
    # end); 40 MiB: a commit is at most 64 MiB (docs/gefs-obj.md 1)
    raw("def ms():\n    f = p.open('#c/bintime')\n    b = p.read(f, 8)\n    p.close(f)\n    return int.from_bytes(b, 'big') // 1000000\n"
        "def sh(c):\n    pid = p.spawn(['/bin/rc', '-c', c])\n    while p.wait()[0] != pid:\n        pass\n"
        "def df():\n    sh('mount -c /srv/gefs.root /usr/glenda/adm adm >[2]/dev/null; echo sync >/usr/glenda/adm/ctl; sleep 1; "
        "echo sync >/usr/glenda/adm/ctl; sleep 1; grep usage /usr/glenda/adm/status')\n"
        "p.create('/usr/glenda/adm', p.OREAD, p.DMDIR | 0o775)", r'\A\Z', name='V5 sh(), df()')
    out = raw("def used():\n    sh('mount -c /srv/gefs.root /usr/glenda/adm adm >[2]/dev/null; echo sync >/usr/glenda/adm/ctl; "
        "grep usage /usr/glenda/adm/status >/env/dfout')\n    f = open('/env/dfout').read().split()\n"
        "    return float(f[1][:-1]) * float(f[3][:-3]) * (1024 if f[3].endswith('GiB') else 1) / 100\n"
        "def settled():\n    u = used()\n    for i in range(30):\n        p.sleep(2000)\n        v = used()\n"
        "        if abs(v - u) < 1:\n            return v\n        u = v\n    return u\n"
        "u = [settled()]\nfor k in range(3):\n    space.create('t5d', space.mkenv({}))\n"
        "    run(\"big4 = [bytes([i]) * 1048576 for i in range(40)]\", 't5d')\n    if k == 0:\n        u.append(used())\n"
        "    for f in ('obj', 'lock', ''):\n        p.remove('/usr/glenda/space/t5d/' + f)\n    u.append(settled())\n"
        "print('used', ' '.join('%.1f' % x for x in u))",
        r"^status ''\nstatus ''\nstatus ''\nused [\d. ]+$", secs=600, name='T5.4 a 40 MiB store made and removed three times')
    m = re.search(r'used ([\d. ]+)$', out or '')
    if m:
        u = [float(x) for x in m.group(1).split()]
        print('\tinfo: T5.4 used (MiB): %.1f, the store %.1f, removed %.1f %.1f %.1f' % tuple(u), flush=True)
        check('T5.4 the store took ~40 MiB, its removals freed it: %+.1f, then %+.1f over two more'
            % (u[1] - u[0], u[4] - u[2]), u[1] - u[0] > 36 and u[4] - u[2] < 8)
    # T5.11: V4's v<N> taken in; a crash before the first commit
    raw("def uv(n):\n    b = bytearray()\n    while n > 127:\n        b.append(n & 127 | 128)\n        n >>= 7\n    b.append(n)\n    return bytes(b)\n"
        "def us(t):\n    return uv(len(t)) + t\n"
        "def b64(n):\n    return bytes((n >> (56 - 8 * i)) & 255 for i in range(8))\n"
        "e5 = space.mkenv({})\nspace.create('t5m', e5)\np.remove('/usr/glenda/space/t5m/obj')\n"
        # objects: 0 __main__'s dict, 1 a list with itself, 2 a dict sharing it
        "body = (uv(3) + bytes([2]) + uv(3) + bytes([7]) + us(b'x') + bytes([4]) + uv(84) + bytes([7]) + us(b'l') + bytes([12]) + uv(1)"
        " + bytes([7]) + us(b'd') + bytes([12]) + uv(2)"
        " + bytes([1]) + uv(3) + bytes([4]) + uv(2) + bytes([8]) + us(b'two') + bytes([12]) + uv(1)"
        " + bytes([2]) + uv(1) + bytes([8]) + us(b'k') + bytes([12]) + uv(1)"
        " + uv(1) + us(b'__main__') + uv(0))\n"
        "img = b'P2SP' + bytes([1]) + b64(3) + e5.encode() + body\nimg += b64(len(img) + 40)\n"
        "h = space.hash(img)\nimg += bytes(int(h[i:i + 2], 16) for i in range(0, 64, 2))\n"
        "with open('/usr/glenda/space/t5m/v0000000000000003', 'wb') as f:\n    f.write(img)\n"
        "pid = p.spawn(['/boot/mp', '-S', 't5m', '-c', \"import space, plan9\\nspace.interval = 100000\\n"
        "print('opened', x, l[1], l[2] is l, d['k'] is l)\\nplan9.sleep(600000)\"])\np.sleep(3000)\n"
        "f = p.open('/proc/%d/ctl' % pid, p.OWRITE); p.write(f, 'kill'); p.close(f)\nwhile p.wait()[0] != pid:\n    pass\n"
        "print(sorted(d[0] for d in p.dirread('/usr/glenda/space/t5m')))\n"
        "run(\"print(x, l[1], l[2] is l, d['k'] is l)\\nx = 43\", 't5m')\n"
        "print(sorted(d[0] for d in p.dirread('/usr/glenda/space/t5m')))\nrun('print(x)', 't5m')",
        r"^opened 42 two True True\n\['lock', 'obj', 'v0000000000000003'\]\n42 two True True\nstatus ''\n"
        r"\['lock', 'obj'\]\n43\nstatus ''$", secs=60, name='T5.11 v<N> taken in, killed before its first commit too')
    # T3.5, T5.9: the root full: a CommitError at a rest and at the end,
    # the version before kept; the space freed: commits again
    out = raw("space.create('t5f', space.mkenv({}))\nrun('x = 1', 't5f')\ndf()\nt = ms()\n"
        "sh('dd -if /dev/zero -of /usr/glenda/fill -bs 1048576 -count 4096 >[2]/dev/null')\n"
        "print('filled', p.stat('/usr/glenda/fill')[2] >> 20, 'MiB', (ms() - t) // 1000, 's')\ndf()",
        r"^status ''\nusage.*\nfilled \d+ MiB \d+ s\nusage.*$", secs=900, name='T5.9 the root filled, gefs read-write')
    if out:
        print('\tinfo: T5.9: ' + out.replace('\n', '; '), flush=True)
    raw("run(\"import space, plan9\\nspace.interval = 0\\ny = b'z' * (48 << 20)\\ntry:\\n    plan9.sleep(1)\\n"
        "except space.CommitError as e:\\n    print('CE', e)\\ndel y\", 't5f')\n"
        "run(\"print(x, 'y' in globals(), 'space' in globals())\", 't5f')",
        r"^CE .*full.*\n(space: commit failed: CommitError: .*\nstatus '.+'\n1 False False|status ''\n1 False True)\nstatus ''$", secs=300,
        name='T3.5 T5.9 the disk full: CommitError at a rest and the end, the version before kept')
    out = raw("try:\n    p.remove('/usr/glenda/fill')\n    print('removed')\nexcept OSError as e:\n    print('rm:', e)\n"
        "    sh('echo usereserve 1 >/usr/glenda/adm/ctl; rm /usr/glenda/fill && echo removed with the reserve; echo usereserve 0 >/usr/glenda/adm/ctl')\n"
        "df()\nrun('x = 2', 't5f')\nrun('print(x)', 't5f')",
        r"^removed\nusage.*\nstatus ''\n2\nstatus ''$", secs=120, name='T5.9 the fill removed (no reserve): x = 2 committed')
    if out:
        print('\tinfo: T5.9: ' + out.split('\n')[0], flush=True)
    # T5.13: killed amid a removed store's sweep: A's sweep keeps the
    # sweeper busy, a sync queued, then B removed: the sync (after A's
    # sweep: waited for) has B's dirent gone, its keys not yet; the
    # boot sweeps on (objresume)
    objs = ("fd = p.open('#s/gefs.root.cmd', p.ORDWR)\np.write(fd, b'show objs\\n')\no = b''\n"
        "while b'objs done' not in o:\n    o += p.read(fd, 8192)\np.close(fd)\n"
        "print([l for l in o.decode().split('\\n') if 'objs done' in l][0])")
    out = raw("p.sleep(6000)\n" + objs, r"objs done: \d+ keys$", secs=120, name='T5.13 show objs before')
    k0 = re.search(r'objs done: (\d+) keys', out or '')
    raw("for n in ('t5ra', 't5rb'):\n    space.create(n, space.mkenv({}))\n"
        "    run(\"big5 = [bytes([i]) * 1048576 for i in range(40)]\", n)\n"
        "p.remove('/usr/glenda/space/t5ra/obj')\n"
        "pid = p.spawn(['/bin/rc', '-c', 'mount -c /srv/gefs.root /usr/glenda/adm adm >[2]/dev/null; echo sync >/usr/glenda/adm/ctl'])\np.sleep(100)\n"
        "p.remove('/usr/glenda/space/t5rb/obj')\nt = ms()\nwhile p.wait()[0] != pid:\n    pass\n"
        "print('removed, synced in', (ms() - t) // 1000, 's')",
        r"^status ''\nstatus ''\nremoved, synced in \d+ s$", secs=300, name='T5.13 two stores removed, a sync between')
    crash()
    if not boot():
        return
    raw(code, r'\A\Z', name='V5 run()')
    out = raw("p.sleep(10000)\n" + objs, r"objs done: \d+ keys$", secs=120, name='T5.13 show objs after the boot')
    k1 = re.search(r'objs done: (\d+) keys', out or '')
    if k0 and k1:
        live = sum(1 for l in text().split('\n') if 'its removal resumed' in l)
        print('\tinfo: T5.13: removals resumed at the boot: %d; keys %s, then %s' % (live, k0.group(1), k1.group(1)), flush=True)
        check('T5.13 the removed stores swept: keys %s == %s' % (k1.group(1), k0.group(1)), k1.group(1) == k0.group(1))
    raw("run(\"print(x, big[5000], len(big2))\")\nrun('print(la, lb)', 't5b')", r"^4 \{'i': -1\} 20000\nstatus ''\n\[99\] \[99\]\nstatus ''$",
        secs=60, name='V5 after the boot')
    if oids:
        raw("run(\"import space\\nprint('oid', space.oid(o5), space.oid(d5), o5)\")",
            r"^oid %s %s \[1, 2\]\nstatus ''$" % oids.groups(), secs=60, name='T5.2 OIDs kept over the boot')
    raw("run('print(x)', 't5f')\nrun('x = 3', 't5f')\nrun('print(x)', 't5f')\nrun('print(x)', 't5m')",
        r"^2\nstatus ''\nstatus ''\n3\nstatus ''\n43\nstatus ''$", secs=60, name='T5.9 T5.11 after the boot')
    halt()

def v6():
    graphpy = open(os.environ['GRAPHPY']).read()
    wpy = ("import graph\nclass P(graph.Node):\n    def __init__(self, n=0):\n        self.n = n\n"
        "def double(x):\n    return 2 * x\n")
    code = ("import plan9 as p, space\n"
        "def run(src, sp='t6', args=None):\n    pid = p.spawn(['/boot/mp'] + (['-S', sp] if args is None else args) + ['-c', src])\n"
        "    while True:\n        w = p.wait()\n        if w[0] == pid:\n            break\n    print('status', repr(w[1]))\n")
    if not boot():
        return
    raw(code + "try:\n    p.create('/usr/glenda/src6', p.OREAD, p.DMDIR | 0o775)\nexcept OSError:\n    pass\n"
        "with open('/usr/glenda/src6/graph.py', 'w') as f:\n    f.write(%r)\n"
        "with open('/usr/glenda/src6/w.py', 'w') as f:\n    f.write(%r)\n"
        "e = space.mkenv({n: space.define('/usr/glenda/src6/%%s.py' %% n) for n in ('graph', 'w')})\n"
        "for n in ('t6', 't6own', 't6b'):\n    space.create(n, e)" % (graphpy, wpy), r'\A\Z', secs=120, name='V6 sources, the Spaces')
    raw("run(\"import graph, w\\nps = [w.P(i) for i in range(5)]\\nfor q in ps:\\n    graph.tag(q, '#p%d #all' % q.n)\\n"
        "ps[1].other = ps[2]\\nx = 41\\nf = w.double\")", r"^status ''$", secs=60, name='V6 t6 made')
    # T6.4: the view's world
    raw("run(\"import space, w\\nv = space.view('t6')\\ng = v.modules['graph']\\nvw = v.modules['w']\\n"
        "print(v.version, v.x, v.f(4), len(g.search('#all')), [q.n for q in g.search('#p3')], v.ps[1].other is v.ps[2], "
        "isinstance(v.ps[0], vw.P), isinstance(v.ps[0], w.P), isinstance(v.ps[0], g.Node))\\nn = v.x + 1\\ndel v, g, vw\", 't6own')",
        r"^2 41 8 5 \[3\] True True False True\nstatus ''$", secs=60, name='T6.4 graph.py in a view, its own classes')
    # T6.3: a foreign object refused, an int of it kept
    raw("run(\"import space\\nv = space.view('t6')\\nkeep = v.ps[0]\\ndel v\", 't6own')\n"
        "run(\"print('keep' in globals(), n)\", 't6own')",
        r"space: commit failed: CommitError: not persistent: a foreign object \(space\.view\) at __main__\.keep\n"
        r"status '.+'\nFalse 42\nstatus ''$", secs=60, name='T6.3 a foreign object: CommitError; n = 42 kept')
    # T6.5: versions 2-4, 2 kept; the writer at work
    raw("run('x = 42')\nrun('x = 43')\npid = p.spawn(['/boot/mp', '-S', 't6', '-c', 'import plan9\\nplan9.sleep(4000)'])\np.sleep(1500)\n"
        "run(\"import space\\nprint(space.view('t6').x, space.view('t6', version=3).x, space.view('t6', version=2).x)\\n"
        "try:\\n    space.view('t6', version=1)\\nexcept OSError as e:\\n    print('refused', e)\", args=[])\n"
        "while p.wait()[0] != pid:\n    pass",
        r"^status ''\nstatus ''\n43 42 41\nrefused .*not kept.*\nstatus ''$", secs=60, name='T6.5 versions; the lock no matter')
    # T6.1: views amid 100 atomic commits
    wr = ("import plan9, space\nspace.interval = 0\nfor i in range(100):\n    with space.atomic():\n"
        "        la[0] += 1\n        plan9.sleep(5)\n        lb[0] += 1\n    plan9.sleep(5)")
    out = raw("run('la = [0]\\nlb = [0]', 't6b')\npid = p.spawn(['/boot/mp', '-S', 't6b', '-c', %r])\n"
        "bad = 0\nvs = []\nwhile len(vs) < 2000:\n    p.sleep(20)\n    v = space.view('t6b')\n    if v.la[0] != v.lb[0]:\n        bad += 1\n"
        "    vs.append(v.version)\n    done = v.la[0] >= 100\n    del v\n    if done:\n        break\n"
        "while p.wait()[0] != pid:\n    pass\nprint('views', len(vs), 'versions', len(set(vs)), 'bad', bad, 'grew', vs == sorted(vs))" % wr,
        r"^status ''\nviews \d+ versions \d+ bad 0 grew True$", secs=180, name='T6.1 views amid 100 commits')
    if out:
        print('\tinfo: T6.1: ' + out.split('\n')[-1], flush=True)
        m = re.search(r'versions (\d+)', out)
        check('T6.1 the views saw many versions: %s' % m.group(1), int(m.group(1)) >= 20)
    # T6.2: none, with a mount of its own
    none = ("import plan9 as p, space\nfd = p.open('#s/gefs.root', p.ORDWR)\nu = p.open('#c/user', p.OWRITE)\n"
        "p.write(u, b'none')\np.close(u)\np.rfork(1)\np.mount(fd, '/usr/glenda/v6mnt', 4, 'main')\n"
        "try:\n    print('none', space.view('/usr/glenda/v6mnt/usr/glenda/space/t6').x)\n"
        "except OSError as e:\n    print('none refused', e)")
    raw("try:\n    p.create('/usr/glenda/v6mnt', p.OREAD, p.DMDIR | 0o775)\nexcept OSError:\n    pass\n"
        "run(%r, args=[])\np.spawn(['/bin/rc', '-c', 'chmod 660 /usr/glenda/space/t6/obj'])\np.wait()\nrun(%r, args=[])" % (none, none),
        r"^none 43\nstatus ''\nnone refused .*permission.*\nstatus ''$", secs=60, name='T6.2 none: a view; 0660 refused')
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

phases = [v0, v1, v2, v3, v4, v5, v6]
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

[ -z "$HW" ] || { echo "PASS: space HW=1"; exit 0; }

# the partition checked and read back in the build VM (userspace gefs)
part=$b/spacepart.img
rm -f "$part"
mib=$(( $(stat -c %s "$disk") / 1048576 - 66 ))
dd if="$disk" of="$part" bs=1M skip=65 count=$mib conv=sparse status=none
echo "### gefsck"
n=${CRASH:-3}
v0=; [ -z "$ONLY" ] || [ "$ONLY" = 0 ] && v0=/usr/glenda/synced$n	# V0's file read back
"$here/gefsck" "$part" $v0 > "$vm/out" 2>&1 || st=1
sed 's/^/	/' "$vm/out" | tail -3
grep -q '^ok' "$vm/out" || { echo "FAIL: gefsck: not ok"; st=1; }
[ -z "$v0" ] || grep -q "^crash $n synced" "$vm/out" || { echo "FAIL: gefsck: no /usr/glenda/synced$n"; st=1; }
rm -f "$part"
[ $st = 0 ] && echo "PASS: space ${ONLY:+ONLY=$ONLY}${ONLY:-PHASE=$phase}" || echo FAIL
exit $st
