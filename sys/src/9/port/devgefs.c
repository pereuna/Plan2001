/*
 * #G: gefs in the kernel (../gefs, docs/gefs.md).  gefs runs as kprocs
 * and serves 9P on a pipe; gefsroot mounts the other end before / as
 * the root, for plan9.ini's root= when the partition is gefs:
 *
 *	root=-w PARTUUID=guid		its tree main
 *	root=-w /dev/sdE0/fs!adm	another tree (snapshot label)
 *
 * The console (sync, halt, check, show) is /srv/gefs.root.cmd, which
 * fshalt halts.  /srv/gefs.root is its 9P (eve's): mount it with a
 * tree's name (mount /srv/gefs.root /n/adm adm), whose ctl file makes
 * snapshots.  The
 * device has nothing of its own but the shutdown: at reboot gefs is
 * halted (synced).
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

Chan*	gefsserve(char*, Chan*, vlong, vlong, int, char*);
void	gefspost(Chan*, char*);
void	gefsshutdown(void);
int	rootdisk(char*, char*, int, vlong*, vlong*);

/* Has c, at off, gefs's superblock? */
static int
isgefs(Chan *c, vlong off)
{
	char sb[8];

	if(waserror())
		return 0;
	if(devtab[c->type]->read(c, sb, sizeof sb, off) != sizeof sb)
		error(Eio);
	poperror();
	return memcmp(sb, "gefs9.00", 8) == 0;
}

/*
 * root= (as devext4's rootmount takes it, without -w): DEVICE or
 * PARTUUID=guid, then !tree.  1 if it is gefs and mounted before /.
 */
int
gefsroot(char *spec, int rw)
{
	char *s, *tree, dev[64];
	vlong off, size;
	Chan *c, *m, *r;
	Dir *d;

	s = smalloc(strlen(spec)+1);
	strcpy(s, spec);
	if(waserror()){
		free(s);
		nexterror();
	}
	tree = "main";
	if((spec = strchr(s, '!')) != nil){
		*spec++ = 0;
		if(*spec != 0)
			tree = spec;
	}
	off = 0;
	size = 0;
	if(cistrncmp(s, "PARTUUID=", 9) == 0){
		if(!rootdisk(s, dev, sizeof dev, &off, &size)){
			poperror();
			free(s);
			return 0;
		}
	}else if(strchr(s, '=') != nil){	/* UUID=, LABEL=: ext4's */
		poperror();
		free(s);
		return 0;
	}else
		kstrcpy(dev, s, sizeof dev);
	c = namec(dev, Aopen, rw ? ORDWR : OREAD, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	if(!isgefs(c, off)){
		poperror();
		cclose(c);
		poperror();
		free(s);
		return 0;
	}
	if(size == 0){
		d = dirchanstat(c);
		size = d->length;
		free(d);
	}
	m = gefsserve(dev, c, off, size, rw, "#s/gefs.root.cmd");
	poperror();	/* c is gefs's now */
	if(!waserror()){
		gefspost(m, "#s/gefs.root");
		poperror();
	}
	r = mntattach(m, nil, tree, 0);
	cclose(m);
	if(waserror()){
		cclose(r);
		nexterror();
	}
	c = namec("/", Amount, 0, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	cmount(r, c, MBEFORE, nil);
	poperror();
	cclose(c);
	poperror();
	cclose(r);
	poperror();
	free(s);
	return 1;
}

static Chan*
gefsattach(char*)
{
	error(Enoattach);
}

static Walkqid*
gefswalk(Chan*, Chan*, char**, int)
{
	error(Egreg);
}

static int
gefsstat(Chan*, uchar*, int)
{
	error(Egreg);
}

static Chan*
gefsopen(Chan*, int)
{
	error(Egreg);
}

static void
gefsclose(Chan*)
{
}

static long
gefsread(Chan*, void*, long, vlong)
{
	error(Egreg);
}

static long
gefswrite(Chan*, void*, long, vlong)
{
	error(Egreg);
}

Dev gefsdevtab = {
	'G',
	"gefs",

	devreset,
	devinit,
	gefsshutdown,
	gefsattach,
	gefswalk,
	gefsstat,
	gefsopen,
	devcreate,
	gefsclose,
	gefsread,
	devbread,
	gefswrite,
	devbwrite,
	devremove,
	devwstat,
};
