/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1997 University of Maryland
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
 * Author: George Scott, Computer Centre, Monash University.
 */
/*
 *  token.c - token bashing routines
 */

#include "amanda.h"
#include "arglist.h"
#include "token.h"

/* Static buffer for these routines.  This shouldn't cause
** a problem in most cases.
*/
static char *buf = (char *)0;

/* Split a string up into tokens.
** There is exactly one separator character between tokens.
**
** Inspired by awk and a routine called splitter() that I snarfed from
** the net ages ago (original author long forgotten).
*/
int split(str, token, toklen, sep)
char *str;	/* String to split */
char **token;	/* Array of token pointers */
int toklen;	/* Size of token[] */
char *sep;	/* Token separators - usually " " */
{
	register char *pi, *po;
	register int fld;
	register int len;

	if (toklen <= 0) abort();	/* You gotta be kidding! */

	token[0] = str;
	for (fld = 1; fld < toklen; fld++) token[fld] = (char *)0;

	fld = 0;

	if (sep[0] == '\0' || str[0] == '\0' || toklen == 1) return fld;

	/* Calculate the length of the unquoted string. */

	len = 0;
	for (pi = str; *pi; pi++) {
		if (*pi == '\\') pi++; /* Had better not be trailing... */
		len++;
	}

	/* Allocate some space */

	if (buf != (char *)0) free(buf); /* Clean up from last time */
	buf = malloc(len+1);

	/* Copy it across and tokenise it */

	po = buf;
	token[++fld] = po;
	for (pi = str; *pi; pi++) {
		if (*pi == '\\') {
			*po++ = *++pi;	/* escaped */
		}
		else if (strchr(sep, *pi)) {
			*po++ = '\0';	/* end of token */
			if (fld+1 >= toklen) return fld;
			token[++fld] = po;
		}
		else {
			*po++ = *pi;	/* normal */
		}
	}
	*po = '\0';

	assert(po-buff == len);	/* Just checking! */

	return fld;
}

/*
** Quote all the funny characters in one token.
**
** At the moment we will just put \ in front of ' ' and '\'.
**/
arglist_function(char *quotef, char *, format)
{
	va_list argp;
	char linebuf[16384];

	/* Format the token */

	arglist_start(argp, format);
	vsprintf(linebuf, format, argp);
	arglist_end(argp);

	return quote(linebuf);
}

char *quote(str)
char *str;	/* The string to quote */
{
	register char *pi, *po;
	register int len;

	/* Calculate the length of the quoted token. */

	len = 0;
	for (pi = str; *pi; pi++) {
		if (*pi == ' ' || *pi == '\\') len++;
		len++;
	}

	/* Allocate some space */

	if (buf != (char *)0) free(buf); /* Clean up from last time */
	buf = malloc(len+1);	/* trailing null */

	/* Copy it across */

	for (pi = str, po = buf; *pi; *po++ = *pi++) {
		if (*pi == ' ' || *pi == '\\') *po++ = '\\';
	}
	*po = '\0';

	assert(po - buff == len);	/* Just checking! */

	return buf;
}

#ifdef TEST

int main()
{
	char str[1024];
	char *t[20];
	int r;
	char *sr;
	int i;

	printf("Testing split() with \" \" token separator\n");
	while(1) {
		printf("Input string: ");
		if (gets(str) == NULL) {
			printf("\n");
			break;
		}
		r = split(str, t, 20, " ");
		printf("%d tokens:\n", r);
		for (i=0; i <= r; i++) printf("tok[%d] = \"%s\"\n", i, t[i]);
	}

	printf("\n");

	printf("Testing quote()\n");
	while(1) {
		printf("Input string: ");
		if (gets(str) == NULL) {
			printf("\n");
			break;
		}
		sr = quote(str);
		printf("Quoted   = \"%s\"\n", sr);
		r = split(strcpy(str,sr), t, 20, " ");
		if (r != 1) printf("split()=%d!\n", r);
		printf("Unquoted = \"%s\"\n", t[1]);
	}
}
#endif
