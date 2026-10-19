/*
 * A Space's image (docs/object-model.md 5.5, 6.1): the instances of its
 * modules (__main__ and the Environment's) and every object they
 * reach, with shared references and cycles; restored without running
 * the modules' top levels, class bodies, __init__ or decorators.
 *
 * The body: objects, then roots.
 *   body    nobj object* nroot (name ref)*
 *   object  kind fields          (index = position; kinds below)
 *   value   tag ...              (inline values, or a reference)
 * Numbers are unsigned varints (7 bits a byte, low first); a string is
 * its length and its bytes.  Strings, numbers and bytes are values;
 * what has an identity of its own (mutable, or holding such) is an
 * object.  Restoring makes every object's shell first, in an order
 * that has what each needs (classes before their instances, functions
 * before closures and methods), then fills them; dicts and sets last,
 * those whose keys may run a user's __hash__ after all the others.
 */
#include <string.h>
#include "py/runtime.h"
#include "py/bc.h"
#include "py/objfun.h"
#include "py/objtype.h"
#include "py/objmodule.h"
#include "py/objstr.h"
#include "py/objtuple.h"
#include "py/objlist.h"
#include "py/parsenum.h"
#include "py/emitglue.h"
#include "p9space.h"

extern const mp_obj_type_t mp_type_closure;
extern const mp_obj_module_t mp_module___main__;
long long nsec(void);

enum {	/* values */
    V_NONE, V_FALSE, V_TRUE, V_ELLIPSIS,
    V_INT,		/* zigzag */
    V_BIGINT,	/* decimal */
    V_FLOAT,	/* 8 bytes */
    V_QSTR, V_STR, V_BYTES,
    V_MODULE,	/* a Space's module: name */
    V_BUILTIN,	/* module, attribute ("" the module) */
    V_REF,		/* index */
};

enum {	/* objects */
    O_TUPLE, O_LIST, O_DICT, O_SET, O_FROZENSET, O_BYTEARRAY, O_CELL,
    O_CLASS,	/* name, bases (n values), dict */
    O_INSTANCE,	/* class, n (key value) */
    O_FUN,		/* module, n path, n defaults, kw defaults (value or none) */
    O_CLOSURE,	/* function, n values */
    O_STATICMETH, O_CLASSMETH,	/* function */
    O_BOUNDMETH,	/* method, self */
    O_NKIND
};

typedef struct _mp_obj_closure_t {	/* objclosure.c */
    mp_obj_base_t base;
    mp_obj_t fun;
    size_t n_closed;
    mp_obj_t closed[];
} closure_t;

typedef struct _mp_obj_bound_meth_t {	/* objboundmeth.c */
    mp_obj_base_t base;
    mp_obj_t meth;
    mp_obj_t self;
} boundmeth_t;

static const mp_obj_type_t *celltype(void) {
    static const mp_obj_type_t *t;

    if (t == NULL) {
        t = mp_obj_get_type(mp_obj_new_cell(mp_const_none));
    }
    return t;
}

/* ---- the builtin objects by name, both ways ---- */

typedef struct {
    const void *p;
    qstr mod, attr;
} builtin_t;
static builtin_t *bt;
static size_t nbt, abt;

static void btadd(const void *p, qstr mod, qstr attr) {
    size_t i;

    for (i = 0; i < nbt; i++) {
        if (bt[i].p == p) {
            return;	/* the first name: builtins.print, not another's */
        }
    }
    if (nbt == abt) {
        abt = abt ? 2 * abt : 512;
        bt = realloc(bt, abt * sizeof bt[0]);
    }
    bt[nbt].p = p;
    bt[nbt].mod = mod;
    bt[nbt].attr = attr;
    nbt++;
}

static void btmap(const mp_map_t *map) {
    size_t i, j;
    mp_obj_t mod;
    mp_map_t *g;

    for (i = 0; i < map->alloc; i++) {
        if (!mp_map_slot_is_filled(map, i) || !mp_obj_is_type(map->table[i].value, &mp_type_module)
            || map->table[i].value == MP_OBJ_FROM_PTR(&mp_module___main__)) {
            continue;	/* __main__'s are the Space's */
        }
        mod = map->table[i].value;
        btadd(MP_OBJ_TO_PTR(mod), mp_obj_str_get_qstr(map->table[i].key), MP_QSTRnull);
        g = &mp_obj_module_get_globals(mod)->map;
        for (j = 0; j < g->alloc; j++) {
            if (mp_map_slot_is_filled(g, j) && mp_obj_is_obj(g->table[j].value)) {
                btadd(MP_OBJ_TO_PTR(g->table[j].value), mp_obj_str_get_qstr(map->table[i].key),
                    mp_obj_str_get_qstr(g->table[j].key));
            }
        }
    }
}

static builtin_t *builtin(const void *p) {
    size_t i;

    if (bt == NULL) {
        btadd(&mp_type_object, MP_QSTR_builtins, MP_QSTR_object);
        btmap(&mp_builtin_module_map);
        btmap(&mp_builtin_extensible_module_map);
    }
    for (i = 0; i < nbt; i++) {
        if (bt[i].p == p) {
            return &bt[i];
        }
    }
    return NULL;
}

/* ---- the OIDs: object -> OID, weak (the GC's frees take entries out) ---- */

typedef struct {
    const void *p;	/* NULL: empty */
    uint64_t oid;
    uint64_t hash;	/* of its record in the store */
    byte dirty;	/* changed in place since (P9_DIRTY) */
    byte isnew;	/* given its OID by the commit going on */
    byte mark;	/* in the commit's (or check's) work */
    byte foreign;	/* a view's (space.view): no OID, never committed */
} oent_t;

size_t p9_space_noid;	/* entries: the barriers on */
static oent_t *ot;
static size_t otsize;	/* a power of 2, or 0 */
static const void **dirtyv;	/* what P9_DIRTY marked */
static size_t ndirty, adirty;
static uint64_t *deadv;	/* the OIDs of the objects freed */
static size_t ndead, adead;
static int dirtylost;	/* dirtyv not grown: see the table */
static uint64_t roothash;	/* of the roots' record (OID 1) */

static size_t ohash(const void *p) {
    return (size_t)((((uintptr_t)p >> 3) * 0x9E3779B97F4A7C15ULL) >> 20) & (otsize - 1);
}

static oent_t *oget(const void *p) {
    size_t h;

    if (otsize == 0) {
        return NULL;
    }
    for (h = ohash(p); ot[h].p != NULL; h = (h + 1) & (otsize - 1)) {
        if (ot[h].p == p) {
            return &ot[h];
        }
    }
    return NULL;
}

static void oput(const oent_t *x) {
    size_t h;

    for (h = ohash(x->p); ot[h].p != NULL; h = (h + 1) & (otsize - 1)) {
    }
    ot[h] = *x;
}

// p's entry made (C's heap: the GC never sees it, so it keeps nothing)
static void oadd(const void *p, uint64_t oid, uint64_t hash) {
    oent_t *old, x;
    size_t n, i;

    if (2 * (p9_space_noid + 1) > otsize) {
        old = ot;
        n = otsize;
        otsize = otsize ? 2 * otsize : 4096;
        ot = calloc(otsize, sizeof ot[0]);
        if (ot == NULL) {
            ot = old;
            otsize = n;
            mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("space: OID table"));
        }
        for (i = 0; i < n; i++) {
            if (old[i].p != NULL) {
                oput(&old[i]);
            }
        }
        free(old);
    }
    memset(&x, 0, sizeof x);
    x.p = p;
    x.oid = oid;
    x.hash = hash;
    oput(&x);
    p9_space_noid++;
}

// p's entry out, the ones after it moved back (linear probing)
static void odel(const void *p) {
    oent_t *x = oget(p);
    size_t i, j, h;

    if (x == NULL) {
        return;
    }
    i = x - ot;
    ot[i].p = NULL;
    p9_space_noid--;
    for (j = (i + 1) & (otsize - 1); ot[j].p != NULL; j = (j + 1) & (otsize - 1)) {
        h = ohash(ot[j].p);
        /* j's entry can fill i if its home is not in (i, j] */
        if ((j > i && (h <= i || h > j)) || (j < i && (h <= i && h > j))) {
            ot[i] = ot[j];
            ot[j].p = NULL;
            i = j;
        }
    }
}

static void *grown(void *v, size_t *a, size_t n, size_t sz) {
    void *w;

    if (n < *a) {
        return v;
    }
    w = realloc(v, (*a ? 2 * *a : 256) * sz);
    if (w == NULL) {
        return NULL;
    }
    *a = *a ? 2 * *a : 256;
    return w;
}

// P9_DIRTY: an object with an OID changed in place (a dict's, an
// instance's map, a list, a set, a bytearray, a cell)
void p9_space_dirty(const void *p) {
    oent_t *x = oget(p);
    const void **v;

    if (x == NULL || x->dirty || x->foreign) {
        return;
    }
    x->dirty = 1;
    if ((v = grown(dirtyv, &adirty, ndirty, sizeof v[0])) == NULL) {
        dirtylost = 1;	/* the commit looks through the table */
        return;
    }
    dirtyv = v;
    dirtyv[ndirty++] = p;
}

// P9_FREED: the GC freed p (in its sweep: nothing of the GC's heap
// here); an object with an OID: a tombstone at the next commit
void p9_space_freed(const void *p) {
    oent_t *x = oget(p);
    uint64_t *v;

    if (x == NULL) {
        return;
    }
    if (!x->isnew && !x->foreign && (v = grown(deadv, &adead, ndead, sizeof v[0])) != NULL) {
        deadv = v;
        deadv[ndead++] = x->oid;
    }
    odel(p);
}

uint64_t p9_image_oid(const void *p) {
    oent_t *x = oget(p);

    return x == NULL || x->foreign || x->isnew ? 0 : x->oid;
}

void p9_image_reset(void) {
    oent_t *old = ot;
    size_t n = otsize, i;

    /* the views' entries kept: theirs stay foreign */
    ot = NULL;
    otsize = 0;
    p9_space_noid = 0;
    for (i = 0; i < n; i++) {
        if (old[i].p != NULL && old[i].foreign) {
            oadd(old[i].p, 0, 0);
            oget(old[i].p)->foreign = 1;
        }
    }
    free(old);
    ndirty = ndead = 0;
    dirtylost = 0;
    roothash = 0;
}

static uint64_t fnv(const char *p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ULL;

    while (n-- > 0) {
        h = (h ^ (byte)*p++) * 0x100000001b3ULL;
    }
    return h;
}

/* ---- encoding ---- */

typedef struct {
    vstr_t *out;		/* one object's record */
    mp_obj_t *obj;		/* the work: what is encoded (the GC's heap: kept) */
    int *parent;		/* for an error's path */
    mp_obj_t *label;
    uint64_t *hash;		/* of each one's record */
    size_t n, a;
    mp_obj_t mainmod;
    uint64_t *next;		/* the next OID */
    int check;		/* $spacecheck: no OIDs given */
    int bad;
} enc_t;

static void putu(vstr_t *v, uint64_t u) {
    while (u >= 0x80) {
        vstr_add_byte(v, (u & 0x7f) | 0x80);
        u >>= 7;
    }
    vstr_add_byte(v, u);
}

static void putstr(vstr_t *v, const char *s, size_t n) {
    putu(v, n);
    vstr_add_strn(v, s, n);
}

static void put64(vstr_t *v, uint64_t u) {
    int i;

    for (i = 56; i >= 0; i -= 8) {
        vstr_add_byte(v, (byte)(u >> i));
    }
}

// the path to work item i (and then label), for an error
static void path(enc_t *e, int i, mp_obj_t label, vstr_t *v) {
    if (i >= 0) {
        path(e, e->parent[i], e->label[i], v);
    }
    if (label == MP_OBJ_NULL) {
        return;
    }
    if (mp_obj_is_small_int(label)) {
        vstr_printf(v, "[%d]", (int)MP_OBJ_SMALL_INT_VALUE(label));
    } else if (mp_obj_is_str(label)) {
        vstr_printf(v, i >= 0 ? ".%s" : "%s", mp_obj_str_get_str(label));
    } else {
        vstr_add_str(v, "[key]");
    }
}

static MP_NORETURN void notpersistent(enc_t *e, mp_obj_t o, int parent, mp_obj_t label, const char *why) {
    vstr_t v;

    vstr_init(&v, 64);
    path(e, parent, label, &v);
    mp_raise_msg_varg(&mp_type_CommitError, MP_ERROR_TEXT("not persistent: %s at %s"),
        why != NULL ? why : qstr_str(mp_obj_get_type(o)->name), vstr_null_terminated_str(&v));
}

// a class of the user's whose bases are the user's too (or object)
static bool pureclass(const mp_obj_type_t *t) {
    const void *p;
    size_t n, i;
    mp_obj_t *b;

    if (t == &mp_type_object) {
        return true;
    }
    if (!mp_obj_is_instance_type(t)) {
        return false;
    }
    if (!MP_OBJ_TYPE_HAS_SLOT(t, parent)) {
        return true;
    }
    p = MP_OBJ_TYPE_GET_SLOT(t, parent);
    if (((mp_obj_base_t *)p)->type == &mp_type_tuple) {
        mp_obj_tuple_get(MP_OBJ_FROM_PTR(p), &n, &b);
        for (i = 0; i < n; i++) {
            if (!pureclass(MP_OBJ_TO_PTR(b[i]))) {
                return false;
            }
        }
        return true;
    }
    return pureclass(p);
}

static int okind(enc_t *e, mp_obj_t o, int parent, mp_obj_t label) {
    const mp_obj_type_t *t = mp_obj_get_type(o);

    if (t == &mp_type_tuple) return O_TUPLE;
    if (t == &mp_type_list) return O_LIST;
    if (t == &mp_type_dict) return O_DICT;
    if (t == &mp_type_set) return O_SET;
    #if MICROPY_PY_BUILTINS_FROZENSET
    if (t == &mp_type_frozenset) return O_FROZENSET;
    #endif
    if (t == &mp_type_bytearray) return O_BYTEARRAY;
    if (t == celltype()) return O_CELL;
    if (t == &mp_type_fun_bc || t == &mp_type_gen_wrap) return O_FUN;
    if (t == &mp_type_closure) return O_CLOSURE;
    if (t == &mp_type_staticmethod) return O_STATICMETH;
    if (t == &mp_type_classmethod) return O_CLASSMETH;
    if (t == &mp_type_bound_meth) return O_BOUNDMETH;
    if (t == &mp_type_type) {
        if (!pureclass(MP_OBJ_TO_PTR(o))) {
            notpersistent(e, o, parent, label, "a class with a builtin base");
        }
        return O_CLASS;
    }
    if (mp_obj_is_instance_type(t)) {
        if (!pureclass(t)) {
            notpersistent(e, o, parent, label, "an instance of a class with a builtin base");
        }
        return O_INSTANCE;
    }
    notpersistent(e, o, parent, label, NULL);
}

// a work item: o encoded by this commit (or check)
static void push(enc_t *e, mp_obj_t o, int parent, mp_obj_t label) {
    if (e->n == e->a) {
        e->a = e->a ? 2 * e->a : 256;
        e->obj = m_renew(mp_obj_t, e->obj, e->n, e->a);
        e->parent = m_renew(int, e->parent, e->n, e->a);
        e->label = m_renew(mp_obj_t, e->label, e->n, e->a);
        e->hash = m_renew(uint64_t, e->hash, e->n, e->a);
    }
    e->obj[e->n] = o;
    e->parent[e->n] = parent;
    e->label[e->n] = label;
    e->n++;
}

// o's OID; one new given (and o encoded too) if it has none
static uint64_t ref(enc_t *e, mp_obj_t o, int parent, mp_obj_t label) {
    oent_t *x = oget(MP_OBJ_TO_PTR(o));
    uint64_t oid;
    vstr_t v;

    if (x != NULL && x->foreign) {
        if (e->check) {
            vstr_init(&v, 64);
            path(e, parent, label, &v);
            mp_printf(&mp_plat_print, "space: check: %s at %s: a foreign object\n",
                qstr_str(mp_obj_get_type(o)->name), vstr_null_terminated_str(&v));
            e->bad++;
            return 0;
        }
        notpersistent(e, o, parent, label, "a foreign object (space.view)");
    }
    if (x != NULL) {
        if (e->check && !x->mark) {
            x->mark = 1;
            push(e, o, parent, label);
        }
        return x->oid;
    }
    if (e->check) {
        vstr_init(&v, 64);
        path(e, parent, label, &v);
        mp_printf(&mp_plat_print, "space: check: %s at %s: no OID (not committed)\n",
            qstr_str(mp_obj_get_type(o)->name), vstr_null_terminated_str(&v));
        e->bad++;
        return 0;
    }
    okind(e, o, parent, label);	/* not persistent: an error now, with its path */
    oid = (*e->next)++;
    oadd(MP_OBJ_TO_PTR(o), oid, 0);
    x = oget(MP_OBJ_TO_PTR(o));
    x->isnew = 1;
    x->mark = 1;
    push(e, o, parent, label);	/* x not used after: push may collect */
    return oid;
}

// the module a Space's module object is, or NULL
static qstr spacemod(enc_t *e, mp_obj_t o) {
    p9_space_mod_t *m;
    mp_map_elem_t *el;
    int n, i;

    if (o == e->mainmod) {
        return MP_QSTR___main__;
    }
    m = p9_space_mods(&n);
    for (i = 0; i < n; i++) {
        if (MP_OBJ_FROM_PTR(m[i].ctx) == o) {
            el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map, MP_OBJ_NEW_QSTR(m[i].name), MP_MAP_LOOKUP);
            if (el != NULL && el->value == o) {
                return m[i].name;
            }
        }
    }
    return MP_QSTRnull;
}

static void putv(enc_t *e, mp_obj_t o, int parent, mp_obj_t label) {
    vstr_t *v = e->out;
    builtin_t *b;
    mp_int_t i;
    size_t len;
    const char *s;
    double d;
    qstr q;
    vstr_t num;
    mp_print_t pr;

    if (o == mp_const_none) {
        vstr_add_byte(v, V_NONE);
    } else if (o == mp_const_false) {
        vstr_add_byte(v, V_FALSE);
    } else if (o == mp_const_true) {
        vstr_add_byte(v, V_TRUE);
    } else if (o == MP_OBJ_FROM_PTR(&mp_const_ellipsis_obj)) {
        vstr_add_byte(v, V_ELLIPSIS);
    } else if (mp_obj_is_small_int(o)) {
        i = MP_OBJ_SMALL_INT_VALUE(o);
        vstr_add_byte(v, V_INT);
        putu(v, i < 0 ? ((uint64_t)~i << 1) | 1 : (uint64_t)i << 1);
    } else if (mp_obj_is_int(o)) {
        vstr_init_print(&num, 32, &pr);
        mp_obj_print_helper(&pr, o, PRINT_REPR);
        vstr_add_byte(v, V_BIGINT);
        putstr(v, num.buf, num.len);
    } else if (mp_obj_is_float(o)) {
        d = mp_obj_float_get(o);
        vstr_add_byte(v, V_FLOAT);
        vstr_add_strn(v, (const char *)&d, 8);
    } else if (mp_obj_is_qstr(o)) {
        s = (const char *)qstr_data(MP_OBJ_QSTR_VALUE(o), &len);
        vstr_add_byte(v, V_QSTR);
        putstr(v, s, len);
    } else if (mp_obj_is_exact_type(o, &mp_type_str)) {
        s = mp_obj_str_get_data(o, &len);
        vstr_add_byte(v, V_STR);
        putstr(v, s, len);
    } else if (mp_obj_is_exact_type(o, &mp_type_bytes)) {
        s = mp_obj_str_get_data(o, &len);
        vstr_add_byte(v, V_BYTES);
        putstr(v, s, len);
    } else if (mp_obj_is_type(o, &mp_type_module) && (q = spacemod(e, o)) != MP_QSTRnull) {
        vstr_add_byte(v, V_MODULE);
        s = (const char *)qstr_data(q, &len);
        putstr(v, s, len);
    } else if (mp_obj_is_obj(o) && (b = builtin(MP_OBJ_TO_PTR(o))) != NULL) {
        vstr_add_byte(v, V_BUILTIN);
        s = (const char *)qstr_data(b->mod, &len);
        putstr(v, s, len);
        if (b->attr == MP_QSTRnull) {
            putstr(v, "", 0);
        } else {
            s = (const char *)qstr_data(b->attr, &len);
            putstr(v, s, len);
        }
    } else if (mp_obj_is_obj(o) && !mp_obj_is_type(o, &mp_type_module)) {
        vstr_add_byte(v, V_REF);
        putu(v, ref(e, o, parent, label));
    } else {
        notpersistent(e, o, parent, label, NULL);
    }
}

static void putfun(enc_t *e, size_t i, mp_obj_fun_bc_t *f) {
    vstr_t *v = e->out;
    p9_space_mod_t *m;
    int n, k, depth;
    const char *s;
    size_t len, np;
    const byte *ip;
    const mp_raw_code_t *rc;
    size_t idx[64];

    m = p9_space_mods(&n);
    for (k = 0; k < n; k++) {
        if (m[k].ctx == f->context && spacemod(e, MP_OBJ_FROM_PTR(m[k].ctx)) != MP_QSTRnull) {
            break;
        }
    }
    if (k == n) {
        notpersistent(e, e->obj[i], e->parent[i], e->label[i],
            f->context->module.globals == &MP_STATE_VM(dict_main)
            ? "a function defined in __main__" : "a function of a module not in the Space");
    }
    /* the path from the module's code to the function's: depth, indices */
    depth = 0;
    {
        /* breadth of the tree is small: walk it with an explicit stack */
        const mp_raw_code_t *st[64];
        size_t at[64];
        int d = 0;

        st[0] = m[k].rc;
        at[0] = 0;
        rc = NULL;
        if (m[k].rc->fun_data == f->bytecode) {
            rc = m[k].rc;
        }
        while (rc == NULL && d >= 0) {
            if (at[d] < st[d]->n_children) {
                const mp_raw_code_t *c = st[d]->children[at[d]];

                idx[d] = at[d];
                at[d]++;
                if (c->fun_data == f->bytecode) {
                    rc = c;
                    depth = d + 1;
                    break;
                }
                if (d + 1 < 64) {
                    d++;
                    st[d] = c;
                    at[d] = 0;
                }
            } else {
                d--;
            }
        }
        if (rc == NULL) {
            notpersistent(e, e->obj[i], e->parent[i], e->label[i], "a function not in its module's code");
        }
    }
    vstr_add_byte(v, mp_obj_get_type(MP_OBJ_FROM_PTR(f)) == &mp_type_gen_wrap);
    s = (const char *)qstr_data(m[k].name, &len);
    putstr(v, s, len);
    putu(v, depth);
    for (k = 0; k < depth; k++) {
        putu(v, idx[k]);
    }
    /* the defaults: positional ones, then the keyword dict */
    ip = f->bytecode;
    MP_BC_PRELUDE_SIG_DECODE(ip);
    (void)n_state; (void)n_exc_stack; (void)n_pos_args; (void)n_kwonly_args;
    np = n_def_pos_args;
    putu(v, np);
    for (k = 0; k < (int)np; k++) {
        putv(e, f->extra_args[k], i, MP_OBJ_NEW_SMALL_INT(k));
    }
    if (scope_flags & MP_SCOPE_FLAG_DEFKWARGS) {
        putv(e, f->extra_args[np], i, MP_OBJ_NEW_QSTR(MP_QSTR_kwargs));
    } else {
        vstr_add_byte(v, V_NONE);
    }
}

// a map's or set's entries, from START to the end of V, each ending at
// END[k]: sorted by their bytes, so that the record (and its hash) does
// not depend on the table's order, which a restore does not keep
typedef struct {
    size_t at, len;
} span_t;
static const char *spanbuf;

static int spancmp(const void *a, const void *b) {
    const span_t *x = a, *y = b;
    int c = memcmp(spanbuf + x->at, spanbuf + y->at, x->len < y->len ? x->len : y->len);

    return c != 0 ? c : (x->len > y->len) - (x->len < y->len);
}

static void sortentries(vstr_t *v, size_t start, const size_t *end, size_t n) {
    span_t *sp;
    char *tmp, *q;
    size_t k, at;

    if (n < 2) {
        return;
    }
    sp = malloc(n * sizeof sp[0]);
    tmp = malloc(v->len - start);
    if (sp == NULL || tmp == NULL) {
        free(sp);
        free(tmp);
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("space: sorting a map"));
    }
    for (at = start, k = 0; k < n; at = end[k++]) {
        sp[k].at = at;
        sp[k].len = end[k] - at;
    }
    spanbuf = v->buf;
    qsort(sp, n, sizeof sp[0], spancmp);
    for (q = tmp, k = 0; k < n; k++) {
        memcpy(q, v->buf + sp[k].at, sp[k].len);
        q += sp[k].len;
    }
    memcpy(v->buf + start, tmp, v->len - start);
    free(sp);
    free(tmp);
}

// END for N entries: C's heap (no GC amid an encoding)
static size_t *entryends(size_t n) {
    size_t *end = malloc((n ? n : 1) * sizeof end[0]);

    if (end == NULL) {
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("space: sorting a map"));
    }
    return end;
}

static void putobj(enc_t *e, size_t i, mp_obj_t transient) {
    vstr_t *v = e->out;
    mp_obj_t o = e->obj[i], *items, it, x;
    size_t n, k, j, start, *end;
    nlr_buf_t nlr;
    mp_map_t *map;
    const mp_obj_type_t *t;
    const void *p;
    int kind = okind(e, o, e->parent[i], e->label[i]);

    vstr_add_byte(v, kind);
    switch (kind) {
    case O_TUPLE:
    case O_LIST:
        mp_obj_get_array(o, &n, &items);
        putu(v, n);
        for (k = 0; k < n; k++) {
            putv(e, items[k], i, MP_OBJ_NEW_SMALL_INT(k));
        }
        break;
    case O_DICT:
        map = &((mp_obj_dict_t *)MP_OBJ_TO_PTR(o))->map;
        putu(v, map->used);
        end = entryends(map->used);
        if (nlr_push(&nlr) != 0) {
            free(end);
            nlr_jump(nlr.ret_val);
        }
        start = v->len;
        for (j = k = 0; k < map->alloc && j < map->used; k++) {
            if (mp_map_slot_is_filled(map, k)) {
                putv(e, map->table[k].key, i, MP_OBJ_NULL);
                /* a module's __transient__ names: None */
                if (transient != MP_OBJ_NULL && mp_obj_is_str(map->table[k].key)
                    && mp_binary_op(MP_BINARY_OP_CONTAINS, transient, map->table[k].key) == mp_const_true) {
                    vstr_add_byte(v, V_NONE);
                } else {
                    putv(e, map->table[k].value, i, map->table[k].key);
                }
                end[j++] = v->len;
            }
        }
        sortentries(v, start, end, j);
        nlr_pop();
        free(end);
        break;
    case O_SET:
    case O_FROZENSET:
        n = MP_OBJ_SMALL_INT_VALUE(mp_obj_len(o));
        putu(v, n);
        end = entryends(n);
        if (nlr_push(&nlr) != 0) {
            free(end);
            nlr_jump(nlr.ret_val);
        }
        start = v->len;
        it = mp_getiter(o, NULL);
        k = 0;
        while (k < n && (x = mp_iternext(it)) != MP_OBJ_STOP_ITERATION) {
            putv(e, x, i, MP_OBJ_NEW_SMALL_INT(k));
            end[k++] = v->len;
        }
        sortentries(v, start, end, k);
        nlr_pop();
        free(end);
        break;
    case O_BYTEARRAY: {
        mp_buffer_info_t b;

        mp_get_buffer_raise(o, &b, MP_BUFFER_READ);
        putstr(v, b.buf, b.len);
        break;
    }
    case O_CELL:
        putv(e, mp_obj_cell_get(o), i, MP_OBJ_NULL);
        break;
    case O_CLASS:
        t = MP_OBJ_TO_PTR(o);
        putstr(v, qstr_str(t->name), strlen(qstr_str(t->name)));
        if (!MP_OBJ_TYPE_HAS_SLOT(t, parent)) {
            putu(v, 0);
        } else {
            p = MP_OBJ_TYPE_GET_SLOT(t, parent);
            if (((mp_obj_base_t *)p)->type == &mp_type_tuple) {
                mp_obj_tuple_get(MP_OBJ_FROM_PTR(p), &n, &items);
                putu(v, n);
                for (k = 0; k < n; k++) {
                    putv(e, items[k], i, MP_OBJ_NEW_QSTR(MP_QSTR___bases__));
                }
            } else {
                putu(v, 1);
                putv(e, MP_OBJ_FROM_PTR(p), i, MP_OBJ_NEW_QSTR(MP_QSTR___bases__));
            }
        }
        putv(e, MP_OBJ_FROM_PTR(MP_OBJ_TYPE_GET_SLOT(t, locals_dict)), i, MP_OBJ_NEW_QSTR(MP_QSTR___dict__));
        break;
    case O_INSTANCE:
        putv(e, MP_OBJ_FROM_PTR(mp_obj_get_type(o)), i, MP_OBJ_NEW_QSTR(MP_QSTR___class__));
        map = &((mp_obj_instance_t *)MP_OBJ_TO_PTR(o))->members;
        putu(v, map->used);
        end = entryends(map->used);
        if (nlr_push(&nlr) != 0) {
            free(end);
            nlr_jump(nlr.ret_val);
        }
        start = v->len;
        for (j = k = 0; k < map->alloc && j < map->used; k++) {
            if (mp_map_slot_is_filled(map, k)) {
                putv(e, map->table[k].key, i, MP_OBJ_NULL);
                putv(e, map->table[k].value, i, map->table[k].key);
                end[j++] = v->len;
            }
        }
        sortentries(v, start, end, j);
        nlr_pop();
        free(end);
        break;
    case O_FUN:
        putfun(e, i, MP_OBJ_TO_PTR(o));
        break;
    case O_CLOSURE: {
        closure_t *c = MP_OBJ_TO_PTR(o);

        putv(e, c->fun, i, MP_OBJ_NULL);
        putu(v, c->n_closed);
        for (k = 0; k < c->n_closed; k++) {
            putv(e, c->closed[k], i, MP_OBJ_NEW_SMALL_INT(k));
        }
        break;
    }
    case O_STATICMETH:
    case O_CLASSMETH:
        putv(e, ((mp_obj_static_class_method_t *)MP_OBJ_TO_PTR(o))->fun, i, MP_OBJ_NULL);
        break;
    case O_BOUNDMETH: {
        boundmeth_t *b = MP_OBJ_TO_PTR(o);

        putv(e, b->meth, i, MP_OBJ_NULL);
        putv(e, b->self, i, MP_OBJ_NEW_QSTR(MP_QSTR___self__));
        break;
    }
    }
}

// the roots: __main__'s dict and the Space's modules' globals
typedef struct {
    mp_obj_t dict[64];
    qstr name[64];
    int n;
} roots_t;

static void getroots(enc_t *e, roots_t *r) {
    p9_space_mod_t *m;
    int n, k;

    r->n = 0;
    r->name[r->n] = MP_QSTR___main__;
    r->dict[r->n++] = MP_OBJ_FROM_PTR(&MP_STATE_VM(dict_main));
    m = p9_space_mods(&n);
    for (k = 0; k < n && r->n < 64; k++) {
        if (spacemod(e, MP_OBJ_FROM_PTR(m[k].ctx)) != MP_QSTRnull) {
            r->name[r->n] = m[k].name;
            r->dict[r->n++] = MP_OBJ_FROM_PTR(m[k].ctx->module.globals);
        }
    }
}

// work item i's record (kind and fields) in *e->out
static void encode(enc_t *e, roots_t *r, size_t i) {
    mp_obj_t transient = MP_OBJ_NULL;
    mp_map_elem_t *el;
    int k;

    for (k = 0; k < r->n; k++) {
        if (e->obj[i] == r->dict[k]) {
            el = mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(r->dict[k]))->map,
                MP_OBJ_NEW_QSTR(MP_QSTR___transient__), MP_MAP_LOOKUP);
            if (el != NULL) {
                transient = el->value;
            }
        }
    }
    e->out->len = 0;
    putobj(e, i, transient);
}

static void rootrec(enc_t *e, roots_t *r, vstr_t *v) {
    oent_t *x;
    int k;

    putu(v, r->n);
    for (k = 0; k < r->n; k++) {
        putstr(v, qstr_str(r->name[k]), strlen(qstr_str(r->name[k])));
        x = oget(MP_OBJ_TO_PTR(r->dict[k]));
        putu(v, x != NULL ? x->oid : 0);
    }
}

static void record(vstr_t *rec, uint64_t oid, const char *p, size_t n) {
    vstr_add_byte(rec, 'p');
    put64(rec, oid);
    put64(rec, n);
    vstr_add_strn(rec, p, n);
}

int p9_image_commit(uint64_t *next, int (*write)(vstr_t *rec, void *a), void *a) {
    nlr_buf_t nlr;
    enc_t e;
    roots_t r;
    vstr_t rec, body, rv;
    uint64_t rh, nexto;
    size_t i, nd, nrec;
    oent_t *x;
    int k, ret;
    char what[80];
    long long t0 = nsec();

    memset(&e, 0, sizeof e);
    e.mainmod = MP_OBJ_FROM_PTR(&mp_module___main__);
    nexto = *next;
    e.next = next;
    nd = 0;
    rh = 0;
    vstr_init(&rec, 4096);
    vstr_init(&body, 256);
    vstr_init(&rv, 256);
    e.out = &body;
    if (nlr_push(&nlr) == 0) {
        getroots(&e, &r);
        /* the roots always (few, and the barrier misses __main__'s
           static dict), then what changed in place, then what is new */
        for (k = 0; k < r.n; k++) {
            if ((x = oget(MP_OBJ_TO_PTR(r.dict[k]))) == NULL) {
                ref(&e, r.dict[k], -1, MP_OBJ_NEW_QSTR(r.name[k]));
            } else if (!x->mark) {
                x->mark = 1;
                push(&e, r.dict[k], -1, MP_OBJ_NEW_QSTR(r.name[k]));
            }
        }
        if (dirtylost) {
            for (i = 0; i < otsize; i++) {
                if (ot[i].p != NULL && ot[i].dirty && !ot[i].foreign && !ot[i].mark) {
                    ot[i].mark = 1;
                    push(&e, MP_OBJ_FROM_PTR(ot[i].p), -1, MP_OBJ_NULL);
                }
            }
        }
        for (i = 0; i < ndirty; i++) {
            if ((x = oget(dirtyv[i])) != NULL && x->dirty && !x->mark) {
                x->mark = 1;
                push(&e, MP_OBJ_FROM_PTR(dirtyv[i]), -1, MP_OBJ_NULL);
            }
        }
        nrec = 0;
        for (i = 0; i < e.n; i++) {	/* e.n grows: the new ones */
            encode(&e, &r, i);
            e.hash[i] = fnv(body.buf, body.len);
            x = oget(MP_OBJ_TO_PTR(e.obj[i]));
            if (x->isnew || x->hash != e.hash[i]) {
                record(&rec, x->oid, body.buf, body.len);
                nrec++;
            }
        }
        nd = ndead;
        for (i = 0; i < nd; i++) {
            vstr_add_byte(&rec, 'd');
            put64(&rec, deadv[i]);
            nrec++;
        }
        rootrec(&e, &r, &rv);
        rh = fnv(rv.buf, rv.len);
        if (rh != roothash) {
            record(&rec, OID_ROOTS, rv.buf, rv.len);
            nrec++;
        }
        snprint(what, sizeof what, "commit: %d encoded, %d records, %d bytes", (int)e.n, (int)nrec, (int)rec.len);
        p9_spacetime(what, t0);
        ret = nrec == 0 ? 0 : write(&rec, a) < 0 ? -1 : 1;
        p9_spacetime("commit: written", t0);
        nlr_pop();
    } else {
        ret = -2;	/* raised: rolled back, then raised again */
    }
    /* done: the hashes kept, the marks cleared; or all as it was */
    for (i = 0; i < e.n; i++) {
        if ((x = oget(MP_OBJ_TO_PTR(e.obj[i]))) != NULL) {
            x->mark = 0;
        }
    }
    if (ret >= 0) {
        for (i = 0; i < e.n; i++) {
            x = oget(MP_OBJ_TO_PTR(e.obj[i]));
            x->hash = e.hash[i];
            x->isnew = 0;
            x->dirty = 0;
        }
        ndirty = 0;
        dirtylost = 0;
        memmove(deadv, deadv + nd, (ndead - nd) * sizeof deadv[0]);
        ndead -= nd;
        roothash = rh;
    } else {
        for (i = 0; i < e.n; i++) {
            x = oget(MP_OBJ_TO_PTR(e.obj[i]));
            if (x != NULL && x->isnew) {
                odel(MP_OBJ_TO_PTR(e.obj[i]));
            }
        }
        *next = nexto;	/* never used: fine either way, but as it was */
    }
    if (ret == -2) {
        nlr_jump(nlr.ret_val);
    }
    return ret;
}

int p9_image_check(void) {
    enc_t e;
    roots_t r;
    vstr_t body, v;
    oent_t *x;
    size_t i;
    int k;

    memset(&e, 0, sizeof e);
    e.mainmod = MP_OBJ_FROM_PTR(&mp_module___main__);
    e.check = 1;
    vstr_init(&body, 256);
    e.out = &body;
    getroots(&e, &r);
    for (k = 0; k < r.n; k++) {
        ref(&e, r.dict[k], -1, MP_OBJ_NEW_QSTR(r.name[k]));
    }
    for (i = 0; i < e.n; i++) {
        encode(&e, &r, i);
        x = oget(MP_OBJ_TO_PTR(e.obj[i]));
        if (x->hash != fnv(body.buf, body.len)) {
            vstr_init(&v, 64);
            path(&e, e.parent[i], e.label[i], &v);
            mp_printf(&mp_plat_print, "space: check: %s at %s (OID %u): changed, not committed\n",
                qstr_str(mp_obj_get_type(e.obj[i])->name), vstr_null_terminated_str(&v), (unsigned)x->oid);
            e.bad++;
        }
    }
    for (i = 0; i < e.n; i++) {
        if ((x = oget(MP_OBJ_TO_PTR(e.obj[i]))) != NULL) {
            x->mark = 0;
        }
    }
    return e.bad;
}

/* ---- decoding ---- */

typedef struct {
    const byte *p, *end;
    mp_obj_t *shell;	/* index -> object (MP_OBJ_NULL: not yet) */
    byte *kind;
    const byte **body;
    size_t n;
    int byoid;		/* the store's: a reference is an OID */
    void *view;		/* a view's (space.view): its modules, not ours */
    uint64_t *oid;		/* index -> OID */
    size_t *mval, msize;	/* OID -> index+1 */
} dec_t;

static size_t omget(dec_t *d, uint64_t oid);

static MP_NORETURN void corrupt(void) {
    mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("space: a corrupt image"));
}

static uint64_t getu(dec_t *d) {
    uint64_t u = 0;
    int s = 0;

    for (;;) {
        if (d->p >= d->end || s > 63) {
            corrupt();
        }
        u |= (uint64_t)(*d->p & 0x7f) << s;
        if (!(*d->p++ & 0x80)) {
            return u;
        }
        s += 7;
    }
}

static int getb(dec_t *d) {
    if (d->p >= d->end) {
        corrupt();
    }
    return *d->p++;
}

static const char *getstr(dec_t *d, size_t *n) {
    const char *s;

    *n = getu(d);
    if (*n > (size_t)(d->end - d->p)) {
        corrupt();
    }
    s = (const char *)d->p;
    d->p += *n;
    return s;
}

// a value; with skip, only past it.  MP_OBJ_NULL: a shell not made yet
static mp_obj_t getv(dec_t *d, bool skip) {
    int tag = getb(d);
    uint64_t u;
    size_t n, m;
    const char *s, *a;
    double f;
    mp_obj_t mod;
    mp_map_elem_t *el;

    switch (tag) {
    case V_NONE: return mp_const_none;
    case V_FALSE: return mp_const_false;
    case V_TRUE: return mp_const_true;
    case V_ELLIPSIS: return MP_OBJ_FROM_PTR(&mp_const_ellipsis_obj);
    case V_INT:
        u = getu(d);
        return skip ? mp_const_none : mp_obj_new_int(u & 1 ? ~(mp_int_t)(u >> 1) : (mp_int_t)(u >> 1));
    case V_BIGINT:
        s = getstr(d, &n);
        return skip ? mp_const_none : mp_parse_num_integer(s, n, 10, NULL);
    case V_FLOAT:
        if (d->end - d->p < 8) {
            corrupt();
        }
        memcpy(&f, d->p, 8);
        d->p += 8;
        return skip ? mp_const_none : mp_obj_new_float(f);
    case V_QSTR:
        s = getstr(d, &n);
        return skip ? mp_const_none : MP_OBJ_NEW_QSTR(qstr_from_strn(s, n));
    case V_STR:
        s = getstr(d, &n);
        return skip ? mp_const_none : mp_obj_new_str(s, n);
    case V_BYTES:
        s = getstr(d, &n);
        return skip ? mp_const_none : mp_obj_new_bytes((const byte *)s, n);
    case V_MODULE:
        s = getstr(d, &n);
        if (skip) {
            return mp_const_none;
        }
        if (d->view != NULL) {
            return MP_OBJ_FROM_PTR(p9_view_loadmod(d->view, qstr_from_strn(s, n))->ctx);
        }
        el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map, MP_OBJ_NEW_QSTR(qstr_from_strn(s, n)), MP_MAP_LOOKUP);
        if (el == NULL) {
            corrupt();
        }
        return el->value;
    case V_BUILTIN:
        s = getstr(d, &n);
        a = getstr(d, &m);
        if (skip) {
            return mp_const_none;
        }
        mod = mp_module_get_builtin(qstr_from_strn(s, n), false);
        if (mod == MP_OBJ_NULL) {
            mod = mp_module_get_builtin(qstr_from_strn(s, n), true);
        }
        if (mod == MP_OBJ_NULL) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("space: no builtin module %s"), qstr_str(qstr_from_strn(s, n)));
        }
        if (m == 0) {
            return mod;
        }
        if (qstr_from_strn(s, n) == MP_QSTR_builtins && qstr_from_strn(a, m) == MP_QSTR_object) {
            return MP_OBJ_FROM_PTR(&mp_type_object);
        }
        return mp_load_attr(mod, qstr_from_strn(a, m));
    case V_REF:
        u = getu(d);
        if (d->byoid) {
            u = omget(d, u);
        } else if (u >= d->n) {
            corrupt();
        }
        return skip ? mp_const_none : d->shell[u];
    }
    corrupt();
}

static void skipobj(dec_t *d, int kind) {
    size_t n, k;

    switch (kind) {
    case O_TUPLE: case O_LIST: case O_SET: case O_FROZENSET:
        for (n = getu(d), k = 0; k < n; k++) getv(d, true);
        break;
    case O_DICT:
        for (n = getu(d), k = 0; k < 2 * n; k++) getv(d, true);
        break;
    case O_BYTEARRAY:
        getstr(d, &n);
        break;
    case O_CELL: case O_STATICMETH: case O_CLASSMETH:
        getv(d, true);
        break;
    case O_CLASS:
        getstr(d, &n);
        for (n = getu(d), k = 0; k < n; k++) getv(d, true);
        getv(d, true);
        break;
    case O_INSTANCE:
        getv(d, true);
        for (n = getu(d), k = 0; k < 2 * n; k++) getv(d, true);
        break;
    case O_FUN:
        getb(d);
        getstr(d, &n);
        for (n = getu(d), k = 0; k < n; k++) getu(d);
        for (n = getu(d), k = 0; k < n; k++) getv(d, true);
        getv(d, true);
        break;
    case O_CLOSURE:
        getv(d, true);
        for (n = getu(d), k = 0; k < n; k++) getv(d, true);
        break;
    case O_BOUNDMETH:
        getv(d, true);
        getv(d, true);
        break;
    default:
        corrupt();
    }
}

static mp_obj_t need(mp_obj_t o) {
    if (o == MP_OBJ_NULL) {
        corrupt();	/* a reference to what cannot be made first */
    }
    return o;
}

// object i's shell, if what it needs is there; false: not yet
static bool mkshell(dec_t *d, size_t i) {
    size_t n, k;
    const char *s;
    mp_obj_t args[3], x, *items;

    d->p = d->body[i];
    switch (d->kind[i]) {
    case O_TUPLE:
        n = getu(d);
        x = mp_obj_new_tuple(n, NULL);
        mp_obj_tuple_get(x, &n, &items);
        for (k = 0; k < n; k++) {
            items[k] = mp_const_none;
        }
        d->shell[i] = x;
        return true;
    case O_LIST:
        d->shell[i] = mp_obj_new_list(0, NULL);
        return true;
    case O_DICT:
        n = getu(d);
        d->shell[i] = mp_obj_new_dict(n);
        return true;
    case O_SET:
    case O_FROZENSET:
        d->shell[i] = mp_obj_new_set(0, NULL);
        return true;
    case O_BYTEARRAY:
        s = getstr(d, &n);
        d->shell[i] = mp_obj_new_bytearray(n, s);
        return true;
    case O_CELL:
        d->shell[i] = mp_obj_new_cell(mp_const_none);
        return true;
    case O_CLASS: {
        mp_obj_type_t *t;

        s = getstr(d, &n);
        args[0] = MP_OBJ_NEW_QSTR(qstr_from_strn(s, n));
        n = getu(d);
        args[1] = mp_obj_new_tuple(n, NULL);
        mp_obj_tuple_get(args[1], &n, &items);
        for (k = 0; k < n; k++) {
            if ((items[k] = getv(d, false)) == MP_OBJ_NULL) {
                return false;	/* a base not made yet */
            }
        }
        args[2] = need(getv(d, false));
        /* the dict empty now, filled later: type() runs nothing of it */
        t = MP_OBJ_TO_PTR(mp_call_function_n_kw(MP_OBJ_FROM_PTR(&mp_type_type), 3, 0, args));
        /* special accessors (__getattr__, properties): looked for */
        t->flags |= MP_TYPE_FLAG_HAS_SPECIAL_ACCESSORS;
        d->shell[i] = MP_OBJ_FROM_PTR(t);
        return true;
    }
    case O_INSTANCE: {
        const mp_obj_type_t *nb = NULL;

        if ((x = getv(d, false)) == MP_OBJ_NULL) {
            return false;
        }
        d->shell[i] = MP_OBJ_FROM_PTR(mp_obj_new_instance(MP_OBJ_TO_PTR(x), &nb));
        return true;
    }
    case O_FUN: {
        p9_space_mod_t *m;
        const mp_raw_code_t *rc;
        mp_obj_t def[2], pos;

        getb(d);
        s = getstr(d, &n);
        m = d->view != NULL ? p9_view_loadmod(d->view, qstr_from_strn(s, n)) : p9_space_loadmod(qstr_from_strn(s, n));
        rc = m->rc;
        for (n = getu(d), k = 0; k < n; k++) {
            size_t c = getu(d);

            if (c >= rc->n_children) {
                corrupt();
            }
            rc = rc->children[c];
        }
        n = getu(d);
        pos = MP_OBJ_NULL;
        if (n > 0) {
            pos = mp_obj_new_tuple(n, NULL);
            mp_obj_tuple_get(pos, &n, &items);
            for (k = 0; k < n; k++) {
                getv(d, true);
                items[k] = mp_const_none;	/* filled later */
            }
        }
        def[0] = pos;
        x = getv(d, false);
        def[1] = x == mp_const_none ? MP_OBJ_NULL : need(x);
        d->shell[i] = mp_make_function_from_proto_fun((mp_proto_fun_t)rc, m->ctx, n > 0 || def[1] != MP_OBJ_NULL ? def : NULL);
        return true;
    }
    case O_CLOSURE: {
        mp_obj_t fun, *closed;

        if ((fun = getv(d, false)) == MP_OBJ_NULL) {
            return false;
        }
        n = getu(d);
        closed = m_new(mp_obj_t, n + 1);
        for (k = 0; k < n; k++) {
            if ((closed[k] = getv(d, false)) == MP_OBJ_NULL) {
                return false;
            }
        }
        d->shell[i] = mp_obj_new_closure(fun, n, closed);
        return true;
    }
    case O_STATICMETH:
    case O_CLASSMETH: {
        mp_obj_static_class_method_t *o;

        if ((x = getv(d, false)) == MP_OBJ_NULL) {
            return false;
        }
        o = m_new_obj(mp_obj_static_class_method_t);
        o->base.type = d->kind[i] == O_STATICMETH ? &mp_type_staticmethod : &mp_type_classmethod;
        o->fun = x;
        d->shell[i] = MP_OBJ_FROM_PTR(o);
        return true;
    }
    case O_BOUNDMETH: {
        mp_obj_t meth, self;

        if ((meth = getv(d, false)) == MP_OBJ_NULL || (self = getv(d, false)) == MP_OBJ_NULL) {
            return false;
        }
        d->shell[i] = mp_obj_new_bound_meth(meth, self);
        return true;
    }
    }
    corrupt();
}

// does a dict's or set's body have only keys that hash without user code
static bool plainkeys(dec_t *d, size_t i) {
    size_t n, k;
    int tag;
    const byte *q;

    d->p = d->body[i];
    n = getu(d);
    for (k = 0; k < n; k++) {
        q = d->p;
        tag = getb(d);
        d->p = q;
        if (tag == V_REF) {
            return false;
        }
        getv(d, true);
        if (d->kind[i] == O_DICT) {
            getv(d, true);
        }
    }
    return true;
}

static void fill(dec_t *d, size_t i) {
    size_t n, k;
    mp_obj_t o = d->shell[i], key, *items;
    mp_map_t *map;

    d->p = d->body[i];
    switch (d->kind[i]) {
    case O_TUPLE:
        n = getu(d);
        mp_obj_tuple_get(o, &n, &items);
        for (k = 0; k < n; k++) {
            items[k] = need(getv(d, false));
        }
        break;
    case O_LIST:
        for (n = getu(d), k = 0; k < n; k++) {
            mp_obj_list_append(o, need(getv(d, false)));
        }
        break;
    case O_DICT:
        for (n = getu(d), k = 0; k < n; k++) {
            key = need(getv(d, false));
            mp_obj_dict_store(o, key, need(getv(d, false)));
        }
        break;
    case O_SET:
    case O_FROZENSET:
        for (n = getu(d), k = 0; k < n; k++) {
            mp_obj_set_store(o, need(getv(d, false)));
        }
        #if MICROPY_PY_BUILTINS_FROZENSET
        if (d->kind[i] == O_FROZENSET) {
            ((mp_obj_base_t *)MP_OBJ_TO_PTR(o))->type = &mp_type_frozenset;
        }
        #else
        if (d->kind[i] == O_FROZENSET) {
            corrupt();
        }
        #endif
        break;
    case O_CELL:
        mp_obj_cell_set(o, need(getv(d, false)));
        break;
    case O_INSTANCE:
        getv(d, true);
        map = &((mp_obj_instance_t *)MP_OBJ_TO_PTR(o))->members;
        for (n = getu(d), k = 0; k < n; k++) {
            key = need(getv(d, false));
            mp_map_lookup(map, key, MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = need(getv(d, false));
        }
        break;
    case O_FUN: {
        mp_obj_fun_bc_t *f = MP_OBJ_TO_PTR(o);

        getb(d);
        getstr(d, &n);
        for (n = getu(d), k = 0; k < n; k++) {
            getu(d);
        }
        for (n = getu(d), k = 0; k < n; k++) {
            f->extra_args[k] = need(getv(d, false));
        }
        break;
    }
    }
}

// the shells, then their contents (both formats)
static void build(dec_t *d, long long t0) {
    size_t i, left;
    bool any;
    int round;

    /* the shells: values first, then what needs others, until done */
    for (i = 0; i < d->n; i++) {
        if (d->shell[i] == MP_OBJ_NULL && d->kind[i] < O_CLASS) {
            mkshell(d, i);
        }
    }
    do {
        any = false;
        left = 0;
        for (i = 0; i < d->n; i++) {
            if (d->shell[i] == MP_OBJ_NULL) {
                if (mkshell(d, i)) {
                    any = true;
                } else {
                    left++;
                }
            }
        }
    } while (left > 0 && any);
    if (left > 0) {
        corrupt();
    }
    p9_spacetime("restore: shells", t0);
    /* filled: all but dicts and sets, then those with plain keys, then
       the rest (a key's __hash__ may use the others) */
    for (i = 0; i < d->n; i++) {
        if (d->kind[i] != O_DICT && d->kind[i] != O_SET && d->kind[i] != O_FROZENSET) {
            fill(d, i);
        }
    }
    p9_spacetime("restore: filled", t0);
    for (round = 0; round < 2; round++) {
        for (i = 0; i < d->n; i++) {
            if ((d->kind[i] == O_DICT || d->kind[i] == O_SET || d->kind[i] == O_FROZENSET)
                && plainkeys(d, i) == (round == 0)) {
                fill(d, i);
            }
        }
    }
    p9_spacetime("restore: dicts and sets", t0);
}

// a root's dict: the module's own (__main__'s, or the Environment's NAME)
static mp_obj_t rootdict(dec_t *d, const char *s, size_t n) {
    qstr q = qstr_from_strn(s, n);

    if (d->view != NULL) {
        return q == MP_QSTR___main__ ? p9_view_main(d->view)
            : MP_OBJ_FROM_PTR(p9_view_loadmod(d->view, q)->ctx->module.globals);
    }
    if (q == MP_QSTR___main__) {
        return MP_OBJ_FROM_PTR(&MP_STATE_VM(dict_main));
    }
    return MP_OBJ_FROM_PTR(p9_space_loadmod(q)->ctx->module.globals);
}

void p9_image_restore(const byte *p, size_t len) {
    dec_t d;
    size_t i, k, n, nroot;
    const char *s;

    long long t0 = nsec();

    memset(&d, 0, sizeof d);
    d.p = p;
    d.end = p + len;
    d.n = getu(&d);
    if (d.n > len) {
        corrupt();
    }
    d.shell = m_new0(mp_obj_t, d.n + 1);
    d.kind = m_new(byte, d.n + 1);
    d.body = m_new(const byte *, d.n + 1);
    for (i = 0; i < d.n; i++) {
        d.kind[i] = getb(&d);
        if (d.kind[i] >= O_NKIND) {
            corrupt();
        }
        d.body[i] = d.p;
        skipobj(&d, d.kind[i]);
    }
    /* the roots: their dicts are the modules' own, made first */
    nroot = getu(&d);
    for (k = 0; k < nroot; k++) {
        s = getstr(&d, &n);
        i = getu(&d);
        if (i >= d.n || d.kind[i] != O_DICT) {
            corrupt();
        }
        d.shell[i] = rootdict(&d, s, n);
    }
    if (d.p != d.end) {
        corrupt();
    }
    p9_spacetime("restore: parsed", t0);
    build(&d, t0);
}

static uint32_t get32(const byte *p) {
    return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static uint64_t get64(const byte *p) {
    return (uint64_t)get32(p) << 32 | get32(p + 4);
}

static size_t omhash(dec_t *d, uint64_t oid) {
    return (size_t)((oid * 0x9E3779B97F4A7C15ULL) >> 20) & (d->msize - 1);
}

static void omadd(dec_t *d, uint64_t oid, size_t i) {
    size_t h;

    for (h = omhash(d, oid); d->mval[h] != 0; h = (h + 1) & (d->msize - 1)) {
        if (d->oid[d->mval[h] - 1] == oid) {
            corrupt();	/* twice */
        }
    }
    d->mval[h] = i + 1;
}

static size_t omget(dec_t *d, uint64_t oid) {
    size_t h;

    for (h = omhash(d, oid); d->mval[h] != 0; h = (h + 1) & (d->msize - 1)) {
        if (d->oid[d->mval[h] - 1] == oid) {
            return d->mval[h] - 1;
        }
    }
    corrupt();	/* a reference to what the store has not */
}

void p9_image_restore_store(const byte *p, size_t len, uint64_t *version, char *user, size_t *nuser, void *view) {
    dec_t d;
    const byte *q, *e = p + len, *rr = NULL, **rec;
    size_t i, k, n, nroot, nr = 0, *recn;
    uint64_t oid, olen;
    const char *s;

    long long t0 = nsec();

    if (len < 21 || p[0] != 'H') {
        corrupt();
    }
    *version = get64(p + 1);
    n = get32(p + 17);
    if (n > *nuser || n > len - 21) {
        corrupt();
    }
    memcpy(user, p + 21, n);
    *nuser = n;
    q = p + 21 + n;
    /* the objects' records: counted, then taken */
    memset(&d, 0, sizeof d);
    for (n = 0; q < e; n++) {
        if (e - q < 25 || *q != 'o' || get64(q + 17) > (uint64_t)(e - q - 25)) {
            corrupt();
        }
        q += 25 + get64(q + 17);
    }
    d.shell = m_new0(mp_obj_t, n + 1);
    d.kind = m_new(byte, n + 1);
    d.body = m_new(const byte *, n + 1);
    d.oid = m_new(uint64_t, n + 1);
    rec = m_new(const byte *, n + 1);
    recn = m_new(size_t, n + 1);
    for (d.msize = 16; d.msize < 2 * n + 2; d.msize *= 2) {
    }
    d.mval = m_new0(size_t, d.msize);
    d.byoid = 1;
    d.view = view;
    d.end = e;
    for (q = p + 21 + *nuser; q < e; q += 25 + olen) {
        oid = get64(q + 1);
        olen = get64(q + 17);
        if (oid == OID_ROOTS) {
            rr = q + 25;
            nr = olen;
            continue;
        }
        if (olen < 1 || q[25] >= O_NKIND) {
            corrupt();
        }
        i = d.n++;
        d.oid[i] = oid;
        d.kind[i] = q[25];
        d.body[i] = q + 26;
        rec[i] = q + 25;
        recn[i] = olen;
        omadd(&d, oid, i);
    }
    if (rr == NULL) {
        corrupt();
    }
    /* the roots: their dicts are the modules' own, made first */
    d.p = rr;
    d.end = rr + nr;
    nroot = getu(&d);
    for (k = 0; k < nroot; k++) {
        s = getstr(&d, &n);
        i = omget(&d, getu(&d));
        if (d.kind[i] != O_DICT) {
            corrupt();
        }
        d.shell[i] = rootdict(&d, s, n);
    }
    d.end = e;
    p9_spacetime("restore: parsed", t0);
    build(&d, t0);
    /* a view's objects: foreign, so that no commit takes them */
    if (view != NULL) {
        for (i = 0; i < d.n; i++) {
            if (oget(MP_OBJ_TO_PTR(d.shell[i])) == NULL) {
                oadd(MP_OBJ_TO_PTR(d.shell[i]), 0, 0);
                oget(MP_OBJ_TO_PTR(d.shell[i]))->foreign = 1;
            }
        }
        p9_spacetime("view: marked", t0);
        return;
    }
    /* the OIDs: the objects as they are in the store */
    p9_image_reset();
    for (i = 0; i < d.n; i++) {
        oadd(MP_OBJ_TO_PTR(d.shell[i]), d.oid[i], fnv((const char *)rec[i], recn[i]));
    }
    roothash = fnv((const char *)rr, nr);
    p9_spacetime("restore: OIDs", t0);
}
