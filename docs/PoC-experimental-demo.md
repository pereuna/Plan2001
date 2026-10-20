# Plan2001 — Experimental O-Only PoC Demo

**Type:** Coding-agent implementation brief  
**Priority:** Immediate; this is the project's central architectural experiment.  
**Baseline:** `pereuna/Plan2001`, branch `docs-ge-fs-o` (`e280d7e`, 2026-10-20), incorporating V7 and the P1/P2 fixes.  
**Status:** Target specification, **not** a claim that O-only storage exists today.

## 0. Objective and non-negotiable architectural decision

Demonstrate an operating system whose **persistent programs, code, and state are objects**, not files. Boot an embedded MicroPython runtime, restore an initialized Space **directly from an O partition**, invoke its persisted `run()` callable, commit changed object state, and reboot into the next state. The kernel/runtime must require **no mounted filesystem, filesystem root, directories, file-backed object store, or source/bytecode files** for this demonstration.

**GE** remains the reusable physical storage engine (blocks, tree, allocation, publication). **O** is a native object store over GE. **FS** is legacy, out of scope for this demo.

**Important corrections to earlier design discussion:**

- **Do not implement `#G`**, a 9P server, `Fid`/`Fmsg`, `qid`, file paths, a pseudo-file transport, or a virtual filesystem as O's API. `#G` was an obsolete example, **not a requirement**.
- **Do not implement a “flat filesystem.”** Names of Spaces/modules are logical object identifiers, **not filenames**. Do not invent entries such as `/a/b/c/d.txt`; `/` has no pathname meaning in O. Prefer a simple Space identifier such as `init` for the demo.
- **Do not pursue Plan 9 / 9P compatibility at kernel level as part of O.** Existing legacy code may remain untouched; the O-only path must not depend on it. This is a new experimental OS, not a Plan 9 compatibility milestone.
- **Do not build an FS→O migration utility.** Seed a fresh O image with a host-side preparation tool. Import individual source/bytecode assets as object blobs when needed; the finished system must not load them as files.
- The **ESP/UEFI loader may still read boot files before kernel entry**. This does not make the running Plan2001 O system file-backed. Embedded MicroPython/console resources are also allowed. No runtime filesystem reads or FS fallback for code or state.

**Engineering rule:** Implement the *minimum coherent vertical slice*. No comprehensive general-purpose subsystem is a prerequisite unless the next demo step demonstrably cannot run without it. Favor explicit PoC limitations over broad redesigns. Keep legacy FS implementation isolated and unchanged where practicable.

## 1. Decouple GE volume opening from FS initialization

Current `gefsserve()` and `loadfs()` retain FS-specific assumptions (`adm` snapshot, `/adm/users`, 9P workers). Refactor the startup path so a **single O volume** can use GE's blocks/tree/recovery without creating or mounting an FS root and without loading FS user metadata.

- Introduce an internal, explicit GE volume-open context that can select the O profile and access its tree.
- Do not require an FS `Mount`, `Fid`, `Conn`, 9P request, or `/adm/users` for O operations. It is acceptable to reuse lower-level GE snapshot/tree mechanisms; do not re-create directories to satisfy them.
- For this PoC **one active O volume is sufficient**. **Do not** fix multi-volume cache sharing, cross-volume user IDs, `#G` volume management, or concurrent FS/O start-up merely to complete this step.

**Gate:** A QEMU test opens a disposable existing GE volume, reads a raw GE key/value record and writes it back **without mounting an FS or starting a 9P server**. An O-specific profile is introduced in step 2.

## 2. Define and prepare the minimal native O partition

Reuse GE's existing physical block/tree machinery; **do not design a second block allocator or new general-purpose filesystem**. Add only the on-disk discrimination and metadata needed to identify an O volume and locate its object catalog.

- Specify a concise **PoC O profile/version** and fixed metadata layout. Opening must distinguish O from FS; opening **never formats a device**.
- Provide a host-side `mkovol`-style preparation tool for a *new, disposable QEMU partition/image*.
- Create a minimal flat catalog `SpaceName -> stable SpaceID`, initially just `init`. Both are object-domain values, not paths. A Space's object keys are scoped by its ID.
- Keep the format small and documented, sufficient to restore the next boot. Production-ready users/groups, access control, advanced reclamation and migration are deferred.

**Gate:** Format a new O image on the host; mount **no FS** in QEMU; the kernel recognizes the O profile and resolves `init` to its SpaceID. Invalid/non-O media are rejected without modification.

## 3. Give O a direct object API, never a file adapter

Extract/reuse the existing `sys/src/9/gefs/obj.c` object logic (OID, versions, put, commit, publication) behind **native O volume/Space handles**. The existing `DMOBJ` path uses `Fid.qpath` to identify a store: replace that dependency with a stable **SpaceID** for native O.

Minimal conceptual operations (names are illustrative; use idiomatic Plan2001 C):

```text
ovol_open(partition) -> OVolume
ospace_open(volume, "init") -> OSpace
object_get(space, oid) -> bytes
object_put(space, oid, bytes)
object_commit(space) -> published version (durable)
ospace_close(space)
```

- Reuse **one** object transaction/version engine rather than duplicating the algorithms for native O and `DMOBJ`.
- Do not translate these calls into `Tread`/`Twrite`, 9P, a fake `Fid`, a hidden object-store *file*, or a directory entry. Native O calls GE directly.
- A single writer for the selected Space is adequate for the demo; a simple native lock is sufficient. Do not use the FS `DMEXCL lock` file.
- Keep enough of the existing GE publication/sync behavior to preserve a cleanly committed version across normal reboot. **Exhaustive power-cut resilience is not a gate.**

**Gate:** Put two small objects with stable OIDs, commit, reboot QEMU, and fetch both by OID. No FS was involved in the transaction. Graph alias/cycle preservation is checked later through MicroPython in step 5.

## 4. Put Environment, definitions and MicroPython imports in O

The V7 Space model still reads Environment, `.py` and `.mpy` via `/usr/$user/lib`. For this experiment, replace the dependency with **immutable O blobs and object references**.

- Preparation tool compiles/collects the demo's source and `.mpy` on the development host and writes them as O objects, with a minimal Environment manifest mapping **module identity -> code blob identity**.
- At runtime, MicroPython loads the selected Environment and compiled definitions **only from O**. The initial implementation may support only the exact demo modules; no generic package manager is required.
- Preserve V7 persistent graph/function identity and restore semantics to the extent needed to keep a persisted `run()` callable and its referenced code valid across boots.
- Check code blob integrity and compatible MicroPython/MPY ABI on load. Fail visibly on a missing/incompatible blob; **never fall back to `sys.path` or FS**.
- Do not require `space.upgrade()`, schema migration, source recompilation, a comprehensive historical epoch checker, or generic dynamic imports for this PoC.

**Gate:** Remove the FS root partition entirely from the QEMU test disk (ESP + O only). The runtime still loads the O-resident Environment/code and restores the persistent Space graph.

## 5. Boot the O-only init Space and prove persistence

Add the minimum selection/startup path, for example:

```ini
init=/boot/mp -O PARTUUID=<O-partition-guid> -S init -B
user=glenda
# No root= parameter
```

The identifier `init` denotes a **Space**, not `init.py` or any filename. Option names are proposed interface details, not permission to reintroduce paths.

Boot order:

1. Existing ESP/UEFI loader starts the kernel with embedded MicroPython/console support.
2. GE opens the O partition, recognizes its profile and locates Space `init`.
3. MicroPython resolves the Environment and code **from O**, restores the committed object graph, and finds persistent global `run`.
4. Call `run()` **without re-executing module initialization**. Recreate transient console/device resources outside persistent state as needed.
5. The demo `run()` increments a persisted counter; commit and sync it. On a normal return, enter the embedded console or exit safely. On a failure, print an error; **do not boot from FS or silently format O**.

**Acceptance demonstration (three clean QEMU boots of the same O image):**

```text
Boot #1: restored init Space; run() -> counter 1
Boot #2: restored init Space; run() -> counter 2
Boot #3: restored init Space; run() -> counter 3
```

Also verify that a persistent class instance and a shared object reference survive reboot, code came from O, and no runtime FS partition, mount, path lookup or source/bytecode file was used.

**This is the PoC definition of done.** Stop feature work here and report results.

## Explicitly NOT required for this PoC

- Concurrent FS + O operation, multiple GE volumes, cache redistribution, per-volume FS user IDs, `#G`, or 9P exposure.
- Kernel-level Plan 9 API compatibility; flat/virtual filesystem; directories or path-like object naming.
- FS→O bulk migration or compatibility conversion of existing `DMOBJ` images.
- Full permission/group model, production management tools, complete checker, historical retention/reclamation matrix, or all V7 upgrade/migration combinations.
- Comprehensive crash/fault-injection campaigns, 10–20 repeated power cuts, full-disk stress, or proof of production durability. Keep existing GE integrity mechanics where available; do not make hardening an excuse to postpone the demo.
- Hardware, ARM64, RISC-V, WASM, networking, multi-process Spaces or package management. **QEMU amd64 is enough.**

## Agent execution discipline

1. Work in **five small sequential changes**, one per section. After each, demonstrate its **specific gate**; do not replace it with a broad unrelated test suite.
2. Do not modify O requirements to reintroduce FS, files, 9P, or `#G`. When a legacy dependency is encountered, isolate/bypass it on the native O code path rather than reproducing the old abstraction.
3. Preserve and reuse existing GE/V7 code where possible. Refactor at the boundary; do not rewrite GEFS wholesale.
4. Keep any PoC-only simplification explicit in comments and a short status note. Data on the disposable test partition may be reformatted by **explicit** provisioning commands, never by boot/open.
5. If a nonessential regression or production requirement blocks a stage, **document the limitation and continue the vertical slice**. Do not enter an endless fix-all loop. A failure of the core O boot/commit/reboot gates, however, **is** a blocker.
6. Report the exact commit, QEMU commands, pass/fail for each of the five gates, the boot log demonstrating `1 -> 2 -> 3`, and any shortcuts or untested cases. Never claim tests were run unless they were run.

**Success statement:** *Plan2001 boots MicroPython from the embedded kernel, loads its init program and persistent object graph directly from an O partition, and resumes at the next committed state after reboot — with no filesystem participating in program or state storage.*
