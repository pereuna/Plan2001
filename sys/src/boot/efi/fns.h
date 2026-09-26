#include <bootinfo.h>

enum {
	MAXPATH = 128,
};

extern char hex[];

void usleep(int t);
/* the ISA side of the loader: archx64.c for bootx64.efi, archaa64.c for bootaa64.efi */
void archconf(BootInfo *bi);
uvlong archentry(uvlong entry);
ulong archdataround(void);
int archblobok(uvlong pa, uvlong len);
char *archcheck(void);
void archjump(void *entry, void *bootinfo);

int fsinit(void **pf);

void* (*open)(char *name);
int (*read)(void *f, void *data, int len);
void (*close)(void *f);
void (*stop)(void);

int readn(void *f, void *data, int len);
int bootmapinit(void);
int bootexit(void);
void *bootinfofinish(void);
int conflen(void);
int efialloc(uvlong pa, uvlong len);
void efifree(uvlong pa, uvlong len);
void fbmark(int stage);

extern char *confaddr;
extern char *logbuf;
extern int logcap, logused;

int getc(void);
void putc(int c);

void memset(void *p, int v, int n);
void memmove(void *dst, void *src, int n);
int memcmp(void *src, void *dst, int n);
int strlen(char *s);
char *strchr(char *s, int c);
char *strrchr(char *s, int c);
void print(char *s);

char *configure(void *f, char *path);
char *bootkern(void *f);
char *findconf(char*);

char *hexfmt(char *s, int i, uvlong a);
void tracehex(char *label, uvlong value);
char *decfmt(char *s, int i, ulong a);

uintptr eficall(void *proc, ...);
void eficonfig(void);
