
#line	2	"/sys/src/games/mix/mixal.y"
#include <u.h>
#include <libc.h>
#include <avl.h>
#include <bio.h>
#include "mix.h"

#line	9	"/sys/src/games/mix/mixal.y"
typedef union  {
	Sym *sym;
	long lval;
	u32int mval;
	Rune *rbuf;
} YYSTYPE;
extern	int	yyerrflag;
#ifndef	YYMAXDEPTH
#define	YYMAXDEPTH	150
#endif
YYSTYPE	yylval;
YYSTYPE	yyval;
#define	LSYMDEF	57346
#define	LSYMREF	57347
#define	LOP	57348
#define	LEQU	57349
#define	LORIG	57350
#define	LCON	57351
#define	LALF	57352
#define	LEND	57353
#define	LBACK	57354
#define	LHERE	57355
#define	LFORW	57356
#define	LNUM	57357
#define	LSTR	57358
#define	LSS	57359
#define YYEOFCODE 1
#define YYERRCODE 2

#line	209	"/sys/src/games/mix/mixal.y"


int back[10];
Sym forw[10];

void
defrefs(Sym *sym, long apart)
{
	u32int inst, mval;
	int *ref, *ep;

	ep = sym->refs + sym->i;
	for(ref = sym->refs; ref < ep; ref++) {
		inst = cells[*ref];
		inst &= ~(MASK2 << BITS*3);
		if(apart < 0) {
			mval = -apart;
			inst |= SIGNB;
		} else
			mval = apart;
		inst |= (mval&MASK2) << BITS*3;
		cells[*ref] = inst;
	}
}

void
defloc(Sym *sym, long val)
{
	if(sym == nil)
		return;
	defrefs(sym, val);
	free(sym->refs);
	sym->lex = LSYMDEF;
	sym->mval = val < 0 ? -val|SIGNB : val;
}

void
addref(Sym *ref, long star)
{
	if(ref->refs == nil || ref->i == ref->max) {
		ref->max = ref->max == 0 ? 3 : ref->max*2;
		ref->refs = erealloc(ref->refs, ref->max * sizeof(int));
	}
	ref->refs[ref->i++] = star;
}

static void
asm(Sym *op, long apart, long ipart, long fpart)
{
	u32int inst, mval;

	inst = op->opc & MASK1;

	if(fpart == -1)
		inst |= (op->f&MASK1) << BITS;
	else
		inst |= (fpart&MASK1) << BITS;

	inst |= (ipart&MASK1) << BITS*2;

	if(apart < 0) {
		mval = -apart;
		inst |= SIGNB;
	} else
		mval = apart;
	inst |= (mval&MASK2) << BITS*3;

	cells[star++] = inst;
}

void
refasm(Sym *op, long ipart, long fpart)
{
	u32int inst;

	inst = op->opc & MASK1;

	if(fpart == -1)
		inst |= (op->f&MASK1) << BITS;
	else
		inst |= (fpart&MASK1) << BITS;

	inst |= (ipart&MASK1) << BITS*2;

	cells[star++] = inst;
}

Sym*
con(u32int exp)
{
	Con *c;
	static int i;
	static char buf[20];

	seprint(buf, buf+20, "con%d\n", i++);
	c = emalloc(sizeof(*c));
	c->sym = sym(buf);
	c->exp = exp;
	c->link = cons;
	cons = c;
	return c->sym;
}

void
alf(int loc, Rune *b)
{
	u32int w;
	int m;
	Rune *r, *e;

	w = 0;
	e = b + 5;
	for(r = b; r < e; r++) {
		if((m = runetomix(*r)) == -1)
			yyerror("Bad mixchar %C\n", *r);
		w |= m;
		if(r+1 < e)
			w <<= BITS;
	}
	cells[loc] = w;
}

void
endprog(int start)
{
	Con *c, *link;
	for(c = cons; c != nil; c = link) {
		defloc(c->sym, star);
		cells[star++] = c->exp;
		link = c->link;
		free(c);
	}
	cons = nil;
	vmstart = start;
	yydone = 1;
}

u32int
wval(u32int old, int exp, int f)
{
	if(f == -1) {
		if(exp < 0)
			return -exp | SIGNB;
		else
			return exp;
	}

	if(exp < 0)
		return mixst(old, -exp&MASK5 | SIGNB, f);
	return mixst(old, exp & MASK5, f);
}
short	yyexca[] =
{-1, 1,
	1, -1,
	-2, 11,
};
#define	YYNPROD	40
#define	YYPRIVATE 57344
#define	YYLAST	104
short	yyact[] =
{
  49,  48,  29,   5,  50,   5,  21,  64,  48,   3,
   6,  16,  38,  39,  40,  41,  42,  43,   7,  36,
  33,  70,  28,  38,  39,  40,  41,  42,  43,  45,
  46,  50,   5,   2,  31,   1,  55,  47,  57,  56,
  51,  52,  44,  53,  54,  58,  59,  60,  61,  62,
  63,  65,  15,  66,  38,  39,  40,  41,  42,  43,
  25,  18,  35,   4,  14,  67,  69,  68,  17,   0,
  20,  24,  25,  22,  23,  26,   0,  25,  37,   0,
  19,   0,   0,  24,   0,  22,  23,  26,  24,  32,
   0,   0,  26,   8,   9,  10,  11,  12,  13,  27,
  30,   0,   0,  34
};
short	yypact[] =
{
-1000,   5,-1000,-1000,  87,-1000,-1000,-1000,  56,  68,
  68,  68,   4,  68,  -4,  -4,  37,-1000,-1000,  68,
-1000,-1000,  73,  73,-1000,-1000,-1000, -22,-1000,   6,
 -22, -24, -15, -24, -22, -21,  68, -21,  73,  73,
  73,  73,  73,  73, -17,-1000,-1000,-1000,  68,-1000,
  68,-1000,-1000,-1000,-1000, -24,  37, -24,-1000,-1000,
-1000,-1000,-1000,-1000,-1000,   6,  -5,-1000,-1000,-1000,
-1000
};
short	yypgo[] =
{
   0,  89,  64,   2,   6,   0,  62,  22,  63,  52,
  35,  33,   9
};
short	yyr1[] =
{
   0,  10,  10,  11,  11,  11,  11,  11,  11,  11,
  11,   8,   8,   8,   2,   2,   2,   9,   9,   9,
   6,   6,   5,   5,   3,   3,   3,   3,   3,   3,
   3,   3,   3,   4,   4,   4,   1,   7,   7,  12
};
short	yyr2[] =
{
   0,   0,   2,   1,   6,   6,   4,   4,   4,   4,
   4,   0,   1,   1,   0,   1,   1,   1,   3,   1,
   0,   2,   0,   3,   1,   2,   2,   3,   3,   3,
   3,   3,   3,   1,   1,   1,   1,   2,   4,   1
};
short	yychk[] =
{
-1000, -10, -11, -12,  -8,  27,   5,  13,   6,   7,
   8,   9,  10,  11,  -2,  -9,  -3,  12,   5,  24,
  14,  -4,  17,  18,  15,   4,  19,  -1,  -7,  -3,
  -1,  -7,  -1,  16,  -1,  -6,  23,  -6,  17,  18,
  19,  20,  21,  22,  -7,  -4,  -4, -12,  23,  -5,
  25, -12, -12, -12, -12,  -5,  -3,  -5,  -4,  -4,
  -4,  -4,  -4,  -4,  24,  -3,  -3, -12, -12,  -5,
  26
};
short	yydef[] =
{
   1,  -2,   2,   3,   0,  39,  12,  13,  14,   0,
   0,   0,   0,   0,  20,  20,  15,  16,  17,   0,
  19,  24,   0,   0,  33,  34,  35,   0,  36,  22,
   0,  36,   0,   0,   0,  22,   0,  22,   0,   0,
   0,   0,   0,   0,  36,  25,  26,   6,   0,  37,
   0,   7,   8,   9,  10,   0,  21,   0,  27,  28,
  29,  30,  31,  32,  18,  22,   0,   4,   5,  38,
  23
};
short	yytok1[] =
{
   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,
  27,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
  25,  26,  19,  17,  23,  18,   0,  20,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,  22,   0,
   0,  24
};
short	yytok2[] =
{
   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,
  12,  13,  14,  15,  16,  21
};
long	yytok3[] =
{
   0
};

#line	1	"/sys/lib/yaccpar"
#define YYFLAG 		-1000
#define	yyclearin	yychar = -1
#define	yyerrok		yyerrflag = 0

#ifdef	yydebug
#include	"y.debug"

char*
yytokname(int yyc)
{
	static char x[16];

	if(yyc > 0 && yyc <= sizeof(yytoknames)/sizeof(yytoknames[0]))
	if(yytoknames[yyc-1])
		return yytoknames[yyc-1];
	sprint(x, "<%d>", yyc);
	return x;
}

char*
yystatname(int yys)
{
	static char x[16];

	if(yys >= 0 && yys < sizeof(yystates)/sizeof(yystates[0]))
	if(yystates[yys])
		return yystates[yys];
	sprint(x, "<%d>\n", yys);
	return x;
}
#else
#define	yydebug		0
#define yytokname(x)	""
#define yystatname(x)	""
#endif

/*	parser for yacc output	*/

int	yynerrs = 0;		/* number of errors */
int	yyerrflag = 0;		/* error recovery flag */


long
yylex1(void)
{
	long yychar;
	long *t3p;
	int c;

	yychar = yylex();
	if(yychar <= 0) {
		c = yytok1[0];
		goto out;
	}
	if(yychar < sizeof(yytok1)/sizeof(yytok1[0])) {
		c = yytok1[yychar];
		goto out;
	}
	if(yychar >= YYPRIVATE)
		if(yychar < YYPRIVATE+sizeof(yytok2)/sizeof(yytok2[0])) {
			c = yytok2[yychar-YYPRIVATE];
			goto out;
		}
	for(t3p=yytok3;; t3p+=2) {
		c = t3p[0];
		if(c == yychar) {
			c = t3p[1];
			goto out;
		}
		if(c == 0)
			break;
	}
	c = 0;

out:
	if(c == 0)
		c = yytok2[1];	/* unknown char */
	if(yydebug >= 3)
		fprint(2, "lex %.4lux %s\n", yychar, yytokname(c));
	return c;
}

int
yyparse(void)
{
	struct
	{
		YYSTYPE	yyv;
		int	yys;
	} yys[YYMAXDEPTH], *yyp, *yypt;
	short *yyxi;
	int yyj, yym, yystate, yyn, yyg;
	long yychar;
	YYSTYPE save1, save2;
	int save3, save4;

	save1 = yylval;
	save2 = yyval;
	save3 = yynerrs;
	save4 = yyerrflag;

	yystate = 0;
	yychar = -1;
	yynerrs = 0;
	yyerrflag = 0;
	yyp = &yys[-1];
	goto yystack;

ret0:
	yyn = 0;
	goto ret;

ret1:
	yyn = 1;
	goto ret;

ret:
	yylval = save1;
	yyval = save2;
	yynerrs = save3;
	yyerrflag = save4;
	return yyn;

yystack:
	/* put a state and value onto the stack */
	if(yydebug >= 4)
		fprint(2, "char %s in %s", yytokname(yychar), yystatname(yystate));

	yyp++;
	if(yyp >= &yys[YYMAXDEPTH]) {
		yyerror("yacc stack overflow");
		goto ret1;
	}
	yyp->yys = yystate;
	yyp->yyv = yyval;

yynewstate:
	yyn = yypact[yystate];
	if(yyn <= YYFLAG)
		goto yydefault; /* simple state */
	if(yychar < 0)
		yychar = yylex1();
	yyn += yychar;
	if(yyn < 0 || yyn >= YYLAST)
		goto yydefault;
	yyn = yyact[yyn];
	if(yychk[yyn] == yychar) { /* valid shift */
		yychar = -1;
		yyval = yylval;
		yystate = yyn;
		if(yyerrflag > 0)
			yyerrflag--;
		goto yystack;
	}

yydefault:
	/* default state action */
	yyn = yydef[yystate];
	if(yyn == -2) {
		if(yychar < 0)
			yychar = yylex1();

		/* look through exception table */
		for(yyxi=yyexca;; yyxi+=2)
			if(yyxi[0] == -1 && yyxi[1] == yystate)
				break;
		for(yyxi += 2;; yyxi += 2) {
			yyn = yyxi[0];
			if(yyn < 0 || yyn == yychar)
				break;
		}
		yyn = yyxi[1];
		if(yyn < 0)
			goto ret0;
	}
	if(yyn == 0) {
		/* error ... attempt to resume parsing */
		switch(yyerrflag) {
		case 0:   /* brand new error */
			yyerror("syntax error");
			yynerrs++;
			if(yydebug >= 1) {
				fprint(2, "%s", yystatname(yystate));
				fprint(2, "saw %s\n", yytokname(yychar));
			}

		case 1:
		case 2: /* incompletely recovered error ... try again */
			yyerrflag = 3;

			/* find a state where "error" is a legal shift action */
			while(yyp >= yys) {
				yyn = yypact[yyp->yys] + YYERRCODE;
				if(yyn >= 0 && yyn < YYLAST) {
					yystate = yyact[yyn];  /* simulate a shift of "error" */
					if(yychk[yystate] == YYERRCODE)
						goto yystack;
				}

				/* the current yyp has no shift onn "error", pop stack */
				if(yydebug >= 2)
					fprint(2, "error recovery pops state %d, uncovers %d\n",
						yyp->yys, (yyp-1)->yys );
				yyp--;
			}
			/* there is no state on the stack with an error shift ... abort */
			goto ret1;

		case 3:  /* no shift yet; clobber input char */
			if(yydebug >= 2)
				fprint(2, "error recovery discards %s\n", yytokname(yychar));
			if(yychar == YYEOFCODE)
				goto ret1;
			yychar = -1;
			goto yynewstate;   /* try again in the same state */
		}
	}

	/* reduction by production yyn */
	if(yydebug >= 2)
		fprint(2, "reduce %d in:\n\t%s", yyn, yystatname(yystate));

	yypt = yyp;
	yyp -= yyr2[yyn];
	yyval = (yyp+1)->yyv;
	yym = yyn;

	/* consult goto table to find next state */
	yyn = yyr1[yyn];
	yyg = yypgo[yyn];
	yyj = yyg + yyp->yys + 1;

	if(yyj >= YYLAST || yychk[yystate=yyact[yyj]] != -yyn)
		yystate = yyact[yyg];
	switch(yym) {
		
case 4:
#line	35	"/sys/src/games/mix/mixal.y"
{
		defloc(yypt[-5].yyv.sym, star);
		asm(yypt[-4].yyv.sym, yypt[-3].yyv.lval, yypt[-2].yyv.lval, yypt[-1].yyv.lval);
	} break;
case 5:
#line	40	"/sys/src/games/mix/mixal.y"
{
		defloc(yypt[-5].yyv.sym, star);
		addref(yypt[-3].yyv.sym, star);
		refasm(yypt[-4].yyv.sym, yypt[-2].yyv.lval, yypt[-1].yyv.lval);
	} break;
case 6:
#line	46	"/sys/src/games/mix/mixal.y"
{
		defloc(yypt[-3].yyv.sym, yypt[-1].yyv.lval);
	} break;
case 7:
#line	50	"/sys/src/games/mix/mixal.y"
{
		defloc(yypt[-3].yyv.sym, star);
		star = yypt[-1].yyv.lval;
	} break;
case 8:
#line	55	"/sys/src/games/mix/mixal.y"
{
		defloc(yypt[-3].yyv.sym, star);
		cells[star++] = yypt[-1].yyv.mval;
	} break;
case 9:
#line	60	"/sys/src/games/mix/mixal.y"
{
		defloc(yypt[-3].yyv.sym, star);
		alf(star++, yypt[-1].yyv.rbuf);
	} break;
case 10:
#line	65	"/sys/src/games/mix/mixal.y"
{
		endprog(yypt[-1].yyv.lval);
		defloc(yypt[-3].yyv.sym, star);
	} break;
case 11:
#line	71	"/sys/src/games/mix/mixal.y"
{
		yyval.sym = nil;
	} break;
case 12:
#line	75	"/sys/src/games/mix/mixal.y"
{
		yyval.sym = yypt[-0].yyv.sym;
	} break;
case 13:
#line	79	"/sys/src/games/mix/mixal.y"
{
		Sym *f;
		int l;

		l = (yypt[-0].yyv.sym)->opc;
		back[l] = star;
		f = forw + l;
		defloc(f, star);
		f->lex = LSYMREF;
		f->refs = nil;
		f->i = f->max = 0;
		yyval.sym = nil;
	} break;
case 14:
#line	94	"/sys/src/games/mix/mixal.y"
{
		yyval.lval = 0;
	} break;
case 16:
#line	99	"/sys/src/games/mix/mixal.y"
{
		yyval.lval = back[(yypt[-0].yyv.sym)->opc];
	} break;
case 18:
#line	106	"/sys/src/games/mix/mixal.y"
{
		yyval.sym = con(yypt[-1].yyv.mval);
	} break;
case 19:
#line	110	"/sys/src/games/mix/mixal.y"
{
		yyval.sym = forw + (yypt[-0].yyv.sym)->opc;
	} break;
case 20:
#line	115	"/sys/src/games/mix/mixal.y"
{
		yyval.lval = 0;
	} break;
case 21:
#line	119	"/sys/src/games/mix/mixal.y"
{
		yyval.lval = yypt[-0].yyv.lval;
	} break;
case 22:
#line	124	"/sys/src/games/mix/mixal.y"
{
		yyval.lval = -1;
	} break;
case 23:
#line	128	"/sys/src/games/mix/mixal.y"
{
		if(yypt[-1].yyv.lval< 0)
			yyerror("invalid fpart %d\n", yypt[-1].yyv.lval);
		yyval.lval = yypt[-1].yyv.lval;
	} break;
case 25:
#line	137	"/sys/src/games/mix/mixal.y"
{
		yyval.lval = yypt[-0].yyv.lval;
	} break;
case 26:
#line	141	"/sys/src/games/mix/mixal.y"
{
		yyval.lval = -yypt[-0].yyv.lval;
	} break;
case 27:
#line	145	"/sys/src/games/mix/mixal.y"
{
		yyval.lval = yypt[-2].yyv.lval+ yypt[-0].yyv.lval;
	} break;
case 28:
#line	149	"/sys/src/games/mix/mixal.y"
{
		yyval.lval = yypt[-2].yyv.lval- yypt[-0].yyv.lval;
	} break;
case 29:
#line	153	"/sys/src/games/mix/mixal.y"
{
		yyval.lval = yypt[-2].yyv.lval* yypt[-0].yyv.lval;
	} break;
case 30:
#line	157	"/sys/src/games/mix/mixal.y"
{
		yyval.lval = (yypt[-2].yyv.lval) / yypt[-0].yyv.lval;
	} break;
case 31:
#line	161	"/sys/src/games/mix/mixal.y"
{
		yyval.lval = (((vlong)yypt[-2].yyv.lval) << 30) / yypt[-0].yyv.lval;
	} break;
case 32:
#line	165	"/sys/src/games/mix/mixal.y"
{
		yyval.lval = F(yypt[-2].yyv.lval, yypt[-0].yyv.lval);
	} break;
case 34:
#line	172	"/sys/src/games/mix/mixal.y"
{
		u32int mval;

		mval = (yypt[-0].yyv.sym)->mval;
		if(mval & SIGNB) {
			mval &= ~SIGNB;
			yyval.lval = -((long)mval);
		} else
			yyval.lval = mval;
	} break;
case 35:
#line	183	"/sys/src/games/mix/mixal.y"
{
		yyval.lval = star;
	} break;
case 36:
#line	189	"/sys/src/games/mix/mixal.y"
{
		if(yypt[-0].yyv.mval& SIGNB)
			yyval.lval = -(long)(yypt[-0].yyv.mval& MASK5);
		else
			yyval.lval = yypt[-0].yyv.mval;
	} break;
case 37:
#line	198	"/sys/src/games/mix/mixal.y"
{
		yyval.mval = wval(0, yypt[-1].yyv.lval, yypt[-0].yyv.lval);
	} break;
case 38:
#line	202	"/sys/src/games/mix/mixal.y"
{
		yyval.mval = wval(yypt[-3].yyv.lval, yypt[-1].yyv.lval, yypt[-0].yyv.lval);
	} break;
	}
	goto yystack;  /* stack new state and value */
}
