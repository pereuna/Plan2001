#include <u.h>
#include <libc.h>
#include <draw.h>
#include <cursor.h>
#include <event.h>

typedef struct	Slave Slave;
typedef struct	Ebuf Ebuf;

struct Slave
{
	int	pid;
	Ebuf	*head;		/* queue of messages for this descriptor */
	Ebuf	*tail;
	int	(*fn)(int, Event*, uchar*, int);
};

struct Ebuf
{
	Ebuf	*next;
	int	n;		/* number of bytes in buf */
	uchar	buf[EMAXMSG];
};

static	Slave	eslave[MAXSLAVE];
static	int	Skeyboard = -1;
static	int	Smouse = -1;
static	int	Stimer = -1;
static	int	logfid;

static	int	nslave;
static	int	parentpid;
static	int	epipe[2];

static	int	eforkslave(ulong);

/*
 * Plan2001 (drawterm's kernel, monolith/wasmapp): the slaves are kprocs,
 * not forked processes - there is no fork - each given what it needs; what
 * they write to the pipe is as 9front's.
 */
enum { Sread, Stime, Skbd };
typedef struct Sarg Sarg;
struct Sarg {
	int	what;
	int	i;
	int	fd;
	int	n;
};
static void	startslave(int, int, int, int);
static	void	extract(void);

static	int	mousefd;
static	int	cursorfd;

static
Ebuf*
ebread(Slave *s)
{
	Ebuf *eb;
 
	while((eb = s->head) == 0)
		extract();
	s->head = eb->next;
	if(s->head == 0)
		s->tail = 0;
	return eb;
}

ulong
event(Event *e)
{
	return eread(~0UL, e);
}

ulong
eread(ulong keys, Event *e)
{
	Ebuf *eb;
	int i, id;

	if(keys == 0)
		return 0;
	for(;;){
		for(i=0; i<nslave; i++)
			if((keys & (1<<i)) && eslave[i].head){
				id = 1<<i;
				if(i == Smouse)
					e->mouse = emouse();
				else if(i == Skeyboard)
					e->kbdc = ekbd();
				else if(i == Stimer)
					eslave[i].head = 0;
				else{
					eb = ebread(&eslave[i]);
					e->n = eb->n;
					if(eslave[i].fn)
						id = (*eslave[i].fn)(id, e, eb->buf, eb->n);
					else
						memmove(e->data, eb->buf, eb->n);
					free(eb);
				}
				return id;
			}
		extract();
	}
}

int
ecanmouse(void)
{
	if(Smouse < 0)
		drawerror(display, "events: mouse not initialized");
	return ecanread(Emouse);
}

int
ecankbd(void)
{
	if(Skeyboard < 0)
		drawerror(display, "events: keyboard not initialzed");
	return ecanread(Ekeyboard);
}

int
ecanread(ulong keys)
{
	Dir *d;
	int i;
	ulong l;

	for(;;){
		for(i=0; i<nslave; i++)
			if((keys & (1<<i)) && eslave[i].head)
				return 1;
		d = dirfstat(epipe[0]);
		if(d == nil)
			drawerror(display, "events: ecanread stat error");
		l = d->length;
		free(d);
		if(l == 0)
			return 0;
		extract();
	}
}

ulong
estartfn(ulong key, int fd, int n, int (*fn)(int, Event*, uchar*, int))
{
	int i;

	if(fd < 0)
		drawerror(display, "events: bad file descriptor");
	if(n <= 0 || n > EMAXMSG)
		n = EMAXMSG;
	i = eforkslave(key);
	eslave[i].fn = fn;
	startslave(Sread, i, fd, n);
	return 1<<i;
}

ulong
estart(ulong key, int fd, int n)
{
	return estartfn(key, fd, n, nil);
}

ulong
etimer(ulong key, int n)
{
	if(Stimer != -1)
		drawerror(display, "events: timer started twice");
	Stimer = eforkslave(key);
	if(n <= 0)
		n = 1000;
	startslave(Stime, Stimer, -1, n);
	return 1<<Stimer;
}

static void
ekeyslave(int fd)
{
	startslave(Skbd, eforkslave(Ekeyboard), fd, 0);
}

void
einit(ulong keys)
{
	int ctl, fd;
	char buf[256];

	parentpid = getpid();
	if(pipe(epipe) < 0)
		drawerror(display, "events: einit pipe");
	/* kprocs: no notes to kill them by; they end with their fds */
	snprint(buf, sizeof buf, "%s/mouse", display->devdir);
	mousefd = open(buf, ORDWR|OCEXEC);
	if(mousefd < 0)
		drawerror(display, "einit: can't open mouse\n");
	snprint(buf, sizeof buf, "%s/cursor", display->devdir);
	cursorfd = open(buf, ORDWR|OCEXEC);
	if(cursorfd < 0)
		drawerror(display, "einit: can't open cursor\n");
	if(keys&Ekeyboard){
		snprint(buf, sizeof buf, "%s/cons", display->devdir);
		fd = open(buf, OREAD);
		if(fd < 0)
			drawerror(display, "events: can't open console");
		snprint(buf, sizeof buf, "%s/consctl", display->devdir);
		ctl = open("/dev/consctl", OWRITE|OCEXEC);
		if(ctl < 0)
			drawerror(display, "events: can't open consctl");
		write(ctl, "rawon", 5);
		for(Skeyboard=0; Ekeyboard & ~(1<<Skeyboard); Skeyboard++)
			;
		ekeyslave(fd);
	}
	if(keys&Emouse){
		estart(Emouse, mousefd, 1+4*12);
		for(Smouse=0; Emouse & ~(1<<Smouse); Smouse++)
			;
	}
}

static void
extract(void)
{
	Slave *s;
	Ebuf *eb;
	int i, n;
	uchar ebuf[EMAXMSG+1];

	if(display->bufp > display->buf)
		flushimage(display, 1);
loop:
	if((n=read(epipe[0], ebuf, EMAXMSG+1)) < 0
	|| ebuf[0] >= MAXSLAVE)
		drawerror(display, "eof on event pipe");
	if(n == 0)
		goto loop;
	i = ebuf[0];
	if(i >= nslave || n <= 1)
		drawerror(display, "events: protocol error: short read");
	s = &eslave[i];
	if(i == Stimer){
		s->head = (Ebuf *)1;
		return;
	}
	if(i == Skeyboard && n != (1+UTFmax))
		drawerror(display, "events: protocol error: keyboard");
	if(i == Smouse){
		if(n < 1+1+2*12)
			drawerror(display, "events: protocol error: mouse");
		if(ebuf[1] == 'r')
			eresized(1);
		/* squash extraneous mouse events */
		if((eb=s->tail) && memcmp(eb->buf+1+2*12, ebuf+1+1+2*12, 12)==0){
			memmove(eb->buf, &ebuf[1], n - 1);
			return;
		}
	}
	/* try to save space by only allocating as much buffer as we need */
	eb = malloc(sizeof(*eb) - sizeof(eb->buf) + n - 1);
	if(eb == 0)
		drawerror(display, "events: protocol error 4");
	eb->n = n - 1;
	memmove(eb->buf, &ebuf[1], n - 1);
	eb->next = 0;
	if(s->head)
		s->tail->next = eb;
	else
		s->head = eb;
	s->tail = eb;
}

static int
eforkslave(ulong key)
{
	int i;

	for(i=0; i<MAXSLAVE; i++)
		if((key & ~(1<<i)) == 0 && eslave[i].pid == 0){
			if(nslave <= i)
				nslave = i + 1;
			eslave[i].pid = 1;	/* a kproc's, started by the caller */
			eslave[i].head = eslave[i].tail = 0;
			return i;
		}
	drawerror(display, "events: bad slave assignment");
	return 0;
}



Mouse
emouse(void)
{
	Mouse m;
	Ebuf *eb;
	static int lastb;
	int b;

	if(Smouse < 0)
		drawerror(display, "events: mouse not initialized");
	for(;;){
		eb = ebread(&eslave[Smouse]);
		b = atoi((char*)eb->buf+1+2*12);
		if(b != lastb || !ecanmouse())
			break;
		free(eb);	/* drop queued mouse events */
	}
	lastb = b;
	m.buttons = b;
	m.xy.x = atoi((char*)eb->buf+1+0*12);
	m.xy.y = atoi((char*)eb->buf+1+1*12);
	m.msec = (ulong)atoll((char*)eb->buf+1+3*12);
	if (logfid)
		fprint(logfid, "b: %d xy: %P\n", m.buttons, m.xy);
	free(eb);
	return m;
}

int
ekbd(void)
{
	Ebuf *eb;
	Rune r;

	if(Skeyboard < 0)
		drawerror(display, "events: keyboard not initialzed");
	eb = ebread(&eslave[Skeyboard]);
	chartorune(&r, (char*)eb->buf);
	free(eb);
	return r;
}

void
emoveto(Point pt)
{
	char buf[2*12+2];
	int n;

	n = sprint(buf, "m%d %d", pt.x, pt.y);
	write(mousefd, buf, n);
}

void
esetcursor(Cursor *c)
{
	uchar curs[2*4+2*2*16];

	if(c == 0)
		write(cursorfd, curs, 0);
	else{
		BPLONG(curs+0*4, c->offset.x);
		BPLONG(curs+1*4, c->offset.y);
		memmove(curs+2*4, c->clr, 2*2*16);
		write(cursorfd, curs, sizeof curs);
	}
}

int
ereadmouse(Mouse *m)
{
	int n;
	char buf[128];

	do{
		n = read(mousefd, buf, sizeof(buf));
		if(n < 0)	/* probably interrupted */
			return -1;
		n = eatomouse(m, buf, n);
	}while(n == 0);
	return n;
}

int
eatomouse(Mouse *m, char *buf, int n)
{
	if(n != 1+4*12){
		werrstr("eatomouse: bad count");
		return -1;
	}

	if(buf[0] == 'r')
		eresized(1);
	m->xy.x = atoi(buf+1+0*12);
	m->xy.y = atoi(buf+1+1*12);
	m->buttons = atoi(buf+1+2*12);
	m->msec = (ulong)atoll(buf+1+3*12);
	return n;
}

static void
slaveproc(void *v)
{
	Sarg a;
	char buf[EMAXMSG+1], t[1+UTFmax], kb[10];
	int k, kn, w;
	Rune r;

	a = *(Sarg*)v;
	free(v);
	switch(a.what){
	case Sread:
		buf[0] = a.i;
		while((k = read(a.fd, buf+1, a.n))>0)
			if(write(epipe[1], buf, k+1)!=k+1)
				break;
		buf[0] = MAXSLAVE;
		write(epipe[1], buf, 1);
		break;
	case Stime:
		t[0] = t[1] = a.i;
		do
			sleep(a.n);
		while(write(epipe[1], t, 2) == 2);
		break;
	case Skbd:
		/* as 9front's ekeyslave: a rune at a time, sizeof t each */
		kn = 0;
		t[0] = a.i;
		for(;;){
			while(!fullrune(kb, kn)){
				k = read(a.fd, kb+kn, sizeof kb - kn);
				if(k <= 0)
					goto out;
				kn += k;
			}
			w = chartorune(&r, kb);
			kn -= w;
			memmove(t+1, kb, w);
			memmove(kb, &kb[w], kn);
			if(write(epipe[1], t, sizeof(t)) != sizeof(t))
				break;
		}
	out:
		t[0] = MAXSLAVE;
		write(epipe[1], t, 1);
		break;
	}
}

static void
startslave(int what, int i, int fd, int n)
{
	Sarg *a;

	a = malloc(sizeof *a);
	a->what = what;
	a->i = i;
	a->fd = fd;
	a->n = n;
	kproc("eslave", slaveproc, a);
}
