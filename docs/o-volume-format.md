# Native O volume format (PoC, version 1)

**Status:** Stage 2 of [the O-only PoC](PoC-experimental-demo.md).  This is
an experimental format, not a production one.  It reuses GE's superblock,
arenas, allocation logs, key/value tree, recovery and sync; it does not
define a second allocator or filesystem.

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
reserves `test.value` for its QEMU persistence check.  Future Stage 3
object records use the existing `Kobj` namespace scoped by the SpaceID;
this document does not define them.

## Preparation and limits

`tools/mkovol OUT [MIB]` formats a new disposable image in the build VM,
using the tree's userspace gefs with GE's own ream machinery (`gefs -O`).
The result contains one GE arena set, an empty O catalog leaf and no FS
root, `adm` tree, user table or `DMOBJ` store.  Images must be at least
129 MiB because GE ream requires at least 128 MiB plus a block.

Stage 2 deliberately omits object transactions, MicroPython, `-O`/`-S`
boot selection, `#G`, 9P, permissions, multi-volume operation, FS-to-O
migration and crash campaigns.  A single writer and normal committed
sync are sufficient; opening never formats or repairs.
