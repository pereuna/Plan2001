#include "p9compat.h"
/* ISO C's math on libc's: what libc lacks is in p9math.c */
#ifndef P9MATH_H
#define P9MATH_H
#define NAN NaN()
#define INFINITY Inf(1)
#define HUGE_VAL Inf(1)
#define isnan(x) isNaN(x)
#define isinf(x) isInf((x), 0)
#define isfinite(x) (!isNaN(x) && !isInf((x), 0))
#define signbit(x) p9signbit(x)
int p9signbit(double);
double copysign(double, double);
double trunc(double);
double nearbyint(double);
double nan(const char *);
#endif
