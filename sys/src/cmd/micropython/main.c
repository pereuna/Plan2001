/*
 * MicroPython on Plan2001 (docs/mpinit.md).
 *	micropython			the REPL
 *	micropython -c source		run source
 *	micropython file [arg...]	run file (sys.argv: file, args)
 *	micropython -B			Plan2001's init: the built-in boot.py
 *					(the kernel's /boot/mp), then the REPL
 *					again and again
 *	micropython -O volume ...	use the native O volume (a PARTUUID or
 *					a device) for the Space, its Environment
 *					and its code: no filesystem
 *	micropython -E env ...		import from the Environment env alone
 *	micropython -S space ...	in the Space space: its image restored,
 *					committed after each REPL statement
 *					and at the program's end
 *					(docs/object-model.md)
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
#include "p9space.h"
#ifdef __PLAN9__
#include "boot.h"
#else
static const char bootpy[] = "";
#endif

static uintptr_t heap[MICROPY_HEAP_SIZE / sizeof(uintptr_t)];
static uintptr_t pystack[MICROPY_PYSTACK_SIZE / sizeof(uintptr_t)];

// the largest area the heap grows by at a time; a failed malloc stops it
size_t gc_get_max_new_split(void) {
    return 256 * 1024 * 1024;
}

// an exception other than SystemExit: no commit at the end
static int uncaught;

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
    uncaught = 1;
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

// a note: interrupt is KeyboardInterrupt in the running code; alarm
// (plan9.alarm) only cuts the blocked call short, which raises OSError
static int onnote(void *ureg, char *note) {
    (void)ureg;
    if (strcmp(note, "interrupt") == 0) {
        mp_sched_keyboard_interrupt();
        return 1;
    }
    if (strcmp(note, "alarm") == 0) {
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    volatile uintptr_t stack_top;
    int fail = 0, i, boot = 0, autorun = 0, bootfail = 0;
    const char *source = NULL, *file = NULL, *ovol = NULL, *env = NULL, *space = NULL;
    char useenv[128];

    // -O VOLUME: the native O volume; then -E or -S as before
    if (argc >= 3 && strcmp(argv[1], "-O") == 0) {
        ovol = argv[2];
        argv[2] = argv[0];
        argv += 2;
        argc -= 2;
    }
    // -E ENV: import from the Environment ENV alone; -S SPACE: in the
    // Space SPACE (p9space.c)
    if (argc >= 3 && strcmp(argv[1], "-E") == 0) {
        env = argv[2];
        argv[2] = argv[0];
        argv += 2;
        argc -= 2;
    } else if (argc >= 3 && strcmp(argv[1], "-S") == 0) {
        space = argv[2];
        argv[2] = argv[0];
        argv += 2;
        argc -= 2;
    }
    if (argc == 2 && strcmp(argv[1], "-B") == 0) {
        boot = 1;
    } else if (argc == 3 && strcmp(argv[1], "-c") == 0) {
        source = argv[2];
    } else if (argc >= 2 && argv[1][0] != '-') {
        file = argv[1];
    } else if (argc != 1) {
        fprint(2, "usage: micropython [-O volume] [-E env | -S space] [-B | -c source | file [arg...]]\n");
        exits("usage");
    }
    // inf and nan as results, as in C: Plan 9 traps them by default
    setfcr(getfcr() & ~(FPINVAL | FPZDIV | FPOVFL));
    // Plan2001's user stack is 16 MiB (USTKSIZE): half of it for C's
    // recursion (Python's is in the pystack, MICROPY_STACKLESS)
    mp_cstack_init_with_top((void *)&stack_top, 8 * 1024 * 1024);
    gc_init(heap, (char *)heap + sizeof(heap));
    mp_pystack_init(pystack, (char *)pystack + sizeof(pystack));
    mp_init();
    if (ovol != NULL && p9_space_o(ovol) < 0) {
        mp_deinit();
        p9_console_exit(1);
        return 1;
    }
    // Stage 5: bootstrap (devices, console) runs before the Space is
    // restored, so its boot-only objects never enter the persistent
    // __main__ that the restore replaces.
    if (boot && space != NULL) {
        execstr(bootpy, MP_QSTR_boot_dot_py);
    }
    if (space != NULL) {
        // the name goes into Python's source: a plain one
        if (strlen(space) >= 64 || space[strcspn(space, "'\\/\n")] != 0) {
            fprint(2, "micropython: -S %s: not a space name\n", space);
            exits("usage");
        }
        snprint(useenv, sizeof useenv, "__import__('space').open('%s')", space);
        if (execstr(useenv, MP_QSTR__lt_command_gt_) != 0) {
            mp_deinit();
            p9_console_exit(1);
            return 1;
        }
    } else if (env == NULL) {
        mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_));
        mp_obj_list_append(mp_sys_path, mp_obj_new_str("/sys/lib/python", 15));
    } else {
        // the name goes into Python's source: an Environment's is hex
        if (strlen(env) > 64 || env[strspn(env, "0123456789abcdef")] != 0) {
            fprint(2, "micropython: -E %s: not an Environment\n", env);
            exits("usage");
        }
        snprint(useenv, sizeof useenv, "import space\nspace.useenv('%s')", env);
        if (execstr(useenv, MP_QSTR__lt_command_gt_) != 0) {
            mp_deinit();
            p9_console_exit(1);
            return 1;
        }
    }
    for (i = file != NULL ? 1 : argc; i < argc; i++) {
        mp_obj_list_append(mp_sys_argv, mp_obj_new_str(argv[i], strlen(argv[i])));
    }
    atnotify(onnote, 1);
    if (source != NULL) {
        fail = execstr(source, MP_QSTR__lt_command_gt_);
    } else if (file != NULL) {
        fail = execfile(file);
    } else {
        if (boot && space == NULL) {
            // boot.py opens the console (kbdfs) itself
            execstr(bootpy, MP_QSTR_boot_dot_py);
        }
        p9_console_init();
        if (boot && space != NULL) {
            // Stage 5: -O ... -S NAME -B: run the restored init Space
            // once and commit; no REPL reads the console
            autorun = 1;
            if (p9_space_boot() < 0) {
                bootfail = 1;
                fail = 1;
            }
        } else {
            // Ctrl-A enters the raw REPL, Ctrl-B returns, Ctrl-D ends
            // it; as init it starts again
            do {
                repl();
                pyexec_mode_kind = PYEXEC_MODE_FRIENDLY_REPL;
            } while (boot);
        }
    }
    // the program's end, normal or SystemExit: a commit point
    if (!uncaught && !bootfail && p9_space_commit(1) < 0) {
        fail = 1;
        if (autorun) {
            bootfail = 1;
            fprint(2, "init: failed, no commit\n");
        }
    }
    if (autorun) {
        // the commit is durable; report the object identity with the
        // OIDs it now has, then stay alive without a REPL.  QEMU (or
        // the operator) stops the machine; an init exit would panic.
        if (!uncaught && !bootfail && p9_space_report() < 0) {
            fail = 1;
        }
        if (!uncaught && !bootfail) {
            fprint(2, "init: done\n");
        }
        p9_console_restore();
        for (;;) {
            sleep(1000);
        }
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
