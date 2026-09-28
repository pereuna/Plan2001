#define	FOR	57346
#define	IN	57347
#define	WHILE	57348
#define	IF	57349
#define	NOT	57350
#define	TWIDDLE	57351
#define	BANG	57352
#define	SUBSHELL	57353
#define	SWITCH	57354
#define	FN	57355
#define	WORD	57356
#define	REDIR	57357
#define	DUP	57358
#define	PIPE	57359
#define	SUB	57360
#define	SIMPLE	57361
#define	ARGLIST	57362
#define	WORDS	57363
#define	BRACE	57364
#define	PAREN	57365
#define	PCMD	57366
#define	PIPEFD	57367
#define	ANDAND	57368
#define	OROR	57369
#define	COUNT	57370

#line	13	"/sys/src/cmd/rc/syn.y"
#include "rc.h"
#include "fns.h"

#line	16	"/sys/src/cmd/rc/syn.y"
typedef union {
	struct tree *tree;
} YYSTYPE;
extern	int	yyerrflag;
#ifndef	YYMAXDEPTH
#define	YYMAXDEPTH	150
#endif
YYSTYPE	yylval;
YYSTYPE	yyval;
#define YYEOFCODE 1
#define YYERRCODE 2
short	yyexca[] =
{-1, 0,
	1, 1,
	-2, 18,
-1, 1,
	1, -1,
	-2, 0,
};
#define	YYNPROD	72
#define	YYPRIVATE 57344
#define	YYLAST	331
short	yyact[] =
{
  45,  21,  64,  65,   5,  21,  66,   3,  40,  41,
  93,   3,  39,  21,  21,  21,  21,  70,  21,  60,
  61,  62,  63,  35,  72,  92,  17,  69,  28,  89,
  78,  76,  21,  21,  21,  43,  57,  59,  79,  80,
  81,  21,  40,  33,  85,  71,  89,  33,  73,  74,
  75, 103,  77,  17,  98, 104,  88,  31,  32,  87,
  82,  72,  90,  71,  29,  30,  38,  86,  21,  91,
  33,  95,  12,  37,  20,  42, 105, 111,  36,  89,
  31,  32,  99,  58,  21,  21, 115,  33,  21,  84,
 101, 102,  83, 107, 106,  96,  97,  31,  32,   1,
  44, 109,  18,  94,  29,  30,   2, 110,  10,  36,
  13,  34,  21,  67,   0, 108,  21,   0, 114,   0,
   0,   0, 116,  46,  47,  48,  49,  50,  51,  52,
  53,  54,  55,  25,  56,  68,   4,   0,   0,   0,
   4,   0,   0,   0,   0, 113,   0,   0,   0,  22,
  24,  23,   0,   0,   0,   0,   0,  27,   0,  26,
  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,
  25,  56,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0, 112,   0,   0,   0,  22,  24,  23,   0,
   0,   0,   0,   0,  27,   0,  26,  46,  47,  48,
  49,  50,  51,  52,  53,  54,  55,  25,  56,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0, 100,
   0,   0,   0,  22,  24,  23,   0,   0,   0,   0,
   0,  27,   0,  26,  46,  47,  48,  49,  50,  51,
  52,  53,  54,  55,  25,  56,  46,  47,  48,  49,
  50,  51,  52,  53,  54,  55,  25,  19,  20,   0,
  22,  24,  23,   0,   0,   0,  17,   0,  27,   0,
  26,   0,  22,  24,  23,   0,   0,   0,   0,   0,
  27,   7,  26,   8,   6,   0,  11,  14,  15,   9,
  16,  25,  19,  20,  46,  47,  48,  49,  50,  51,
  52,  53,  54,  55,  25,  56,   0,  22,  24,  23,
   0,   0,   0,  17,   0,  27,   0,  26,   0,   0,
  22,  24,  23,   0,   0,   0,   0,   0,  27,   0,
  26
};
short	yypact[] =
{
 277,-1000,  -5,  30, 277,  58,   4, -29, -30, 290,
 242, 290, 277, 277, 277, 277,-1000, 277, -12, 230,
-1000,-1000, 290, 290, 290,-1000, 230,-1000,-1000,-1000,
-1000, 277, 277, 277,-1000,-1000,  58, 290,-1000,-1000,
 277, 290,-1000,   0,-1000,-1000,-1000,-1000,-1000,-1000,
-1000,-1000,-1000,-1000,-1000,-1000, -10,   0,-1000,   0,
  26,  26,  26,  26, 230, -27,  70, 277,-1000, 290,
 290,   0,-1000,  36,-1000,-1000,-1000,  17, 193,  26,
  26,-1000,-1000, 277, 277,  25,  50, 277, -10, 290,
 290,-1000,   0,-1000,-1000,-1000,   0,-1000,-1000,-1000,
-1000,  53,  53,-1000,-1000,-1000,  53,-1000,-1000, 156,
 119, 277,-1000,-1000,  53, 277,  53
};
short	yypgo[] =
{
   0, 106,  66,   4,   3, 135, 113, 110,  23,  72,
   6, 108, 102,  25,   0, 100,   2,  99,  92,  89,
  86,  77,  59,  56
};
short	yyr1[] =
{
   0,  17,  17,   1,   1,   4,   4,   5,   5,   6,
   6,   3,   2,   7,   8,   8,   9,   9,  10,  10,
  18,  10,  19,  10,  20,  10,  21,  10,  22,  10,
  23,  10,  10,  10,  10,  10,  10,  10,  10,  10,
  10,  10,  10,  11,  11,  11,  12,  12,  13,  13,
  13,  14,  14,  14,  14,  14,  14,  14,  14,  14,
  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,
  16,  16
};
short	yyr2[] =
{
   0,   0,   2,   1,   2,   1,   2,   2,   2,   1,
   2,   3,   3,   3,   0,   2,   2,   1,   0,   2,
   0,   4,   0,   4,   0,   8,   0,   6,   0,   4,
   0,   4,   1,   3,   3,   3,   3,   2,   2,   2,
   2,   3,   2,   1,   2,   2,   1,   3,   1,   1,
   3,   2,   5,   2,   2,   1,   2,   3,   3,   2,
   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
   0,   2
};
short	yychk[] =
{
-1000, -17,  -1, -10,  -5,  -3,   7,   4,   6,  12,
 -11,   9,  -9,  -7,  10,  11,  13,  36, -12,  15,
  16, -14,  30,  32,  31,  14,  40,  38,  33,  34,
  35,  27,  28,  17,  -1,  -8,  -9,  15,  -2,   8,
  38,  38,  -2, -13, -15, -14,   4,   5,   6,   7,
   8,   9,  10,  11,  12,  13,  15, -13,  -9, -13,
 -10, -10, -10, -10, -16,  -4, -10,  -6,  -5,  39,
  29, -13,  -3, -13, -13, -13,  -3, -13, -16, -10,
 -10, -10,  -8, -18, -19,  -4, -13, -22, -23,  29,
 -16,  -3, -13,  37,  33,  -4, -13, -13,  18,  -3,
  26, -10, -10,  26,   5,  26, -10,  -3, -13, -16,
 -16, -21,  26,  26, -10, -20, -10
};
short	yydef[] =
{
  -2,  -2,   0,   3,  18,  14,   0,   0,   0,   0,
  32,   0,  18,  18,  18,  18,  70,  18,  43,   0,
  17,  46,   0,   0,   0,  55,   0,  70,   2,   7,
   8,  18,  18,  18,   4,  19,  14,   0,  20,  22,
  18,   0,  28,  30,  48,  49,  60,  61,  62,  63,
  64,  65,  66,  67,  68,  69,   0,  44,  45,  70,
  37,  38,  39,  40,  42,   0,   5,  18,   9,   0,
   0,  16,  59,  51,  53,  54,  56,   0,   0,  34,
  35,  36,  15,  18,  18,   0,   0,  18,   0,   0,
  33,  41,  71,  11,  10,   6,  13,  47,  70,  57,
  58,  21,  23,  12,  70,  26,  29,  31,  50,   0,
   0,  18,  52,  24,  27,  18,  25
};
short	yytok1[] =
{
   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,
  33,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,  32,   0,  30,   0,  35,   0,
  38,  26,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,  34,
   0,  39,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,  29,   0,  40,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,  36,   0,  37
};
short	yytok2[] =
{
   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,
  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,
  22,  23,  24,  25,  27,  28,  31
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
#line	24	"/sys/src/cmd/rc/syn.y"
{ return 1;} break;
case 2:
#line	25	"/sys/src/cmd/rc/syn.y"
{readhere(lex->input); return !compile(yypt[-1].yyv.tree);} break;
case 4:
#line	27	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree2(';', yypt[-1].yyv.tree, yypt[-0].yyv.tree);} break;
case 6:
#line	29	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree2(';', yypt[-1].yyv.tree, yypt[-0].yyv.tree);} break;
case 8:
#line	31	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree1('&', yypt[-1].yyv.tree);} break;
case 10:
#line	33	"/sys/src/cmd/rc/syn.y"
{readhere(lex->input);} break;
case 11:
#line	34	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree1(BRACE, yypt[-1].yyv.tree);} break;
case 12:
#line	35	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree1(PCMD, yypt[-1].yyv.tree);} break;
case 13:
#line	36	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree2('=', yypt[-2].yyv.tree, yypt[-0].yyv.tree);} break;
case 14:
#line	37	"/sys/src/cmd/rc/syn.y"
{yyval.tree=0;} break;
case 15:
#line	38	"/sys/src/cmd/rc/syn.y"
{yyval.tree=mung2(yypt[-1].yyv.tree, yypt[-1].yyv.tree->child[0], yypt[-0].yyv.tree);} break;
case 16:
#line	39	"/sys/src/cmd/rc/syn.y"
{yyval.tree=mung1(yypt[-1].yyv.tree, yypt[-0].yyv.tree); if(yyval.tree->rtype==HERE) heredoc(yyval.tree);} break;
case 18:
#line	41	"/sys/src/cmd/rc/syn.y"
{yyval.tree=0;} break;
case 19:
#line	42	"/sys/src/cmd/rc/syn.y"
{yyval.tree=epimung(yypt[-1].yyv.tree, yypt[-0].yyv.tree);} break;
case 20:
#line	43	"/sys/src/cmd/rc/syn.y"
{skipnl();} break;
case 21:
#line	44	"/sys/src/cmd/rc/syn.y"
{yyval.tree=mung2(yypt[-3].yyv.tree, yypt[-2].yyv.tree, yypt[-0].yyv.tree);} break;
case 22:
#line	45	"/sys/src/cmd/rc/syn.y"
{skipnl();} break;
case 23:
#line	45	"/sys/src/cmd/rc/syn.y"
{yyval.tree=mung1(yypt[-2].yyv.tree, yypt[-0].yyv.tree);} break;
case 24:
#line	46	"/sys/src/cmd/rc/syn.y"
{skipnl();} break;
case 25:
#line	55	"/sys/src/cmd/rc/syn.y"
{yyval.tree=mung3(yypt[-7].yyv.tree, yypt[-5].yyv.tree, yypt[-3].yyv.tree ? yypt[-3].yyv.tree : tree1(PAREN, yypt[-3].yyv.tree), yypt[-0].yyv.tree);} break;
case 26:
#line	56	"/sys/src/cmd/rc/syn.y"
{skipnl();} break;
case 27:
#line	57	"/sys/src/cmd/rc/syn.y"
{yyval.tree=mung3(yypt[-5].yyv.tree, yypt[-3].yyv.tree, (tree*)0, yypt[-0].yyv.tree);} break;
case 28:
#line	58	"/sys/src/cmd/rc/syn.y"
{skipnl();} break;
case 29:
#line	59	"/sys/src/cmd/rc/syn.y"
{yyval.tree=mung2(yypt[-3].yyv.tree, yypt[-2].yyv.tree, yypt[-0].yyv.tree);} break;
case 30:
#line	60	"/sys/src/cmd/rc/syn.y"
{skipnl();} break;
case 31:
#line	61	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree2(SWITCH, yypt[-2].yyv.tree, yypt[-0].yyv.tree);} break;
case 32:
#line	62	"/sys/src/cmd/rc/syn.y"
{yyval.tree=simplemung(yypt[-0].yyv.tree);} break;
case 33:
#line	63	"/sys/src/cmd/rc/syn.y"
{yyval.tree=mung2(yypt[-2].yyv.tree, yypt[-1].yyv.tree, yypt[-0].yyv.tree);} break;
case 34:
#line	64	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree2(ANDAND, yypt[-2].yyv.tree, yypt[-0].yyv.tree);} break;
case 35:
#line	65	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree2(OROR, yypt[-2].yyv.tree, yypt[-0].yyv.tree);} break;
case 36:
#line	66	"/sys/src/cmd/rc/syn.y"
{yyval.tree=mung2(yypt[-1].yyv.tree, yypt[-2].yyv.tree, yypt[-0].yyv.tree);} break;
case 37:
#line	67	"/sys/src/cmd/rc/syn.y"
{yyval.tree=mung2(yypt[-1].yyv.tree, yypt[-1].yyv.tree->child[0], yypt[-0].yyv.tree);} break;
case 38:
#line	68	"/sys/src/cmd/rc/syn.y"
{yyval.tree=mung3(yypt[-1].yyv.tree, yypt[-1].yyv.tree->child[0], yypt[-1].yyv.tree->child[1], yypt[-0].yyv.tree);} break;
case 39:
#line	69	"/sys/src/cmd/rc/syn.y"
{yyval.tree=mung1(yypt[-1].yyv.tree, yypt[-0].yyv.tree);} break;
case 40:
#line	70	"/sys/src/cmd/rc/syn.y"
{yyval.tree=mung1(yypt[-1].yyv.tree, yypt[-0].yyv.tree);} break;
case 41:
#line	71	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree2(FN, yypt[-1].yyv.tree, yypt[-0].yyv.tree);} break;
case 42:
#line	72	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree1(FN, yypt[-0].yyv.tree);} break;
case 44:
#line	74	"/sys/src/cmd/rc/syn.y"
{yyval.tree=globprop(tree2(ARGLIST, yypt[-1].yyv.tree, yypt[-0].yyv.tree));} break;
case 45:
#line	75	"/sys/src/cmd/rc/syn.y"
{yyval.tree=globprop(tree2(ARGLIST, yypt[-1].yyv.tree, yypt[-0].yyv.tree));} break;
case 47:
#line	77	"/sys/src/cmd/rc/syn.y"
{yyval.tree=globprop(tree2('^', yypt[-2].yyv.tree, yypt[-0].yyv.tree));} break;
case 48:
#line	78	"/sys/src/cmd/rc/syn.y"
{lex->lastword=1; yypt[-0].yyv.tree->type=WORD;} break;
case 50:
#line	80	"/sys/src/cmd/rc/syn.y"
{yyval.tree=globprop(tree2('^', yypt[-2].yyv.tree, yypt[-0].yyv.tree));} break;
case 51:
#line	81	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree1('$', yypt[-0].yyv.tree);} break;
case 52:
#line	82	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree2(SUB, yypt[-3].yyv.tree, yypt[-1].yyv.tree);} break;
case 53:
#line	83	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree1('"', yypt[-0].yyv.tree);} break;
case 54:
#line	84	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree1(COUNT, yypt[-0].yyv.tree);} break;
case 56:
#line	86	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree2('`', (tree*)0, yypt[-0].yyv.tree);} break;
case 57:
#line	87	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree2('`', yypt[-1].yyv.tree, yypt[-0].yyv.tree);} break;
case 58:
#line	88	"/sys/src/cmd/rc/syn.y"
{yyval.tree=globprop(tree1(PAREN, yypt[-1].yyv.tree));} break;
case 59:
#line	89	"/sys/src/cmd/rc/syn.y"
{yyval.tree=mung1(yypt[-1].yyv.tree, yypt[-0].yyv.tree); yyval.tree->type=PIPEFD;} break;
case 70:
#line	91	"/sys/src/cmd/rc/syn.y"
{yyval.tree=(tree*)0;} break;
case 71:
#line	92	"/sys/src/cmd/rc/syn.y"
{yyval.tree=tree2(WORDS, yypt[-1].yyv.tree, yypt[-0].yyv.tree);} break;
	}
	goto yystack;  /* stack new state and value */
}
