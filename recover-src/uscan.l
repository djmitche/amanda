/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1996 University of Maryland at College Park
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
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/*
 * $Id: uscan.l,v 1.5 1997/12/16 20:44:42 jrj Exp $
 *
 * lexer for amrecover interactive language
 */
%{
#include "amanda.h"
#include "uparse.h"

#define YY_NO_UNPUT

#define	DATE_ALLOC_SIZE		sizeof("YYYY-MM-DD")	/* includes null */

extern void yyerror P((char *s));
extern int  yyparse P((void));
%}

%x quotedstring

%%

%{
char string_buf[1024];
char *string_buf_ptr;
%}

    /* literal keyword tokens */

sethost	{ return SETHOST; }
setdisk	{ return SETDISK; }
setdate { return SETDATE; }
setmode	{ return SETMODE; }
cd	{ return CD; }
quit    { return QUIT; }
exit    { return QUIT; }
history { return DHIST; }
ls      { return LS; }
add     { return ADD; }
list    { return LIST; }
delete  { return DELETE; }
pwd     { return PWD; }
clear   { return CLEAR; }
help    { return HELP; }
lcd     { return LCD; }
lpwd    { return LPWD; }
extract { return EXTRACT; }

    /* dates */

---[0-9][0-9] { time_t now;
	        struct tm *t;
                yylval.strval = (char *)malloc(DATE_ALLOC_SIZE);
                now = time((time_t *)NULL);
                t = localtime(&now);
                ap_snprintf(yylval.strval, DATE_ALLOC_SIZE,
			"%04d-%02d%s", 1900+t->tm_year, t->tm_mon+1, yytext+2);
                return DATE; }

--[0-9][0-9]-[0-9][0-9] { time_t now;
	                  struct tm *t;
                          yylval.strval = (char *)malloc(DATE_ALLOC_SIZE);
                          now = time((time_t *)NULL);
                          t = localtime(&now);
                          ap_snprintf(yylval.strval, DATE_ALLOC_SIZE,
				   "%04d%s", 1900+t->tm_year, yytext+1);
                          return DATE; }
                        
[1-2][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9] { yylval.strval = stralloc(yytext); return DATE; }

    /* file names */

[-A-Za-z0-9_/.]* { yylval.strval = stralloc(yytext); return PATH; }
\"               { string_buf_ptr = string_buf; BEGIN(quotedstring); }

<quotedstring>\"        { /* saw closing quote - all done */
  BEGIN(INITIAL);
  *string_buf_ptr = '\0';
  yylval.strval = stralloc(string_buf);
  return PATH;
}

<quotedstring>\\\" {
  /* escaped quote */
  *string_buf_ptr++ = '\\';
  *string_buf_ptr++ = '"';
}

<quotedstring>\n        {
  /* error - unterminated string constant */
  yyerror("unterminated string");
}

<quotedstring>[^\\\n\"]+        {
  char *yptr = yytext;
     
  while ( *yptr )
    *string_buf_ptr++ = *yptr++;
}
  
    /* whitespace */

[ \t\r]+	;     /* whitespace */

    /* anything else */

.	{ yyerror("invalid character"); }

%%

int process_line(line)
char *line;
{
    (void)yy_scan_string(line);		/* tell lex to scan lineread */
    return yyparse();			/* parse lineread and act */
}

int yywrap() {
  return 1;
}
