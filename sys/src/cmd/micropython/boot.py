# Plan2001's first user program (docs/mpinit.md): the kernel runs
# /boot/mp -B (plan9.ini init=), whose built-in source this is.  The
# kernel's initcode has bound #c #d #e #p #s #σ and opened fds 0-2 on
# #c/cons, which only prints.  This makes the rest of the name space and
# the console (kbdfs, from the kernel's image too); the REPL comes after
# it, and again after each Ctrl-D.  A root is optional: plan9.ini root=.
import plan9 as p


def env(name):
    try:
        fd = p.open('/env/' + name)
    except OSError:
        return None
    try:
        return p.read(fd).decode().rstrip('\0').strip()
    finally:
        p.close(fd)


def exists(name):
    try:
        p.stat(name)
        return True
    except OSError:
        return False


def run(argv):
    pid = p.spawn(argv)
    while True:
        w = p.wait()
        if w[0] == pid:
            return w[1]


# the devices (as the rc init's bind -qa)
for d in ('#¤', '#¶', '#P', '#S', '#f', '#æ', '#t', '#b', '#m'):
    try:
        p.bind(d, '/dev', p.MAFTER)
    except OSError:
        pass

# the console: kbdfs serves /dev/cons and the rest, on the serial port
# if plan9.ini has console=N
argv = ['/boot/kbdfs', '-q', '-s', 'cons']
c = env('console')
if c:
    argv.append('/dev/eia' + c.split()[0])
e = run(argv)
if e:
    print('boot: kbdfs:', e)
fd = p.open('/dev/cons', p.ORDWR)
for i in (0, 1, 2):
    p.dup(fd, i)
p.close(fd)

# the root, if the kernel mounted one: its programs, the keyboard's map
root = exists('/amd64/bin')
if root:
    p.bind('/amd64/bin', '/bin')
    p.bind('/rc/bin', '/bin', p.MAFTER)
km = env('kbmap')
if km and exists('/sys/lib/kbmap/' + km):
    f = p.open('/sys/lib/kbmap/' + km)
    m = p.read(f, 65536)
    p.close(f)
    k = p.open('/dev/kbmap', p.OWRITE)
    p.write(k, m)
    p.close(k)
home = '/usr/' + (env('user') or 'none')
if exists(home):
    p.chdir(home)
print('boot: MicroPython from the kernel,', 'root' if root else 'no root')
del p, argv, c, e, fd, i, d, root, km, home, env, exists, run
