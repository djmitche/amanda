/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1995 University of Maryland at College Park
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
 * $Id: match.c,v 1.9 1998/06/01 19:46:41 jrj Exp $
 *
 * functions for checking and matching regular expressions
 */

#include "amanda.h"
#include "regex.h"

char *validate_regexp(regex)
char *regex;
{
    regex_t regc;
    int result;
    static char errmsg[STR_SIZE];

    if ((result = regcomp(&regc, regex,
			  REG_EXTENDED|REG_NOSUB|REG_NEWLINE)) != 0) {
      regerror(result, &regc, errmsg, sizeof(errmsg));
      return errmsg;
    }

    regfree(&regc);

    return NULL;
}

int match(regex, str)
char *regex, *str;
{
    regex_t regc;
    int result;
    char errmsg[STR_SIZE];

    if((result = regcomp(&regc, regex,
			 REG_EXTENDED|REG_NOSUB|REG_NEWLINE)) != 0) {
        regerror(result, &regc, errmsg, sizeof(errmsg));
	error("regex \"%s\": %s", regex, errmsg);
    }

    if((result = regexec(&regc, str, 0, 0, 0)) != 0
       && result != REG_NOMATCH) {
        regerror(result, &regc, errmsg, sizeof(errmsg));
	error("regex \"%s\": %s", regex, errmsg);
    }

    regfree(&regc);

    return result == 0;
}

char *validate_glob(glob)
char *glob;
{
    char *regex = NULL;
    regex_t regc;
    int result;
    static char errmsg[STR_SIZE];

    regex = glob_to_regex(glob);
    if ((result = regcomp(&regc, regex,
			  REG_EXTENDED|REG_NOSUB|REG_NEWLINE)) != 0) {
      regerror(result, &regc, errmsg, sizeof(errmsg));
      amfree(regex);
      return errmsg;
    }

    regfree(&regc);
    amfree(regex);

    return NULL;
}

int match_glob(glob, str)
char *glob, *str;
{
    char *regex = NULL;
    regex_t regc;
    int result;
    char errmsg[STR_SIZE];

    regex = glob_to_regex(glob);
    if((result = regcomp(&regc, regex,
			 REG_EXTENDED|REG_NOSUB|REG_NEWLINE)) != 0) {
        regerror(result, &regc, errmsg, sizeof(errmsg));
	amfree(regex);
	error("glob \"%s\" -> regex \"%s\": %s", glob, regex, errmsg);
    }

    if((result = regexec(&regc, str, 0, 0, 0)) != 0
       && result != REG_NOMATCH) {
        regerror(result, &regc, errmsg, sizeof(errmsg));
	amfree(regex);
	error("glob \"%s\" -> regex \"%s\": %s", glob, regex, errmsg);
    }

    regfree(&regc);
    amfree(regex);

    return result == 0;
}

char *glob_to_regex(glob)
char *glob;
{
    char *regex;
    char *r;
    int len;
    int ch;
    int last_ch;

    /*
     * Allocate an area to convert into.  The worst case is a five to
     * one expansion.
     */
    len = strlen(glob);
    regex = alloc(1 + len * 5 + 1 + 1);

    /*
     * Do the conversion:
     *
     *  ?      -> [^/]
     *  *      -> [^/]*
     *  [!...] -> [^...]
     *
     * The following are given a leading backslash to protect them
     * unless they already have a backslash:
     *
     *   ( ) { } + . ^ $ |
     *
     * Put a leading ^ and trailing $ around the result.  If the last
     * non-escaped character is \ leave the $ off to cause a syntax
     * error when the regex is compiled.
     */

    r = regex;
    *r++ = '^';
    last_ch = '\0';
    for (ch = *glob++; ch != '\0'; last_ch = ch, ch = *glob++) {
	if (last_ch == '\\') {
	    *r++ = ch;
	    ch = '\0';			/* so last_ch != '\\' next time */
	} else if (last_ch == '[' && ch == '!') {
	    *r++ = '^';
	} else if (ch == '\\') {
	    *r++ = ch;
	} else if (ch == '*' || ch == '?') {
	    *r++ = '[';
	    *r++ = '^';
	    *r++ = '/';
	    *r++ = ']';
	    if (ch == '*') {
		*r++ = '*';
	    }
	} else if (ch == '('
		   || ch == ')'
		   || ch == '{'
		   || ch == '}'
		   || ch == '+'
		   || ch == '.'
		   || ch == '^'
		   || ch == '$'
		   || ch == '|') {
	    *r++ = '\\';
	    *r++ = ch;
	} else {
	    *r++ = ch;
	}
    }
    if (last_ch != '\\') {
	*r++ = '$';
    }
    *r = '\0';

    return regex;
}
