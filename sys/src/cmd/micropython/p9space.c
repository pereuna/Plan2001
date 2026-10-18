/*
 * The persistent object environment (docs/object-model.md): the space
 * module.  V1: a module's definition is its source's SHA-256, the
 * source and its .mpy (compiled once, never again); an Environment is
 * a runtime and import names bound to definitions, itself named by its
 * text's SHA-256.  mp -E ENV imports from the Environment alone: the
 * builtin modules, then the Environment's, no sys.path, no packages.
 *   /usr/$user/lib/mod/<sha256>.py    the source
 *   /usr/$user/lib/mod/<sha256>.mpy   its bytecode
 *   /usr/$user/lib/env/<sha256>       runtime <id>\n<name> <sha256>\n...
 * V2: a Space, mp -S NAME: its modules' instances (__main__ too) and
 * what they reach, a whole image (p9image.c) committed at the end of a
 * REPL statement and of the program; opened without running the
 * modules' top levels again.  One writer: the lock, DMEXCL.
 *   /usr/$user/space/<name>/lock      held open by the writer
 *   /usr/$user/space/<name>/v<N>      version N, N in 16 digits
 * V3: a commit at a rest too (plan9.read, wait, sleep) when
 * space.interval seconds have passed; space.atomic defers them.
 * V5: the Space's objects in a gefs object store (docs/gefs-obj.md),
 * each its own key by OID: a commit writes what changed, the store's
 * versions are the Space's (V2-V4's v<N> taken in at the first open).
 *   /usr/$user/space/<name>/obj       DMOBJ: the objects, OID 1 the roots;
 *                                     the header's user part env[64] next[8]
 */
#include <string.h>
#include "py/runtime.h"
#include "py/compile.h"
#include "py/builtin.h"
#include "py/persistentcode.h"
#include "py/objmodule.h"
#include "py/objstr.h"
#include "py/objtuple.h"
#include "py/objexcept.h"
#include "p9space.h"

// the runtime an Environment is made for: the .mpy's format
#define RUNTIME "mp-" MICROPY_VERSION_STRING "-mpy" MP_STRINGIFY(MPY_VERSION) "." MP_STRINGIFY(MPY_SUB_VERSION)
MP_DEFINE_STR_OBJ(space_runtime_obj, RUNTIME);

// the Environment of mp -E: (its name, {import name: definition})
MP_REGISTER_ROOT_POINTER(mp_obj_t p9_space_env);

// the modules loaded from the Environment, for the image: their
// context (globals, constants) and their bytecode's tree
MP_REGISTER_ROOT_POINTER(void *p9_space_mods);
static int nmods;

MP_DEFINE_EXCEPTION(CommitError, Exception)

// the Space of mp -S
static struct {
    int isopen;
    char name[64];
    char dir[256];
    int lock;
    int writer;
    vlong n;		/* the store's version */
    int store;		/* obj, ORDWR */
    uint64_t next;	/* the next OID */
    int migrate;	/* v<N> taken in: removed after the first commit */
} sp = { 0, "", "", -1, 0, 0, -1 };
static vlong lastcommit;	/* nsec() */

// $spacetime: the times of a restore and a commit on fd 2 (ms)
void p9_spacetime(const char *what, vlong t0) {
    static int on = -1;
    char *e;

    if (on < 0) {
        e = getenv("spacetime");
        on = e != NULL;
        free(e);
    }
    if (on) {
        fprint(2, "spacetime: %s %lld ms\n", what, (nsec() - t0) / 1000000);
    }
}

// a version's file: the header, the body (p9image.c), the trailer
//   "P2SP" format[1] N[8] env[64] body length[8] sha256[32]
// the length and SHA-256 of all before them
#define FORMAT 1
#define HDRLEN (4 + 1 + 8 + 64)
#define TRLLEN (8 + 32)

static void addmod(qstr name, mp_module_context_t *ctx, mp_raw_code_t *rc) {
    p9_space_mod_t *m = MP_STATE_VM(p9_space_mods);

    m = m_renew(p9_space_mod_t, m, nmods, nmods + 1);
    MP_STATE_VM(p9_space_mods) = m;
    m[nmods].name = name;
    m[nmods].ctx = ctx;
    m[nmods].rc = rc;
    nmods++;
}

p9_space_mod_t *p9_space_mods(int *n) {
    *n = nmods;
    return MP_STATE_VM(p9_space_mods);
}

static mp_obj_t envitem(int i) {
    return ((mp_obj_tuple_t *)MP_OBJ_TO_PTR(MP_STATE_VM(p9_space_env)))->items[i];
}

void p9sha256(uchar *, ulong, uchar *);

static void raise_err(void) {
    char buf[ERRMAX];

    mp_handle_pending(true);
    rerrstr(buf, sizeof buf);
    mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("%s"), buf);
}

// /usr/$user/lib/SUB/NAME (SUB, NAME may be "")
static void libpath(char *buf, int n, const char *sub, const char *name) {
    snprint(buf, n, "/usr/%s/lib%s%s%s%s", getuser(), *sub ? "/" : "", sub, *name ? "/" : "", name);
}

static void hexof(const void *p, size_t n, char *hex) {
    uchar d[32];
    int i;

    p9sha256((uchar *)p, n, d);
    for (i = 0; i < 32; i++) {
        snprint(hex + 2 * i, 3, "%02x", d[i]);
    }
}

// the whole file into v (initialised here); -1: no such file
static int readall(const char *path, vstr_t *v) {
    int fd;
    long n;

    if ((fd = open((char *)path, OREAD)) < 0) {
        return -1;
    }
    vstr_init(v, 8192);
    for (;;) {
        vstr_hint_size(v, 8192);
        n = read(fd, v->buf + v->len, v->alloc - v->len);
        if (n <= 0) {
            break;
        }
        v->len += n;
    }
    close(fd);
    if (n < 0) {
        raise_err();
    }
    return 0;
}

static void mkdirs(const char *sub) {
    char path[256];
    int fd;

    libpath(path, sizeof path, "", "");
    if (access(path, AEXIST) < 0 && (fd = create(path, OREAD, DMDIR | 0775)) >= 0) {
        close(fd);
    }
    libpath(path, sizeof path, sub, "");
    if (access(path, AEXIST) < 0) {
        if ((fd = create(path, OREAD, DMDIR | 0775)) < 0) {
            raise_err();
        }
        close(fd);
    }
}

// SUB/NAME written whole or not at all: NAME.tmp, renamed, synced
static void writefile(const char *sub, const char *name, const char *p, size_t n) {
    char path[256], tmp[256], tname[128];
    Dir d;
    int fd;

    libpath(path, sizeof path, sub, name);
    // named by its contents: one there is the same
    if (access(path, AEXIST) == 0) {
        return;
    }
    snprint(tname, sizeof tname, "%s.tmp", name);
    libpath(tmp, sizeof tmp, sub, tname);
    remove(tmp);
    if ((fd = create(tmp, OWRITE, 0664)) < 0) {
        raise_err();
    }
    if (write(fd, p, n) != (long)n) {
        close(fd);
        remove(tmp);
        raise_err();
    }
    close(fd);
    nulldir(&d);
    d.name = (char *)name;
    if (dirwstat(tmp, &d) < 0) {
        remove(tmp);
        raise_err();
    }
    nulldir(&d);
    if (dirwstat(path, &d) < 0) {
        raise_err();
    }
}

static bool ishex(const char *s) {
    int i;

    for (i = 0; i < 64; i++) {
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) {
            return false;
        }
    }
    return s[64] == 0;
}

// hash(data) -> the SHA-256 of a str or bytes, hex
static mp_obj_t space_hash(mp_obj_t data) {
    mp_buffer_info_t b;
    char hex[65];

    mp_get_buffer_raise(data, &b, MP_BUFFER_READ);
    hexof(b.buf, b.len, hex);
    return mp_obj_new_str(hex, 64);
}
static MP_DEFINE_CONST_FUN_OBJ_1(space_hash_obj, space_hash);

// define(path) -> the definition of the module whose source is in path:
// the source's SHA-256; its .mpy compiled the first time only
static mp_obj_t space_define(mp_obj_t path_in) {
    const char *path = mp_obj_str_get_str(path_in), *base;
    char hex[65], name[80], mpy[256];
    vstr_t src, out;
    mp_print_t pr;
    mp_lexer_t *lex;
    mp_parse_tree_t pt;
    mp_compiled_module_t cm;

    if (readall(path, &src) < 0) {
        raise_err();
    }
    hexof(src.buf, src.len, hex);
    snprint(name, sizeof name, "%s.mpy", hex);
    libpath(mpy, sizeof mpy, "mod", name);
    if (access(mpy, AEXIST) == 0) {
        return mp_obj_new_str(hex, 64);
    }
    // the source's own name in tracebacks
    base = strrchr(path, '/');
    base = base != NULL ? base + 1 : path;
    lex = mp_lexer_new_from_str_len(qstr_from_str(base), src.buf, src.len, 0);
    pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
    memset(&cm, 0, sizeof cm);	/* arch_flags: the compiler leaves it */
    cm.context = m_new_obj(mp_module_context_t);
    cm.context->module.globals = mp_globals_get();
    mp_compile_to_raw_code(&pt, qstr_from_str(base), false, &cm);
    vstr_init_print(&out, 1024, &pr);
    mp_raw_code_save(&cm, &pr);
    mkdirs("mod");
    snprint(name, sizeof name, "%s.py", hex);
    writefile("mod", name, src.buf, src.len);
    snprint(name, sizeof name, "%s.mpy", hex);
    writefile("mod", name, out.buf, out.len);
    return mp_obj_new_str(hex, 64);
}
static MP_DEFINE_CONST_FUN_OBJ_1(space_define_obj, space_define);

// mkenv({name: definition}) -> the Environment: this runtime and the
// names, sorted; each a plain module name, each definition defined
static mp_obj_t space_mkenv(mp_obj_t names) {
    mp_obj_t keys, sorted, *k;
    size_t n, i;
    vstr_t t;
    char hex[65], path[256], mpy[80];
    const char *s, *h;

    keys = mp_call_function_1(MP_OBJ_FROM_PTR(&mp_type_list),
        mp_call_function_0(mp_load_attr(names, MP_QSTR_keys)));
    sorted = mp_call_function_1(MP_OBJ_FROM_PTR(&mp_builtin_sorted_obj), keys);
    mp_obj_list_get(sorted, &n, &k);
    vstr_init(&t, 256);
    vstr_printf(&t, "runtime %s\n", RUNTIME);
    for (i = 0; i < n; i++) {
        s = mp_obj_str_get_str(k[i]);
        h = mp_obj_str_get_str(mp_obj_dict_get(names, k[i]));
        if (*s == 0 || strchr(s, '.') != NULL || strchr(s, ' ') != NULL || strchr(s, '\n') != NULL) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("not a module name: '%s'"), s);
        }
        snprint(mpy, sizeof mpy, "%s.mpy", h);
        libpath(path, sizeof path, "mod", mpy);
        if (!ishex(h) || access(path, AEXIST) < 0) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("not defined: %s"), h);
        }
        vstr_printf(&t, "%s %s\n", s, h);
    }
    hexof(t.buf, t.len, hex);
    libpath(path, sizeof path, "env", hex);
    if (access(path, AEXIST) < 0) {
        mkdirs("env");
        writefile("env", hex, t.buf, t.len);
    }
    return mp_obj_new_str(hex, 64);
}
static MP_DEFINE_CONST_FUN_OBJ_1(space_mkenv_obj, space_mkenv);

// useenv(env): import from this Environment from now on (mp -E); its
// runtime must be this mp's, its text its name's
static mp_obj_t space_useenv(mp_obj_t env_in) {
    const char *env = mp_obj_str_get_str(env_in);
    char path[256], hex[65], *p, *e, *sp;
    mp_obj_t d, items[2];
    vstr_t t;

    if (MP_STATE_VM(p9_space_env) != MP_OBJ_NULL) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("an Environment is in use"));
    }
    if (!ishex(env)) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("not an Environment: '%s'"), env);
    }
    libpath(path, sizeof path, "env", env);
    if (readall(path, &t) < 0) {
        raise_err();
    }
    hexof(t.buf, t.len, hex);
    if (strcmp(hex, env) != 0) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("env %s: corrupt (its text is %s)"), env, hex);
    }
    vstr_add_char(&t, 0);
    p = t.buf;
    if (strncmp(p, "runtime ", 8) != 0 || (e = strchr(p, '\n')) == NULL) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("env %s: no runtime"), env);
    }
    *e = 0;
    if (strcmp(p + 8, RUNTIME) != 0) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("env %s: runtime %s, this mp is %s"), env, p + 8, RUNTIME);
    }
    d = mp_obj_new_dict(0);
    for (p = e + 1; *p != 0; p = e + 1) {
        if ((e = strchr(p, '\n')) == NULL || (sp = strchr(p, ' ')) == NULL || sp > e) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("env %s: bad line"), env);
        }
        mp_obj_dict_store(d, mp_obj_new_str(p, sp - p), mp_obj_new_str(sp + 1, e - sp - 1));
    }
    items[0] = env_in;
    items[1] = d;
    MP_STATE_VM(p9_space_env) = mp_obj_new_tuple(2, items);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(space_useenv_obj, space_useenv);

// env() -> the Environment in use (its name), None without one
static mp_obj_t space_env(void) {
    return MP_STATE_VM(p9_space_env) == MP_OBJ_NULL ? mp_const_none : envitem(0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(space_env_obj, space_env);

// a module's bytecode run in its module (builtinimport.c's
// do_execute_proto_fun)
static void execmodule(mp_module_context_t *context, mp_proto_fun_t proto_fun) {
    mp_obj_dict_t *globals = context->module.globals;
    nlr_jump_callback_node_globals_locals_t ctx;

    ctx.globals = mp_globals_get();
    ctx.locals = mp_locals_get();
    mp_globals_set(globals);
    mp_locals_set(globals);
    nlr_push_jump_callback(&ctx.callback, mp_globals_locals_set_from_nlr_jump_callback);
    mp_call_function_0(mp_make_function_from_proto_fun(proto_fun, context, NULL));
    nlr_pop_jump_callback(true);
}

// the Environment's module NAME: a new module (in sys.modules) with
// its .mpy loaded, not run
static p9_space_mod_t *loadmod(qstr name) {
    mp_map_elem_t *el;
    mp_obj_t mod;
    mp_compiled_module_t cm;
    char path[256], file[80];
    vstr_t v;

    el = mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(envitem(1)))->map,
        mp_obj_new_str(qstr_str(name), qstr_len(name)), MP_MAP_LOOKUP);
    if (el == NULL) {
        mp_raise_msg_varg(&mp_type_ImportError, MP_ERROR_TEXT("no module named '%q' in the Environment"), name);
    }
    snprint(file, sizeof file, "%s.mpy", mp_obj_str_get_str(el->value));
    libpath(path, sizeof path, "mod", file);
    if (readall(path, &v) < 0) {
        raise_err();
    }
    mod = mp_obj_new_module(name);
    memset(&cm, 0, sizeof cm);
    cm.context = MP_OBJ_TO_PTR(mod);
    mp_raw_code_load_mem((const byte *)v.buf, v.len, &cm);
    path[strlen(path) - 3] = 0;
    strcat(path, "py");
    mp_store_attr(mod, MP_QSTR___file__, MP_OBJ_NEW_QSTR(qstr_from_str(path)));
    addmod(name, cm.context, (mp_raw_code_t *)cm.rc);
    return &((p9_space_mod_t *)MP_STATE_VM(p9_space_mods))[nmods - 1];
}

// the Environment's module NAME for a restore: the one in sys.modules,
// else loaded
p9_space_mod_t *p9_space_loadmod(qstr name) {
    p9_space_mod_t *m = MP_STATE_VM(p9_space_mods);
    mp_map_elem_t *el;
    int i;

    if (MP_STATE_VM(p9_space_env) == MP_OBJ_NULL) {
        mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("space: a module without an Environment"));
    }
    el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map, MP_OBJ_NEW_QSTR(name), MP_MAP_LOOKUP);
    for (i = nmods - 1; el != NULL && i >= 0; i--) {
        if (m[i].name == name && MP_OBJ_FROM_PTR(m[i].ctx) == el->value) {
            return &m[i];
        }
    }
    return loadmod(name);
}

// import (mpconfigport.h: mp_builtin___import__): without an
// Environment MicroPython's; with one the builtin modules, then the
// Environment's, loaded once; a module whose top level fails is not
// left in sys.modules (CPython's way)
mp_obj_t p9_import(size_t n, const mp_obj_t *a) {
    mp_map_t *loaded = &MP_STATE_VM(mp_loaded_modules_dict).map;
    mp_map_elem_t *el;
    qstr name;
    p9_space_mod_t *m;
    mp_module_context_t *ctx;
    nlr_buf_t nlr;

    if (MP_STATE_VM(p9_space_env) == MP_OBJ_NULL) {
        return mp_builtin___import___default(n, a);
    }
    name = mp_obj_str_get_qstr(a[0]);
    if ((n > 4 && mp_obj_get_int(a[4]) != 0) || strchr(qstr_str(name), '.') != NULL) {
        mp_raise_msg(&mp_type_ImportError, MP_ERROR_TEXT("packages not supported"));
    }
    if (mp_module_get_builtin(name, false) != MP_OBJ_NULL || mp_module_get_builtin(name, true) != MP_OBJ_NULL) {
        return mp_builtin___import___default(n, a);
    }
    if ((el = mp_map_lookup(loaded, MP_OBJ_NEW_QSTR(name), MP_MAP_LOOKUP)) != NULL) {
        return el->value;
    }
    if (nlr_push(&nlr) == 0) {
        m = loadmod(name);
        ctx = m->ctx;
        execmodule(ctx, m->rc);
        nlr_pop();
    } else {
        mp_map_lookup(loaded, MP_OBJ_NEW_QSTR(name), MP_MAP_LOOKUP_REMOVE_IF_FOUND);
        nlr_jump(nlr.ret_val);
    }
    return MP_OBJ_FROM_PTR(ctx);
}

// /usr/$user/space/NAME/FILE (NAME, FILE may be "")
static void spacepath(char *buf, int n, const char *name, const char *file) {
    snprint(buf, n, "/usr/%s/space%s%s%s%s", getuser(), *name ? "/" : "", name, *file ? "/" : "", file);
}

static void checkname(const char *name) {
    if (*name == 0 || strlen(name) >= sizeof sp.name || strchr(name, '/') != NULL
        || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("not a space name: '%s'"), name);
    }
}

static void put64(byte *p, uint64_t u) {
    int i;

    for (i = 7; i >= 0; i--, u >>= 8) {
        p[i] = u;
    }
}

static uint64_t get64(const byte *p) {
    uint64_t u = 0;
    int i;

    for (i = 0; i < 8; i++) {
        u = u << 8 | p[i];
    }
    return u;
}

// the store's records: the header's user part and the commit ('h',
// 'c' 1: synced) after REC, written; $spaceslow ms amid the write (a
// crash there: tools/test-space.sh T3.3), $spacefail a bad record
// before the commit (T5.7).  -1 failed (errstr)
static int storewrite(vstr_t *rec, void *a) {
    char *slow, *fail;
    size_t half;
    byte b[8];

    (void)a;
    vstr_add_byte(rec, 'h');
    vstr_add_strn(rec, "\0\0\0\x48", 4);	/* 72 */
    vstr_add_strn(rec, mp_obj_str_get_str(envitem(0)), 64);
    put64(b, sp.next);
    vstr_add_strn(rec, (const char *)b, 8);
    vstr_add_strn(rec, "c\1", 2);
    slow = getenv("spaceslow");
    fail = getenv("spacefail");
    half = fail != NULL ? rec->len - 2 : slow != NULL ? rec->len / 2 : rec->len;
    if (write(sp.store, rec->buf, half) != (long)half) {
        free(slow);
        free(fail);
        return -1;
    }
    if (slow != NULL) {
        sleep(atoi(slow));
        free(slow);
    }
    if (fail != NULL) {
        /* before the commit record: the transaction aborted */
        free(fail);
        if (write(sp.store, "x", 1) != 1) {
            return -1;
        }
    }
    if (write(sp.store, rec->buf + half, rec->len - half) != (long)(rec->len - half)) {
        return -1;
    }
    return 0;
}

// the store's stream (gefs-obj.md 3.2) of version VER (0: the newest)
static int storeread(int fd, vlong ver, vstr_t *v) {
    byte b[9];
    vlong off;
    long n;

    if (ver > 0) {
        b[0] = 'v';
        put64(b + 1, ver);
        if (write(fd, b, 9) != 9) {
            return -1;
        }
    }
    vstr_init(v, 65536);
    for (off = 0;; off += n) {
        if (v->alloc - v->len < 32768) {
            vstr_hint_size(v, v->alloc);
        }
        if ((n = pread(fd, v->buf + v->len, 32768, off)) < 0) {
            return -1;
        }
        if (n == 0) {
            return 0;
        }
        v->len += n;
    }
}

// version file PATH read and checked: its body in v (from *body); NULL
// a reason it is no version
static const char *readversion(const char *path, vlong n, vstr_t *v, size_t *body, char *env) {
    uchar h[32];
    const byte *p;

    if (readall(path, v) < 0) {
        return "unreadable";
    }
    p = (const byte *)v->buf;
    if (v->len < HDRLEN + TRLLEN || memcmp(p, "P2SP", 4) != 0) {
        return "not an image";
    }
    if (get64(p + v->len - TRLLEN) != v->len) {
        return "its length";
    }
    p9sha256((uchar *)p, v->len - 32, h);
    if (memcmp(h, p + v->len - 32, 32) != 0) {
        return "its SHA-256";
    }
    if (p[4] != FORMAT) {
        return "another format";
    }
    if ((vlong)get64(p + 5) != n) {
        return "another version's";
    }
    memcpy(env, p + 13, 64);
    env[64] = 0;
    if (!ishex(env)) {
        return "its Environment";
    }
    *body = HDRLEN;
    return NULL;
}

// create(name, env): a new Space, its version 0 empty
static mp_obj_t space_create(mp_obj_t name_in, mp_obj_t env_in) {
    const char *name = mp_obj_str_get_str(name_in), *env = mp_obj_str_get_str(env_in);
    char path[256];
    int fd;
    byte empty[2] = { 0, 0 }, b[8];	/* no roots */
    vstr_t rec;

    checkname(name);
    if (!ishex(env)) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("not an Environment: '%s'"), env);
    }
    libpath(path, sizeof path, "env", env);
    if (access(path, AEXIST) < 0) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("no Environment %s"), env);
    }
    spacepath(path, sizeof path, "", "");
    if (access(path, AEXIST) < 0 && (fd = create(path, OREAD, DMDIR | 0775)) >= 0) {
        close(fd);
    }
    spacepath(path, sizeof path, name, "");
    if (access(path, AEXIST) == 0) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("space %s exists"), name);
    }
    if ((fd = create(path, OREAD, DMDIR | 0775)) < 0) {
        raise_err();
    }
    close(fd);
    spacepath(path, sizeof path, name, "lock");
    if ((fd = create(path, OREAD, DMEXCL | 0664)) < 0) {
        raise_err();
    }
    close(fd);
    /* the store, its version 1: no roots */
    spacepath(path, sizeof path, name, "obj");
    if ((fd = create(path, ORDWR, DMOBJ | 0664)) < 0) {
        raise_err();
    }
    vstr_init(&rec, 128);
    vstr_add_byte(&rec, 'p');
    put64(b, OID_ROOTS);
    vstr_add_strn(&rec, (const char *)b, 8);
    put64(b, sizeof empty - 1);
    vstr_add_strn(&rec, (const char *)b, 8);
    vstr_add_byte(&rec, 0);
    vstr_add_byte(&rec, 'h');
    vstr_add_strn(&rec, "\0\0\0\x48", 4);
    vstr_add_strn(&rec, env, 64);
    put64(b, OID_FIRST);
    vstr_add_strn(&rec, (const char *)b, 8);
    vstr_add_strn(&rec, "c\1", 2);
    if (write(fd, rec.buf, rec.len) != (long)rec.len) {
        close(fd);
        raise_err();
    }
    close(fd);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(space_create_obj, space_create);

static int vcmp(const void *a, const void *b) {
    vlong x = *(const vlong *)a, y = *(const vlong *)b;

    return x < y ? 1 : x > y ? -1 : 0;
}

// the Environment of a Space (named in its store or version)
static void useenv(const char *name, const char *env) {
    if (MP_STATE_VM(p9_space_env) == MP_OBJ_NULL) {
        space_useenv(mp_obj_new_str(env, 64));
    } else if (strcmp(mp_obj_str_get_str(envitem(0)), env) != 0) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("space %s: its Environment is %s"), name, env);
    }
}

// V2-V4's Space in DIR: its newest valid v<N> restored (t*, older
// versions and broken ones as then), its Environment out
static void openversions(const char *dir, const char *name, char *env) {
    char path[300], bad[32];
    const char *why;
    Dir *d, rn;
    long nent, i;
    int fd, nv;
    vlong *v, n;
    vstr_t img;
    size_t body;
    char *e;
    vlong t0;

    if ((fd = open(dir, OREAD)) < 0) {
        raise_err();
    }
    nent = dirreadall(fd, &d);
    close(fd);
    v = m_new(vlong, nent > 0 ? nent : 1);
    nv = 0;
    for (i = 0; i < nent; i++) {
        if (d[i].name[0] == 't') {
            snprint(path, sizeof path, "%s/%s", dir, d[i].name);
            remove(path);
        } else if (d[i].name[0] == 'v' && strlen(d[i].name) == 17) {
            n = strtoll(d[i].name + 1, &e, 10);
            if (*e == 0) {
                v[nv++] = n;
            }
        }
    }
    free(d);
    qsort(v, nv, sizeof v[0], vcmp);
    for (i = 0; i < nv; i++) {
        snprint(path, sizeof path, "%s/v%016lld", dir, v[i]);
        if ((why = readversion(path, v[i], &img, &body, env)) == NULL) {
            break;
        }
        snprint(bad, sizeof bad, "bad%016lld", v[i]);
        mp_printf(&mp_plat_print, "space: %s/v%016lld: %s: renamed %s\n", name, (long long)v[i], why, bad);
        nulldir(&rn);
        rn.name = bad;
        dirwstat(path, &rn);
    }
    if (i == nv) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("space %s: no valid version"), name);
    }
    useenv(name, env);
    t0 = nsec();
    p9_image_restore((const byte *)img.buf + body, img.len - body - TRLLEN);
    p9_spacetime("restore", t0);
    vstr_clear(&img);
}

// open(name[, version]): this process the Space's writer (mp -S): its
// newest version restored, its Environment in use.  An older version
// kept (version=v) is opened to be read: no commits
static mp_obj_t space_open(size_t n_args, const mp_obj_t *args) {
    const char *name = mp_obj_str_get_str(args[0]);
    char dir[256], path[300], env[65], user[256];
    vlong ver, t0;
    uint64_t n;
    size_t nu;
    vstr_t st;
    int fd;
    nlr_buf_t nlr;

    if (sp.isopen) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("a Space is open"));
    }
    checkname(name);
    ver = n_args > 1 && args[1] != mp_const_none ? mp_obj_get_int(args[1]) : 0;
    spacepath(dir, sizeof dir, name, "");
    if (access(dir, AEXIST) < 0) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("no space %s"), name);
    }
    snprint(path, sizeof path, "%s/lock", dir);
    if ((sp.lock = open(path, ORDWR | OCEXEC)) < 0) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("space %s locked"), name);
    }
    if (nlr_push(&nlr) != 0) {
        close(sp.lock);
        sp.lock = -1;
        if (sp.store >= 0) {
            close(sp.store);
            sp.store = -1;
        }
        p9_image_reset();
        nlr_jump(nlr.ret_val);
    }
    snprint(path, sizeof path, "%s/obj", dir);
    sp.migrate = 0;
    if ((fd = open(path, ORDWR | OCEXEC)) >= 0) {
        sp.store = fd;
        if (storeread(fd, ver, &st) < 0) {
            raise_err();
        }
        /* the header: N K len env[64] next[8] */
        if (st.len < 21 + 72 || st.buf[0] != 'H' || get64((byte *)st.buf + 17) >> 32 != 72) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("space %s: not a Space's store"), name);
        }
        memcpy(env, st.buf + 21, 64);
        env[64] = 0;
        if (!ishex(env)) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("space %s: its Environment"), name);
        }
        useenv(name, env);
        t0 = nsec();
        nu = sizeof user;
        p9_image_restore_store((const byte *)st.buf, st.len, &n, user, &nu);
        p9_spacetime("restore", t0);
        sp.n = n;
        sp.next = get64((byte *)user + 64);
        vstr_clear(&st);
    } else {
        /* V2-V4's: taken into a store at the first commit */
        if (ver > 0) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("space %s: no store"), name);
        }
        openversions(dir, name, env);
        if ((sp.store = create(path, ORDWR | OCEXEC, DMOBJ | 0664)) < 0) {
            raise_err();
        }
        sp.n = 0;
        sp.next = OID_FIRST;
        sp.migrate = 1;
    }
    nlr_pop();
    strcpy(sp.name, name);
    strcpy(sp.dir, dir);
    sp.writer = ver > 0 ? -1 : getpid();
    sp.isopen = 1;
    lastcommit = nsec();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(space_open_obj, 1, 2, space_open);

// info() -> (name, version, Environment) of the open Space, or None
static mp_obj_t space_info(void) {
    mp_obj_t t[3];

    if (!sp.isopen) {
        return mp_const_none;
    }
    t[0] = mp_obj_new_str(sp.name, strlen(sp.name));
    t[1] = mp_obj_new_int_from_ll(sp.n);
    t[2] = envitem(0);
    return mp_obj_new_tuple(3, t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(space_info_obj, space_info);

int p9_space_isopen(void) {
    return sp.isopen;
}

// commit points 3 (a rest) and space.atomic (docs/object-model.md 5.3)
static mp_float_t interval = 1.0;
static int atomicdepth, atomicpending;

static int iswriter(void) {
    return sp.isopen && getpid() == sp.writer;
}

// the V2-V4 files of a Space taken into its store
static void rmversions(void) {
    char path[300];
    Dir *d;
    long nent, i;
    int fd;

    if ((fd = open(sp.dir, OREAD)) < 0) {
        return;
    }
    nent = dirreadall(fd, &d);
    close(fd);
    for (i = 0; i < nent; i++) {
        if (d[i].name[0] == 'v' || d[i].name[0] == 't' || strncmp(d[i].name, "bad", 3) == 0) {
            snprint(path, sizeof path, "%s/%s", sp.dir, d[i].name);
            remove(path);
        }
    }
    free(d);
}

// what changed since, as the store's version N+1 (the two before it
// kept).  Any failure a CommitError, the version before staying
static void commit(void) {
    nlr_buf_t nlr;
    vlong t0;
    vstr_t msg;
    mp_print_t pr;
    char *chk;
    int r;

    if (nlr_push(&nlr) == 0) {
        t0 = nsec();
        r = p9_image_commit(&sp.next, storewrite, NULL);
        p9_spacetime("commit", t0);
        if (r < 0) {
            raise_err();
        }
        if (r > 0) {
            sp.n++;
            if (sp.migrate) {
                rmversions();
                sp.migrate = 0;
            }
        }
        lastcommit = nsec();
        if ((chk = getenv("spacecheck")) != NULL) {
            free(chk);
            if ((r = p9_image_check()) > 0) {
                mp_raise_msg_varg(&mp_type_CommitError, MP_ERROR_TEXT("space: check: %d objects differ"), r);
            }
        }
        nlr_pop();
        return;
    }
    if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(((mp_obj_base_t *)nlr.ret_val)->type), MP_OBJ_FROM_PTR(&mp_type_CommitError))) {
        nlr_jump(nlr.ret_val);
    }
    // another (an OSError: the disk full): a CommitError saying it
    vstr_init_print(&msg, 64, &pr);
    mp_obj_print_helper(&pr, MP_OBJ_FROM_PTR(nlr.ret_val), PRINT_EXC);
    mp_raise_msg_varg(&mp_type_CommitError, MP_ERROR_TEXT("%s"), vstr_null_terminated_str(&msg));
}

// a commit point 1 or 2 (FORCE: the program's end, atomic or not); in
// the writer only, not an rfork'd child.  A failure is printed
int p9_space_commit(int force) {
    nlr_buf_t nlr;

    if (!iswriter()) {
        return 0;
    }
    if (atomicdepth > 0 && !force) {
        atomicpending = 1;
        return 0;
    }
    if (nlr_push(&nlr) == 0) {
        commit();
        nlr_pop();
        return 0;
    }
    mp_printf(&mp_plat_print, "space: commit failed: ");
    mp_obj_print_helper(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val), PRINT_EXC);
    mp_printf(&mp_plat_print, "\n");
    return -1;
}

// a rest (modplan9.c: before a call blocks): a commit if space.interval
// has passed since the last; a failure raises CommitError instead of
// blocking
void p9_space_rest(void) {
    if (!iswriter() || nsec() - lastcommit < (vlong)(interval * 1e9)) {
        return;
    }
    if (atomicdepth > 0) {
        atomicpending = 1;
        return;
    }
    commit();
}

// with space.atomic(): the commit points within it deferred to the
// outermost's end
typedef struct {
    mp_obj_base_t base;
} atomic_t;
static const mp_obj_type_t atomic_type;
static const atomic_t atomic_obj = { { &atomic_type } };

static mp_obj_t atomic_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)type;
    mp_arg_check_num(n_args, n_kw, 0, 0, false);
    return MP_OBJ_FROM_PTR(&atomic_obj);
}

static mp_obj_t atomic_enter(mp_obj_t self) {
    atomicdepth++;
    return self;
}
static MP_DEFINE_CONST_FUN_OBJ_1(atomic_enter_obj, atomic_enter);

static mp_obj_t atomic_exit(size_t n, const mp_obj_t *a) {
    (void)n;
    (void)a;
    if (atomicdepth > 0 && --atomicdepth == 0 && atomicpending) {
        atomicpending = 0;
        if (iswriter()) {
            commit();
        }
    }
    return mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(atomic_exit_obj, 4, 4, atomic_exit);

static const mp_rom_map_elem_t atomic_locals[] = {
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&atomic_enter_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&atomic_exit_obj) },
};
static MP_DEFINE_CONST_DICT(atomic_locals_dict, atomic_locals);

static MP_DEFINE_CONST_OBJ_TYPE(
    atomic_type,
    MP_QSTR_atomic,
    MP_TYPE_FLAG_NONE,
    make_new, atomic_make_new,
    locals_dict, &atomic_locals_dict
    );

static const mp_rom_map_elem_t space_globals[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_space) },
    { MP_ROM_QSTR(MP_QSTR_runtime), MP_ROM_PTR(&space_runtime_obj) },
    { MP_ROM_QSTR(MP_QSTR_hash), MP_ROM_PTR(&space_hash_obj) },
    { MP_ROM_QSTR(MP_QSTR_define), MP_ROM_PTR(&space_define_obj) },
    { MP_ROM_QSTR(MP_QSTR_mkenv), MP_ROM_PTR(&space_mkenv_obj) },
    { MP_ROM_QSTR(MP_QSTR_useenv), MP_ROM_PTR(&space_useenv_obj) },
    { MP_ROM_QSTR(MP_QSTR_env), MP_ROM_PTR(&space_env_obj) },
    { MP_ROM_QSTR(MP_QSTR_create), MP_ROM_PTR(&space_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&space_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_info), MP_ROM_PTR(&space_info_obj) },
    { MP_ROM_QSTR(MP_QSTR_CommitError), MP_ROM_PTR(&mp_type_CommitError) },
    { MP_ROM_QSTR(MP_QSTR_atomic), MP_ROM_PTR(&atomic_type) },
};
static MP_DEFINE_CONST_DICT(space_globals_dict, space_globals);

const mp_obj_module_t mp_module_space = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&space_globals_dict,
};

MP_REGISTER_MODULE(MP_QSTR_space, mp_module_space);

// space.interval: the seconds between commits at rests (>= 0)
void space_attr(mp_obj_t self, qstr attr, mp_obj_t *dest) {
    mp_float_t v;

    (void)self;
    if (attr != MP_QSTR_interval) {
        return;
    }
    if (dest[0] == MP_OBJ_NULL) {
        dest[0] = mp_obj_new_float(interval);
    } else if (dest[1] != MP_OBJ_NULL) {
        v = mp_obj_get_float(dest[1]);
        if (!(v >= 0)) {
            mp_raise_ValueError(MP_ERROR_TEXT("space.interval: not >= 0"));
        }
        interval = v;
        dest[0] = MP_OBJ_NULL;
    }
}
MP_REGISTER_MODULE_DELEGATION(mp_module_space, space_attr);
