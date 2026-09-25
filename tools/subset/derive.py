#!/usr/bin/env python3
"""Work out the install subset from what tools/subset/derive fetched.

  derive.py runtime B   -> B/runtime.tsv           (path, how, why)
  derive.py want B      -> prints the source paths still to fetch into B/src
  derive.py sources B   -> B/sources.tsv           (path, how, why)

B is build/subset: trace/{s1,s2} (tools/subset/trace), vm/tree and
vm/text/ (tree.rc), src/ (fetch.rc).  Paths are absolute 9front paths.

runtime: files a 9front install uses when it runs -
  boot     the kernel's bootdir and bootfs.proto, the loader files of
           9bootproto (what the ISO boots with)
  dynamic  read or run during a real install (trace s1/s2: atime on cwfs)
  static   named by an rc script of the subset (inst/*, termrc, ...),
           followed script to script
  iterative  found missing by tools/subset/test; tools/subset/extra
           lists them with the reason
sources: what those binaries are built from - their /sys/src/cmd source,
the mkfiles and mk templates, and the #include/#pragma lib closure over
/sys/include and /sys/src/lib*; the kernel and loader source directories.
"""
import os, re, sys

ARCH = 'amd64'
BIN = ['/%s/bin' % ARCH, '/rc/bin']
# ours, not 9front's: the trace helpers
IGNORE = re.compile(r'^/usr/glenda/(trace/|tmp/|snap\.rc$|export\.rc$)'
	# 9front's git repository (130 MB): the installer only checks that the
	# directory /dist/9front exists (copydist, mountdist's havedist), and
	# subset/proto makes it
	r'|^/dist/')
# rc words that are not commands
RCWORDS = set('if not for in while switch case fn eval exec exit shift cd builtin . ~ ! @ wait whatis rfork flag status'.split())
KERNELDIRS = ['/sys/src/9/port', '/sys/src/9/pc', '/sys/src/9/pc64', '/sys/src/9/ip',
	'/sys/src/9/boot', '/sys/src/boot/efi', '/sys/src/boot/pc', '/sys/src/boot/iso']
SRCMAP = {	# binary -> source, where the name alone does not say it
	'cwfs64x': '/sys/src/cmd/cwfs',
	'9660srv': '/sys/src/cmd/9660srv',
	'rc': '/sys/src/cmd/rc',
	'paqfs': '/sys/src/cmd/paqfs',
	'edisk': '/sys/src/cmd/disk/prep',
	'fdisk': '/sys/src/cmd/disk/prep',
	'plumber': '/sys/src/cmd/plumb',
}


class Tree:
	def __init__(self, b):
		self.files, self.dirs, self.attr = {}, set(), {}
		for line in open(os.path.join(b, 'vm/tree'), encoding='latin1'):
			f = line.rstrip('\n').split(' ', 4)
			if len(f) != 5:
				continue
			mode, uid, gid, size, path = f
			self.attr[path] = (mode, uid, gid)
			if mode.startswith('d'):
				self.dirs.add(path)
			else:
				self.files[path] = int(size)
		self.text = os.path.join(b, 'vm/text')

	def under(self, d):
		d = d.rstrip('/') + '/'
		return [p for p in self.files if p.startswith(d)]

	def script(self, path):
		"""Text of an rc script, or None."""
		p = self.text + path
		if not os.path.isfile(p):
			return None
		t = open(p, 'rb').read()
		if b'\0' in t:
			return None
		return t.decode('latin1')


def resolve(tree, name, cwd=None):
	if name.startswith('/'):
		return name if name in tree.files else None
	if name.startswith('./') and cwd:
		p = os.path.normpath(os.path.join(cwd, name))
		return p if p in tree.files else None
	for d in BIN:
		p = d + '/' + name
		if p in tree.files:
			return p
	return None


def rccommands(text):
	"""Command names and absolute paths an rc script mentions (roughly)."""
	cmds, paths = set(), set()
	text = re.sub(r'test\s+-d\s+\S+', ' ', text)	# only needs a directory, no files
	for line in text.split('\n'):
		line = re.sub(r"'[^']*'", ' ', line.split('#')[0] if not line.lstrip().startswith("'") else line)
		for m in re.finditer(r'(?:^|[;&|{}()`!@=]|\bif\s*\(|\bnot\b|\bwhile\s*\()\s*([A-Za-z0-9_./+-][A-Za-z0-9_./+-]*)', line):
			w = m.group(1)
			if w not in RCWORDS and not w.startswith('$'):
				cmds.add(w)
		line = re.sub(r'\$(objtype|cputype)\b', ARCH, line)
		for m in re.finditer(r'(?<![A-Za-z0-9_$])(/[A-Za-z0-9_.+-]+(?:/[A-Za-z0-9_.+-]+)+)', line):
			paths.add(m.group(1))
	return cmds, paths


def runtime(b):
	tree = Tree(b)
	have = {}	# path -> (how, why)

	def add(p, how, why):
		if p and p in tree.files and not IGNORE.match(p) and p not in have:
			have[p] = (how, why)
			return True
		return False

	# boot: kernel bootdir, bootfs.proto, loader files
	for p in ['/%s/9pc64' % ARCH, '/%s/bin/paqfs' % ARCH, '/%s/bin/auth/factotum' % ARCH, '/cfg/plan9.ini']:
		add(p, 'boot', 'kernel bootdir / ISO boot')
	for p in tree.under('/386'):
		if os.path.dirname(p) == '/386':
			add(p, 'boot', '9bootproto')
	stack, out = [], []
	for line in open(tree.text + '/sys/src/9/boot/bootfs.proto', encoding='latin1'):
		if not line.strip():
			continue
		depth = len(line) - len(line.lstrip('\t'))
		f = line.split()
		name = f[0].replace('$objtype', ARCH)
		del stack[depth:]
		stack.append(name)
		src = f[4] if len(f) > 4 else None
		p = '/' + '/'.join(stack)
		if src:
			p = os.path.normpath('/sys/src/9/boot/' + src) if not src.startswith('/') else src
		if name == '+':
			for q in tree.under('/' + '/'.join(stack[:-1])):
				add(q, 'boot', 'bootfs.proto')
		else:
			add(p, 'boot', 'bootfs.proto')

	# dynamic
	for s, what in (('s1', 'boot + installer up to copydist'), ('s2', 'installer after copydist')):
		for line in open(os.path.join(b, 'trace', s), encoding='latin1'):
			add(line.strip(), 'dynamic', what)

	# iterative: what tools/subset/test found missing
	for line in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'extra')):
		if line.strip() and not line.startswith('#'):
			p, why = line.rstrip('\n').split('\t', 1)
			add(p, 'iterative', why)

	# static: every rc script in the set, followed
	for p in tree.under('/rc/bin/inst'):
		add(p, 'static', 'installer')
	done = set()
	while True:
		todo = [p for p in have if p not in done and tree.script(p) is not None]
		if not todo:
			break
		for p in todo:
			done.add(p)
			cmds, paths = rccommands(tree.script(p))
			for c in cmds:
				add(resolve(tree, c, os.path.dirname(p)), 'static', 'run by ' + p)
			for q in paths:
				# the installer names the media and the new disk by their
				# mounts; a directory it looks into on the new disk (tzsetup:
				# /n/newfs/adm/timezone) must come with its files
				newfs = q.startswith('/n/newfs/')
				q = re.sub(r'^/n/(newfs|dist)(?=/)', '', q)
				if newfs and q in tree.dirs:
					for f in tree.under(q):
						add(f, 'static', 'in %s, named in %s' % (q, p))
				else:
					add(resolve(tree, q), 'static', 'named in ' + p)
	return tree, have


def cmd_runtime(b):
	tree, have = runtime(b)
	with open(os.path.join(b, 'runtime.tsv'), 'w') as f:
		for p in sorted(have):
			f.write('%s\t%s\t%s\n' % (p, have[p][0], have[p][1]))
	n = {}
	for how, _ in have.values():
		n[how] = n.get(how, 0) + 1
	print('runtime: %d files %s' % (len(have), n))


def binsource(tree, p):
	"""Source (file or directory) of the binary p, or None."""
	for d in BIN[:1]:
		if p.startswith(d + '/'):
			rel = p[len(d) + 1:]
			break
	else:
		return None
	base = os.path.basename(rel)
	if base in SRCMAP:
		return SRCMAP[base]
	for c in ('/sys/src/cmd/%s' % rel, '/sys/src/cmd/%s.c' % rel, '/sys/src/cmd/%s.y' % rel):
		if c in tree.dirs or c in tree.files:
			return c
	return None


def sources(b):
	tree, have = runtime(b)
	src = os.path.join(b, 'src')
	need = {}

	def add(p, how, why):
		if p not in need:
			need[p] = (how, why)

	for p, (how, why) in sorted(have.items()):
		if p.startswith(BIN[0] + '/'):
			s = binsource(tree, p)
			if s:
				add(s, 'source', 'builds ' + p)
			else:
				add(p, 'unresolved', 'no source found for ' + p)
		elif tree.script(p) is not None or not p.startswith('/%s/' % ARCH) and not p.startswith('/386/'):
			add(p, 'runtime', how + ': ' + why)
	for d in KERNELDIRS:
		if d in tree.dirs:
			add(d, 'source', 'kernel / loader')
	add('/sys/src/9/mkfile', 'source', 'kernel')

	# closure over what has been fetched so far
	def expand(p):
		if p in tree.dirs:
			return [q for q in tree.under(p) if not re.search(r'\.(6|8|5|7|a|out)$|/[0-9a-z]\.out$', q)]
		return [p] if p in tree.files else []

	seen = set()
	changed = True
	while changed:
		changed = False
		for p in list(need):
			if need[p][0] == 'unresolved':
				continue
			for q in expand(p):
				if q in seen:
					continue
				seen.add(q)
				local = src + q
				if not os.path.isfile(local):
					continue
				text = open(local, 'rb').read().decode('latin1')
				d = os.path.dirname(q)
				if q.endswith(('.c', '.h', '.y', '.s')):
					for m in re.finditer(r'#\s*include\s*([<"])([^>"]+)[>"]', text):
						if m.group(1) == '"':
							c = [os.path.normpath(os.path.join(d, m.group(2)))]
						else:
							c = ['/sys/include/' + m.group(2), '/%s/include/%s' % (ARCH, m.group(2))]
						for x in c:
							if x in tree.files and x not in need:
								add(x, 'source', 'included by ' + q); changed = True
					for m in re.finditer(r'#\s*pragma\s+lib\s+"([^"]+)"', text):
						lib = re.sub(r'^.*/', '', m.group(1)).replace('.a', '')
						x = '/sys/src/' + lib
						if x in tree.dirs and x not in need:
							add(x, 'source', 'linked by ' + q); changed = True
				if os.path.basename(q) == 'mkfile' or q.endswith(('/mkone', '/mkmany', '/mksyslib', '/mkfile.proto')):
					for m in re.finditer(r'^<\s*(\S+)', text, re.M):
						x = m.group(1).replace('$objtype', ARCH)
						x = x if x.startswith('/') else os.path.normpath(os.path.join(d, x))
						if x in tree.files and x not in need:
							add(x, 'source', 'mk template of ' + q); changed = True
					for m in re.finditer(r'/\$objtype/lib/(lib\w+)\.a', text):
						x = '/sys/src/' + m.group(1)
						if x in tree.dirs and x not in need:
							add(x, 'source', 'LIB of ' + q); changed = True
				# a single-file command is built by its directory's mkfile
				if need.get(p, ('',))[0] == 'source' and p == q and q.startswith('/sys/src/cmd/') and q.endswith(('.c', '.y')):
					x = d + '/mkfile'
					if x in tree.files and x not in need:
						add(x, 'source', 'builds ' + q); changed = True
	return tree, need, seen


def cmd_want(b):
	tree, need, seen = sources(b)
	src = os.path.join(b, 'src')
	for p in sorted(need):
		if need[p][0] == 'unresolved':
			continue
		missing = [q for q in (tree.under(p) if p in tree.dirs else [p]) if not os.path.exists(src + q)]
		if missing:
			print(p.lstrip('/'))


def cmd_sources(b):
	tree, need, seen = sources(b)
	with open(os.path.join(b, 'sources.tsv'), 'w') as f:
		for p in sorted(need):
			f.write('%s\t%s\t%s\n' % (p, need[p][0], need[p][1]))
	un = [p for p in need if need[p][0] == 'unresolved']
	print('sources: %d entries, %d unresolved%s' % (len(need), len(un), (': ' + ' '.join(sorted(un))) if un else ''))


if __name__ == '__main__':
	{'runtime': cmd_runtime, 'want': cmd_want, 'sources': cmd_sources}[sys.argv[1]](sys.argv[2])
