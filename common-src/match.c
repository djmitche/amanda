/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998 University of Maryland at College Park
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
 * $Id: match.c,v 1.19 2004/08/02 18:56:26 martinea Exp $
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

char *clean_regex(regex)
char *regex;
{
    char *result;
    int j;
    size_t i;
    result = alloc(2*strlen(regex)+1);

    for(i=0,j=0;i<strlen(regex);i++) {
	if(!isalnum((int)regex[i]))
	    result[j++]='\\';
	result[j++]=regex[i];
    }
    result[j++] = '\0';
    return result;
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
    size_t len;
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


int match_tar(glob, str)
char *glob, *str;
{
    char *regex = NULL;
    regex_t regc;
    int result;
    char errmsg[STR_SIZE];

    regex = tar_to_regex(glob);
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

char *tar_to_regex(glob)
char *glob;
{
    char *regex;
    char *r;
    size_t len;
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
     *  *      -> .*
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
	} else if (ch == '*') {
	    *r++ = '.';
	    *r++ = '*';
	} else if (ch == '?') {
	    *r++ = '[';
	    *r++ = '^';
	    *r++ = '/';
	    *r++ = ']';
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


int match_word(glob, word, separator)
char *glob, *word;
char separator;
{
    char *regex;
    char *r;
    size_t  len;
    int  ch;
    int  last_ch;
    int  next_ch;
    size_t  lenword;
    char *nword;
    char *nglob;
    char *g, *w;
    int  i;

    lenword = strlen(word);
    nword = (char *)alloc(lenword + 3);

    r = nword;
    w = word;
    if(lenword == 1 && *w == separator) {
	*r++ = separator;
	*r++ = separator;
    }
    else {
	if(*w != separator)
	    *r++ = separator;
	while(*w != '\0')
	    *r++ = *w++;
	if(*(r-1) != separator)
	    *r++ = separator;    
    }
    *r = '\0';

    /*
     * Allocate an area to convert into.  The worst case is a six to
     * one expansion.
     */
    len = strlen(glob);
    regex = (char *)alloc(1 + len * 6 + 1 + 1 + 2 + 2);
    r = regex;
    nglob = stralloc(glob);
    g = nglob;

    if((len == 1 && nglob[0] == separator) ||
       (len == 2 && nglob[0] == '^' && nglob[1] == separator) ||
       (len == 2 && nglob[0] == separator && nglob[1] == '$') ||
       (len == 3 && nglob[0] == '^' && nglob[1] == separator &&
        nglob[2] == '$')) {
	*r++ = '^';
	*r++ = '\\';
	*r++ = separator;
	*r++ = '\\';
	*r++ = separator;
	*r++ = '$';
    }
    else {
	/*
	 * Do the conversion:
	 *
	 *  ?      -> [^\separator]
	 *  *      -> [^\separator]*
	 *  [!...] -> [^...]
	 *  **     -> .*
	 *
	 * The following are given a leading backslash to protect them
	 * unless they already have a backslash:
	 *
	 *   ( ) { } + . ^ $ |
	 *
	 * If the last
	 * non-escaped character is \ leave it to cause a syntax
	 * error when the regex is compiled.
	 */

	if(*g == '^') {
	    *r++ = '^';
	    *r++ = '\\';	/* escape the separator */
	    *r++ = separator;
	    g++;
	    if(*g == separator) g++;
	}
	else if(*g != separator) {
	    *r++ = '\\';	/* add a leading \separator */
	    *r++ = separator;
	}
	last_ch = '\0';
	for (ch = *g++; ch != '\0'; last_ch = ch, ch = *g++) {
	    next_ch = *g;
	    if (last_ch == '\\') {
		*r++ = ch;
		ch = '\0';		/* so last_ch != '\\' next time */
	    } else if (last_ch == '[' && ch == '!') {
		*r++ = '^';
	    } else if (ch == '\\') {
		*r++ = ch;
	    } else if (ch == '*' || ch == '?') {
		if(ch == '*' && next_ch == '*') {
		    *r++ = '.';
		    g++;
		}
		else {
		    *r++ = '[';
		    *r++ = '^';
		    *r++ = '\\';
		    *r++ = separator;
		    *r++ = ']';
		}
		if (ch == '*') {
		    *r++ = '*';
		}
	    } else if (ch == '$' && next_ch == '\0') {
		if(last_ch != separator) {
		    *r++ = '\\';
		    *r++ = separator;
		}
		*r++ = ch;
	    } else if (   ch == '('
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
	if(last_ch != '\\') {
	    if(last_ch != separator && last_ch != '$') {
		*r++ = '\\';
		*r++ = separator;		/* add a trailing \separator */
	    }
	}
    }
    *r = '\0';

    i = match(regex,nword);

    amfree(nword);
    amfree(nglob);
    amfree(regex);
    return i;
}


int match_host(glob, host)
char *glob, *host;
{
    char *lglob, *lhost;
    char *c, *d;
    int i;

    
    lglob = (char *)alloc(strlen(glob)+1);
    c = lglob, d=glob;
    while( *d != '\0')
	*c++ = tolower(*d++);
    *c = *d;

    lhost = (char *)alloc(strlen(host)+1);
    c = lhost, d=host;
    while( *d != '\0')
	*c++ = tolower(*d++);
    *c = *d;

    i = match_word(lglob, lhost, '.');
    amfree(lglob);
    amfree(lhost);
    return i;
}


int match_disk(glob, disk)
char *glob, *disk;
{
    return match_word(glob, disk, '/');
}

int match_datestamp(dateexp, datestamp)
char *dateexp, *datestamp;
{
    char *dash;
    size_t len, len_suffix;
    int len_prefix;
    char firstdate[100], lastdate[100];
    char mydateexp[100];
    int match_exact;

    if(strlen(dateexp) >= 100 || strlen(dateexp) < 1) {
	error("Illegal datestamp expression %s",dateexp);
    }
   
    if(dateexp[0] == '^') {
	strncpy(mydateexp, dateexp+1, strlen(dateexp)-1); 
	mydateexp[strlen(dateexp)-1] = '\0';
    }
    else {
	strncpy(mydateexp, dateexp, strlen(dateexp));
	mydateexp[strlen(dateexp)] = '\0';
    }

    if(mydateexp[strlen(mydateexp)] == '$') {
	match_exact = 1;
	mydateexp[strlen(mydateexp)] = '\0';
    }
    else
	match_exact = 0;

    if((dash = strchr(mydateexp,'-'))) {
	if(match_exact == 1) {
	    error("Illegal datestamp expression %s",dateexp);
	}
	len = dash - mydateexp;
	len_suffix = strlen(dash) - 1;
	len_prefix = len - len_suffix;

	if(len_prefix < 0) {
	    error("Illegal datestamp expression %s",dateexp);
	}

	dash++;
	strncpy(firstdate, mydateexp, len);
	firstdate[len] = '\0';
	strncpy(lastdate, mydateexp, len_prefix);
	strncpy(&(lastdate[len_prefix]), dash, len_suffix);
	lastdate[len] = '\0';
	return ((strncmp(datestamp, firstdate, strlen(firstdate)) >= 0) &&
		(strncmp(datestamp, lastdate , strlen(lastdate))  <= 0));
    }
    else {
	if(match_exact == 1) {
	    return (strcmp(datestamp, mydateexp) == 0);
	}
	else {
	    return (strncmp(datestamp, mydateexp, strlen(mydateexp)) == 0);
	}
    }
}
