/*
 * wasm32 system calls, made by mksyscall from 9syscall/sys.h:
 * _trap(number, the arguments) (3l: plan9.syscall), they are in memory
 * as a trap would find them; the result in RET (both classes)
 */
typedef long long vlong;
extern vlong _trap(int, void*);

vlong
sysr1(void *a)
{
	return _trap(0, &a);
}

vlong
_errstr(void *a)
{
	return _trap(1, &a);
}

vlong
bind(void *a)
{
	return _trap(2, &a);
}

vlong
chdir(void *a)
{
	return _trap(3, &a);
}

vlong
close(void *a)
{
	return _trap(4, &a);
}

vlong
dup(void *a)
{
	return _trap(5, &a);
}

vlong
alarm(void *a)
{
	return _trap(6, &a);
}

vlong
exec(void *a)
{
	return _trap(7, &a);
}

vlong
_exits(void *a)
{
	return _trap(8, &a);
}

vlong
_fsession(void *a)
{
	return _trap(9, &a);
}

vlong
fauth(void *a)
{
	return _trap(10, &a);
}

vlong
_fstat(void *a)
{
	return _trap(11, &a);
}

vlong
segbrk(void *a)
{
	return _trap(12, &a);
}

vlong
_mount(void *a)
{
	return _trap(13, &a);
}

vlong
open(void *a)
{
	return _trap(14, &a);
}

vlong
_read(void *a)
{
	return _trap(15, &a);
}

vlong
oseek(void *a)
{
	return _trap(16, &a);
}

vlong
sleep(void *a)
{
	return _trap(17, &a);
}

vlong
_stat(void *a)
{
	return _trap(18, &a);
}

vlong
rfork(void *a)
{
	return _trap(19, &a);
}

vlong
_write(void *a)
{
	return _trap(20, &a);
}

vlong
pipe(void *a)
{
	return _trap(21, &a);
}

vlong
create(void *a)
{
	return _trap(22, &a);
}

vlong
fd2path(void *a)
{
	return _trap(23, &a);
}

vlong
brk_(void *a)
{
	return _trap(24, &a);
}

vlong
remove(void *a)
{
	return _trap(25, &a);
}

vlong
_wstat(void *a)
{
	return _trap(26, &a);
}

vlong
_fwstat(void *a)
{
	return _trap(27, &a);
}

vlong
notify(void *a)
{
	return _trap(28, &a);
}

vlong
noted(void *a)
{
	return _trap(29, &a);
}

vlong
segattach(void *a)
{
	return _trap(30, &a);
}

vlong
segdetach(void *a)
{
	return _trap(31, &a);
}

vlong
segfree(void *a)
{
	return _trap(32, &a);
}

vlong
segflush(void *a)
{
	return _trap(33, &a);
}

vlong
rendezvous(void *a)
{
	return _trap(34, &a);
}

vlong
unmount(void *a)
{
	return _trap(35, &a);
}

vlong
_wait(void *a)
{
	return _trap(36, &a);
}

vlong
semacquire(void *a)
{
	return _trap(37, &a);
}

vlong
semrelease(void *a)
{
	return _trap(38, &a);
}

vlong
seek(void *a)
{
	return _trap(39, &a);
}

vlong
fversion(void *a)
{
	return _trap(40, &a);
}

vlong
errstr(void *a)
{
	return _trap(41, &a);
}

vlong
stat(void *a)
{
	return _trap(42, &a);
}

vlong
fstat(void *a)
{
	return _trap(43, &a);
}

vlong
wstat(void *a)
{
	return _trap(44, &a);
}

vlong
fwstat(void *a)
{
	return _trap(45, &a);
}

vlong
mount(void *a)
{
	return _trap(46, &a);
}

vlong
await(void *a)
{
	return _trap(47, &a);
}

vlong
pread(void *a)
{
	return _trap(50, &a);
}

vlong
pwrite(void *a)
{
	return _trap(51, &a);
}

vlong
tsemacquire(void *a)
{
	return _trap(52, &a);
}

vlong
_nsec(void *a)
{
	return _trap(53, &a);
}

