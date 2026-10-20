# Plan2001 V7 — P1/P2 Review Fixes

- **Review target:** `space-v7` at `f3ff602` (PR #2)
- **Date:** 2026-10-20
- **Merge rule:** Address these findings before merging. Keep the existing
  GEFS on-disk block format and V5/V6 store compatibility.

## P1 — Preserve persistent method OIDs

**File:** `sys/src/cmd/micropython/p9image.c`
(`upg_funs`, `bindfun`, `classmembers`, `oidmove`).

`upg_funs()` can move a method's OID to a temporary replacement function;
`classmembers()` then installs a separately created class-body method and
attempts to move the OID from the original function again. That OID is no
longer attached to the original.

**Fix:** Establish one canonical replacement per old method and transfer its
OID exactly once to the function ultimately installed in the upgraded class.
Reconcile aliases with that same replacement; reject conflicting OID
ownership.

**Regression:** Save method references in a class, list, dict and instance
attribute; upgrade, reboot, and assert identical OIDs, aliases and new
method behavior.

## P1 — Reject ambiguous symbol identities

**File:** `sys/src/cmd/micropython/p9image.c`
(`p9_image_symtab`, `symfind`, `p9_image_symcheck`).

Duplicate qualified names currently mark only a later entry ambiguous,
while `symfind()` can return the earlier, apparently valid entry. Checking
only the number of `.mpy` scopes also does not prove source/code
correspondence.

**Fix:** Mark all duplicate `(module, qualname)` matches ambiguous; reject
them before publication. Validate scope kind, complete path, and code-tree
correspondence, not just the scope count.

**Regression:** Two definitions of the same name and altered scope ordering
must fail safely (no version or Environment change), rather than silently
bind the wrong bytecode.

## P1/P2 — Require a valid Environment epoch catalog for V7

**File:** `sys/src/cmd/micropython/p9space.c`
(`readcat`, `space_open`, `space_view`).

A missing catalog is currently accepted as a legacy case even with a V7
(`fmt=2`) header. Historical reads can then fall back to the latest
Environment.

**Fix:** Permit a missing catalog only for V5/V6 (`fmt=1`). For V7, require a
valid, ordered catalog covering the requested version and consistent with
the latest header Environment. Fail closed if it is absent, corrupt or
inconsistent.

**Regression:** Delete or corrupt the catalog on a copied V7 store;
historical open/view must reject it. V5/V6 72-byte headers must still open.

## P2 — Define safe `__migrate__` old-state semantics

**File:** `sys/src/cmd/micropython/p9image.c` (`oldstate`, `upg_migrate`).

`old` is currently a mutable shallow dict copy: nested lists/dicts remain
shared, and graph rewriting may have already changed their references. It
is not the read-only pre-upgrade snapshot promised by invariant I5.

**Fix:** Provide a genuinely read-only pre-migration view/snapshot, or
explicitly narrow I5 and reject migrations that need stronger isolation. Do
not describe a shallow copy as immutable.

**Regression:** A migration attempting to mutate `old` or shared nested
state must not alter another object's pre-upgrade state.

## P2 — Verify code identity and provenance

**Files:** `sys/src/cmd/micropython/p9space.c` (`loadmpy`),
`p9image.c` (`p9_image_symcheck`).

The Environment text is SHA-256 checked, but `.mpy` bytes are not
independently authenticated against an expected digest; matching scope
counts is insufficient.

**Fix:** Bind immutable source and compiled `.mpy` bytes with an explicitly
verified digest/manifest or deterministic recompilation and byte
comparison. Verify the runtime/MPY ABI and full symbol mapping before
rebinding objects.

**Regression:** Tampered or swapped `.mpy` with a plausible code tree must
fail before publication; unchanged legitimate definitions must still load.

## Merge gate

- Add targeted regression tests for every item.
- Rerun `ONLY=7` and `PHASE=7` Space tests.
- Rerun GEFS `KTEST=fsobj`/`KTEST=all`.
- Rerun the crash tests.
- Report actual commands and results.
- Do not merge while any P1 remains unresolved or documented I3/I5/I6/I8
  invariants are violated.
