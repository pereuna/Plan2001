#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"

#define	MAXCONF 64
static char *confname[MAXCONF];
static char *confval[MAXCONF];
static int nconf;

static char acpibuf[24];
static char fbbuf[128];

/* set name to val, replacing an earlier value; the strings are not copied */
static void
addconf(char *name, char *val)
{
	int j;

	for(j = 0; j < nconf; j++){
		if(cistrcmp(confname[j], name) == 0)
			break;
	}
	if(j == MAXCONF)
		return;
	confname[j] = name;
	confval[j] = val;
	if(j == nconf)
		nconf++;
}

void
bootargsinit(void)
{
	int i, n;
	char *cp, *line[MAXCONF], *p, *q;

	/*
	 *  parse configuration args from dos file plan9.ini
	 */
	cp = BOOTARGS;	/* where b.com leaves its config */
	cp[BOOTARGSLEN-1] = 0;

	/*
	 * Strip out '\r', change '\t' -> ' '.
	 */
	p = cp;
	for(q = cp; *q; q++){
		if(*q == '\r')
			continue;
		if(*q == '\t')
			*q = ' ';
		*p++ = *q;
	}
	*p = 0;

	n = getfields(cp, line, MAXCONF, 1, "\n");
	for(i = 0; i < n; i++){
		if(*line[i] == '#')
			continue;
		cp = strchr(line[i], '=');
		if(cp == nil)
			continue;
		*cp++ = '\0';
		addconf(line[i], cp);
	}

	/*
	 * BootInfo is the source of these; keep their historical text names
	 * for the code that reads them with getconf (archacpi.c, screen.c) and,
	 * through the environment, for user programs.
	 */
	if(bootinfo != nil){
		if(bootinfo->acpi != 0){
			snprint(acpibuf, sizeof acpibuf, "%#llux", bootinfo->acpi);
			addconf("*acpi", acpibuf);
		}
		if(bootinfo->fbbase != 0){
			snprint(fbbuf, sizeof fbbuf, "%udx%udx%udx%ud %s %#llux %#llux",
				bootinfo->fbwidth, bootinfo->fbheight, bootinfo->fbstride,
				bootinfo->fbdepth, bootinfo->fbchan, bootinfo->fbbase, bootinfo->fbsize);
			addconf("*bootscreen", fbbuf);
		}
	}
}

char*
getconf(char *name)
{
	int i;

	for(i = 0; i < nconf; i++)
		if(cistrcmp(confname[i], name) == 0)
			return confval[i];
	return 0;
}

void
setconfenv(void)
{
	int i;

	for(i = 0; i < nconf; i++){
		if(confname[i][0] != '*')
			ksetenv(confname[i], confval[i], 0);
		ksetenv(confname[i], confval[i], 1);
	}
}

void
writeconf(void)
{
	char *p, *q;
	int n;

	p = getconfenv();
	if(waserror()) {
		free(p);
		nexterror();
	}

	/* convert to name=value\n format */
	for(q=p; *q; q++) {
		q += strlen(q);
		*q = '=';
		q += strlen(q);
		*q = '\n';
	}
	n = q - p + 1;
	if(n >= BOOTARGSLEN)
		error("kernel configuration too large");
	memmove(BOOTARGS, p, n);
	memset(BOOTLINE, 0, BOOTLINELEN);
	poperror();
	free(p);
}
