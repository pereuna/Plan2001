/* The console: /dev/consctl rawon for the REPL, fds 0 and 1. */
#include "p9compat.h"
#include <keyboard.h>
#include "p9console.h"
static int consctl = -1;

void p9_console_restore(void) {
    if (consctl >= 0) {
        write(consctl, "rawoff", 6);
        close(consctl);
        consctl = -1;
    }
}

// raw for the line editor, cooked while code runs (the console only)
void p9_console_raw(int on) {
    if (consctl >= 0) {
        if (on) write(consctl, "rawon", 5);
        else write(consctl, "rawoff", 6);
    }
}

void p9_console_init(void) {
    char path[64];
    int n;
    // raw mode only for the console itself, not a pipe or a file
    if (fd2path(0, path, sizeof path) < 0) return;
    n = strlen(path);
    if (n < 4 || strcmp(path + n - 4, "cons") != 0) return;
    consctl = open("/dev/consctl", OWRITE);
    if (consctl >= 0) write(consctl, "rawon", 5);
    atexit(p9_console_restore);
}

// Bytes still to hand out: a key's escape sequence, or a rune passed on.
static char pend[8];
static int npend, ipend;

static int pending(char *b, int n) {
    memmove(pend, b, n);
    npend = n;
    ipend = 1;
    return (uchar)pend[0];
}

// The console's keys as a VT100 terminal sends them, for readline: the
// raw console (kbdfs) gives \n for Enter and Plan 9's private runes for
// the arrows, Home, End and Del.  Other bytes go through as they are.
// -1 at EOF, -2 on a read error.
static int rd(char *b) {
    long n = readn(0, b, 1);
    return n == 1 ? 0 : n == 0 ? -1 : -2;
}

int p9_console_read(void) {
    char b[UTFmax];
    Rune r;
    int n, i;

    if (ipend < npend) return (uchar)pend[ipend++];
    if ((n = rd(b)) < 0) return n;
    // a line's end, from a pipe too (readline ends a line at \r)
    if (b[0] == '\n') return '\r';
    if (consctl < 0) return (uchar)b[0];
    if (b[0] == Kdel) return pending("\x1b[3~", 4);
    if ((uchar)b[0] < Runeself) return (uchar)b[0];
    for (n = 1; n < UTFmax && !fullrune(b, n); n++)
        if ((i = rd(b + n)) < 0) return i;
    chartorune(&r, b);
    switch (r) {
    case Kup: return pending("\x1b[A", 3);
    case Kdown: return pending("\x1b[B", 3);
    case Kright: return pending("\x1b[C", 3);
    case Kleft: return pending("\x1b[D", 3);
    case Khome: return pending("\x1b[H", 3);
    case Kend: return pending("\x1b[F", 3);
    }
    if (r >= KF && r < KF + 0x1000) return p9_console_read();  // other special keys
    return pending(b, n);
}

int p9_console_write(const char *data, size_t size) {
    long n;
    while (size) {
        n = write(1, (void *)data, size > 8192 ? 8192 : (long)size);
        if (n <= 0) return -1;
        data += n;
        size -= n;
    }
    return 0;
}

unsigned long long p9_console_msec(void) {
    return (unsigned long long)nsec() / 1000000;
}

void p9_console_exit(int failed) {
    p9_console_restore();
    exits(failed ? "micropython" : nil);
}
