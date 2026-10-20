# GE + FS + O: partition-backed Spaces and an object-backed init

**Status:** implementation specification; the target behavior below is not
implemented by this document.  Stage 1 (GE instance separation) and the
object-store part of stage 2 are implemented; see
[ge-fs-o-impl.md](ge-fs-o-impl.md) for what is in the tree, what is not, and
the tests that were run.

**Baseline:** Plan2001 `f3ff602` (2026-10-20).

**Scope:** separate the existing GEFS storage engine, legacy file system,
and object store, then boot an initialized Space directly from an object
partition without a file-system root.

## 1. Goal

Split the modified GEFS implementation into three functional components:

```text
                        GE
                shared storage engine
                         |
              +----------+----------+
              |                     |
             FS                     O
       legacy file system      object storage
              |                     |
       file-system partition   object partition
```

The components share implementation, not allocation state across partitions.
Each mounted volume has its own GE instance. FS and O must operate on
different partitions concurrently, including partitions on the same disk.

The long-term direction is a system whose programs are Spaces: their code,
persistent state, and execution environment are selected together. An
application should not have to open a separate persistence database to
become persistent. Explicit Space operations remain available.

The first complete milestone is an independently bootable init Space.
Legacy FS remains usable during the transition; replacing every existing
program or virtual device namespace is not part of this milestone.

## 2. Current implementation and its dependencies

Today, `space.create("name", env)` creates
`/usr/<user>/space/name/`, containing a `lock` file and a `DMOBJ` object
store named `obj`. This is not an ordinary serialized database file:
GEFS interprets its read/write protocol and stores versioned `Kobj` keys.
The store identifier is the file's `qid.path`; transactions and cursors
are attached to file-server `Fid` objects.

MicroPython separately reads Environment records and immutable definition
blobs from `/usr/<user>/lib`. Moving only `obj` to a partition would leave
boot dependent on these files.

The kernel implementation also has a global `Gefs *fs`, shared process
state, and an explicit one-instance restriction in `gefsserve()`. These
must be removed as dependencies of volume operation, rather than bypassed
by starting a second instance over the same globals.

Current boot executes an init program selected by `init=`. It can execute
the kernel-embedded `/boot/mp -B` without a file-system root, but that
bootstrap does not automatically restore an init Space. Existing `-S`
selects a Space through its per-user directory.

See [GEFS](gefs.md), [the object-store protocol](gefs-obj.md),
[the object model](object-model.md), [MicroPython init](mpinit.md), and
[kernel root mounting](init.md) for the baseline behavior. Their existing
implementation claims do not establish completion of this specification.

## 3. Responsibilities and boundaries

### GE: shared persistent storage engine

GE owns volume I/O, block allocation and reclamation, caches, checksummed
block pointers, the key/value tree, shared snapshot machinery, sync, and
the common crash-recovery mechanisms. Its interfaces take an explicit
volume context, directly or through a handle bound to that context.

Workers, queues, locks, epochs, active transactions, device handles,
shutdown state, and recovery state must belong to the appropriate volume.
Cache budgets must account for the combined memory use of all instances.
A fault or a full volume must not change the other volume's publication
state or allocation metadata. Shared physical-device failures can still
affect both partitions.

GE does not interpret directory ancestry, Python object records,
Environment hashes, schema migrations, or program entry points. Atomic
tree-root publication and durable sync are shared mechanisms; O supplies
the Space-version visibility and retention policy built on them.

### FS: legacy file-system semantics

FS owns file and directory names, hierarchy, file contents and metadata,
permissions, and the existing 9P file-system interface. It remains
compatible with existing **Plan2001 GEFS** volumes, including existing
`DMOBJ` stores. This does not promise upstream 9front GEFS support for
Plan2001's `Kobj` extension.

The old `DMOBJ` interface becomes an adapter to the common object-store
implementation. It maps file identity and file-server requests to object
store handles. There must not be two independent copies of the
transaction, abort, history, and pin algorithms.

### O: partition-backed object storage

O owns Space identities, the flat Space-name catalog, object records,
OID/version lookup, writer exclusion, transaction publication, version
history, and recovery pins. Its native handles must not require a file
entry, directory, file `qid`, or file-server `Fid`.

A Space has a stable identifier independent of its name. Object identity
is scoped by volume and Space; an OID is not globally unique across
volumes. Writer exclusion is implemented by O, without a `lock` file.
Ownership and access permissions are O metadata and must not depend on
parent directories. User/group resolution needed for O access must also
be available without FS.

O exposes storage services for immutable Environment and definition
blobs. MicroPython owns their interpretation, hashing/ABI checks,
serialization of Python objects, graph restoration, and code migration.
The current Python-specific header pin encoding must not become GE policy.

There is no implied atomic transaction spanning FS and O partitions.

## 4. Names and the self-contained object volume

The O catalog is flat. A name such as `/lib/net/sdf` is one complete
name, not a request to traverse or create `/lib` and `/lib/net`.
Slash is an ordinary character in that name. Names are looked up as
supplied; path-component normalization and parent traversal do not apply.

O does not expose directory creation, parent-directory relationships, or
hierarchical permission inheritance. A compatibility view may later
present objects as files, but that view must not become a storage
dependency. How prefix browsing should appear to humans and legacy
programs is deferred to practical user testing.

An independently bootable O volume contains:

- A catalog mapping complete Space names to stable Space identifiers.
- Each Space's published graph, version metadata, and recovery pin.
- The Environment catalog and immutable definition blobs needed to restore
  both current and retained historical Space versions.
- The source and `.mpy` material required for supported code upgrades.
- The metadata needed to enforce ownership and access without FS.

Retained Space versions retain their code dependencies. Reclamation must
not remove a blob needed by another Space, a retained version, or a pin.
Missing or invalid dependencies cause a clear error, never a fallback to
unrelated code on FS or `sys.path`.

The existing GE physical storage mechanisms should be reused. The native
O volume must have an unambiguous format/profile identifier and version.
FS must reject an unsupported O volume and O must reject an FS volume
passed as a native O volume. Opening a device never formats it or performs
an implicit conversion. Exact byte layouts and compatibility rules must
be documented and reviewed before format-changing implementation begins.

## 5. MicroPython selection and boot contract

Add `-O VOLUME` to select the process's default O volume. `-S NAME`
selects and automatically restores a Space on that volume before user
program execution. Explicit `space.create()` and `space.open()` remain
available and use the selected volume. Without `-O`, the current legacy
selection remains supported during the transition.

The proposed `plan9.ini` configuration is:

```text
init=/boot/mp -O PARTUUID=<object-partition-guid> -S init.py -B
user=glenda
```

An explicit partition device path is accepted in place of `PARTUUID=...`.
Device selection must identify partition bounds and validate the O
format before use. `init.py` is a Space name, not a source-file path.
No `root=` or FS partition is required for this configuration.

The first milestone retains the existing UEFI loader and kernel loading
from the ESP. MicroPython and `kbdfs` remain embedded in the kernel image;
loading the kernel itself from O is a separate project.

The `-O ... -S ... -B` sequence is:

1. The loader enters the kernel with the existing BootInfo contract.
2. The kernel starts its embedded MicroPython. Bootstrap prepares the
   devices and console using embedded resources, outside persistent
   Space globals. It does not require an FS mount.
3. GE opens the selected O partition and completes recovery. O locates
   the existing named Space and acquires its writer ownership.
4. MicroPython validates the matching Environment, code hashes, and ABI,
   then restores the published graph and its persistent globals.
5. The runtime invokes the restored global `run()` callable with no
   arguments. It does not re-execute module initialization code.

Ordinary `-S` restoration must not run bootstrap with a half-restored
graph. Implement this ordering explicitly for the boot combination;
do not assume the current `main.c` option ordering already provides it.
Boot-only objects and process resources must not leak into persistence.

This restores program state, not an instruction pointer, execution stack,
open device handles, or a previously running process. `run()` starts a new
execution and recreates transient resources as needed. Existing commit
points and the 64 MiB transaction limit remain in force.

## 6. Preparation, failure, and recovery

A preparation tool creates a new O image and installs an initialized init
Space before boot. Its published graph already contains a persistent
`run()` callable and all required definitions. Preparation may use files
and build tools; execution of the finished image must not depend on them.
The first boot never silently creates or initializes a missing Space.

Initialization is explicit and targets a new image or explicitly selected
volume. Boot, inspection, and ordinary open operations are non-formatting.
The first milestone does not convert existing FS volumes in place and
does not include migration of old file-backed Spaces.

If `run()` returns normally, commit its remaining supported changes,
release writer ownership, and enter the embedded recovery console.
If it raises, report the exception and do not perform a final commit.
Earlier successful commits remain published; this is not rollback of all
activity since boot. A final commit failure is reported as a failure,
not a successful shutdown.

The recovery console operates outside the restored Space and must not
implicitly commit its failed in-memory graph. It remains available for
missing Spaces, corrupt/missing definitions, unsupported ABI, lock
conflicts, or failure to open the O partition. Do not automatically rerun
`run()` or fall back to a different init. If the console itself cannot be
initialized, report the failure through the kernel console.

Before durable publication, a crash exposes only the previous committed
version. After recovery, state and Environment must belong to the same
published version. A failed boot does not discard a recovery pin.
External I/O performed by init or migration code is not transactional.

## 7. Delivery stages

1. **GE instance separation.** Remove the global-instance dependencies and
   one-volume restriction. Preserve existing FS disk compatibility and
   behavior; verify two independent volumes concurrently.
2. **Object-core separation.** Introduce native volume/Space handles and
   preserve `DMOBJ` through an FS adapter. Reuse the same version,
   publication, abort, and retention implementation.
3. **Native O volume.** Specify the format, catalog, access metadata,
   immutable-code storage, initialization, and volume checker. Add tools
   to prepare new volumes and initialized Spaces.
4. **Automatic init-Space startup.** Add MicroPython volume selection and
   the boot sequence above, including recovery-console behavior.

Each stage has its own regression results and completion status. Do not
mark independent O boot complete while Environment or code loading still
depends on an FS volume. Changes to shared formats require matching
inspection/checking tools, not just kernel support.

## 8. Acceptance tests

| ID | Required experiment and result |
|---|---|
| GFO.1 | Existing Plan2001 FS images remain readable and writable. Existing `DMOBJ` stores retain V5/V6/V7 behavior, including historical reads and recovery pins. |
| GFO.2 | FS and O operate on separate partitions concurrently. Writes, shutdown, recovery, writer locks, and allocation state are volume-specific; a full volume does not corrupt the other. |
| GFO.3 | Prepare a new O image, then boot with the documented configuration and no FS partition or `root=`. Init's `run()` executes using only embedded bootstrap resources and O-resident dependencies. |
| GFO.4 | Init increments a persistent global counter. Across repeated boots the counter continues from its last commit, while `run()` executes again and module initialization does not. |
| GFO.5 | Create and reopen a Space named `/lib/net/sdf` without parent objects. Directory creation is unavailable through native O operations. |
| GFO.6 | A second writer is refused. Committed readers remain consistent; releasing a process's handles releases its writer ownership. |
| GFO.7 | Upgrade a native O Space, retain its pin across 100 ordinary commits and reboot, and read the old version with old code. Repeat the upgrade and verify atomic pin replacement. |
| GFO.8 | Kill QEMU before staging, during staging, around publication/sync, and after acknowledged sync. Recovery yields a complete old or new graph/Environment pair. Repeat 10–20 crash cycles against one image and check it after each recovery. |
| GFO.9 | Missing init, missing/corrupt code, wrong ABI, wrong volume type, full volume, and lock conflict produce diagnostics and the recovery console without implicit initialization or code fallback. |
| GFO.10 | Normal `run()` return commits and enters recovery. An exception does not make a final commit or restart init. The recovery console holds no failed Space writer lock. |
| GFO.11 | The O checker validates native catalogs, object versions, dependencies, and pins; the FS checker continues to validate legacy volumes. Cross-volume identity collisions do not resolve to the wrong object. |

Use focused kernel storage tests plus QEMU boot tests. Preserve serial logs
and failed images. Report exact commands, build identity, results, and
skipped cases. Existing Space tests are regressions, not proof of native O
boot. Real-hardware validation follows the QEMU gates and must be reported
separately; preparing a hardware test is not evidence that it passed.

## 9. Deferred work

- Importing existing file-backed Spaces into O; sources must remain intact
  when a later explicit migration tool is introduced.
- Automatically starting a separate process, namespace, and Space for
  `import vim; vim.run()`. This needs a separate specification for import
  selection, cross-process calls, arguments, return values, and errors.
- The human-facing or Unix-compatible browsing view of flat Space names.
- Restoring execution stacks, general process checkpointing, cross-volume
  transactions, and migrations larger than 64 MiB.
- Replacing the ESP boot chain, moving all legacy tools to Spaces, or
  removing the existing virtual-device namespace.

These deferrals do not change the direction: O becomes the native storage
model, FS remains the legacy compatibility component, and GE supplies
their shared storage mechanisms.
