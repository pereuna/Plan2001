#include <u.h>
#include <libc.h>

/*
 * D2 (tools/test-9wasm32): /net over WebSockets - dial the machine's
 * rcpu (webterm /17019): its p9any server speaks first, the ways it
 * authenticates; then the auth server (567) as authdial names it
 * (net!...!ticket: /net/cs), and a port webterm refuses
 */
void
main(void)
{
	char buf[256], dom[64], *p;
	int fd, n, i;

	if((fd = dial("tcp!cpu!17019", nil, nil, nil)) < 0)
		sysfatal("dial rcpu: %r");
	n = 0;
	while(n < sizeof buf - 1 && (i = read(fd, buf+n, 1)) == 1 && buf[n] != 0)
		n++;
	buf[n] = 0;
	print("rcpu says: %s\n", strstr(buf, "dp9ik@") != nil ? "p9any: dp9ik" : buf);
	/* dp9ik, and a client's challenge: the server's ticket request comes back */
	p = strchr(buf, '@');
	snprint(dom, sizeof dom, "dp9ik %s", p != nil ? p+1 : "");
	if(write(fd, dom, strlen(dom)+1) != strlen(dom)+1 || write(fd, "01234567", 8) != 8)
		sysfatal("write rcpu: %r");
	for(n = 0; n < 141 && (i = read(fd, buf, sizeof buf)) > 0; n += i)
		;
	print("rcpu answers the challenge: %s\n", n >= 141 ? "a ticket request" : "no");
	close(fd);
	if((fd = dial("net!cpu!ticket", nil, nil, nil)) < 0)	/* through /net/cs */
		sysfatal("dial ticket: %r");
	print("auth: connected\n");
	close(fd);
	if((fd = dial("tcp!cpu!25", nil, nil, nil)) >= 0)
		print("port 25: connected?\n");
	else
		print("port 25: %r\n");
	exits(nil);
}
