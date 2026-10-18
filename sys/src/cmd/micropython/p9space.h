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
};

// V2-V4's image (v<N>): restored once, its objects then given OIDs
void p9_image_restore(const byte *p, size_t n);
// the store (V5): its stream read (gefs-obj.md 3.2), the objects of
// version *n restored and given their OIDs; the header's user part out.
// VIEW (space.view): restored into the view's modules and __main__,
// the objects foreign (no OIDs, a commit refuses them)
void p9_image_restore_store(const byte *p, size_t n, uint64_t *version, char *user, size_t *nuser, void *view);
// a view's module NAME (loaded once, not run, not in sys.modules) and
// its __main__'s dict (p9space.c)
p9_space_mod_t *p9_view_loadmod(void *view, qstr name);
mp_obj_t p9_view_main(void *view);
// a commit's records: the changed objects (new OIDs from *next) and
// the freed, given to write (0 written, -1 failed: all rolled back).
// Returns 1 written, 0 nothing changed, -1 write's failure
int p9_image_commit(uint64_t *next, int (*write)(vstr_t *rec, void *a), void *a);
// $spacecheck: every object reached encoded again and compared with
// what the store has: the number differing (printed)
int p9_image_check(void);
// the Space's OIDs forgotten (a failed open); the views' kept
void p9_image_reset(void);

// a commit point (mp -S; FORCE: the program's end, within space.atomic
// too): 0 done or nothing to do, -1 failed (printed)
int p9_space_commit(int force);
// a rest, before a call blocks: a commit if due; CommitError
void p9_space_rest(void);
// main.c: a program's end in a Space
int p9_space_isopen(void);
// $spacetime: WHAT's time since T0 (nsec()) on fd 2
void p9_spacetime(const char *what, long long t0);
