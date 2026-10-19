/* Native harness for the actual shared ownership walker; no disk I/O.
 * The fake graph has two snapshots sharing an old three-node subtree,
 * logical and buffered data pointers, an allocation log and a deadlist.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>

typedef unsigned char uchar;
typedef unsigned int uint;
typedef long long vlong;
typedef unsigned long long uvlong;
enum { Blksz=16384, Offksz=17, Ptrsz=24, Pptrsz=26, Dlksz=17,
       Dlvsz=48, Logspc=16348, Tlog=3, Tdlist=4, Tpivot=1, Tleaf=2,
       Kdat=0, Ksnap=4, Kdlist=5, GBraw=1 };
#define nil NULL
#define nelem(x) (sizeof(x)/sizeof((x)[0]))
typedef struct { vlong addr, hash, gen; } Bptr;
typedef struct { char *k; int nk; char *v; int nv; } Kvp;
typedef struct { int op; Kvp Kvp; } Msg;
typedef struct { Bptr bp; int ht; vlong pred; } Tree;
#define Treesz ((int)sizeof(Tree))
typedef struct { Bptr hd, tl; } Dlist;
typedef struct Blk Blk;
struct Blk { Bptr bp, logp; int type,nval,nbuf,logsz; char *data; Kvp vals[4]; Msg msgs[2]; };
typedef struct Arange Arange;
struct Arange { vlong off,len; Arange *next; };
typedef Arange Avl;
typedef struct { Blk *h0,*h1,*logtl; vlong size,used; Arange *free; Bptr loghd; } Arena;
typedef struct Mount Mount;
struct Mount { Mount *next; Tree *root; };
typedef struct { Tree *tree; int i,kind; Kvp kv; } Scan;
typedef struct { Arena *arenas; int narena; Blk *sb0,*sb1; Tree snap;
                 Dlist snapdl,reclaimdl,dropdl; Mount *mounts; long nlimbo,syncing; vlong qgen; } Gefs;
static Gefs state, *fs=&state;
static Blk nodes[65];
static Arena arena;
static Arange ranges[2];
static Tree version, alternate;
static Bptr dataref;
static char datakey[17], snapkeys[2][9], deadbuf[8];
static jmp_buf errors[64];
static int depth;
static char message[256];
#define waserror() setjmp(errors[depth++])
#define poperror() (--depth)
static void error(const char *fmt, ...){
    va_list ap; va_start(ap,fmt); vsnprintf(message,sizeof message,fmt,ap); va_end(ap);
    if(depth<1) abort(); longjmp(errors[--depth],1);
}
static void nexterror(void){ if(depth<1) abort(); longjmp(errors[--depth],1); }
static char *errmsg(void){ return message; }
static void *emalloc(size_t n,int zero){ void *p=zero?calloc(1,n):malloc(n); if(!p) error("out of memory"); return p; }
static void fprint(int fd,const char *fmt,...){ va_list ap; (void)fd; va_start(ap,fmt); vprintf(fmt,ap); va_end(ap); }
#define agetl(p) (*(p))
#define agetv(p) (*(p))
#define agetp(p) (*(p))
#define qlock(p) ((void)(p))
#define qunlock(p) ((void)(p))
static Avl *avlmin(Arange *r){ return r; }
static Avl *avlnext(Avl *r){ return r->next; }
static uint64_t unpack64(const char *p){ uint64_t x=0; for(int i=0;i<8;i++) x=(x<<8)|(uchar)p[i]; return x; }
#define UNPACK64(p) unpack64(p)
static Bptr unpackbp(char *p,int n){ Bptr b; if(n<24) error("short pointer"); memcpy(&b,p,24); return b; }
static Blk *getblk(Bptr bp,int flags){
    (void)flags;
    if(bp.addr%Blksz || bp.addr<0 || bp.addr/Blksz>=65) error("invalid block");
    return &nodes[bp.addr/Blksz];
}
static void dropblk(Blk *b){ (void)b; }
static void getval(Blk *b,int i,Kvp *kv){ *kv=b->vals[i]; }
static void getmsg(Blk *b,int i,Msg *m){ *m=b->msgs[i]; }
static Bptr getptr(Kvp *kv,int *fill){ *fill=0; return unpackbp(kv->v,kv->nv); }
static void btnewscan(Scan *s,char *p,int n){ (void)n; memset(s,0,sizeof *s); s->kind=p[0]; }
static void btenter(Tree *t,Scan *s){ s->tree=t; }
static void btexit(Scan *s){ (void)s; }
static int btnext(Scan *s,Kvp *kv){
    if(s->kind==Ksnap && s->tree==&fs->snap && s->i<2){ *kv=nodes[4].vals[s->i++]; return 1; }
    if(s->kind==Kdat && s->tree!=&fs->snap && s->i++==0){ *kv=nodes[6].vals[0]; return 1; }
    return 0;
}
static void kv2dlist(Kvp *kv,Dlist *dl){ (void)kv; (void)dl; error("unexpected deadlist scan"); }
static void unpacktree(Tree *t,char *p,int n){ if(n!=sizeof *t) error("short tree"); memcpy(t,p,n); }
#define GEFS_AUDIT_USER
#include "../sys/src/9/gefs/audit.c"

int main(int argc,char **argv){
    char *mode=argc>1?argv[1]:"valid";
    static Bptr children[2];
    for(int i=0;i<65;i++){
        nodes[i].bp=(Bptr){(vlong)i*Blksz,0,1};
        nodes[i].logp=(Bptr){-1,-1,-1};
    }
    arena.h0=&nodes[1]; arena.h1=&nodes[2]; arena.logtl=&nodes[3];
    arena.loghd=nodes[3].bp; arena.size=60LL*Blksz; arena.used=8LL*Blksz;
    ranges[0]=(Arange){11LL*Blksz,52LL*Blksz,NULL}; arena.free=&ranges[0];
    fs->arenas=&arena; fs->narena=1; fs->sb0=&nodes[0]; fs->sb1=&nodes[64];
    fs->snap=(Tree){nodes[4].bp,1,-1};
    fs->snapdl=(Dlist){nodes[8].bp,nodes[8].bp};
    fs->reclaimdl=(Dlist){{-1,-1,-1},{-1,-1,-1}};
    fs->dropdl=(Dlist){{-1,-1,-1},{-1,-1,-1}};
    nodes[3].type=Tlog; nodes[8].type=Tdlist;
    for(int i=0;i<8;i++) deadbuf[7-i]=(10LL*Blksz)>>(8*i);
    nodes[8].data=deadbuf; nodes[8].logsz=8;
    version=(Tree){nodes[5].bp,2,9999};
    nodes[4].type=Tleaf; nodes[4].nval=2;
    for(int i=0;i<2;i++){
        snapkeys[i][0]=Ksnap;
        nodes[4].vals[i]=(Kvp){snapkeys[i],9,(char*)&version,sizeof version};
    }
    nodes[5].type=Tpivot; nodes[5].nval=2;
    for(int i=0;i<2;i++){
        children[i]=nodes[6+i].bp;
        nodes[5].vals[i]=(Kvp){snapkeys[i],9,(char*)&children[i],Pptrsz};
        nodes[6+i].type=Tleaf;
    }
    dataref=nodes[9].bp; datakey[0]=Kdat;
    nodes[6].nval=1; nodes[6].vals[0]=(Kvp){datakey,17,(char*)&dataref,24};
    nodes[5].nbuf=1; nodes[5].msgs[0].Kvp=nodes[6].vals[0];
    if(!strcmp(mode,"conflicting")){ alternate=version; alternate.bp.hash++; nodes[4].vals[1].v=(char*)&alternate; }
    if(!strcmp(mode,"overflow")) ranges[0].len=0x7fffffffffffc000LL;
    if(!strcmp(mode,"invalid")) children[0].addr=80LL*Blksz;
    if(!strcmp(mode,"cycle")) children[0]=nodes[5].bp;
    if(!strcmp(mode,"chain-cycle")){ fs->snapdl.tl=nodes[10].bp; nodes[8].logp=nodes[8].bp; }
    if(!strcmp(mode,"free-owned")) dataref=nodes[11].bp;
    if(!strcmp(mode,"duplicate-free")){ ranges[1]=ranges[0]; ranges[0].next=&ranges[1]; }
    if(!strcmp(mode,"unaccounted")){ ranges[0].off+=Blksz; ranges[0].len-=Blksz; }
    if(!strcmp(mode,"live")) fs->nlimbo=7;
    return auditfs(1,strcmp(mode,"live")!=0)?0:1;
}
