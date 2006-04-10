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
 * $Id: conffile.c,v 1.129 2006/04/10 11:22:24 martinea Exp $
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

#include "conffile.h"
#include "diskfile.h"
#include "driverio.h"
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
    ORG, MAILTO, DUMPUSER,
    TAPECYCLE, TAPEDEV, CHNGRDEV, CHNGRFILE, LABELSTR,
    BUMPPERCENT, BUMPSIZE, BUMPDAYS, BUMPMULT, ETIMEOUT, DTIMEOUT, CTIMEOUT,
    TAPEBUFS, TAPELIST, DISKFILE, INFOFILE, LOGDIR, LOGFILE,
    DISKDIR, DISKSIZE, INDEXDIR, NETUSAGE, INPARALLEL, DUMPORDER, TIMEOUT,
    TPCHANGER, RUNTAPES,
    DEFINE, DUMPTYPE, TAPETYPE, INTERFACE,
    PRINTER, AUTOFLUSH, RESERVE, MAXDUMPSIZE,
    COLUMNSPEC, 
    AMRECOVER_DO_FSF, AMRECOVER_CHECK_LABEL, AMRECOVER_CHANGER,
    LABEL_NEW_TAPES, USETIMESTAMPS,

    TAPERALGO, FIRST, FIRSTFIT, LARGEST, LARGESTFIT, SMALLEST, LAST,
    DISPLAYUNIT,

    /* kerberos 5 */
    KRB5KEYTAB, KRB5PRINCIPAL, 

    /* holding disk */
    COMMENT, DIRECTORY, USE, CHUNKSIZE,

    /* dump type */
    /*COMMENT,*/ PROGRAM, DUMPCYCLE, RUNSPERCYCLE, MAXCYCLE, MAXDUMPS,
    OPTIONS, PRIORITY, FREQUENCY, INDEX, MAXPROMOTEDAY,
    STARTTIME, COMPRESS, ENCRYPT, AUTH, STRATEGY, ESTIMATE,
    SKIP_INCR, SKIP_FULL, RECORD, HOLDING,
    EXCLUDE, INCLUDE, KENCRYPT, IGNORE, COMPRATE, TAPE_SPLITSIZE,
    SPLIT_DISKBUFFER, FALLBACK_SPLITSIZE, SRVCOMPPROG, CLNTCOMPPROG,
    SRV_ENCRYPT, CLNT_ENCRYPT, SRV_DECRYPT_OPT, CLNT_DECRYPT_OPT,
    AMANDAD_PATH, CLIENT_USERNAME,

    /* tape type */
    /*COMMENT,*/ BLOCKSIZE, FILE_PAD, LBL_TEMPL, FILEMARK, LENGTH, SPEED,

    /* network interface */
    /*COMMENT, USE,*/

    /* dump options (obsolete) */
    EXCLUDE_FILE, EXCLUDE_LIST,

    /* compress, estimate, encryption */
    NONE, FAST, BEST, SERVER, CLIENT, CALCSIZE, CUSTOM,

    /* priority */
    LOW, MEDIUM, HIGH,

    /* dump strategy */
    SKIP, STANDARD, NOFULL, NOINC, HANOI, INCRONLY,

    /* exclude list */
    LIST, EFILE, APPEND, OPTIONAL,

    /* numbers */
    INFINITY, MULT1, MULT7, MULT1K, MULT1M, MULT1G,

    /* boolean */
    ATRUE, AFALSE,

    RAWTAPEDEV
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

/* this corresponds to the normal output of amanda, but may
 * be adapted to any spacing as you like.
 */
ColumnInfo ColumnData[] = {
    { "HostName",   0, 12, 12, 0, "%-*.*s", "HOSTNAME" },
    { "Disk",       1, 11, 11, 0, "%-*.*s", "DISK" },
    { "Level",      1, 1,  1,  0, "%*.*d",  "L" },
    { "OrigKB",     1, 7,  0,  0, "%*.*f",  "ORIG-KB" },
    { "OutKB",      1, 7,  0,  0, "%*.*f",  "OUT-KB" },
    { "Compress",   1, 6,  1,  0, "%*.*f",  "COMP%" },
    { "DumpTime",   1, 7,  7,  0, "%*.*s",  "MMM:SS" },
    { "DumpRate",   1, 6,  1,  0, "%*.*f",  "KB/s" },
    { "TapeTime",   1, 6,  6,  0, "%*.*s",  "MMM:SS" },
    { "TapeRate",   1, 6,  1,  0, "%*.*f",  "KB/s" },
    { NULL,         0, 0,  0,  0, NULL,     NULL }
};

char *config_name = NULL;
char *config_dir = NULL;

/* visible holding disk variables */

holdingdisk_t *holdingdisks;
int num_holdingdisks;

long int unit_divisor = 1;

/* configuration parameters */

/* strings */
static val_t conf_org;
static val_t conf_mailto;
static val_t conf_dumpuser;
static val_t conf_printer;
static val_t conf_tapedev;
static val_t conf_rawtapedev;
static val_t conf_tpchanger;
static val_t conf_chngrdev;
static val_t conf_chngrfile;
static val_t conf_labelstr;
static val_t conf_tapelist;
static val_t conf_infofile;
static val_t conf_logdir;
static val_t conf_diskfile;
static val_t conf_diskdir;
static val_t conf_tapetype;
static val_t conf_indexdir;
static val_t conf_columnspec;
static val_t conf_dumporder;
static val_t conf_amrecover_changer;

/* ints */
static val_t conf_dumpcycle;
static val_t conf_runspercycle;
static val_t conf_maxcycle;
static val_t conf_tapecycle;
static val_t conf_runtapes;
static val_t conf_disksize;
static val_t conf_netusage;
static val_t conf_inparallel;
static val_t conf_timeout;
static val_t conf_maxdumps;
static val_t conf_bumppercent;
static val_t conf_bumpsize;
static val_t conf_bumpdays;
static val_t conf_etimeout;
static val_t conf_dtimeout;
static val_t conf_ctimeout;
static val_t conf_tapebufs;
static val_t conf_autoflush;
static val_t conf_reserve;
static val_t conf_maxdumpsize;
static val_t conf_amrecover_do_fsf;
static val_t conf_amrecover_check_label;
static val_t conf_taperalgo;
static val_t conf_displayunit;
static val_t conf_krb5keytab;
static val_t conf_krb5principal;
static val_t conf_label_new_tapes;
static val_t conf_usetimestamps;

/* reals */
static val_t conf_bumpmult;

/* other internal variables */
static holdingdisk_t hdcur;

static tapetype_t tpcur;

static dumptype_t dpcur;

static interface_t ifcur;

static int seen_org;
static int seen_mailto;
static int seen_dumpuser;
static int seen_rawtapedev;
static int seen_printer;
static int seen_tapedev;
static int seen_tpchanger;
static int seen_chngrdev;
static int seen_chngrfile;
static int seen_labelstr;
static int seen_runtapes;
static int seen_maxdumps;
static int seen_tapelist;
static int seen_infofile;
static int seen_diskfile;
static int seen_diskdir;
static int seen_logdir;
static int seen_bumppercent;
static int seen_bumpsize;
static int seen_bumpmult;
static int seen_bumpdays;
static int seen_tapetype;
static int seen_dumpcycle;
static int seen_runspercycle;
static int seen_maxcycle;
static int seen_tapecycle;
static int seen_disksize;
static int seen_netusage;
static int seen_inparallel;
static int seen_dumporder;
static int seen_timeout;
static int seen_indexdir;
static int seen_etimeout;
static int seen_dtimeout;
static int seen_ctimeout;
static int seen_tapebufs;
static int seen_autoflush;
static int seen_reserve;
static int seen_maxdumpsize;
static int seen_columnspec;
static int seen_amrecover_do_fsf;
static int seen_amrecover_check_label;
static int seen_amrecover_changer;
static int seen_taperalgo;
static int seen_displayunit;
static int seen_krb5keytab;
static int seen_krb5principal;
static int seen_label_new_tapes;
static int seen_usetimestamps;

static int allow_overwrites;
static int token_pushed;

static tok_t tok, pushed_tok;
static val_t tokenval;

static int line_num, got_parserror;
static dumptype_t *dumplist = NULL;
static tapetype_t *tapelist = NULL;
static interface_t *interface_list = NULL;
static FILE *conf = (FILE *)NULL;
static char *confname = NULL;

/* predeclare local functions */

static void init_defaults P((void));
static void read_conffile_recursively P((char *filename));

static int read_confline P((void));
static void get_holdingdisk P((void));
static void init_holdingdisk_defaults P((void));
static void save_holdingdisk P((void));
static void get_dumptype P((void));
static void init_dumptype_defaults P((void));
static void save_dumptype P((void));
static void copy_dumptype P((void));
static void get_tapetype P((void));
static void init_tapetype_defaults P((void));
static void save_tapetype P((void));
static void copy_tapetype P((void));
static void get_interface P((void));
static void init_interface_defaults P((void));
static void save_interface P((void));
static void copy_interface P((void));
static void get_dumpopts P((void));
static void get_comprate P((void));
static void get_compress P((void));
static void get_encrypt P((void));
static void get_priority P((void));
static void get_strategy P((void));
static void get_estimate P((void));
static void get_exclude P((void));
static void get_include P((void));
static void get_taperalgo P((val_t *c_taperalgo, int *s_taperalgo));

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

int read_conffile(filename)
char *filename;
{
    interface_t *ip;

    init_defaults();

    /* We assume that confname & conf are initialized to NULL above */
    read_conffile_recursively(filename);

    if(got_parserror != -1 ) {
	if(lookup_tapetype(conf_tapetype.s) == NULL) {
	    char *save_confname = confname;

	    confname = filename;
	    if(!seen_tapetype)
		parserror("default tapetype %s not defined", conf_tapetype.s);
	    else {
		line_num = seen_tapetype;
		parserror("tapetype %s not defined", conf_tapetype.s);
	    }
	    confname = save_confname;
	}
    }

    ip = alloc(sizeof(interface_t));
    malloc_mark(ip);
    ip->name = "";
    ip->seen = seen_netusage;
    ip->comment = "implicit from NETUSAGE";
    ip->maxusage = conf_netusage.i;
    ip->curusage = 0;
    ip->next = interface_list;
    interface_list = ip;

    return got_parserror;
}

struct byname {
    char *name;
    confparm_t parm;
    tok_t typ;
} byname_table [] = {
    { "ORG", CNF_ORG, STRING },
    { "MAILTO", CNF_MAILTO, STRING },
    { "DUMPUSER", CNF_DUMPUSER, STRING },
    { "PRINTER", CNF_PRINTER, STRING },
    { "TAPEDEV", CNF_TAPEDEV, STRING },
    { "TPCHANGER", CNF_TPCHANGER, STRING },
    { "CHANGERDEV", CNF_CHNGRDEV, STRING },
    { "CHANGERFILE", CNF_CHNGRFILE, STRING },
    { "LABELSTR", CNF_LABELSTR, STRING },
    { "TAPELIST", CNF_TAPELIST, STRING },
    { "DISKFILE", CNF_DISKFILE, STRING },
    { "INFOFILE", CNF_INFOFILE, STRING },
    { "LOGDIR", CNF_LOGDIR, STRING },
    /*{ "LOGFILE", CNF_LOGFILE, STRING },*/
    /*{ "DISKDIR", CNF_DISKDIR, STRING },*/
    { "INDEXDIR", CNF_INDEXDIR, STRING },
    { "TAPETYPE", CNF_TAPETYPE, STRING },
    { "DUMPCYCLE", CNF_DUMPCYCLE, INT },
    { "RUNSPERCYCLE", CNF_RUNSPERCYCLE, INT },
    { "MINCYCLE",  CNF_DUMPCYCLE, INT },
    { "RUNTAPES",   CNF_RUNTAPES, INT },
    { "TAPECYCLE", CNF_TAPECYCLE, INT },
    /*{ "DISKSIZE", CNF_DISKSIZE, INT },*/
    { "BUMPDAYS", CNF_BUMPDAYS, INT },
    { "BUMPSIZE", CNF_BUMPSIZE, INT },
    { "BUMPPERCENT", CNF_BUMPPERCENT, INT },
    { "BUMPMULT", CNF_BUMPMULT, REAL },
    { "NETUSAGE", CNF_NETUSAGE, INT },
    { "INPARALLEL", CNF_INPARALLEL, INT },
    { "DUMPORDER", CNF_DUMPORDER, STRING },
    /*{ "TIMEOUT", CNF_TIMEOUT, INT },*/
    { "MAXDUMPS", CNF_MAXDUMPS, INT },
    { "ETIMEOUT", CNF_ETIMEOUT, INT },
    { "DTIMEOUT", CNF_DTIMEOUT, INT },
    { "CTIMEOUT", CNF_CTIMEOUT, INT },
    { "TAPEBUFS", CNF_TAPEBUFS, INT },
    { "RAWTAPEDEV", CNF_RAWTAPEDEV, STRING },
    { "COLUMNSPEC", CNF_COLUMNSPEC, STRING },
    { "AMRECOVER_DO_FSF", CNF_AMRECOVER_DO_FSF, BOOL },
    { "AMRECOVER_CHECK_LABEL", CNF_AMRECOVER_CHECK_LABEL, BOOL },
    { "AMRECOVER_CHANGER", CNF_AMRECOVER_CHANGER, STRING },
    { "TAPERALGO", CNF_TAPERALGO, INT },
    { "DISPLAYUNIT", CNF_DISPLAYUNIT, STRING },
    { "AUTOFLUSH", CNF_AUTOFLUSH, BOOL },
    { "RESERVE", CNF_RESERVE, INT },
    { "MAXDUMPSIZE", CNF_MAXDUMPSIZE, AM64 },
    { "KRB5KEYTAB", CNF_KRB5KEYTAB, STRING },
    { "KRB5PRINCIPAL", CNF_KRB5PRINCIPAL, STRING },
    { "LABEL_NEW_TAPES", CNF_LABEL_NEW_TAPES, STRING },
    { "USETIMESTAMPS", CNF_USETIMESTAMPS, BOOL },
    { NULL }
};

char *getconf_byname(str)
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
	snprintf(number, sizeof(number), "%d", getconf_int(np->parm));
	tmpstr = newstralloc(tmpstr, number);
    } else if(np->typ == BOOL) {
	if(getconf_int(np->parm) == 0) {
	    tmpstr = newstralloc(tmpstr, "off");
	}
	else {
	    tmpstr = newstralloc(tmpstr, "on");
	}
    } else if(np->typ == REAL) {
	snprintf(number, sizeof(number), "%f", getconf_real(np->parm));
	tmpstr = newstralloc(tmpstr, number);
    } else {
	tmpstr = newstralloc(tmpstr, getconf_str(np->parm));
    }

    return tmpstr;
}

int getconf_seen(parm)
confparm_t parm;
{
    switch(parm) {
    case CNF_ORG: return seen_org;
    case CNF_MAILTO: return seen_mailto;
    case CNF_DUMPUSER: return seen_dumpuser;
    case CNF_PRINTER: return seen_printer;
    case CNF_TAPEDEV: return seen_tapedev;
    case CNF_TPCHANGER: return seen_tpchanger;
    case CNF_CHNGRDEV: return seen_chngrdev;
    case CNF_CHNGRFILE: return seen_chngrfile;
    case CNF_LABELSTR: return seen_labelstr;
    case CNF_RUNTAPES: return seen_runtapes;
    case CNF_MAXDUMPS: return seen_maxdumps;
    case CNF_TAPELIST: return seen_tapelist;
    case CNF_INFOFILE: return seen_infofile;
    case CNF_DISKFILE: return seen_diskfile;
    /*case CNF_DISKDIR: return seen_diskdir;*/
    case CNF_LOGDIR: return seen_logdir;
    /*case CNF_LOGFILE: return seen_logfile;*/
    case CNF_BUMPPERCENT: return seen_bumppercent;
    case CNF_BUMPSIZE: return seen_bumpsize;
    case CNF_BUMPMULT: return seen_bumpmult;
    case CNF_BUMPDAYS: return seen_bumpdays;
    case CNF_TAPETYPE: return seen_tapetype;
    case CNF_DUMPCYCLE: return seen_dumpcycle;
    case CNF_RUNSPERCYCLE: return seen_runspercycle;
    /*case CNF_MAXCYCLE: return seen_maxcycle;*/
    case CNF_TAPECYCLE: return seen_tapecycle;
    /*case CNF_DISKSIZE: return seen_disksize;*/
    case CNF_NETUSAGE: return seen_netusage;
    case CNF_INPARALLEL: return seen_inparallel;
    case CNF_DUMPORDER: return seen_dumporder;
    /*case CNF_TIMEOUT: return seen_timeout;*/
    case CNF_INDEXDIR: return seen_indexdir;
    case CNF_ETIMEOUT: return seen_etimeout;
    case CNF_DTIMEOUT: return seen_dtimeout;
    case CNF_CTIMEOUT: return seen_ctimeout;
    case CNF_TAPEBUFS: return seen_tapebufs;
    case CNF_RAWTAPEDEV: return seen_rawtapedev;
    case CNF_AUTOFLUSH: return seen_autoflush;
    case CNF_RESERVE: return seen_reserve;
    case CNF_MAXDUMPSIZE: return seen_maxdumpsize;
    case CNF_AMRECOVER_DO_FSF: return seen_amrecover_do_fsf;
    case CNF_AMRECOVER_CHECK_LABEL: return seen_amrecover_check_label;
    case CNF_AMRECOVER_CHANGER: return seen_amrecover_changer;
    case CNF_TAPERALGO: return seen_taperalgo;
    case CNF_DISPLAYUNIT: return seen_displayunit;
    case CNF_KRB5KEYTAB: return seen_krb5keytab;
    case CNF_KRB5PRINCIPAL: return seen_krb5principal;
    case CNF_LABEL_NEW_TAPES: return seen_label_new_tapes;
    case CNF_USETIMESTAMPS: return seen_usetimestamps;
    default: return 0;
    }
}

int getconf_int(parm)
confparm_t parm;
{
    int r = 0;

    switch(parm) {

    case CNF_DUMPCYCLE: r = conf_dumpcycle.i; break;
    case CNF_RUNSPERCYCLE: r = conf_runspercycle.i; break;
    case CNF_TAPECYCLE: r = conf_tapecycle.i; break;
    case CNF_RUNTAPES: r = conf_runtapes.i; break;
    /*case CNF_DISKSIZE: r = conf_disksize.i; break;*/
    case CNF_BUMPPERCENT: r = conf_bumppercent.i; break;
    case CNF_BUMPSIZE: r = conf_bumpsize.i; break;
    case CNF_BUMPDAYS: r = conf_bumpdays.i; break;
    case CNF_NETUSAGE: r = conf_netusage.i; break;
    case CNF_INPARALLEL: r = conf_inparallel.i; break;
    /*case CNF_TIMEOUT: r = conf_timeout.i; break;*/
    case CNF_MAXDUMPS: r = conf_maxdumps.i; break;
    case CNF_ETIMEOUT: r = conf_etimeout.i; break;
    case CNF_DTIMEOUT: r = conf_dtimeout.i; break;
    case CNF_CTIMEOUT: r = conf_ctimeout.i; break;
    case CNF_TAPEBUFS: r = conf_tapebufs.i; break;
    case CNF_AUTOFLUSH: r = conf_autoflush.i; break;
    case CNF_RESERVE: r = conf_reserve.i; break;
    case CNF_AMRECOVER_DO_FSF: r = conf_amrecover_do_fsf.i; break;
    case CNF_AMRECOVER_CHECK_LABEL: r = conf_amrecover_check_label.i; break;
    case CNF_TAPERALGO: r = conf_taperalgo.i; break;
    case CNF_USETIMESTAMPS: r = conf_usetimestamps.i; break;

    default:
	error("error [unknown getconf_int parm: %d]", parm);
	/* NOTREACHED */
    }
    return r;
}

am64_t getconf_am64(parm)
confparm_t parm;
{
    am64_t r = 0;

    switch(parm) {
    case CNF_MAXDUMPSIZE: r = conf_maxdumpsize.am64; break;

    default:
	error("error [unknown getconf_am64 parm: %d]", parm);
	/* NOTREACHED */
    }
    return r;
}

double getconf_real(parm)
confparm_t parm;
{
    double r = 0;

    switch(parm) {

    case CNF_BUMPMULT: r = conf_bumpmult.r; break;

    default:
	error("error [unknown getconf_real parm: %d]", parm);
	/* NOTREACHED */
    }
    return r;
}

char *getconf_str(parm)
confparm_t parm;
{
    char *r = 0;

    switch(parm) {

    case CNF_ORG: r = conf_org.s; break;
    case CNF_MAILTO: r = conf_mailto.s; break;
    case CNF_DUMPUSER: r = conf_dumpuser.s; break;
    case CNF_PRINTER: r = conf_printer.s; break;
    case CNF_TAPEDEV: r = conf_tapedev.s; break;
    case CNF_TPCHANGER: r = conf_tpchanger.s; break;
    case CNF_CHNGRDEV: r = conf_chngrdev.s; break;
    case CNF_CHNGRFILE: r = conf_chngrfile.s; break;
    case CNF_LABELSTR: r = conf_labelstr.s; break;
    case CNF_TAPELIST: r = conf_tapelist.s; break;
    case CNF_INFOFILE: r = conf_infofile.s; break;
    case CNF_DUMPORDER: r = conf_dumporder.s; break;
    case CNF_LOGDIR: r = conf_logdir.s; break;
    /*case CNF_LOGFILE: r = conf_logfile.s; break;*/
    case CNF_DISKFILE: r = conf_diskfile.s; break;
    /*case CNF_DISKDIR: r = conf_diskdir.s; break;*/
    case CNF_TAPETYPE: r = conf_tapetype.s; break;
    case CNF_INDEXDIR: r = conf_indexdir.s; break;
    case CNF_RAWTAPEDEV: r = conf_rawtapedev.s; break;
    case CNF_COLUMNSPEC: r = conf_columnspec.s; break;
    case CNF_AMRECOVER_CHANGER: r = conf_amrecover_changer.s; break;
    case CNF_DISPLAYUNIT: r = conf_displayunit.s; break;
    case CNF_KRB5PRINCIPAL: r = conf_krb5principal.s; break;
    case CNF_KRB5KEYTAB: r = conf_krb5keytab.s; break;
    case CNF_LABEL_NEW_TAPES: r = conf_label_new_tapes.s; break;

    default:
	error("error [unknown getconf_str parm: %d]", parm);
	/* NOTREACHED */
    }
    return r;
}

holdingdisk_t *getconf_holdingdisks()
{
    return holdingdisks;
}

dumptype_t *lookup_dumptype(str)
char *str;
{
    dumptype_t *p;

    for(p = dumplist; p != NULL; p = p->next) {
	if(strcmp(p->name, str) == 0) return p;
    }
    return NULL;
}

tapetype_t *lookup_tapetype(str)
char *str;
{
    tapetype_t *p;

    for(p = tapelist; p != NULL; p = p->next) {
	if(strcmp(p->name, str) == 0) return p;
    }
    return NULL;
}

interface_t *lookup_interface(str)
char *str;
{
    interface_t *p;

    if(str == NULL) return interface_list;
    for(p = interface_list; p != NULL; p = p->next) {
	if(strcmp(p->name, str) == 0) return p;
    }
    return NULL;
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
    s = "YOUR ORG";
#endif
    conf_org.s = stralloc(s);
    malloc_mark(conf_org.s);
    conf_mailto.s = stralloc("operators");
    malloc_mark(conf_mailto.s);
    conf_dumpuser.s = stralloc(CLIENT_LOGIN);
    malloc_mark(conf_dumpuser.s);
#ifdef DEFAULT_TAPE_DEVICE
    s = DEFAULT_TAPE_DEVICE;
#else
    s = "/dev/rmt8";
#endif
    conf_tapedev.s = stralloc(s);
    malloc_mark(conf_tapedev.s);
#ifdef DEFAULT_RAW_TAPE_DEVICE
    s = DEFAULT_RAW_TAPE_DEVICE;
#else
    s = "/dev/rawft0";
#endif
    conf_rawtapedev.s = stralloc(s);
    malloc_mark(conf_rawtapedev.s);
    conf_tpchanger.s = stralloc("");
    malloc_mark(conf_tpchanger.s);
#ifdef DEFAULT_CHANGER_DEVICE
    s = DEFAULT_CHANGER_DEVICE;
#else
    s = "/dev/null";
#endif
    conf_chngrdev.s = stralloc(s);
    malloc_mark(conf_chngrdev.s);
    conf_chngrfile.s = stralloc("/usr/adm/amanda/changer-status");
    malloc_mark(conf_chngrfile.s);
    conf_labelstr.s = stralloc(".*");
    malloc_mark(conf_labelstr.s);
    conf_tapelist.s = stralloc("tapelist");
    malloc_mark(conf_tapelist.s);
    conf_infofile.s = stralloc("/usr/adm/amanda/curinfo");
    malloc_mark(conf_infofile.s);
    conf_logdir.s = stralloc("/usr/adm/amanda");
    malloc_mark(conf_logdir.s);
    conf_diskfile.s = stralloc("disklist");
    malloc_mark(conf_diskfile.s);
    conf_diskdir.s  = stralloc("/dumps/amanda");
    malloc_mark(conf_diskdir.s);
    conf_tapetype.s = stralloc("EXABYTE");
    malloc_mark(conf_tapetype.s);
    conf_indexdir.s = stralloc("/usr/adm/amanda/index");
    malloc_mark(conf_indexdir.s);
    conf_columnspec.s = stralloc("");
    malloc_mark(conf_columnspec.s);
    conf_dumporder.s = stralloc("ttt");
    malloc_mark(conf_dumporder.s);
    conf_amrecover_changer.s = stralloc("");
    conf_printer.s = stralloc("");
    conf_displayunit.s = stralloc("k");
    conf_label_new_tapes.s = stralloc("");

    conf_krb5keytab.s = stralloc("/.amanda-v5-keytab");
    conf_krb5principal.s = stralloc("service/amanda");

    conf_dumpcycle.i	= 10;
    conf_runspercycle.i	= 0;
    conf_tapecycle.i	= 15;
    conf_runtapes.i	= 1;
    conf_disksize.i	= 0;
    conf_netusage.i	= 300;
    conf_inparallel.i	= 10;
    conf_maxdumps.i	= 1;
    conf_timeout.i	= 2;
    conf_bumppercent.i	= 0;
    conf_bumpsize.i	= 10*1024;
    conf_bumpdays.i	= 2;
    conf_bumpmult.r	= 1.5;
    conf_etimeout.i     = 300;
    conf_dtimeout.i     = 1800;
    conf_ctimeout.i     = 30;
    conf_tapebufs.i     = 20;
    conf_autoflush.i	= 0;
    conf_reserve.i	= 100;
    conf_maxdumpsize.am64	= -1;
    conf_amrecover_do_fsf.i = 1;
    conf_amrecover_check_label.i = 1;
    conf_taperalgo.i    = 0;
    conf_usetimestamps.i = 0;

    /* defaults for internal variables */

    seen_org = 0;
    seen_mailto = 0;
    seen_dumpuser = 0;
    seen_tapedev = 0;
    seen_rawtapedev = 0;
    seen_printer = 0;
    seen_tpchanger = 0;
    seen_chngrdev = 0;
    seen_chngrfile = 0;
    seen_labelstr = 0;
    seen_runtapes = 0;
    seen_maxdumps = 0;
    seen_tapelist = 0;
    seen_infofile = 0;
    seen_diskfile = 0;
    seen_diskdir = 0;
    seen_logdir = 0;
    seen_bumppercent = 0;
    seen_bumpsize = 0;
    seen_bumpmult = 0;
    seen_bumpdays = 0;
    seen_tapetype = 0;
    seen_dumpcycle = 0;
    seen_runspercycle = 0;
    seen_maxcycle = 0;
    seen_tapecycle = 0;
    seen_disksize = 0;
    seen_netusage = 0;
    seen_inparallel = 0;
    seen_dumporder = 0;
    seen_timeout = 0;
    seen_indexdir = 0;
    seen_etimeout = 0;
    seen_dtimeout = 0;
    seen_ctimeout = 0;
    seen_tapebufs = 0;
    seen_autoflush = 0;
    seen_reserve = 0;
    seen_maxdumpsize = 0;
    seen_columnspec = 0;
    seen_amrecover_do_fsf = 0;
    seen_amrecover_check_label = 0;
    seen_amrecover_changer = 0;
    seen_taperalgo = 0;
    seen_displayunit = 0;
    seen_krb5keytab = 0;
    seen_krb5principal = 0;
    seen_label_new_tapes = 0;
    seen_usetimestamps = 0;

    line_num = got_parserror = 0;
    allow_overwrites = 0;
    token_pushed = 0;

    while(holdingdisks != NULL) {
	holdingdisk_t *hp;

	hp = holdingdisks;
	holdingdisks = holdingdisks->next;
	amfree(hp);
    }
    num_holdingdisks = 0;

    /* free any previously declared dump, tape and interface types */

    while(dumplist != NULL) {
	dumptype_t *dp;

	dp = dumplist;
	dumplist = dumplist->next;
	amfree(dp);
    }
    while(tapelist != NULL) {
	tapetype_t *tp;

	tp = tapelist;
	tapelist = tapelist->next;
	amfree(tp);
    }
    while(interface_list != NULL) {
	interface_t *ip;

	ip = interface_list;
	interface_list = interface_list->next;
	amfree(ip);
    }

    /* create some predefined dumptypes for backwards compatability */
    init_dumptype_defaults();
    dpcur.name = "NO-COMPRESS"; dpcur.seen = -1;
    dpcur.compress = COMP_NONE; dpcur.s_compress = -1;
    save_dumptype();

    init_dumptype_defaults();
    dpcur.name = "COMPRESS-FAST"; dpcur.seen = -1;
    dpcur.compress = COMP_FAST; dpcur.s_compress = -1;
    save_dumptype();

    init_dumptype_defaults();
    dpcur.name = "COMPRESS-BEST"; dpcur.seen = -1;
    dpcur.compress = COMP_BEST; dpcur.s_compress = -1;
    save_dumptype();

    init_dumptype_defaults();
    dpcur.name = "COMPRESS-CUST"; dpcur.seen = -1;
    dpcur.compress = COMP_CUST; dpcur.s_compress = -1;
    save_dumptype();

    init_dumptype_defaults();
    dpcur.name = "SRVCOMPRESS"; dpcur.seen = -1;
    dpcur.compress = COMP_SERV_FAST; dpcur.s_compress = -1;
    save_dumptype();

    init_dumptype_defaults();
    dpcur.name = "BSD-AUTH"; dpcur.seen = -1;
    amfree(dpcur.security_driver);
    dpcur.security_driver = stralloc("BSD"); dpcur.s_security_driver = -1;
    save_dumptype();

    init_dumptype_defaults();
    dpcur.name = "KRB4-AUTH"; dpcur.seen = -1;
    amfree(dpcur.security_driver);
    dpcur.security_driver = stralloc("KRB4"); dpcur.s_security_driver = -1;
    save_dumptype();

    init_dumptype_defaults();
    dpcur.name = "NO-RECORD"; dpcur.seen = -1;
    dpcur.record = 0; dpcur.s_record = -1;
    save_dumptype();

    init_dumptype_defaults();
    dpcur.name = "NO-HOLD"; dpcur.seen = -1;
    dpcur.no_hold = 1; dpcur.s_no_hold = -1;
    save_dumptype();

    init_dumptype_defaults();
    dpcur.name = "NO-FULL"; dpcur.seen = -1;
    dpcur.strategy = DS_NOFULL; dpcur.s_strategy = -1;
    save_dumptype();
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
    { "BUMPDAYS", BUMPDAYS },
    { "BUMPMULT", BUMPMULT },
    { "BUMPSIZE", BUMPSIZE },
    { "BUMPPERCENT", BUMPPERCENT },
    { "DEFINE", DEFINE },
    { "DISKDIR", DISKDIR },	/* XXX - historical */
    { "DISKFILE", DISKFILE },
    { "DISKSIZE", DISKSIZE },	/* XXX - historical */
    { "DUMPCYCLE", DUMPCYCLE },
    { "RUNSPERCYCLE", RUNSPERCYCLE },
    { "DUMPTYPE", DUMPTYPE },
    { "DUMPUSER", DUMPUSER },
    { "PRINTER", PRINTER },
    { "HOLDINGDISK", HOLDING },
    { "INCLUDEFILE", INCLUDEFILE },
    { "INDEXDIR", INDEXDIR },
    { "INFOFILE", INFOFILE },
    { "INPARALLEL", INPARALLEL },
    { "DUMPORDER", DUMPORDER },
    { "INTERFACE", INTERFACE },
    { "LABELSTR", LABELSTR },
    { "LOGDIR", LOGDIR },
    { "LOGFILE", LOGFILE },	/* XXX - historical */
    { "MAILTO", MAILTO },
    { "MAXCYCLE", MAXCYCLE },	/* XXX - historical */
    { "MAXDUMPS", MAXDUMPS },
    { "MINCYCLE", DUMPCYCLE },	/* XXX - historical */
    { "NETUSAGE", NETUSAGE },	/* XXX - historical */
    { "ORG", ORG },
    { "RUNTAPES", RUNTAPES },
    { "TAPECYCLE", TAPECYCLE },
    { "TAPEDEV", TAPEDEV },
    { "TAPELIST", TAPELIST },
    { "TAPETYPE", TAPETYPE },
    { "TIMEOUT", TIMEOUT },	/* XXX - historical */
    { "TPCHANGER", TPCHANGER },
    { "CHANGERDEV", CHNGRDEV },
    { "CHANGERFILE", CHNGRFILE },
    { "ETIMEOUT", ETIMEOUT },
    { "DTIMEOUT", DTIMEOUT },
    { "CTIMEOUT", CTIMEOUT },
    { "TAPEBUFS", TAPEBUFS },
    { "RAWTAPEDEV", RAWTAPEDEV },
    { "AUTOFLUSH", AUTOFLUSH },
    { "RESERVE", RESERVE },
    { "MAXDUMPSIZE", MAXDUMPSIZE },
    { "COLUMNSPEC", COLUMNSPEC },
    { "AMRECOVER_DO_FSF", AMRECOVER_DO_FSF },
    { "AMRECOVER_CHECK_LABEL", AMRECOVER_CHECK_LABEL },
    { "AMRECOVER_CHANGER", AMRECOVER_CHANGER },
    { "TAPERALGO", TAPERALGO },
    { "DISPLAYUNIT", DISPLAYUNIT },
    { "KRB5KEYTAB", KRB5KEYTAB },
    { "KRB5PRINCIPAL", KRB5PRINCIPAL },
    { "LABEL_NEW_TAPES", LABEL_NEW_TAPES },
    { "USETIMESTAMPS", USETIMESTAMPS },
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

    case ORG:       get_simple(&conf_org,       &seen_org,       STRING); break;
    case MAILTO:    get_simple(&conf_mailto,    &seen_mailto,    STRING); break;
    case DUMPUSER:  get_simple(&conf_dumpuser,  &seen_dumpuser,  STRING); break;
    case PRINTER:   get_simple(&conf_printer,   &seen_printer,   STRING); break;
    case DUMPCYCLE: get_simple(&conf_dumpcycle, &seen_dumpcycle, INT);
		    if(conf_dumpcycle.i < 0) {
			parserror("dumpcycle must be positive");
		    }
		    break;
    case RUNSPERCYCLE: get_simple(&conf_runspercycle, &seen_runspercycle, INT);
		    if(conf_runspercycle.i < -1) {
			parserror("runspercycle must be >= -1");
		    }
		    break;
    case MAXCYCLE:  get_simple(&conf_maxcycle,  &seen_maxcycle,  INT);    break;
    case TAPECYCLE: get_simple(&conf_tapecycle, &seen_tapecycle, INT);
		    if(conf_tapecycle.i < 1) {
			parserror("tapecycle must be positive");
		    }
		    break;
    case RUNTAPES:  get_simple(&conf_runtapes,  &seen_runtapes,  INT);
		    if(conf_runtapes.i < 0) {
			parserror("runtapes must be positive");
		    }
		    break;
    case TAPEDEV:   get_simple(&conf_tapedev,   &seen_tapedev,   STRING); break;
    case RAWTAPEDEV:get_simple(&conf_rawtapedev,&seen_rawtapedev,STRING); break;
    case TPCHANGER: get_simple(&conf_tpchanger, &seen_tpchanger, STRING); break;
    case CHNGRDEV:  get_simple(&conf_chngrdev,  &seen_chngrdev,  STRING); break;
    case CHNGRFILE: get_simple(&conf_chngrfile, &seen_chngrfile, STRING); break;
    case LABELSTR:  get_simple(&conf_labelstr,  &seen_labelstr,  STRING); break;
    case TAPELIST:  get_simple(&conf_tapelist,  &seen_tapelist,  STRING); break;
    case INFOFILE:  get_simple(&conf_infofile,  &seen_infofile,  STRING); break;
    case LOGDIR:    get_simple(&conf_logdir,    &seen_logdir,    STRING); break;
    case DISKFILE:  get_simple(&conf_diskfile,  &seen_diskfile,  STRING); break;
    case BUMPMULT:  get_simple(&conf_bumpmult,  &seen_bumpmult,  REAL);
		    if(conf_bumpmult.r < 0.999) {
			parserror("bumpmult must be positive");
		    }
		    break;
    case BUMPPERCENT:  get_simple(&conf_bumppercent,  &seen_bumppercent,  INT);
		    if(conf_bumppercent.i < 0 || conf_bumppercent.i > 100) {
			parserror("bumppercent must be between 0 and 100");
		    }
		    break;
    case BUMPSIZE:  get_simple(&conf_bumpsize,  &seen_bumpsize,  INT);
		    if(conf_bumpsize.i < 1) {
			parserror("bumpsize must be positive");
		    }
		    break;
    case BUMPDAYS:  get_simple(&conf_bumpdays,  &seen_bumpdays,  INT);
		    if(conf_bumpdays.i < 1) {
			parserror("bumpdays must be positive");
		    }
		    break;
    case NETUSAGE:  get_simple(&conf_netusage,  &seen_netusage,  INT);
		    if(conf_netusage.i < 1) {
			parserror("netusage must be positive");
		    }
		    break;
    case INPARALLEL:get_simple(&conf_inparallel,&seen_inparallel,INT);
		    if(conf_inparallel.i < 1 || conf_inparallel.i >MAX_DUMPERS){
			parserror(
			    "inparallel must be between 1 and MAX_DUMPERS (%d)",
			    MAX_DUMPERS);
		    }
		    break;
    case DUMPORDER: get_simple(&conf_dumporder, &seen_dumporder, STRING); break;
    case TIMEOUT:   get_simple(&conf_timeout,   &seen_timeout,   INT);    break;
    case MAXDUMPS:  get_simple(&conf_maxdumps,  &seen_maxdumps,  INT);
		    if(conf_maxdumps.i < 1) {
			parserror("maxdumps must be positive");
		    }
		    break;
    case TAPETYPE:  get_simple(&conf_tapetype,  &seen_tapetype,  IDENT);  break;
    case INDEXDIR:  get_simple(&conf_indexdir,  &seen_indexdir,  STRING); break;
    case ETIMEOUT:  get_simple(&conf_etimeout,  &seen_etimeout,  INT);    break;
    case DTIMEOUT:  get_simple(&conf_dtimeout,  &seen_dtimeout,  INT);
		    if(conf_dtimeout.i < 1) {
			parserror("dtimeout must be positive");
		    }
		    break;
    case CTIMEOUT:  get_simple(&conf_ctimeout,  &seen_ctimeout,  INT);
		    if(conf_ctimeout.i < 1) {
			parserror("ctimeout must be positive");
		    }
		    break;
    case TAPEBUFS:  get_simple(&conf_tapebufs,  &seen_tapebufs,  INT);
		    if(conf_tapebufs.i < 1) {
			parserror("tapebufs must be positive");
		    }
		    break;
    case AUTOFLUSH: get_simple(&conf_autoflush, &seen_autoflush, BOOL);   break;
    case RESERVE:   get_simple(&conf_reserve,   &seen_reserve,	 INT);
		    if(conf_reserve.i < 0 || conf_reserve.i > 100) {
			parserror("reserve must be between 0 and 100");
		    }
		    break;
    case MAXDUMPSIZE:get_simple(&conf_maxdumpsize,&seen_maxdumpsize,AM64); break;
    case COLUMNSPEC:get_simple(&conf_columnspec,&seen_columnspec,STRING); break;

    case AMRECOVER_DO_FSF: get_simple(&conf_amrecover_do_fsf,&seen_amrecover_do_fsf, BOOL); break;
    case AMRECOVER_CHECK_LABEL: get_simple(&conf_amrecover_check_label,&seen_amrecover_check_label, BOOL); break;
    case AMRECOVER_CHANGER: get_simple(&conf_amrecover_changer,&seen_amrecover_changer, STRING); break;

    case TAPERALGO: get_taperalgo(&conf_taperalgo,&seen_taperalgo); break;
    case DISPLAYUNIT: get_simple(&conf_displayunit,&seen_displayunit, STRING);
		      if(strcmp(conf_displayunit.s,"k") == 0 ||
			 strcmp(conf_displayunit.s,"K") == 0) {
			  conf_displayunit.s[0] = toupper(conf_displayunit.s[0]);
			  unit_divisor=1;
		      }
		      else if(strcmp(conf_displayunit.s,"m") == 0 ||
			 strcmp(conf_displayunit.s,"M") == 0) {
			  conf_displayunit.s[0] = toupper(conf_displayunit.s[0]);
			  unit_divisor=1024;
		      }
		      else if(strcmp(conf_displayunit.s,"g") == 0 ||
			 strcmp(conf_displayunit.s,"G") == 0) {
			  conf_displayunit.s[0] = toupper(conf_displayunit.s[0]);
			  unit_divisor=1024*1024;
		      }
		      else if(strcmp(conf_displayunit.s,"t") == 0 ||
			 strcmp(conf_displayunit.s,"T") == 0) {
			  conf_displayunit.s[0] = toupper(conf_displayunit.s[0]);
			  unit_divisor=1024*1024*1024;
		      }
		      else {
			  parserror("displayunit must be k,m,g or t.");
		      }
		      break;

    /* kerberos 5 bits.  only useful when kerberos 5 built in... */
    case KRB5KEYTAB:    get_simple(&conf_krb5keytab,   &seen_krb5keytab,   STRING); break;
    case KRB5PRINCIPAL: get_simple(&conf_krb5principal,&seen_krb5principal,STRING); break;

    case LOGFILE: /* XXX - historical */
	/* truncate the filename part and pretend he said "logdir" */
	{
	    char *p;

	    get_simple(&conf_logdir, &seen_logdir, STRING);

	    p = strrchr(conf_logdir.s, '/');
	    if (p != (char *)0) *p = '\0';
	}
	break;

    case DISKDIR:
	{
	    char *s;

	    get_conftoken(STRING);
	    s = tokenval.s;

	    if(!seen_diskdir) {
		conf_diskdir.s = newstralloc(conf_diskdir.s, s);
		seen_diskdir = line_num;
	    }

	    init_holdingdisk_defaults();
	    hdcur.name = "default from DISKDIR";
	    hdcur.seen = line_num;
	    hdcur.diskdir = stralloc(s);
	    hdcur.s_disk = line_num;
	    hdcur.disksize = conf_disksize.i;
	    hdcur.s_size = seen_disksize;
	    save_holdingdisk();
	}
	break;

    case DISKSIZE:
	{
	    int i;

	    i = get_int();
	    i = (i / DISK_BLOCK_KB) * DISK_BLOCK_KB;

	    if(!seen_disksize) {
		conf_disksize.i = i;
		seen_disksize = line_num;
	    }

	    if(holdingdisks != NULL)
		holdingdisks->disksize = i;
	}

	break;

    case HOLDING:
	get_holdingdisk();
	break;

    case DEFINE:
	get_conftoken(ANY);
	if(tok == DUMPTYPE) get_dumptype();
	else if(tok == TAPETYPE) get_tapetype();
	else if(tok == INTERFACE) get_interface();
	else parserror("DUMPTYPE, INTERFACE or TAPETYPE expected");
	break;
    case LABEL_NEW_TAPES:
        get_simple(&conf_label_new_tapes, &seen_label_new_tapes, STRING);
        break;

    case USETIMESTAMPS:
        get_simple(&conf_usetimestamps, &seen_usetimestamps, BOOL); break;

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

keytab_t holding_keytable[] = {
    { "DIRECTORY", DIRECTORY },
    { "COMMENT", COMMENT },
    { "USE", USE },
    { "CHUNKSIZE", CHUNKSIZE },
    { NULL, IDENT }
};

static void get_holdingdisk()
{
    int done;
    int save_overwrites;
    keytab_t *save_kt;

    save_overwrites = allow_overwrites;
    allow_overwrites = 1;

    save_kt = keytable;
    keytable = holding_keytable;

    init_holdingdisk_defaults();

    get_conftoken(IDENT);
    hdcur.name = stralloc(tokenval.s);
    malloc_mark(hdcur.name);
    hdcur.seen = line_num;

    get_conftoken(LBRACE);
    get_conftoken(NL);

    done = 0;
    do {
	line_num += 1;
	get_conftoken(ANY);
	switch(tok) {

	case COMMENT:
	    get_simple((val_t *)&hdcur.comment, &hdcur.s_comment, STRING);
	    break;
	case DIRECTORY:
	    get_simple((val_t *)&hdcur.diskdir, &hdcur.s_disk, STRING);
	    break;
	case USE:
	    get_simple((val_t *)&hdcur.disksize, &hdcur.s_size, LONG);
	    hdcur.disksize = am_floor(hdcur.disksize, DISK_BLOCK_KB);
	    break;
	case CHUNKSIZE:
	    get_simple((val_t *)&hdcur.chunksize, &hdcur.s_csize, LONG);
	    if(hdcur.chunksize == 0) {
	        hdcur.chunksize =  ((INT_MAX / 1024) - (2 * DISK_BLOCK_KB));
	    } else if(hdcur.chunksize < 0) {
		parserror("Negative chunksize (%ld) is no longer supported",
			  hdcur.chunksize);
	    }
	    hdcur.chunksize = am_floor(hdcur.chunksize, DISK_BLOCK_KB);
	    break;
	case RBRACE:
	    done = 1;
	    break;
	case NL:	/* empty line */
	    break;
	case END:	/* end of file */
	    done = 1;
	default:
	    parserror("holding disk parameter expected");
	}
	if(tok != NL && tok != END) get_conftoken(NL);
    } while(!done);

    save_holdingdisk();

    allow_overwrites = save_overwrites;
    keytable = save_kt;
}

static void init_holdingdisk_defaults()
{
    hdcur.comment = stralloc("");
    hdcur.diskdir = stralloc(conf_diskdir.s);
    malloc_mark(hdcur.diskdir);
    hdcur.disksize = 0;
    hdcur.chunksize = 1024*1024/**1024*/; /* 1 Gb = 1M counted in 1Kb blocks */

    hdcur.s_comment = 0;
    hdcur.s_disk = 0;
    hdcur.s_size = 0;
    hdcur.s_csize = 0;

    hdcur.up = (void *)0;
}

static void save_holdingdisk()
{
    holdingdisk_t *hp;

    hp = alloc(sizeof(holdingdisk_t));
    malloc_mark(hp);
    *hp = hdcur;
    hp->next = holdingdisks;
    holdingdisks = hp;

    num_holdingdisks++;
}


keytab_t dumptype_keytable[] = {
    { "AUTH", AUTH },
    { "BUMPDAYS", BUMPDAYS },
    { "BUMPMULT", BUMPMULT },
    { "BUMPSIZE", BUMPSIZE },
    { "BUMPPERCENT", BUMPPERCENT },
    { "COMMENT", COMMENT },
    { "COMPRATE", COMPRATE },
    { "COMPRESS", COMPRESS },
    { "ENCRYPT", ENCRYPT },
    { "SERVER_DECRYPT_OPTION", SRV_DECRYPT_OPT },
    { "CLIENT_DECRYPT_OPTION", CLNT_DECRYPT_OPT },
    { "DUMPCYCLE", DUMPCYCLE },
    { "EXCLUDE", EXCLUDE },
    { "FREQUENCY", FREQUENCY },	/* XXX - historical */
    { "HOLDINGDISK", HOLDING },
    { "IGNORE", IGNORE },
    { "INCLUDE", INCLUDE },
    { "INDEX", INDEX },
    { "KENCRYPT", KENCRYPT },
    { "MAXCYCLE", MAXCYCLE },	/* XXX - historical */
    { "MAXDUMPS", MAXDUMPS },
    { "MAXPROMOTEDAY", MAXPROMOTEDAY },
    { "OPTIONS", OPTIONS },	/* XXX - historical */
    { "PRIORITY", PRIORITY },
    { "PROGRAM", PROGRAM },
    { "RECORD", RECORD },
    { "SKIP-FULL", SKIP_FULL },
    { "SKIP-INCR", SKIP_INCR },
    { "STARTTIME", STARTTIME },
    { "STRATEGY", STRATEGY },
    { "TAPE_SPLITSIZE", TAPE_SPLITSIZE },
    { "SPLIT_DISKBUFFER", SPLIT_DISKBUFFER },
    { "FALLBACK_SPLITSIZE", FALLBACK_SPLITSIZE },
    { "ESTIMATE", ESTIMATE },
    { "SERVER_CUSTOM_COMPRESS", SRVCOMPPROG },
    { "CLIENT_CUSTOM_COMPRESS", CLNTCOMPPROG },
    { "SERVER_ENCRYPT", SRV_ENCRYPT },
    { "CLIENT_ENCRYPT", CLNT_ENCRYPT },
    { "AMANDAD_PATH", AMANDAD_PATH },
    { "CLIENT_USERNAME", CLIENT_USERNAME },
    { NULL, IDENT }
};

dumptype_t *read_dumptype(name, from, fname, linenum)
     char *name;
     FILE *from;
     char *fname;
     int *linenum;
{
    int done;
    int save_overwrites;
    keytab_t *save_kt;
    val_t tmpval;
    FILE *saved_conf = NULL;
    char *saved_fname = NULL;

    if (from) {
	saved_conf = conf;
	conf = from;
    }

    if (fname) {
	saved_fname = confname;
	confname = fname;
    }

    if (linenum)
	line_num = *linenum;

    save_overwrites = allow_overwrites;
    allow_overwrites = 1;

    save_kt = keytable;
    keytable = dumptype_keytable;

    init_dumptype_defaults();

    if (name) {
	dpcur.name = name;
    } else {
	get_conftoken(IDENT);
	dpcur.name = stralloc(tokenval.s);
	malloc_mark(dpcur.name);
    }

    dpcur.seen = line_num;

    if (! name) {
	get_conftoken(LBRACE);
	get_conftoken(NL);
    }

    done = 0;
    do {
	line_num += 1;
	get_conftoken(ANY);
	switch(tok) {

	case AUTH:
	    get_simple((val_t *)&dpcur.security_driver,
		&dpcur.s_security_driver, STRING);
	    break;
	case COMMENT:
	    get_simple((val_t *)&dpcur.comment, &dpcur.s_comment, STRING);
	    break;
	case COMPRATE:
	    get_comprate();
	    break;
	case COMPRESS:
	    get_compress();
	    break;
	case ENCRYPT:
	    get_encrypt();
	    break;
	case SRV_DECRYPT_OPT:
	    get_simple((val_t *)&dpcur.srv_decrypt_opt, &dpcur.s_srv_decrypt_opt, STRING);
	    break;
	case CLNT_DECRYPT_OPT:
	    get_simple((val_t *)&dpcur.clnt_decrypt_opt, &dpcur.s_clnt_decrypt_opt, STRING);
	    break;
	case DUMPCYCLE:
	    get_simple((val_t *)&dpcur.dumpcycle, &dpcur.s_dumpcycle, INT);
	    if(dpcur.dumpcycle < 0) {
		parserror("dumpcycle must be positive");
	    }
	    break;
	case EXCLUDE:
	    get_exclude();
	    break;
	case FREQUENCY:
	    get_simple((val_t *)&dpcur.frequency, &dpcur.s_frequency, INT);
	    break;
	case HOLDING:
	    get_simple(&tmpval, &dpcur.s_no_hold, BOOL);
	    dpcur.no_hold = (tmpval.i == 0);
	    break;
	case IGNORE:
	    get_simple(&tmpval, &dpcur.s_ignore, BOOL);
	    dpcur.ignore = (tmpval.i != 0);
	    break;
	case INCLUDE:
	    get_include();
	    break;
	case INDEX:
	    get_simple(&tmpval, &dpcur.s_index, BOOL);
	    dpcur.index = (tmpval.i != 0);
	    break;
	case KENCRYPT:
	    get_simple(&tmpval, &dpcur.s_kencrypt, BOOL);
	    dpcur.kencrypt = (tmpval.i != 0);
	    break;
	case MAXCYCLE:
	    get_simple((val_t *)&conf_maxcycle, &dpcur.s_maxcycle, INT);
	    break;
	case MAXDUMPS:
	    get_simple((val_t *)&dpcur.maxdumps, &dpcur.s_maxdumps, INT);
	    if(dpcur.maxdumps < 1) {
		parserror("maxdumps must be positive");
	    }
	    break;
	case MAXPROMOTEDAY:
	    get_simple((val_t *)&dpcur.maxpromoteday, &dpcur.s_maxpromoteday, INT);
	    if(dpcur.maxpromoteday < 0) {
		parserror("dpcur.maxpromoteday must be >= 0");
	    }
	    break;
	case BUMPPERCENT:
	    get_simple((val_t *)&dpcur.bumppercent,  &dpcur.s_bumppercent,  INT);
	    if(dpcur.bumppercent < 0 || dpcur.bumppercent > 100) {
		parserror("bumppercent must be between 0 and 100");
	    }
	    break;
	case BUMPSIZE:
	    get_simple((val_t *)&dpcur.bumpsize,  &dpcur.s_bumpsize,  INT);
	    if(dpcur.bumpsize < 1) {
		parserror("bumpsize must be positive");
	    }
	    break;
	case BUMPDAYS:
	    get_simple((val_t *)&dpcur.bumpdays,  &dpcur.s_bumpdays,  INT);
	    if(dpcur.bumpdays < 1) {
		parserror("bumpdays must be positive");
	    }
	    break;
	case BUMPMULT:
	    get_simple((val_t *)&dpcur.bumpmult,  &dpcur.s_bumpmult,  REAL);
	    if(dpcur.bumpmult < 0.999) {
		parserror("bumpmult must be positive (%f)",dpcur.bumpmult);
	    }
	    break;
	case OPTIONS:
	    get_dumpopts();
	    break;
	case PRIORITY:
	    get_priority();
	    break;
	case PROGRAM:
	    get_simple((val_t *)&dpcur.program, &dpcur.s_program, STRING);
	    break;
	case RECORD:
	    get_simple(&tmpval, &dpcur.s_record, BOOL);
	    dpcur.record = (tmpval.i != 0);
	    break;
	case SKIP_FULL:
	    get_simple(&tmpval, &dpcur.s_skip_full, BOOL);
	    dpcur.skip_full = (tmpval.i != 0);
	    break;
	case SKIP_INCR:
	    get_simple(&tmpval, &dpcur.s_skip_incr, BOOL);
	    dpcur.skip_incr = (tmpval.i != 0);
	    break;
	case STARTTIME:
	    get_simple((val_t *)&dpcur.start_t, &dpcur.s_start_t, TIME);
	    break;
	case STRATEGY:
	    get_strategy();
	    break;
	case ESTIMATE:
	    get_estimate();
	    break;
	case IDENT:
	    copy_dumptype();
	    break;
	case TAPE_SPLITSIZE:
	    get_simple((val_t *)&dpcur.tape_splitsize,  &dpcur.s_tape_splitsize,  INT);
	    if(dpcur.tape_splitsize < 0) {
	      parserror("tape_splitsize must be >= 0");
	    }
	    break;
	case SPLIT_DISKBUFFER:
	    get_simple((val_t *)&dpcur.split_diskbuffer, &dpcur.s_split_diskbuffer, STRING);
	    break;
	case FALLBACK_SPLITSIZE:
	    get_simple((val_t *)&dpcur.fallback_splitsize,  &dpcur.s_fallback_splitsize,  INT);
	    if(dpcur.fallback_splitsize < 0) {
	      parserror("fallback_splitsize must be >= 0");
	    }
	    break;
	case SRVCOMPPROG:
	    get_simple((val_t *)&dpcur.srvcompprog, &dpcur.s_srvcompprog, STRING);
	    break;
        case CLNTCOMPPROG:
	    get_simple((val_t *)&dpcur.clntcompprog, &dpcur.s_clntcompprog, STRING);
	    break;
	case SRV_ENCRYPT:
	    get_simple((val_t *)&dpcur.srv_encrypt, &dpcur.s_srv_encrypt, STRING);
	    break;
        case CLNT_ENCRYPT:
	    get_simple((val_t *)&dpcur.clnt_encrypt, &dpcur.s_clnt_encrypt, STRING);
	    break;
        case AMANDAD_PATH:
	    get_simple((val_t *)&dpcur.amandad_path, &dpcur.s_amandad_path, STRING);
	    break;
        case CLIENT_USERNAME:
	    get_simple((val_t *)&dpcur.client_username, &dpcur.s_client_username, STRING);
	    break;
	case RBRACE:
	    done = 1;
	    break;
	case NL:	/* empty line */
	    break;
	case END:	/* end of file */
	    done = 1;
	default:
	    parserror("dump type parameter expected");
	}
	if(tok != NL && tok != END &&
	   /* When a name is specified, we shouldn't consume the NL
	      after the RBRACE.  */
	   (tok != RBRACE || name == 0))
	    get_conftoken(NL);
    } while(!done);

    /* XXX - there was a stupidity check in here for skip-incr and
    ** skip-full.  This check should probably be somewhere else. */

    save_dumptype();

    allow_overwrites = save_overwrites;
    keytable = save_kt;

    if (linenum)
	*linenum = line_num;

    if (fname)
	confname = saved_fname;

    if (from)
	conf = saved_conf;

    return lookup_dumptype(dpcur.name);
}

static void get_dumptype()
{
    read_dumptype(NULL, NULL, NULL, NULL);
}

static void init_dumptype_defaults()
{
    dpcur.comment = stralloc("");
    dpcur.program = stralloc("DUMP");
    dpcur.srvcompprog = stralloc("");
    dpcur.clntcompprog = stralloc("");
    dpcur.srv_encrypt = stralloc("");
    dpcur.clnt_encrypt = stralloc("");
    dpcur.amandad_path = stralloc("X");
    dpcur.client_username = stralloc("X");
    dpcur.exclude_file = NULL;
    dpcur.exclude_list = NULL;
    dpcur.include_file = NULL;
    dpcur.include_list = NULL;
    dpcur.priority = 1;
    dpcur.dumpcycle = conf_dumpcycle.i;
    dpcur.maxcycle = conf_maxcycle.i;
    dpcur.frequency = 1;
    dpcur.maxdumps = conf_maxdumps.i;
    dpcur.maxpromoteday = 10000;
    dpcur.bumppercent = conf_bumppercent.i;
    dpcur.bumpsize = conf_bumpsize.i;
    dpcur.bumpdays = conf_bumpdays.i;
    dpcur.bumpmult = conf_bumpmult.r;
    dpcur.start_t = 0;
    dpcur.security_driver = stralloc("BSD");

    /* options */
    dpcur.record = 1;
    dpcur.strategy = DS_STANDARD;
    dpcur.estimate = ES_CLIENT;
    dpcur.compress = COMP_FAST;
    dpcur.encrypt = ENCRYPT_NONE;
    dpcur.srv_decrypt_opt = stralloc("-d");
    dpcur.clnt_decrypt_opt = stralloc("-d");
    dpcur.comprate[0] = dpcur.comprate[1] = 0.50;
    dpcur.skip_incr = dpcur.skip_full = 0;
    dpcur.no_hold = 0;
    dpcur.kencrypt = 0;
    dpcur.ignore = 0;
    dpcur.index = 0;
    dpcur.tape_splitsize = 0;
    dpcur.split_diskbuffer = NULL;
    dpcur.fallback_splitsize = 10 * 1024;

    dpcur.s_comment = 0;
    dpcur.s_program = 0;
    dpcur.s_srvcompprog = 0;
    dpcur.s_clntcompprog = 0;
    dpcur.s_clnt_encrypt= 0;
    dpcur.s_srv_encrypt= 0;
    dpcur.s_amandad_path= 0;
    dpcur.s_client_username= 0;

    dpcur.s_exclude_file = 0;
    dpcur.s_exclude_list = 0;
    dpcur.s_include_file = 0;
    dpcur.s_include_list = 0;
    dpcur.s_priority = 0;
    dpcur.s_dumpcycle = 0;
    dpcur.s_maxcycle = 0;
    dpcur.s_frequency = 0;
    dpcur.s_maxdumps = 0;
    dpcur.s_maxpromoteday = 0;
    dpcur.s_bumppercent = 0;
    dpcur.s_bumpsize = 0;
    dpcur.s_bumpdays = 0;
    dpcur.s_bumpmult = 0;
    dpcur.s_start_t = 0;
    dpcur.s_security_driver = 0;
    dpcur.s_record = 0;
    dpcur.s_strategy = 0;
    dpcur.s_estimate = 0;
    dpcur.s_compress = 0;
    dpcur.s_encrypt = 0;
    dpcur.s_srv_decrypt_opt = 0;
    dpcur.s_clnt_decrypt_opt = 0;
    dpcur.s_comprate = 0;
    dpcur.s_skip_incr = 0;
    dpcur.s_skip_full = 0;
    dpcur.s_no_hold = 0;
    dpcur.s_kencrypt = 0;
    dpcur.s_ignore = 0;
    dpcur.s_index = 0;
    dpcur.s_tape_splitsize = 0;
    dpcur.s_split_diskbuffer = 0;
    dpcur.s_fallback_splitsize = 0;
}

static void save_dumptype()
{
    dumptype_t *dp;

    dp = lookup_dumptype(dpcur.name);

    if(dp != (dumptype_t *)0) {
	parserror("dumptype %s already defined on line %d", dp->name, dp->seen);
	return;
    }

    dp = alloc(sizeof(dumptype_t));
    malloc_mark(dp);
    *dp = dpcur;
    dp->next = dumplist;
    dumplist = dp;
}

static void copy_dumptype()
{
    dumptype_t *dt;

    dt = lookup_dumptype(tokenval.s);

    if(dt == NULL) {
	parserror("dump type parameter expected");
	return;
    }

#define dtcopy(v,s) if(dt->s) { dpcur.v = dt->v; dpcur.s = dt->s; }

    if(dt->s_comment) {
	dpcur.comment = newstralloc(dpcur.comment, dt->comment);
	dpcur.s_comment = dt->s_comment;
    }
    if(dt->s_program) {
	dpcur.program = newstralloc(dpcur.program, dt->program);
	dpcur.s_program = dt->s_program;
    }
    if(dt->s_security_driver) {
	dpcur.security_driver = newstralloc(dpcur.security_driver,
					    dt->security_driver);
	dpcur.s_security_driver = dt->s_security_driver;
    }
    if(dt->s_srvcompprog) {
	dpcur.srvcompprog = newstralloc(dpcur.srvcompprog, dt->srvcompprog);
	dpcur.s_srvcompprog = dt->s_srvcompprog;
    }
    if(dt->s_clntcompprog) {
	dpcur.clntcompprog = newstralloc(dpcur.clntcompprog, dt->clntcompprog);
	dpcur.s_clntcompprog = dt->s_clntcompprog;
    }
    if(dt->s_srv_encrypt) {
	dpcur.srv_encrypt = newstralloc(dpcur.srv_encrypt, dt->srv_encrypt);
	dpcur.s_srv_encrypt = dt->s_srv_encrypt;
    }
    if(dt->s_clnt_encrypt) {
	dpcur.clnt_encrypt = newstralloc(dpcur.clnt_encrypt, dt->clnt_encrypt);
	dpcur.s_clnt_encrypt = dt->s_clnt_encrypt;
    }
    if(dt->s_srv_decrypt_opt) {
	dpcur.srv_decrypt_opt = newstralloc(dpcur.srv_decrypt_opt, dt->srv_decrypt_opt);
	dpcur.s_srv_decrypt_opt = dt->s_srv_decrypt_opt;
    }
    if(dt->s_clnt_decrypt_opt) {
	dpcur.clnt_decrypt_opt = newstralloc(dpcur.clnt_decrypt_opt, dt->clnt_decrypt_opt);
	dpcur.s_clnt_decrypt_opt = dt->s_clnt_decrypt_opt;
    }

    if(dt->s_amandad_path) {
	dpcur.amandad_path = newstralloc(dpcur.amandad_path, dt->amandad_path);
	dpcur.s_amandad_path = dt->s_amandad_path;
    }
    if(dt->s_client_username) {
	dpcur.client_username = newstralloc(dpcur.client_username, dt->client_username);
	dpcur.s_client_username = dt->s_client_username;
    }

    if(dt->s_exclude_file) {
	dpcur.exclude_file = duplicate_sl(dt->exclude_file);
	dpcur.s_exclude_file = dt->s_exclude_file;
    }
    if(dt->s_exclude_list) {
	dpcur.exclude_list = duplicate_sl(dt->exclude_list);
	dpcur.s_exclude_list = dt->s_exclude_list;
    }
    if(dt->s_include_file) {
	dpcur.include_file = duplicate_sl(dt->include_file);
	dpcur.s_include_file = dt->s_include_file;
    }
    if(dt->s_include_list) {
	dpcur.include_list = duplicate_sl(dt->include_list);
	dpcur.s_include_list = dt->s_include_list;
    }
    dtcopy(priority, s_priority);
    dtcopy(dumpcycle, s_dumpcycle);
    dtcopy(maxcycle, s_maxcycle);
    dtcopy(frequency, s_frequency);
    dtcopy(maxdumps, s_maxdumps);
    dtcopy(maxpromoteday, s_maxpromoteday);
    dtcopy(bumppercent, s_bumppercent);
    dtcopy(bumpsize, s_bumpsize);
    dtcopy(bumpdays, s_bumpdays);
    dtcopy(bumpmult, s_bumpmult);
    dtcopy(start_t, s_start_t);
    dtcopy(record, s_record);
    dtcopy(strategy, s_strategy);
    dtcopy(estimate, s_estimate);
    dtcopy(compress, s_compress);
    dtcopy(encrypt, s_encrypt);
    dtcopy(comprate[0], s_comprate);
    dtcopy(comprate[1], s_comprate);
    dtcopy(skip_incr, s_skip_incr);
    dtcopy(skip_full, s_skip_full);
    dtcopy(no_hold, s_no_hold);
    dtcopy(kencrypt, s_kencrypt);
    dtcopy(ignore, s_ignore);
    dtcopy(index, s_index);
    dtcopy(tape_splitsize, s_tape_splitsize);
    dtcopy(split_diskbuffer, s_split_diskbuffer);
    dtcopy(fallback_splitsize, s_fallback_splitsize);
}

keytab_t tapetype_keytable[] = {
    { "COMMENT", COMMENT },
    { "LBL-TEMPL", LBL_TEMPL },
    { "BLOCKSIZE", BLOCKSIZE },
    { "FILE-PAD", FILE_PAD },
    { "FILEMARK", FILEMARK },
    { "LENGTH", LENGTH },
    { "SPEED", SPEED },
    { NULL, IDENT }
};

static void get_tapetype()
{
    int done;
    int save_overwrites;
    val_t value;

    keytab_t *save_kt;

    save_overwrites = allow_overwrites;
    allow_overwrites = 1;

    save_kt = keytable;
    keytable = tapetype_keytable;

    init_tapetype_defaults();

    get_conftoken(IDENT);
    tpcur.name = stralloc(tokenval.s);
    malloc_mark(tpcur.name);
    tpcur.seen = line_num;

    get_conftoken(LBRACE);
    get_conftoken(NL);

    done = 0;
    do {
	line_num += 1;
	get_conftoken(ANY);
	switch(tok) {

	case RBRACE:
	    done = 1;
	    break;
	case COMMENT:
	    get_simple((val_t *)&tpcur.comment, &tpcur.s_comment, STRING);
	    break;
	case LBL_TEMPL:
	    get_simple((val_t *)&tpcur.lbl_templ, &tpcur.s_lbl_templ, STRING);
	    break;
	case BLOCKSIZE:
	    get_simple((val_t *)&tpcur.blocksize, &tpcur.s_blocksize, LONG);
	    if(tpcur.blocksize < DISK_BLOCK_KB) {
		parserror("Tape blocksize must be at least %d KBytes",
			  DISK_BLOCK_KB);
	    } else if(tpcur.blocksize > MAX_TAPE_BLOCK_KB) {
		parserror("Tape blocksize must not be larger than %d KBytes",
			  MAX_TAPE_BLOCK_KB);
	    }
	    break;
	case FILE_PAD:
	    get_simple(&value, &tpcur.s_file_pad, BOOL);
	    tpcur.file_pad = (value.i != 0);
	    break;
	case LENGTH:
	    get_simple(&value, &tpcur.s_length, LONG);
	    if(value.l < 0) {
		parserror("Tape length must be positive");
	    }
	    else {
		tpcur.length = (unsigned long) value.l;
	    }
	    break;
	case FILEMARK:
	    get_simple(&value, &tpcur.s_filemark, LONG);
	    if(value.l < 0) {
		parserror("Tape file mark size must be positive");
	    }
	    else {
		tpcur.filemark = (unsigned long) value.l;
	    }
	    break;
	case SPEED:
	    get_simple((val_t *)&tpcur.speed, &tpcur.s_speed, INT);
	    if(tpcur.speed < 0) {
		parserror("Speed must be positive");
	    }
	    break;
	case IDENT:
	    copy_tapetype();
	    break;
	case NL:	/* empty line */
	    break;
	case END:	/* end of file */
	    done = 1;
	default:
	    parserror("tape type parameter expected");
	}
	if(tok != NL && tok != END) get_conftoken(NL);
    } while(!done);

    save_tapetype();

    allow_overwrites = save_overwrites;
    keytable = save_kt;
}

static void init_tapetype_defaults()
{
    tpcur.comment = stralloc("");
    tpcur.lbl_templ = stralloc("");
    tpcur.blocksize = (DISK_BLOCK_KB);
    tpcur.file_pad = 1;
    tpcur.length = 2000 * 1024;
    tpcur.filemark = 1000;
    tpcur.speed = 200;

    tpcur.s_comment = 0;
    tpcur.s_lbl_templ = 0;
    tpcur.s_blocksize = 0;
    tpcur.s_file_pad = 0;
    tpcur.s_length = 0;
    tpcur.s_filemark = 0;
    tpcur.s_speed = 0;
}

static void save_tapetype()
{
    tapetype_t *tp;

    tp = lookup_tapetype(tpcur.name);

    if(tp != (tapetype_t *)0) {
	amfree(tpcur.name);
	parserror("tapetype %s already defined on line %d", tp->name, tp->seen);
	return;
    }

    tp = alloc(sizeof(tapetype_t));
    malloc_mark(tp);
    *tp = tpcur;
    tp->next = tapelist;
    tapelist = tp;
}

static void copy_tapetype()
{
    tapetype_t *tp;

    tp = lookup_tapetype(tokenval.s);

    if(tp == NULL) {
	parserror("tape type parameter expected");
	return;
    }

#define ttcopy(v,s) if(tp->s) { tpcur.v = tp->v; tpcur.s = tp->s; }

    if(tp->s_comment) {
	tpcur.comment = newstralloc(tpcur.comment, tp->comment);
	tpcur.s_comment = tp->s_comment;
    }
    if(tp->s_lbl_templ) {
	tpcur.lbl_templ = newstralloc(tpcur.lbl_templ, tp->lbl_templ);
	tpcur.s_lbl_templ = tp->s_lbl_templ;
    }
    ttcopy(blocksize, s_blocksize);
    ttcopy(file_pad, s_file_pad);
    ttcopy(length, s_length);
    ttcopy(filemark, s_filemark);
    ttcopy(speed, s_speed);
}

keytab_t interface_keytable[] = {
    { "COMMENT", COMMENT },
    { "USE", USE },
    { NULL, IDENT }
};

static void get_interface()
{
    int done;
    int save_overwrites;
    keytab_t *save_kt;

    save_overwrites = allow_overwrites;
    allow_overwrites = 1;

    save_kt = keytable;
    keytable = interface_keytable;

    init_interface_defaults();

    get_conftoken(IDENT);
    ifcur.name = stralloc(tokenval.s);
    malloc_mark(ifcur.name);
    ifcur.seen = line_num;

    get_conftoken(LBRACE);
    get_conftoken(NL);

    done = 0;
    do {
	line_num += 1;
	get_conftoken(ANY);
	switch(tok) {

	case RBRACE:
	    done = 1;
	    break;
	case COMMENT:
	    get_simple((val_t *)&ifcur.comment, &ifcur.s_comment, STRING);
	    break;
	case USE:
	    get_simple((val_t *)&ifcur.maxusage, &ifcur.s_maxusage, INT);
	    if(ifcur.maxusage <1) {
		parserror("use must bbe positive");
	    }
	    break;
	case IDENT:
	    copy_interface();
	    break;
	case NL:	/* empty line */
	    break;
	case END:	/* end of file */
	    done = 1;
	default:
	    parserror("interface parameter expected");
	}
	if(tok != NL && tok != END) get_conftoken(NL);
    } while(!done);

    save_interface();

    allow_overwrites = save_overwrites;
    keytable = save_kt;

    return;
}

static void init_interface_defaults()
{
    ifcur.comment = stralloc("");
    ifcur.maxusage = 300;

    ifcur.s_comment = 0;
    ifcur.s_maxusage = 0;

    ifcur.curusage = 0;
}

static void save_interface()
{
    interface_t *ip;

    ip = lookup_interface(ifcur.name);

    if(ip != (interface_t *)0) {
	parserror("interface %s already defined on line %d", ip->name, ip->seen);
	return;
    }

    ip = alloc(sizeof(interface_t));
    malloc_mark(ip);
    *ip = ifcur;
    ip->next = interface_list;
    interface_list = ip;
}

static void copy_interface()
{
    interface_t *ip;

    ip = lookup_interface(tokenval.s);

    if(ip == NULL) {
	parserror("interface parameter expected");
	return;
    }

#define ifcopy(v,s) if(ip->s) { ifcur.v = ip->v; ifcur.s = ip->s; }

    if(ip->s_comment) {
	ifcur.comment = newstralloc(ifcur.comment, ip->comment);
	ifcur.s_comment = ip->s_comment;
    }
    ifcopy(maxusage, s_maxusage);
}

keytab_t dumpopts_keytable[] = {
    { "COMPRESS", COMPRESS },
    { "ENCRYPT", ENCRYPT },
    { "INDEX", INDEX },
    { "EXCLUDE-FILE", EXCLUDE_FILE },
    { "EXCLUDE-LIST", EXCLUDE_LIST },
    { "KENCRYPT", KENCRYPT },
    { "SKIP-FULL", SKIP_FULL },
    { "SKIP-INCR", SKIP_INCR },
    { NULL, IDENT }
};

static void get_dumpopts() /* XXX - for historical compatability */
{
    int done;
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = dumpopts_keytable;

    done = 0;
    do {
	get_conftoken(ANY);
	switch(tok) {
	case COMPRESS:   ckseen(&dpcur.s_compress);  dpcur.compress = COMP_FAST; break;
	case ENCRYPT:   ckseen(&dpcur.s_encrypt);  dpcur.encrypt = ENCRYPT_NONE; break;
	case EXCLUDE_FILE:
	    ckseen(&dpcur.s_exclude_file);
	    get_conftoken(STRING);
	    dpcur.exclude_file = append_sl(dpcur.exclude_file, tokenval.s);
	    break;
	case EXCLUDE_LIST:
	    ckseen(&dpcur.s_exclude_list);
	    get_conftoken(STRING);
	    dpcur.exclude_list = append_sl(dpcur.exclude_list, tokenval.s);
	    break;
	case KENCRYPT:   ckseen(&dpcur.s_kencrypt);  dpcur.kencrypt = 1; break;
	case SKIP_INCR:  ckseen(&dpcur.s_skip_incr); dpcur.skip_incr= 1; break;
	case SKIP_FULL:  ckseen(&dpcur.s_skip_full); dpcur.skip_full= 1; break;
	case INDEX:      ckseen(&dpcur.s_index);     dpcur.index    = 1; break;
	case IDENT:
	    copy_dumptype();
	    break;
	case NL: done = 1; break;
	case COMMA: break;
	case END:
	    done = 1;
	default:
	    parserror("dump option expected");
	}
    } while(!done);

    keytable = save_kt;
}

static void get_comprate()
{
    val_t var;

    get_simple(&var, &dpcur.s_comprate, REAL);
    dpcur.comprate[0] = dpcur.comprate[1] = var.r;
    if(dpcur.comprate[0] < 0) {
	parserror("full compression rate must be >= 0");
    }

    get_conftoken(ANY);
    switch(tok) {
    case NL:
	return;
    case COMMA:
	break;
    default:
	unget_conftoken();
    }

    get_conftoken(REAL);
    dpcur.comprate[1] = tokenval.r;
    if(dpcur.comprate[1] < 0) {
	parserror("incremental compression rate must be >= 0");
    }
}

keytab_t compress_keytable[] = {
    { "BEST", BEST },
    { "CLIENT", CLIENT },
    { "FAST", FAST },
    { "NONE", NONE },
    { "SERVER", SERVER },
    { "CUSTOM", CUSTOM },
    { NULL, IDENT }
};

static void get_compress()
{
    keytab_t *save_kt;
    int serv, clie, none, fast, best, custom;
    int done;
    int comp;

    save_kt = keytable;
    keytable = compress_keytable;

    ckseen(&dpcur.s_compress);

    serv = clie = none = fast = best = custom  = 0;

    done = 0;
    do {
	get_conftoken(ANY);
	switch(tok) {
	case NONE:   none = 1; break;
	case FAST:   fast = 1; break;
	case BEST:   best = 1; break;
	case CLIENT: clie = 1; break;
	case SERVER: serv = 1; break;
	case CUSTOM: custom=1; break;
	case NL:     done = 1; break;
	default:
	    done = 1;
	    serv = clie = 1; /* force an error */
	}
    } while(!done);

    if(serv + clie == 0) clie = 1;	/* default to client */
    if(none + fast + best + custom  == 0) fast = 1; /* default to fast */

    comp = -1;

    if(!serv && clie) {
	if(none && !fast && !best && !custom) comp = COMP_NONE;
	if(!none && fast && !best && !custom) comp = COMP_FAST;
	if(!none && !fast && best && !custom) comp = COMP_BEST;
	if(!none && !fast && !best && custom) comp = COMP_CUST;
    }

    if(serv && !clie) {
	if(none && !fast && !best && !custom) comp = COMP_NONE;
	if(!none && fast && !best && !custom) comp = COMP_SERV_FAST;
	if(!none && !fast && best && !custom) comp = COMP_SERV_BEST;
	if(!none && !fast && !best && custom) comp = COMP_SERV_CUST;
    }

    if(comp == -1) {
	parserror("NONE, CLIENT FAST, CLIENT BEST, CLIENT CUSTOM, SERVER FAST, SERVER BEST or SERVER CUSTOM expected");
	comp = COMP_NONE;
    }

    dpcur.compress = comp;

    keytable = save_kt;
}

keytab_t encrypt_keytable[] = {
    { "NONE", NONE },
    { "CLIENT", CLIENT },
    { "SERVER", SERVER },
    { NULL, IDENT }
};

static void get_encrypt()
{
   keytab_t *save_kt;
   int encrypt;

   save_kt = keytable;
   keytable = encrypt_keytable;

   ckseen(&dpcur.s_encrypt);

   get_conftoken(ANY);
   switch(tok) {
   case NONE:  
     encrypt = ENCRYPT_NONE; 
     break;
   case CLIENT:  
     encrypt = ENCRYPT_CUST;
     break;
   case SERVER: 
     encrypt = ENCRYPT_SERV_CUST;
     break;
   default:
     parserror("NONE, CLIENT or SERVER expected");
     encrypt = ENCRYPT_NONE;
   }

   dpcur.encrypt = encrypt;
   keytable = save_kt;	
}

keytab_t taperalgo_keytable[] = {
    { "FIRST", FIRST },
    { "FIRSTFIT", FIRSTFIT },
    { "LARGEST", LARGEST },
    { "LARGESTFIT", LARGESTFIT },
    { "SMALLEST", SMALLEST },
    { "LAST", LAST },
    { NULL, IDENT }
};

static void get_taperalgo(c_taperalgo, s_taperalgo)
val_t *c_taperalgo;
int *s_taperalgo;
{
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = taperalgo_keytable;

    ckseen(s_taperalgo);

    get_conftoken(ANY);
    switch(tok) {
    case FIRST:      c_taperalgo->i = ALGO_FIRST;      break;
    case FIRSTFIT:   c_taperalgo->i = ALGO_FIRSTFIT;   break;
    case LARGEST:    c_taperalgo->i = ALGO_LARGEST;    break;
    case LARGESTFIT: c_taperalgo->i = ALGO_LARGESTFIT; break;
    case SMALLEST:   c_taperalgo->i = ALGO_SMALLEST;   break;
    case LAST:       c_taperalgo->i = ALGO_LAST;       break;
    default:
	parserror("FIRST, FIRSTFIT, LARGEST, LARGESTFIT, SMALLEST or LAST expected");
    }

    keytable = save_kt;
}

keytab_t priority_keytable[] = {
    { "HIGH", HIGH },
    { "LOW", LOW },
    { "MEDIUM", MEDIUM },
    { NULL, IDENT }
};

static void get_priority()
{
    int pri;
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = priority_keytable;

    ckseen(&dpcur.s_priority);

    get_conftoken(ANY);
    switch(tok) {
    case LOW: pri = 0; break;
    case MEDIUM: pri = 1; break;
    case HIGH: pri = 2; break;
    case INT: pri = tokenval.i; break;
    default:
	parserror("LOW, MEDIUM, HIGH or integer expected");
	pri = 0;
    }
    dpcur.priority = pri;

    keytable = save_kt;
}

keytab_t strategy_keytable[] = {
    { "HANOI", HANOI },
    { "NOFULL", NOFULL },
    { "NOINC", NOINC },
    { "SKIP", SKIP },
    { "STANDARD", STANDARD },
    { "INCRONLY", INCRONLY },
    { NULL, IDENT }
};

static void get_strategy()
{
    int strat;
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = strategy_keytable;

    ckseen(&dpcur.s_strategy);

    get_conftoken(ANY);
    switch(tok) {
    case SKIP:
	strat = DS_SKIP;
	break;
    case STANDARD:
	strat = DS_STANDARD;
	break;
    case NOFULL:
	strat = DS_NOFULL;
	break;
    case NOINC:
	strat = DS_NOINC;
	break;
    case HANOI:
	strat = DS_HANOI;
	break;
    case INCRONLY:
	strat = DS_INCRONLY;
	break;
    default:
	parserror("STANDARD or NOFULL expected");
	strat = DS_STANDARD;
    }
    dpcur.strategy = strat;

    keytable = save_kt;
}

keytab_t estimate_keytable[] = {
    { "CLIENT", CLIENT },
    { "SERVER", SERVER },
    { "CALCSIZE", CALCSIZE }
};

static void get_estimate()
{
    int estime;
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = estimate_keytable;

    ckseen(&dpcur.s_estimate);

    get_conftoken(ANY);
    switch(tok) {
    case CLIENT:
	estime = ES_CLIENT;
	break;
    case SERVER:
	estime = ES_SERVER;
	break;
    case CALCSIZE:
	estime = ES_CALCSIZE;
	break;
    default:
	parserror("CLIENT, SERVER or CALCSIZE expected");
	estime = ES_CLIENT;
    }
    dpcur.estimate = estime;

    keytable = save_kt;
}

keytab_t exclude_keytable[] = {
    { "LIST", LIST },
    { "FILE", EFILE },
    { "APPEND", APPEND },
    { "OPTIONAL", OPTIONAL },
    { NULL, IDENT }
};

static void get_exclude()
{
    int list, got_one = 0;
    keytab_t *save_kt;
    sl_t *exclude;
    int optional = 0;
    int append = 0;

    save_kt = keytable;
    keytable = exclude_keytable;

    get_conftoken(ANY);
    if(tok == LIST) {
	list = 1;
	exclude = dpcur.exclude_list;
	ckseen(&dpcur.s_exclude_list);
	get_conftoken(ANY);
    }
    else {
	list = 0;
	exclude = dpcur.exclude_file;
	ckseen(&dpcur.s_exclude_file);
	if(tok == EFILE) get_conftoken(ANY);
    }

    if(tok == OPTIONAL) {
	get_conftoken(ANY);
	optional = 1;
    }

    if(tok == APPEND) {
	get_conftoken(ANY);
	append = 1;
    }
    else {
	free_sl(exclude);
	exclude = NULL;
	append = 0;
    }

    while(tok == STRING) {
	exclude = append_sl(exclude, tokenval.s);
	got_one = 1;
	get_conftoken(ANY);
    }
    unget_conftoken();

    if(got_one == 0) { free_sl(exclude); exclude = NULL; }

    if(list == 0)
	dpcur.exclude_file = exclude;
    else {
	dpcur.exclude_list = exclude;
	if(!append || optional)
	    dpcur.exclude_optional = optional;
    }

    keytable = save_kt;
}


static void get_include()
{
    int list, got_one = 0;
    keytab_t *save_kt;
    sl_t *include;
    int optional = 0;
    int append = 0;

    save_kt = keytable;
    keytable = exclude_keytable;

    get_conftoken(ANY);
    if(tok == LIST) {
	list = 1;
	include = dpcur.include_list;
	ckseen(&dpcur.s_include_list);
	get_conftoken(ANY);
    }
    else {
	list = 0;
	include = dpcur.include_file;
	ckseen(&dpcur.s_include_file);
	if(tok == EFILE) get_conftoken(ANY);
    }

    if(tok == OPTIONAL) {
	get_conftoken(ANY);
	optional = 1;
    }

    if(tok == APPEND) {
	get_conftoken(ANY);
	append = 1;
    }
    else {
	free_sl(include);
	include = NULL;
	append = 0;
    }

    while(tok == STRING) {
	include = append_sl(include, tokenval.s);
	got_one = 1;
	get_conftoken(ANY);
    }
    unget_conftoken();

    if(got_one == 0) { free_sl(include); include = NULL; }

    if(list == 0)
	dpcur.include_file = include;
    else {
	dpcur.include_list = include;
	if(!append || optional)
	    dpcur.include_optional = optional;
    }

    keytable = save_kt;
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

int ColumnDataCount()
{
    return sizeof(ColumnData) / sizeof(ColumnData[0]);
}

/* conversion from string to table index
 */
int
StringToColumn(s)
    char *s;
{
    int cn;

    for (cn=0; ColumnData[cn].Name != NULL; cn++) {
    	if (strcasecmp(s, ColumnData[cn].Name) == 0) {
	    break;
	}
    }
    return cn;
}

char
LastChar(s)
    char *s;
{
    return s[strlen(s)-1];
}

int
SetColumDataFromString(ci, s, errstr)
    ColumnInfo* ci;
    char *s;
    char **errstr;
{
    /* Convert from a Columspec string to our internal format
     * of columspec. The purpose is to provide this string
     * as configuration paramter in the amanda.conf file or
     * (maybe) as environment variable.
     * 
     * This text should go as comment into the sample amanda.conf
     *
     * The format for such a ColumnSpec string s is a ',' seperated
     * list of triples. Each triple consists of
     *   -the name of the column (as in ColumnData.Name)
     *   -prefix before the column
     *   -the width of the column
     *       if set to -1 it will be recalculated
     *	 to the maximum length of a line to print.
     * Example:
     * 	"Disk=1:17,HostName=1:10,OutKB=1:7"
     * or
     * 	"Disk=1:-1,HostName=1:10,OutKB=1:7"
     *	
     * You need only specify those colums that should be changed from
     * the default. If nothing is specified in the configfile, the
     * above compiled in values will be in effect, resulting in an
     * output as it was all the time.
     *							ElB, 1999-02-24.
     */
#ifdef TEST
    char *myname= "SetColumDataFromString";
#endif

    while (s && *s) {
	int Space, Width;
	int cn;
    	char *eon= strchr(s, '=');

	if (eon == NULL) {
	    *errstr = stralloc2("invalid columnspec: ", s);
#ifdef TEST
	    fprintf(stderr, "%s: %s\n", myname, *errstr);
#endif
	    return -1;
	}
	*eon= '\0';
	cn=StringToColumn(s);
	if (ColumnData[cn].Name == NULL) {
	    *errstr = stralloc2("invalid column name: ", s);
#ifdef TEST
	    fprintf(stderr, "%s: %s\n", myname, *errstr);
#endif
	    return -1;
	}
	if (sscanf(eon+1, "%d:%d", &Space, &Width) != 2) {
	    *errstr = stralloc2("invalid format: ", eon + 1);
#ifdef TEST
	    fprintf(stderr, "%s: %s\n", myname, *errstr);
#endif
	    return -1;
	}
	ColumnData[cn].Width= Width;
	ColumnData[cn].PrefixSpace= Space;
	if (LastChar(ColumnData[cn].Format) == 's') {
	    if (Width < 0)
		ColumnData[cn].MaxWidth= 1;
	    else
		if (Width > ColumnData[cn].Precision)
		    ColumnData[cn].Precision= Width;
	}
	else if (Width < ColumnData[cn].Precision)
	    ColumnData[cn].Precision= Width;
	s= strchr(eon+1, ',');
	if (s != NULL)
	    s++;
    }
    return 0;
}


char *taperalgo2str(taperalgo)
int taperalgo;
{
    if(taperalgo == ALGO_FIRST) return "FIRST";
    if(taperalgo == ALGO_FIRSTFIT) return "FIRSTFIT";
    if(taperalgo == ALGO_LARGEST) return "LARGEST";
    if(taperalgo == ALGO_LARGESTFIT) return "LARGESTFIT";
    if(taperalgo == ALGO_SMALLEST) return "SMALLEST";
    if(taperalgo == ALGO_LAST) return "LAST";
    return "UNKNOWN";
}

long int getconf_unit_divisor()
{
    return unit_divisor;
}

/* ------------------------ */


#ifdef TEST

void
dump_configuration(filename)
    char *filename;
{
    tapetype_t *tp;
    dumptype_t *dp;
    interface_t *ip;
    holdingdisk_t *hp;
    time_t st;
    struct tm *stm;

    printf("AMANDA CONFIGURATION FROM FILE \"%s\":\n\n", filename);

    printf("conf_org = \"%s\"\n", getconf_str(CNF_ORG));
    printf("conf_mailto = \"%s\"\n", getconf_str(CNF_MAILTO));
    printf("conf_dumpuser = \"%s\"\n", getconf_str(CNF_DUMPUSER));
    printf("conf_printer = \"%s\"\n", getconf_str(CNF_PRINTER));
    printf("conf_tapedev = \"%s\"\n", getconf_str(CNF_TAPEDEV));
    printf("conf_rawtapedev = \"%s\"\n", getconf_str(CNF_RAWTAPEDEV));
    printf("conf_tpchanger = \"%s\"\n", getconf_str(CNF_TPCHANGER));
    printf("conf_chngrdev = \"%s\"\n", getconf_str(CNF_CHNGRDEV));
    printf("conf_chngrfile = \"%s\"\n", getconf_str(CNF_CHNGRFILE));
    printf("conf_labelstr = \"%s\"\n", getconf_str(CNF_LABELSTR));
    printf("conf_tapelist = \"%s\"\n", getconf_str(CNF_TAPELIST));
    printf("conf_infofile = \"%s\"\n", getconf_str(CNF_INFOFILE));
    printf("conf_logdir = \"%s\"\n", getconf_str(CNF_LOGDIR));
    printf("conf_diskfile = \"%s\"\n", getconf_str(CNF_DISKFILE));
    printf("conf_tapetype = \"%s\"\n", getconf_str(CNF_TAPETYPE));

    printf("conf_dumpcycle = %d\n", getconf_int(CNF_DUMPCYCLE));
    printf("conf_runspercycle = %d\n", getconf_int(CNF_RUNSPERCYCLE));
    printf("conf_runtapes = %d\n", getconf_int(CNF_RUNTAPES));
    printf("conf_tapecycle = %d\n", getconf_int(CNF_TAPECYCLE));
    printf("conf_bumppercent = %d\n", getconf_int(CNF_BUMPPERCENT));
    printf("conf_bumpsize = %d\n", getconf_int(CNF_BUMPSIZE));
    printf("conf_bumpdays = %d\n", getconf_int(CNF_BUMPDAYS));
    printf("conf_bumpmult = %f\n", getconf_real(CNF_BUMPMULT));
    printf("conf_netusage = %d\n", getconf_int(CNF_NETUSAGE));
    printf("conf_inparallel = %d\n", getconf_int(CNF_INPARALLEL));
    printf("conf_dumporder = \"%s\"\n", getconf_str(CNF_DUMPORDER));
    /*printf("conf_timeout = %d\n", getconf_int(CNF_TIMEOUT));*/
    printf("conf_maxdumps = %d\n", getconf_int(CNF_MAXDUMPS));
    printf("conf_etimeout = %d\n", getconf_int(CNF_ETIMEOUT));
    printf("conf_dtimeout = %d\n", getconf_int(CNF_DTIMEOUT));
    printf("conf_ctimeout = %d\n", getconf_int(CNF_CTIMEOUT));
    printf("conf_tapebufs = %d\n", getconf_int(CNF_TAPEBUFS));
    printf("conf_autoflush  = %d\n", getconf_int(CNF_AUTOFLUSH));
    printf("conf_reserve  = %d\n", getconf_int(CNF_RESERVE));
    printf("conf_maxdumpsize  = " AM64_FMT "\n", getconf_am64(CNF_MAXDUMPSIZE));
    printf("conf_amrecover_do_fsf  = %d\n", getconf_int(CNF_AMRECOVER_DO_FSF));
    printf("conf_amrecover_check_label  = %d\n", getconf_int(CNF_AMRECOVER_CHECK_LABEL));
    printf("conf_amrecover_changer = \"%s\"\n", getconf_str(CNF_AMRECOVER_CHANGER));
    printf("conf_taperalgo  = %s\n", taperalgo2str(getconf_int(CNF_TAPERALGO)));
    printf("conf_displayunit  = %s\n", getconf_str(CNF_DISPLAYUNIT));

    /*printf("conf_diskdir = \"%s\"\n", getconf_str(CNF_DISKDIR));*/
    /*printf("conf_disksize = %d\n", getconf_int(CNF_DISKSIZE));*/
    printf("conf_columnspec = \"%s\"\n", getconf_str(CNF_COLUMNSPEC));
    printf("conf_indexdir = \"%s\"\n", getconf_str(CNF_INDEXDIR));
    printf("num_holdingdisks = %d\n", num_holdingdisks);
    printf("conf_krb5keytab = \"%s\"\n", getconf_str(CNF_KRB5KEYTAB));
    printf("conf_krb5principal = \"%s\"\n", getconf_str(CNF_KRB5PRINCIPAL));
    printf("conf_label_new_tapes  = \"%s\"\n", getconf_str(CNF_LABEL_NEW_TAPES));
    printf("conf_usetimestamps  = %d\n", getconf_int(CNF_USETIMESTAMPS));
    for(hp = holdingdisks; hp != NULL; hp = hp->next) {
	printf("\nHOLDINGDISK %s:\n", hp->name);
	printf("	COMMENT \"%s\"\n", hp->comment);
	printf("	DISKDIR \"%s\"\n", hp->diskdir);
	printf("	SIZE %ld\n", (long)hp->disksize);
	printf("	CHUNKSIZE %ld\n", (long)hp->chunksize);
    }

    for(tp = tapelist; tp != NULL; tp = tp->next) {
	printf("\nTAPETYPE %s:\n", tp->name);
	printf("	COMMENT \"%s\"\n", tp->comment);
	printf("	LBL_TEMPL %s\n", tp->lbl_templ);
	printf("	BLOCKSIZE %ld\n", (long)tp->blocksize);
	printf("	FILE_PAD %s\n", (tp->file_pad) ? "YES" : "NO");
	printf("	LENGTH %lu\n", (unsigned long)tp->length);
	printf("	FILEMARK %lu\n", (unsigned long)tp->filemark);
	printf("	SPEED %ld\n", (long)tp->speed);
    }

    for(dp = dumplist; dp != NULL; dp = dp->next) {
	printf("\nDUMPTYPE %s:\n", dp->name);
	printf("	COMMENT \"%s\"\n", dp->comment);
	printf("	PROGRAM \"%s\"\n", dp->program);
	printf("	SERVER_CUSTOM_COMPRESS \"%s\"\n", dp->srvcompprog);
	printf("	CLIENT_CUSTOM_COMPRESS \"%s\"\n", dp->clntcompprog);
	printf("	SERVER_ENCRYPT \"%s\"\n", dp->srv_encrypt);
	printf("	CLIENT_ENCRYPT \"%s\"\n", dp->clnt_encrypt);
	printf("	SERVER_DECRYPT_OPTION \"%s\"\n", dp->srv_decrypt_opt);
	printf("	CLIENT_DECRYPT_OPTION \"%s\"\n", dp->clnt_decrypt_opt);
	printf("	AMANDAD_PATH \"%s\"\n", dp->amandad_path);
	printf("	CLIENT_USERNAME \"%s\"\n", dp->client_username);
	printf("	PRIORITY %ld\n", (long)dp->priority);
	printf("	DUMPCYCLE %ld\n", (long)dp->dumpcycle);
	st = dp->start_t;
	if(st) {
	    stm = localtime(&st);
	    printf("	STARTTIME %d:%02d:%02d\n",
	      stm->tm_hour, stm->tm_min, stm->tm_sec);
	}
	if(dp->exclude_file) {
	    sle_t *excl;
	    printf("	EXCLUDE FILE");
	    for(excl = dp->exclude_file->first; excl != NULL; excl =excl->next){
		printf(" \"%s\"", excl->name);
	    }
	    printf("\n");
	}
	if(dp->exclude_list) {
	    sle_t *excl;
	    printf("	EXCLUDE LIST");
	    for(excl = dp->exclude_list->first; excl != NULL; excl =excl->next){
		printf(" \"%s\"", excl->name);
	    }
	    printf("\n");
	}
	if(dp->include_file) {
	    sle_t *incl;
	    printf("	INCLUDE FILE");
	    for(incl = dp->include_file->first; incl != NULL; incl =incl->next){
		printf(" \"%s\"", incl->name);
	    }
	    printf("\n");
	}
	if(dp->include_list) {
	    sle_t *incl;
	    printf("	INCLUDE LIST");
	    for(incl = dp->include_list->first; incl != NULL; incl =incl->next){
		printf(" \"%s\"", incl->name);
	    }
	    printf("\n");
	}
	printf("	FREQUENCY %ld\n", (long)dp->frequency);
	printf("	MAXDUMPS %d\n", dp->maxdumps);
	printf("	MAXPROMOTEDAY %d\n", dp->maxpromoteday);
	printf("	STRATEGY ");
	switch(dp->strategy) {
	case DS_SKIP:
	    printf("SKIP");
	    break;
	case DS_STANDARD:
	    printf("STANDARD");
	    break;
	case DS_NOFULL:
	    printf("NOFULL");
	    break;
	case DS_NOINC:
	    printf("NOINC");
	    break;
	case DS_HANOI:
	    printf("HANOI");
	    break;
	case DS_INCRONLY:
	    printf("INCRONLY");
	    break;
	}
	putchar('\n');
	printf("	ESTIMATE ");
	switch(dp->estimate) {
	case ES_CLIENT:
	    printf("CLIENT");
	    break;
	case ES_SERVER:
	    printf("SERVER");
	    break;
	case ES_CALCSIZE:
	    printf("CALCSIZE");
	    break;
	}
	putchar('\n');
	printf("	COMPRATE %f, %f\n", dp->comprate[0], dp->comprate[1]);

	printf("	OPTIONS: ");

	switch(dp->compress) {
	case COMP_NONE:
	    printf("NO-COMPRESS ");
	    break;
	case COMP_FAST:
	    printf("COMPRESS-FAST ");
	    break;
	case COMP_BEST:
	    printf("COMPRESS-BEST ");
	    break;
	case COMP_CUST:
	    printf("COMPRESS-CUST ");
	    break;
	case COMP_SERV_FAST:
	    printf("SRVCOMP-FAST ");
	    break;
	case COMP_SERV_BEST:
	    printf("SRVCOMP-BEST ");
	    break;
	case COMP_SERV_CUST:
	    printf("SRVCOMP-CUST ");
	    break;
	}

	switch(dp->encrypt) {
	case ENCRYPT_NONE:
	    printf("ENCRYPT-NONE ");
	    break;
	case ENCRYPT_CUST:
	    printf("ENCRYPT-CUST ");
	    break;
	case ENCRYPT_SERV_CUST:
	    printf("ENCRYPT-SERV-CUST ");
	    break;
	}

	if(!dp->record) printf("NO-");
	printf("RECORD");
	printf(" %s-AUTH", dp->security_driver);
	if(dp->skip_incr) printf(" SKIP-INCR");
	if(dp->skip_full) printf(" SKIP-FULL");
	if(dp->no_hold) printf(" NO-HOLD");
	if(dp->kencrypt) printf(" KENCRYPT");
	/* an ignored disk will never reach this point */
	assert(!dp->ignore);
	if(dp->index) printf(" INDEX");
	putchar('\n');
    }

    for(ip = interface_list; ip != NULL; ip = ip->next) {
	printf("\nINTERFACE %s:\n", ip->name);
	printf("	COMMENT \"%s\"\n", ip->comment);
	printf("	USE %d\n", ip->maxusage);
    }
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
      diskfile = getconf_str(CNF_DISKFILE);
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
generic_get_security_conf(string, arg)
	char *string;
	void *arg;
{
	if(!string || !*string)
		return(NULL);

	if(strcmp(string, "krb5principal")==0) {
		return(getconf_str(CNF_KRB5PRINCIPAL));
	} else if(strcmp(string, "krb5keytab")==0) {
		return(getconf_str(CNF_KRB5KEYTAB));
	}
	return(NULL);
}
