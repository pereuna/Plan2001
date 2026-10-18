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

/* ---- encoding ---- */

typedef struct {
    vstr_t *out;
    mp_obj_t *obj;		/* index -> object */
    int *parent;		/* for an error's path */
    mp_obj_t *label;
    size_t n, a;
    mp_obj_t *tkey;		/* object -> index, open addressing */
    size_t *tval, tsize;
    mp_obj_t mainmod;
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

static size_t slot(enc_t *e, mp_obj_t o) {
    size_t h = (((uintptr_t)o >> 3) * 0x9E3779B97F4A7C15ULL) & (e->tsize - 1);

    while (e->tkey[h] != MP_OBJ_NULL && e->tkey[h] != o) {
        h = (h + 1) & (e->tsize - 1);
    }
    return h;
}

static void grow(enc_t *e) {
    mp_obj_t *ok = e->tkey;
    size_t *ov = e->tval, os = e->tsize, i, h;

    e->tsize = os ? 2 * os : 1024;
    e->tkey = m_new0(mp_obj_t, e->tsize);
    e->tval = m_new(size_t, e->tsize);
    for (i = 0; i < os; i++) {
        if (ok[i] != MP_OBJ_NULL) {
            h = slot(e, ok[i]);
            e->tkey[h] = ok[i];
            e->tval[h] = ov[i];
        }
    }
}

// the path to object i (and then label), for an error
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

static size_t ref(enc_t *e, mp_obj_t o, int parent, mp_obj_t label) {
    size_t h;

    if (2 * (e->n + 1) > e->tsize) {
        grow(e);
    }
    h = slot(e, o);
    if (e->tkey[h] == o) {
        return e->tval[h];
    }
    okind(e, o, parent, label);	/* not persistent: an error now, with its path */
    if (e->n == e->a) {
        e->a = e->a ? 2 * e->a : 256;
        e->obj = m_renew(mp_obj_t, e->obj, e->n, e->a);
        e->parent = m_renew(int, e->parent, e->n, e->a);
        e->label = m_renew(mp_obj_t, e->label, e->n, e->a);
    }
    e->obj[e->n] = o;
    e->parent[e->n] = parent;
    e->label[e->n] = label;
    e->tkey[h] = o;
    e->tval[h] = e->n;
    return e->n++;
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

static void putobj(enc_t *e, size_t i, mp_obj_t transient) {
    vstr_t *v = e->out;
    mp_obj_t o = e->obj[i], *items, it, x;
    size_t n, k;
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
        for (k = 0; k < map->alloc; k++) {
            if (mp_map_slot_is_filled(map, k)) {
                putv(e, map->table[k].key, i, MP_OBJ_NULL);
                /* a module's __transient__ names: None */
                if (transient != MP_OBJ_NULL && mp_obj_is_str(map->table[k].key)
                    && mp_binary_op(MP_BINARY_OP_CONTAINS, transient, map->table[k].key) == mp_const_true) {
                    vstr_add_byte(v, V_NONE);
                } else {
                    putv(e, map->table[k].value, i, map->table[k].key);
                }
            }
        }
        break;
    case O_SET:
    case O_FROZENSET:
        putu(v, MP_OBJ_SMALL_INT_VALUE(mp_obj_len(o)));
        it = mp_getiter(o, NULL);
        k = 0;
        while ((x = mp_iternext(it)) != MP_OBJ_STOP_ITERATION) {
            putv(e, x, i, MP_OBJ_NEW_SMALL_INT(k++));
        }
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
        for (k = 0; k < map->alloc; k++) {
            if (mp_map_slot_is_filled(map, k)) {
                putv(e, map->table[k].key, i, MP_OBJ_NULL);
                putv(e, map->table[k].value, i, map->table[k].key);
            }
        }
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

void p9_image_encode(vstr_t *out) {
    enc_t e;
    vstr_t objs, roots;
    p9_space_mod_t *m;
    int n, k;
    size_t i, nroot = 0;
    mp_obj_t g, *rootdict, transient;
    qstr *rootname;
    mp_map_elem_t *el;

    memset(&e, 0, sizeof e);
    e.mainmod = MP_OBJ_FROM_PTR(&mp_module___main__);
    m = p9_space_mods(&n);
    rootdict = m_new(mp_obj_t, n + 1);
    rootname = m_new(qstr, n + 1);
    rootname[nroot] = MP_QSTR___main__;
    rootdict[nroot++] = MP_OBJ_FROM_PTR(&MP_STATE_VM(dict_main));
    for (k = 0; k < n; k++) {
        if (spacemod(&e, MP_OBJ_FROM_PTR(m[k].ctx)) != MP_QSTRnull) {
            rootname[nroot] = m[k].name;
            rootdict[nroot++] = MP_OBJ_FROM_PTR(m[k].ctx->module.globals);
        }
    }
    /* the roots' dicts are objects 0..nroot-1 */
    for (i = 0; i < nroot; i++) {
        ref(&e, rootdict[i], -1, MP_OBJ_NEW_QSTR(rootname[i]));
    }
    vstr_init(&objs, 4096);
    e.out = &objs;
    for (i = 0; i < e.n; i++) {
        transient = MP_OBJ_NULL;
        if (i < nroot) {
            g = rootdict[i];
            el = mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(g))->map, MP_OBJ_NEW_QSTR(MP_QSTR___transient__), MP_MAP_LOOKUP);
            if (el != NULL) {
                transient = el->value;
            }
        }
        putobj(&e, i, transient);
    }
    vstr_init(&roots, 256);
    putu(&roots, nroot);
    for (i = 0; i < nroot; i++) {
        putstr(&roots, qstr_str(rootname[i]), strlen(qstr_str(rootname[i])));
        putu(&roots, i);
    }
    putu(out, e.n);
    vstr_add_strn(out, objs.buf, objs.len);
    vstr_add_strn(out, roots.buf, roots.len);
}

/* ---- decoding ---- */

typedef struct {
    const byte *p, *end;
    mp_obj_t *shell;	/* index -> object (MP_OBJ_NULL: not yet) */
    byte *kind;
    const byte **body;
    size_t n;
} dec_t;

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
        if (u >= d->n) {
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
        m = p9_space_loadmod(qstr_from_strn(s, n));
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

void p9_image_restore(const byte *p, size_t len) {
    dec_t d;
    size_t i, k, n, nroot, left;
    const char *s;
    mp_obj_t g;
    mp_module_context_t *ctx;
    bool any;
    int round;

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
        if (qstr_from_strn(s, n) == MP_QSTR___main__) {
            g = MP_OBJ_FROM_PTR(&MP_STATE_VM(dict_main));
        } else {
            ctx = p9_space_loadmod(qstr_from_strn(s, n))->ctx;
            g = MP_OBJ_FROM_PTR(ctx->module.globals);
        }
        d.shell[i] = g;
    }
    if (d.p != d.end) {
        corrupt();
    }
    /* the shells: values first, then what needs others, until done */
    for (i = 0; i < d.n; i++) {
        if (d.shell[i] == MP_OBJ_NULL && d.kind[i] < O_CLASS) {
            mkshell(&d, i);
        }
    }
    do {
        any = false;
        left = 0;
        for (i = 0; i < d.n; i++) {
            if (d.shell[i] == MP_OBJ_NULL) {
                if (mkshell(&d, i)) {
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
    /* filled: all but dicts and sets, then those with plain keys, then
       the rest (a key's __hash__ may use the others) */
    for (i = 0; i < d.n; i++) {
        if (d.kind[i] != O_DICT && d.kind[i] != O_SET && d.kind[i] != O_FROZENSET) {
            fill(&d, i);
        }
    }
    for (round = 0; round < 2; round++) {
        for (i = 0; i < d.n; i++) {
            if ((d.kind[i] == O_DICT || d.kind[i] == O_SET || d.kind[i] == O_FROZENSET)
                && plainkeys(&d, i) == (round == 0)) {
                fill(&d, i);
            }
        }
    }
}
