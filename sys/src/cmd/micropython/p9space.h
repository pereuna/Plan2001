/* The persistent object environment: p9space.c and p9image.c */

// a module loaded from the Environment: its context and bytecode tree
typedef struct _p9_space_mod_t {
    qstr name;
    mp_module_context_t *ctx;
    mp_raw_code_t *rc;
} p9_space_mod_t;

extern const mp_obj_type_t mp_type_CommitError;

p9_space_mod_t *p9_space_mods(int *n);
// the module NAME of the Environment, loaded without running it
p9_space_mod_t *p9_space_loadmod(qstr name);

// a store's OIDs: 1 the roots' record (n (name OID)*), then the objects
enum {
    OID_ROOTS = 1,
    OID_FIRST = 2,
    // V7 (docs/v7-upgrade.md 4.1): the Environment epochs, an internal
    // record of p9image.c's (not a MicroPython object).  UINT64_MAX is
    // obj.c's "no object selected" sentinel, so the one below it.
    OID_UPGRADE_META = 0xfffffffffffffffeULL,
    // Stage 4 (docs/o-volume-format.md): a native O Space's graph and
    // its Environment manifest.  The definitions' code objects start at
    // OID_CODE; the host preparation tool assigns them explicitly.
    OID_GRAPH = 0xfffffffffffffffdULL,
    OID_ENV = 0xfffffffffffffffcULL,
    OID_CODE = 0xffffffff00000000ULL,
};

// V2-V4's image (v<N>): restored once, its objects then given OIDs
void p9_image_restore(const byte *p, size_t n);
// the store (V5): its stream read (gefs-obj.md 3.2), the objects of
// version *n restored and given their OIDs; the header's user part out.
// VIEW (space.view): restored into the view's modules and __main__,
// the objects foreign (no OIDs, a commit refuses them)
void p9_image_restore_store(const byte *p, size_t n, uint64_t *version, char *user, size_t *nuser, void *view);
// V7: the restored graph rebound to the new Environment's definitions
// (WORLD: p9_world_env's), each function, class and instance, and each
// __schema__ change migrated by its class's __migrate__.  Every OID is
// kept; the graph is re-encoded in full by the commit.  A failure
// raises, and nothing is published.
void p9_image_upgrade(void *world);
// the symbol table of MODULE's definition (its source compiled), for
// p9_image_upgrade: the qualnames of its functions and classes.  The
// source is compiled under SOURCE_FILE, the name the definition's .mpy
// was compiled with (its qstr table's first entry).  A raised error:
// the definition cannot be read.
void p9_image_symtab(qstr module, const char *source, qstr source_file);
// and it must describe the definition's raw code (the .mpy's tree),
// and its source recompiled must be the .mpy at MPYPATH
void p9_image_symcheck(qstr module, const mp_raw_code_t *rc, const char *mpypath);
// a failed upgrade before p9_image_upgrade: its symbol tables forgotten
void p9_image_upgrade_clean(void);
// a world's module NAME (loaded once, not run, not in sys.modules) and
// its __main__'s dict (p9space.c): the world is what a view and an
// upgrade hand to p9_image_restore_store / p9_image_upgrade
p9_space_mod_t *p9_view_loadmod(void *view, qstr name);
mp_obj_t p9_view_main(void *view);

// V7: the upgrade's new Environment, a world of its own (p9space.c):
// its modules are loaded from an Environment into its own contexts, not
// in sys.modules, and their globals are the Space's own dicts (the
// roots) so that the rebound objects see the same state
void *p9_world_env(const char *lib, mp_obj_t envmap);
p9_space_mod_t *p9_world_loadmod(void *w, qstr name);
// the module's definition (its .py's path: LIB/mod/SHA.py) and the
// Environment's definition for the name (NULL: not in the Environment)
const char *p9_world_defpath(void *w, qstr name, char *buf, size_t n);
// and its .mpy's path (LIB/mod/SHA.mpy)
const char *p9_world_defmpypath(void *w, qstr name, char *buf, size_t n);
const char *p9_world_defsha(void *w, qstr name);
// 1: the same definition as the Space's Environment has (its code, so
// its paths, are the same and no name is needed)
int p9_world_unchanged(void *w, qstr name);
p9_space_mod_t *p9_world_mods(void *w, int *n);
// a commit's records: the changed objects (new OIDs from *next) and
// the freed, given to write (0 written, -1 failed: all rolled back).
// FORCE: write even when no object changed (an Environment or a
// catalog only).  Returns 1 written, 0 nothing changed, -1 failure
int p9_image_commit(uint64_t *next, int (*write)(vstr_t *rec, void *a), void *a, int force);
// every object of the graph changed (an upgrade: all records rewritten)
void p9_image_all_dirty(void);
// $spacecheck: every object reached encoded again and compared with
// what the store has: the number differing (printed)
int p9_image_check(void);
// space.oid: p's OID, 0 none (not committed yet, a view's)
uint64_t p9_image_oid(const void *p);
// the Space's OIDs forgotten (a failed open); the views' kept
void p9_image_reset(void);

// Stage 4: use the native O volume SPEC (a PARTUUID or device) for the
// Space, its Environment and its code; 0 ok, -1 failed (printed)
int p9_space_o(const char *spec);
// Stage 5 (mp -O ... -S NAME -B): the first boot of a fresh O Space
// imports the O-resident demo module, initializes it once and persists
// its run global; later boots restore that global and call it without
// running any module top level.  Failure is printed and nothing is
// committed.  0 ok, -1 failed
int p9_space_boot(void);
// Stage 5: after the commit, the persistent Counter's value, alias and
// OIDs (the same object across boots); failure is printed
int p9_space_report(void);

// a commit point (mp -S; FORCE: the program's end, within space.atomic
// too): 0 done or nothing to do, -1 failed (printed)
int p9_space_commit(int force);
// a rest, before a call blocks: a commit if due; CommitError
void p9_space_rest(void);
// main.c: a program's end in a Space
int p9_space_isopen(void);
// $spacetime: WHAT's time since T0 (nsec()) on fd 2
void p9_spacetime(const char *what, long long t0);
