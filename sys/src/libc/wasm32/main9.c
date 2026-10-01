#include <u.h>
#include <libc.h>
#include <tos.h>

/*
 * wasm32: the kernel puts argc, argv[0] ... nil at SP and calls _main
 * (3l's _start): they are _main's parameters, &arg0 is argv
 */
char	*argv0;
Tos	*_tos;
void	**_privates;
int	_nprivates;

enum {
	NPRIVATES = 16,
};

static	Tos	tos;

void
_main(int argc, char *arg0)
{
	void *privates[NPRIVATES];

	_tos = &tos;
	memset(privates, 0, sizeof(privates));
	_privates = privates;
	_nprivates = NPRIVATES;
	main(argc, &arg0);
	exits("main");
}
