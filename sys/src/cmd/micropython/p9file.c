/*
 * open() and file objects (io.FileIO, io.TextIOWrapper) on a Plan 9
 * file descriptor, as CPython's: what a model writes first.  Unbuffered:
 * each read and write is a system call (readline a byte at a time).  A
 * failure raises OSError with the error string, as the plan9 module.
 */
#include <string.h>
#include "py/runtime.h"
#include "py/stream.h"
#include "py/mperrno.h"
#include "py/builtin.h"

typedef struct _p9_file_t {
    mp_obj_base_t base;
    int fd;
} p9_file_t;

static const mp_obj_type_t p9_fileio_type;
static const mp_obj_type_t p9_textio_type;

static void raise_err(void) {
    char buf[ERRMAX];

    // a call the interrupt note cut short: KeyboardInterrupt
    mp_handle_pending(true);
    rerrstr(buf, sizeof buf);
    mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("%s"), buf);
}

static int fdof(mp_obj_t o_in) {
    p9_file_t *o = MP_OBJ_TO_PTR(o_in);

    if (o->fd < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("I/O operation on closed file"));
    }
    return o->fd;
}

static void p9_file_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    p9_file_t *self = MP_OBJ_TO_PTR(self_in);

    (void)kind;
    mp_printf(print, "<io.%s %d>", mp_obj_get_type_str(self_in), self->fd);
}

// open(file, mode='r', buffering=-1, encoding=None, errors=None,
// newline=None): file a name or a file descriptor; mode r, w, a, x,
// with + and b or t.  w creates (truncating), a writes at the end, x
// fails if the file exists.  encoding and the rest: UTF-8 always
static mp_obj_t p9_open(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_file, ARG_mode };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_file, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_mode, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_QSTR(MP_QSTR_r)} },
        { MP_QSTR_buffering, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_encoding, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_errors, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_newline, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed)];
    const mp_obj_type_t *type;
    const char *m, *name;
    int how, rw, plus, fd;
    p9_file_t *o;

    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed), allowed, args);
    type = &p9_textio_type;
    how = 'r';
    plus = 0;
    for (m = mp_obj_str_get_str(args[ARG_mode].u_obj); *m; m++) {
        switch (*m) {
            case 'r':
            case 'w':
            case 'a':
            case 'x':
                how = *m;
                break;
            case '+':
                plus = 1;
                break;
            case 'b':
                type = &p9_fileio_type;
                break;
            case 't':
                type = &p9_textio_type;
                break;
            default:
                mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("invalid mode: '%s'"),
                    mp_obj_str_get_str(args[ARG_mode].u_obj));
        }
    }
    if (mp_obj_is_small_int(args[ARG_file].u_obj)) {
        fd = MP_OBJ_SMALL_INT_VALUE(args[ARG_file].u_obj);
    } else {
        name = mp_obj_str_get_str(args[ARG_file].u_obj);
        rw = plus ? ORDWR : how == 'r' ? OREAD : OWRITE;
        switch (how) {
            case 'r':
                fd = open((char *)name, rw);
                break;
            case 'w':
                fd = create((char *)name, rw, 0666);
                break;
            case 'x':
                fd = create((char *)name, rw | OEXCL, 0666);
                break;
            default:
                if ((fd = open((char *)name, rw)) >= 0) {
                    seek(fd, 0, 2);
                } else {
                    fd = create((char *)name, rw, 0666);
                }
                break;
        }
        if (fd < 0) {
            raise_err();
        }
    }
    o = mp_obj_malloc_with_finaliser(p9_file_t, type);
    o->fd = fd;
    return MP_OBJ_FROM_PTR(o);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, p9_open);

static mp_uint_t p9_file_read(mp_obj_t o_in, void *buf, mp_uint_t size, int *errcode) {
    long r;

    (void)errcode;
    if ((r = read(fdof(o_in), buf, size)) < 0) {
        raise_err();
    }
    return r;
}

static mp_uint_t p9_file_write(mp_obj_t o_in, const void *buf, mp_uint_t size, int *errcode) {
    long r;

    (void)errcode;
    if ((r = write(fdof(o_in), (void *)buf, size)) < 0) {
        raise_err();
    }
    return r;
}

static mp_uint_t p9_file_ioctl(mp_obj_t o_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    p9_file_t *o = MP_OBJ_TO_PTR(o_in);
    struct mp_stream_seek_t *s;
    vlong off;

    switch (request) {
        case MP_STREAM_FLUSH:
            fdof(o_in);
            return 0;
        case MP_STREAM_SEEK:
            s = (struct mp_stream_seek_t *)arg;
            if ((off = seek(fdof(o_in), s->offset, s->whence)) < 0) {
                raise_err();
            }
            s->offset = off;
            return 0;
        case MP_STREAM_CLOSE:
            if (o->fd >= 0) {
                close(o->fd);
            }
            o->fd = -1;
            return 0;
        case MP_STREAM_GET_FILENO:
            return fdof(o_in);
        default:
            *errcode = MP_EINVAL;
            return MP_STREAM_ERROR;
    }
}

static mp_obj_t p9_file_fileno(mp_obj_t self_in) {
    return MP_OBJ_NEW_SMALL_INT(fdof(self_in));
}
static MP_DEFINE_CONST_FUN_OBJ_1(p9_file_fileno_obj, p9_file_fileno);

static const mp_rom_map_elem_t p9_file_locals[] = {
    { MP_ROM_QSTR(MP_QSTR_fileno), MP_ROM_PTR(&p9_file_fileno_obj) },
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_readlines), MP_ROM_PTR(&mp_stream_unbuffered_readlines_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_seek), MP_ROM_PTR(&mp_stream_seek_obj) },
    { MP_ROM_QSTR(MP_QSTR_tell), MP_ROM_PTR(&mp_stream_tell_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&mp_stream_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&mp_stream___exit___obj) },
};
static MP_DEFINE_CONST_DICT(p9_file_locals_dict, p9_file_locals);

static const mp_stream_p_t p9_fileio_stream = {
    .read = p9_file_read,
    .write = p9_file_write,
    .ioctl = p9_file_ioctl,
};

static const mp_stream_p_t p9_textio_stream = {
    .read = p9_file_read,
    .write = p9_file_write,
    .ioctl = p9_file_ioctl,
    .is_text = true,
};

static MP_DEFINE_CONST_OBJ_TYPE(
    p9_fileio_type,
    MP_QSTR_FileIO,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    print, p9_file_print,
    protocol, &p9_fileio_stream,
    locals_dict, &p9_file_locals_dict
    );

static MP_DEFINE_CONST_OBJ_TYPE(
    p9_textio_type,
    MP_QSTR_TextIOWrapper,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    print, p9_file_print,
    protocol, &p9_textio_stream,
    locals_dict, &p9_file_locals_dict
    );
