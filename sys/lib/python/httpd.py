# An HTTP server with MicroPython's plan9 module only: the system calls on
# the kernel's devices (docs/mpinit.md), no libraries, no rc, no root
# needed.  Typed into the console of /boot/mp -B (paste mode: Ctrl-E,
# the file, Ctrl-D) or, on a root, import httpd:
#	net()		ether0 and ip into /net, an address on it as
#			ip/ipconfig would (QEMU's user network by default)
#	serve(80)	/net/tcp's announce and listen; each call in a
#			process of its own (rfork: the interpreter copied)
# Pages: / (the request and the system), /boot (the kernel's image),
# /env/NAME (the kernel's environment).  Ctrl-C stops serve.
import plan9 as p


def exists(name):
    try:
        p.stat(name)
        return True
    except OSError:
        return False


def cat(name):
    f = p.open(name)
    try:
        return p.read(f).decode().strip()
    finally:
        p.close(f)


# a line in one write: print's pieces of calls at once would mix
def log(*a):
    p.write(1, 'httpd: ' + ' '.join(str(x) for x in a) + '\n')


def net(addr='10.0.2.15', mask='255.255.255.0', gw='10.0.2.2'):
    if exists('/net/ipifc/0'):
        return
    for d in ('#l', '#I'):
        p.bind(d, '/net', p.MAFTER)
    ifc = p.open('/net/ipifc/clone', p.ORDWR)
    p.write(ifc, 'bind ether /net/ether0')
    p.write(ifc, 'add %s %s' % (addr, mask))
    p.close(ifc)
    r = p.open('/net/iproute', p.OWRITE)
    p.write(r, 'add 0 0 ' + gw)
    p.close(r)


def page(method, path, remote, hits):
    if path == '/boot':
        rows = ''.join('<li>%s %d</li>' % (d[0], d[2]) for d in p.dirread('/boot'))
        return 'text/html', '<h1>/boot</h1><ul>%s</ul>' % rows
    if path.startswith('/env/'):
        return 'text/plain', cat(path) + '\n'
    root = 'a root' if exists('/amd64/bin') else 'no root'
    return 'text/html', (
        '<h1>Plan2001: MicroPython as init</h1>'
        f'<p>{method} {path} from {remote}, request {hits}</p>'
        f'<p>user {cat("#c/user")}, pid {p.getpid()}, {root}</p>'
        '<p><a href="/boot">/boot</a> <a href="/env/cputype">/env/cputype</a></p>')


def handle(c, hits):
    fd = p.open(c + '/data', p.ORDWR)
    req = b''
    while b'\r\n\r\n' not in req:
        b = p.read(fd, 4096)
        if not b:
            return
        req += b
    remote = cat(c + '/remote')
    try:
        method, path = req.split(b'\r\n')[0].decode().split(' ')[:2]
        ctype, body = page(method, path, remote, hits)
        status = '200 OK'
    except Exception as e:
        method, path, status = '?', '?', '500 Internal Server Error'
        ctype, body = 'text/plain', repr(e) + '\n'
    body = body.encode()
    p.write(fd, 'HTTP/1.0 %s\r\nContent-Type: %s; charset=utf-8\r\n'
            'Content-Length: %d\r\nConnection: close\r\n\r\n' % (status, ctype, len(body)))
    p.write(fd, body)
    log(remote, method, path, status[:3], 'pid', p.getpid())


def serve(port=80):
    ctl = p.open('/net/tcp/clone', p.ORDWR)
    conv = '/net/tcp/' + p.read(ctl, 32).decode().strip()
    p.write(ctl, 'announce *!%d' % port)
    log(conv, 'port', port)
    hits = 0
    try:
        while True:
            lcfd = p.open(conv + '/listen', p.ORDWR)
            c = '/net/tcp/' + p.read(lcfd, 32).decode().strip()
            hits += 1
            # a browser's idle connection waits in its own process
            if p.rfork(p.RFPROC | p.RFFDG | p.RFNOWAIT) == 0:
                try:
                    handle(c, hits)
                except BaseException as e:
                    log(c, repr(e))
                p.exits()
            p.close(lcfd)
    finally:
        p.close(ctl)
