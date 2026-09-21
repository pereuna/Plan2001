#include <u.h>
#include <libc.h>

/*
 * Stand-in for the real /boot/boot (boot.c, bootrc, ...), which is outside
 * this stripped source tree.  The kernel's initcode ends with
 * exec("/boot/boot"); reaching this point proves the whole path from the
 * UEFI loader through main() and init0() to the first user process.
 */
void
main(void)
{
	print("boot: /boot/boot reached\n");
	for(;;)
		sleep(1000);
}
