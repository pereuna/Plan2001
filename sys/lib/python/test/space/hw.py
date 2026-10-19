# The persistent object environment's tests V0-V3 (docs/object-model.md
# 7) on real hardware: tools/test-space.sh's, with the power cut by hand
# instead of QEMU killed.  Started once at the console (spacehw); it puts
# itself in $home/lib/profile and goes on at each boot, from the stage
# its state file names, until it is done.  Three times it says to cut
# the power: the crash steps of V0 (T0.1), V2 (T2.1-2.4) and V3 (T3.1-3.3,
# T3.6) at once.  Each result is a line in $home/spacehw/log, synced.
#   spacehw         start, or go on
#   spacehw reset   start again (the spaces of the last run renamed)
import sys, json
import plan9 as p, space

def readf(path):
    with open(path) as f:
        return f.read()

def readb(path):
    with open(path, 'rb') as f:
        return f.read()

user = readf('/dev/user').strip()
home = '/usr/' + user
H = home + '/spacehw'
SP = home + '/space'
LOG = H + '/log'
STATE = H + '/state'
PROFILE = home + '/lib/profile'
HOOK = 'spacehw\n'
CUTS = 3
fails = 0

def mkdir(path):
    try:
        p.close(p.create(path, p.OREAD, p.DMDIR | 0o775))
    except OSError:
        pass

def writef(path, s, sync=False):
    with open(path, 'w') as f:
        f.write(s)
    if sync:
        p.sync(path)

def log(line):
    print(line)
    fd = p.open(LOG, p.OWRITE)
    p.seek(fd, 0, 2)
    p.write(fd, line + '\n')
    p.close(fd)
    p.sync(LOG)

def check(name, ok, out=''):
    global fails
    if not ok:
        fails += 1
        out = out.strip()
        log('FAIL ' + name + (': ' + repr(out[-300:]) if out else ''))
    else:
        log('ok   ' + name)
    return ok

def save(st):
    writef(STATE, json.dumps(st), True)

_bt = None
def ns():
    global _bt
    if _bt is None:
        _bt = p.open('#c/bintime')
    p.seek(_bt, 0, 0)
    return int.from_bytes(p.read(_bt, 8), 'big')

def waitpid(pid):
    while True:
        w = p.wait()
        if w[0] == pid:
            return w[1]

def mp(args):
    """/boot/mp ARGS: its output and status"""
    fd = p.create('/tmp/spacehw.out', p.OWRITE, 0o664)
    pid = p.spawn(['/boot/mp'] + args, [None, fd, fd])
    p.close(fd)
    st = waitpid(pid)
    return readf('/tmp/spacehw.out'), st

def run(src, sp):
    return mp(['-S', sp, '-c', src])

def runenv(src, env=None):
    return mp((['-E', env] if env else []) + ['-c', src])

def bg(src, sp):
    return p.spawn(['/boot/mp', '-S', sp, '-c', src])

def rc(cmd):
    return mp(['-c', "import plan9 as p\npid = p.spawn(['/bin/rc', '-c', %r])\n"
        "while p.wait()[0] != pid:\n    pass" % cmd])

def expect(name, got, out, status=''):
    o, st = got
    return check(name, o == out and (st == status if status is not None else st != ''),
        'output %r status %r' % (o, st))

def blob(i):
    line = 'crash %d synced ' % i
    line += '.' * (63 - len(line)) + '\n'
    return line * (1048576 // 64)

# ---- V0 ----

def t02():
    lk = H + '/lock'
    try:
        p.remove(lk)
    except OSError:
        pass
    fd = p.create(lk, p.ORDWR | p.OCEXEC, p.DMEXCL | 0o600)
    check('T0.2 create DMEXCL', p.stat(lk)[1] & p.DMEXCL != 0)
    try:
        p.close(p.open(lk, p.ORDWR))
        ok = False
    except OSError:
        ok = True
    check('T0.2 a second open in this process fails', ok)
    o, st = mp(['-c', "import plan9 as p\ntry:\n    p.open(%r, p.ORDWR)\n    print('opened')\n"
        "except OSError as e:\n    print('child:', e)" % lk])
    check('T0.2 another process fails', o.startswith('child:'), o)
    o, st = mp(['-c', "import plan9 as p\ntry:\n    p.fd2path(%d)\n    print('fd there')\n"
        "except OSError:\n    print('no fd')" % fd])
    check('T0.2 OCEXEC: no fd after exec', o == 'no fd\n', o)
    p.close(fd)
    o, st = mp(['-c', "import plan9 as p\np.open(%r, p.ORDWR)\nprint('opened')" % lk])
    check('T0.2 open after close', o == 'opened\n', o)
    pid = p.spawn(['/boot/mp', '-c', "import plan9 as p\nf = p.open(%r, p.ORDWR)\np.sleep(600000)" % lk])
    p.sleep(1000)
    try:
        p.close(p.open(lk, p.ORDWR))
        held = False
    except OSError:
        held = True
    f = p.open('/proc/%d/ctl' % pid, p.OWRITE)
    p.write(f, 'kill')
    p.close(f)
    waitpid(pid)
    try:
        p.close(p.open(lk, p.ORDWR))
        freed = True
    except OSError:
        freed = False
    check("T0.2 the holder's death frees it", held and freed, 'held %r freed %r' % (held, freed))

def t03():
    def t(n):
        r = []
        for i in range(10):
            writef(H + '/synct', 'y' * n + str(i))
            a = ns()
            p.sync(H + '/synct')
            r.append((ns() - a) // 1000000)
        return sorted(r)[5]
    small, big = t(10), t(1048576)
    log('info T0.3 plan9.sync median (this machine): small %d ms, 1 MiB %d ms' % (small, big))

def t01_before(i):
    writef(H + '/synced%d' % i, blob(i), True)

def t01_after(i):
    try:
        ok = readf(H + '/synced%d' % i) == blob(i)
    except OSError:
        ok = False
    check('T0.1 power cut %d: synced%d kept' % (i, i), ok)

# ---- V1 ----

def v1():
    src = {
        'calc1': 'def f(x):\n    return x * 2\n',
        'calc2': 'def f(x):\n    return x * 3\n',
        'user2': 'import calc\n',
        'bad': 'x = 1 / 0\n',
    }
    mkdir(H + '/src')
    for n, t in src.items():
        writef(H + '/src/%s.py' % n, t)
    writef(H + '/calc.py', "def f(x):\n    return 'shadow'\n")
    check('space.runtime', space.runtime == 'mp-1.27.0-mpy6.3', space.runtime)
    h = {n: space.define(H + '/src/%s.py' % n) for n in src}
    check("T1.1 define: the source's SHA-256", all(h[n] == space.hash(t) for n, t in src.items()), repr(h))
    m = home + '/lib/mod/%s.mpy' % h['calc1']
    st, b = p.stat(m), readb(m)
    check('T1.1 the .mpy made once, the source kept', space.define(H + '/src/calc1.py') == h['calc1']
        and p.stat(m) == st and readb(m) == b and len(b) > 0
        and readf(home + '/lib/mod/%s.py' % h['calc1']) == src['calc1'])
    e1 = space.mkenv({'calc': h['calc1']})
    e2 = space.mkenv({'calc': h['calc2']})
    e3 = space.mkenv({'calc': h['calc1'], 'user2': h['user2'], 'bad': h['bad']})
    check('mkenv: named by its text', e1 != e2 and e1 == space.mkenv({'calc': h['calc1']})
        and readf(home + '/lib/env/' + e1) == 'runtime %s\ncalc %s\n' % (space.runtime, h['calc1']))
    f10 = "import calc; print('f', calc.f(10))"
    check('T1.2 two Environments, two versions', runenv(f10, e1) == ('f 20\n', '') and runenv(f10, e2) == ('f 30\n', ''))
    p.chdir(H)
    check('T1.3 the working directory does not count', runenv(f10, e1) == ('f 20\n', '') and runenv(f10) == ('f shadow\n', ''))
    p.chdir(home)
    o, st = runenv('import nope', e1)
    check('T1.4 an unknown name', "ImportError: no module named 'nope' in the Environment" in o and st != '', o)
    o, st = runenv('import a.b', e1)
    check('T1.4 a package', 'ImportError: packages not supported' in o and st != '', o)
    expect('T1.4 builtin modules', runenv('import json, plan9, space; print(json.dumps([1]), space.env() == %r)' % e1, e1), '[1] True\n')
    expect('T1.4 a module loaded once', runenv('import calc, user2; print(user2.calc is calc)', e3), 'True\n')
    expect('T1.4 a failed top level not in sys.modules',
        runenv('import sys\ntry:\n    import bad\nexcept ZeroDivisionError:\n    print("bad" in sys.modules)', e3), 'False\n')
    t = 'runtime mp-0.0.0-mpy0.0\ncalc %s\n' % h['calc1']
    x = space.hash(t)
    writef(home + '/lib/env/' + x, t)
    o, st = runenv('print(1)', x)
    check('T1.5 a wrong runtime', 'runtime mp-0.0.0-mpy0.0, this mp is ' + space.runtime in o and st != '', o)
    x = 'f' * 64
    writef(home + '/lib/env/' + x, readf(home + '/lib/env/' + e1))
    o, st = runenv('print(1)', x)
    check('T1.5 a corrupt Environment', 'corrupt' in o and st != '', o)
    o, st = runenv('print(1)', 'x;y')
    check('T1.5 not an Environment', 'not an Environment' in o and st != '', o)

# ---- V2 ----

SRC2 = {
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
INC = "import counter; print('c', counter.increment())"

def v2_setup():
    mkdir(H + '/src2')
    for n, t in SRC2.items():
        writef(H + '/src2/%s.py' % n, t)
    e = space.mkenv({n: space.define(H + '/src2/%s.py' % n) for n in SRC2})
    space.create('t2', e)
    space.create('t2b', e)
    check('space.create', sorted(d[0] for d in p.dirread(SP + '/t2')) == ['lock', 'obj'])
    try:
        space.create('t2', e)
        ok = False
    except ValueError as x:
        ok = 'space t2 exists' in str(x)
    check('space.create: one there', ok)
    expect('T2.1 before cut 1: 1', run(INC, 't2'), 'init counter\nc 1\n')
    expect('T2.3-4, 6, 10: the objects made', run(
        "import shapes, keyed\nb = shapes.Base(5)\ns = shapes.Sub.make(7)\nadd = shapes.adder(10)\n"
        "m = s.get\nsc = shapes.scale\nl = []\nd = {'x': l, 'y': l}\nl.append(d)\nbig = 2 ** 100\n"
        "fl = 1.5\nfs = frozenset([1, 2])\nby = bytearray(b'ab')\n__transient__ = ('tmp',)\ntmp = [1]\nkeep = 2\n"
        "kd = {keyed.K(1): 'one', keyed.K((2, 3)): 'two'}\nprint('made')", 't2'),
        'init shapes\nclass body Base\nmade\n')
    o, st = rc("echo 'r = 10' | /boot/mp -S t2 >/dev/null")
    check('T2.1 a REPL statement', st == '', o)

def v2_after1():
    expect('T2.1-2 after cut 1: 2, no top level', run(INC, 't2'), 'c 2\n')
    expect('T2.3-4, 6, 10: restored, no class body', run(
        "print(b.get(), s.get(), m(), add(5), sc(2), sc(2, 4, off=0), shapes.Base.twice(4), shapes.Sub.make(1).get(), "
        "type(s) is shapes.Sub, isinstance(s, shapes.Base), b.kind)\n"
        "print(d['x'] is d['y'], l[0] is d, big, fl, sorted(fs), type(fs) is frozenset, by)\nprint(tmp, keep, r)\n"
        "print(kd[keyed.K(1)], kd[keyed.K((2, 3))], len(kd))", 't2'),
        "5 107 107 15 7 8 8 101 True True base\nTrue True 1267650600228229401496703205376 1.5 [1, 2] True bytearray(b'ab')\n"
        "None 2 10\none two 2\n")

def v2_after2():
    expect('T2.1 after cut 2: 3', run(INC, 't2'), 'c 3\n')
    o, st = run('x = (i for i in range(3))', 't2')
    check('T2.5 a generator', 'space: commit failed: CommitError: not persistent: generator at __main__.x' in o and st != '', o)
    expect('T2.5 the version before', run("print('x' in globals(), counter.n)", 't2'), 'False 3\n')
    expect('T2.7 a failed import', run('try:\n    import badtop\nexcept ZeroDivisionError:\n    print("failed")', 't2'), 'failed\n')
    expect('T2.7 no instance', run("import sys; print('badtop' in sys.modules, 'badtop' in globals())", 't2'), 'False False\n')
    ok = run(INC, 't2b') == ('init counter\nc 1\n', '') and run(INC, 't2b') == ('c 2\n', '')
    check('T2.8 two Spaces', ok and run("print('c', counter.increment())", 't2') == ('c 4\n', ''))
    sl = p.spawn(['/boot/mp', '-S', 't2', '-c', 'import plan9\nplan9.sleep(5000)'])
    p.sleep(1500)
    o, st = run('print(1)', 't2')
    check('T2.9 a second mp -S', 'space t2 locked' in o and st != '', o)
    waitpid(sl)
    o = run("import plan9\nif plan9.rfork(16 | plan9.RFFDG) == 0:\n    child = 1\n    raise SystemExit\n"
        "plan9.wait()\nprint('parent')", 't2')
    check("T2.9 an rfork child's changes lost", o == ('parent\n', '') and run("print(globals().get('child'))", 't2') == ('None\n', ''))
    v = sorted(d[0] for d in p.dirread(SP + '/t2'))
    check('the versions in the store (V5)', v == ['lock', 'obj'], repr(v))

# ---- V3 ----

LOOP1 = "import plan9\nif 'n' not in globals():\n    n = 0\nwhile True:\n    n += 1\n    plan9.sleep(100)"
LOOP2 = ("import plan9, space\nspace.interval = 0.3\nif 'a' not in globals():\n    a = b = 0\n"
    "while True:\n    with space.atomic():\n        a += 1\n        plan9.sleep(50)\n        b += 1\n    plan9.sleep(10)")

def v3_setup():
    e = space.mkenv({})
    for n in ('t3a', 't3b', 't3c', 't3d'):
        space.create(n, e)
    log('ok   V3 spaces')

def v3_after(st, i):
    o, s = run("print(n)", 't3a')
    n = int(o) if s == '' and o.strip().isdigit() else -1
    check('T3.1/T3.6 after cut %d: n grew: %d > %d' % (i, n, st['n']), n > st['n'], o)
    o, s = run("print(a, b)", 't3b')
    try:
        a, b = (int(x) for x in o.split())
    except ValueError:
        a, b = -1, -2
    check('T3.2 after cut %d: a == b == %d, grew from %d' % (i, a, st['a']), a == b and a > st['a'], o)
    st['n'], st['a'] = n, a

def t33_before():
    expect('T3.3 x = 1', run('x = 1', 't3c'), '')
    writef('/env/spaceslow', '600000')
    bg('x = 2', 't3c')
    p.sleep(3000)
    p.remove('/env/spaceslow')
    t = sorted(d[0] for d in p.dirread(SP + '/t3c'))
    check('T3.3 version 3 half written, in the store', t == ['lock', 'obj'], repr(t))

def t33_after():
    expect('T3.3 v1 used', run('print(x)', 't3c'), '1\n')
    t = [d[0] for d in p.dirread(SP + '/t3c') if d[0][0] == 't']
    check('T3.3 t gone', t == [], repr(t))

# T3.4 (a version file's byte changed) and T3.5 (a small gefs filled):
# not since V5, as in tools/test-space.sh: the versions are in the
# store (no files), and the userspace gefs has no DMOBJ; the disk
# full is T5.9 there.

# ---- the stages ----

def hook(on):
    try:
        prof = readf(PROFILE)
    except OSError:
        prof = ''
    prof = prof.replace(HOOK, '')
    if on:
        prof += HOOK
    mkdir(home + '/lib')
    writef(PROFILE, prof, True)

def cut(st, i):
    """the crash steps' before, then the power cut"""
    t01_before(i)
    # T3.3 first: $spaceslow slows every commit while it is set
    if i == CUTS:
        t33_before()
    bg(LOOP1, 't3a')
    bg(LOOP2, 't3b')
    p.sleep(3000)
    st['stage'] = i
    save(st)
    # unsynced: kept or not (info)
    writef(H + '/unsynced%d' % i, 'x')
    print()
    print('=' * 60)
    print('  spacehw: CUT THE POWER NOW (%d of %d): hold the power button.' % (i, CUTS))
    print('  Then boot Plan2001 again; the tests go on by themselves.')
    print('=' * 60)
    while True:
        p.sleep(60000)

def main():
    global fails
    if len(sys.argv) > 1 and sys.argv[1] == 'reset':
        try:
            p.remove(STATE)
        except OSError:
            pass
    try:
        st = json.loads(readf(STATE))
    except (OSError, ValueError):
        st = None
    if st is None:
        mkdir(H)
        # a run before: its spaces aside
        try:
            p.wstat(SP, 'space-%d' % (ns() // 1000000000))
        except OSError:
            pass
        writef(LOG, '', True)
        hook(True)
        st = {'stage': 0, 'n': 0, 'a': 0, 'fails': 0}
        log('spacehw: start (V0-V3, %d power cuts)' % CUTS)
        t02()
        t03()
        v1()
        v2_setup()
        v3_setup()
        st['fails'] = fails
        cut(st, 1)
    i = st['stage']
    if i == 'done':
        print('spacehw: done; the results in ' + LOG + ' (spacehw reset: again)')
        return
    fails = st['fails']
    log('spacehw: after power cut %d' % i)
    t01_after(i)
    v3_after(st, i)
    if i == 1:
        v2_after1()
    elif i == 2:
        v2_after2()
    if i < CUTS:
        st['fails'] = fails
        cut(st, i + 1)
    t33_after()
    n = sum(1 for d in p.dirread(H) if d[0].startswith('unsynced'))
    log('info T0.1: unsynced files kept after a power cut: %d of %d' % (n, CUTS))
    log('spacehw: done: %d FAIL' % fails)
    st['stage'] = 'done'
    save(st)
    hook(False)
    print('spacehw: done; fshalt, then Linux: the log is ' + LOG)
    p.spawn(['/bin/rc', '-c', 'fshalt'])
    p.wait()

main()
