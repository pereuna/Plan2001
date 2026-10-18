# Vikoja 9frontille

Plan2001:ssä löytyneet viat 9frontin koodissa, kerätty tänne ja
lähetetään 9frontille kerralla (vielä lähettämättä). Jokaisesta:
oire, syy, korjaus, toisto ja tila 9frontin HEADissa. Raportit ovat
englanniksi, jotta ne voi lähettää sellaisenaan. Lisää uudet tänne;
merkitse lähetetyt.

Tarkistettu 17.10.2026: 9front HEAD `a411fb63c` (2026-10-16), julkaisu
9front-11952. Plan2001: github.com/pereuna/Plan2001.

---

## Bugs found in 9front by Plan2001

All of these were found while running gefs as the root file system of a
fork of 9front (in its kernel, as kprocs; the code is the same as
/sys/src/cmd/gefs) and while testing it (a kernel test suite, crash
tests by killing QEMU). Every gefs bug below is present in 9front HEAD
a411fb63c (2026-10-16); line numbers are from there. The Plan2001
commits named carry the fixes, for both the kernel copy and
/sys/src/cmd/gefs unless noted.

### gefs 1: a rename can be answered as done without being done

`fs.c:1768` (fswstat). After a rename, `de->Xdir = n` leaves the cached
Dent's `name` pointing at the Twstat's stat strings (`n.name = d.name`,
the `strs` buffer on fswstat's stack); `cpkey` copies only the key into
`de->buf`. If the same Dent is still cached at the next rename of that
file (a fid kept open, or a delayed clunk), `strcmp(d.name, de->name)`
reads freed or rewritten memory, typically the new name itself, so
`rename` stays 0: an Owstat is applied, Rwstat is returned, and the file
keeps its old name. The next walk to the new name fails.

Repeat: create a file, keep it open, rename it a -> b -> a with
dirwstat, checking after each that the new name exists: the second
rename "succeeds" and the name is still b. Quick renames of many files
(a0..a49 back and forth 20 times) lose names now and then, depending on
timing.

Fix (Plan2001 0ace6d3): the name from the dent's own key, as getdent
sets it:

	de->Xdir = n;
	if(rename)
		cpkey(de, &k, de->buf, sizeof(de->buf));
	de->name = de->buf + 11;

### gefs 2: truncation frees no blocks, and extending shows old data

`fs.c:1631` and `fs.c:3144`. A wstat that shrinks a file sends AOclear
with `off = d.length`, and runsweep clears keys from `am->off` in steps
of Blksz. Data keys are block offsets, so when the new length is not a
multiple of Blksz no key matches: no block is freed (space leaks), and
a later wstat that extends the file reads the old data back. Even with
an aligned start, the last partial block keeps its old tail beyond the
new length, which a later extension or write into that block exposes.

Repeat: write 1000000 bytes, wstat length 100000, wstat length 200000,
read 100000..160000: not zero.

Fix (Plan2001 e89a17c): runsweep clears from the next block boundary,

	for(off = (am->off + Blksz-1) & ~(Blksz-1); off < am->end; off += Blksz){

and fswstat, when the new length is inside a block, writes a new block
for it with the tail zeroed (writeb with zeros from the new length to
the end of the block or the old length), in the same upsert as the
Owstat. (Careful: `n.length` is unsigned; compare as vlong.)

### gefs 3: after a crash, the first sync frees a free block

`blk.c:358` (loadlog, case LogSync). Replaying the allocation log stops
at the sync barrier of the last committed sync and truncates the tail
block there, but only in memory (`b->logp = Zb; b->logsz = i`). On the
disk the tail still chains on to log blocks allocated after the barrier,
which the replay now has as free. A walk of the log chain from the disk
follows them: check's checklog caches them, and runsweep's free of the
old log after compresslog (the first sync after a load compresses)
frees one that is already free. Plan2001's kernel then hit the
`s->off+s->len < r->off` assert in freerange; the userspace gefs, the
`b->bp.gen == bp.gen` assert in getblk (a cached stale block reused).

Repeat: write files for a while, kill the machine (QEMU) between syncs,
restart, run check, remove some files, sync. Not every time; with
writes over 10-20 seconds before the kill, most times.

Fix (Plan2001 30c19de): write the truncated tail back at once when the
file system is writable:

	setflag(b, Bdirty, 0);
	if(!agetl(&fs->rdonly)){
		finalize(b);
		syncblk(b);
	}
	return;

### gefs 4: writes to a tree since its last sync are lost at its last clunk

`fs.c:749` (clunkmount). When the last fid of a mount is clunked, the
Mount is taken off `fs->mounts` and freed. sync only updates the labels
of the mounts on that list, so a mutable tree written since the last
sync loses those writes. Unmounting and halting within the 5 second
sync interval lost a whole copied tree.

Repeat: gefs -n x; mount /srv/x /n/x; copy a tree in; unmount /n/x;
echo halt >>/srv/x.cmd (all within 5 seconds); restart: the tree is
empty.

Fix (Plan2001 4038669, kernel copy only): keep a dirty mutable tree on
the list until a sync has put it in its label:

	if(aincl(&mnt->ref, -1) == 0){
		if((mnt->flag & Lmut) && ((Tree*)agetp(&mnt->root))->dirty){
			qunlock(&fs->mountlk);
			return;
		}
		...

### gefs 5: smaller ones

- `user.c` slurp: `b = getblk(bp, GBraw)` without `dropblk(b)`: a block
  reference leaks per block of /adm/users at every load (Plan2001
  4038669, kernel copy).
- `blk.c:84` readblk: `error("invalid block type %d @%llx", b->type, bp)`
  passes the Bptr struct to %llx; `bp.addr` meant (Plan2001 4038669,
  kernel copy).
- check.c: checktree skips the free-list check of pointers with
  `gen <= pred` and of each pivot's last child, and checklog does not
  check log blocks against the free list, so the state of gefs 3 passed
  check. Checking every pointer (old generations too, the last child,
  the log chain) found it at once (Plan2001 30c19de, kernel copy).

### lwext4 in ext4srv

`/sys/src/cmd/ext4srv` (unchanged in HEAD since 9front-11952). Found
while Plan2001 had lwext4 in its kernel; fixed in Plan2001's copy and
carried to ext4srv in Monolith (github.com/pereuna/monolith, branch
ext4srv-fixes f7ff2ca: ext4.c, ext4_fs.c, ext4_dir.c):

1. `ext4_fread`: a hole (or an unwritten extent) reads block 0 of the
   device instead of zeros, in the multi-block path and in the last
   block, so sparse files read wrong (mkfs.ext4 -d makes holes of zero
   blocks, so binaries copied that way are corrupt).
2. `metadata_csum_seed` (Debian's mkfs.ext4 default): the checksum seed
   is the superblock's s_checksum_seed, not the crc of the uuid.
3. A writable mount sets ERROR_FS in the superblock state ("mounted");
   Linux and e2fsck read it as errors found. Linux clears VALID_FS for
   the mount and sets it back at umount.
4. Directory entry names longer than `de.name` (a revision 0 entry's
   16-bit length) are copied past it.
5. Checksum failures of inodes, group descriptors and directory leaves
   are only debug warnings; they should be errors (when no other
   reference to the block is changing it: lwext4 sets the checksum at
   put).
6. `ext4_fwrite` does not mark the inode dirty when only mtime changes,
   leaving the new mtime under the old checksum.

### sdnvme: synchronize cache is not done

`/sys/src/9/port/sdnvme.c`: SCSI synchronize cache (0x35) is answered
as done without an NVMe Flush, so data a file system has synced can
still be in the controller's volatile write cache at a power cut.

Fix (Plan2001 9947779; its earlier history is in pereuna/Plan2001-history): send Flush (opcode 0) for the namespace:

	e = qcmd(&ws, ctlr, 0, 0x00, ctlr->nsid[u->subno], nil, 0);
	checkstatus(wcmd(&ws, e), "flush");

### cc: a static initializer of a flexible array member changes the struct

`/sys/src/cmd/cc/dcl.c:492` (init1, TARRAY). A flexible array member
(`T a[];`, width 0) initialized in a static declaration gets
`t->width = mw+w` set on the member's type, which is the struct's own
member: the struct type is changed for every later use. The next
static initializer of the same struct type with more elements stops
at the new width ("more initializers than structure"); one with fewer
or the same compiles, but the symbol's size is still the struct's
without the array, so the linker refuses the data past it
("initialize bounds"), or with only one such initializer the array
silently runs past the symbol.

Repeat:

	struct S { int n; int a[]; };
	struct S x = { 1, { 1, 2 } };
	struct S y = { 2, { 1, 2, 3 } };	/* more initializers than structure: y */

Found porting MicroPython (its type objects and qstr pools are such
structs). Not fixed in Plan2001 yet: the port sizes the arrays instead.
The fix is for init1 to work on a copy of the member type and to give
the symbol a copy of the struct type widened by the array.

### 6c: float comparisons ignore unordered (NaN) operands

`/sys/src/cmd/6c/txt.c` gopcode (OEQ ... OHI): a float comparison is
UCOMISS/UCOMISD followed by a single conditional jump (JEQ, JNE, or
the unsigned JCS/JLS/JCC/JHI after cgen.c's logrel/invrel swap). For
an unordered result UCOMISD sets ZF, PF and CF together, and no jump
looks at PF: `x == x` is true and `x != x` false for a NaN, and
`n < 1` is true or false depending on how boolgen ordered the
operands. IEEE 754 (and C99 Annex F) want every relation false and
`!=` true. A fix needs PF: for == a JPS over the JEQ; for != (and a
complemented relation, boolgen's comrel when `true`) a JPS to the
same target as the jump, so gopcode has to know whether the relation
was complemented.

Repeat (prints 1 0, wants 0 1):

	double z = 0; double n = z/z;	/* after setfcr(getfcr() & ~FPINVAL) */
	print("%d %d\n", n == n, n != n);

### 6c: -x of a float is 0-x, so -(+0.0) is +0.0

A float negation is compiled as a subtraction from zero, which gives
+0.0 for x = +0.0 (IEEE: -0.0). copysign, 1/x and printing see the
difference (Python's -0.0, math.copysign(1, -0.0)). A fix: XOR the
sign bit (XORPD with a constant), as other compilers do.

Found by tools/mptest (MicroPython's float tests on Plan2001, 18.10.2026).
Plan2001 fixes both in the compiler: boolgen tells gopcode (the global
nanjump) whether the branch is to be taken for unordered operands, and
fbranch adds a JPS over the branch or to it (6c/cgen.c, txt.c; peep.c
knows JPS/JPC); cc/com.c makes a float's -x into -1*x, and scon.c folds
-x as -1.0*x, so that a compiler built by an unfixed one folds -0.0
right too. MicroPython keeps its workaround (MICROPY_FLOAT_CC_FIXES)
for code built before.

### 6c, cc, 6l: the constant -0.0 becomes +0.0

Three places compare a float constant with 0 and so take -0.0 for
+0.0: 6c/txt.c gmove loads a constant `== 0.0` with XORPD (+0.0);
cc/pswt.c ieeedtod, which writes a constant into the object file,
tests `native < 0` for the sign and writes 0 for `native == 0`; 6l's
ieeedtof (a float constant) returns 0 for any zero high word and
calls -0.0 an overflow. `return -0.0;` in musl's atan2 (atan2(-0.5,
inf)) gave +0.0. Plan2001 takes the sign from the bits in all three.

### sdiahci: a disk that the firmware has not used never gets ready (QEMU)

`/sys/src/9/pc/sdiahci.c` checkdrive, state Dnew with the phy up: it
waits for DRDY in the task file of an ATA signature. QEMU's AHCI disk
reports 0x30 after its reset (error 1, the reset's diagnostic code,
and no DRDY) until a command comes. When UEFI booted from the AHCI
disk, the firmware had sent commands and it worked; booted from another
disk (virtio), every AHCI disk went from "slow reset" to portreset over
and over, and iaonline, which waits while waitready says reset, never
returned: the boot hung where the kernel first read #S. Plan2001 takes
error 1 with BSY clear as ready too. (An HBA reset at attach did not
help: the disks report the same after it.)

### ip: Maxincall 10

`/sys/src/9/ip/ip.h`: ten calls waiting for a listen; the eleventh at
once gets a reset ("Fsnewcall: incall queue full"). A server that forks
for each call (or is slow to listen again) loses connections under
modest load: 32 clients at once to an echo server lost 2. Plan2001 has
128.
