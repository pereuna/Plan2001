#include <bootinfo.h>

enum {
	MAXPATH = 128,
};

extern char hex[];

void usleep(int t);
void jump64(void *pc, void *mark, ulong pitch);
uvlong getcr3(void);
uvlong getcr4(void);
uvlong rdtsc(void);

int pxeinit(void **pf);
int isoinit(void **pf);
int fsinit(void **pf);

void* (*open)(char *name);
int (*read)(void *f, void *data, int len);
void (*close)(void *f);
void (*stop)(void);

int readn(void *f, void *data, int len);
int bootmapinit(void);
int bootexit(void);
int efialloc(uvlong pa, uvlong len);
int efiallocdata(uvlong pa, uvlong len);
void efifree(uvlong pa, uvlong len);
void fbmark(int stage);
void *fbmarkaddr(int stage);
ulong fbmarkpitch(void);

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
char *decfmt(char *s, int i, ulong a);

uintptr eficall(void *proc, ...);
void eficonfig(void);
