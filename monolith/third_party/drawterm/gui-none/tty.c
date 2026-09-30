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

static void
kbdproc(void *v)
{
	unsigned char b[512];
	int n, i, j, need;
	Rune r;

	(void)v;
	need = 0;
	r = 0;
	while((n = read(0, b, sizeof b)) > 0){
		for(i = 0; i < n; i++){
			unsigned char c = b[i];

			if(need > 0){	/* UTF-8 continuation */
				if((c & 0xC0) == 0x80){
					r = r<<6 | (c & 0x3F);
					if(--need == 0)
						key(r);
					continue;
				}
				need = 0;
			}
			if(c >= 0xC0){
				need = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
				r = c & (0x3F >> need);
				continue;
			}
			if(c == 0x1b){
				if(i+1 < n && (b[i+1] == '[' || b[i+1] == 'O')){
					for(j = i+2; j < n && !(b[j] >= 0x40 && b[j] <= 0x7e); j++)
						;
					if(j < n){
						if((r = csi(b+i+2, j-(i+2)+1)) != 0 && ninetermkeys)
							key(r);
						i = j;
						continue;
					}
				}
				key(Kesc);
				continue;
			}
			switch(c){
			case 0x7f:	key(Kbs); break;	/* the terminal's Backspace */
			case 0x03:	key(Kdel); break;	/* ^C: Plan 9's interrupt */
			case '\r':	key('\n'); break;
			default:	key(c); break;
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
