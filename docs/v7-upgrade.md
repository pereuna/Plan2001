# Plan2001 V7 — Atomic Space Code Upgrade and Schema Migration

**Status:** implementation specification / agent work order (not a claim of completed functionality)  
**Baseline:** `pereuna/Plan2001`, `main` at `2bda0a720886d705854a8c45dc3d86a170c7c994` (2026-10-19)  
**Destination in repository:** `docs/v7-upgrade.md`  
**Authority:** `docs/object-model.md`, section **V7**. This document refines implementation details; if an existing interface conflicts, report the conflict rather than silently weakening V7's acceptance criteria.

## 0. Agent instructions

Implement V7 in a new `space-v7` branch. **Read** `AGENTS.md`, `docs/object-model.md`, `docs/gefs-obj.md`, `docs/space-v6-open.md`, `docs/gefs.md`, `docs/status.md`, and the referenced source files before editing. Follow the repository's build, test, commit, and status-reporting rules. Work in small reviewable commits with tests. Do not push or merge unless the operator explicitly requests it.

**Do not start by rewriting GEFS's physical block format.** V7 is a logical Space/code migration feature, not the proposed `gefs salvage` project. Do not modify `loadlog`/crash recovery to reclaim an assumed 200 MiB leak. The latest commit `2bda0a7` improved GEFS diagnostics; its 27 `gefslog` tests and one-crash test passed, but **the recommended 10–20 same-image crash-cycle test remains outstanding**. Keep this as a separate reliability item, not a V7 implementation dependency.

Produce actual code and tests, not merely an implementation plan. If a proposed implementation breaks an invariant below, stop that subtask, explain the blocker precisely, and do not mark V7 complete. Never hide skipped tests.

## 1. Goal and exact public contract

```python
space.upgrade(name, new_env)  # an offline, exclusive operation
```

Upgrade an existing V5/V6 object-backed Space from its old immutable `Environment` to `new_env` while preserving its persistent object graph and stable OIDs. Old functions and classes, **including references saved elsewhere in the graph**, resolve to the new code using a stable `(module, qualified_name)` identity. A class whose integer `__schema__` changed (default `0`) requires `__migrate__(self, old)`; absence or failure aborts the upgrade. Publish the new Environment and migrated state **as one atomic Space version**. Retain the version immediately before upgrade as a readable recovery point until the next successful upgrade.

The operation runs in a **separate MicroPython process with no Space already open**. An active writer holds `space/<name>/lock` with `DMEXCL`: fail clearly if that lock cannot be acquired; do not hot-swap a running process's VM. After success, future `mp -S name`, `space.open`, and `space.view` must use the correct code for each selected Space version. The upgrade process must release all resources on every error.

Scope of first complete V7: existing `obj` stores whose migration fits `Objtxmax` (64 MiB of transaction payload); unrepresentable or larger upgrades fail **before publication**, leaving the previous committed Space intact. Multi-transaction >64 MiB support is a separately gated extension (§8), not permission to publish a partial upgrade.

**No undocumented migration of V2–V4 `v<N>` files.** Require a finished V5 object-store conversion first, or implement and test a separately specified preflight conversion. Do not delete legacy files during a failed V7 attempt.

## 2. Baseline facts verified in code

| Current code | Relevant fact / gap |
|---|---|
| `sys/src/cmd/micropython/p9image.c:594` (`putfun`) | Functions encode module + index path through the module raw-code tree, not a qualified symbol name. Reordering compiled functions can silently select the wrong code. |
| `p9image.c` `O_CLASS`, `O_FUN`, `O_CLOSURE`, `O_INSTANCE` | Restoration creates class/function shells and later fills dictionaries; migration must preserve aliases, cycles, closure policy, and instance OIDs. |
| `sys/src/cmd/micropython/p9space.c` `useenv`, `loadmpy`, `p9_view_loadmod` | Existing Environment is effectively fixed for a normal Space; V6 views have separate module contexts but are marked **foreign** and cannot be committed as the local Space. Create an explicit upgrade path, not `space.view()` followed by a normal commit. |
| `p9space.c` `storewrite`, `p9_image_commit` | The current header user payload is `env[64] + next_oid[8]` (72 bytes). `p9_image_commit` skips writing when no object changed: an Environment-only upgrade needs a forced published commit. |
| `sys/src/9/gefs/obj.c` `Objkeep=3`, `commit` | The GC/history lower bound advances with `N`; there is no pinned recovery version. |
| `obj.c` `objread` | Reading a **historical** object version returns the **current** store header user payload; it does not return the historic Environment. Pinning Kobj values alone is therefore insufficient for rollback. |
| `sys/src/cmd/micropython/p9space.h` | `OID_ROOTS=1`, `OID_FIRST=2`; ordinary OID `2` may already exist. Do not introduce metadata by stealing a low OID. |
| `obj.c` `Objtxmax=64*MiB` | Total transaction data is bounded. |
| `tools/test-space.sh` | `phases = [v0, ..., v6]`; add V7 and tests `T7.1`–`T7.4` plus the edge cases below. |

The baseline commit `2bda0a7` changes the user-space GEFS checker, `tools/gefslog`, tests, and documentation. It **does not implement V7** or alter its MicroPython object-storage architecture.

## 3. Non-negotiable invariants

**I1 — Atomic visibility.** A reader and a restarted process see either `(old_env, old_graph)` at version `N` or `(new_env, migrated_graph)` at `N+1`. No mixed pair and no partially migrated graph is ever published. Use existing versioned `Kobj` records and one final header publication followed by the durable sync acknowledgement. Multiple `btupsert` calls alone are *not* atomic.

**I2 — Exclusive writer.** The upgrade owns the same `DMEXCL` lock as `space.open` for the entire operation. Concurrent writer attempts fail; readers may continue to use committed versions according to existing policies.

**I3 — Persistent identity.** Existing OIDs are unchanged for surviving mutable objects, classes, and persistent named functions. Aliases, cycles, graph edges, `space.oid()`, and the mapping of class instances to their new type survive upgrade. All new OIDs remain unique; metadata IDs cannot collide or wrap into normal OIDs.

**I4 — No stale code.** Every reachable persisted reference to an old named function or class is rebound, not merely the module's global dictionary. If `(module, qualified_name)` is missing or ambiguous, abort before publication. This includes objects stored in lists, dicts, sets, defaults, closures, and attributes; where faithful transformation is unsupported, fail explicitly rather than silently keeping old bytecode.

**I5 — Schema semantics.** Compare old/new `__schema__` (integer, default 0). On change invoke `new_class.__migrate__(new_self, old_read_only_state)` exactly once per surviving instance. Do not call normal `__init__` on restored instances. The migration must preserve graph identity, handle cycles, and finish before publication. If the hook is absent, raises, or produces unsupported persistent data, abort. Define/document that `old` is a read-only view/copy so a hook cannot accidentally destroy data needed by other hooks.

**I6 — Recovery point and code match.** The immediately preceding Space version stays readable through arbitrarily many ordinary commits until the *next successful upgrade*. Selecting version `v` must load **the Environment effective at `v`**, not merely the latest Environment. It must fail if requisite immutable `.mpy`/Environment blobs are missing or invalid. Before discarding the previous pin, atomically establish a new one.

**I7 — Failure safety.** Failed validation, migration exceptions, disk-full, aborted writes, and process/QEMU crashes cannot move the published version to a half-upgraded state. Intermediate `W>N` records remain invisible and are reclaimed by the existing abort/recovery path. Post-publication history cleanup failures must not revert the new published version (preserve V5 T5.12 behavior).

**I8 — Code identity and provenance.** Definition blobs remain immutable and SHA-256 checked. `new_env` must match the supported MicroPython runtime/MPY ABI. Persist the Environment transitions as part of the same durable transaction as the new Space state.

**I9 — I/O limits.** GEFS rollback does **not** undo writes to other files, network packets, or hardware I/O performed by import-time or migration code. Document this explicitly. Initially support only upgrade code with no external side effects; an isolated process by itself is not a security sandbox.

## 4. Required storage design — settle before coding the interpreter

### 4.1 Version-to-Environment history (new mandatory requirement)

The current `H` record from GEFS has `N, K, user`, where `user` describes only the **latest** Environment. For `space.open(name, version=old)` and `space.view(name, version=old)` this becomes wrong immediately after the first upgrade.

Implement an **atomic, versioned Environment-epoch catalog** with entries:

```
(start_version, environment_sha256)
```

Its lookup is `env_at(v) = env from the last epoch with start_version <= v`. At initial upgrade from old version `N`, bootstrap epochs `(first_available_old_version, old_env)` and `(N+1, new_env)`; on subsequent upgrades append another epoch. Retain all epochs necessary to interpret `[K .. latest]` plus the pinned recovery point. Old versions which precede the first upgrade must still resolve to the old Environment. Do not infer the selected version's Environment from the current `H.user`.

**Recommended concrete representation:** reserve the otherwise-unused **high** OID `UINT64_MAX-1` as `OID_UPGRADE_META` for an internal `Kobj` blob encoding the epoch catalog and its format version; reject this OID for normal object allocation. `OID_ROOTS=1` and historical ordinary `OID_FIRST=2` remain unchanged. Do not decode the internal blob as an `O_CLASS`/`O_FUN` record and do not add it to the MicroPython GC/OID table. Existing GEFS `'s'` selects an OID for a read; with this near-maximal OID it is effectively last in the permitted OID namespace. `objread` initializes `tx->roid` to `~0ULL`, so **do not use `UINT64_MAX` itself**: that sentinel would cause the first metadata record to be skipped. Reject both `UINT64_MAX-1` (reserved) and `UINT64_MAX` (sentinel) in the ordinary OID allocator. Fetch the **latest** catalog in a separate read *before* selecting a historical Space version, then restore that version with `env_at(v)`. When no catalog exists, use the legacy 72-byte header format. Add robust length, ordering, uniqueness, and hash/format validation.

Keep the header's latest `(env[64], next_oid[8])` for backward-oriented readers while extending its user payload with the recovery pin and explicit format marker. `Objuser=256` allows a bounded header extension; keep an unbounded epoch list in the internal object, not in the header. Update all readers that currently assume an exact 72-byte payload (`p9space.c` open/view and test utilities). Backward compatibility is required for previously created V5/V6 stores, but older binaries need not read upgraded stores.

The catalog change, new Environment in the header, and migrated object records **must be in the same GEFS object transaction**. Use an existing `Kobj` record plus one `h` header update and the final `c` sync. Before changing the format, write down exact byte layouts and an upgrade/reopen test fixture.

### 4.2 Pinned recovery point and GEFS GC

Use a persistent `pin_version` in the validated header extension (`0` = none). A successful upgrade at version `N+1` pins `N` before the history sweep. On each commit, compute the normal three-version floor, then cap its advancement at the pinned version, *never decreasing `K` below an already-pruned version*. Invariant:

```
K <= pin_version <= N       when pin_version != 0
K <= selected_read_version <= N
```

Conservatively retaining the contiguous range from the pin through current `N` is acceptable for V7. It costs disk space but avoids an untested multi-interval GC algorithm. Release the former pin and replace it with the latest pre-upgrade version **atomically during the next successful upgrade**. A failed upgrade must not release the old pin. Ordinary commits must not reset it.

**Implementation trap:** `obj.c:commit()` currently computes `h.k` *before* applying `tx->newuser`. For the upgrade publication it must validate/apply the prospective pin before calculating the new `K` and running `dropold`. Ensure the special metadata cannot be accidentally wiped by a normal 72-byte header write. Sweep may lag; a later safe cleanup is allowed, not a rollback of publication.

### 4.3 Atomic forced commit

Extend the writer path to allow a forced commit for changed Environment/epoch/pin metadata, even if no user object is dirty. The explicit upgrade commit writes at least the internal catalog and updated header. `p9_image_commit`'s current `nrec == 0` early exit must not swallow it. Validate failure/abort in every prefix of the record stream, including a crash just before the final `c` and just after its durable acknowledgement.

## 5. Stable code identity and migration mechanics

### 5.1 Versioned object serialization

Implement stable `(module, qualified_name)` identities for persistent functions and classes. Existing `O_FUN` embeds a raw-code child-index path; using that path against a different `.mpy` may silently run the wrong function. Existing `O_CLASS` carries only its simple name/bases/dict. Add explicit versioned **named** serialization forms (or an equally precise versioned symbol table), preserving old readers for existing V5/V6 records. A version bump to the legacy `P2SP FORMAT` *alone* does not version the V5 `Kobj` object-record grammar: test both paths.

Resolve and validate the full symbol map **before** binding a new graph. On legacy first upgrade use the *old* Environment to decode the legacy index paths, establish each old symbol's identity, then bind to the new Environment by stable name. For an old symbol not resolvable to a unique qualified name, abort with its object path/OID rather than guess from raw-code ordering.

For the initial V7 release, reject unsupported anonymous/nested functions or closures that cannot be unambiguously rebound, explaining which symbol/OID is unsupported. Do not claim general `__qualname__` support solely because a simple module-level `calc.f` works. Support saved aliases to supported named functions, methods, and classes. Test bytecode reordering and both renamed and deleted symbols.

### 5.2 Build the new runtime world without importing the old state twice

Design an explicit upgrade-only restoration mode in `p9image.c` / `p9space.c`: load the **old** snapshot under its matching old definitions, prepare the **new** module contexts/definitions separately, then build a coherent new graph preserving existing OIDs. Do **not** pass the V6 foreign-marked `space.view` objects into a normal commit. Record the old->new object map before populating dicts/sets to preserve cycles and shared references. Rehash dict/set keys only after final rebinding; do not mutate hashed keys in place across a class change.

Modules and class definitions need actual executable objects, not only unloaded `mp_raw_code_t` pointers. Verify exactly how `.mpy` modules and class bodies are instantiated in this MicroPython port; handle top-level side effects as restricted by I9. If safety cannot be ensured, refuse the upgrade rather than executing unknown module code as a purportedly atomic operation.

Explicitly handle `O_FUN`, `O_CLASS`, `O_INSTANCE`, `O_CLOSURE`, bound methods, `staticmethod`/`classmethod`, positional and keyword defaults, base classes, and `__main__` globals. For unsupported constructs, raise a deterministic upgrade error *before* publication. A schema-equal instance can retain its members; when schema differs, run the hook with the old state accessible as specified in I5.

Do not create duplicate persistent OIDs for the replacement symbols. Rebuilding the graph can change Python pointers; remap the OID table accordingly. Force dirty/re-encode of any function/class whose persisted representation must change; a plain update to the Environment string cannot fix old serialized function records.

### 5.3 Upgrade sequencing

1. **Preflight** (no mutations): validate Space exists, has a published `obj` store, `new_env` and its blobs exist and match hashes/ABI, version and size limits, symbol compatibility, schema hooks, available disk space, and current pin/catalog consistency.
2. **Lock:** acquire the exact existing Space `DMEXCL` lock; re-read the current published `N` under lock. If already locked, fail without altering data. All subsequent operations occur in an upgrade-only process with a clean VM state.
3. **Prepare:** read `env_at(N)` and version `N`; restore the old graph, construct new definitions and symbol map, then migrate the graph in memory. Check OID uniqueness, roots, shared references, cycles, class invariants, and persistability. No GEFS publication yet.
4. **Stage version `N+1`:** begin the existing `Kobj` transaction; write all changed object versions, the Environment-epoch catalog and prospective `pin_version=N`; then write the new header. `N` is still visible until the final published header.
5. **Publish:** send the one final commit record with sync and await completion. If the operation fails before publication, abort or let GEFS discard `W>N` on recovery. If it fails **after** publication (including post-commit history sweep), report the committed version truthfully; never report or attempt a rollback that would erase a published upgrade.
6. **Verify in a fresh process:** `space.open(name)` reports `(N+1, new_env)`, old references execute new code, persistent OIDs and relations match; `space.open(name, version=N)` reports `(N, old_env)` and uses old code. Confirm pin survives many ordinary commits and a restart.

Because GEFS commit publication and sync can be interrupted, do not infer success solely from the caller's exception/timeout. On restart, inspect the committed header and epoch catalog. Re-running `space.upgrade` must not double-apply migration; reject an already-applied `(old_env -> new_env)` attempt or provide a verified idempotent response.

## 6. Concrete files to inspect and modify

- **`sys/src/cmd/micropython/p9space.c`** — `space.upgrade`, lock, preflight, old/new Environment handling, version-aware Environment selection in `open` and `view`, upgraded header parsing, forced commit, error paths.
- **`sys/src/cmd/micropython/p9image.c` and `p9space.h`** — named function/class serialization and legacy decoding, upgrade-only graph conversion, OID/alias/cycle preservation, old/new symbol rebinding, reserved metadata OID exclusion.
- **`sys/src/9/gefs/obj.c`** — header pin and retention (`h.k`), internal catalog payload protocol via existing `Kobj`, atomic stage/publication and crash semantics; preserve T5.12 and T5.13 behaviors.
- **`sys/src/9/gefs/gdat.h`, `pack.c`, other GEFS files** — touch only if a proven protocol/format requirement cannot be localized. Keep 9front disk-block layout unchanged.
- **`sys/src/cmd/micropython/py/*`** — only minimal, tested runtime support actually needed for stable function/type binding; no speculative MicroPython VM rewrite.
- **`tools/test-space.sh`, `sys/lib/python/test/space/*`** — V7 tests and QEMU kill/reboot phases. Regenerate MicroPython qstr headers through the normal `genhdr` build path.
- **`docs/object-model.md`, `docs/gefs-obj.md`, `docs/status.md`** — document exact implemented behavior, limitations, layout and testing; keep historical findings accurate.

## 7. Acceptance tests (mandatory)

| ID | Experiment | Pass condition |
|---|---|---|
| **T7.1** | `calc` v1->v2, persistent instance/data and changed method body | OIDs, state, graph edges and saved aliases preserved; new code executes after reboot. |
| **T7.2** | Store `f = calc.foo` as a global and inside `list`, `dict`, another object's attribute; reorder raw-code children in v2 | Every alias executes v2 code; no stale index-path binding. |
| **T7.3** | Change `C.__schema__` without `C.__migrate__` | Upgrade rejected, `N`, `old_env`, data and old recovery pin unchanged. A valid hook runs exactly once per instance. |
| **T7.4** | Kill QEMU before/during object staging, just before final publication, during sync, after acknowledged sync | On each restart: entirely old or entirely new `(env, graph)`, never a mixture; no half-migrated graph; `gefsck` reports its normal result. |
| **T7.5** | Upgrade changes only `.mpy`/Environment, no user data | Published `N+1` exists; new function code runs after restart. |
| **T7.6** | Do 100 ordinary commits after upgrade and reboot | Pre-upgrade version remains readable with **old** Environment and old code. `K <= pin` throughout. |
| **T7.7** | Upgrade again to v3, then commit | Previous pin replaced atomically by version just before second upgrade; latest/previous code maps correctly; no invalid Environment epochs. |
| **T7.8** | Deleted/renamed/ambiguous function, unsupported closure, missing new `.mpy`, invalid Runtime/ABI | Each fails with useful diagnostic and zero published change. |
| **T7.9** | 64 MiB limit, no disk space, injected failure before header, failure after published header | No partial publication; post-publication cleanup error must not roll back committed version. |
| **T7.10** | Cyclic object graph; same object through multiple paths; self-reference; dict/set keys with methods/classes | Stable OIDs, topology, aliasing and usable hashing after migration. |
| **T7.11** | V5/V6 store (72-byte header) upgraded first time; old `v<N>`-only Space | Legacy decoding works; unsupported source format explicitly rejected without corrupting files. |
| **T7.12** | Concurrent writer vs. upgrade; reader of pinned old version during 100 commits | One writer only; reader gets a consistent version and correct historical code. |

All first four tests from `docs/object-model.md` (**T7.1–T7.4**) are release gates, not optional demonstrations. Add T7.5–T7.12 wherever the implementation affects those invariants. Cover special `OID_UPGRADE_META` encode/decode and K-boundary logic in a focused kernel test (`fsobj` or a new ktest).

Run at least:

```sh
# In own branch; use documented VM and build prerequisites.
tools/build.sh
ONLY=7 CRASH=3 tools/test-space.sh
PHASE=7 CRASH=3 tools/test-space.sh
KTEST=fsobj tools/test-gefs.sh
NOROOT=1 CRASH=3 tools/test-gefs.sh
# When relevant: HW=1 tools/test-space.sh and spacehw on silli.
```

Avoid assuming a `PASS` from a previous commit applies to changed code. If `PHASE=7` becomes too large to finish, show precisely which phase/test failed or timed out and never mark V7 complete. Check `gefsck` output and preserve serial logs/crash images on failures. Add failure injection for GEFS `h`/`c` boundaries rather than relying only on nondeterministic kill timing. Rerun `ONLY=3`, `ONLY=4`, `ONLY=5`, `ONLY=6` where changes affect their code paths.

## 8. Explicit non-goals and deferred work

- No `gefs salvage`/whole-disk rebuild and no GEFS physical block-header rewrite.
- No opportunistic crash-log tail reclamation. Commit `2bda0a7` classifies the anomaly; 10–20 **repeated crash cycles against one image** are a separate pending durability test.
- No automatic hot upgrade of a running Space; no cross-Space object mutation or cross-Space tag format changes.
- No lazy 30,000-object restore optimization in V7 unless required for correctness. Its ~8.3 s cost is a measured performance issue, not a blocker.
- No claim of transactional rollback of external I/O in `__migrate__`/module initialization.
- >64 MiB migrations: first release rejects before publication. Later, design staged durable batches with hidden version `W>N`, explicit resumability, pin-safe garbage collection, and **one** final publication, then test >64 MiB QEMU crashes. Do not merely raise `Objtxmax` without analysis.
- General Python function migration (anonymous closures, dynamic functions, unexported nested code) is not silently considered supported. Document deterministic errors and extend only with corresponding tests.

## 9. Delivery checklist and agent report

Before declaring V7 done, provide:

- [ ] A brief on-disk/protocol layout note: named object records; legacy compatibility; metadata epoch catalog; header pin; exact failure boundaries.
- [ ] Implemented, exported `space.upgrade(name, new_env)` with exclusive lock and explicit error reporting.
- [ ] Verified upgrade of named functions/classes including saved references, plus schema migration and preserved OIDs/graph edges.
- [ ] Atomic forced Environment publication, version-matched `space.open/version` and `space.view/version`, and persistent pin across many commits/reboots.
- [ ] Added and run T7.1–T7.4, required T7.5–T7.12, `PHASE=7` regression, and GEFS tests; report exact results and skipped environments.
- [ ] No regression of V5 T5.12/T5.13 or V6 view/foreign-OID rules.
- [ ] `docs/object-model.md`/`docs/status.md` updated; commit(s) describe what changed and why, attribution according to `AGENTS.md`.

**Agent response format:** `Base SHA`, `Design decisions`, `Changed files`, `Tests run (exact commands/results)`, `Not run (why)`, `Open correctness risks`, `Commit SHAs`. Only mark **V7 complete** when all release gates pass. Do not replace evidence with statements such as "implementation plan complete".
