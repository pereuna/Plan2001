#!/usr/bin/env python3
"""Controlled gefs recovery comparison (AMD64 QEMU/KVM).

  tools/test-gefs-audit.py --root PARTITION --repo space=DIR --repo main=DIR

ROOT is one clean, identical root image for both kernels. It must contain
/usr/glenda/auditread.py, auditwork.rc and ktest-{main,space} (see
--prepare-tree). Defaults: control plus all six stops, three repetitions.
Only disposable images below build/phase2 are written. Run gefs-audit on
its saved *.gefs images afterwards. No unknown blocks are reclaimed.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
BASE = ROOT / 'build/phase2'
OUTPUT = BASE / 'matrix'
CHECKPOINTED_DELETE = False
ANSI = re.compile(r'\x1b\[[0-9;=?]*[a-zA-Z]')
PHASES = ['control', 'log-chain', 'log-flush', 'arena-heads', 'supers', 'arena-tails', 'cleanup']

READPY = '''import plan9 as p, sys
cmd = ' '.join(sys.argv[1:])
f = p.open('#s/gefs.root.cmd', p.ORDWR)
p.write(f, (cmd + '\\n').encode())
s = b''
marker = b'auditstop:' if cmd.startswith('auditstop ') else b'audit complete:'
while marker not in s or b'\\n' not in s.split(marker, 1)[1]:
    s += p.read(f, 8192)
p.close(f)
print(s.decode(), end='')
'''
WORKRC = '''#!/bin/rc
mkdir -p /usr/glenda/audit || exit mkdir
for(i in `{seq 1 64}){
    dd -if /dev/zero -of /usr/glenda/audit/f$i -bs 1048576 -count 1 >[2]/dev/null || exit write
}
for(i in `{seq 2 2 64})
    rm /usr/glenda/audit/f$i || exit remove
'''

def run(args, **kwargs):
    return subprocess.run([str(x) for x in args], check=True, **kwargs)

def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for data in iter(lambda: f.read(1024*1024), b''):
            h.update(data)
    return h.hexdigest()

class Guest:
    def __init__(self, directory, disk):
        self.dir, self.disk = directory, disk
        self.q = self.relay = None
        self.serial = directory / 'serial.log'
        self.bootno = 0

    def text(self):
        return ANSI.sub('', self.serial.read_text(errors='replace')).replace('\r', '')

    def wait(self, text, timeout=150):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            out = self.text()
            if re.search(text, out, re.M):
                return out
            if self.q.poll() is not None or re.search(r'^panic', out, re.M):
                raise RuntimeError('guest died: ' + out[-2000:])
            time.sleep(.1)
        raise TimeoutError(text + ': ' + self.text()[-2000:])

    def boot(self):
        self.stop()
        self.dir.mkdir(parents=True, exist_ok=True)
        self.bootno += 1
        if self.serial.exists():
            self.serial.rename(self.dir / f'serial-{self.bootno-1}.log')
        self.serial.touch()
        for p in self.dir.glob('*.sock'):
            p.unlink()
        fifo = self.dir / 'serial.in'
        if fifo.exists(): fifo.unlink()
        os.mkfifo(fifo)
        shutil.copyfile('/usr/share/OVMF/OVMF_VARS_4M.fd', self.dir / 'vars.fd')
        args = ['qemu-system-x86_64', '-name', 'gefs-audit', '-nodefaults', '-accel', 'kvm',
                '-cpu', 'host', '-machine', 'q35', '-vga', 'std', '-m', '2048', '-smp', '2',
                '-drive', 'if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd',
                '-drive', f'if=pflash,format=raw,file={self.dir}/vars.fd',
                '-drive', f'file={self.disk},format=raw,if=none,id=d0',
                '-device', 'virtio-blk-pci-non-transitional,drive=d0,bootindex=0',
                '-nic', 'none', '-display', 'none', '-monitor', f'unix:{self.dir}/mon.sock,server=on,wait=off',
                '-chardev', f'socket,id=ser,path={self.dir}/serial.sock,server=on,wait=off', '-serial', 'chardev:ser']
        self.q = subprocess.Popen(args, stdout=subprocess.DEVNULL,
                                  stderr=open(self.dir / 'qemu.log', 'w'))
        self.relay = subprocess.Popen([str(ROOT / 'tools/9run'), '--relay', str(self.dir)],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.wait(r'plan2001% ')

    def stop(self):
        for p in (self.q, self.relay):
            if p is not None and p.poll() is None:
                p.kill(); p.wait()
        self.q = self.relay = None

    def command(self, command, timeout=180, name='command'):
        r = subprocess.run([str(ROOT / 'tools/9run'), '-d', str(self.dir), '-t', str(timeout), command],
                           capture_output=True, text=True)
        with open(self.dir / 'commands.log', 'a') as f:
            f.write(f'### {name}: {command}\n{r.stdout}{r.stderr}\nstatus {r.returncode}\n')
        if r.returncode:
            raise RuntimeError(f'{name}: {r.stdout[-1000:]} {r.stderr}')
        return r.stdout

    def monitor(self, command):
        with socket.socket(socket.AF_UNIX) as s:
            s.settimeout(10); s.connect(str(self.dir / 'mon.sock'))
            data = b''
            while b'(qemu) ' not in data: data += s.recv(4096)
            s.sendall(command.encode() + b'\n')
            data = b''
            while b'(qemu) ' not in data: data += s.recv(4096)
            if b'Error' in data: raise RuntimeError(str(data))

    def capture(self, label, size):
        path = self.dir / (label + '.gefs')
        if self.q is not None: self.monitor('stop')
        try:
            run(['dd', f'if={self.disk}', f'of={path}', 'bs=1M', 'skip=65',
                 f'count={size//1048576}', 'conv=sparse', 'status=none'])
        finally:
            if self.q is not None: self.monitor('cont')
        return str(path)

    def live(self, label):
        text = self.command('micropython /usr/glenda/auditread.py audit', name='audit-' + label)
        (self.dir / (label + '-live.log')).write_text(text)
        self.command('echo check >/n/adm/ctl', name='check-' + label)

    def halt(self):
        self.command('fshalt', name='halt')
        self.wait(r'done halting', 120)
        self.stop()


def trial(name, repo, template, size, phase, repeat, ktest=False):
    directory = OUTPUT / name / f'{phase}-{repeat}'
    if directory.exists():
        raise RuntimeError(f'refusing to overwrite trial: {directory}')
    directory.mkdir(parents=True)
    disk = directory / 'disk.img'
    run(['cp', '--sparse=always', '--reflink=auto', template, disk])
    g = Guest(directory, disk)
    samples = {}
    result = dict(variant=name, phase=phase, repeat=repeat, samples=samples)
    try:
        g.boot()
        g.command('mount -c /srv/gefs.root /n/adm adm && mkdir -p /usr/glenda/audit && '
                  'echo durable >/usr/glenda/audit/durable && echo snap main auditbase >/n/adm/ctl && '
                  'echo sync >/n/adm/ctl', name='prepare')
        if CHECKPOINTED_DELETE:
            g.command('rc /usr/glenda/auditwork.rc && echo sync >/n/adm/ctl',
                      timeout=300, name='checkpoint-files')
        g.live('before')
        samples['before'] = g.capture('before', size)
        workload = 'rc /usr/glenda/auditwork.rc && echo changed >/usr/glenda/audit/trigger && echo sync >/n/adm/ctl'
        if CHECKPOINTED_DELETE:
            workload = 'rm -f /usr/glenda/audit/f* && echo changed >/usr/glenda/audit/trigger && echo sync >/n/adm/ctl'
        if phase == 'log-chain':
            workload = 'for(c in 1 2 3 4 5 6 7 8){ rc /usr/glenda/auditwork.rc || exit work }; echo sync >/n/adm/ctl'
        if phase == 'control':
            g.command(workload, timeout=300, name='workload')
            g.halt()
        else:
            g.command(f'micropython /usr/glenda/auditread.py auditstop {phase}', name='arm')
            pending = subprocess.Popen([str(ROOT / 'tools/9run'), '-d', str(directory), '-t', '300', workload],
                                       stdout=open(directory / 'workload.log', 'w'), stderr=subprocess.STDOUT)
            try:
                g.wait(r'^audit stopped: phase=' + re.escape(phase) + r' ', 300)
            finally:
                g.stop()
                if pending.poll() is None: pending.kill()
                pending.wait()
        # Preserve the crash bytes BEFORE loadlog can truncate a tail.
        samples['raw'] = g.capture('raw', size)
        for label in ('recovered', 'rebooted'):
            g.boot()
            g.command('mount -c /srv/gefs.root /n/adm adm && cat /usr/glenda/audit/durable', name=label)
            out = g.command('md5sum /usr/glenda/audit/durable', name='durable-checksum')
            expected = hashlib.md5(b'durable\n').hexdigest()
            lines = re.findall(r'^([0-9a-f]{32})\s+/usr/glenda/audit/durable\s*$',
                               ANSI.sub('', out).replace('\r', ''), re.M)
            if lines != [expected]:
                raise RuntimeError('synced content checksum mismatch: ' + out)
            result.setdefault('checksums', []).append(expected + '\t/usr/glenda/audit/durable')
            if len(set(result['checksums'])) != 1:
                raise RuntimeError('synced content changed')
            g.live(label)
            samples[label] = g.capture(label, size)
            if label == 'recovered':
                g.command('echo sync >/n/adm/ctl', name='first-sync')
                g.live('synced')
                samples['synced'] = g.capture('synced', size)
            g.halt()
        result['ok'] = True
    except Exception as e:
        result['error'] = str(e)
        raise
    finally:
        g.stop()
        (directory / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result), flush=True)


def main():
    global OUTPUT, CHECKPOINTED_DELETE
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path)
    p.add_argument('--repo', action='append', default=[])
    p.add_argument('--repeat', type=int, default=3)
    p.add_argument('--phases', default=','.join(PHASES))
    p.add_argument('--prepare-tree', type=Path)
    p.add_argument('--ktest', action='store_true')
    p.add_argument('--resume', action='store_true')
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--checkpointed-delete', action='store_true')
    p.add_argument('--retry-failed', action='store_true')
    p.add_argument('--output', type=Path,
                   help='output directory for a separate diagnostic run')
    args = p.parse_args()
    CHECKPOINTED_DELETE = args.checkpointed_delete
    if CHECKPOINTED_DELETE: OUTPUT = BASE / 'delete-matrix'
    if args.output: OUTPUT = args.output.resolve()
    if args.prepare_tree:
        home = args.prepare_tree / 'usr/glenda'
        home.mkdir(parents=True, exist_ok=True)
        (home / 'auditread.py').write_text(READPY)
        (home / 'auditwork.rc').write_text(WORKRC)
        return
    repos = {k: Path(v).resolve() for k, v in (x.split('=', 1) for x in args.repo)}
    phases = args.phases.split(',')
    if not args.root or not repos or any(x not in PHASES for x in phases) or args.repeat < 1:
        p.error('root, repo and valid phases/repeat required')
    size = args.root.stat().st_size
    if size % 1048576: p.error('root size must be whole MiB')
    BASE.mkdir(parents=True, exist_ok=True)
    manifest = dict(root=str(args.root.resolve()), root_sha256=sha(args.root),
                    repeat=args.repeat, phases=phases, nosync=True, checkpointed_delete=CHECKPOINTED_DELETE, variants={})
    templates = {}
    for name, repo in repos.items():
        out = BASE / (OUTPUT.name + '-' + name + '-template.img')
        env = dict(os.environ, ROOTIMG=str(args.root.resolve()), CONSOLE='0', INIT='/init',
                   INI='gefsauditnosync=1;gefsaudittrace=1')
        run([repo / 'tools/gefsdisk', out], env=env)
        templates[name] = out
        manifest['variants'][name] = dict(repo=str(repo), kernel_sha256=sha(repo / 'build/amd64/9pc64'),
            head=subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip())
    oldmanifest = BASE / (OUTPUT.name + '-manifest.json')
    if args.resume and oldmanifest.exists():
        old = json.loads(oldmanifest.read_text())
        if old['root_sha256'] != manifest['root_sha256'] or old['variants'] != manifest['variants']:
            raise RuntimeError('resume root or kernel manifest mismatch')
    oldmanifest.write_text(json.dumps(manifest, indent=2) + '\n')
    if args.ktest:
        for name in repos:
            d = OUTPUT / name / 'kernel-tests'
            if d.exists(): raise RuntimeError(f'kernel test directory exists: {d}')
            d.mkdir(parents=True)
            disk = d / 'disk.img'
            run(['cp', '--sparse=always', '--reflink=auto', templates[name], disk])
            g = Guest(d, disk)
            try:
                g.boot()
                out = g.command(f'/usr/glenda/ktest-{name} all', timeout=1800, name='ktest')
                if not re.search(r'^KTEST-DONE ok [1-9][0-9]* fail 0$', out, re.M):
                    raise RuntimeError('incomplete kernel tests')
                g.halt()
            finally:
                g.stop()
    tasks = []
    for phase in phases:
        for repeat in range(1, args.repeat + 1):
            for name, repo in repos.items():
                result = OUTPUT / name / f'{phase}-{repeat}' / 'result.json'
                if args.resume and result.exists():
                    previous = json.loads(result.read_text())
                    expected = hashlib.md5(b'durable\n').hexdigest() + '\t/usr/glenda/audit/durable'
                    if not previous.get('ok') and args.retry_failed:
                        original = result.parent
                        archive = BASE / 'failures' / OUTPUT.name / name / (original.name + '-' + str(time.time_ns()))
                        archive.parent.mkdir(parents=True, exist_ok=True)
                        original.rename(archive)
                        old = json.dumps(previous).replace(str(original), str(archive))
                        (archive / 'result.json').write_text(json.dumps(json.loads(old), indent=2) + '\n')
                        print('retry archived: ' + str(archive), flush=True)
                    else:
                        if not previous.get('ok') or previous.get('checksums') != [expected, expected]:
                            raise RuntimeError(f'invalid completed trial: {result}')
                        print('resume: ' + str(result), flush=True)
                        continue
                tasks.append((name, repo, templates[name], size, phase, repeat))
    if not 1 <= args.jobs <= 4: p.error('jobs must be 1..4')
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(trial, *task) for task in tasks]
        for f in futures: f.result()

if __name__ == '__main__':
    main()
