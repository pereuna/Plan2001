#include "ext4_config.h"

/*
 * lwext4's werrstr in the kernel: the message goes to up->errstr, where
 * devext4 takes it for error().  %r is the message there now, put into
 * the format as text: libc's %r would make an errstr system call.
 */
void
werrstr(char *fmt, ...)
{
	char nfmt[ERRMAX+64], buf[ERRMAX], *p, *f, *e, *s;
	va_list arg;

	if(up == nil)
		return;
	p = fmt;
	f = nfmt;
	e = nfmt + sizeof nfmt - 1;
	while(*p != 0 && f < e){
		if(p[0] == '%' && p[1] == 'r'){
			for(s = up->errstr; *s != 0 && f < e-1; s++){
				if(*s == '%')
					*f++ = '%';
				*f++ = *s;
			}
			p += 2;
			continue;
		}
		if(p[0] == '%' && p[1] == '%' && f < e-1){
			*f++ = *p++;
			*f++ = *p++;
			continue;
		}
		*f++ = *p++;
	}
	*f = 0;
	va_start(arg, fmt);
	vsnprint(buf, sizeof buf, nfmt, arg);
	va_end(arg);
	kstrcpy(up->errstr, buf, ERRMAX);
}
