typedef union {
  int intval;
  double floatval;
  char *strval;
  int subtok;
} YYSTYPE;
#define	SETHOST	258
#define	SETDISK	259
#define	SETDATE	260
#define	SETMODE	261
#define	CD	262
#define	QUIT	263
#define	DHIST	264
#define	LS	265
#define	ADD	266
#define	EXTRACT	267
#define	LIST	268
#define	DELETE	269
#define	PWD	270
#define	CLEAR	271
#define	HELP	272
#define	LCD	273
#define	LPWD	274
#define	PATH	275
#define	DATE	276


extern YYSTYPE yylval;
