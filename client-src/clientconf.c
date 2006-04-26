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
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/*
 * $Id: clientconf.c,v 1.1 2006/04/26 15:02:04 martinea Exp $
 *
 * read configuration file
 */
/*
 *
 * XXX - I'm not happy *at all* with this implementation, but I don't
 * think YACC would be any easier.  A more table based implementation
 * would be better.  Also clean up memory leaks.
 */
#include "amanda.h"
#include "arglist.h"

#include "clientconf.h"
#include "clock.h"

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#ifndef INT_MAX
#define INT_MAX 2147483647
#endif

#define BIGINT	1000000000		/* 2 million yrs @ 1 per day */

/* internal types and variables */

typedef enum {
    UNKNOWN, ANY, COMMA, LBRACE, RBRACE, NL, END,
    IDENT, INT, LONG, AM64, BOOL, REAL, STRING, TIME,

    /* config parameters */
    INCLUDEFILE,
    CONF, INDEX_SERVER, TAPE_SERVER, TAPEDEV, AUTH, SSH_KEYS,

    /* numbers */
    INFINITY, MULT1, MULT7, MULT1K, MULT1M, MULT1G,

    /* boolean */
    ATRUE, AFALSE
} tok_t;

typedef struct {	/* token table entry */
    char *keyword;
    tok_t token;
} keytab_t;

keytab_t *keytable;

typedef union {
    int i;
    long l;
    am64_t am64;
    double r;
    char *s;
} val_t;

char *config_name = NULL;
char *config_dir = NULL;

/* configuration parameters */

/* strings */
static val_t conf_conf;
static val_t conf_index_server;
static val_t conf_tape_server;
static val_t conf_tapedev;
static val_t conf_auth;
static val_t conf_ssh_keys;

/* ints */

/* reals */

/* other internal variables */

static int seen_conf;
static int seen_index_server;
static int seen_tape_server;
static int seen_tapedev;
static int seen_auth;
static int seen_ssh_keys;

static int allow_overwrites;
static int token_pushed;

static tok_t tok, pushed_tok;
static val_t tokenval;

static int line_num, got_parserror;
static FILE *conf = (FILE *)NULL;
static char *confname = NULL;

/* predeclare local functions */

static void init_defaults P((void));
static void read_conffile_recursively P((char *filename));

static int read_confline P((void));

static void get_simple P((val_t *var, int *seen, tok_t type));
static int get_time P((void));
static int get_int P((void));
static long get_long P((void));
static am64_t get_am64_t P((void));
static int get_bool P((void));
static void ckseen P((int *seen));
static void parserror P((char *format, ...))
    __attribute__ ((format (printf, 1, 2)));
static tok_t lookup_keyword P((char *str));
static void unget_conftoken P((void));
static void get_conftoken P((tok_t exp));

/*
** ------------------------
**  External entry points
** ------------------------
*/

int read_clientconf(filename)
char *filename;
{
    init_defaults();

    /* We assume that confname & conf are initialized to NULL above */
    read_conffile_recursively(filename);

    return got_parserror;
}

struct byname {
    char *name;
    confparm_t parm;
    tok_t typ;
} byname_table [] = {
    { "CONF", CLN_CONF, STRING },
    { "INDEX_SERVER", CLN_INDEX_SERVER, STRING },
    { "TAPE_SERVER", CLN_TAPE_SERVER, STRING },
    { "TAPEDEV", CLN_TAPEDEV, STRING },
    { "AUTH", CLN_AUTH, STRING },
    { "SSH_KEYS", CLN_SSH_KEYS, STRING },
    { NULL }
};

char *client_getconf_byname(str)
char *str;
{
    static char *tmpstr;
    char number[NUM_STR_SIZE];
    struct byname *np;
    char *s;
    char ch;

    tmpstr = stralloc(str);
    s = tmpstr;
    while((ch = *s++) != '\0') {
	if(islower((int)ch)) s[-1] = toupper(ch);
    }

    for(np = byname_table; np->name != NULL; np++)
	if(strcmp(np->name, tmpstr) == 0) break;

    if(np->name == NULL) return NULL;

    if(np->typ == INT) {
	snprintf(number, sizeof(number), "%d", client_getconf_int(np->parm));
	tmpstr = newstralloc(tmpstr, number);
    } else if(np->typ == BOOL) {
	if(client_getconf_int(np->parm) == 0) {
	    tmpstr = newstralloc(tmpstr, "off");
	}
	else {
	    tmpstr = newstralloc(tmpstr, "on");
	}
    } else if(np->typ == REAL) {
	snprintf(number, sizeof(number), "%f", client_getconf_real(np->parm));
	tmpstr = newstralloc(tmpstr, number);
    } else {
	tmpstr = newstralloc(tmpstr, client_getconf_str(np->parm));
    }

    return tmpstr;
}

int client_getconf_seen(parm)
confparm_t parm;
{
    switch(parm) {
    case CLN_CONF: return seen_conf;
    case CLN_INDEX_SERVER: return seen_index_server;
    case CLN_TAPE_SERVER: return seen_tape_server;
    case CLN_TAPEDEV: return seen_tapedev;
    case CLN_AUTH: return seen_auth;
    case CLN_SSH_KEYS: return seen_ssh_keys;
    default: return 0;
    }
}

int client_getconf_int(parm)
confparm_t parm;
{
    int r = 0;

    switch(parm) {
    default:
	error("error [unknown client_getconf_int parm: %d]", parm);
	/* NOTREACHED */
    }
    return r;
}

am64_t client_getconf_am64(parm)
confparm_t parm;
{
    am64_t r = 0;

    switch(parm) {
    default:
	error("error [unknown client_getconf_am64 parm: %d]", parm);
	/* NOTREACHED */
    }
    return r;
}

double client_getconf_real(parm)
confparm_t parm;
{
    double r = 0;

    switch(parm) {
    default:
	error("error [unknown client_getconf_real parm: %d]", parm);
	/* NOTREACHED */
    }
    return r;
}

char *client_getconf_str(parm)
confparm_t parm;
{
    char *r = 0;

    switch(parm) {

    case CLN_CONF: r = conf_conf.s; break;
    case CLN_INDEX_SERVER: r = conf_index_server.s; break;
    case CLN_TAPE_SERVER: r = conf_tape_server.s; break;
    case CLN_TAPEDEV: r = conf_tapedev.s; break;
    case CLN_AUTH: r = conf_auth.s; break;
    case CLN_SSH_KEYS: r = conf_ssh_keys.s; break;

    default:
	error("error [unknown client_getconf_str parm: %d]", parm);
	/* NOTREACHED */
    }
    return r;
}

/*
** ------------------------
**  Internal routines
** ------------------------
*/


static void init_defaults()
{
    char *s;

    /* defaults for exported variables */

#ifdef DEFAULT_CONFIG
    s = DEFAULT_CONFIG;
#else
    s = "";
#endif
    conf_conf.s = stralloc(s);

#ifdef DEFAULT_SERVER
    s = DEFAULT_SERVER;
#else
    s = "";
#endif
    conf_index_server.s = stralloc(s);


#ifdef DEFAULT_TAPE_SERVER
    s = DEFAULT_TAPE_SERVER;
#else
#ifdef DEFAULT_SERVER
    s = DEFAULT_SERVER;
#else
    s = "";
#endif
#endif
    conf_tape_server.s = stralloc(s);

#ifdef DEFAULT_TAPE_DEVICE
    s = DEFAULT_TAPE_DEVICE;
#else
    s = "/dev/nst0";
#endif
    conf_tapedev.s = stralloc(s);

    conf_auth.s = stralloc("bsd");

    conf_ssh_keys.s = stralloc("");

    /* defaults for internal variables */

    seen_conf = 0;
    seen_index_server = 0;
    seen_tape_server = 0;
    seen_tapedev = 0;
    seen_auth = 0;
    seen_ssh_keys = 0;

    line_num = got_parserror = 0;
    allow_overwrites = 0;
    token_pushed = 0;

}

static void read_conffile_recursively(filename)
char *filename;
{
    extern int errno;

    /* Save globals used in read_confline(), elsewhere. */
    int  save_line_num  = line_num;
    FILE *save_conf     = conf;
    char *save_confname = confname;

    if (*filename == '/' || config_dir == NULL) {
	confname = stralloc(filename);
    } else {
	confname = stralloc2(config_dir, filename);
    }

    if((conf = fopen(confname, "r")) == NULL) {
	fprintf(stderr, "could not open conf file \"%s\": %s\n", confname,
		strerror(errno));
	amfree(confname);
	got_parserror = -1;
	return;
    }

    line_num = 0;

    /* read_confline() can invoke us recursively via "includefile" */
    while(read_confline());
    afclose(conf);

    amfree(confname);

    /* Restore globals */
    line_num = save_line_num;
    conf     = save_conf;
    confname = save_confname;
}


/* ------------------------ */


keytab_t main_keytable[] = {
    { "CONF", CONF },
    { "INDEX_SERVER", INDEX_SERVER },
    { "TAPE_SERVER", TAPE_SERVER },
    { "TAPEDEV", TAPEDEV },
    { "AUTH", AUTH },
    { "SSH_KEYS", SSH_KEYS },
    { NULL, IDENT }
};

static int read_confline()
{
    keytable = main_keytable;

    line_num += 1;
    get_conftoken(ANY);
    switch(tok) {
    case INCLUDEFILE:
	{
	    char *fn;

	    get_conftoken(STRING);
	    fn = tokenval.s;
	    read_conffile_recursively(fn);
	}
	break;
    case CONF: get_simple(&conf_conf, &seen_conf, STRING); break;
    case INDEX_SERVER: get_simple(&conf_index_server, &seen_index_server, STRING); break;
    case TAPE_SERVER: get_simple(&conf_tape_server, &seen_tape_server, STRING); break;
    case TAPEDEV:  get_simple(&conf_tapedev, &seen_tapedev, STRING); break;
    case AUTH:     get_simple(&conf_auth, &seen_auth, STRING); break;
    case SSH_KEYS: get_simple(&conf_ssh_keys, &seen_ssh_keys, STRING); break;

    case NL:	/* empty line */
	break;
    case END:	/* end of file */
	return 0;
    default:
	parserror("configuration keyword expected");
    }
    if(tok != NL) get_conftoken(NL);
    return 1;
}














/* ------------------------ */


static void get_simple(var, seen, type)
val_t *var;
int *seen;
tok_t type;
{
    ckseen(seen);

    switch(type) {
    case STRING:
    case IDENT:
	get_conftoken(type);
	var->s = newstralloc(var->s, tokenval.s);
	malloc_mark(var->s);
	break;
    case INT:
	var->i = get_int();
	break;
    case LONG:
	var->l = get_long();
	break;
    case AM64:
	var->am64 = get_am64_t();
	break;
    case BOOL:
	var->i = get_bool();
	break;
    case REAL:
	get_conftoken(REAL);
	var->r = tokenval.r;
	break;
    case TIME:
	var->i = get_time();
	break;
    default:
	error("error [unknown get_simple type: %d]", type);
	/* NOTREACHED */
    }
    return;
}

static int get_time()
{
    time_t st = start_time.r.tv_sec;
    struct tm *stm;
    int hhmm;

    get_conftoken(INT);
    hhmm = tokenval.i;

    stm = localtime(&st);
    st -= stm->tm_sec + 60 * (stm->tm_min + 60 * stm->tm_hour);
    st += ((hhmm/100*60) + hhmm%100)*60;

    if (st-start_time.r.tv_sec<-43200)
	st += 86400;

    return st;
}

keytab_t numb_keytable[] = {
    { "B", MULT1 },
    { "BPS", MULT1 },
    { "BYTE", MULT1 },
    { "BYTES", MULT1 },
    { "DAY", MULT1 },
    { "DAYS", MULT1 },
    { "INF", INFINITY },
    { "K", MULT1K },
    { "KB", MULT1K },
    { "KBPS", MULT1K },
    { "KBYTE", MULT1K },
    { "KBYTES", MULT1K },
    { "KILOBYTE", MULT1K },
    { "KILOBYTES", MULT1K },
    { "KPS", MULT1K },
    { "M", MULT1M },
    { "MB", MULT1M },
    { "MBPS", MULT1M },
    { "MBYTE", MULT1M },
    { "MBYTES", MULT1M },
    { "MEG", MULT1M },
    { "MEGABYTE", MULT1M },
    { "MEGABYTES", MULT1M },
    { "G", MULT1G },
    { "GB", MULT1G },
    { "GBPS", MULT1G },
    { "GBYTE", MULT1G },
    { "GBYTES", MULT1G },
    { "GIG", MULT1G },
    { "GIGABYTE", MULT1G },
    { "GIGABYTES", MULT1G },
    { "MPS", MULT1M },
    { "TAPE", MULT1 },
    { "TAPES", MULT1 },
    { "WEEK", MULT7 },
    { "WEEKS", MULT7 },
    { NULL, IDENT }
};

static int get_int()
{
    int val;
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = numb_keytable;

    get_conftoken(ANY);

    switch(tok) {
    case AM64:
	if(abs(tokenval.am64) > INT_MAX)
	    parserror("value too large");
	val = (int) tokenval.am64;
	break;
    case INFINITY:
	val = (int) BIGINT;
	break;
    default:
	parserror("an integer expected");
	val = 0;
    }

    /* get multiplier, if any */
    get_conftoken(ANY);

    switch(tok) {
    case NL:			/* multiply by one */
    case MULT1:
    case MULT1K:
	break;
    case MULT7:
	if(abs(val) > INT_MAX/7)
	    parserror("value too large");
	val *= 7;
	break;
    case MULT1M:
	if(abs(val) > INT_MAX/1024)
	    parserror("value too large");
	val *= 1024;
	break;
    case MULT1G:
	if(abs(val) > INT_MAX/(1024*1024))
	    parserror("value too large");
	val *= 1024*1024;
	break;
    default:	/* it was not a multiplier */
	unget_conftoken();
    }

    keytable = save_kt;

    return val;
}

static long get_long()
{
    long val;
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = numb_keytable;

    get_conftoken(ANY);

    switch(tok) {
    case AM64:
	if(tokenval.am64 > LONG_MAX || tokenval.am64 < LONG_MIN)
	    parserror("value too large");
	val = (long) tokenval.am64;
	break;
    case INFINITY:
	val = (long) LONG_MAX;
	break;
    default:
	parserror("a long expected");
	val = 0;
    }

    /* get multiplier, if any */
    get_conftoken(ANY);

    switch(tok) {
    case NL:			/* multiply by one */
    case MULT1:
    case MULT1K:
	break;
    case MULT7:
	if(val > LONG_MAX/7 || val < LONG_MIN/7)
	    parserror("value too large");
	val *= 7;
	break;
    case MULT1M:
	if(val > LONG_MAX/1024 || val < LONG_MIN/7)
	    parserror("value too large");
	val *= 1024;
	break;
    case MULT1G:
	if(val > LONG_MAX/(1024*1024) || val < LONG_MIN/(1024*1024))
	    parserror("value too large");
	val *= 1024*1024;
	break;
    default:	/* it was not a multiplier */
	unget_conftoken();
    }

    keytable = save_kt;

    return val;
}

static am64_t get_am64_t()
{
    am64_t val;
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = numb_keytable;

    get_conftoken(ANY);

    switch(tok) {
    case AM64:
	val = tokenval.am64;
	break;
    case INFINITY:
	val = AM64_MAX;
	break;
    default:
	parserror("a am64 expected %d", tok);
	val = 0;
    }

    /* get multiplier, if any */
    get_conftoken(ANY);

    switch(tok) {
    case NL:			/* multiply by one */
    case MULT1:
    case MULT1K:
	break;
    case MULT7:
	if(val > AM64_MAX/7 || val < AM64_MIN/7)
	    parserror("value too large");
	val *= 7;
	break;
    case MULT1M:
	if(val > AM64_MAX/1024 || val < AM64_MIN/1024)
	    parserror("value too large");
	val *= 1024;
	break;
    case MULT1G:
	if(val > AM64_MAX/(1024*1024) || val < AM64_MIN/(1024*1024))
	    parserror("value too large");
	val *= 1024*1024;
	break;
    default:	/* it was not a multiplier */
	unget_conftoken();
    }

    keytable = save_kt;

    return val;
}

keytab_t bool_keytable[] = {
    { "Y", ATRUE },
    { "YES", ATRUE },
    { "T", ATRUE },
    { "TRUE", ATRUE },
    { "ON", ATRUE },
    { "N", AFALSE },
    { "NO", AFALSE },
    { "F", AFALSE },
    { "FALSE", AFALSE },
    { "OFF", AFALSE },
    { NULL, IDENT }
};

static int get_bool()
{
    int val;
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = bool_keytable;

    get_conftoken(ANY);

    switch(tok) {
    case INT:
	val = tokenval.i ? 1 : 0;
	break;
    case ATRUE:
	val = 1;
	break;
    case AFALSE:
	val = 0;
	break;
    case NL:
    default:
	unget_conftoken();
	val = 2; /* no argument - most likely TRUE */
    }

    keytable = save_kt;

    return val;
}

static void ckseen(seen)
int *seen;
{
    if(*seen && !allow_overwrites) {
	parserror("duplicate parameter, prev def on line %d", *seen);
    }
    *seen = line_num;
}

printf_arglist_function(static void parserror, char *, format)
{
    va_list argp;

    /* print error message */

    fprintf(stderr, "\"%s\", line %d: ", confname, line_num);
    arglist_start(argp, format);
    vfprintf(stderr, format, argp);
    arglist_end(argp);
    fputc('\n', stderr);

    got_parserror = 1;
}

static tok_t lookup_keyword(str)
char *str;
{
    keytab_t *kwp;

    /* switch to binary search if performance warrants */

    for(kwp = keytable; kwp->keyword != NULL; kwp++) {
	if(strcmp(kwp->keyword, str) == 0) break;
    }
    return kwp->token;
}

static char tkbuf[4096];

/* push the last token back (can only unget ANY tokens) */
static void unget_conftoken()
{
    token_pushed = 1;
    pushed_tok = tok;
    tok = UNKNOWN;
    return;
}

static void get_conftoken(exp)
tok_t exp;
{
    int ch, d;
    am64_t am64;
    char *buf;
    int token_overflow;

    if(token_pushed) {
	token_pushed = 0;
	tok = pushed_tok;

	/* If it looked like a key word before then look it
	** up again in the current keyword table. */
	switch(tok) {
	case LONG:    case AM64:
	case INT:     case REAL:    case STRING:
	case LBRACE:  case RBRACE:  case COMMA:
	case NL:      case END:     case UNKNOWN:
	    break;
	default:
	    if(exp == IDENT) tok = IDENT;
	    else tok = lookup_keyword(tokenval.s);
	}
    }
    else {
	ch = getc(conf);

	while(ch != EOF && ch != '\n' && isspace(ch)) ch = getc(conf);
	if(ch == '#') {		/* comment - eat everything but eol/eof */
	    while((ch = getc(conf)) != EOF && ch != '\n') {}
	}

	if(isalpha(ch)) {		/* identifier */
	    buf = tkbuf;
	    token_overflow = 0;
	    do {
		if(islower(ch)) ch = toupper(ch);
		if(buf < tkbuf+sizeof(tkbuf)-1) {
		    *buf++ = ch;
		} else {
		    *buf = '\0';
		    if(!token_overflow) {
			parserror("token too long: %.20s...", tkbuf);
		    }
		    token_overflow = 1;
		}
		ch = getc(conf);
	    } while(isalnum(ch) || ch == '_' || ch == '-');

	    ungetc(ch, conf);
	    *buf = '\0';

	    tokenval.s = tkbuf;

	    if(token_overflow) tok = UNKNOWN;
	    else if(exp == IDENT) tok = IDENT;
	    else tok = lookup_keyword(tokenval.s);
	}
	else if(isdigit(ch)) {	/* integer */
	    int sign;
	    if (1) {
		sign = 1;
	    } else {
	    negative_number: /* look for goto negative_number below */
		sign = -1;
	    }
	    tokenval.am64 = 0;
	    do {
		tokenval.am64 = tokenval.am64 * 10 + (ch - '0');
		ch = getc(conf);
	    } while(isdigit(ch));
	    if(ch != '.') {
		if(exp == INT) {
		    tok = INT;
		    tokenval.i *= sign;
		}
		else if(exp == LONG) {
		    tok = LONG;
		    tokenval.l *= sign;
		}
		else if(exp != REAL) {
		    tok = AM64;
		    tokenval.am64 *= sign;
		} else {
		    /* automatically convert to real when expected */
		    am64 = tokenval.am64;
		    tokenval.r = sign * (double) am64;
		    tok = REAL;
		}
	    }
	    else {
		/* got a real number, not an int */
		am64 = tokenval.am64;
		tokenval.r = sign * (double) am64;
		am64=0; d=1;
		ch = getc(conf);
		while(isdigit(ch)) {
		    am64 = am64 * 10 + (ch - '0');
		    d = d * 10;
		    ch = getc(conf);
		}
		tokenval.r += sign * ((double)am64)/d;
		tok = REAL;
	    }
	    ungetc(ch,conf);
	}
	else switch(ch) {

	case '"':			/* string */
	    buf = tkbuf;
	    token_overflow = 0;
	    ch = getc(conf);
	    while(ch != '"' && ch != '\n' && ch != EOF) {
		if(buf < tkbuf+sizeof(tkbuf)-1) {
		    *buf++ = ch;
		} else {
		    *buf = '\0';
		    if(!token_overflow) {
			parserror("string too long: %.20s...", tkbuf);
		    }
		    token_overflow = 1;
		}
		ch = getc(conf);
	    }
	    if(ch != '"') {
		parserror("missing end quote");
		ungetc(ch, conf);
	    }
	    *buf = '\0';
	    tokenval.s = tkbuf;
	    if(token_overflow) tok = UNKNOWN;
	    else tok = STRING;
	    break;

	case '-':
	    ch = getc(conf);
	    if (isdigit(ch))
		goto negative_number;
	    else {
		ungetc(ch, conf);
		tok = UNKNOWN;
	    }
	    break;
	case ',':  tok = COMMA; break;
	case '{':  tok = LBRACE; break;
	case '}':  tok = RBRACE; break;
	case '\n': tok = NL; break;
	case EOF:  tok = END; break;
	default:   tok = UNKNOWN;
	}
    }

    if(exp != ANY && tok != exp) {
	char *str;
	keytab_t *kwp;

	switch(exp) {
	case LBRACE: str = "\"{\""; break;
	case RBRACE: str = "\"}\""; break;
	case COMMA:  str = "\",\""; break;

	case NL: str = "end of line"; break;
	case END: str = "end of file"; break;
	case INT: str = "an integer"; break;
	case REAL: str = "a real number"; break;
	case STRING: str = "a quoted string"; break;
	case IDENT: str = "an identifier"; break;
	default:
	    for(kwp = keytable; kwp->keyword != NULL; kwp++)
		if(exp == kwp->token) break;
	    if(kwp->keyword == NULL) str = "token not";
	    else str = kwp->keyword;
	}
	parserror("%s expected", str);
	tok = exp;
	if(tok == INT) tokenval.i = 0;
	else tokenval.s = "";
    }

    return;
}

/* ------------------------ */


#ifdef TEST

void
dump_configuration(filename)
    char *filename;
{
    printf("AMANDA CLIENT CONFIGURATION FROM FILE \"%s\":\n\n", filename);

    printf("conf_conf = \"%s\"\n", client_getconf_str(CLN_CONF));
    printf("conf_index_server = \"%s\"\n", client_getconf_str(CLN_INDEX_SERVER));
    printf("conf_tape_server = \"%s\"\n", client_getconf_str(CLN_TAPE_SERVER));
    printf("conf_tapedev = \"%s\"\n", client_getconf_str(CLN_TAPEDEV));
    printf("conf_auth = \"%s\"\n", client_getconf_str(CLN_AUTH));
    printf("conf_ssh_keys = \"%s\"\n", client_getconf_str(CLN_SSH_KEYS));
}

int
main(argc, argv)
    int argc;
    char *argv[];
{
  char *conffile;
  char *diskfile;
  disklist_t lst;
  int result;
  unsigned long malloc_hist_1, malloc_size_1;
  unsigned long malloc_hist_2, malloc_size_2;

  safe_fd(-1, 0);

  set_pname("conffile");

  /* Don't die when child closes pipe */
  signal(SIGPIPE, SIG_IGN);

  malloc_size_1 = malloc_inuse(&malloc_hist_1);

  startclock();

  if (argc > 1) {
    if (argv[1][0] == '/') {
      config_dir = stralloc(argv[1]);
      config_name = strrchr(config_dir, '/') + 1;
      config_name[-1] = '\0';
      config_dir = newstralloc2(config_dir, config_dir, "/");
    } else {
      config_name = stralloc(argv[1]);
      config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
    }
  } else {
    char my_cwd[STR_SIZE];

    if (getcwd(my_cwd, sizeof(my_cwd)) == NULL) {
      error("cannot determine current working directory");
    }
    config_dir = stralloc2(my_cwd, "/");
    if ((config_name = strrchr(my_cwd, '/')) != NULL) {
      config_name = stralloc(config_name + 1);
    }
  }

  conffile = stralloc2(config_dir, CONFFILE_NAME);
  result = read_conffile(conffile);
  if (result == 0) {
      diskfile = client_getconf_str(CNF_DISKFILE);
      if (diskfile != NULL && access(diskfile, R_OK) == 0) {
	  result = read_diskfile(diskfile, &lst);
      }
  }
  dump_configuration(CONFFILE_NAME);
  amfree(conffile);

  malloc_size_2 = malloc_inuse(&malloc_hist_2);

  if(malloc_size_1 != malloc_size_2) {
    malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
  }

  return result;
}

#endif /* TEST */

char *
generic_client_get_security_conf(string, arg)
	char *string;
	void *arg;
{
	if(!string || !*string)
		return(NULL);

	if(strcmp(string, "conf")==0) {
		return(client_getconf_str(CLN_CONF));
	} else if(strcmp(string, "index_server")==0) {
		return(client_getconf_str(CLN_INDEX_SERVER));
	} else if(strcmp(string, "tape_server")==0) {
		return(client_getconf_str(CLN_TAPE_SERVER));
	} else if(strcmp(string, "tapedev")==0) {
		return(client_getconf_str(CLN_TAPEDEV));
	} else if(strcmp(string, "auth")==0) {
		return(client_getconf_str(CLN_AUTH));
	} else if(strcmp(string, "ssh_keys")==0) {
		return(client_getconf_str(CLN_SSH_KEYS));
/*
	} else if(strcmp(string, "krb5principal")==0) {
		return(client_getconf_str(CNF_KRB5PRINCIPAL));
	} else if(strcmp(string, "krb5keytab")==0) {
		return(client_getconf_str(CNF_KRB5KEYTAB));
*/
	}
	return(NULL);
}
