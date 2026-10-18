/* ISO C's math that Plan 9's libc lacks (include/math.h), for doubles. */
#include "p9compat.h"
#include <math.h>

int p9signbit(double x) {
    FPdbleword w;

    w.x = x;
    return (w.hi >> 31) != 0;
}

double copysign(double x, double y) {
    FPdbleword w, s;

    w.x = x;
    s.x = y;
    w.hi = (w.hi & 0x7fffffff) | (s.hi & 0x80000000);
    return w.x;
}

// toward zero; the sign of zero kept
double trunc(double x) {
    double i;

    if (isNaN(x) || isInf(x, 0)) {
        return x;
    }
    modf(x, &i);
    return copysign(i, x);
}

// the nearest integer, halves to even (the default rounding mode)
double nearbyint(double x) {
    double i, f;

    if (isNaN(x) || isInf(x, 0)) {
        return x;
    }
    f = modf(x, &i);
    if (f >= 0.5 || f <= -0.5) {
        if (f == 0.5 || f == -0.5) {
            if (fmod(i, 2) != 0) {
                i += f > 0 ? 1 : -1;
            }
        } else {
            i += f > 0 ? 1 : -1;
        }
    }
    return copysign(i, x);
}

double nan(const char *s) {
    (void)s;
    return NaN();
}
