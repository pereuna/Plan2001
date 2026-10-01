/*
 * 9front's libc functions drawterm's portable libc lacks, for Plan 9
 * programs on drawterm's kernel (wasmapp): localtime as Plan 9 has it
 * (Tm, a long), on the host's.
 */
#include <time.h>
#include <string.h>

typedef struct Tm Tm;
struct Tm
{
	int	sec;
	int	min;
	int	hour;
	int	mday;
	int	mon;
	int	year;
	int	wday;
	int	yday;
	char	zone[4];
	int	tzoff;
};

Tm*
p9localtime(long t)
{
	static Tm r;
	struct tm tm;
	time_t tt;

	tt = t;
	localtime_r(&tt, &tm);
	r.sec = tm.tm_sec;
	r.min = tm.tm_min;
	r.hour = tm.tm_hour;
	r.mday = tm.tm_mday;
	r.mon = tm.tm_mon;
	r.year = tm.tm_year;
	r.wday = tm.tm_wday;
	r.yday = tm.tm_yday;
	strncpy(r.zone, tm.tm_zone != NULL ? tm.tm_zone : "GMT", 3);
	r.zone[3] = 0;
	r.tzoff = tm.tm_gmtoff;
	return &r;
}
