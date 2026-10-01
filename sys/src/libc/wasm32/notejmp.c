#include <u.h>
#include <libc.h>
#include <ureg.h>

void
notejmp(void*, jmp_buf, int)
{
	exits("notejmp: not on wasm32 yet");
}
