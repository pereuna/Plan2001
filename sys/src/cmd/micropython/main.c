/*
 * MicroPython on Plan2001 (docs/mpinit.md).
 *	micropython			the REPL
 *	micropython -c source		run source
 *	micropython file [arg...]	run file (sys.argv: file, args)
 *	micropython -B			Plan2001's init: the built-in boot.py
 *					(the kernel's /boot/mp), then the REPL
 *					again and again
 * A failure exits with status micropython (usage: usage); sys.exit(n)
 * with n.
 */
#include <string.h>
#include "py/compile.h"
#include "py/builtin.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/cstack.h"
#include "py/objlist.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"
#include "p9console.h"
#ifdef __PLAN9__
#include "boot.h"
#else
static const char bootpy[] = "";
#endif

static uintptr_t heap[MICROPY_HEAP_SIZE / sizeof(uintptr_t)];
static uintptr_t pystack[MICROPY_PYSTACK_SIZE / sizeof(uintptr_t)];

// SystemExit's status, or 1 for another exception (printed)
static int failed(mp_obj_t exc) {
    mp_obj_t code;

    if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(mp_obj_get_type(exc)), MP_OBJ_FROM_PTR(&mp_type_SystemExit))) {
        code = mp_obj_exception_get_value(exc);
        if (code == mp_const_none) {
            return 0;
        }
        if (mp_obj_is_small_int(code)) {
            return MP_OBJ_SMALL_INT_VALUE(code) != 0;
        }
        return 1;
    }
    mp_obj_print_exception(&mp_plat_print, exc);
    return 1;
}

static int execute(mp_lexer_t *lex, qstr name) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_parse_tree_t tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t fun = mp_compile(&tree, name, false);
        mp_call_function_0(fun);
        nlr_pop();
        return 0;
    }
    return failed(MP_OBJ_FROM_PTR(nlr.ret_val));
}

static int execstr(const char *source, qstr name) {
    return execute(mp_lexer_new_from_str_len(name, source, strlen(source), 0), name);
}

static int execfile(const char *file) {
    nlr_buf_t nlr;
    qstr name;
    mp_lexer_t *lex;

    name = qstr_from_str(file);
    if (nlr_push(&nlr) == 0) {
        lex = mp_lexer_new_from_file(name);
        nlr_pop();
    } else {
        return failed(MP_OBJ_FROM_PTR(nlr.ret_val));
    }
    return execute(lex, name);
}

static void repl(void) {
    for (;;) {
        int result = pyexec_mode_kind == PYEXEC_MODE_RAW_REPL
            ? pyexec_raw_repl() : pyexec_friendly_repl();
        if (result != 0) {
            break;
        }
    }
}

// a note: interrupt is KeyboardInterrupt in the running code
static int onnote(void *ureg, char *note) {
    (void)ureg;
    if (strcmp(note, "interrupt") == 0) {
        mp_sched_keyboard_interrupt();
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    volatile uintptr_t stack_top;
    int fail = 0, i, boot = 0;
    const char *source = NULL, *file = NULL;

    if (argc == 2 && strcmp(argv[1], "-B") == 0) {
        boot = 1;
    } else if (argc == 3 && strcmp(argv[1], "-c") == 0) {
        source = argv[2];
    } else if (argc >= 2 && argv[1][0] != '-') {
        file = argv[1];
    } else if (argc != 1) {
        fprint(2, "usage: micropython [-B | -c source | file [arg...]]\n");
        exits("usage");
    }
    // Plan2001's user stack is 16 MiB (USTKSIZE)
    mp_cstack_init_with_top((void *)&stack_top, 256 * 1024);
    gc_init(heap, (char *)heap + sizeof(heap));
    mp_pystack_init(pystack, (char *)pystack + sizeof(pystack));
    mp_init();
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_));
    mp_obj_list_append(mp_sys_path, mp_obj_new_str("/sys/lib/python", 15));
    for (i = file != NULL ? 1 : argc; i < argc; i++) {
        mp_obj_list_append(mp_sys_argv, mp_obj_new_str(argv[i], strlen(argv[i])));
    }
    atnotify(onnote, 1);
    if (source != NULL) {
        fail = execstr(source, MP_QSTR__lt_command_gt_);
    } else if (file != NULL) {
        fail = execfile(file);
    } else {
        if (boot) {
            // boot.py opens the console (kbdfs) itself
            execstr(bootpy, MP_QSTR_boot_dot_py);
        }
        p9_console_init();
        // Ctrl-A enters the raw REPL, Ctrl-B returns, Ctrl-D ends it;
        // as init it starts again
        do {
            repl();
            pyexec_mode_kind = PYEXEC_MODE_FRIENDLY_REPL;
        } while (boot);
    }
    mp_deinit();
    p9_console_exit(fail);
    return fail;
}

void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}

MP_NORETURN void nlr_jump_fail(void *value) {
    (void)value;
    const char *message = "fatal: uncaught MicroPython exception\n";
    p9_console_write(message, strlen(message));
    p9_console_exit(1);
    for (;;) {}
}

// a file as source: import and micropython file
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    int fd;
    long n;
    vstr_t v;

    if ((fd = open((char *)qstr_str(filename), OREAD)) < 0) {
        mp_raise_OSError(MP_ENOENT);
    }
    vstr_init(&v, 8192);
    for (;;) {
        vstr_hint_size(&v, 8192);
        n = read(fd, v.buf + v.len, v.alloc - v.len);
        if (n <= 0) {
            break;
        }
        v.len += n;
    }
    close(fd);
    if (n < 0) {
        vstr_clear(&v);
        mp_raise_OSError(MP_EIO);
    }
    return mp_lexer_new_from_str_len(filename, v.buf, v.len, v.alloc);
}

mp_import_stat_t mp_import_stat(const char *path) {
    Dir *d;
    mp_import_stat_t s;

    if ((d = dirstat((char *)path)) == nil) {
        return MP_IMPORT_STAT_NO_EXIST;
    }
    s = (d->mode & DMDIR) ? MP_IMPORT_STAT_DIR : MP_IMPORT_STAT_FILE;
    free(d);
    return s;
}
