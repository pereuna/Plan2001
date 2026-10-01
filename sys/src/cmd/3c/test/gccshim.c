/* the tests under gcc: _trap as POSIX, for tools/test-3c's comparison */
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

long long
_trap(int n, ...)
{
	va_list a;
	int fd;
	char *p;
	long len;

	va_start(a, n);
	switch(n) {
	case 51:
		fd = va_arg(a, int);
		p = va_arg(a, char*);
		len = va_arg(a, long);
		return write(fd, p, len);
	case 8:
		p = va_arg(a, char*);
		if(p && *p) {
			fprintf(stderr, "exits: %s\n", p);
			exit(1);
		}
		exit(0);
	}
	return -1;
}

extern void _main(void);

int
main(void)
{
	_main();
	return 0;
}
