# GE + FS + O: what the kernel implements now

**Scope:** the first delivery stage of [GE + FS + O](ge-fs-o.md) and the
object-store part of the second.  It is the *engine's* separation: a
volume is an instance, its state is the volume's, and no code reaches a
volume through a global.  The native object format, the object catalog
and booting an init Space from an object partition (stages 3 and 4 of the
specification) are **not** implemented; §4 below says what is missing and
why the format work is deliberately not started in this change.

## 1. The three components in the tree

| Component | Where | What it owns |
|---|---|---|
| **GE** | `sys/src/9/gefs/` `kern.c`, `blk.c`, `cache.c`, `tree.c`, `snap.c`, `load.c`, `pack.c`, `hash.c`, `avl.c`, `error.c` | volume I/O, block allocation and reclamation, the block cache, trees, snapshots, sync, epochs, the key/value tree, crash recovery |
| **FS** | `fs.c`, `ctl.c`, `cons.c`, `user.c`, and the `DMOBJ` entry points of `obj.c` | the 9P file-system interface, names and directories, permissions, users, the legacy `DMOBJ` file |
| **O** | the store engine in `obj.c` (versions, publication, abort, retention) | object records, OID/version lookup, writer exclusion, publication and history |

O's engine is compiled once and is what the `DMOBJ` file maps onto
(`fs.c`'s `objwrite`/`objread`/`objclunk` adapter).  There is one
transaction/abort/history/pin implementation, as the specification
requires; FS adds file identity on top of it.

## 2. GE instances: the same code, no shared state

`struct Gefs` (`gdat.h`) is the GE instance.  It holds everything that
used to be a global in `kern.c`, `blk.c`, `obj.c` or `check.c`, and it is
now the only way to name a volume:

- the volume's kernel glue (`Gefs.k`, a `Gkern`): its file descriptors,
  its console's srv file, the disk's raw file for cache flushes, its
  halt/halting state.  `gopen`, `gread`, `gwrite`, `gclose`,
  `gdirfstat` and `gflushdev` take the volume (`gefs.h`'s `open`, `read`,
  `write`, … macros bind the one in scope);
- its syncing and sweeping processes (`syncproc`, `sweepproc`), so a
  full volume's reserve is that volume's;
- its object transactions (`otxs`, `obj.c`), under its own `mutlk`;
- the state of a check that runs on it (`ck`, `check.c`), so two consoles
  may check two volumes;
- its cache budget accounting (see §3).

`gefsserve()` (`kern.c`) allocates one instance per call.  There is no
"one at a time" restriction and no `Gefs *fs` global.  Each instance's
kprocs are named `gefs<id><kind>` (`gefs1cons.0`, `gefs1read.1`, …), and
its console is posted at the srv file the caller names.  `devgefs.c`
keeps the handles of the volumes it started (`Maxvols`, 4) and halts each
at reboot.

## 3. How a function names its volume

The specification allows an explicit context "directly or through a
handle bound to that context".  Both are used:

- **A handle bound to the volume.**  The objects a volume owns carry it:
  `Limbo` (the base of `Blk`, `Tree`, `Mount`, `Dlist`, `Bfree`), and
  `Fid`, `Conn`, `Dent`, `Arena`, `Syncq`, `Scan` and `Otx` have a
  `Gefs *fs`.  A function that already receives one of these derives the
  volume from it, e.g. `Gefs *fs = b->fs;` in `syncblk`, `fs = t->fs` in
  the tree functions, `m->conn->fs` in the 9P handlers.
- **An explicit parameter.**  The functions that have no such object take
  `Gefs *` first: `getblk(fs, bp, flg)`, `cacheget(fs, addr)`,
  `opensnap(fs, name, &flg)`, `opentree(fs, gen)`, `newconn(fs, …)`,
  `getmount(fs, name)`, `checkfs(fs, fd)`, `freeblk(fs, t, b)`,
  `freebp(fs, t, bp)`, `killblk(fs, t, bp)`, `broke(fs, …)`,
  `_trace(fs, …)`, the `run*` kproc entry points, and the console and
  dump helpers.

`freeblk`/`freebp`/`killblk` take the volume as well as the tree because
the tree may be nil there (a free with no tree is used by the sweeper and
the log compaction); the volume is not.

The formatter callbacks `%M`/`%P` (`dump.c`) run with no volume by
design: they decode a `Kent` value themselves (`showkvdir`), print to the
kernel console or a formatter, and never mark a volume read-only.

### Cache budget

The specification requires the instances' caches to be budgeted
together.  `kern.c`'s `cachemb()` now hands out from one kernel-wide
budget (`plan9.ini`'s `gefscache=`, else an eighth of memory, 16–512
MiB): the first volume gets the whole budget, a later one gets what is
left, and if less than 16 MiB is left the volume is refused with
`gefs: no cache left for another volume (set gefscache=)` instead of
taking the heap past the budget.  A machine that runs several volumes
sets `gefscache=` for all of them; dividing the budget more evenly is
part of the second-volume work below.

## 4. What is not implemented

1. **A second volume's entry point.**  The engine supports N instances,
   but the only caller of `gefsserve()` is `rootmount()` (`devgefs.c`).
   Nothing in the kernel can start an object volume on another partition
   yet, and `-O VOLUME` (§5 of the specification) is not implemented.
   The natural next step is a `#G` control file (`add PARTUUID=…`) that
   calls `gefsserve()` and posts the volume's srv file; it is what the
   GFO.2 experiment (FS and O on different partitions at once) needs.
2. **Native O handles.**  O is still reached through the `DMOBJ` file and
   a `Fid`; `objread`/`objwrite` take `Fmsg`/`Fid`.  The native
   volume/Space handle, the flat Space-name catalog, writer exclusion
   without the `lock` file, and ownership without parent directories are
   stage 2 work.
3. **The native O format.**  The specification requires the byte layout
   and compatibility rules to be documented and reviewed before
   format-changing implementation; none of that was done here, so no new
   profile or catalog was written.  Existing Plan2001 FS volumes and
   their `DMOBJ` stores are unchanged (GFO.1).
4. **Boot from O.**  `-S NAME` restoring a Space from an object volume,
   the preparation tool, the recovery console contract and the related
   acceptance tests (GFO.3–GFO.11) are not implemented.

## 5. What was verified

- `tools/build.sh WHAT=kernel`: the kernel, loader, kbdfs and ktest
  build (`6c`/`6l`, including 6c's cross-module type signatures).
- `tools/test-gefs.sh CRASH=1 KTEST=all` on the final build (and
  `CRASH=2` on the build before it): boot from a gefs root, writes, a
  compile-and-run, the console's `check`, an `adm` snapshot, `fshalt`,
  `gefsck`, the crash boots and every kernel test (31 `KTEST … OK`),
  including `fsobj` (the `DMOBJ` store) — `PASS: gefs root`, no failure.
- `tools/test-space.sh PHASE=5 CRASH=1`: the Space/V5 object-store tests
  up to `T5.11` (the run was stopped by its time limit; no test failed).
  The tests exercise `obj.c`'s versioning, publication, retention and
  pins through the `DMOBJ` adapter on the refactored kernel.  It ran on
  the build before the shared cache budget; that change leaves a single
  volume's cache at the reported 254 MiB, as the boot log shows.

The tests' expected boot line is now `gefs[0-9]*: … read-write`
(`tools/test-gefs.sh`, `tools/test-mpinit.sh`, `tools/test-space.sh`):
the line carries the instance number.

## 6. Risks and follow-ups

- **Object `fs` bindings are load-bearing.**  A new object must set its
  volume when it is made (`initfs`, `getmount`, `getdent`, `newconn`,
  `dupfid`, `getdl`, `opentree`, `qinit`, `btnewscan`, `gettx`, …), and
  a decoder that memsets its result must put the volume back
  (`unpackarena`, `unpacktree`).  The runtime fault this caused while
  developing the change (`a->fs` wiped by `unpackarena`) is fixed; a new
  constructor that forgets it fails as a nil dereference, not silently.
- **Multi-volume testing is missing** until (1) above exists; the cache
  split and the per-volume console/fd isolation have no QEMU test yet.
- The second stage's format work should start with the format document
  and the checker (`tools/gefsck`) changes, as the specification
  requires, not with the kernel.
