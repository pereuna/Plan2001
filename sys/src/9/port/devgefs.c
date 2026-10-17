/*
 * #G: gefs in the kernel (../gefs, docs/gefs.md), the root's file
 * system.  gefs runs as kprocs and serves 9P on a pipe; rootmount mounts
 * the other end before / for plan9.ini's root=:
 *
 *	root=-w PARTUUID=guid		its tree main
 *	root=-w #S/sdE0/data!adm	another tree (snapshot label)
 *
 * A PARTUUID is looked for on the disks of #S: each GPT partition, read
 * from the disk's data file, so no partitions need be set up.  The
 * console (halt, check, show) is /srv/gefs.root.cmd, which fshalt halts.
 * /srv/gefs.root is its 9P (eve's): mount it with a tree's name (mount
 * /srv/gefs.root /n/adm adm), whose ctl file makes snapshots.  The device
 * has nothing of its own but the shutdown: at reboot gefs is halted
 * (synced).
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

static uvlong
le(uchar *p, int n)
{
	uvlong v;

	v = 0;
	while(n-- > 0)
		v = v<<8 | p[n];
	return v;
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
 * The disk c's GPT partition guid: its offset and size; 0 if none.
 * Logical blocks of 512 or 4096 bytes, as the header is found.
 */
static int
gptpart(Chan *c, char *guid, vlong *off, vlong *size)
{
	uchar h[92], *e, *ents;
	char s[37];
	vlong lb, elba;
	ulong ne, esz;
	int i, bs, found;

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
	found = 0;
	for(i = 0; i < ne && !found; i++){
		e = ents + i*esz;
		if(le(e, 8) == 0 && le(e+8, 8) == 0)	/* unused */
			continue;
		guidstr(s, e+16);
		if(cistrcmp(s, guid) == 0){
			*off = le(e+32, 8)*bs;
			*size = (le(e+40, 8) - le(e+32, 8) + 1)*bs;
			found = 1;
		}
	}
	free(ents);
	return found;
}

/* The disk of #S (#S/sdXX/data, in dev) with the partition guid */
static int
scandisks(char *guid, char *dev, int ndev, vlong *off, vlong *size)
{
	Chan *top, *c;
	uchar *buf;
	Dir d;
	char strs[256];
	vlong doff;
	long n, m;
	int i, found;

	found = 0;
	top = namec("#S", Aopen, OREAD, 0);
	buf = smalloc(8192);
	if(waserror()){
		cclose(top);
		free(buf);
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
			found = gptpart(c, guid, off, size);
			cclose(c);
		}
	}
	poperror();
	cclose(top);
	free(buf);
	return found;
}

/*
 * root= without -w: DEVICE or PARTUUID=guid, then !tree; mounted
 * before / (the root's /rc and so on, not #/'s empty mount points of
 * those names; #/ gives those it lacks: /dev, /env, ...).
 */
static void
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
		if(!scandisks(s+9, dev, sizeof dev, &off, &size))
			error("no disk has that partition");
	}else
		kstrcpy(dev, s, sizeof dev);
	c = namec(dev, Aopen, rw ? ORDWR : OREAD, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	if(!isgefs(c, off))
		error("no gefs there");
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
}

/*
 * plan9.ini's root= ([-w] PARTUUID=guid|DEVICE [!tree]): the kernel's
 * gefs mounted before / in the first process, before it runs init
 * (init0, docs/init.md).  The host owner is user= (as factotum -u would
 * make it).  1 if done.
 */
int
rootmount(void)
{
	char *s, *u;
	int rw;

	if((s = getconf("root")) == nil)
		return 0;
	if((u = getconf("user")) != nil && *u != 0){
		kstrdup(&eve, u);
		kstrdup(&up->user, u);
	}
	rw = strncmp(s, "-w ", 3) == 0;
	if(waserror()){
		print("root=%s: %s\n", s, up->errstr);
		return 0;
	}
	gefsroot(rw ? s+3 : s, rw);
	poperror();
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
