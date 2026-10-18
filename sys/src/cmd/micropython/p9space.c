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
 */
#include <string.h>
#include "py/runtime.h"
#include "py/compile.h"
#include "py/builtin.h"
#include "py/persistentcode.h"
#include "py/objmodule.h"
#include "py/objstr.h"
#include "py/objtuple.h"

// the runtime an Environment is made for: the .mpy's format
#define RUNTIME "mp-" MICROPY_VERSION_STRING "-mpy" MP_STRINGIFY(MPY_VERSION) "." MP_STRINGIFY(MPY_SUB_VERSION)
MP_DEFINE_STR_OBJ(space_runtime_obj, RUNTIME);

// the Environment of mp -E: (its name, {import name: definition})
MP_REGISTER_ROOT_POINTER(mp_obj_t p9_space_env);

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
static void execmodule(mp_module_context_t *context, mp_proto_fun_t proto_fun, qstr file) {
    mp_obj_dict_t *globals = context->module.globals;
    nlr_jump_callback_node_globals_locals_t ctx;

    mp_store_attr(MP_OBJ_FROM_PTR(&context->module), MP_QSTR___file__, MP_OBJ_NEW_QSTR(file));
    ctx.globals = mp_globals_get();
    ctx.locals = mp_locals_get();
    mp_globals_set(globals);
    mp_locals_set(globals);
    nlr_push_jump_callback(&ctx.callback, mp_globals_locals_set_from_nlr_jump_callback);
    mp_call_function_0(mp_make_function_from_proto_fun(proto_fun, context, NULL));
    nlr_pop_jump_callback(true);
}

// import (mpconfigport.h: mp_builtin___import__): without an
// Environment MicroPython's; with one the builtin modules, then the
// Environment's, loaded once; a module whose top level fails is not
// left in sys.modules (CPython's way)
mp_obj_t p9_import(size_t n, const mp_obj_t *a) {
    mp_map_t *loaded = &MP_STATE_VM(mp_loaded_modules_dict).map;
    mp_map_elem_t *el;
    qstr name;
    mp_obj_t mod;
    mp_compiled_module_t cm;
    char path[256], file[80];
    nlr_buf_t nlr;
    vstr_t v;

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
    if (nlr_push(&nlr) == 0) {
        memset(&cm, 0, sizeof cm);
        cm.context = MP_OBJ_TO_PTR(mod);
        mp_raw_code_load_mem((const byte *)v.buf, v.len, &cm);
        path[strlen(path) - 3] = 0;
        strcat(path, "py");
        execmodule(cm.context, cm.rc, qstr_from_str(path));
        nlr_pop();
    } else {
        mp_map_lookup(loaded, MP_OBJ_NEW_QSTR(name), MP_MAP_LOOKUP_REMOVE_IF_FOUND);
        nlr_jump(nlr.ret_val);
    }
    return mod;
}

static const mp_rom_map_elem_t space_globals[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_space) },
    { MP_ROM_QSTR(MP_QSTR_runtime), MP_ROM_PTR(&space_runtime_obj) },
    { MP_ROM_QSTR(MP_QSTR_hash), MP_ROM_PTR(&space_hash_obj) },
    { MP_ROM_QSTR(MP_QSTR_define), MP_ROM_PTR(&space_define_obj) },
    { MP_ROM_QSTR(MP_QSTR_mkenv), MP_ROM_PTR(&space_mkenv_obj) },
    { MP_ROM_QSTR(MP_QSTR_useenv), MP_ROM_PTR(&space_useenv_obj) },
    { MP_ROM_QSTR(MP_QSTR_env), MP_ROM_PTR(&space_env_obj) },
};
static MP_DEFINE_CONST_DICT(space_globals_dict, space_globals);

const mp_obj_module_t mp_module_space = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&space_globals_dict,
};

MP_REGISTER_MODULE(MP_QSTR_space, mp_module_space);
