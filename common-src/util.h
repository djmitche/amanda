/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1999 University of Maryland at College Park
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
 * $Id: util.h,v 1.9 2006/05/25 01:47:12 johnfranks Exp $
 */
#ifndef UTIL_H
#define	UTIL_H

#include "amanda.h"

#define BSTRNCMP(a,b)  strncmp(a, b, strlen(b)) 
						  
typedef enum {
    CONF_UNKNOWN,		CONF_ANY,		CONF_COMMA,
    CONF_LBRACE,		CONF_RBRACE,		CONF_NL,
    CONF_END,			CONF_IDENT,		CONF_INT,
    CONF_LONG,			CONF_AM64,		CONF_BOOL,
    CONF_REAL,			CONF_STRING,		CONF_TIME,
    CONF_SIZE,

    /* config parameters */
    CONF_INCLUDEFILE,		CONF_ORG,		CONF_MAILTO,
    CONF_DUMPUSER,		CONF_TAPECYCLE,		CONF_TAPEDEV,
    CONF_CHNGRDEV,		CONF_CHNGRFILE,		CONF_LABELSTR,
    CONF_BUMPPERCENT,		CONF_BUMPSIZE,		CONF_BUMPDAYS,
    CONF_BUMPMULT,		CONF_ETIMEOUT,		CONF_DTIMEOUT,
    CONF_CTIMEOUT,		CONF_TAPEBUFS,		CONF_TAPELIST,
    CONF_DISKFILE,		CONF_INFOFILE,		CONF_LOGDIR,
    CONF_LOGFILE,		CONF_DISKDIR,		CONF_DISKSIZE,
    CONF_INDEXDIR,		CONF_NETUSAGE,		CONF_INPARALLEL,
    CONF_DUMPORDER,		CONF_TIMEOUT,		CONF_TPCHANGER,
    CONF_RUNTAPES,		CONF_DEFINE,		CONF_DUMPTYPE,
    CONF_TAPETYPE,		CONF_INTERFACE,		CONF_PRINTER,
    CONF_AUTOFLUSH,		CONF_RESERVE,		CONF_MAXDUMPSIZE,
    CONF_COLUMNSPEC,		CONF_AMRECOVER_DO_FSF,	CONF_AMRECOVER_CHECK_LABEL,
    CONF_AMRECOVER_CHANGER,	CONF_LABEL_NEW_TAPES,	CONF_USETIMESTAMPS,

    CONF_TAPERALGO,		CONF_FIRST,		CONF_FIRSTFIT,
    CONF_LARGEST,		CONF_LARGESTFIT,	CONF_SMALLEST,
    CONF_LAST,			CONF_DISPLAYUNIT,

    /* kerberos 5 */
    CONF_KRB5KEYTAB,		CONF_KRB5PRINCIPAL,

    /* holding disk */
    CONF_COMMENT,		CONF_DIRECTORY,		CONF_USE,
    CONF_CHUNKSIZE,

    /* dump type */
    /*COMMENT,*/		CONF_PROGRAM,		CONF_DUMPCYCLE,
    CONF_RUNSPERCYCLE,		CONF_MAXCYCLE,		CONF_MAXDUMPS,
    CONF_OPTIONS,		CONF_PRIORITY,		CONF_FREQUENCY,
    CONF_INDEX,			CONF_MAXPROMOTEDAY,	CONF_STARTTIME,
    CONF_COMPRESS,		CONF_ENCRYPT,		CONF_AUTH,
    CONF_STRATEGY,		CONF_ESTIMATE,		CONF_SKIP_INCR,
    CONF_SKIP_FULL,		CONF_RECORD,		CONF_HOLDING,
    CONF_EXCLUDE,		CONF_INCLUDE,		CONF_KENCRYPT,
    CONF_IGNORE,		CONF_COMPRATE,		CONF_TAPE_SPLITSIZE,
    CONF_SPLIT_DISKBUFFER,	CONF_FALLBACK_SPLITSIZE,CONF_SRVCOMPPROG,
    CONF_CLNTCOMPPROG,		CONF_SRV_ENCRYPT,	CONF_CLNT_ENCRYPT,
    CONF_SRV_DECRYPT_OPT,	CONF_CLNT_DECRYPT_OPT,	CONF_AMANDAD_PATH,
    CONF_CLIENT_USERNAME,

    /* tape type */
    /*COMMENT,*/		CONF_BLOCKSIZE,		CONF_FILE_PAD,
    CONF_LBL_TEMPL,		CONF_FILEMARK,		CONF_LENGTH,
    CONF_SPEED,

    /* client conf */
    CONF_CONF,			CONF_INDEX_SERVER,	CONF_TAPE_SERVER,
    CONF_SSH_KEYS,

    /* network interface */
    /* COMMENT, */		/* USE, */

    /* dump options (obsolete) */
    CONF_EXCLUDE_FILE,		CONF_EXCLUDE_LIST,

    /* compress, estimate, encryption */
    CONF_NONE,			CONF_FAST,		CONF_BEST,
    CONF_SERVER,		CONF_CLIENT,		CONF_CALCSIZE,
    CONF_CUSTOM,

    /* priority */
    CONF_LOW,			CONF_MEDIUM,		CONF_HIGH,

    /* dump strategy */
    CONF_SKIP,			CONF_STANDARD,		CONF_NOFULL,
    CONF_NOINC,			CONF_HANOI,		CONF_INCRONLY,

    /* exclude list */
    CONF_LIST,			CONF_EFILE,		CONF_APPEND,
    CONF_OPTIONAL,

    /* numbers */
    CONF_AMINFINITY,		CONF_MULT1,		CONF_MULT7,
    CONF_MULT1K,		CONF_MULT1M,		CONF_MULT1G,

    /* boolean */
    CONF_ATRUE,			CONF_AFALSE,

    CONF_RAWTAPEDEV
} tok_t;

#define BIGINT  INT_MAX

/* internal types and variables */

typedef struct {        /* token table entry */
    char *keyword;
    tok_t token;
} keytab_t;

keytab_t *keytable;

typedef union {
    int		i;
    long	l;
    time_t	t;
    ssize_t	size;
    am64_t	am64;
    double	r;
    char *	s;
} val_t;

extern int	allow_overwrites;
extern int	token_pushed;

extern tok_t	tok, pushed_tok;
extern val_t	tokenval;

extern int	conf_line_num, got_parserror;
extern FILE *	conf_conf;
extern char *	conf_confname;

/* predeclare local functions */

void	get_simple(val_t *var, int *seen, tok_t type);
int	get_int(void);
long	get_long(void);
time_t	get_time(void);
ssize_t	get_size(void);
am64_t	get_am64_t(void);
int	get_bool(void);
void	ckseen(int *seen);
void	conf_parserror(const char *format, ...)
		__attribute__ ((format (printf, 1, 2)));
tok_t	lookup_keyword(char *str);
void	unget_conftoken(void);
void	get_conftoken(tok_t exp);


ssize_t	fullread(int, void *, size_t);
ssize_t	fullwrite(int, const void *, size_t);

int	connect_portrange(struct sockaddr_in *, int, int, char *,
		struct sockaddr_in *, int);
int	bind_portrange(int, struct sockaddr_in *, in_port_t, in_port_t, char *);

char *	construct_datestamp(time_t *t);
char *	construct_timestamp(time_t *t);

/*@only@*//*@null@*/char *quote_string(const char *str);
/*@only@*//*@null@*/char *unquote_string(const char *str);
int	needs_quotes(const char * str);

char *	sanitize_string(const char *str);
char *	strquotedstr(void);
ssize_t	hexdump(const char *buffer, size_t bytes);
void	dump_sockaddr(struct sockaddr_in *	sa);

/*
 *   validate_email return 0 if the following characters are present
 *   * ( ) < > [ ] , ; : ! $ \ / "
 *   else returns 1
 */
int validate_mailto(const char *mailto);

#endif	/* UTIL_H */
