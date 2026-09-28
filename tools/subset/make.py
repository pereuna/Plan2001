#!/usr/bin/env python3
"""Write the install subset into the repo from build/subset (derive.py).

  TARGET=<target> make.py B REPO      (B: build/subset/<target>)

  REPO/subset/<target>/proto  9front proto (mkfs) of the runtime files, with
                       their modes and owners from the VM: the whole root of
                       the target's USB install medium (tools/subset/mkusb)
  REPO/subset/<target>/files  path, kind (runtime|source|excluded), how, why, md5
                       ('-' where nothing is copied: compiled binaries, and
                       the legacy left out)
  REPO/subset/9front/  the copied files, in their 9front paths, shared by
                       every target: every runtime file that is not a
                       compiled binary, and the sources the binaries are
                       built from.  A file no target's list names any more
                       is removed.
"""
import hashlib, os, re, shutil, sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import derive

# top-level directories of 9front's distproto that are not arch trees
TOP = ['adm', 'cfg', 'cron', 'lib', 'rc', 'mail', derive.ARCH, derive.T['loaderdir'], 'acme', 'sys', 'tmp', 'usr', 'dist']


def topattrs(b):
	"""Mode and owners of the top-level directories as 9front's distproto
	gives them.  They matter: /tmp is d555 there, so a booted medium cannot
	write /tmp and glenda's profile starts a ramfs - the installer's state
	(/tmp/copydone, ...) must not survive on the stick into the next install."""
	a = {}
	for line in open(os.path.join(b, 'vm/text/sys/lib/sysconfig/proto/distproto')):
		f = line.split()
		if line[:1] not in ('\t', ' ', '\n', '#') and len(f) >= 2 and '=' not in f[0]:
			a[f[0]] = '%s %s %s' % (f[1], f[2] if len(f) > 2 else 'sys', f[3] if len(f) > 3 else 'sys')
	return a


def pmode(x):
	"""walk's permission string (d-rwxrwxr-x) -> proto mode (d775)."""
	bits = x[2:11]
	n = 0
	for i, c in enumerate(bits):
		if c != '-':
			n |= 1 << (8 - i)
	pre = 'd' if x[0] == 'd' else ('a' if x[1] == 'a' else 'l' if x[1] == 'l' else '')
	return '%s%o' % (pre, n)


def binary(path):
	with open(path, 'rb') as f:
		return b'\0' in f.read(65536)


def main(b, repo):
	tree, have = derive.runtime(b)
	stree, need, _ = derive.sources(b)
	for p, why in stree.excluded.items():
		tree.excluded.setdefault(p, why)
	out = os.path.join(repo, 'subset', derive.TARGET)
	cp = os.path.join(repo, 'subset', '9front')
	os.makedirs(out, exist_ok=True)
	os.makedirs(cp, exist_ok=True)

	# proto: a tree of the runtime paths plus the top-level directories
	paths = sorted(have) + ['/dist/9front/']
	root = {}
	for t in TOP:
		root.setdefault(t, {})
	for p in paths:
		node = root
		for e in p.strip('/').split('/'):
			node = node.setdefault(e, {})
	lines = []
	top = topattrs(b)

	def emit(node, prefix, depth):
		for name in sorted(node):
			p = prefix + '/' + name
			a = tree.attr.get(p)
			if depth == 0 and name in top:
				lines.append('%s\t%s' % (name, top[name]))
			elif a:
				lines.append('\t' * depth + '%s\t%s %s %s' % (name, pmode(a[0]), a[1], a[2]))
			else:
				lines.append('\t' * depth + '%s\td775 sys sys' % name)
			emit(node[name], p, depth + 1)
	emit(root, '', 0)
	with open(os.path.join(out, 'proto'), 'w') as f:
		f.write('# 9front install subset (tools/subset); generated, do not edit\n')
		f.write('\n'.join(lines) + '\n')

	# the copy and the file list
	src = os.path.join(b, 'src')
	rows = []

	def copy(p):
		s = src + p
		if not os.path.isfile(s) or binary(s):
			return '-'
		d = cp + p
		os.makedirs(os.path.dirname(d), exist_ok=True)
		shutil.copy2(s, d)
		return hashlib.md5(open(d, 'rb').read()).hexdigest()

	for p in sorted(have):
		rows.append((p, 'runtime', have[p][0], have[p][1], copy(p)))
	listed = set()
	for p in sorted(need):
		how, why = need[p]
		if how in ('runtime',):
			continue
		for q in (sorted(tree.under(p)) if p in tree.dirs else [p]):
			if q in have or q in listed:
				continue
			listed.add(q)
			m = copy(q)
			if m != '-' or how == 'unresolved':
				rows.append((q, 'source', how, why, m))
	for p in sorted(tree.excluded):
		rows.append((p, 'excluded', 'legacy', tree.excluded[p], '-'))
	with open(os.path.join(out, 'files'), 'w') as f:
		f.write('# path\tkind\thow\twhy\tmd5 (tools/subset; generated, do not edit)\n')
		for r in rows:
			f.write('\t'.join(r) + '\n')
	# the shared copy holds what some target's list names, nothing else
	keep = set()
	for d in os.listdir(os.path.join(repo, 'subset')):
		f = os.path.join(repo, 'subset', d, 'files')
		if d != '9front' and os.path.isfile(f):
			for l in open(f):
				r = l.rstrip('\n').split('\t')
				if not l.startswith('#') and len(r) == 5 and r[4] != '-':
					keep.add(r[0])
	gone = 0
	for dirpath, dirs, files in os.walk(cp, topdown=False):
		for fn in files:
			p = os.path.join(dirpath, fn)[len(cp):]
			if p not in keep:
				os.remove(cp + p)
				gone += 1
		if dirpath != cp and not os.listdir(dirpath):
			os.rmdir(dirpath)
	n = sum(1 for r in rows if r[4] != '-')
	print('make: %s: %d proto entries, %d files listed, %d copied to %s, %d stale removed' % (derive.TARGET, len(lines), len(rows), n, cp, gone))


if __name__ == '__main__':
	main(sys.argv[1], sys.argv[2])
