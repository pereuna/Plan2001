#include <u.h>
#include <libc.h>

/*
 * wasm32: no setjmp yet (WebAssembly's exceptions will do it); a
 * longjmp ends the process
 */
int
setjmp(jmp_buf)
{
	return 0;
}

_Noreturn void
longjmp(jmp_buf, int)
{
	exits("longjmp: not on wasm32 yet");
}
