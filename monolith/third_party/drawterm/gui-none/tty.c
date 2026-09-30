/*
 * 9pterm's interactive keyboard: the local terminal raw and its keys
 * as Plan 9 runes into the kernel's keyboard (devkbd), which is the
 * CPU server's /mnt/term/dev/kbd; kbdfs there edits lines, echoes and
 * turns Delete into an interrupt, as with drawterm's screen.  Only
 * system headers here: drawterm's rename read, write and atexit.
 */
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <poll.h>

typedef unsigned int Rune;

extern void	kbdkey(Rune, int);
extern void*	kproc(char*, void(*)(void*), void*);

enum {
	KF	= 0xF000,
	Khome	= KF|0x0D,
	Kup	= KF|0x0E,
	Kpgup	= KF|0x0F,
	Kleft	= KF|0x11,
	Kright	= KF|0x12,
	Kpgdown	= KF|0x13,
	Kins	= KF|0x14,
	Kend	= KF|0x18,
	Kdown	= 0x80,
	Kbs	= 0x08,
	Kdel	= 0x7f,
	Kesc	= 0x1b,
};

int	ninetermkbd;	/* the keyboard is ours: devcons reads no fd 0 (devcons.c) */
int	ninetermkeys;	/* -K: send cursor keys too - for a kbdfs with a line
			   editor (Plan2001's); 9front's puts them in the line */

static struct termios saved;
static int rawset;

static void
restore(void)
{
	if(rawset){
		tcsetattr(0, TCSAFLUSH, &saved);
		rawset = 0;
	}
}

static void
onsig(int s)
{
	restore();
	signal(s, SIG_DFL);
	raise(s);
}

static void
key(Rune r)
{
	kbdkey(r, 1);
	kbdkey(r, 0);
}

/* ESC [ ... final: the key, 0 for none */
static Rune
csi(unsigned char *p, int n)
{
	int num;

	num = 0;
	for(; n > 1 && p[0] >= '0' && p[0] <= '9'; p++, n--)
		num = num*10 + p[0]-'0';
	switch(p[0]){
	case 'A':	return Kup;
	case 'B':	return Kdown;
	case 'C':	return Kright;
	case 'D':	return Kleft;
	case 'H':	return Khome;
	case 'F':	return Kend;
	case '~':
		switch(num){
		case 1: case 7:	return Khome;
		case 4: case 8:	return Kend;
		case 2:	return Kins;
		case 5:	return Kpgup;
		case 6:	return Kpgdown;
		}
	}
	return 0;
}

/*
 * The input's state across reads: a UTF-8 sequence, or an escape
 * sequence (ESC, then ESC [ or ESC O and its bytes up to the final one)
 * may be split between reads.
 */
static Rune	ur;		/* UTF-8: the rune so far */
static int	uneed;		/* its continuation bytes to come */
static int	esc;		/* 0; 1: after ESC; 2: in ESC [ or ESC O */
static unsigned char	seq[16];	/* the escape sequence's bytes after ESC [ */
static int	nseq;

static void
plain(unsigned char c)
{
	switch(c){
	case 0x7f:	key(Kbs); break;	/* the terminal's Backspace */
	case 0x03:	key(Kdel); break;	/* ^C: Plan 9's interrupt */
	case '\r':	key('\n'); break;
	default:	key(c); break;
	}
}

static void
feed(unsigned char c)
{
	Rune r;

	if(esc == 1){
		if(c == '[' || c == 'O'){
			esc = 2;
			nseq = 0;
			return;
		}
		esc = 0;
		key(Kesc);	/* ESC then something else: both */
	}else if(esc == 2){
		if(nseq < (int)sizeof seq)
			seq[nseq++] = c;
		if(c >= 0x40 && c <= 0x7e){	/* the final byte */
			esc = 0;
			if((r = csi(seq, nseq)) != 0 && ninetermkeys)
				key(r);
		}else if(nseq == (int)sizeof seq)
			esc = 0;	/* not a sequence we know: dropped */
		return;
	}
	if(uneed > 0){
		if((c & 0xC0) == 0x80){
			ur = ur<<6 | (c & 0x3F);
			if(--uneed == 0)
				key(ur < 0x80 || ur > 0x10FFFF ? 0xFFFD : ur);
			return;
		}
		uneed = 0;
		key(0xFFFD);	/* cut short: the byte starts anew */
	}
	if(c == 0x1b){
		esc = 1;
		return;
	}
	if(c < 0x80){
		plain(c);
		return;
	}
	if(c < 0xC2 || c > 0xF4){	/* a stray continuation, an overlong or no start */
		key(0xFFFD);
		return;
	}
	uneed = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
	ur = c & (0x3F >> uneed);
}

static void
kbdproc(void *v)
{
	unsigned char b[512];
	struct pollfd pf;
	int n, i;

	(void)v;
	while((n = read(0, b, sizeof b)) > 0){
		for(i = 0; i < n; i++)
			feed(b[i]);
		/* ESC alone at the end: Esc, unless the rest of a sequence follows at once */
		if(esc == 1){
			pf.fd = 0;
			pf.events = POLLIN;
			if(poll(&pf, 1, 50) <= 0){
				esc = 0;
				key(Kesc);
			}
		}
	}
	key(0x04);	/* the terminal went away: end of file */
}

/* for an interactive session (cpu.c): the terminal raw, the keyboard ours */
void
ninetermkbdstart(void)
{
	struct termios t;

	if(!isatty(0) || tcgetattr(0, &saved) < 0)
		return;
	t = saved;
	cfmakeraw(&t);
	t.c_oflag = saved.c_oflag;	/* output as before: \n is \r\n */
	if(tcsetattr(0, TCSAFLUSH, &t) < 0)
		return;
	rawset = 1;
	atexit(restore);
	signal(SIGTERM, onsig);
	signal(SIGHUP, onsig);
	signal(SIGQUIT, onsig);
	ninetermkbd = 1;
	kproc("kbd", kbdproc, 0);
}
