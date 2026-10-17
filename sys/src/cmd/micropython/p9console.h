#ifndef P9CONSOLE_H
#define P9CONSOLE_H
#include <stddef.h>
void p9_console_init(void);
void p9_console_restore(void);
void p9_console_raw(int on);
int p9_console_read(void);
int p9_console_write(const char *data, size_t size);
unsigned long long p9_console_msec(void);
void p9_console_exit(int failed);
#endif
