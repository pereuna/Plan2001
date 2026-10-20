# O Volume Requirements: Direct Partition-Backed Object Storage

**Status:** requirements.  No native O volume is implemented; this document
states what must be true before O can be used directly from a partition, in
the way the V7 object store is used from a file today.

**Baseline:** `docs-ge-fs-o` at `ca6b2be` plus the V7 P1/P2 fixes in
`docs/reviews/2026-10-20-v7-pr2-p1-p2-review-fixes.md`.

**Related documents:** [GE + FS + O](ge-fs-o.md),
[what the kernel implements now](ge-fs-o-impl.md),
[the object-store protocol](gefs-obj.md),
[the object model](object-model.md), [GEFS](gefs.md), and
[V7 upgrade](v7-upgrade.md).

## 1. Scope and definitions

- **GE** is the shared storage engine: block allocation, checksummed block
  pointers, the key/value tree, snapshots, sync, recovery, versions, and
  the common crash mechanisms.  A GE instance is a volume
  (`struct Gefs`).
- **FS** is the legacy Plan2001 GEFS file system: names, directories,
  permissions, 9P, and the `DMOBJ` adapter.
- **O** is the object-storage layer: Space identities, object records,
  OID/version lookup, writer exclusion, publication, history, pins, and
  the Environment/code dependencies of a Space.
- **V7 store on a file** is the current object store: a `DMOBJ` file at
  `/usr/$user/space/<name>/obj`, interpreted by `obj.c`, with the V7
  header (`fmt 2`, recovery pin), the internal `OID_UPGRADE_META` epoch
  catalog, and named `O_FUNN`/`O_CLASSN` records.
- **Direct partition** means an O volume that GE can open from a block
  device or partition by its own profile and version, without an FS
  mount, an FS file, a file-server `Fid`, a `qid`, or a path under
  `/usr/$user/space`.

This document does not claim that any requirement below is implemented.
The current implementation is described in section 2.

## 2. Current state

| Area | What exists now | Evidence |
|---|---|---|
| GE instances | `gefsserve()` creates one `Gefs` per volume: file descriptors, console, halt state, raw disk file, sync/sweep kprocs, object transactions, check state, cache.  `devgefs` keeps up to four handles for shutdown. | `sys/src/9/gefs/kern.c`, `sys/src/9/port/devgefs.c` |
| Second volume | There is no entry point.  The only caller of `gefsserve()` is `rootmount()`.  `#G` has no attach/control file. | `docs/ge-fs-o-impl.md` 4.1; `devgefs.c:276` |
| GE cache | One kernel-wide budget; the first volume takes all of it, so a later volume receives less than the minimum and is refused. | `kern.c:871-948`; `docs/reviews/2026-10-20-v7-and-ge-fs-o.md` GE-1 |
| FS side | GEFS root on a GPT partition, `root=-w PARTUUID=...`, tested in QEMU with a partition image (ESP + GEFS) on virtio-blk.  Real p5 has an older GEFS root from 17.10.2026; the GE+FS+O kernel has not been run on it. | `tools/test-gefs.sh`, `tools/gefsdisk`; `docs/gefs.md`; `docs/ge-fs-o-impl.md` 5 |
| O storage | O is a `DMOBJ` file in the FS tree.  The store identifier is the file's `qid.path`; transactions, cursors, and reads use `Fid` and `Fmsg`. | `docs/gefs-obj.md` 1, 3.2; `obj.c:353-360`, `549`, `703` |
| Space names and locks | A Space is `/usr/$user/space/<name>/`; the writer holds a `DMEXCL` `lock` file.  The name is a directory name, not a flat O catalog entry. | `p9space.c:483-485`, `1038-1061` |
| Code and Environment | `.py`, `.mpy`, and Environment text live as files under `/usr/$user/lib`. | `p9space.c:125-187`; `docs/object-model.md` 6.1 |
| V7 store on a file | Working and QEMU-tested: versions, `K`, recovery pin, `OID_UPGRADE_META` epochs, named records, `space.upgrade`, P1/P2 fixes. | `docs/v7-upgrade.md`; `tools/test-space.sh` V7 |
| Boot from O | Not implemented.  `root=` selects a legacy FS; `-S` selects a legacy FS-backed Space.  `-O` does not exist. | `docs/ge-fs-o-impl.md` 4.4 |
| O checker and preparation | Not implemented.  `tools/gefsck`/`gefslog` validate GEFS FS images. | `tools/gefsck`, `tools/gefslog` |
| FS and O concurrently | Not implemented; no second-volume entry point and no cache split.  There is no QEMU test with two volumes. | `docs/ge-fs-o-impl.md` 6 |

## 3. Gap summary

| ID | Requirement | Current state |
|---|---|---|
| O-1 | O volume format/profile/version, format and validation tools | Missing |
| O-2 | Native O volume and Space handles, no FS `Fid`/file dependency | Missing |
| O-3 | Flat Space-name catalog with stable Space IDs | Missing |
| O-4 | Writer exclusion, ownership, permissions without FS | Missing |
| O-5 | V7 object semantics (OID/version/history/K/pin/epochs/named records) on O | Engine exists; O volume integration missing |
| O-6 | Immutable Environment/code blobs in O with dependency retention | Missing (currently FS files) |
| O-7 | Boot contract `-O VOLUME` / `-S NAME`, init Space, recovery console | Missing |
| O-8 | GE multi-volume lifecycle, cache budget, per-volume user state, `#G` entry point | Missing |
| O-9 | O checker, preparation tool, crash/recovery and cross-volume tests | Missing |
| O-10 | Compatibility with V5/V6/V7 `DMOBJ` stores and explicit migration | FS-side V7 exists; O-side migration missing |
| O-11 | Acceptance matrix, QEMU gates, separately reported hardware run | Not run |

## 4. Requirements

The requirements below use MUST for release gates and SHOULD for desirable
properties.  Each requirement is satisfied only when its acceptance test is
in the tree and has passed.

### O-1. Volume format, profile, and version

- **O-1.1 MUST** define an unambiguous O volume profile identifier and a
  format version, with a superblock (or equivalent) at a documented
  location, length, checksums, and reserved fields.
- **O-1.2 MUST** define the byte layout, alignment, endianness, allocation
  metadata, catalog location, version metadata, and compatibility rules in
  a format document, and have that document reviewed before
  format-changing implementation begins.  This is the same gate the
  specification requires for `docs/ge-fs-o.md` 4.
- **O-1.3 MUST** reject an FS volume presented as an O volume and an O
  volume of an unknown/unsupported profile.  Opening a device MUST NOT
  format it or convert it implicitly.
- **O-1.4 MUST** provide a preparation/format tool and a read-only checker
  for the O profile; neither may be the only way to read a valid volume.
- **O-1.5 MUST** preserve the existing Plan2001 FS profile and V5/V6/V7
  `DMOBJ` stores unchanged.
- **Acceptance:** prepare an empty O volume; open it with the kernel O
  path; check it clean; reject an FS volume and a corrupted profile; read
  an existing FS volume and its `DMOBJ` store unchanged.

### O-2. Native O volume and Space handles

- **O-2.1 MUST** provide an O volume handle that is opened from a block
  device/partition (or `PARTUUID`) and owns the volume's recovery, writer
  state, catalog, and allocation metadata.
- **O-2.2 MUST** provide O Space handles that do not require an FS file
  entry, directory, file `qid`, file-server `Fid`, or 9P request.
  `objread`/`objwrite`'s `Fid`-based API may remain for the legacy
  `DMOBJ` adapter, but native O MUST use the native handle.
- **O-2.3 MUST** make object get/put/commit/abort/scan take the native
  volume/Space handle, so that FS and O do not share allocation or
  publication state.
- **O-2.4 MUST** define open/close/halt/failure semantics: a failed open
  leaves the previous published version intact; closing releases the
  writer's resources; a crash is recovered on the next open.
- **Acceptance:** open, write, commit, read, and close an O Space from the
  native handle with no FS mounted; kill the process before and after
  publication and verify the old or new version is whole.

### O-3. Flat Space catalog and stable identities

- **O-3.1 MUST** maintain a flat catalog mapping complete Space names to
  stable Space identifiers.  A name such as `/lib/net/sdf` is one name;
  slash is an ordinary character.  Parent traversal and directory
  creation are not part of O.
- **O-3.2 MUST** validate catalog order, uniqueness, length, checksums,
  and version coverage.  A corrupt or inconsistent catalog fails closed.
- **O-3.3 MUST** keep catalog entries needed to interpret the retained
  versions `[K..N]` and the recovery pin; removing a catalog entry must
  not make a retained version unreadable.
- **O-3.4 MUST NOT** reuse `OID_UPGRADE_META` as the Space-name catalog.
  That object is the Environment-epoch catalog of one store; the native O
  volume needs its own defined catalog.
- **Acceptance:** create and reopen `/lib/net/sdf` with no `/lib` or
  `/lib/net` object; catalog corruption is rejected; retained versions
  still resolve after many ordinary commits.

### O-4. Writer exclusion, ownership, and permissions without FS

- **O-4.1 MUST** implement writer exclusion in O, not with a `lock` file,
  and refuse a second writer deterministically.
- **O-4.2 MUST** store ownership and access metadata in O; permission
  checks MUST work without parent directories and without inheriting FS
  directory permissions.
- **O-4.3 MUST** define how users and groups are resolved without an FS
  `/adm/users` file.  The O format document MUST name the source of
  identity (for example an O user table) and its update rules.
- **O-4.4 MUST** make releasing a process's handles release its writer
  ownership, including after a crash.
- **Acceptance:** a second writer is refused; a reader sees a consistent
  published version; ownership/access tests pass with no FS mounted; a
  process death releases the writer without an FS lock cleanup.

### O-5. Object records and V7 semantics

- **O-5.1 MUST** reuse the existing object engine's semantics: OIDs,
  per-object versions, atomic publication, abort, history `K`, recovery
  pin, `OID_UPGRADE_META` epochs, and named `O_FUNN`/`O_CLASSN` records.
  There MUST NOT be a second implementation of transaction/abort/history/
  pin logic.
- **O-5.2 MUST** keep OID identity stable for surviving mutable objects,
  classes, and persistent named functions, including methods and
  static/classmethod wrappers, as fixed by the V7 P1/P2 review.
- **O-5.3 MUST** preserve the V7 pin invariant `K <= pin <= N` and make
  historical reads select the Environment effective at the selected
  version.
- **O-5.4 MUST** make a crash before publication expose the previous
  version and a crash after publication expose the new version, never a
  mixture, with the same boundaries as the V7 store.
- **Acceptance:** the V7 tests (upgrade, method aliases and OIDs, ambiguous
  names, epoch catalog, old-state policy, code provenance, 100 commits,
  pin replacement, crash) run against native O with no FS.

### O-6. Environment and code storage in O

- **O-6.1 MUST** store immutable Environment text, source, and `.mpy` bytes
  as O objects with explicit digests and ABI/runtime checks.
- **O-6.2 MUST** retain the Environment/code blobs required by every
  retained version and the recovery pin; reclamation MUST NOT remove a
  blob used by another Space or a retained version.
- **O-6.3 MUST** reject missing, corrupt, swapped, or ABI-incompatible
  code before publication or boot.  It MUST NOT fall back to unrelated
  files on FS or `sys.path`.
- **O-6.4 MUST** apply the V7 provenance rule (source recompiled under the
  definition's source name and compared byte-for-byte with the stored
  `.mpy`) or an equivalent verified digest/manifest.
- **Acceptance:** boot and upgrade with O-resident code and no FS; missing
  or tampered code is rejected; a pinned version still loads its old code
  after 100 commits and a reboot.

### O-7. Boot contract

- **O-7.1 MUST** add `-O VOLUME` (PARTUUID or device) to select an O volume
  and `-S NAME` to select and restore a Space on it.  Legacy `-S` on an FS
  volume remains supported during the transition.
- **O-7.2 MUST** boot without `root=` and without an FS partition:
  kernel/loader from the ESP, embedded MicroPython and `kbdfs`, then open
  O, recover, validate Environment/code/ABI, restore the graph, and call
  the restored global `run()` without re-running module initialization.
- **O-7.3 MUST** fail closed on a missing volume, missing Space, missing or
  corrupt code, wrong ABI, writer conflict, or full volume, and enter a
  recovery console.  It MUST NOT initialize a volume or fall back to
  another code source.
- **O-7.4 MUST** keep the failed writer's lock out of the recovery console
  and must not discard a recovery pin on a failed boot.
- **Acceptance:** GFO.3, GFO.4, GFO.9, and GFO.10 on QEMU, then hardware.

### O-8. GE integration and multi-volume lifecycle

- **O-8.1 MUST** provide a second-volume entry point.  The natural one is a
  `#G` control interface (`add PARTUUID=... NAME`, `list`, `remove`) that
  calls `gefsserve()`, posts the 9P srv file and the volume's console, and
  registers the volume for shutdown.
- **O-8.2 MUST** replace the first-takes-all cache allocator with a
  per-volume share of a kernel-wide budget, so two or more volumes can run
  with at least the minimum cache.  `gefscache=` remains the total.
- **O-8.3 MUST** move `noneid`, `nogroupid`, `admid`, and other volume
  semantic state into the `Gefs` instance, so loading the second volume's
  users cannot change the first volume's access decisions.
- **O-8.4 MUST** allow FS and O to operate on separate partitions of the
  same disk concurrently, including independent writes, sync, halt,
  recovery, writer locks, and allocation metadata.
- **O-8.5 MUST** make a fault or full volume on one instance leave the
  other instance's published state and allocation metadata unchanged.
- **Acceptance:** GFO.2: two partitions, both volumes started, writes and
  checks on both, separate halt/sync, each volume's `gefsN:` identity, and
  no cross-volume state change.

### O-9. Checker, preparation, recovery, and crash durability

- **O-9.1 MUST** provide an O checker that validates the profile, catalog
  order and uniqueness, object versions, Environment/code dependencies,
  pins, and cross-volume identity collisions.
- **O-9.2 MUST** provide a preparation tool for a new O volume and an
  initialized Space; the tool MUST NOT be required to read an already
  valid volume.
- **O-9.3 MUST** document and test the recovery-console contract and the
  state of a failed publication.
- **O-9.4 MUST** pass repeated crash cycles (at least 10-20 against one
  image, as required by the V7/GFO review gates) with the checker clean and
  no old/new mixture.
- **Acceptance:** GFO.8 and GFO.11; failed images and serial logs preserved.

### O-10. Compatibility and migration

- **O-10.1 MUST** keep existing V5/V6/V7 `DMOBJ` stores readable through
  the FS adapter, including historical versions, pins, and the V7 P1/P2
  behavior.
- **O-10.2 MUST** define an explicit migration/import from an FS-backed
  Space or `DMOBJ` store to native O.  A failed migration MUST leave the
  source intact; no legacy file may be deleted implicitly.
- **O-10.3 MUST** make FS reject an O profile and O reject an FS profile.
- **Acceptance:** GFO.1 and the existing V5/V6/V7 regression suites on FS;
  a migration fixture that succeeds, plus failure cases that leave the
  source unchanged.

### O-11. Acceptance and reporting

- **O-11.1 MUST** map every requirement above to a test from GFO.2-GFO.11
  or a focused new test, and keep the mapping in this document or a linked
  test manifest.
- **O-11.2 MUST** run QEMU gates before hardware, then a separate hardware
  run on the reserved partition.  Preparing a hardware image is not
  evidence that hardware passed.
- **O-11.3 MUST** report exact build identity, commands, results, skipped
  cases, and the checker output for both FS and O.
- **Definition of done:** all MUST requirements have a passing test, FS
  and O run concurrently on separate partitions, boot from O works with no
  FS root, the O checker is clean after every crash cycle, and hardware
  results are reported separately from QEMU.

## 5. What can be reused from V7, and what cannot

**Reusable store-level mechanisms:**
- Object OID/version model, atomic publication, abort, history `K`,
  `Objkeep`, and recovery pin.
- Environment-epoch catalog and `env_at(version)` selection.
- Named object records (`O_FUNN`, `O_CLASSN`) and the V7 upgrade/migration
  path.
- The P1/P2 fixes: one canonical method replacement and OID transfer,
  ambiguous-symbol rejection, catalog consistency checks, narrowed
  `__migrate__` old-state policy, and recompiled `.mpy` comparison.

**Not reusable as-is:**
- The `DMOBJ` file protocol and the `Fid`/`Fmsg`/`qid` store identity.
- The FS directory-based Space name and `lock` file.
- The FS `/usr/$user/lib` code/Environment storage and `/adm/users`
  identity source.
- `root=` FS boot and the legacy `-S` path.
- The FS checker as an O checker.

V7 working from a file is therefore **not** evidence that a native O volume
works.  It is the baseline the native format must preserve semantically.

## 6. Non-goals and deferred work

- A Unix-like browsing view of flat Space names.
- Cross-volume transactions or a transaction spanning FS and O.
- General process checkpointing, restoring execution stacks, or
  automatically starting a separate process per imported Space.
- Migrations larger than the existing 64 MiB transaction limit without a
  separately specified staged/resumable design.
- Replacing the ESP boot chain or removing the legacy FS namespace.
- Claiming native O boot complete while any code/Environment dependency
  still comes from FS.

## 7. Open questions and risks

1. **Format choice:** a new O volume format/profile, or a distinct GEFS
   profile that reuses the existing block/tree layout?  The format document
   and review gate come first.
2. **Identity source:** how O stores users/groups/ownership without FS, and
   how an administrator updates that table safely.
3. **Catalog retention:** how catalog entries and code blobs are reclaimed
   without breaking retained Space versions or pins.
4. **Cache policy:** a fixed per-volume share, a requested share at open, or
   a dynamic split; the total must remain bounded.
5. **Migration:** import from FS-backed Spaces and `DMOBJ` stores, including
   how to prove that the source was not modified on failure.
6. **Hardware layout:** the p5/p6 split, preserving or updating the p5
   PARTUUID, the ESP/GRUB entry, and the boot-time test hook.
7. **Review:** the format document and checker must be reviewed before
   format-changing code, as `docs/ge-fs-o.md` 4 requires.

## 8. Test strategy

**Stage A - QEMU, FS only and GE lifecycle:** boot a GEFS root from a GPT
partition image; run the existing GEFS tests and the P1/P2 V7 suite.  This
is the current baseline and does not prove native O.

**Stage B - QEMU, FS and O on separate partitions:** one disk with an ESP,
an FS GEFS partition, and an O-capable second partition.  Boot the FS root,
start the second volume through `#G`, and exercise concurrent writes,
syncs, checks, halts, a full/error volume, and crash recovery.  This is
GFO.2.  Until O-1 and O-2 exist, the second partition can only be an
interim GEFS volume with a `DMOBJ` store; that tests GE isolation, not
native O.

**Stage C - QEMU, native O boot:** prepare an O volume, boot with
`-O ... -S ...`, run an init Space, upgrade it, retain the pin across 100
commits and reboot, and run the crash matrix.  This is GFO.3-GFO.11.

**Stage D - hardware:** after the QEMU gates, split the reserved p5 into
the FS partition and an O partition (for example p5 + p6), write the
prepared images, update the ESP kernel/loader, boot through GRUB, run the
same tests, and report the hardware results separately.  The p5 data is
disposable; a GPT backup and the p5 PARTUUID are kept so the boot entry can
be preserved.

The acceptance matrix in section 4 is the release gate.  A requirement
without a passing test is open.
