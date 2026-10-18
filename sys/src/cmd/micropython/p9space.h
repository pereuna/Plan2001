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

// the image's body: the Space's modules and what they reach
void p9_image_encode(vstr_t *out);
void p9_image_restore(const byte *p, size_t n);

// a commit point (mp -S; FORCE: the program's end, within space.atomic
// too): 0 done or nothing to do, -1 failed (printed)
int p9_space_commit(int force);
// a rest, before a call blocks: a commit if due; CommitError
void p9_space_rest(void);
// main.c: a program's end in a Space
int p9_space_isopen(void);
