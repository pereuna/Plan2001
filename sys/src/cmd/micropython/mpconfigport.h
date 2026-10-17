/*
 * MicroPython on Plan2001 (amd64; docs/mpinit.md): 6c -p -u and Plan 9's
 * libc.  The same file preprocesses the qstrs on Linux (gcc -E, Makefile:
 * make genhdr after a change here).
 */
#include <stdint.h>
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)
/* no floats or big integers yet */
#define MICROPY_PY_MATH (0)
#define MICROPY_PY_CMATH (0)
/* io needs a port's open(): files are the plan9 module's for now */
#define MICROPY_PY_IO (0)
#define MICROPY_PY_BUILTINS_STR_UNICODE (1)
#define MICROPY_PY_FSTRINGS (1)
#define MICROPY_KBD_EXCEPTION (1)
#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_ENABLE_GC (1)
#define MICROPY_HELPER_REPL (1)
#define MICROPY_REPL_EMACS_KEYS (1)
#define MICROPY_REPL_AUTO_INDENT (1)
#define MICROPY_REPL_AUTO_INDENT_ENTER_ENDS (1)
#define MICROPY_REPL_UTF8 (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)
#define MICROPY_PY_BUILTINS_OPEN (0)
#define MICROPY_VFS (0)
#define MICROPY_READER_POSIX (0)
#define MICROPY_NLR_SETJMP (1)
#define MICROPY_GCREGS_SETJMP (1)
#define MICROPY_STACK_CHECK (1)
/* no alloca: the Python stack has its own area */
#define MICROPY_ENABLE_PYSTACK (1)
#define MICROPY_NO_ALLOCA (1)
#define MICROPY_USE_INTERNAL_ERRNO (1)
#define MICROPY_PY_GC (1)
#define MICROPY_PY_SYS (1)
#define MICROPY_PY_SYS_MODULES (1)
#define MICROPY_PY_SYS_PATH (1)
#define MICROPY_PY_SYS_ARGV (1)
#define MICROPY_PY_SYS_EXIT (1)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT (16)
#define MICROPY_HW_BOARD_NAME "Plan2001"
#define MICROPY_HW_MCU_NAME "amd64"
#define MICROPY_PY_SYS_PLATFORM "plan9"
#define MICROPY_HEAP_SIZE (4 * 1024 * 1024)
#define MICROPY_PYSTACK_SIZE (64 * 1024)
#define MP_SSIZE_MAX INTPTR_MAX
#define MP_ENDIANNESS_LITTLE (1)

#ifdef __PLAN9__
#define MP_NORETURN _Noreturn
#define MP_WEAK
#define MP_NOINLINE
#define MP_ALWAYSINLINE
#define MP_LIKELY(x) (x)
#define MP_UNLIKELY(x) (x)
#define MP_UNREACHABLE for (;;);
#define PACKED
#define MP_BITS(n)
#define MP_OBJ_TYPE_NSLOTS 11
#define MP_ROM_TUPLE_NITEMS 4
#define MP_QSTR_POOL_SIZED (1)
#define MP_NO_NONCONSTEXPR_ASSERT
/* typecode and free in one int: str and bytearray share the layout */
#define MP_OBJ_ARRAY_FREE_SIZE_BITS 24
#define snprintf snprint
#endif

typedef long long mp_off_t;
#define MP_STATE_PORT MP_STATE_VM
#define MICROPY_USE_GCC_MUL_OVERFLOW_INTRINSIC (0)
