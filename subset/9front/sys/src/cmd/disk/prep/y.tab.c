
#line	2	"/sys/src/cmd/disk/prep/calc.y"
typedef struct Exp Exp;
enum {
	NUM,
	DOT,
	DOLLAR,
	ADD,
	SUB,
	MUL,
	DIV,
	FRAC,
	NEG,
};

struct Exp {
	int ty;
	long long n;
	Exp *e1;
	Exp *e2;
};

typedef Exp* Expptr;
#define YYSTYPE Expptr
Exp *yyexp;
extern	int	yyerrflag;
#ifndef	YYMAXDEPTH
#define	YYMAXDEPTH	150
#endif
#ifndef	YYSTYPE
#define	YYSTYPE	int
#endif
YYSTYPE	yylval;
YYSTYPE	yyval;
#define	NUMBER	57346
#define	UNARYMINUS	57347
#define YYEOFCODE 1
#define YYERRCODE 2

#line	47	"/sys/src/cmd/disk/prep/calc.y"


#include <u.h>
#include <libc.h>
#include <ctype.h>
#include "disk.h"
#include "edit.h"

static Exp*
mkNUM(vlong x)
{
	Exp *n;

	n = emalloc(sizeof *n);

	n->ty = NUM;
	n->n = x;
	return n;
}

static Exp*
mkOP(int ty, Exp *e1, Exp *e2)
{
	Exp *n;

	n = emalloc(sizeof *n);
	n->ty = ty;
	n->e1 = e1;
	n->e2 = e2;

	return n;
}

static char *inp;
static jmp_buf jmp;
static vlong dot, size, dollar, unit;
static char** errp;

static int
yylex(void)
{
	int c;
	uvlong n;

	while(isspace(*inp))
		inp++;

	if(*inp == 0)
		return 0;

	if(isdigit(*inp)) {
		n = strtoull(inp, &inp, 0);	/* default unit is sectors */
		c = *inp++;
		if(isascii(c) && isupper(c))
			c = tolower(c);
		switch(c) {
		case 't':
			n *= 1024;
			/* fall through */
		case 'g':
			n *= 1024;
			/* fall through */
		case 'm':
			n *= 1024;
			/* fall through */
		case 'k':
			n *= 1024;
			n /= unit;		/* convert to sectors */
			break;
		default:
			--inp;
			break;
		}
		yylval = mkNUM(n);
		return NUMBER;
	}
	return *inp++;
}

static void
yyerror(char *s)
{
	*errp = s;
	longjmp(jmp, 1);
}

static vlong
eval(Exp *e)
{
	vlong i;

	switch(e->ty) {
	case NUM:
		return e->n;
	case DOT:
		return dot;
	case DOLLAR:
		return dollar;
	case ADD:
		return eval(e->e1)+eval(e->e2);
	case SUB:
		return eval(e->e1)-eval(e->e2);
	case MUL:
		return eval(e->e1)*eval(e->e2);
	case DIV:
		i = eval(e->e2);
		if(i == 0)
			yyerror("division by zero");
		return eval(e->e1)/i;
	case FRAC:
		return (size*eval(e->e1))/100;
	case NEG:
		return -eval(e->e1);
	}
	assert(0);
	return 0;
}

int yyparse(void);

char*
parseexpr(char *s, vlong xdot, vlong xdollar, vlong xsize, vlong xunit, vlong *result)
{
	char *err;

	errp = &err;
	if(setjmp(jmp))
		return err;

	inp = s;
	dot = xdot;
	size = xsize;
	dollar = xdollar;
	unit = xunit;
	yyparse();
	if(yyexp == nil)
		return "nil yylval?";
	*result = eval(yyexp);
	return nil;
}

#ifdef TEST
void
main(int argc, char **argv)
{
	int i;
	vlong r;
	char *e;

	for(i=1; i<argc; i++)
		if(e = parseexpr(argv[i], 1000, 1000000, 1000000, &r))
			print("%s\n", e);
		else
			print("%lld\n", r);
}
#endif
short	yyexca[] =
{-1, 1,
	1, -1,
	-2, 0,
};
#define	YYNPROD	12
#define	YYPRIVATE 57344
#define	YYLAST	30
short	yyact[] =
{
   8,   9,  10,  11,   3,  12,   7,   2,  12,  19,
   1,   4,   5,   6,  13,  14,  15,  16,  17,  18,
   8,   9,  10,  11,   0,  12,  10,  11,   0,  12
};
short	yypact[] =
{
   0,-1000,  15,-1000,-1000,-1000,   0,   0,   0,   0,
   0,   0,-1000,  -5,-1000,  19,  19,  -2,  -2,-1000
};
short	yypgo[] =
{
   0,  10,   7
};
short	yyr1[] =
{
   0,   1,   2,   2,   2,   2,   2,   2,   2,   2,
   2,   2
};
short	yyr2[] =
{
   0,   1,   1,   1,   1,   3,   3,   3,   3,   3,
   2,   2
};
short	yychk[] =
{
-1000,  -1,  -2,   4,  11,  12,  13,   6,   5,   6,
   7,   8,  10,  -2,  -2,  -2,  -2,  -2,  -2,  14
};
short	yydef[] =
{
   0,  -2,   1,   2,   3,   4,   0,   0,   0,   0,
   0,   0,  10,   0,  11,   6,   7,   8,   9,   5
};
short	yytok1[] =
{
   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,  12,  10,   0,   0,
  13,  14,   7,   5,   0,   6,  11,   8
};
short	yytok2[] =
{
   2,   3,   4,   9
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
		
case 1:
#line	33	"/sys/src/cmd/disk/prep/calc.y"
{ yyexp = yypt[-0].yyv; return 0; } break;
case 3:
#line	36	"/sys/src/cmd/disk/prep/calc.y"
{ yyval = mkOP(DOT, nil, nil); } break;
case 4:
#line	37	"/sys/src/cmd/disk/prep/calc.y"
{ yyval = mkOP(DOLLAR, nil, nil); } break;
case 5:
#line	38	"/sys/src/cmd/disk/prep/calc.y"
{ yyval = yypt[-1].yyv; } break;
case 6:
#line	39	"/sys/src/cmd/disk/prep/calc.y"
{ yyval = mkOP(ADD, yypt[-2].yyv, yypt[-0].yyv); } break;
case 7:
#line	40	"/sys/src/cmd/disk/prep/calc.y"
{ yyval = mkOP(SUB, yypt[-2].yyv, yypt[-0].yyv); } break;
case 8:
#line	41	"/sys/src/cmd/disk/prep/calc.y"
{ yyval = mkOP(MUL, yypt[-2].yyv, yypt[-0].yyv); } break;
case 9:
#line	42	"/sys/src/cmd/disk/prep/calc.y"
{ yyval = mkOP(DIV, yypt[-2].yyv, yypt[-0].yyv); } break;
case 10:
#line	43	"/sys/src/cmd/disk/prep/calc.y"
{ yyval = mkOP(FRAC, yypt[-1].yyv, nil); } break;
case 11:
#line	44	"/sys/src/cmd/disk/prep/calc.y"
{ yyval = mkOP(NEG, yypt[-0].yyv, nil); } break;
	}
	goto yystack;  /* stack new state and value */
}
