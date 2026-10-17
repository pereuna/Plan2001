#pragma once

/*
 * Plan2001: lwext4 (9front's ext4srv) in the kernel, for devext4
 * (port/devext4.c).  The kernel's headers and allocator instead of
 * libc's; werrstr and %r are ext4glue.c's, because libc's %r would make
 * an errstr system call.
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

typedef enum { false, true } bool;

enum {
	O_RDONLY = 00,
	O_WRONLY = 01,
	O_RDWR = 02,
	O_CREAT = 0100,
	O_EXCL = 0200,
	O_TRUNC = 01000,
	O_APPEND = 02000,

	O_WRMASK = O_WRONLY | O_RDWR,
};

#define CONFIG_EXT4_MAX_BLOCKDEV_NAME 128
#define CONFIG_EXT4_MAX_MP_NAME 128
#define CONFIG_EXT4_BLOCKDEVS_COUNT 32
#define CONFIG_EXT4_MOUNTPOINTS_COUNT 32
#define CONFIG_BLOCK_DEV_CACHE_SIZE 1024

/* Maximum single truncate size. Transactions must be limited to reduce
 * number of allocations for single transaction
 */
#define CONFIG_MAX_TRUNCATE_SIZE (16ul * 1024ul * 1024ul)

/* the kernel's malloc clears what it gives */
#define calloc(n, sz)	malloc((n)*(sz))
#define memcpy(d, s, n)	memmove(d, s, n)
#define time(p)	seconds()
#define abort()	panic("ext4: abort")
#define fprint(fd, ...)	print(__VA_ARGS__)

void	werrstr(char*, ...);
#pragma	varargck	argpos	werrstr	1
#pragma	varargck	type	"r"	void
char*	utfrrune(char*, long);

extern char Eexists[];
extern char Einval[];
extern char Eio[];
extern char Enomem[];
extern char Enospc[];
extern char Enotfound[];
extern char Eperm[];
extern char Erdonlyfs[];
