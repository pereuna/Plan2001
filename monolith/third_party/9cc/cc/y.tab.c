
#line	2	"/sys/src/cmd/cc/cc.y"
#include "cc.h"

#line	4	"/sys/src/cmd/cc/cc.y"
typedef union 	{
	Node*	node;
	Sym*	sym;
	Type*	type;
	struct
	{
		Type*	t;
		uchar	c;
	} tycl;
	struct
	{
		Type*	t1;
		Type*	t2;
		Type*	t3;
		uchar	c;
	} tyty;
	struct
	{
		char*	s;
		long	l;
	} sval;
	double	dval;
	vlong	vval;
	Spec	spec;
} YYSTYPE;
extern	int	yyerrflag;
#ifndef	YYMAXDEPTH
#define	YYMAXDEPTH	150
#endif
YYSTYPE	yylval;
YYSTYPE	yyval;
#define	LPE	57346
#define	LME	57347
#define	LMLE	57348
#define	LDVE	57349
#define	LMDE	57350
#define	LRSHE	57351
#define	LLSHE	57352
#define	LANDE	57353
#define	LXORE	57354
#define	LORE	57355
#define	LOROR	57356
#define	LANDAND	57357
#define	LEQ	57358
#define	LNE	57359
#define	LLE	57360
#define	LGE	57361
#define	LLSH	57362
#define	LRSH	57363
#define	LMM	57364
#define	LPP	57365
#define	LMG	57366
#define	LNAME	57367
#define	LTYPE	57368
#define	LFCONST	57369
#define	LDCONST	57370
#define	LCONST	57371
#define	LLCONST	57372
#define	LUCONST	57373
#define	LULCONST	57374
#define	LVLCONST	57375
#define	LUVLCONST	57376
#define	LSTRING	57377
#define	LLSTRING	57378
#define	LAUTO	57379
#define	LBREAK	57380
#define	LCASE	57381
#define	LCHAR	57382
#define	LCONTINUE	57383
#define	LDEFAULT	57384
#define	LDO	57385
#define	LDOUBLE	57386
#define	LELSE	57387
#define	LEXTERN	57388
#define	LFLOAT	57389
#define	LFOR	57390
#define	LGOTO	57391
#define	LIF	57392
#define	LINT	57393
#define	LLONG	57394
#define	LREGISTER	57395
#define	LRETURN	57396
#define	LSHORT	57397
#define	LSIZEOF	57398
#define	LUSED	57399
#define	LSTATIC	57400
#define	LSTRUCT	57401
#define	LSWITCH	57402
#define	LTYPEDEF	57403
#define	LTYPESTR	57404
#define	LUNION	57405
#define	LUNSIGNED	57406
#define	LWHILE	57407
#define	LVOID	57408
#define	LENUM	57409
#define	LSIGNED	57410
#define	LCONSTNT	57411
#define	LVOLATILE	57412
#define	LSET	57413
#define	LSIGNOF	57414
#define	LRESTRICT	57415
#define	LINLINE	57416
#define	LNORET	57417
#define	LDOTDOTDOT	57418
#define YYEOFCODE 1
#define YYERRCODE 2

#line	1190	"/sys/src/cmd/cc/cc.y"

short	yyexca[] =
{-1, 1,
	1, -1,
	-2, 182,
-1, 38,
	4, 8,
	5, 8,
	6, 9,
	-2, 5,
-1, 55,
	97, 195,
	-2, 194,
-1, 58,
	97, 199,
	-2, 198,
-1, 60,
	97, 203,
	-2, 202,
-1, 79,
	6, 9,
	-2, 8,
-1, 275,
	4, 100,
	66, 89,
	97, 85,
	-2, 0,
-1, 311,
	66, 89,
	97, 85,
	-2, 100,
-1, 317,
	4, 100,
	66, 89,
	97, 85,
	-2, 0,
-1, 346,
	6, 21,
	-2, 20,
-1, 384,
	4, 100,
	66, 89,
	97, 85,
	-2, 0,
-1, 388,
	4, 100,
	66, 89,
	97, 85,
	-2, 0,
-1, 390,
	4, 100,
	66, 89,
	97, 85,
	-2, 0,
-1, 404,
	4, 100,
	66, 89,
	97, 85,
	-2, 0,
-1, 411,
	4, 100,
	66, 89,
	97, 85,
	-2, 0,
};
#define	YYNPROD	248
#define	YYPRIVATE 57344
#define	YYLAST	1351
short	yyact[] =
{
 177, 307, 312, 345, 211, 209,  88, 257,   5,   4,
  43, 205, 310, 326, 325,  90, 258, 207, 267,  92,
  55,  58,  60,  23, 136,  82, 266,  41, 212, 127,
  49,  49,  93, 203,  89, 381, 203,  44,  45, 138,
  85, 278, 206, 139,  68, 269, 106,  83, 139,  38,
 143, 141,  44,  45, 255, 143, 141,  44,  45, 126,
  57, 334, 332, 289, 144, 411, 392, 391,  91, 340,
  49,  40, 339,  44,  45,  49, 333,  49, 294,  42,
  44,  45, 132, 291, 288, 134, 131,   5,  69, 373,
 130,  61, 120, 121, 196, 120,  49,  71, 353, 255,
 255,  25,  26, 404, 219,  27, 197,  28, 176,  79,
 128, 255, 255,  84, 119, 124, 184, 185, 186, 187,
 188, 189, 190, 191,  25,  26, 304,  57,  27, 202,
  28, 137, 255, 253, 132, 175, 365, 192, 194,  37,
 143, 254, 218, 217,  91, 296, 222, 223, 224, 225,
 226, 227, 228, 229, 230, 231, 232, 233, 234, 235,
 236, 237, 238, 239, 198, 241, 242, 243, 244, 245,
 246, 247, 248, 249, 250, 251, 208, 240, 221, 259,
 389, 220,  84, 215, 216,  47, 371,  69, 253, 406,
 390, 261, 262, 260, 219, 143, 254,  56,  46, 362,
 252, 388, 384,  78, 361, 274,  51, 176,  59, 176,
 357, 132,  25,  26,  91, 279,  27, 354,  28,  91,
 352, 344, 256,  44,  45, 284,  67,  66, 270, 283,
 364, 272,  70, 273, 263, 255, 264,  70, 302, 280,
 120,  25,  26, 286, 383,  27,   6,  28, 290, 282,
 271,  40,  72, 287,  22,  52, 285, 204,  70,  42,
  44,  45, 119,  54,  84,  81,  40, 367, 368, 293,
 139, 122,  91, 125,  42,  44,  45, 143, 141,  44,
  45, 368,   5, 276, 277, 308, 255, 303, 297, 298,
 331, 335, 299, 270, 220, 330, 259,  36, 292,  40,
 145, 146, 147,  91, 301, 409,   7,  42,  44,  45,
 295, 336, 338,  48,  48,  53, 120, 281, 343, 355,
 342, 356, 135, 208, 349, 351, 348,  40, 363, 270,
  62,  63, 285, 360, 253,  42,  44,  45, 132, 408,
 402, 143, 254, 366, 401, 396, 376, 374,  24, 359,
 358, 350, 347,  48, 346,  50,  50, 341,  48, 300,
  48,  77, 259, 259, 201,  76, 370,  75, 372, 377,
 378, 375, 382,  73, 386,  74, 315, 313,   5,  48,
 393, 387, 265, 200, 132, 123, 395, 369, 394,  65,
 398, 397, 400, 129, 309,  50,  80,  64,   3, 405,
  50,   2,  50, 399,   1, 385, 407, 142, 140, 210,
 268, 410, 311, 412, 346,  97, 148, 149, 145, 146,
 147,  50,  39, 116,  98,  99,  96, 115, 275, 103,
 102, 306,  95, 346,  94, 329,  12, 112, 111, 107,
 108, 109, 110, 113, 114, 117, 118,  29, 320, 327,
  13, 321, 328, 317,  20,   8,  31,  19,   0, 322,
 314,  15,  16,  34, 318,  14, 104, 323,  30,   9,
 319,  32,  33,  10,  18, 316,  21,  11,  17,  25,
  26, 324, 105,  27,  35,  28,  97,   0,   0,   0,
 305, 100, 101,   0,   0,  98,  99,  96,   0,   0,
 103, 102,   0,   0,   0,  94,  87,  12, 112, 111,
 107, 108, 109, 110, 113, 114, 117, 118,  29,   0,
   0,  13,   0,   0,   0,  20,   0,  31,  19,   0,
   0,   0,  15,  16,  34,   0,  14, 104, 309,  30,
   9,   0,  32,  33,  10,  18,   0,  21,  11,  17,
  25,  26,   0, 105,  27,  35,  28,   0,   0,  97,
   0,   0, 100, 101,   0,   0,   0,   0,  98,  99,
  96,   0,   0, 103, 102,   0,   0,   0,  94, 329,
   0, 112, 111, 107, 108, 109, 110, 113, 114, 117,
 118,   0, 320, 327,   0, 321, 328, 317,   0,   0,
   0,   0,   0, 322, 314,   0,   0,   0, 318,   0,
 104, 323,   0,   0, 319,   0,   0,   0,  97, 316,
   0,   0,   0,   0,   0, 324, 105,  98,  99,  96,
   0,   0, 103, 102,   0, 100, 101,  94, 329,   0,
 112, 111, 107, 108, 109, 110, 113, 114, 117, 118,
   0, 320, 327,   0, 321, 328, 317,   0,   0,   0,
   0,   0, 322, 314,   0,   0,   0, 318,   0, 104,
 323,   0,   0, 319,   0,   0,   0,   0, 316,   0,
   0,   0,   0,   0, 324, 105, 183, 182, 180, 181,
 179, 178,   0,   0, 100, 101, 164, 165, 166, 167,
 168, 169, 171, 170, 172, 173, 174, 163, 380, 162,
 161, 160, 159, 158, 156, 157, 152, 153, 154, 155,
 151, 150, 148, 149, 145, 146, 147,  97, 151, 150,
 148, 149, 145, 146, 147,   0,  98,  99,  96,   0,
   0, 103, 102,   0, 214, 213,  94,  87,   0, 112,
 111, 107, 108, 109, 110, 113, 114, 117, 118, 161,
 160, 159, 158, 156, 157, 152, 153, 154, 155, 151,
 150, 148, 149, 145, 146, 147,   0,   0, 104,   0,
   0,   0,   0,   0, 379,   0,   0,   0,   0,   0,
   0,   0,   0,   0, 105,   0,  97,   0,   0,   0,
   0, 133,   0, 100, 101,  98,  99,  96,   0,   0,
 103, 102,   0,   0,   0,  94,  87,   0, 112, 111,
 107, 108, 109, 110, 113, 114, 117, 118,   0,  97,
   0,   0,   0,   0,   0,   0,   0,   0,  98,  99,
  96,   0,   0, 103, 102,   0,   0, 104,  94,  87,
   0, 112, 111, 107, 108, 109, 110, 113, 114, 117,
 118,   0,   0, 105,   0,   0,   0,   0,   0,   0,
 133,   0, 100, 101,   0,   0,   0,   0,   0,   0,
 104,   0,   0,   0,   0,   0,   0,   0,   0,   0,
  12,   0,   0,   0,   0,   0, 105,   0,   0,   0,
   0,  29,   0, 337,  13, 100, 101,   0,  20,   0,
  31,  19,   0,   0,   0,  15,  16,  34,   0,  14,
   0,   0,  30,   9,   0,  32,  33,  10,  18,   0,
  21,  11,  17,  25,  26,   0,  97,  27,  35,  28,
   0,   0,   0,   0, 199,  98,  99,  96,   0,   0,
 103, 102,   0,   0,   0,  94,  87,   0, 112, 111,
 107, 108, 109, 110, 113, 114, 117, 118,   0,  97,
   0,   0,   0,   0,   0,   0,   0,   0,  98,  99,
  96,   0,   0, 103, 102,   0,   0, 104, 195,  87,
   0, 112, 111, 107, 108, 109, 110, 113, 114, 117,
 118,   0,   0, 105,   0,   0,   0,   0,   0,   0,
   0,   0, 100, 101,   0,   0,   0,   0,   0,   0,
 104,   0,   0,   0,   0,   0,   0,   0,  97,   0,
   0,   0,   0,   0,   0,   0, 105,  98,  99,  96,
   0,   0, 103, 102,   0, 100, 101, 193,  87,   0,
 112, 111, 107, 108, 109, 110, 113, 114, 117, 118,
 160, 159, 158, 156, 157, 152, 153, 154, 155, 151,
 150, 148, 149, 145, 146, 147,   0,   0,   0, 104,
   0,  87,  12,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,  29,   0, 105,  13,   0,   0,   0,
  20,   0,  31,  19, 100, 101,   0,  15,  16,  34,
   0,  14,   0,   0,  30,   9,   0,  32,  33,  10,
  18,   0,  21,  11,  17,  25,  26,  12,   0,  27,
  35,  28,  86,   0,   0,   0,   0,   0,  29,   0,
   0,  13,   0,   0,   0,  20,   0,  31,  19,   0,
   0,   0,  15,  16,  34,   0,  14,   0,   0,  30,
   9,   0,  32,  33,  10,  18,   0,  21,  11,  17,
  25,  26,   0,   0,  27,  35,  28,  29,   0,   0,
  13,   0,   0,   0,  20,   0,  31,  19,   0,   0,
   0,  15,  16,  34,   0,  14,   0,   0,  30,   0,
   0,  32,  33,   0,  18,   0,  21,   0,  17,  25,
  26,   0,   0,  27,  35,  28, 164, 165, 166, 167,
 168, 169, 171, 170, 172, 173, 174, 163, 403, 162,
 161, 160, 159, 158, 156, 157, 152, 153, 154, 155,
 151, 150, 148, 149, 145, 146, 147, 164, 165, 166,
 167, 168, 169, 171, 170, 172, 173, 174, 163,   0,
 162, 161, 160, 159, 158, 156, 157, 152, 153, 154,
 155, 151, 150, 148, 149, 145, 146, 147, 163,   0,
 162, 161, 160, 159, 158, 156, 157, 152, 153, 154,
 155, 151, 150, 148, 149, 145, 146, 147, 159, 158,
 156, 157, 152, 153, 154, 155, 151, 150, 148, 149,
 145, 146, 147, 158, 156, 157, 152, 153, 154, 155,
 151, 150, 148, 149, 145, 146, 147, 156, 157, 152,
 153, 154, 155, 151, 150, 148, 149, 145, 146, 147,
 152, 153, 154, 155, 151, 150, 148, 149, 145, 146,
 147
};
short	yypact[] =
{
-1000,1083,-1000, 293,-1000,-1000,1122,1122,1083,  30,
  30,  -6,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,
-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,
-1000,-1000,-1000,-1000,-1000,-1000,-1000, 326,-1000, 185,
-1000,-1000, 265,-1000,-1000,-1000,1122,-1000,-1000,-1000,
-1000,1122,-1000,1122,-1000,-1000,-1000,-1000,-1000,-1000,
-1000,-1000,-1000, 265,-1000, 259,1038, 913,  37,  -2,
-1000, 125,1122, -37,1083, -37, -38,  67,-1000,-1000,
1083, 773, -10, 317,-1000, 236,-1000,-1000,-1000, -32,
-1000,1241,-1000,-1000, 463, 649, 913, 913, 913, 913,
 913, 913, 913, 913,1005, 946,-1000,-1000,-1000,-1000,
-1000,-1000,-1000,-1000,-1000,  41,  52,-1000,-1000,-1000,
-1000,-1000,-1000, 846,-1000,-1000,-1000,  31, 251, -55,
 265,-1000,1241, 704,-1000,1038,-1000,-1000,-1000,-1000,
 101,   9,-1000, 913,-1000, 913, 913, 913, 913, 913,
 913, 913, 913, 913, 913, 913, 913, 913, 913, 913,
 913, 913, 913, 913, 913, 913, 913, 913, 913, 913,
 913, 913, 913, 913, 913, 300, 127,1241, 913, 913,
 180, 180,-1000,-1000,-1000,-1000,-1000,-1000,-1000,-1000,
-1000,-1000,-1000, 463,-1000, 463,-1000,-1000,-1000,-1000,
 232,  67,-1000,  67, 913,-1000,-1000, 279,-1000, -57,
 704, 312, 243, 913, 180,-1000,  14,1038, 913,-1000,
 -11, -33,-1000,-1000,-1000, 266, 266, 384, 384, 698,
 698, 698, 698,1314,1314,1303,1290,1276,1039, 739,
 230,1241,1241,1241,1241,1241,1241,1241,1241,1241,
1241,1241, -12,-1000,  99, 913,-1000, -17, 305,1241,
  49,-1000,-1000, 300, 300, 232, 355, 299,-1000,-1000,
 220, 913,  28,-1000,1241, 392,-1000, 265,-1000, 285,
 243,-1000,-1000, -34,-1000,-1000, -19, -35,-1000,-1000,
 913, 806, 154,-1000,-1000, 913,-1000, -23, -26, 353,
-1000, 232, 913,-1000,-1000,-1000,-1000,-1000, 217, 348,
-1000, 595, 347, -55, 178,  32, 175, 536, 913, 168,
 346, 345, 180, 162, 157,-1000, 281, 913, 212, 118,
-1000,-1000,-1000,-1000,-1000,1261,-1000, 704,-1000,-1000,
-1000,-1000,-1000,-1000,-1000, 263,-1000,-1000,-1000,-1000,
-1000,-1000, 913, 144, 913,   6, 343, 913,-1000,-1000,
 342, 913, 913, 690,-1000,-1000, -63,-1000, 265, 238,
 107, 463, 106, 138,-1000,  95,-1000, -28, -29, 913,
-1000,-1000,-1000, 773, 536, 341,-1000, 265, 536, 913,
 536, 340, 336,1210,-1000,  40, 913, 276,-1000,  94,
-1000,-1000,-1000,-1000, 536, 335, 301,-1000, 913,-1000,
 -30, 536,-1000
};
short	yypgo[] =
{
   0,  10, 185, 254, 348,  23, 306, 198, 455,  44,
  40, 197, 246,   6,  25,  47,   2,  46,  11,   1,
  13,   0,  19, 432,   7,  16, 431, 428,  32, 427,
 423,  45, 422, 412,  14,  12,   3, 410,  27,  28,
 409,  24,  39, 408, 407,  34,  15,   4,   5, 405,
 404, 401, 398, 139, 397, 396, 393, 389,   9, 387,
  17, 385, 383,  26, 382,  18, 377, 376, 375, 373,
 367, 365, 364,  29, 361
};
short	yyr1[] =
{
   0,  50,  50,  51,  51,  54,  56,  51,  53,  57,
  53,  53,  31,  31,  32,  32,  32,  32,  26,  26,
  36,  59,  36,  36,  55,  55,  60,  60,  62,  61,
  64,  61,  63,  63,  65,  65,  37,  37,  37,  41,
  41,  42,  42,  42,  43,  43,  43,  44,  44,  44,
  47,  47,  39,  39,  39,  40,  40,  40,  40,  48,
  48,  48,  14,  14,  15,  15,  15,  15,  15,  18,
  27,  27,  27,  33,  33,  34,  34,  34,  34,  19,
  19,  19,  49,  49,  35,  66,  35,  35,  35,  67,
  35,  35,  35,  35,  35,  35,  35,  35,  35,  35,
  16,  16,  45,  45,  46,  20,  20,  21,  21,  21,
  21,  21,  21,  21,  21,  21,  21,  21,  21,  21,
  21,  21,  21,  21,  21,  21,  21,  21,  21,  21,
  21,  21,  21,  21,  21,  21,  21,  21,  22,  22,
  22,  28,  28,  28,  28,  28,  28,  28,  28,  28,
  28,  28,  23,  23,  23,  23,  23,  23,  23,  23,
  23,  23,  23,  23,  23,  23,  23,  23,  23,  23,
  23,  23,  29,  29,  30,  30,  24,  24,  25,  25,
  68,  11,  52,  52,  13,  13,  13,  13,  13,  13,
  13,  13,  10,  58,  12,  69,  12,  12,  12,  70,
  12,  12,  12,  71,  72,  12,  74,  12,  12,   7,
   7,   9,   9,   2,   2,   2,   8,   8,   3,   3,
  73,  73,  73,  73,   6,   6,   6,   6,   6,   6,
   6,   6,   6,   4,   4,   4,   4,   4,   4,   4,
   5,   5,   5,   5,  17,  38,   1,   1
};
short	yyr2[] =
{
   0,   0,   2,   2,   3,   0,   0,   6,   1,   0,
   4,   3,   1,   3,   1,   3,   4,   4,   2,   3,
   1,   0,   4,   3,   0,   4,   1,   3,   0,   4,
   0,   5,   0,   1,   1,   3,   1,   3,   2,   0,
   1,   2,   3,   1,   1,   4,   4,   2,   3,   3,
   1,   3,   3,   2,   2,   2,   3,   1,   2,   1,
   1,   2,   0,   1,   1,   2,   2,   1,   3,   3,
   0,   2,   2,   1,   2,   5,   3,   2,   2,   2,
   1,   2,   1,   2,   2,   0,   2,   5,   7,   0,
  10,   5,   7,   3,   5,   2,   2,   3,   5,   5,
   0,   1,   0,   1,   1,   1,   3,   1,   3,   3,
   3,   3,   3,   3,   3,   3,   3,   3,   3,   3,
   3,   3,   3,   3,   3,   3,   5,   3,   3,   3,
   3,   3,   3,   3,   3,   3,   3,   3,   1,   5,
   7,   1,   2,   2,   2,   2,   2,   2,   2,   2,
   2,   2,   3,   5,   5,   4,   4,   3,   3,   2,
   2,   1,   1,   1,   1,   1,   1,   1,   1,   1,
   1,   1,   1,   2,   1,   2,   0,   1,   1,   3,
   0,   4,   0,   1,   1,   1,   1,   2,   2,   3,
   2,   3,   1,   1,   2,   0,   4,   2,   2,   0,
   4,   2,   2,   0,   0,   7,   0,   5,   1,   1,
   2,   0,   2,   1,   1,   1,   1,   2,   1,   1,
   1,   3,   2,   3,   1,   1,   1,   1,   1,   1,
   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
   1,   1,   1,   1,   1,   1,   1,   1
};
short	yychk[] =
{
-1000, -50, -51, -52, -58, -13, -12,  -6,  -8,  77,
  81,  85,  44,  58,  73,  69,  70,  86,  82,  65,
  62,  84,  -3,  -5,  -4,  87,  88,  91,  93,  55,
  76,  64,  79,  80,  71,  92,   4, -53, -31, -32,
  34, -38,  42,  -1,  43,  44,  -7,  -2,  -6,  -5,
  -4,  -7, -12,  -6,  -3,  -1, -11,  97,  -1, -11,
  -1,  97,   4,   5, -54, -57,  42,  41,  -9, -31,
  -2,  -9,  -7, -69, -68, -70, -71, -74, -53, -31,
 -55,   6, -14, -15, -17, -10,  94,  43, -13, -45,
 -46, -21, -22, -28,  42, -23,  34,  23,  32,  33,
  99, 100,  38,  37,  74,  90, -17,  47,  48,  49,
  50,  46,  45,  51,  52, -29, -30,  53,  54, -31,
  -5,  95, -11, -61, -10, -11,  97, -73,  43, -56,
 -58, -47, -21,  97,  95,   5, -41, -31, -42,  34,
 -43,  42, -44,  41,  96,  34,  35,  36,  32,  33,
  31,  30,  26,  27,  28,  29,  24,  25,  23,  22,
  21,  20,  19,  17,   6,   7,   8,   9,  10,  11,
  13,  12,  14,  15,  16, -10, -20, -21,  42,  41,
  39,  40,  38,  37, -22, -22, -22, -22, -22, -22,
 -22, -22, -28,  42, -28,  42,  53,  54, -10,  98,
 -62, -72,  98,   5,   6, -18,  97, -60, -31, -48,
 -40, -47, -39,  41,  40, -15,  -9,  42,  41,  95,
 -42, -45, -21, -21, -21, -21, -21, -21, -21, -21,
 -21, -21, -21, -21, -21, -21, -21, -21, -21, -21,
 -20, -21, -21, -21, -21, -21, -21, -21, -21, -21,
 -21, -21, -41,  34,  42,   5,  95, -24, -25, -21,
 -20,  -1,  -1, -10, -10, -64, -63, -65, -37, -31,
 -38,  18, -73, -73, -21, -27,   4,   5,  98, -47,
 -39,   5,   6, -46,  -1, -42, -14, -45,  95,  96,
  18,  95,  -9, -20,  95,   5,  96, -41, -41, -63,
   4,   5,  18, -46,  98,  98, -26, -19, -58,   2,
 -35, -33, -16, -66,  68, -67,  83,  61,  72,  78,
  56,  59,  67,  75,  89, -34, -20,  57,  60,  43,
 -60,   5,  96,  95,  96, -21, -22,  97, -25,  95,
  95,   4, -65, -46,   4, -36, -31,   4, -34, -35,
   4, -18,  42,  66,  42, -19, -16,  42,   4,   4,
  -1,  42,  42, -21,  18,  18, -48,   4,   5, -59,
 -20,  42, -20,  83,   4, -20,   4, -24, -24,  94,
  18,  98, -36,   6,  95, -49, -16, -58,  95,  42,
  95,  95,  95, -21, -47, -19,   4, -36, -19, -20,
 -19,   4,   4,  18,  63, -16,  95, -19,   4,   4,
 -16,  95, -19
};
short	yydef[] =
{
   1,  -2,   2,   0, 183, 193, 184, 185, 186,   0,
   0,   0, 208, 224, 225, 226, 227, 228, 229, 230,
 231, 232, 216, 218, 219, 240, 241, 242, 243, 233,
 234, 235, 236, 237, 238, 239,   3,   0,  -2,  12,
 211,  14,   0, 245, 246, 247, 187, 209, 213, 214,
 215, 188, 211, 190, 217,  -2, 197, 180,  -2, 201,
  -2, 206,   4,   0,  24,   0,  62, 102,   0,   0,
 210, 189, 191,   0,   0,   0,   0,   0,  11,  -2,
   6,   0,   0,  63,  64,  39,  67, 244, 192,   0,
 103, 104, 107, 138,   0, 141,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0, 161, 162, 163, 164,
 165, 166, 167, 168, 169, 170, 171, 172, 174,  13,
 212,  15, 196,   0,  28, 200, 204,   0, 220,   0,
   0,  10,  50,   0,  16,   0,  65,  66,  40, 211,
  43,   0,  44, 102,  17,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,  39,   0, 105, 176,   0,
   0,   0, 159, 160, 142, 143, 144, 145, 146, 147,
 148, 149, 150,   0, 151,   0, 173, 175,  30, 181,
  32,   0, 207, 222,   0,   7,  70,   0,  26,   0,
  59,  60,  57,   0,   0,  68,  41,  62, 102,  47,
   0,   0, 108, 109, 110, 111, 112, 113, 114, 115,
 116, 117, 118, 119, 120, 121, 122, 123, 124, 125,
   0, 127, 128, 129, 130, 131, 132, 133, 134, 135,
 136, 137,   0, 211,   0,   0, 152,   0, 177, 178,
   0, 157, 158,  39,  39,  32,   0,  33,  34,  36,
  14,   0,   0, 223, 221,  -2,  25,   0,  51,  61,
  58,  55,  54,   0,  53,  42,   0,   0,  49,  48,
   0,   0,  41, 106, 155,   0, 156,   0,   0,   0,
  29,   0,   0,  38, 205,  69,  71,  72,   0,   0,
  80,  -2,   0,   0,   0,   0,   0,  -2, 100,   0,
   0,   0,   0,   0,   0,  73, 101,   0,   0, 244,
  27,  56,  52,  45,  46, 126, 139,   0, 179, 153,
 154,  31,  35,  37,  18,   0,  -2,  79,  74,  81,
  84,  86,   0,   0,   0,   0,   0,   0,  95,  96,
   0, 176, 176,   0,  77,  78,   0,  19,   0,   0,
   0, 100,   0,   0,  93,   0,  97,   0,   0,   0,
  76, 140,  23,   0,  -2,   0,  82,   0,  -2,   0,
  -2,   0,   0,   0,  22,  87, 100,  83,  91,   0,
  94,  98,  99,  75,  -2,   0,   0,  88, 100,  92,
   0,  -2,  90
};
short	yytok1[] =
{
   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,  99,   0,   0,   0,  36,  23,   0,
  42,  95,  34,  32,   5,  33,  40,  35,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,  18,   4,
  26,   6,  27,  17,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,  41,   0,  96,  22,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0,   0,  97,  21,  98, 100
};
short	yytok2[] =
{
   2,   3,   7,   8,   9,  10,  11,  12,  13,  14,
  15,  16,  19,  20,  24,  25,  28,  29,  30,  31,
  37,  38,  39,  43,  44,  45,  46,  47,  48,  49,
  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,
  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,
  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,
  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,
  90,  91,  92,  93,  94
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
		
case 3:
#line	77	"/sys/src/cmd/cc/cc.y"
{
		dodecl(xdecl, lastclass, lasttype, Z);
	} break;
case 5:
#line	82	"/sys/src/cmd/cc/cc.y"
{
		lastdcl = T;
		firstarg = S;
		thisfnnode = dodecl(xdecl, lastclass, lasttype, yypt[-0].yyv.node);
		if(lastdcl == T || lastdcl->etype != TFUNC) {
			diag(yypt[-0].yyv.node, "not a function");
			lastdcl = types[TFUNC];
		}
		thisfn = lastdcl;
		markdcl();
		firstdcl = dclstack;
		argmark(yypt[-0].yyv.node, 0);
	} break;
case 6:
#line	96	"/sys/src/cmd/cc/cc.y"
{
		argmark(yypt[-2].yyv.node, 1);
		fndecls(0);
	} break;
case 7:
#line	101	"/sys/src/cmd/cc/cc.y"
{
		Node *n;

		fndecls(1);
		n = revertdcl();
		if(n)
			yypt[-0].yyv.node = new(OLIST, n, yypt[-0].yyv.node);
		if(!debug['a'] && !debug['Z'])
			codgen(yypt[-0].yyv.node, yypt[-4].yyv.node);
	} break;
case 8:
#line	114	"/sys/src/cmd/cc/cc.y"
{
		dodecl(xdecl, lastclass, lasttype, yypt[-0].yyv.node);
	} break;
case 9:
#line	118	"/sys/src/cmd/cc/cc.y"
{
		yypt[-0].yyv.node = dodecl(xdecl, lastclass, lasttype, yypt[-0].yyv.node);
	} break;
case 10:
#line	122	"/sys/src/cmd/cc/cc.y"
{
		doinit(yypt[-3].yyv.node->sym, yypt[-3].yyv.node->type, 0L, yypt[-0].yyv.node);
	} break;
case 13:
#line	130	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OIND, yypt[-0].yyv.node, Z);
		yyval.node->garb = simpleg(yypt[-1].yyv.spec);
	} break;
case 15:
#line	138	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = yypt[-1].yyv.node;
	} break;
case 16:
#line	142	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OFUNC, yypt[-3].yyv.node, yypt[-1].yyv.node);
	} break;
case 17:
#line	146	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OARRAY, yypt[-3].yyv.node, yypt[-1].yyv.node);
	} break;
case 18:
#line	155	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = dodecl(adecl, lastclass, lasttype, Z);
	} break;
case 19:
#line	159	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = yypt[-1].yyv.node;
	} break;
case 20:
#line	165	"/sys/src/cmd/cc/cc.y"
{
		dodecl(adecl, lastclass, lasttype, yypt[-0].yyv.node);
		yyval.node = Z;
	} break;
case 21:
#line	170	"/sys/src/cmd/cc/cc.y"
{
		yypt[-0].yyv.node = dodecl(adecl, lastclass, lasttype, yypt[-0].yyv.node);
	} break;
case 22:
#line	174	"/sys/src/cmd/cc/cc.y"
{
		long w;

		w = yypt[-3].yyv.node->sym->type->width;
		yyval.node = doinit(yypt[-3].yyv.node->sym, yypt[-3].yyv.node->type, 0L, yypt[-0].yyv.node);
		yyval.node = contig(yypt[-3].yyv.node->sym, yyval.node, w);
	} break;
case 23:
#line	182	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = yypt[-2].yyv.node;
		if(yypt[-0].yyv.node != Z) {
			yyval.node = yypt[-0].yyv.node;
			if(yypt[-2].yyv.node != Z)
				yyval.node = new(OLIST, yypt[-2].yyv.node, yypt[-0].yyv.node);
		}
	} break;
case 26:
#line	199	"/sys/src/cmd/cc/cc.y"
{
		dodecl(pdecl, lastclass, lasttype, yypt[-0].yyv.node);
	} break;
case 28:
#line	209	"/sys/src/cmd/cc/cc.y"
{
		lasttype = yypt[-0].yyv.type;
	} break;
case 30:
#line	214	"/sys/src/cmd/cc/cc.y"
{
		lasttype = yypt[-0].yyv.type;
	} break;
case 32:
#line	220	"/sys/src/cmd/cc/cc.y"
{
		lastfield = 0;
		edecl(CXXX, lasttype, S);
	} break;
case 34:
#line	228	"/sys/src/cmd/cc/cc.y"
{
		dodecl(edecl, CXXX, lasttype, yypt[-0].yyv.node);
	} break;
case 36:
#line	235	"/sys/src/cmd/cc/cc.y"
{
		lastbit = 0;
		firstbit = 1;
	} break;
case 37:
#line	240	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OBIT, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 38:
#line	244	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OBIT, Z, yypt[-0].yyv.node);
	} break;
case 39:
#line	252	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = (Z);
	} break;
case 41:
#line	259	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OIND, (Z), Z);
		yyval.node->garb = simpleg(yypt[-0].yyv.spec);
	} break;
case 42:
#line	264	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OIND, yypt[-0].yyv.node, Z);
		yyval.node->garb = simpleg(yypt[-1].yyv.spec);
	} break;
case 45:
#line	273	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OFUNC, yypt[-3].yyv.node, yypt[-1].yyv.node);
	} break;
case 46:
#line	277	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OARRAY, yypt[-3].yyv.node, yypt[-1].yyv.node);
	} break;
case 47:
#line	283	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OFUNC, (Z), Z);
	} break;
case 48:
#line	287	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OARRAY, (Z), yypt[-1].yyv.node);
	} break;
case 49:
#line	291	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = yypt[-1].yyv.node;
	} break;
case 51:
#line	298	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OINIT, invert(yypt[-1].yyv.node), Z);
	} break;
case 52:
#line	304	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OARRAY, yypt[-1].yyv.node, Z);
	} break;
case 53:
#line	308	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OELEM, Z, Z);
		yyval.node->sym = yypt[-0].yyv.sym;
	} break;
case 56:
#line	317	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OLIST, yypt[-2].yyv.node, yypt[-1].yyv.node);
	} break;
case 58:
#line	322	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OLIST, yypt[-1].yyv.node, yypt[-0].yyv.node);
	} break;
case 61:
#line	330	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OLIST, yypt[-1].yyv.node, yypt[-0].yyv.node);
	} break;
case 62:
#line	335	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = Z;
	} break;
case 63:
#line	339	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = invert(yypt[-0].yyv.node);
	} break;
case 65:
#line	347	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OPROTO, yypt[-0].yyv.node, Z);
		yyval.node->type = yypt[-1].yyv.type;
	} break;
case 66:
#line	352	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OPROTO, yypt[-0].yyv.node, Z);
		yyval.node->type = yypt[-1].yyv.type;
	} break;
case 67:
#line	357	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(ODOTDOT, Z, Z);
	} break;
case 68:
#line	361	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OLIST, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 69:
#line	367	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = invert(yypt[-1].yyv.node);
	//	if(yypt[-1].yyv.node != Z)
	//		yyval.node = new(OLIST, yypt[-1].yyv.node, yyval.node);
		if(yyval.node == Z)
			yyval.node = new(OLIST, Z, Z);
	} break;
case 70:
#line	376	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = Z;
	} break;
case 71:
#line	380	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OLIST, yypt[-1].yyv.node, yypt[-0].yyv.node);
	} break;
case 72:
#line	384	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OLIST, yypt[-1].yyv.node, yypt[-0].yyv.node);
	} break;
case 74:
#line	391	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OLIST, yypt[-1].yyv.node, yypt[-0].yyv.node);
	} break;
case 75:
#line	397	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCASE, yypt[-3].yyv.node, yypt[-1].yyv.node);
	} break;
case 76:
#line	401	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCASE, yypt[-1].yyv.node, Z);
	} break;
case 77:
#line	405	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCASE, Z, Z);
	} break;
case 78:
#line	409	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OLABEL, dcllabel(yypt[-1].yyv.sym, 1), Z);
	} break;
case 79:
#line	415	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = Z;
	} break;
case 81:
#line	420	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OLIST, yypt[-1].yyv.node, yypt[-0].yyv.node);
	} break;
case 83:
#line	427	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = yypt[-0].yyv.node;
	} break;
case 85:
#line	433	"/sys/src/cmd/cc/cc.y"
{
		markdcl();
	} break;
case 86:
#line	437	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = revertdcl();
		if(yyval.node)
			yyval.node = new(OLIST, yyval.node, yypt[-0].yyv.node);
		else
			yyval.node = yypt[-0].yyv.node;
	} break;
case 87:
#line	445	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OIF, yypt[-2].yyv.node, new(OLIST, yypt[-0].yyv.node, Z));
		if(yypt[-0].yyv.node == Z)
			warn(yypt[-2].yyv.node, "empty if body");
	} break;
case 88:
#line	451	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OIF, yypt[-4].yyv.node, new(OLIST, yypt[-2].yyv.node, yypt[-0].yyv.node));
		if(yypt[-2].yyv.node == Z)
			warn(yypt[-4].yyv.node, "empty if body");
		if(yypt[-0].yyv.node == Z)
			warn(yypt[-4].yyv.node, "empty else body");
	} break;
case 89:
#line	458	"/sys/src/cmd/cc/cc.y"
{ markdcl(); } break;
case 90:
#line	459	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = revertdcl();
		if(yyval.node){
			if(yypt[-6].yyv.node)
				yypt[-6].yyv.node = new(OLIST, yyval.node, yypt[-6].yyv.node);
			else
				yypt[-6].yyv.node = yyval.node;
		}
		yyval.node = new(OFOR, new(OLIST, yypt[-4].yyv.node, new(OLIST, yypt[-6].yyv.node, yypt[-2].yyv.node)), yypt[-0].yyv.node);
	} break;
case 91:
#line	470	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OWHILE, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 92:
#line	474	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(ODWHILE, yypt[-2].yyv.node, yypt[-5].yyv.node);
	} break;
case 93:
#line	478	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(ORETURN, yypt[-1].yyv.node, Z);
		yyval.node->type = thisfn->link;
	} break;
case 94:
#line	483	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCONST, Z, Z);
		yyval.node->vconst = 0;
		yyval.node->type = types[TINT];
		yypt[-2].yyv.node = new(OSUB, yyval.node, yypt[-2].yyv.node);

		yyval.node = new(OCONST, Z, Z);
		yyval.node->vconst = 0;
		yyval.node->type = types[TINT];
		yypt[-2].yyv.node = new(OSUB, yyval.node, yypt[-2].yyv.node);

		yyval.node = new(OSWITCH, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 95:
#line	497	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OBREAK, Z, Z);
	} break;
case 96:
#line	501	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCONTINUE, Z, Z);
	} break;
case 97:
#line	505	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OGOTO, dcllabel(yypt[-1].yyv.sym, 0), Z);
	} break;
case 98:
#line	509	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OUSED, yypt[-2].yyv.node, Z);
	} break;
case 99:
#line	513	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OSET, yypt[-2].yyv.node, Z);
	} break;
case 100:
#line	518	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = Z;
	} break;
case 102:
#line	524	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = Z;
	} break;
case 104:
#line	531	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCAST, yypt[-0].yyv.node, Z);
		yyval.node->type = types[TLONG];
	} break;
case 106:
#line	539	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCOMMA, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 108:
#line	546	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OMUL, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 109:
#line	550	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(ODIV, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 110:
#line	554	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OMOD, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 111:
#line	558	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OADD, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 112:
#line	562	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OSUB, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 113:
#line	566	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OASHR, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 114:
#line	570	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OASHL, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 115:
#line	574	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OLT, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 116:
#line	578	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OGT, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 117:
#line	582	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OLE, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 118:
#line	586	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OGE, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 119:
#line	590	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OEQ, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 120:
#line	594	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(ONE, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 121:
#line	598	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OAND, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 122:
#line	602	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OXOR, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 123:
#line	606	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OOR, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 124:
#line	610	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OANDAND, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 125:
#line	614	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OOROR, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 126:
#line	618	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCOND, yypt[-4].yyv.node, new(OLIST, yypt[-2].yyv.node, yypt[-0].yyv.node));
	} break;
case 127:
#line	622	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OAS, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 128:
#line	626	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OASADD, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 129:
#line	630	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OASSUB, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 130:
#line	634	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OASMUL, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 131:
#line	638	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OASDIV, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 132:
#line	642	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OASMOD, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 133:
#line	646	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OASASHL, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 134:
#line	650	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OASASHR, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 135:
#line	654	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OASAND, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 136:
#line	658	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OASXOR, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 137:
#line	662	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OASOR, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 139:
#line	669	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCAST, yypt[-0].yyv.node, Z);
		dodecl(NODECL, CXXX, yypt[-3].yyv.type, yypt[-2].yyv.node);
		yyval.node->type = lastdcl;
		yyval.node->xcast = 1;
	} break;
case 140:
#line	676	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OSTRUCT, yypt[-1].yyv.node, Z);
		dodecl(NODECL, CXXX, yypt[-5].yyv.type, yypt[-4].yyv.node);
		yyval.node->type = lastdcl;
	} break;
case 142:
#line	685	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OIND, yypt[-0].yyv.node, Z);
	} break;
case 143:
#line	689	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OADDR, yypt[-0].yyv.node, Z);
	} break;
case 144:
#line	693	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OPOS, yypt[-0].yyv.node, Z);
	} break;
case 145:
#line	697	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(ONEG, yypt[-0].yyv.node, Z);
	} break;
case 146:
#line	701	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(ONOT, yypt[-0].yyv.node, Z);
	} break;
case 147:
#line	705	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCOM, yypt[-0].yyv.node, Z);
	} break;
case 148:
#line	709	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OPREINC, yypt[-0].yyv.node, Z);
	} break;
case 149:
#line	713	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OPREDEC, yypt[-0].yyv.node, Z);
	} break;
case 150:
#line	717	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OSIZE, yypt[-0].yyv.node, Z);
	} break;
case 151:
#line	721	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OSIGN, yypt[-0].yyv.node, Z);
	} break;
case 152:
#line	727	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = yypt[-1].yyv.node;
	} break;
case 153:
#line	731	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OSIZE, Z, Z);
		dodecl(NODECL, CXXX, yypt[-2].yyv.type, yypt[-1].yyv.node);
		yyval.node->type = lastdcl;
	} break;
case 154:
#line	737	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OSIGN, Z, Z);
		dodecl(NODECL, CXXX, yypt[-2].yyv.type, yypt[-1].yyv.node);
		yyval.node->type = lastdcl;
	} break;
case 155:
#line	743	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OFUNC, yypt[-3].yyv.node, Z);
		if(yypt[-3].yyv.node->op == ONAME)
		if(yypt[-3].yyv.node->type == T)
			dodecl(xdecl, CXXX, types[TINT], yyval.node);
		yyval.node->right = invert(yypt[-1].yyv.node);
	} break;
case 156:
#line	751	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OIND, new(OADD, yypt[-3].yyv.node, yypt[-1].yyv.node), Z);
	} break;
case 157:
#line	755	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(ODOT, new(OIND, yypt[-2].yyv.node, Z), Z);
		yyval.node->sym = yypt[-0].yyv.sym;
	} break;
case 158:
#line	760	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(ODOT, yypt[-2].yyv.node, Z);
		yyval.node->sym = yypt[-0].yyv.sym;
	} break;
case 159:
#line	765	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OPOSTINC, yypt[-1].yyv.node, Z);
	} break;
case 160:
#line	769	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OPOSTDEC, yypt[-1].yyv.node, Z);
	} break;
case 162:
#line	774	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCONST, Z, Z);
		yyval.node->type = types[TINT];
		yyval.node->vconst = yypt[-0].yyv.vval;
		yyval.node->cstring = strdup(symb);
	} break;
case 163:
#line	781	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCONST, Z, Z);
		yyval.node->type = types[TLONG];
		yyval.node->vconst = yypt[-0].yyv.vval;
		yyval.node->cstring = strdup(symb);
	} break;
case 164:
#line	788	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCONST, Z, Z);
		yyval.node->type = types[TUINT];
		yyval.node->vconst = yypt[-0].yyv.vval;
		yyval.node->cstring = strdup(symb);
	} break;
case 165:
#line	795	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCONST, Z, Z);
		yyval.node->type = types[TULONG];
		yyval.node->vconst = yypt[-0].yyv.vval;
		yyval.node->cstring = strdup(symb);
	} break;
case 166:
#line	802	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCONST, Z, Z);
		yyval.node->type = types[TDOUBLE];
		yyval.node->fconst = yypt[-0].yyv.dval;
		yyval.node->cstring = strdup(symb);
	} break;
case 167:
#line	809	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCONST, Z, Z);
		yyval.node->type = types[TFLOAT];
		yyval.node->fconst = yypt[-0].yyv.dval;
		yyval.node->cstring = strdup(symb);
	} break;
case 168:
#line	816	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCONST, Z, Z);
		yyval.node->type = types[TVLONG];
		yyval.node->vconst = yypt[-0].yyv.vval;
		yyval.node->cstring = strdup(symb);
	} break;
case 169:
#line	823	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OCONST, Z, Z);
		yyval.node->type = types[TUVLONG];
		yyval.node->vconst = yypt[-0].yyv.vval;
		yyval.node->cstring = strdup(symb);
	} break;
case 172:
#line	834	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OSTRING, Z, Z);
		yyval.node->type = typ(TARRAY, types[TCHAR]);
		yyval.node->type->width = yypt[-0].yyv.sval.l + 1;
		yyval.node->cstring = yypt[-0].yyv.sval.s;
		yyval.node->sym = symstring;
		yyval.node->etype = TARRAY;
		yyval.node->class = CSTATIC;
	} break;
case 173:
#line	844	"/sys/src/cmd/cc/cc.y"
{
		char *s;
		int n;

		n = yypt[-1].yyv.node->type->width - 1;
		s = alloc(n+yypt[-0].yyv.sval.l+MAXALIGN);

		memcpy(s, yypt[-1].yyv.node->cstring, n);
		memcpy(s+n, yypt[-0].yyv.sval.s, yypt[-0].yyv.sval.l);
		s[n+yypt[-0].yyv.sval.l] = 0;

		yyval.node = yypt[-1].yyv.node;
		yyval.node->type->width += yypt[-0].yyv.sval.l;
		yyval.node->cstring = s;
	} break;
case 174:
#line	862	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OLSTRING, Z, Z);
		yyval.node->type = typ(TARRAY, types[TRUNE]);
		yyval.node->type->width = yypt[-0].yyv.sval.l + sizeof(Rune);
		yyval.node->rstring = (Rune*)yypt[-0].yyv.sval.s;
		yyval.node->sym = symstring;
		yyval.node->etype = TARRAY;
		yyval.node->class = CSTATIC;
	} break;
case 175:
#line	872	"/sys/src/cmd/cc/cc.y"
{
		char *s;
		int n;

		n = yypt[-1].yyv.node->type->width - sizeof(Rune);
		s = alloc(n+yypt[-0].yyv.sval.l+MAXALIGN);

		memcpy(s, yypt[-1].yyv.node->rstring, n);
		memcpy(s+n, yypt[-0].yyv.sval.s, yypt[-0].yyv.sval.l);
		*(Rune*)(s+n+yypt[-0].yyv.sval.l) = 0;

		yyval.node = yypt[-1].yyv.node;
		yyval.node->type->width += yypt[-0].yyv.sval.l;
		yyval.node->rstring = (Rune*)s;
	} break;
case 176:
#line	889	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = Z;
	} break;
case 179:
#line	897	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(OLIST, yypt[-2].yyv.node, yypt[-0].yyv.node);
	} break;
case 180:
#line	903	"/sys/src/cmd/cc/cc.y"
{
		yyval.tyty.t1 = strf;
		yyval.tyty.t2 = strl;
		yyval.tyty.t3 = lasttype;
		yyval.tyty.c = lastclass;
		strf = T;
		strl = T;
		lastbit = 0;
		firstbit = 1;
		lastclass = CXXX;
		lasttype = T;
	} break;
case 181:
#line	916	"/sys/src/cmd/cc/cc.y"
{
		yyval.type = strf;
		strf = yypt[-2].yyv.tyty.t1;
		strl = yypt[-2].yyv.tyty.t2;
		lasttype = yypt[-2].yyv.tyty.t3;
		lastclass = yypt[-2].yyv.tyty.c;
	} break;
case 182:
#line	925	"/sys/src/cmd/cc/cc.y"
{
		lastclass = CXXX;
		lasttype = types[TINT];
	} break;
case 184:
#line	933	"/sys/src/cmd/cc/cc.y"
{
		yyval.tycl.t = yypt[-0].yyv.type;
		yyval.tycl.c = CXXX;
	} break;
case 185:
#line	938	"/sys/src/cmd/cc/cc.y"
{
		yyval.tycl.t = simplet(yypt[-0].yyv.spec);
		yyval.tycl.c = CXXX;
	} break;
case 186:
#line	943	"/sys/src/cmd/cc/cc.y"
{
		yyval.tycl.t = simplet(yypt[-0].yyv.spec);
		yyval.tycl.c = simplec(yypt[-0].yyv.spec);
		yyval.tycl.t = garbt(yyval.tycl.t, yypt[-0].yyv.spec);
	} break;
case 187:
#line	949	"/sys/src/cmd/cc/cc.y"
{
		yyval.tycl.t = yypt[-1].yyv.type;
		yyval.tycl.c = simplec(yypt[-0].yyv.spec);
		yyval.tycl.t = garbt(yyval.tycl.t, yypt[-0].yyv.spec);
		if(yypt[-0].yyv.spec.type)
			diag(Z, "duplicate types given: %T and %Q", yypt[-1].yyv.type, yypt[-0].yyv.spec);
	} break;
case 188:
#line	957	"/sys/src/cmd/cc/cc.y"
{
		yyval.tycl.t = simplet(typebitor(yypt[-1].yyv.spec, yypt[-0].yyv.spec));
		yyval.tycl.c = simplec(yypt[-0].yyv.spec);
		yyval.tycl.t = garbt(yyval.tycl.t, yypt[-0].yyv.spec);
	} break;
case 189:
#line	963	"/sys/src/cmd/cc/cc.y"
{
		yyval.tycl.t = yypt[-1].yyv.type;
		yyval.tycl.c = simplec(yypt[-2].yyv.spec);
		yyval.tycl.t = garbt(yyval.tycl.t, typebitor(yypt[-2].yyv.spec, yypt[-0].yyv.spec));
	} break;
case 190:
#line	969	"/sys/src/cmd/cc/cc.y"
{
		yyval.tycl.t = simplet(yypt[-0].yyv.spec);
		yyval.tycl.c = simplec(yypt[-1].yyv.spec);
		yyval.tycl.t = garbt(yyval.tycl.t, yypt[-1].yyv.spec);
	} break;
case 191:
#line	975	"/sys/src/cmd/cc/cc.y"
{
		yyval.tycl.t = simplet(typebitor(yypt[-1].yyv.spec, yypt[-0].yyv.spec));
		yyval.tycl.c = simplec(typebitor(yypt[-2].yyv.spec, yypt[-0].yyv.spec));
		yyval.tycl.t = garbt(yyval.tycl.t, typebitor(yypt[-2].yyv.spec, yypt[-0].yyv.spec));
	} break;
case 192:
#line	983	"/sys/src/cmd/cc/cc.y"
{
		yyval.type = yypt[-0].yyv.tycl.t;
		if(yypt[-0].yyv.tycl.c != CXXX)
			diag(Z, "illegal combination of class 4: %s", cnames[yypt[-0].yyv.tycl.c]);
	} break;
case 193:
#line	991	"/sys/src/cmd/cc/cc.y"
{
		lasttype = yypt[-0].yyv.tycl.t;
		lastclass = yypt[-0].yyv.tycl.c;
	} break;
case 194:
#line	998	"/sys/src/cmd/cc/cc.y"
{
		dotag(yypt[-0].yyv.sym, TSTRUCT, 0);
		yyval.type = yypt[-0].yyv.sym->suetag;
	} break;
case 195:
#line	1003	"/sys/src/cmd/cc/cc.y"
{
		dotag(yypt[-0].yyv.sym, TSTRUCT, autobn);
	} break;
case 196:
#line	1007	"/sys/src/cmd/cc/cc.y"
{
		yyval.type = yypt[-2].yyv.sym->suetag;
		if(yyval.type->link != T)
			diag(Z, "redeclare tag: %s", yypt[-2].yyv.sym->name);
		yyval.type->link = yypt[-0].yyv.type;
		sualign(yyval.type);
	} break;
case 197:
#line	1015	"/sys/src/cmd/cc/cc.y"
{
		taggen++;
		sprint(symb, "_%d_", taggen);
		yyval.type = dotag(lookup(), TSTRUCT, autobn);
		yyval.type->link = yypt[-0].yyv.type;
		sualign(yyval.type);
	} break;
case 198:
#line	1023	"/sys/src/cmd/cc/cc.y"
{
		dotag(yypt[-0].yyv.sym, TUNION, 0);
		yyval.type = yypt[-0].yyv.sym->suetag;
	} break;
case 199:
#line	1028	"/sys/src/cmd/cc/cc.y"
{
		dotag(yypt[-0].yyv.sym, TUNION, autobn);
	} break;
case 200:
#line	1032	"/sys/src/cmd/cc/cc.y"
{
		yyval.type = yypt[-2].yyv.sym->suetag;
		if(yyval.type->link != T)
			diag(Z, "redeclare tag: %s", yypt[-2].yyv.sym->name);
		yyval.type->link = yypt[-0].yyv.type;
		sualign(yyval.type);
	} break;
case 201:
#line	1040	"/sys/src/cmd/cc/cc.y"
{
		taggen++;
		sprint(symb, "_%d_", taggen);
		yyval.type = dotag(lookup(), TUNION, autobn);
		yyval.type->link = yypt[-0].yyv.type;
		sualign(yyval.type);
	} break;
case 202:
#line	1048	"/sys/src/cmd/cc/cc.y"
{
		dotag(yypt[-0].yyv.sym, TENUM, 0);
		yyval.type = yypt[-0].yyv.sym->suetag;
		if(yyval.type->link == T)
			yyval.type->link = types[TINT];
		yyval.type = yyval.type->link;
	} break;
case 203:
#line	1056	"/sys/src/cmd/cc/cc.y"
{
		dotag(yypt[-0].yyv.sym, TENUM, autobn);
	} break;
case 204:
#line	1060	"/sys/src/cmd/cc/cc.y"
{
		en.tenum = T;
		en.cenum = T;
	} break;
case 205:
#line	1065	"/sys/src/cmd/cc/cc.y"
{
		yyval.type = yypt[-5].yyv.sym->suetag;
		if(yyval.type->link != T)
			diag(Z, "redeclare tag: %s", yypt[-5].yyv.sym->name);
		if(en.tenum == T) {
			diag(Z, "enum type ambiguous: %s", yypt[-5].yyv.sym->name);
			en.tenum = types[TINT];
		}
		yyval.type->link = en.tenum;
		yyval.type = en.tenum;
	} break;
case 206:
#line	1077	"/sys/src/cmd/cc/cc.y"
{
		en.tenum = T;
		en.cenum = T;
	} break;
case 207:
#line	1082	"/sys/src/cmd/cc/cc.y"
{
		yyval.type = en.tenum;
	} break;
case 208:
#line	1086	"/sys/src/cmd/cc/cc.y"
{
		yyval.type = tcopy(yypt[-0].yyv.sym->type);
	} break;
case 210:
#line	1093	"/sys/src/cmd/cc/cc.y"
{
		yyval.spec = typebitor(yypt[-1].yyv.spec, yypt[-0].yyv.spec);
	} break;
case 211:
#line	1098	"/sys/src/cmd/cc/cc.y"
{
		yyval.spec = (Spec){0, 0};
	} break;
case 212:
#line	1102	"/sys/src/cmd/cc/cc.y"
{
		yyval.spec = typebitor(yypt[-1].yyv.spec, yypt[-0].yyv.spec);
	} break;
case 217:
#line	1114	"/sys/src/cmd/cc/cc.y"
{
		yyval.spec = typebitor(yypt[-1].yyv.spec, yypt[-0].yyv.spec);
	} break;
case 220:
#line	1124	"/sys/src/cmd/cc/cc.y"
{
		doenum(yypt[-0].yyv.sym, Z);
	} break;
case 221:
#line	1128	"/sys/src/cmd/cc/cc.y"
{
		doenum(yypt[-2].yyv.sym, yypt[-0].yyv.node);
	} break;
case 224:
#line	1135	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){BCHAR, 0}; } break;
case 225:
#line	1136	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){BSHORT, 0}; } break;
case 226:
#line	1137	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){BINT, 0}; } break;
case 227:
#line	1138	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){BLONG, 0}; } break;
case 228:
#line	1139	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){BSIGNED, 0}; } break;
case 229:
#line	1140	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){BUNSIGNED, 0}; } break;
case 230:
#line	1141	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){BFLOAT, 0}; } break;
case 231:
#line	1142	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){BDOUBLE, 0}; } break;
case 232:
#line	1143	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){BVOID, 0}; } break;
case 233:
#line	1146	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){0, BAUTO}; } break;
case 234:
#line	1147	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){0, BSTATIC}; } break;
case 235:
#line	1148	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){0, BEXTERN}; } break;
case 236:
#line	1149	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){0, BTYPEDEF}; } break;
case 237:
#line	1150	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){0, BTYPESTR}; } break;
case 238:
#line	1151	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){0, BREGISTER}; } break;
case 239:
#line	1152	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){0, 0}; } break;
case 240:
#line	1155	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){0, BCONSTNT}; } break;
case 241:
#line	1156	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){0, BVOLATILE}; } break;
case 242:
#line	1157	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){0, 0}; } break;
case 243:
#line	1158	"/sys/src/cmd/cc/cc.y"
{ yyval.spec = (Spec){0, BNORET}; } break;
case 244:
#line	1162	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(ONAME, Z, Z);
		if(yypt[-0].yyv.sym->class == CLOCAL)
			yypt[-0].yyv.sym = mkstatic(yypt[-0].yyv.sym);
		yyval.node->sym = yypt[-0].yyv.sym;
		yyval.node->type = yypt[-0].yyv.sym->type;
		yyval.node->etype = TVOID;
		if(yyval.node->type != T)
			yyval.node->etype = yyval.node->type->etype;
		yyval.node->xoffset = yypt[-0].yyv.sym->offset;
		yyval.node->class = yypt[-0].yyv.sym->class;
		yypt[-0].yyv.sym->aused = 1;
	} break;
case 245:
#line	1177	"/sys/src/cmd/cc/cc.y"
{
		yyval.node = new(ONAME, Z, Z);
		yyval.node->sym = yypt[-0].yyv.sym;
		yyval.node->type = yypt[-0].yyv.sym->type;
		yyval.node->etype = TVOID;
		if(yyval.node->type != T)
			yyval.node->etype = yyval.node->type->etype;
		yyval.node->xoffset = yypt[-0].yyv.sym->offset;
		yyval.node->class = yypt[-0].yyv.sym->class;
	} break;
	}
	goto yystack;  /* stack new state and value */
}
