/*
 * The plan9 module: Plan 9's system calls for Python (docs/mpinit.md).
 * Names, modes and flags as in libc; a failure raises OSError with the
 * error string.  Files are file descriptors (ints), as in libc.
 */
#include <string.h>
#include "py/runtime.h"
#include "py/objstr.h"
#include "py/objtuple.h"

static void raise_err(void) {
    char buf[ERRMAX];

    // a call the interrupt note cut short: KeyboardInterrupt
    mp_handle_pending(true);
    rerrstr(buf, sizeof buf);
    mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("%s"), buf);
}

static mp_obj_t check(long r) {
    if (r < 0) {
        raise_err();
    }
    return mp_obj_new_int(r);
}

static const char *str(mp_obj_t o) {
    return mp_obj_str_get_str(o);
}

// open(name, mode=OREAD) -> fd
static mp_obj_t p9_open(size_t n, const mp_obj_t *a) {
    return check(open((char *)str(a[0]), n > 1 ? mp_obj_get_int(a[1]) : OREAD));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_open_obj, 1, 2, p9_open);

// create(name, mode, perm) -> fd
static mp_obj_t p9_create(mp_obj_t name, mp_obj_t mode, mp_obj_t perm) {
    return check(create((char *)str(name), mp_obj_get_int(mode), mp_obj_get_int(perm)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(p9_create_obj, p9_create);

static mp_obj_t p9_close(mp_obj_t fd) {
    check(close(mp_obj_get_int(fd)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_close_obj, p9_close);

// alarm(ms) -> the ms the one before had left; at its end the note
// "alarm" cuts a blocked call short (OSError, interrupted); 0 cancels
static mp_obj_t p9_alarm(mp_obj_t ms) {
    return mp_obj_new_int(alarm(mp_obj_get_int(ms)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_alarm_obj, p9_alarm);

// read(fd, n=8192) -> bytes, b'' at the end
static mp_obj_t p9_read(size_t n, const mp_obj_t *a) {
    vstr_t v;
    long r;

    vstr_init_len(&v, n > 1 ? mp_obj_get_int(a[1]) : 8192);
    r = read(mp_obj_get_int(a[0]), v.buf, v.len);
    if (r < 0) {
        vstr_clear(&v);
        raise_err();
    }
    v.len = r;
    return mp_obj_new_bytes_from_vstr(&v);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_read_obj, 1, 2, p9_read);

// write(fd, str or bytes) -> count
static mp_obj_t p9_write(mp_obj_t fd, mp_obj_t data) {
    mp_buffer_info_t b;

    mp_get_buffer_raise(data, &b, MP_BUFFER_READ);
    return check(write(mp_obj_get_int(fd), b.buf, b.len));
}
static MP_DEFINE_CONST_FUN_OBJ_2(p9_write_obj, p9_write);

// seek(fd, offset, type=0) -> offset
static mp_obj_t p9_seek(size_t n, const mp_obj_t *a) {
    vlong r;

    r = seek(mp_obj_get_int(a[0]), mp_obj_get_int(a[1]), n > 2 ? mp_obj_get_int(a[2]) : 0);
    if (r < 0) {
        raise_err();
    }
    return mp_obj_new_int(r);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_seek_obj, 2, 3, p9_seek);

static mp_obj_t p9_remove(mp_obj_t name) {
    check(remove((char *)str(name)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_remove_obj, p9_remove);

// bind(new, old, flag=MREPL)
static mp_obj_t p9_bind(size_t n, const mp_obj_t *a) {
    check(bind((char *)str(a[0]), (char *)str(a[1]), n > 2 ? mp_obj_get_int(a[2]) : MREPL));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_bind_obj, 2, 3, p9_bind);

// mount(fd, old, flag=MREPL, aname='', afd=-1)
static mp_obj_t p9_mount(size_t n, const mp_obj_t *a) {
    check(mount(mp_obj_get_int(a[0]), n > 4 ? mp_obj_get_int(a[4]) : -1, (char *)str(a[1]),
        n > 2 ? mp_obj_get_int(a[2]) : MREPL, n > 3 ? (char *)str(a[3]) : ""));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_mount_obj, 2, 5, p9_mount);

// unmount(old) or unmount(name, old)
static mp_obj_t p9_unmount(size_t n, const mp_obj_t *a) {
    if (n == 1) {
        check(unmount(nil, (char *)str(a[0])));
    } else {
        check(unmount((char *)str(a[0]), (char *)str(a[1])));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_unmount_obj, 1, 2, p9_unmount);

static mp_obj_t p9_chdir(mp_obj_t dir) {
    check(chdir((char *)str(dir)));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_chdir_obj, p9_chdir);

static mp_obj_t p9_fd2path(mp_obj_t fd) {
    char buf[1024];

    check(fd2path(mp_obj_get_int(fd), buf, sizeof buf));
    return mp_obj_new_str(buf, strlen(buf));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_fd2path_obj, p9_fd2path);

// a Dir as (name, mode, length, mtime)
static mp_obj_t dirtuple(Dir *d) {
    mp_obj_t t[4];

    t[0] = mp_obj_new_str(d->name, strlen(d->name));
    t[1] = mp_obj_new_int_from_uint(d->mode);
    t[2] = mp_obj_new_int(d->length);
    t[3] = mp_obj_new_int_from_uint(d->mtime);
    return mp_obj_new_tuple(4, t);
}

// stat(name) -> (name, mode, length, mtime)
static mp_obj_t p9_stat(mp_obj_t name) {
    Dir *d;
    mp_obj_t t;

    if ((d = dirstat((char *)str(name))) == nil) {
        raise_err();
    }
    t = dirtuple(d);
    free(d);
    return t;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_stat_obj, p9_stat);

// dirread(name) -> [(name, mode, length, mtime), ...]
static mp_obj_t p9_dirread(mp_obj_t name) {
    Dir *d;
    long i, n;
    int fd;
    mp_obj_t l;

    if ((fd = open((char *)str(name), OREAD)) < 0) {
        raise_err();
    }
    n = dirreadall(fd, &d);
    close(fd);
    if (n < 0) {
        raise_err();
    }
    l = mp_obj_new_list(0, NULL);
    for (i = 0; i < n; i++) {
        mp_obj_list_append(l, dirtuple(&d[i]));
    }
    free(d);
    return l;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_dirread_obj, p9_dirread);

// wstat(name, newname=None, length=-1, mode=-1): a new name in the same
// directory, length or mode; the rest as it is (nulldir)
static mp_obj_t p9_wstat(size_t n, const mp_obj_t *a) {
    Dir d;

    nulldir(&d);
    if (n > 1 && a[1] != mp_const_none) {
        d.name = (char *)str(a[1]);
    }
    if (n > 2 && mp_obj_get_int(a[2]) >= 0) {
        d.length = mp_obj_get_int(a[2]);
    }
    if (n > 3 && mp_obj_get_int(a[3]) >= 0) {
        d.mode = mp_obj_get_int(a[3]);
    }
    check(dirwstat((char *)str(a[0]), &d));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_wstat_obj, 2, 4, p9_wstat);

// sync(name or fd): a wstat of nothing (nulldir), Plan 9's fsync: gefs
// returns when what was written is on the disk (docs/object-model.md)
static mp_obj_t p9_sync(mp_obj_t f) {
    Dir d;

    nulldir(&d);
    if (mp_obj_is_int(f)) {
        check(dirfwstat(mp_obj_get_int(f), &d));
    } else {
        check(dirwstat((char *)str(f), &d));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_sync_obj, p9_sync);

// dup(old, new=-1) -> fd
static mp_obj_t p9_dup(size_t n, const mp_obj_t *a) {
    return check(dup(mp_obj_get_int(a[0]), n > 1 ? mp_obj_get_int(a[1]) : -1));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_dup_obj, 1, 2, p9_dup);

// pipe() -> (fd, fd)
static mp_obj_t p9_pipe(void) {
    int p[2];
    mp_obj_t t[2];

    check(pipe(p));
    t[0] = mp_obj_new_int(p[0]);
    t[1] = mp_obj_new_int(p[1]);
    return mp_obj_new_tuple(2, t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_pipe_obj, p9_pipe);

// argv as C strings, in memory of its own (the child may not touch the heap)
static char **cargv(mp_obj_t argv) {
    size_t n, i;
    mp_obj_t *items;
    char **v;

    mp_obj_get_array(argv, &n, &items);
    if (n == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("empty argv"));
    }
    v = malloc((n + 1) * sizeof(char *));
    for (i = 0; i < n; i++) {
        v[i] = strdup(str(items[i]));
    }
    v[n] = nil;
    return v;
}

static void freeargv(char **v) {
    char **p;

    for (p = v; *p != nil; p++) {
        free(*p);
    }
    free(v);
}

// spawn(argv, fds=None) -> pid: argv[0] run in a new process with a copy
// of the file descriptors, sharing the name space; fds, if given, are
// its 0, 1, 2 (None: as they are)
static mp_obj_t p9_spawn(size_t n, const mp_obj_t *a) {
    char **v;
    int fd[3], i, pid;
    size_t nf;
    mp_obj_t *items;
    char err[ERRMAX];

    v = cargv(a[0]);
    for (i = 0; i < 3; i++) {
        fd[i] = -1;
    }
    if (n > 1 && a[1] != mp_const_none) {
        mp_obj_get_array(a[1], &nf, &items);
        for (i = 0; i < 3 && i < (int)nf; i++) {
            if (items[i] != mp_const_none) {
                fd[i] = mp_obj_get_int(items[i]);
            }
        }
    }
    pid = rfork(RFPROC | RFFDG | RFREND);
    if (pid == 0) {
        for (i = 0; i < 3; i++) {
            if (fd[i] >= 0 && fd[i] != i) {
                dup(fd[i], i);
            }
        }
        exec(v[0], v);
        rerrstr(err, sizeof err);
        _exits(err);
    }
    freeargv(v);
    return check(pid);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_spawn_obj, 1, 2, p9_spawn);

// exec(argv): this process becomes argv[0]
static mp_obj_t p9_exec(mp_obj_t argv) {
    char **v;

    v = cargv(argv);
    exec(v[0], v);
    freeargv(v);
    raise_err();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_exec_obj, p9_exec);

// wait() -> (pid, status): status '' if the child succeeded
static mp_obj_t p9_wait(void) {
    Waitmsg *w;
    mp_obj_t t[2];

    if ((w = wait()) == nil) {
        raise_err();
    }
    t[0] = mp_obj_new_int(w->pid);
    t[1] = mp_obj_new_str(w->msg, strlen(w->msg));
    free(w);
    return mp_obj_new_tuple(2, t);
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_wait_obj, p9_wait);

// rfork(flags) -> pid: name space, environment, note group and the
// like; with RFPROC the child runs on in Python (a copy of everything)
static mp_obj_t p9_rfork(mp_obj_t flags) {
    return check(rfork(mp_obj_get_int(flags)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_rfork_obj, p9_rfork);

static mp_obj_t p9_sleep(mp_obj_t ms) {
    sleep(mp_obj_get_int(ms));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_sleep_obj, p9_sleep);

static mp_obj_t p9_getpid(void) {
    return mp_obj_new_int(getpid());
}
static MP_DEFINE_CONST_FUN_OBJ_0(p9_getpid_obj, p9_getpid);

// exits(msg=''): the process ends with that status
static mp_obj_t p9_exits(size_t n, const mp_obj_t *a) {
    const char *m;

    m = n > 0 ? str(a[0]) : "";
    exits(*m ? (char *)m : nil);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(p9_exits_obj, 0, 1, p9_exits);

static const mp_rom_map_elem_t plan9_globals[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_plan9) },
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&p9_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_create), MP_ROM_PTR(&p9_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&p9_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&p9_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&p9_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_seek), MP_ROM_PTR(&p9_seek_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove), MP_ROM_PTR(&p9_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_bind), MP_ROM_PTR(&p9_bind_obj) },
    { MP_ROM_QSTR(MP_QSTR_mount), MP_ROM_PTR(&p9_mount_obj) },
    { MP_ROM_QSTR(MP_QSTR_unmount), MP_ROM_PTR(&p9_unmount_obj) },
    { MP_ROM_QSTR(MP_QSTR_chdir), MP_ROM_PTR(&p9_chdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_fd2path), MP_ROM_PTR(&p9_fd2path_obj) },
    { MP_ROM_QSTR(MP_QSTR_stat), MP_ROM_PTR(&p9_stat_obj) },
    { MP_ROM_QSTR(MP_QSTR_alarm), MP_ROM_PTR(&p9_alarm_obj) },
    { MP_ROM_QSTR(MP_QSTR_wstat), MP_ROM_PTR(&p9_wstat_obj) },
    { MP_ROM_QSTR(MP_QSTR_sync), MP_ROM_PTR(&p9_sync_obj) },
    { MP_ROM_QSTR(MP_QSTR_dirread), MP_ROM_PTR(&p9_dirread_obj) },
    { MP_ROM_QSTR(MP_QSTR_dup), MP_ROM_PTR(&p9_dup_obj) },
    { MP_ROM_QSTR(MP_QSTR_pipe), MP_ROM_PTR(&p9_pipe_obj) },
    { MP_ROM_QSTR(MP_QSTR_spawn), MP_ROM_PTR(&p9_spawn_obj) },
    { MP_ROM_QSTR(MP_QSTR_exec), MP_ROM_PTR(&p9_exec_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait), MP_ROM_PTR(&p9_wait_obj) },
    { MP_ROM_QSTR(MP_QSTR_rfork), MP_ROM_PTR(&p9_rfork_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep), MP_ROM_PTR(&p9_sleep_obj) },
    { MP_ROM_QSTR(MP_QSTR_getpid), MP_ROM_PTR(&p9_getpid_obj) },
    { MP_ROM_QSTR(MP_QSTR_exits), MP_ROM_PTR(&p9_exits_obj) },
    { MP_ROM_QSTR(MP_QSTR_OREAD), MP_ROM_INT(OREAD) },
    { MP_ROM_QSTR(MP_QSTR_OWRITE), MP_ROM_INT(OWRITE) },
    { MP_ROM_QSTR(MP_QSTR_ORDWR), MP_ROM_INT(ORDWR) },
    { MP_ROM_QSTR(MP_QSTR_OEXEC), MP_ROM_INT(OEXEC) },
    { MP_ROM_QSTR(MP_QSTR_OTRUNC), MP_ROM_INT(OTRUNC) },
    { MP_ROM_QSTR(MP_QSTR_OCEXEC), MP_ROM_INT(OCEXEC) },
    { MP_ROM_QSTR(MP_QSTR_ORCLOSE), MP_ROM_INT(ORCLOSE) },
    { MP_ROM_QSTR(MP_QSTR_OEXCL), MP_ROM_INT(OEXCL) },
    { MP_ROM_QSTR(MP_QSTR_MREPL), MP_ROM_INT(MREPL) },
    { MP_ROM_QSTR(MP_QSTR_MBEFORE), MP_ROM_INT(MBEFORE) },
    { MP_ROM_QSTR(MP_QSTR_MAFTER), MP_ROM_INT(MAFTER) },
    { MP_ROM_QSTR(MP_QSTR_MCREATE), MP_ROM_INT(MCREATE) },
    { MP_ROM_QSTR(MP_QSTR_MCACHE), MP_ROM_INT(MCACHE) },
    { MP_ROM_QSTR(MP_QSTR_RFNAMEG), MP_ROM_INT(RFNAMEG) },
    { MP_ROM_QSTR(MP_QSTR_RFENVG), MP_ROM_INT(RFENVG) },
    { MP_ROM_QSTR(MP_QSTR_RFFDG), MP_ROM_INT(RFFDG) },
    { MP_ROM_QSTR(MP_QSTR_RFNOTEG), MP_ROM_INT(RFNOTEG) },
    { MP_ROM_QSTR(MP_QSTR_RFPROC), MP_ROM_INT(RFPROC) },
    { MP_ROM_QSTR(MP_QSTR_RFNOWAIT), MP_ROM_INT(RFNOWAIT) },
    { MP_ROM_QSTR(MP_QSTR_RFCNAMEG), MP_ROM_INT(RFCNAMEG) },
    { MP_ROM_QSTR(MP_QSTR_RFCENVG), MP_ROM_INT(RFCENVG) },
    { MP_ROM_QSTR(MP_QSTR_RFCFDG), MP_ROM_INT(RFCFDG) },
    { MP_ROM_QSTR(MP_QSTR_RFREND), MP_ROM_INT(RFREND) },
    { MP_ROM_QSTR(MP_QSTR_RFNOMNT), MP_ROM_INT(RFNOMNT) },
    { MP_ROM_QSTR(MP_QSTR_DMDIR), MP_ROM_INT(DMDIR) },
    { MP_ROM_QSTR(MP_QSTR_DMEXCL), MP_ROM_INT(DMEXCL) },
};
static MP_DEFINE_CONST_DICT(plan9_globals_dict, plan9_globals);

const mp_obj_module_t mp_module_plan9 = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&plan9_globals_dict,
};

MP_REGISTER_MODULE(MP_QSTR_plan9, mp_module_plan9);
