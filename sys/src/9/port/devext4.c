/*
 * #E: ext4 in the kernel.  lwext4 (../ext4, 9front's ext4srv without its
 * 9P server) on a block device file, so that the root file system needs
 * no file server process.  #E/ctl takes
 *
 *	mount root /dev/sdE0/linux
 *	mount -w root UUID=1b2c...
 *	mount root LABEL=plan2001 /dev/sdE0/data ...
 *
 * the device itself, or the first of the candidates that has an ext4
 * superblock with that uuid or volume label.  With no candidates (and
 * for PARTUUID=, a GPT partition's) the disks of #S are looked through:
 * the whole disk and each partition of its GPT, read from its data
 * file, so no partitions need be set up.  rootmount does that for
 * plan9.ini's root=, before the first process: the root with no boot
 * file system and no file server (docs/init.md).  #E/root (or #Eroot)
 * is then the file system.  DEVICE!/dir (or UUID=uuid!/dir) makes a
 * directory in it the root: .. and absolute links stay in it, so a
 * Plan 9 tree can live in a Linux file system.  The device is opened in
 * the writer's name space, so it may be a partition of #S or a file
 * served by a process (nusb/disk).  umount NAME lets it go when nothing
 * uses it.  A file system is mounted once if written (by its uuid).
 *
 * Read-only unless -w.  Written, as ext4srv: lwext4's journal (replayed
 * at mount) and its block cache in write-back; dirty blocks go to the
 * device at sync NAME (or sync) and every Syncms, then the disk's own
 * write cache is flushed (SCSI synchronize cache through #S's raw file).
 * halt [NAME] (as ext4srv's, umount and the kernel's shutdown too)
 * leaves it clean: flushed, the journal stopped, the superblock
 * VALID_FS; it is read-only then.  The root's ctl is /srv/ext4.root.cmd
 * too, which 9front's fshalt halts (and waits to see gone).
 *
 * Files are by path (lwext4's interface): a Chan whose path names
 * another inode since (renamed or removed meanwhile) is an error.
 *
 * Symbolic links are followed (an absolute one from the file system's
 * root); other special files are not shown.  Owners are names from
 * etc/group (as ext4srv), numbers otherwise.
 */
#include "../ext4/ext4_config.h"
#include "../port/error.h"
#include "ext4.h"
#include "ext4_inode.h"
#include "ext4_super.h"
#include "ext4_fs.h"

typedef struct Ext Ext;
typedef struct Xfile Xfile;
typedef struct Xuser Xuser;

enum {
	Qtop	= 0,		/* #E: ctl and the file systems */
	Qctl,

	Maxlink	= 8,		/* symbolic links followed in one walk */
	Maxname	= 255,		/* of a name or a link's target */
	Maxread	= 64*1024,	/* bounce buffer for a read or a write */
	Growchunk	= 1024*1024,	/* zeros written with fs locked, at most */
	Sectsize	= 512,		/* lwext4's unit for the device */
	Syncms	= 5000,		/* dirty blocks to the device */
	Nvers	= 256,		/* write counts, by inode, for qid.vers */
};

struct Xuser {
	u32int	id;
	char	*name;
};

struct Ext {
	Ref;			/* Xfiles on it, and the list */
	QLock;			/* lwext4 is not reentrant */
	Ext	*next;
	int	id;		/* qid.path>>32 of its files */
	char	name[KNAMELEN];
	char	*dev;		/* as it was given */
	Chan	*c;
	char	uuid[37];
	char	label[17];
	char	*base;		/* the root, canonical: "" or a directory in it */
	Qid	rootqid;
	Xuser	*users;
	int	nusers;
	char	*userbuf;
	struct ext4_mountpoint mp;
	struct ext4_blockdev bd;
	struct ext4_blockdev_iface bdif;
	struct ext4_sblock *sb;
	int	rw;
	ulong	vers[Nvers];	/* writes, hashed by inode: qid.vers is mtime+this */
	Lock	xlk;
	Xfile	*xfiles;	/* all on it: a removed inode's are made stale */
	char	*raw;		/* the disk's raw file, for its cache flush */
	int	flushwarned;
	ulong	nflush;		/* cache flushes done */
	uchar	sect[Sectsize];
};

struct Xfile {
	Ext	*fs;
	Xfile	*xnext;		/* fs->xfiles */
	u32int	ino;
	int	stale;		/* its inode was removed: errors from now on */
	char	*path;		/* in fs, "" its root */
	ext4_file *file;
	ext4_dir *dir;
	int	doff;		/* entries of dir read */
	int	elast;		/* ename is entry doff-1 */
	int	rclose;		/* remove at close (ORCLOSE) */
	char	ename[Maxname+1];	/* the last entry's name */
	char	uidbuf[12], gidbuf[12];
};

static struct {
	QLock;
	Ext	*list;
	int	id;
	int	syncing;	/* the sync kproc runs */
	Rendez;
} exts;

static uchar zeros[8192];
static ulong kstackmax;	/* deepest kernel stack seen at the device */
static char Estale[] = "ext4: file removed";

#define FSID(q)	((int)((q).path>>32))
#define INO(q)	((u32int)(q).path)

static void
ext4error(char *what)
{
	char buf[ERRMAX];

	if(up->errstr[0] == 0)
		snprint(buf, sizeof buf, "ext4: %s failed", what);
	else
		snprint(buf, sizeof buf, "ext4: %s: %s", what, up->errstr);
	error(buf);
}

/*
 * The kernel stack in use when lwext4 calls the device (the device's own
 * below that): KSTACK was raised to 16K for lwext4 (pc64/mem.h); #E/ctl
 * shows the deepest seen.  The stack is just below the Proc.
 */
static void
stackmark(void)
{
	ulong used;

	if(up == nil)
		return;
	used = (uintptr)up - (uintptr)&used;
	if(used < KSTACK && used > kstackmax)
		kstackmax = used;
}

/*
 * The block device.  Errors in the device's read are lwext4's -1,
 * which ext4error makes an error again.
 */
static int
bdopen(struct ext4_blockdev *)
{
	return 0;
}

static int
bdclose(struct ext4_blockdev *)
{
	return 0;
}

static int
bdread(struct ext4_blockdev *bd, void *buf, u64int blk, u32int cnt)
{
	Ext *fs;
	uchar *p;
	long n, m;
	vlong off;

	fs = bd->bdif->p_user;
	stackmark();
	p = buf;
	n = cnt*Sectsize;
	off = blk*Sectsize;
	if(waserror()){
		werrstr("%s: %r", fs->dev);
		return -1;
	}
	while(n > 0){
		m = devtab[fs->c->type]->read(fs->c, p, n, off);
		if(m <= 0)
			error("short read");
		p += m;
		off += m;
		n -= m;
	}
	poperror();
	return 0;
}

static int
bdwrite(struct ext4_blockdev *bd, const void *buf, u64int blk, u32int cnt)
{
	Ext *fs;
	uchar *p;
	long n, m;
	vlong off;

	fs = bd->bdif->p_user;
	stackmark();
	if(!fs->rw){
		werrstr(Erdonlyfs);
		return -1;
	}
	p = (uchar*)buf;
	n = cnt*Sectsize;
	off = blk*Sectsize;
	if(waserror()){
		werrstr("%s: %r", fs->dev);
		return -1;
	}
	while(n > 0){
		m = devtab[fs->c->type]->write(fs->c, p, n, off);
		if(m <= 0)
			error("short write");
		p += m;
		off += m;
		n -= m;
	}
	poperror();
	return 0;
}

static Ext*
fsget(int id)
{
	Ext *fs;

	qlock(&exts);
	for(fs = exts.list; fs != nil; fs = fs->next)
		if(fs->id == id){
			incref(fs);
			break;
		}
	qunlock(&exts);
	return fs;
}

static void
fsput(Ext *fs)
{
	if(decref(fs) == 0)
		panic("ext4: %s: last reference", fs->name);
}

static Xfile*
newxfile(Ext *fs, char *path, u32int ino)
{
	Xfile *x;

	x = smalloc(sizeof *x);
	x->fs = fs;
	x->ino = ino;
	x->path = smalloc(strlen(path)+1);
	strcpy(x->path, path);
	incref(fs);
	lock(&fs->xlk);
	x->xnext = fs->xfiles;
	fs->xfiles = x;
	unlock(&fs->xlk);
	return x;
}

/*
 * ino was removed: the other Xfiles of it are stale, before the inode
 * (and its blocks) can be another file's.  lwext4 frees it at once, as
 * Plan 9's file servers do; reads and writes through them are errors.
 */
static void
staleino(Ext *fs, u32int ino, Xfile *self)
{
	Xfile *x;

	lock(&fs->xlk);
	for(x = fs->xfiles; x != nil; x = x->xnext)
		if(x->ino == ino && x != self)
			x->stale = 1;
	unlock(&fs->xlk);
}

/* lwext4's file and dir: closed with fs locked */
static void
freexfile(Xfile *x)
{
	Xfile **l;
	Ext *fs;

	if(x == nil)
		return;
	fs = x->fs;
	lock(&fs->xlk);
	for(l = &fs->xfiles; *l != nil; l = &(*l)->xnext)
		if(*l == x){
			*l = x->xnext;
			break;
		}
	unlock(&fs->xlk);
	if(x->file != nil || x->dir != nil){
		qlock(fs);
		if(x->file != nil)
			ext4_fclose(x->file);
		if(x->dir != nil)
			ext4_dir_close(x->dir);
		qunlock(fs);
	}
	free(x->file);
	free(x->dir);
	free(x->path);
	free(x);
	fsput(fs);
}

static char*
uname(Ext *fs, u32int id, char *buf)
{
	int i;

	for(i = 0; i < fs->nusers; i++)
		if(fs->users[i].id == id)
			return fs->users[i].name;
	snprint(buf, 12, "%ud", id);
	return buf;
}

/* name's id in etc/group, or ~0 */
static u32int
userid(Ext *fs, char *name)
{
	int i;

	for(i = 0; i < fs->nusers; i++)
		if(strcmp(fs->users[i].name, name) == 0)
			return fs->users[i].id;
	return ~0;
}

/*
 * The inode at x's path, which must still be c's (not renamed or
 * removed and made anew meanwhile).  With fs locked.
 */
static void
xinode(Chan *c, Xfile *x, u32int *ino, struct ext4_inode *inode)
{
	if(x->stale)
		error(Estale);
	up->errstr[0] = 0;
	if(ext4_raw_inode_fill(&x->fs->mp, x->path, ino, inode) < 0)
		ext4error(x->path);
	if(*ino != INO(c->qid))
		error("ext4: file renamed or removed");
}

/* x's file's size from its inode: another Chan may have written it */
static void
syncsize(Xfile *x)
{
	struct ext4_inode_ref ref;
	Ext *fs;

	fs = x->fs;
	up->errstr[0] = 0;
	if(ext4_fs_get_inode_ref(&fs->mp.fs, x->file->inode, &ref) != 0)
		ext4error(x->path);
	x->file->fsize = ext4_inode_get_size(&fs->mp.fs.sb, ref.inode);
	ext4_fs_put_inode_ref(&ref);
}

static void
written(Ext *fs, u32int ino)
{
	fs->vers[ino % Nvers]++;
}

static void
needrw(Ext *fs)
{
	if(!fs->rw)
		error(Erdonlyfs);
}

static char*
join(char *dir, char *name)
{
	char *s;
	int n;

	n = strlen(dir);
	s = smalloc(n+1+strlen(name)+1);
	strcpy(s, dir);
	if(n > 0)
		s[n++] = '/';
	strcpy(s+n, name);
	return s;
}

/* dir's parent, in place */
static void
parent(char *dir)
{
	char *s;

	if((s = strrchr(dir, '/')) != nil)
		*s = 0;
	else
		*dir = 0;
}

static void walkpath(Ext*, char**, char*, struct ext4_inode*, u32int*, int);

/*
 * name in the directory *path (canonical: no links in it), which
 * becomes the canonical path of what name is: links followed.
 * With fs locked; error() on failure.
 */
static void
walkname(Ext *fs, char **path, char *name, struct ext4_inode *inode, u32int *ino, int depth)
{
	char *s, *link;
	usize n;

	if(strcmp(name, ".") == 0 || *name == 0){
		up->errstr[0] = 0;
		if(ext4_raw_inode_fill(&fs->mp, *path, ino, inode) < 0)
			ext4error(*path);
		return;
	}
	if(strcmp(name, "..") == 0){
		if(strcmp(*path, fs->base) != 0)
			parent(*path);
		up->errstr[0] = 0;
		if(ext4_raw_inode_fill(&fs->mp, *path, ino, inode) < 0)
			ext4error(*path);
		return;
	}
	s = join(*path, name);
	if(waserror()){
		free(s);
		nexterror();
	}
	up->errstr[0] = 0;
	if(ext4_raw_inode_fill(&fs->mp, s, ino, inode) < 0)
		error(Enonexist);
	if(ext4_inode_type(fs->sb, inode) != EXT4_INODE_MODE_SOFTLINK){
		poperror();
		free(*path);
		*path = s;
		return;
	}
	if(depth >= Maxlink)
		error("ext4: too many symbolic links");
	/* on the heap: walks of links nest, on a kernel stack */
	link = smalloc(Maxname+1);
	if(waserror()){
		free(link);
		nexterror();
	}
	n = 0;
	up->errstr[0] = 0;
	if(ext4_readlink(&fs->mp, s, link, Maxname, &n) < 0)
		ext4error(s);
	link[n] = 0;
	if(n == 0)
		error(Enonexist);
	/* from the directory the link is in, or the root */
	if(link[0] == '/'){
		free(*path);
		*path = smalloc(strlen(fs->base)+1);
		strcpy(*path, fs->base);
	}
	walkpath(fs, path, link, inode, ino, depth+1);
	poperror();
	free(link);
	poperror();
	free(s);
}

/* each name of the slash-separated p from *path */
static void
walkpath(Ext *fs, char **path, char *p, struct ext4_inode *inode, u32int *ino, int depth)
{
	char *e, *s, *name;
	int n, walked;

	name = smalloc(Maxname+1);
	if(waserror()){
		free(name);
		nexterror();
	}
	s = p;
	walked = 0;
	for(;;){
		while(*s == '/')
			s++;
		if(*s == 0)
			break;
		if((e = strchr(s, '/')) == nil)
			e = s + strlen(s);
		n = e - s;
		if(n > Maxname)
			error(Etoolong);
		memmove(name, s, n);
		name[n] = 0;
		walkname(fs, path, name, inode, ino, depth);
		walked = 1;
		if(*e != 0 && ext4_inode_type(fs->sb, inode) != EXT4_INODE_MODE_DIRECTORY)
			error(Enotdir);
		s = e;
	}
	if(!walked){	/* "" or "/": the start itself */
		up->errstr[0] = 0;
		if(ext4_raw_inode_fill(&fs->mp, *path, ino, inode) < 0)
			ext4error(*path);
	}
	poperror();
	free(name);
}

static Qid
inodeqid(Ext *fs, u32int ino, struct ext4_inode *inode)
{
	Qid q;

	q.path = (uvlong)fs->id<<32 | ino;
	q.vers = ext4_inode_get_modif_time(inode) + fs->vers[ino % Nvers];
	q.type = QTFILE;
	if(ext4_inode_type(fs->sb, inode) == EXT4_INODE_MODE_DIRECTORY)
		q.type = QTDIR;
	if(ext4_inode_get_flags(inode) & EXT4_INODE_FLAG_APPEND)
		q.type |= QTAPPEND;
	return q;
}

static void
inodedir(Chan *c, Xfile *x, char *name, u32int ino, struct ext4_inode *inode, Dir *d)
{
	Ext *fs;
	Qid q;
	ulong perm;

	fs = x->fs;
	q = inodeqid(fs, ino, inode);
	perm = ext4_inode_get_mode(fs->sb, inode) & 0777;
	if(q.type & QTDIR)
		perm |= DMDIR;
	if(q.type & QTAPPEND)
		perm |= DMAPPEND;
	devdir(c, q, name, 0, eve, perm, d);
	if(!(q.type & QTDIR))
		d->length = ext4_inode_get_size(fs->sb, inode);
	d->uid = uname(fs, ext4_inode_get_uid(inode), x->uidbuf);
	d->gid = uname(fs, ext4_inode_get_gid(inode), x->gidbuf);
	d->muid = d->uid;
	d->atime = ext4_inode_get_access_time(inode);
	d->mtime = ext4_inode_get_modif_time(inode);
}

/* "1b2c..." from the superblock's bytes */
static void
uuidstr(char *s, uchar *u)
{
	snprint(s, 37, "%.2ux%.2ux%.2ux%.2ux-%.2ux%.2ux-%.2ux%.2ux-%.2ux%.2ux-%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux",
		u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
		u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
}

/* "1b2c..." from a GPT GUID: its first three fields little-endian */
static void
guidstr(char *s, uchar *u)
{
	snprint(s, 37, "%.2ux%.2ux%.2ux%.2ux-%.2ux%.2ux-%.2ux%.2ux-%.2ux%.2ux-%.2ux%.2ux%.2ux%.2ux%.2ux%.2ux",
		u[3], u[2], u[1], u[0], u[5], u[4], u[7], u[6],
		u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
}

/*
 * Has c, at off, an ext4 superblock with uuid (or label)?  Any failure
 * is no.
 */
static int
probesb(Chan *c, vlong off, char *uuid, char *label)
{
	uchar sb[256];
	char s[37];

	if(waserror())
		return 0;
	if(devtab[c->type]->read(c, sb, sizeof sb, off+EXT4_SUPERBLOCK_OFFSET) != sizeof sb)
		error(Eio);
	poperror();
	if(sb[0x38] != 0x53 || sb[0x39] != 0xEF)
		return 0;
	if(uuid != nil){
		uuidstr(s, sb+0x68);
		return cistrcmp(s, uuid) == 0;
	}
	memmove(s, sb+0x78, 16);
	s[16] = 0;
	return strcmp(s, label) == 0;
}

/* c's ext4 uuid at off, in s; 0 if it has no ext4 superblock there */
static int
sbuuid(Chan *c, vlong off, char *s)
{
	uchar sb[256];

	if(waserror())
		return 0;
	if(devtab[c->type]->read(c, sb, sizeof sb, off+EXT4_SUPERBLOCK_OFFSET) != sizeof sb)
		error(Eio);
	poperror();
	if(sb[0x38] != 0x53 || sb[0x39] != 0xEF)
		return 0;
	uuidstr(s, sb+0x68);
	return 1;
}

/* Does the device dev (opened in our name space) have it? */
static int
probe(char *dev, char *uuid, char *label)
{
	Chan *c;
	int ok;

	if(waserror())
		return 0;
	c = namec(dev, Aopen, OREAD, 0);
	poperror();
	ok = 0;
	if(!(c->qid.type & QTDIR))
		ok = probesb(c, 0, uuid, label);
	cclose(c);
	return ok;
}

static uvlong
le(uchar *p, int n)
{
	uvlong v;

	v = 0;
	while(n-- > 0)
		v = v<<8 | p[n];
	return v;
}

/*
 * The disk c's GPT: its partitions' offsets, sizes and GUIDs, at most
 * max.  Logical blocks of 512 or 4096 bytes, as the header is found.
 */
typedef struct Gpart Gpart;
struct Gpart {
	vlong	off;
	vlong	size;
	char	guid[37];
};

static int
readgpt(Chan *c, Gpart *gp, int max)
{
	uchar h[92], *e, *ents;
	vlong lb, elba;
	ulong ne, esz;
	int i, n, bs;

	bs = 0;
	for(lb = 512; lb <= 4096; lb *= 8){
		if(devtab[c->type]->read(c, h, sizeof h, lb) == sizeof h && memcmp(h, "EFI PART", 8) == 0){
			bs = lb;
			break;
		}
	}
	if(bs == 0)
		return 0;
	elba = le(h+72, 8);
	ne = le(h+80, 4);
	esz = le(h+84, 4);
	if(esz < 128 || esz > 1024 || ne == 0 || ne > 1024)
		return 0;
	ents = smalloc(ne*esz);
	if(waserror()){
		free(ents);
		return 0;
	}
	if(devtab[c->type]->read(c, ents, ne*esz, elba*bs) != ne*esz)
		error(Eio);
	poperror();
	n = 0;
	for(i = 0; i < ne && n < max; i++){
		e = ents + i*esz;
		if(le(e, 8) == 0 && le(e+8, 8) == 0)	/* unused */
			continue;
		gp[n].off = le(e+32, 8)*bs;
		gp[n].size = (le(e+40, 8) - le(e+32, 8) + 1)*bs;
		guidstr(gp[n].guid, e+16);
		n++;
	}
	free(ents);
	return n;
}

/*
 * The disk (#S/sdXX/data), and where on it, of the file system that is
 * want: UUID=uuid, LABEL=label or PARTUUID=guid (a GPT partition's).
 * The whole disk, then each GPT partition.
 */
static int
scandisks(char *want, char *dev, int ndev, vlong *off, vlong *size)
{
	Chan *top, *c;
	uchar *buf;
	Gpart *gp;
	Dir d;
	char *uuid, *label, *partuuid, strs[256];
	vlong doff;
	long n, m;
	int i, np, found;

	uuid = label = partuuid = nil;
	if(cistrncmp(want, "UUID=", 5) == 0)
		uuid = want+5;
	else if(strncmp(want, "LABEL=", 6) == 0)
		label = want+6;
	else if(cistrncmp(want, "PARTUUID=", 9) == 0)
		partuuid = want+9;
	else
		return 0;
	found = 0;
	top = namec("#S", Aopen, OREAD, 0);
	buf = smalloc(8192);
	gp = smalloc(128*sizeof(Gpart));
	if(waserror()){
		cclose(top);
		free(buf);
		free(gp);
		nexterror();
	}
	doff = 0;
	while(!found && (n = devtab[top->type]->read(top, buf, 8192, doff)) > 0){
		doff += n;
		for(i = 0; !found && i < n; i += m){
			m = BIT16SZ + GBIT16(buf+i);
			if(m > 8192-i || m - STATFIXLEN > sizeof strs || convM2D(buf+i, m, &d, strs) == 0)
				break;
			if(strncmp(d.name, "sd", 2) != 0 || strcmp(d.name, "sdctl") == 0)
				continue;
			snprint(dev, ndev, "#S/%s/data", d.name);
			if(waserror())
				continue;
			c = namec(dev, Aopen, OREAD, 0);
			poperror();
			if(partuuid == nil && probesb(c, 0, uuid, label)){
				*off = 0;
				*size = 0;
				found = 1;
			}
			np = found ? 0 : readgpt(c, gp, 128);
			while(--np >= 0){
				if(partuuid != nil ? cistrcmp(gp[np].guid, partuuid) == 0
				: probesb(c, gp[np].off, uuid, label)){
					*off = gp[np].off;
					*size = gp[np].size;
					found = 1;
					break;
				}
			}
			cclose(c);
		}
	}
	poperror();
	cclose(top);
	free(buf);
	free(gp);
	return found;
}

/* scandisks for gefs's root (devgefs.c): PARTUUID= */
int
rootdisk(char *want, char *dev, int ndev, vlong *off, vlong *size)
{
	return scandisks(want, dev, ndev, off, size);
}

/* the owners' names, from the root's etc/group (name:x:id:...), as ext4srv */
static void
loadusers(Ext *fs)
{
	ext4_file f;
	usize n, got;
	char *s, *e, *a[4];
	int nl, r;

	s = join(fs->base, "etc/group");
	up->errstr[0] = 0;
	r = ext4_fopen2(&fs->mp, &f, s, O_RDONLY);
	free(s);
	if(r < 0)
		return;
	n = ext4_fsize(&f);
	if(n == 0 || n > 64*1024){
		ext4_fclose(&f);
		return;
	}
	fs->userbuf = smalloc(n+1);
	got = 0;
	if(ext4_fread(&f, fs->userbuf, n, &got) < 0)
		got = 0;
	ext4_fclose(&f);
	fs->userbuf[got] = 0;
	nl = 0;
	for(s = fs->userbuf; *s != 0; s++)
		if(*s == '\n')
			nl++;
	fs->users = smalloc((nl+1)*sizeof(Xuser));
	for(s = fs->userbuf; *s != 0; s = e){
		if((e = strchr(s, '\n')) != nil)
			*e++ = 0;
		else
			e = s + strlen(s);
		if(getfields(s, a, nelem(a), 0, ":") < 3 || *a[0] == 0 || *a[2] == 0)
			continue;
		fs->users[fs->nusers].name = a[0];
		fs->users[fs->nusers].id = strtoul(a[2], nil, 10);
		fs->nusers++;
	}
}

static void
fsfree(Ext *fs)
{
	if(fs->c != nil)
		cclose(fs->c);
	free(fs->users);
	free(fs->userbuf);
	free(fs->dev);
	free(fs->base);
	free(fs->raw);
	free(fs);
}

int gefsroot(char*, int);
static void syncproc(void*);
static void unpost(char*);
static char *rawfile(char*);
static void flushdev(Ext*);

static void
fsmount(char *name, char *dev, vlong off, vlong psize, char *dir, int rw)
{
	struct ext4_inode inode;
	u32int ino;
	Ext *fs, *f;
	Dir *d;
	vlong size;
	char *base, ebuf[ERRMAX];

	if(strlen(name) >= KNAMELEN || *name == 0 || strcmp(name, "ctl") == 0
	|| strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strchr(name, '/') != nil)
		error(Ebadarg);
	fs = smalloc(sizeof *fs);
	if(waserror()){
		fsfree(fs);
		nexterror();
	}
	strcpy(fs->name, name);
	if(off == 0 && psize == 0)
		kstrdup(&fs->dev, dev);
	else{
		fs->dev = smalloc(strlen(dev)+24);
		sprint(fs->dev, "%s@%lld", dev, off);
	}
	fs->rw = rw;
	fs->c = namec(dev, Aopen, rw ? ORDWR : OREAD, 0);
	if(fs->c->qid.type & QTDIR)
		error(Eisdir);
	d = dirchanstat(fs->c);
	size = d->length;
	free(d);
	if(psize == 0)
		psize = size - off;
	if(off < 0 || off % Sectsize || off+psize > size || psize < 2*EXT4_SUPERBLOCK_OFFSET)
		error("ext4: device too small");

	fs->bdif.open = bdopen;
	fs->bdif.bread = bdread;
	fs->bdif.bwrite = bdwrite;
	fs->bdif.close = bdclose;
	fs->bdif.ph_bsize = Sectsize;
	fs->bdif.ph_bcnt = size/Sectsize;
	fs->bdif.ph_bbuf = fs->sect;
	fs->bdif.p_user = fs;
	fs->bd.bdif = &fs->bdif;
	fs->bd.part_offset = off;
	fs->bd.part_size = psize/Sectsize*Sectsize;

	qlock(&exts);
	if(waserror()){
		qunlock(&exts);
		nexterror();
	}
	for(f = exts.list; f != nil; f = f->next)
		if(strcmp(f->name, name) == 0)
			error(Einuse);
	/*
	 * The same file system (by any path to it: a partition, the disk
	 * at an offset) mounted twice, one of them written, would be two
	 * lwext4s with their own caches on it: its uuid, before lwext4
	 * writes anything (the superblock, at a written mount).
	 */
	if(!sbuuid(fs->c, off, fs->uuid))
		error("ext4: no ext4 superblock");
	for(f = exts.list; f != nil; f = f->next)
		if(strcmp(f->uuid, fs->uuid) == 0 && (rw || f->rw)){
			snprint(ebuf, sizeof ebuf, "ext4: %s is mounted as #E/%s", fs->uuid, f->name);
			error(ebuf);
		}
	up->errstr[0] = 0;
	if(ext4_mount(&fs->mp, &fs->bd, !rw) < 0)
		ext4error(dev);
	if(ext4_get_sblock(&fs->mp, &fs->sb) < 0 || fs->sb == nil){
		ext4_umount(&fs->mp);
		ext4error(dev);
	}
	if(rw && fs->mp.fs.read_only){
		ext4_umount(&fs->mp);
		error("ext4: features lwext4 can only read");
	}
	/* the journal replayed, as ext4srv */
	if(rw && (ext4_recover(&fs->mp) < 0 || ext4_journal_start(&fs->mp) < 0)){
		kstrcpy(ebuf, up->errstr, ERRMAX);
		ext4_umount(&fs->mp);
		kstrcpy(up->errstr, ebuf, ERRMAX);
		ext4error("journal");
	}
	if(rw)
		ext4_cache_write_back(&fs->mp, 1);
	fs->raw = rawfile(dev);
	uuidstr(fs->uuid, fs->sb->uuid);
	memmove(fs->label, fs->sb->volume_name, 16);
	/* dir from the file system's root (fs->base "" while it is walked) */
	fs->base = smalloc(1);
	base = smalloc(1);
	if(waserror()){
		free(base);
		ext4_umount(&fs->mp);
		nexterror();
	}
	if(waserror()){
		if(rw){
			ext4_cache_write_back(&fs->mp, 0);
			ext4_journal_stop(&fs->mp);
		}
		nexterror();
	}
	walkpath(fs, &base, dir, &inode, &ino, 0);
	if(ext4_inode_type(fs->sb, &inode) != EXT4_INODE_MODE_DIRECTORY)
		error(Enotdir);
	poperror();
	poperror();
	free(fs->base);
	fs->base = base;
	fs->rootqid = inodeqid(fs, ino, &inode);
	loadusers(fs);
	fs->id = ++exts.id;
	fs->rootqid.path |= (uvlong)fs->id<<32;
	fs->ref = 1;
	fs->next = exts.list;
	exts.list = fs;
	if(rw && !exts.syncing){
		exts.syncing = 1;
		kproc("ext4sync", syncproc, nil);
	}
	poperror();
	qunlock(&exts);
	poperror();
	print("#E/%s: ext4 %s!/%s, uuid %s%s%s, %s\n", fs->name, fs->dev, fs->base,
		fs->uuid, fs->label[0] ? " label " : "", fs->label, rw ? "read-write" : "read-only");
}

/*
 * #S's raw file of the disk dev is on (#S/sdN0/data, /dev/sdE0/plan9,
 * ...), for its SCSI commands: nil if it is not one of #S's (nusb/disk)
 */
static char*
rawfile(char *dev)
{
	char *s, *e, *r;
	int n;

	if(strncmp(dev, "#S/", 3) == 0)
		s = dev+3;
	else if(strncmp(dev, "/dev/sd", 7) == 0)
		s = dev+5;
	else
		return nil;
	if(strncmp(s, "sd", 2) != 0 || (e = strchr(s, '/')) == nil)
		return nil;
	n = e - s;
	r = smalloc(3+n+5);
	memmove(r, "#S/", 3);
	memmove(r+3, s, n);
	strcpy(r+3+n, "/raw");
	return r;
}

/*
 * The disk's volatile write cache to the medium: SCSI's synchronize
 * cache through #S's raw file (the drivers make it ATA's flush cache,
 * NVMe's flush, virtio's); what was written (synced) then survives a
 * power cut.  A failure is reported once and tried again next time.
 */
static void
flushdev(Ext *fs)
{
	uchar cmd[10];
	char st[32];
	Chan *c;
	long n;

	if(fs->raw == nil)
		return;
	if(waserror()){
		if(!fs->flushwarned)
			print("#E/%s: %s: cache flush: %s\n", fs->name, fs->raw, up->errstr);
		fs->flushwarned = 1;
		return;
	}
	c = namec(fs->raw, Aopen, ORDWR, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	memset(cmd, 0, sizeof cmd);
	cmd[0] = 0x35;
	devtab[c->type]->write(c, cmd, sizeof cmd, 0);
	devtab[c->type]->read(c, st, 0, 0);	/* no data */
	n = devtab[c->type]->read(c, st, sizeof st - 1, 0);
	st[n < 0 ? 0 : n] = 0;
	if(strtol(st, nil, 0) != 0)
		error("status not 0");
	poperror();
	cclose(c);
	poperror();
	fs->nflush++;
}

/*
 * fs's dirty blocks to the device, then the superblock: lwext4 keeps
 * its free counts in memory and writes it only at umount.  With fs
 * locked.
 */
static int
fssync(Ext *fs)
{
	if(!fs->rw)
		return 0;
	up->errstr[0] = 0;
	if(ext4_cache_flush(&fs->mp) < 0 || ext4_sb_write(fs->mp.fs.bdev, &fs->mp.fs.sb) < 0){
		print("#E/%s: sync: %s\n", fs->name, up->errstr);
		return -1;
	}
	flushdev(fs);
	return 0;
}

/*
 * fs clean on the device, read-only from now on: the cache flushed,
 * the journal stopped (its RECOVER flag cleared), the superblock
 * VALID_FS.  With fs locked.
 */
static int
fshalt(Ext *fs)
{
	int r;

	if(!fs->rw)
		return 0;
	up->errstr[0] = 0;
	r = 0;
	if(ext4_cache_write_back(&fs->mp, 0) < 0)
		r = -1;
	if(ext4_journal_stop(&fs->mp) < 0)
		r = -1;
	if(ext4_fs_fini(&fs->mp.fs) < 0 || ext4_cache_flush(&fs->mp) < 0)
		r = -1;
	flushdev(fs);
	fs->mp.fs.read_only = true;
	fs->rw = 0;
	if(r < 0)
		print("#E/%s: halt: %s\n", fs->name, up->errstr);
	else
		print("#E/%s: halted\n", fs->name);
	return r;
}

static void
syncproc(void*)
{
	Ext *fs;

	while(waserror())
		;
	for(;;){
		tsleep(&exts, return0, nil, Syncms);
		qlock(&exts);
		for(fs = exts.list; fs != nil; fs = fs->next)
			if(fs->rw){
				qlock(fs);
				fssync(fs);
				qunlock(fs);
			}
		qunlock(&exts);
	}
}

/* at reboot: what can be had of the file systems */
static void
ext4shutdown(void)
{
	Ext *fs;

	if(!canqlock(&exts))
		return;
	for(fs = exts.list; fs != nil; fs = fs->next)
		if(fs->rw && canqlock(fs)){
			if(!waserror()){
				fshalt(fs);
				poperror();
			}
			qunlock(fs);
		}
	qunlock(&exts);
}

static void
fsumount(char *name)
{
	Ext **l, *fs;

	qlock(&exts);
	for(l = &exts.list; (fs = *l) != nil; l = &fs->next)
		if(strcmp(fs->name, name) == 0)
			break;
	if(fs == nil){
		qunlock(&exts);
		error(Enonexist);
	}
	if(fs->ref != 1){
		qunlock(&exts);
		error(Einuse);
	}
	*l = fs->next;
	qunlock(&exts);
	qlock(fs);
	fshalt(fs);
	if(ext4_umount(&fs->mp) < 0)
		print("#E/%s: umount: %s\n", fs->name, up->errstr);
	qunlock(fs);
	fsfree(fs);
}

enum {
	CMmount,
	CMumount,
	CMsync,
	CMhalt,
};

static Cmdtab ext4cmd[] = {
	CMmount,	"mount",	0,
	CMumount,	"umount",	2,
	CMsync,	"sync",	0,
	CMhalt,	"halt",	0,
};

static void
ctlwrite(void *a, long n)
{
	Cmdbuf *cb;
	Cmdtab *ct;
	char *spec, *dir, **f, dev[KNAMELEN+16];
	int i, nf, rw, bad;
	vlong off, size;
	Ext *fs;

	cb = parsecmd(a, n);
	if(waserror()){
		free(cb);
		nexterror();
	}
	ct = lookupcmd(cb, ext4cmd, nelem(ext4cmd));
	switch(ct->index){
	case CMmount:
		f = cb->f;
		nf = cb->nf;
		rw = nf > 1 && strcmp(f[1], "-w") == 0;
		if(rw){
			f++;
			nf--;
		}
		if(nf < 3)
			cmderror(cb, "mount [-w] name device|UUID=uuid|LABEL=label|PARTUUID=guid[!/dir] [candidate ...]");
		spec = f[2];
		if((dir = strchr(spec, '!')) != nil)
			*dir++ = 0;
		else
			dir = "";
		off = size = 0;
		if(nf == 3 && scandisks(spec, dev, sizeof dev, &off, &size))
			spec = dev;
		else if(cistrncmp(spec, "UUID=", 5) == 0 || strncmp(spec, "LABEL=", 6) == 0){
			for(i = 3; i < nf; i++)
				if(spec[0] == 'L' ? probe(f[i], nil, spec+6) : probe(f[i], spec+5, nil))
					break;
			if(i >= nf)
				error("ext4: no device with that file system");
			spec = f[i];
		}else if(cistrncmp(spec, "PARTUUID=", 9) == 0)
			error("ext4: no partition with that GUID");
		else if(nf != 3)
			cmderror(cb, "too many arguments");
		fsmount(f[1], spec, off, size, dir, rw);
		break;
	case CMumount:
		fsumount(cb->f[1]);
		break;
	case CMsync:
		bad = 0;
		qlock(&exts);
		for(fs = exts.list; fs != nil; fs = fs->next)
			if(cb->nf < 2 || strcmp(fs->name, cb->f[1]) == 0){
				qlock(fs);
				bad |= fssync(fs);
				qunlock(fs);
			}
		qunlock(&exts);
		if(bad)
			error("ext4: sync failed");
		break;
	case CMhalt:
		bad = 0;
		qlock(&exts);
		for(fs = exts.list; fs != nil; fs = fs->next)
			if(cb->nf < 2 || strcmp(fs->name, cb->f[1]) == 0){
				qlock(fs);
				bad |= fshalt(fs);
				qunlock(fs);
			}
		qunlock(&exts);
		/* fshalt waits for it to go, as for ext4srv's */
		if(cb->nf < 2 || strcmp(cb->f[1], "root") == 0)
			unpost("#s/ext4.root.cmd");
		if(bad)
			error("ext4: halt failed");
		break;
	}
	poperror();
	free(cb);
}

static long
ctlread(void *a, long n, vlong off)
{
	char *buf, *p, *e;
	Ext *fs;

	buf = smalloc(READSTR);
	if(waserror()){
		free(buf);
		nexterror();
	}
	p = buf;
	e = buf + READSTR;
	qlock(&exts);
	for(fs = exts.list; fs != nil; fs = fs->next)
		p = seprint(p, e, "%s %s!/%s %s uuid %s label '%s' flushes %lud\n", fs->name, fs->dev, fs->base,
			fs->rw ? "rw" : "ro", fs->uuid, fs->label, fs->nflush);
	qunlock(&exts);
	p = seprint(p, e, "kstack %lud of %d at the device\n", kstackmax, KSTACK);
	USED(p);
	n = readstr(off, a, n, buf);
	poperror();
	free(buf);
	return n;
}

static void
ext4reset(void)
{
}

/* our ctl as srv (a file of #s), as ext4srv posts its command file */
static void
post(char *srv)
{
	Chan *c, *s;
	char buf[16];
	int fd;

	c = namec("#E/ctl", Aopen, OWRITE, 0);
	if((fd = newfd(c, 0)) < 0){
		cclose(c);
		error(Enofd);
	}
	if(waserror()){
		fdclose(fd, 0);
		nexterror();
	}
	s = namec(srv, Acreate, OWRITE, 0600);
	if(waserror()){
		cclose(s);
		nexterror();
	}
	snprint(buf, sizeof buf, "%d", fd);
	devtab[s->type]->write(s, buf, strlen(buf), 0);
	poperror();
	cclose(s);
	poperror();
	fdclose(fd, 0);
}

static void
unpost(char *srv)
{
	Chan *c;

	if(waserror())
		return;
	c = namec(srv, Aremove, 0, 0);
	if(waserror()){
		c->type = 0;	/* as sysremove: remove clunked it */
		cclose(c);
		nexterror();
	}
	devtab[c->type]->remove(c);
	poperror();
	c->type = 0;
	cclose(c);
	poperror();
}

/*
 * plan9.ini's root= ([-w] DEVICE|UUID=uuid|LABEL=label|PARTUUID=guid
 * [!/dir]): mounted as #E/root and bound before / in the first process,
 * before it runs init (init0): its /rc and so on, not #/'s empty mount
 * points of those names; #/ gives those it lacks (/dev, /env, ...).
 * $rootdir is #E/root for init's newns.
 * The host owner is user= (as factotum -u would make it).  1 if done.
 */
int
rootmount(void)
{
	char *s, *u, cmd[256];
	Chan *c0, *c1;

	if((s = getconf("root")) == nil)
		return 0;
	if((u = getconf("user")) != nil && *u != 0){
		kstrdup(&eve, u);
		kstrdup(&up->user, u);
	}
	if(strncmp(s, "-w ", 3) == 0)
		snprint(cmd, sizeof cmd, "mount -w root %s", s+3);
	else
		snprint(cmd, sizeof cmd, "mount root %s", s);
	if(waserror()){
		print("root=%s: %s\n", s, up->errstr);
		return 0;
	}
	/* a gefs partition: its tree before / (devgefs.c) */
	if(gefsroot(strncmp(s, "-w ", 3) == 0 ? s+3 : s, strncmp(s, "-w ", 3) == 0)){
		poperror();
		return 1;
	}
	ctlwrite(cmd, strlen(cmd));
	c0 = namec("#E/root", Abind, 0, 0);
	if(waserror()){
		cclose(c0);
		nexterror();
	}
	c1 = namec("/", Amount, 0, 0);
	if(waserror()){
		cclose(c1);
		nexterror();
	}
	cmount(c0, c1, MBEFORE, nil);
	poperror();
	cclose(c1);
	poperror();
	cclose(c0);
	poperror();
	ksetenv("rootdir", "#E/root", 0);
	if(!waserror()){
		post("#s/ext4.root.cmd");
		poperror();
	}
	return 1;
}

/* #E: the top; #ENAME: that file system's root */
static Chan*
ext4attach(char *spec)
{
	Chan *c;
	Ext *fs;
	Xfile *x;

	if(*spec == 0){
		c = devattach('E', spec);
		mkqid(&c->qid, Qtop, 0, QTDIR);
		return c;
	}
	qlock(&exts);
	for(fs = exts.list; fs != nil; fs = fs->next)
		if(strcmp(fs->name, spec) == 0){
			incref(fs);
			break;
		}
	qunlock(&exts);
	if(fs == nil)
		error(Enonexist);
	x = newxfile(fs, fs->base, INO(fs->rootqid));
	fsput(fs);
	c = devattach('E', spec);
	c->qid = fs->rootqid;
	c->aux = x;
	return c;
}

/* a walk of Qtop: ctl, a file system's root, or .. */
static int
topwalk1(Chan *nc, char *name)
{
	Ext *fs;
	Xfile *x;

	if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return 0;
	if(strcmp(name, "ctl") == 0){
		mkqid(&nc->qid, Qctl, 0, QTFILE);
		return 0;
	}
	qlock(&exts);
	for(fs = exts.list; fs != nil; fs = fs->next)
		if(strcmp(fs->name, name) == 0){
			incref(fs);
			break;
		}
	qunlock(&exts);
	if(fs == nil)
		return -1;
	x = newxfile(fs, fs->base, INO(fs->rootqid));
	fsput(fs);
	nc->qid = fs->rootqid;
	nc->aux = x;
	return 0;
}

static Walkqid*
ext4walk(Chan *c, Chan *nc, char **name, int nname)
{
	struct ext4_inode inode;
	Walkqid *wq;
	Xfile *x, *ox;
	Ext *fs;
	char *path;
	u32int ino;
	int j, alloc;

	if(nname > 0)
		isdir(c);
	alloc = nc == nil;
	wq = smalloc(sizeof(Walkqid)+(nname-1)*sizeof(Qid));
	if(alloc){
		nc = devclone(c);
		nc->type = 0;	/* device doesn't know about this channel yet */
	}
	/* nc gets its own Xfile */
	ox = c->aux;
	nc->aux = nil;
	if(ox != nil){
		if(ox->stale)
			error(Estale);
		nc->aux = newxfile(ox->fs, ox->path, ox->ino);
	}
	wq->clone = nc;
	if(waserror()){
		freexfile(nc->aux);
		nc->aux = nil;
		if(alloc)
			cclose(nc);
		free(wq);
		return nil;
	}
	for(j = 0; j < nname; j++){
		if(!(nc->qid.type & QTDIR)){
			if(j == 0)
				error(Enotdir);
			break;
		}
		x = nc->aux;
		if(x == nil){
			if(topwalk1(nc, name[j]) < 0){
				if(j == 0)
					error(Enonexist);
				kstrcpy(up->errstr, Enonexist, ERRMAX);
				break;
			}
			wq->qid[wq->nqid++] = nc->qid;
			continue;
		}
		fs = x->fs;
		if(x->stale)
			error(Estale);
		/* .. of a file system's root is #E */
		if(strcmp(name[j], "..") == 0 && strcmp(x->path, fs->base) == 0){
			freexfile(x);
			nc->aux = nil;
			mkqid(&nc->qid, Qtop, 0, QTDIR);
			wq->qid[wq->nqid++] = nc->qid;
			continue;
		}
		path = smalloc(strlen(x->path)+1);
		strcpy(path, x->path);
		qlock(fs);
		if(waserror()){
			qunlock(fs);
			free(path);
			if(j == 0)
				nexterror();
			break;
		}
		walkname(fs, &path, name[j], &inode, &ino, 0);
		if(ext4_inode_type(fs->sb, &inode) != EXT4_INODE_MODE_DIRECTORY
		&& ext4_inode_type(fs->sb, &inode) != EXT4_INODE_MODE_FILE)
			error(Enonexist);
		qunlock(fs);
		poperror();
		free(x->path);
		x->path = path;
		x->ino = ino;
		nc->qid = inodeqid(fs, ino, &inode);
		wq->qid[wq->nqid++] = nc->qid;
	}
	poperror();
	if(wq->nqid < nname){
		freexfile(nc->aux);
		nc->aux = nil;
		if(alloc)
			cclose(nc);
		wq->clone = nil;
	}else if(alloc)
		nc->type = c->type;
	return wq;
}

static int
ext4stat(Chan *c, uchar *db, int n)
{
	struct ext4_inode inode;
	u32int ino;
	Xfile *x;
	Ext *fs;
	Dir d;
	char *name;

	x = c->aux;
	if(x == nil){
		if(c->qid.path == Qctl)
			devdir(c, c->qid, "ctl", 0, eve, 0664, &d);
		else
			devdir(c, c->qid, "#E", 0, eve, DMDIR|0555, &d);
		n = convD2M(&d, db, n);
		if(n == 0)
			error(Eshortstat);
		return n;
	}
	fs = x->fs;
	if(strcmp(x->path, fs->base) == 0)
		name = fs->name;
	else if((name = strrchr(x->path, '/')) != nil)
		name++;
	else
		name = x->path;
	qlock(fs);
	if(waserror()){
		qunlock(fs);
		nexterror();
	}
	up->errstr[0] = 0;
	if(ext4_raw_inode_fill(&fs->mp, x->path, &ino, &inode) < 0)
		ext4error(x->path);
	inodedir(c, x, name, ino, &inode, &d);
	qunlock(fs);
	poperror();
	n = convD2M(&d, db, n);
	if(n == 0)
		error(Eshortstat);
	return n;
}

static int
ext4flags(int omode)
{
	int f;

	switch(omode&3){
	case OWRITE:
		f = O_WRONLY;
		break;
	case ORDWR:
		f = O_RDWR;
		break;
	default:
		f = O_RDONLY;
	}
	if(omode&OTRUNC)
		f |= O_TRUNC;
	return f;
}

static int
writing(int omode)
{
	return (omode&3) == OWRITE || (omode&3) == ORDWR || (omode&(OTRUNC|ORCLOSE)) != 0;
}

/* may x be removed: its directory written?  With fs locked */
static void
dirwritable(Xfile *x)
{
	struct ext4_inode dinode;
	u32int dino;
	Ext *fs;
	char *p;

	fs = x->fs;
	if(strcmp(x->path, fs->base) == 0)
		error(Eperm);
	p = smalloc(strlen(x->path)+1);
	strcpy(p, x->path);
	parent(p);
	if(waserror()){
		free(p);
		nexterror();
	}
	up->errstr[0] = 0;
	if(ext4_raw_inode_fill(&fs->mp, p, &dino, &dinode) < 0)
		ext4error(p);
	devpermcheck(uname(fs, ext4_inode_get_uid(&dinode), x->gidbuf),
		ext4_inode_get_mode(fs->sb, &dinode) & 0777, OWRITE);
	poperror();
	free(p);
}

static Chan*
ext4open(Chan *c, int omode)
{
	struct ext4_inode inode;
	u32int ino;
	Xfile *x;
	Ext *fs;

	x = c->aux;
	if(x == nil){
		if(c->qid.path == Qctl)
			devpermcheck(eve, 0664, omode);
		else if(omode != OREAD)
			error(Eperm);
		c->mode = openmode(omode);
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}
	fs = x->fs;
	if(writing(omode))
		needrw(fs);
	if((c->qid.type & QTDIR) && omode != OREAD)
		error(Eisdir);
	qlock(fs);
	if(waserror()){
		qunlock(fs);
		nexterror();
	}
	xinode(c, x, &ino, &inode);
	devpermcheck(uname(fs, ext4_inode_get_uid(&inode), x->uidbuf),
		ext4_inode_get_mode(fs->sb, &inode) & 0777, omode);
	/* devpermcheck looks at omode&3 only */
	if(omode & OTRUNC)
		devpermcheck(uname(fs, ext4_inode_get_uid(&inode), x->uidbuf),
			ext4_inode_get_mode(fs->sb, &inode) & 0777, OWRITE);
	if(omode & ORCLOSE)
		dirwritable(x);
	if(c->qid.type & QTDIR){
		x->dir = smalloc(sizeof(ext4_dir));
		if(ext4_dir_open(&fs->mp, x->dir, x->path) < 0){
			free(x->dir);
			x->dir = nil;
			ext4error(x->path);
		}
		x->doff = 0;
	}else{
		x->file = smalloc(sizeof(ext4_file));
		if(ext4_fopen2(&fs->mp, x->file, x->path, ext4flags(omode)) < 0){
			free(x->file);
			x->file = nil;
			ext4error(x->path);
		}
		if(omode & OTRUNC){
			written(fs, ino);
			xinode(c, x, &ino, &inode);
			c->qid = inodeqid(fs, ino, &inode);
		}
	}
	x->rclose = (omode & ORCLOSE) != 0;
	qunlock(fs);
	poperror();
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

/*
 * name in the directory c: a file opened omode or a directory, with
 * the directory's permissions (Plan 9's) and owner, the creator's id if
 * it has one in etc/group.  c becomes it.
 */
static Chan*
ext4create(Chan *c, char *name, int omode, ulong perm)
{
	struct ext4_inode inode;
	u32int ino, dperm, uid, gid, iflags;
	Xfile *x;
	Ext *fs;
	char *s, ebuf[ERRMAX];
	ulong now;

	x = c->aux;
	if(x == nil)
		error(Eperm);
	fs = x->fs;
	needrw(fs);
	if(strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strlen(name) > Maxname)
		error(Ebadarg);
	if((perm & DMDIR) && (omode&3) != OREAD)
		error(Eperm);
	s = join(x->path, name);
	qlock(fs);
	if(waserror()){
		qunlock(fs);
		free(s);
		nexterror();
	}
	xinode(c, x, &ino, &inode);
	if(ext4_inode_type(fs->sb, &inode) != EXT4_INODE_MODE_DIRECTORY)
		error(Enotdir);
	devpermcheck(uname(fs, ext4_inode_get_uid(&inode), x->uidbuf),
		ext4_inode_get_mode(fs->sb, &inode) & 0777, OWRITE);
	dperm = ext4_inode_get_mode(fs->sb, &inode) & 0777;
	if((uid = userid(fs, up->user)) == ~0)
		uid = ext4_inode_get_uid(&inode);
	gid = ext4_inode_get_gid(&inode);
	if(ext4_inode_exist(&fs->mp, s, EXT4_DE_UNKNOWN) == 0)
		error(Eexist);
	up->errstr[0] = 0;
	if(perm & DMDIR){
		perm &= ~0777 | (dperm & 0777);
		if(ext4_dir_mk(&fs->mp, s) < 0)
			ext4error(s);
	}else{
		perm &= ~0666 | (dperm & 0666);
		x->file = smalloc(sizeof(ext4_file));
		if(ext4_fopen2(&fs->mp, x->file, s, O_CREAT|ext4flags(omode)) < 0){
			free(x->file);
			x->file = nil;
			ext4error(s);
		}
	}
	iflags = 0;
	if(perm & DMAPPEND)
		iflags |= EXT4_INODE_FLAG_APPEND;
	now = seconds();
	if(ext4_mode_set(&fs->mp, s, perm & 0777, iflags) < 0
	|| ext4_owner_set(&fs->mp, s, uid, gid) < 0
	|| ext4_mtime_set(&fs->mp, s, now) < 0
	|| ext4_ctime_set(&fs->mp, s, now) < 0
	|| ext4_atime_set(&fs->mp, s, now) < 0){
		kstrcpy(ebuf, up->errstr, sizeof ebuf);
		if(x->file != nil){
			ext4_fclose(x->file);
			free(x->file);
			x->file = nil;
			ext4_fremove(&fs->mp, s);
		}else
			ext4_dir_rm(&fs->mp, s);
		kstrcpy(up->errstr, ebuf, ERRMAX);
		ext4error(s);
	}
	ext4_mtime_set(&fs->mp, x->path, now);
	written(fs, ino);
	if(perm & DMDIR){
		x->dir = smalloc(sizeof(ext4_dir));
		if(ext4_dir_open(&fs->mp, x->dir, s) < 0){
			free(x->dir);
			x->dir = nil;
			ext4error(s);
		}
		x->doff = 0;
	}
	free(x->path);
	x->path = s;
	s = nil;
	USED(s);
	up->errstr[0] = 0;
	if(ext4_raw_inode_fill(&fs->mp, x->path, &ino, &inode) < 0)
		ext4error(x->path);
	x->ino = ino;
	c->qid = inodeqid(fs, ino, &inode);
	x->rclose = (omode & ORCLOSE) != 0;
	qunlock(fs);
	poperror();
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

/* the file or the (empty) directory at x's path; with fs locked */
static void
xremove(Ext *fs, Xfile *x, struct ext4_inode *inode)
{
	const ext4_direntry *e;
	ext4_dir dir;
	int empty;
	char *p;

	up->errstr[0] = 0;
	if(ext4_inode_type(fs->sb, inode) == EXT4_INODE_MODE_DIRECTORY){
		/* lwext4's ext4_dir_rm takes all in it */
		if(ext4_dir_open(&fs->mp, &dir, x->path) < 0)
			ext4error(x->path);
		empty = 1;
		while((e = ext4_dir_entry_next(&dir)) != nil)
			if(e->inode != 0 && e->name_length != 0
			&& !(e->name_length == 1 && e->name[0] == '.')
			&& !(e->name_length == 2 && e->name[0] == '.' && e->name[1] == '.')){
				empty = 0;
				break;
			}
		ext4_dir_close(&dir);
		if(!empty)
			error("directory not empty");
		if(ext4_dir_rm(&fs->mp, x->path) < 0)
			ext4error(x->path);
	}else if(ext4_fremove(&fs->mp, x->path) < 0)
		ext4error(x->path);
	p = smalloc(strlen(x->path)+1);
	strcpy(p, x->path);
	parent(p);
	ext4_mtime_set(&fs->mp, p, seconds());
	free(p);
}

static void
ext4close(Chan *c)
{
	struct ext4_inode inode;
	u32int ino;
	Xfile *x;
	Ext *fs;

	x = c->aux;
	if(x != nil && x->rclose && (c->flag & COPEN)){
		fs = x->fs;
		if(x->file != nil){
			qlock(fs);
			ext4_fclose(x->file);
			qunlock(fs);
			free(x->file);
			x->file = nil;
		}
		qlock(fs);
		if(!waserror()){
			xinode(c, x, &ino, &inode);
			dirwritable(x);
			xremove(fs, x, &inode);
			staleino(fs, ino, x);
			poperror();
		}
		qunlock(fs);
	}
	freexfile(x);
	c->aux = nil;
}

/* the entries of #E: ctl and the file systems */
static int
topgen(Chan *c, char*, Dirtab*, int, int i, Dir *d)
{
	Ext *fs;
	Qid q;

	if(i == DEVDOTDOT){
		devdir(c, c->qid, "#E", 0, eve, DMDIR|0555, d);
		return 1;
	}
	if(i == 0){
		mkqid(&q, Qctl, 0, QTFILE);
		devdir(c, q, "ctl", 0, eve, 0664, d);
		return 1;
	}
	qlock(&exts);
	for(fs = exts.list; fs != nil && --i > 0; fs = fs->next)
		;
	if(fs == nil){
		qunlock(&exts);
		return -1;
	}
	q = fs->rootqid;
	devdir(c, q, fs->name, 0, eve, DMDIR|0555, d);
	qunlock(&exts);
	return 1;
}

/*
 * Entry i of the directory: on from the last one, or from the start.
 * The entries are files, directories and symbolic links; a link to
 * something else (or nothing) is 0, no entry.  The Dir's strings stay
 * in x until the next call.  The last one again (it did not fit in a
 * read: devdirread asks for it in the next) is by its name, not by
 * counting from the start, which would skip one for each removed.
 */
static int
dirgen(Chan *c, char*, Dirtab*, int, int i, Dir *d)
{
	const ext4_direntry *e;
	struct ext4_inode inode;
	u32int ino, t;
	Xfile *x;
	Ext *fs;
	char *path;
	int r;

	x = c->aux;
	fs = x->fs;
	if(x->stale)
		error(Estale);
	qlock(fs);
	if(waserror()){
		qunlock(fs);
		nexterror();
	}
	if(i == x->doff-1 && x->elast)
		goto Again;
	if(i < x->doff){
		ext4_dir_entry_rewind(x->dir);
		x->doff = 0;
	}
	x->elast = 0;
	for(;;){
		if((e = ext4_dir_entry_next(x->dir)) == nil){
			qunlock(fs);
			poperror();
			return -1;
		}
		if(e->name_length == 0 || e->inode == 0)
			continue;
		if(e->inode_type != EXT4_DE_REG_FILE && e->inode_type != EXT4_DE_DIR
		&& e->inode_type != EXT4_DE_SYMLINK)
			continue;
		memmove(x->ename, e->name, e->name_length);
		x->ename[e->name_length] = 0;
		if(strcmp(x->ename, ".") == 0 || strcmp(x->ename, "..") == 0)
			continue;
		if(x->doff++ == i)
			break;
	}
	x->elast = 1;
Again:
	/* what a link is */
	r = 0;
	path = smalloc(strlen(x->path)+1);
	strcpy(path, x->path);
	if(!waserror()){
		walkname(fs, &path, x->ename, &inode, &ino, 0);
		poperror();
		t = ext4_inode_type(fs->sb, &inode);
		if(t == EXT4_INODE_MODE_FILE || t == EXT4_INODE_MODE_DIRECTORY){
			inodedir(c, x, x->ename, ino, &inode, d);
			r = 1;
		}
	}
	free(path);
	qunlock(fs);
	poperror();
	return r;
}

static long
ext4read(Chan *c, void *a, long n, vlong off)
{
	Xfile *x;
	Ext *fs;
	usize got;
	void *buf;

	x = c->aux;
	if(x == nil){
		if(c->qid.path == Qctl)
			return ctlread(a, n, off);
		if(off == 0)
			c->dri = 0;
		return devdirread(c, a, n, nil, 0, topgen);
	}
	if(c->qid.type & QTDIR){
		if(off == 0)
			c->dri = 0;
		return devdirread(c, a, n, nil, 0, dirgen);
	}
	if(n <= 0 || off < 0)
		return 0;
	if(n > Maxread)
		n = Maxread;
	/* the copy to a, which may fault, without fs locked */
	buf = smalloc(n);
	if(waserror()){
		free(buf);
		nexterror();
	}
	fs = x->fs;
	qlock(fs);
	if(waserror()){
		qunlock(fs);
		nexterror();
	}
	got = 0;
	if(x->stale)
		error(Estale);
	syncsize(x);
	up->errstr[0] = 0;
	if(off < ext4_fsize(x->file)){
		if(ext4_fseek(x->file, off, 0) < 0 || ext4_fread(x->file, buf, n, &got) < 0)
			ext4error(x->path);
	}
	qunlock(fs);
	poperror();
	memmove(a, buf, got);
	poperror();
	free(buf);
	return got;
}

/*
 * c's file grown to size with zeros (lwext4 leaves a new block's rest as
 * it was on the device, so no holes are made): Growchunk at a time with
 * fs locked, so that a write far past the end holds the file system
 * only so long, and a note stops it.  With fs not locked.
 */
static void
growto(Chan *c, Xfile *x, u64int to)
{
	struct ext4_inode inode;
	ext4_file f;
	u32int ino;
	u64int size, done;
	usize w, z;
	Ext *fs;

	fs = x->fs;
	for(;;){
		qlock(fs);
		if(waserror()){
			qunlock(fs);
			nexterror();
		}
		xinode(c, x, &ino, &inode);
		up->errstr[0] = 0;
		if(ext4_fopen2(&fs->mp, &f, x->path, O_WRONLY) < 0)
			ext4error(x->path);
		size = ext4_fsize(&f);
		for(done = 0; size < to && done < Growchunk; done += w){
			z = to - size;
			if(z > sizeof zeros)
				z = sizeof zeros;
			ext4_fseek(&f, 0, 2);
			w = 0;
			if(ext4_fwrite(&f, zeros, z, &w) < 0 || w == 0){
				ext4_fclose(&f);
				if(up->errstr[0] == 0)
					error("ext4: no space");
				ext4error(x->path);
			}
			size += w;
		}
		ext4_fclose(&f);
		if(done > 0)
			written(fs, ino);
		qunlock(fs);
		poperror();
		if(size >= to)
			return;
		/* not notepending: a driver's sleep may have taken that */
		if(up->nnote > 0)
			error(Eintr);
	}
}

/*
 * At off, or at the end for an append-only file; a file grows to off
 * with zeros first (growto).  At most Maxread: ext4write makes a
 * bigger write of it.
 */
static long
ext4write1(Chan *c, void *a, long n, vlong off)
{
	Xfile *x;
	Ext *fs;
	usize w;
	u64int size;
	void *buf;

	x = c->aux;
	if(x == nil){
		if(c->qid.path == Qctl){
			ctlwrite(a, n);
			return n;
		}
		error(Eperm);
	}
	if(x->file == nil)
		error(Eisdir);
	fs = x->fs;
	needrw(fs);
	if(n <= 0)
		return 0;
	if(n > Maxread)
		n = Maxread;
	if(off < 0)
		error(Enegoff);
	/* the copy from a, which may fault, without fs locked */
	buf = smalloc(n);
	if(waserror()){
		free(buf);
		nexterror();
	}
	memmove(buf, a, n);
	for(;;){
		qlock(fs);
		if(waserror()){
			qunlock(fs);
			nexterror();
		}
		if(x->stale)
			error(Estale);
		syncsize(x);
		size = ext4_fsize(x->file);
		if(c->qid.type & QTAPPEND)
			off = size;
		if(size >= off)
			break;	/* fs locked */
		qunlock(fs);
		poperror();
		growto(c, x, off);
	}
	up->errstr[0] = 0;
	ext4_fseek(x->file, off, 0);
	if(ext4_ftell(x->file) != off)
		error("ext4: seek failed");
	w = 0;
	if(ext4_fwrite(x->file, buf, n, &w) < 0)
		ext4error(x->path);
	written(fs, INO(c->qid));
	qunlock(fs);
	poperror();
	poperror();
	free(buf);
	return w;
}

/*
 * All of it, Maxread at a time: a short count is an error to a writer
 * (ar wrote a member bigger than that at once and stopped).
 */
static long
ext4write(Chan *c, void *a, long n, vlong off)
{
	long m, w, t;

	if(c->aux == nil || n <= Maxread)
		return ext4write1(c, a, n, off);
	for(t = 0; t < n; t += w){
		m = n - t;
		if(m > Maxread)
			m = Maxread;
		w = ext4write1(c, (uchar*)a + t, m, off + t);
		if(w < m)
			return t + w;
	}
	return t;
}

static void
ext4remove(Chan *c)
{
	struct ext4_inode inode, dinode;
	u32int ino, dino;
	Xfile *x;
	Ext *fs;
	char *p;

	x = c->aux;
	if(x == nil)
		error(Eperm);
	fs = x->fs;
	needrw(fs);
	if(strcmp(x->path, fs->base) == 0)
		error(Eperm);
	p = smalloc(strlen(x->path)+1);
	strcpy(p, x->path);
	parent(p);
	qlock(fs);
	if(waserror()){
		qunlock(fs);
		free(p);
		nexterror();
	}
	xinode(c, x, &ino, &inode);
	up->errstr[0] = 0;
	if(ext4_raw_inode_fill(&fs->mp, p, &dino, &dinode) < 0)
		ext4error(p);
	devpermcheck(uname(fs, ext4_inode_get_uid(&dinode), x->uidbuf),
		ext4_inode_get_mode(fs->sb, &dinode) & 0777, OWRITE);
	if(x->file != nil){
		ext4_fclose(x->file);
		free(x->file);
		x->file = nil;
	}
	if(x->dir != nil){
		ext4_dir_close(x->dir);
		free(x->dir);
		x->dir = nil;
	}
	xremove(fs, x, &inode);
	staleino(fs, ino, x);
	written(fs, dino);
	x->rclose = 0;
	qunlock(fs);
	poperror();
	free(p);
}

/* as ext4srv: length, mode, mtime, gid and name (in the same directory) */
static int
ext4wstat(Chan *c, uchar *dp, int n)
{
	struct ext4_inode inode, dinode;
	u32int ino, dino, uid, gid, iflags;
	int isdir, owner, grow;
	ext4_file f;
	Xfile *x;
	Ext *fs;
	Dir *d;
	char *p, *s, *strs;
	u64int size;
	vlong length;

	x = c->aux;
	if(x == nil)
		error(Eperm);
	fs = x->fs;
	needrw(fs);
	d = smalloc(sizeof(Dir) + n);
	strs = (char*)&d[1];
	if(convM2D(dp, n, d, strs) != n){
		free(d);
		error(Eshortstat);
	}
	p = nil;
	s = nil;
	qlock(fs);
	if(waserror()){
		qunlock(fs);
		free(d);
		free(p);
		free(s);
		nexterror();
	}
	xinode(c, x, &ino, &inode);
	isdir = ext4_inode_type(fs->sb, &inode) == EXT4_INODE_MODE_DIRECTORY;
	uid = ext4_inode_get_uid(&inode);
	owner = strcmp(up->user, uname(fs, uid, x->uidbuf)) == 0 || strcmp(up->user, eve) == 0;
	if(d->uid[0] != 0 && strcmp(d->uid, uname(fs, uid, x->uidbuf)) != 0)
		error(Eperm);
	if(d->mode != ~0 && (!owner || !!isdir != !!(d->mode & DMDIR)))
		error(Eperm);
	if(d->mtime != ~0 && !owner)
		error(Eperm);
	gid = ~0;
	if(d->gid[0] != 0 && strcmp(d->gid, uname(fs, ext4_inode_get_gid(&inode), x->gidbuf)) != 0){
		if(!owner || (gid = userid(fs, d->gid)) == ~0)
			error(Eperm);
	}
	if(d->length != ~0){
		if(isdir)
			error(Eisdir);
		devpermcheck(uname(fs, uid, x->uidbuf), ext4_inode_get_mode(fs->sb, &inode) & 0777, OWRITE);
	}
	if(d->name[0] != 0 && strcmp(d->name, ".") != 0 && strcmp(x->path, fs->base) != 0
	&& (strrchr(x->path, '/') == nil ? strcmp(d->name, x->path) != 0 : strcmp(d->name, strrchr(x->path, '/')+1) != 0)){
		if(strchr(d->name, '/') != nil || strcmp(d->name, "..") == 0 || strlen(d->name) > Maxname)
			error(Ebadarg);
		p = smalloc(strlen(x->path)+1);
		strcpy(p, x->path);
		parent(p);
		up->errstr[0] = 0;
		if(ext4_raw_inode_fill(&fs->mp, p, &dino, &dinode) < 0)
			ext4error(p);
		devpermcheck(uname(fs, ext4_inode_get_uid(&dinode), x->gidbuf),
			ext4_inode_get_mode(fs->sb, &dinode) & 0777, OWRITE);
		s = join(p, d->name);
		if(ext4_inode_exist(&fs->mp, s, EXT4_DE_UNKNOWN) == 0)
			error(Eexist);
		up->errstr[0] = 0;
		if(ext4_frename(&fs->mp, x->path, s) < 0)
			ext4error(x->path);
		free(x->path);
		x->path = s;
		s = nil;
		USED(s);
		ext4_mtime_set(&fs->mp, p, seconds());
		written(fs, dino);
	}
	up->errstr[0] = 0;
	grow = 0;
	if(d->length != ~0){
		if(ext4_fopen2(&fs->mp, &f, x->path, O_WRONLY) < 0)
			ext4error(x->path);
		size = ext4_fsize(&f);
		if(d->length < size){
			if(ext4_ftruncate(&f, d->length) < 0){
				ext4_fclose(&f);
				ext4error(x->path);
			}
		}else
			grow = d->length > size;
		ext4_fclose(&f);
		written(fs, ino);
	}
	if(d->mode != ~0){
		iflags = 0;
		if(d->mode & DMAPPEND)
			iflags |= EXT4_INODE_FLAG_APPEND;
		if(ext4_mode_set(&fs->mp, x->path, d->mode & 0777, iflags) < 0)
			ext4error(x->path);
	}
	if(d->mtime != ~0 && ext4_mtime_set(&fs->mp, x->path, d->mtime) < 0)
		ext4error(x->path);
	if(gid != ~0 && ext4_owner_set(&fs->mp, x->path, uid, gid) < 0)
		ext4error(x->path);
	ext4_ctime_set(&fs->mp, x->path, seconds());
	written(fs, ino);
	up->errstr[0] = 0;
	if(ext4_raw_inode_fill(&fs->mp, x->path, &ino, &inode) < 0)
		ext4error(x->path);
	c->qid = inodeqid(fs, ino, &inode);
	qunlock(fs);
	poperror();
	length = d->length;
	free(d);
	free(p);
	if(grow){
		growto(c, x, length);
		qlock(fs);
		if(!waserror()){
			xinode(c, x, &ino, &inode);
			c->qid = inodeqid(fs, ino, &inode);
			poperror();
		}
		qunlock(fs);
	}
	return n;
}

Dev ext4devtab = {
	'E',
	"ext4",

	ext4reset,
	devinit,
	ext4shutdown,
	ext4attach,
	ext4walk,
	ext4stat,
	ext4open,
	ext4create,
	ext4close,
	ext4read,
	devbread,
	ext4write,
	devbwrite,
	ext4remove,
	ext4wstat,
};
