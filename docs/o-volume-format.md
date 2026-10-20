# Native O volume format (PoC, version 1)

**Status:** Stage 4 of [the O-only PoC](PoC-experimental-demo.md).  This is
an experimental format, not a production one.  It reuses GE's superblock,
arenas, allocation logs, key/value tree, recovery and sync; it does not
define a second allocator or filesystem.  Stage 3 added object records and
the native kernel API; Stage 4 adds the userspace boundary and the
Environment/code/graph objects on top of it.  The on-disk profile is
still version 1.

## Profile and version

A GE superblock's `flag` field carries the profile:

- bit 63 (`Ovolflag`) set: native O volume;
- low 8 bits (`Ovolvermask`): format version, currently `1`;
- a legacy FS volume has this bit clear (its `flag` is normally 0).

The GE superblock version string remains `gefs9.00`, so GE itself loads
both profiles.  The profile check is made after the superblock is read:

- the FS opener (`loadfs`) rejects `Ovolflag` volumes;
- the O opener (`oopen`) requires `Ovolflag`, version 1, and the `init`
  catalog entry.

Opening an existing volume only reads it.  Formatting is a separate,
explicit preparation step; it is never triggered by open or boot.

## Records and catalog

O records are GE key/value pairs in the volume's snapshot tree.  The key
type is `Ko` (appended to the existing GE key types; legacy FS keys are
unchanged).  For a record name `n`, the key is:

```text
Ko | n
```

with no terminator.  The value is the record's bytes.  Names are flat
object-domain identifiers, not paths; there is no parent traversal,
directory creation or pathname normalization.

Version 1 defines one flat catalog entry:

```text
Ko "init"  ->  8-byte little-endian SpaceID = 1
```

`init` is a Space name, not a filename and not `init.py`.  The PoC
reserves `test.value` for its Stage 2 QEMU persistence check.

## Object records

Stage 3 reuses the existing `Kobj` record engine, with the catalog's
stable SpaceID where the `DMOBJ` adapter uses a file's `qid.path`:

```text
Kobj | SpaceID[8]                         => N[8] K[8] W[8] user[]
Kobj | SpaceID[8] oid[8] ~ver[8] idx[4]   => chunk
```

`N` is the published version, `K` the oldest kept version and `W` the
last begun transaction.  An object is the newest version `<= N` of its
OID; a tombstone (`len ~ 0`) means no object.  Version 0 (`N = 0`) means
the Space has no objects.  The records live in the O volume's snapshot
tree, not in a file, directory, `Fid` or 9P stream.

The native kernel API in `obj.c` is:

```text
ospaceopen(volume, "init") -> OSpace
ospaceget(space, oid, buf, len, &ver) -> bytes or -1
ospaceput(space, oid, buf, len)
ospacecommit(space) -> published version (sync)
ospaceclose(space)
```

`ospaceget` reads only the published version; it does not see an open
transaction.  `ospaceput` begins the Space's transaction on its first
put, and `ospacecommit` publishes it in one tree update and syncs.  A
Space handle's lock is the PoC's single writer; `begin` additionally
excludes any other transaction with the same SpaceID.  There is no
filesystem lock file.

## Preparation and limits (Stage 2/3 baseline)

`tools/mkovol OUT [MIB]` formats a new disposable image in the build VM,
using the tree's userspace gefs with GE's own ream machinery (`gefs -O`).
The result contains one GE arena set, an empty O catalog leaf and no FS
root, `adm` tree, user table or `DMOBJ` store.  Images must be at least
129 MiB because GE ream requires at least 128 MiB plus a block.

A single writer and normal committed sync are sufficient; opening never
formats or repairs.

## Direct userspace interface (Stage 4)

MicroPython does not go through `#G`, 9P, `Fid`, `qid`, a device file or a
path.  A small system-call interface (`sys/src/9/port/devgefs.c`,
`sys/src/libc/9syscall/sys.h`) carries the native calls:

```text
ovolopen(spec, len) -> volume handle       spec: PARTUUID=... or a device
ospaceopen(vol, name, len) -> space handle flat catalog lookup ("init")
ospaceget(space, oid, buf, len, &ver)      len 0, buf nil: the size
ospaceput(space, oid, buf, len)
ospacecommit(space, &ver)                  publish and sync
ospaceclose(space)
```

The kernel keeps the volume and Space handles; a process only sees small
integers.  `-O VOLUME` calls `ovolopen`, then `-S init` calls
`ospaceopen`.  The volume is the one native O instance the PoC allows.

## Environment and code objects (Stage 4)

Two reserved OIDs in the `init` Space carry the runtime metadata (the
same `Kobj` versioned object records as Stage 3):

```text
OID_ENV   = 0xfffffffffffffffc   the Environment manifest
OID_GRAPH = 0xfffffffffffffffd   the V7 Space graph stream
OID_CODE  = 0xffffffff00000000   base of the host-assigned code OIDs
```

The manifest is exactly the bytes named by the Space header's Environment
hash:

```text
runtime <mp-version>-mpy<MPY_VERSION>.<MPY_SUB_VERSION>
<import name> <definition SHA-256> <mpy SHA-256> <code OID, 16 hex>
```

A definition's identity stays the source's SHA-256.  The code OID names an
immutable `.mpy` object; `mpy SHA-256` is checked when the runtime reads
it, and MicroPython's persistent-code loader checks the MPY ABI.  There is
no filename, search path, source fallback or `sys.path` lookup on this
path.

`OID_GRAPH` is the existing V7 store read stream (`H` header, then one
`o` record per object), so `p9_image_restore_store` and the existing
OID/function/module identity machinery restore it unchanged.  A PoC
simplification: a commit rewrites the whole stream as one O object, not
one native O object per Python object.  The manifest and code objects are
rewritten in the same transaction so the engine's history sweep cannot
drop them.  The graph header's version is the Space version; the native O
engine's own header version is independent of it.

## Preparation and limits

`tools/mkovol OUT [MIB] [SOURCE]` formats a new disposable image in the
build VM, using the tree's userspace gefs with GE's own ream machinery
(`gefs -O`).  With `SOURCE`, the VM also compiles it with the tree's
MicroPython (`space.define`) and `gefs -O -i DIR` writes the manifest and
`.mpy` as version-1 O objects.  The seed's records must fit the single
catalog leaf; this is a demo limit, not a general package mechanism.

Stage 4 deliberately omits automatic `run()` startup, boot counters,
space versions/history in O, `space.create`/`view`/`upgrade` on O, `#G`,
9P, permissions, multi-volume operation, FS migration and crash
campaigns.  `tools/test-o-space.sh` boots an ESP + O disk with no FS
root twice: the first boot creates and commits the demo graph, the second
restores it, the same shared object, and the same function code from O.
