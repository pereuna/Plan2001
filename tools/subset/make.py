#!/usr/bin/env python3
"""Write the install subset into the repo from build/subset (derive.py).

  make.py B REPO

  REPO/subset/proto    9front proto (mkfs/mk9660) of the runtime files, with
                       their modes and owners from the VM; mkiso.rc adds
                       9bootproto (loader files, cfg/plan9.ini) as 9front does
  REPO/subset/files    path, kind (runtime|source), how, why, md5 ('-' where
                       nothing is copied: compiled binaries)
  REPO/subset/9front/  the copied files, in their 9front paths: every
                       runtime file that is not a compiled binary, and the
                       sources the binaries are built from
"""
import hashlib, os, re, shutil, sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import derive

# the loader files and plan9.ini: 9bootproto, added by mkiso.rc
BOOTPROTO = re.compile(r'^/386/[^/]+$|^/cfg/plan9\.ini$')
# top-level directories of 9front's distproto that are not arch trees
TOP = ['adm', 'cfg', 'cron', 'lib', 'rc', 'mail', '386', 'amd64', 'acme', 'sys', 'tmp', 'usr', 'dist']


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
	_, need, _ = derive.sources(b)
	out = os.path.join(repo, 'subset')
	cp = os.path.join(out, '9front')
	shutil.rmtree(cp, ignore_errors=True)
	os.makedirs(cp)

	# proto: a tree of the runtime paths plus the top-level directories
	paths = sorted(p for p in have if not BOOTPROTO.match(p)) + ['/dist/9front/']
	root = {}
	for t in TOP:
		root.setdefault(t, {})
	for p in paths:
		node = root
		for e in p.strip('/').split('/'):
			node = node.setdefault(e, {})
	lines = []

	def emit(node, prefix, depth):
		for name in sorted(node):
			p = prefix + '/' + name
			a = tree.attr.get(p)
			if a:
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
	with open(os.path.join(out, 'files'), 'w') as f:
		f.write('# path\tkind\thow\twhy\tmd5 (tools/subset; generated, do not edit)\n')
		for r in rows:
			f.write('\t'.join(r) + '\n')
	n = sum(1 for r in rows if r[4] != '-')
	print('make: %d proto entries, %d files listed, %d copied to %s' % (len(lines), len(rows), n, cp))


if __name__ == '__main__':
	main(sys.argv[1], sys.argv[2])
