/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-2000 University of Maryland at College Park
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of U.M. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  U.M. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * U.M. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL U.M.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors: the Amanda Development Team.  Its members are listed in a
 * file named AUTHORS, in the root directory of this distribution.
 */
/*
 * $Id: uscan.l,v 1.14.2.3.4.1 2001/11/03 14:02:32 martinea Exp $
 *
 * lexer for amrecover interactive language
 */
%{
#include "amanda.h"
#undef ECHO
#include "uparse.h"

#define YY_NO_UNPUT

#define	DATE_ALLOC_SIZE		sizeof("YYYY-MM-DD")	/* includes null */

extern void yyerror P((char *s));
extern int  yyparse P((void));
static int  ll_parse_date P((int type, char *text));
%}

%x quotedstring

%{
static char *string_buf = NULL;
%}

%%

%{
    /* literal keyword tokens */
%}

sethost	{ return SETHOST; }
setdisk	{ return SETDISK; }
setdate { return SETDATE; }
setmode	{ return SETMODE; }
settape { return SETTAPE; }
cd	{ return CD; }
cdx	{ return CDX; }
quit    { return QUIT; }
exit    { return QUIT; }
history { return DHIST; }
ls      { return LS; }
add     { return ADD; }
addx    { return ADDX; }
list    { return LIST; }
delete  { return DELETE; }
deletex { return DELETEX; }
pwd     { return PWD; }
clear   { return CLEAR; }
help    { return HELP; }
\?      { return HELP; }
lcd     { return LCD; }
lpwd    { return LPWD; }
extract { return EXTRACT; }
smb     { return SMB; }
tar     { return TAR; }
mode    { return MODE; }

%{
    /* dates */
%}

---[0-9]+		{ return ll_parse_date(1, yytext); }
--[0-9]+-[0-9]+		{ return ll_parse_date(2, yytext); }
[0-9]+-[0-9]+-[0-9]+	{ return ll_parse_date(3, yytext); }

%{
    /* file names */
%}

[^ \t\r"]+		{
  yylval.strval = stralloc(yytext); return PATH;
}

%{
    /* quoted file names */
%}

\"               { amfree(string_buf); BEGIN(quotedstring); }

<quotedstring>\"        { /* saw closing quote - all done */
  BEGIN(INITIAL);
  if(string_buf) {
    yylval.strval = string_buf;
  } else {
    yylval.strval = "";
  }
  return PATH;
}

<quotedstring>\\\" {
  /* escaped quote */
  strappend(string_buf, "\\\"");
}

<quotedstring>\n        {
  /* error - unterminated string constant */
  yyerror("unterminated string");
  amfree(string_buf);
}

<quotedstring>[^\\\n\"]+        { strappend(string_buf, yytext); }

%{
    /* whitespace */
%}

[ \t\r]+	;     /* whitespace */

%{
    /* anything else */
    /* everything should have been handled by now, so this rule is disabled */
%}

%{
#if 0
.	{ yyerror("invalid character"); }
#endif
%}

%%

int process_line(line)
char *line;
{
    (void)yy_scan_string(line);		/* tell lex to scan lineread */
    return yyparse();			/* parse lineread and act */
}

static int ll_parse_date(type, text)
int type;
char *text;
{
    time_t now;
    struct tm *t;
    int y, m, d;
    int ret;

    now = time((time_t *)NULL);
    t = localtime(&now);
    y = 1900+t->tm_year;
    m = t->tm_mon+1;
    d = t->tm_mday;
    if(type == 1) {
	sscanf(text, "---%d", &d);
    } else if(type == 2) {
	sscanf(text, "--%d-%d", &m, &d);
    } else {
	sscanf(text, "%d-%d-%d", &y, &m, &d);
	if(y < 70) {
	    y += 2000;
	} else if(y < 100) {
	    y += 1900;
	}
    }
    ret = PATH;				/* cause a parse error */
    if(y < 1000 || y > 9999) {
	yyerror("invalid year");
    } else if(m < 1 || m > 12) {
	yyerror("invalid month");
    } else if(d < 1 || d > 31) {
	yyerror("invalid day");
    } else {
	yylval.strval = alloc(DATE_ALLOC_SIZE);
	ap_snprintf(yylval.strval, DATE_ALLOC_SIZE, "%04d-%02d-%02d", y, m, d);
	ret = DATE;
    }
    return ret;
}

int yywrap() {
  return 1;
}
