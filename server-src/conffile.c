/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991 University of Maryland
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
 * $Id: conffile.c,v 1.28 1997/12/19 17:23:40 george Exp $
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
#include "clock.h"

#define BIGINT	1000000000		/* 2 million yrs @ 1 per day */

/* internal types and variables */

/*
 * XXX - this is used by the krb4 stuff.
 * Hopefully nobody will need this here.  (not very likely).  -kovert
 */
#if defined(INTERFACE)
#  undef INTERFACE
#endif

typedef enum {
    UNKNOWN, ANY, COMMA, LBRACE, RBRACE, NL, END,
    IDENT, INT, BOOL, REAL, STRING, TIME,

    /* config parameters */
    INCLUDEFILE,
    ORG, MAILTO, DUMPUSER,
    TAPECYCLE, TAPEDEV, CHNGRDEV, CHNGRFILE, LABELSTR,
    BUMPSIZE, BUMPDAYS, BUMPMULT,
    TAPELIST, DISKFILE, INFOFILE, LOGFILE,
    DISKDIR, DISKSIZE, INDEXDIR, NETUSAGE, INPARALLEL, TIMEOUT,
    TPCHANGER, RUNTAPES,
    DEFINE, DUMPTYPE, TAPETYPE, INTERFACE,

    /* holding disk */
    COMMENT, DIRECTORY, USE,

    /* dump type */
    /*COMMENT,*/ PROGRAM, DUMPCYCLE, MAXCYCLE, MAXDUMPS,
    OPTIONS, PRIORITY, FREQUENCY, INDEX,
    STARTTIME, COMPRESS, AUTH, STRATEGY,
    SKIP_INCR, SKIP_FULL, RECORD, HOLDING,
    EXCLUDE, KENCRYPT, IGNORE, COMPRATE,

    /* tape type */
    /*COMMENT,*/ FILEMARK, LENGTH, SPEED,

    /* network interface */
    /*COMMENT, USE,*/

    /* dump options (obsolete) */
    EXCLUDE_FILE, EXCLUDE_LIST,

    /* compress */
    NONE, FAST, BEST, SERVER, CLIENT,

    /* priority */
    LOW, MEDIUM, HIGH,

    /* authentication */
    KRB4_AUTH, BSD_AUTH,

    /* dump strategy */
    SKIP, STANDARD, NOFULL, NOINC, HANOI,

    /* exclude list */
    LIST,

    /* numbers */
    INFINITY, MULT1, MULT7, MULT1K, MULT1M,

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
    double r;
    char *s;
} val_t;

/* visible holding disk variables */

holdingdisk_t *holdingdisks;
int num_holdingdisks;

/* configuration parameters */

/* strings */
static val_t conf_org;
static val_t conf_mailto;
static val_t conf_dumpuser;
static val_t conf_tapedev;
static val_t conf_tpchanger;
static val_t conf_chngrdev;
static val_t conf_chngrfile;
static val_t conf_labelstr;
static val_t conf_tapelist;
static val_t conf_infofile;
static val_t conf_logfile;
static val_t conf_diskfile;
static val_t conf_diskdir;
static val_t conf_tapetype;
static val_t conf_indexdir;

/* ints */
static val_t conf_dumpcycle;
static val_t conf_maxcycle;
static val_t conf_tapecycle;
static val_t conf_runtapes;
static val_t conf_disksize;
static val_t conf_netusage;
static val_t conf_inparallel;
static val_t conf_timeout;
static val_t conf_maxdumps;
static val_t conf_bumpsize;
static val_t conf_bumpdays;

/* reals */
static val_t conf_bumpmult;

/* other internal variables */
static holdingdisk_t hdcur;

static tapetype_t tpcur;

static dumptype_t dpcur;

static interface_t ifcur;

static int seen_org, seen_mailto, seen_dumpuser;
static int seen_tapedev, seen_tpchanger, seen_chngrdev, seen_chngrfile;
static int seen_labelstr, seen_runtapes, seen_maxdumps;
static int seen_tapelist, seen_infofile, seen_diskfile, seen_diskdir;
static int seen_logfile, seen_bumpsize, seen_bumpmult, seen_bumpdays;
static int seen_tapetype, seen_dumpcycle, seen_maxcycle, seen_tapecycle;
static int seen_disksize, seen_netusage, seen_inparallel, seen_timeout;
static int seen_indexdir;

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
static void get_priority P((void));
static void get_auth P((void));
static void get_strategy P((void));
static void get_exclude P((void));

static void get_simple P((val_t *var, int *seen, tok_t type));
static int get_time P((void));
static int get_number P((void));
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

    ip = alloc(sizeof(interface_t));
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
    { "TAPEDEV", CNF_TAPEDEV, STRING },
    { "TPCHANGER", CNF_TPCHANGER, STRING },
    { "CHANGERDEV", CNF_CHNGRDEV, STRING },
    { "CHANGERFILE", CNF_CHNGRFILE, STRING },
    { "LABELSTR", CNF_LABELSTR, STRING },
    { "TAPELIST", CNF_TAPELIST, STRING },
    { "DISKFILE", CNF_DISKFILE, STRING },
    { "INFOFILE", CNF_INFOFILE, STRING },
    { "LOGFILE", CNF_LOGFILE, STRING },
    /*{ "DISKDIR", CNF_DISKDIR, STRING },*/
    { "INDEXDIR", CNF_INDEXDIR, STRING },
    { "TAPETYPE", CNF_TAPETYPE, STRING },
    { "DUMPCYCLE", CNF_DUMPCYCLE, INT },
    { "MINCYCLE",  CNF_DUMPCYCLE, INT },
    { "RUNTAPES",   CNF_RUNTAPES, INT },
    { "TAPECYCLE", CNF_TAPECYCLE, INT },
    /*{ "DISKSIZE", CNF_DISKSIZE, INT },*/
    { "BUMPDAYS", CNF_BUMPDAYS, INT },
    { "BUMPSIZE", CNF_BUMPSIZE, INT },
    { "BUMPMULT", CNF_BUMPMULT, REAL },
    { "NETUSAGE", CNF_NETUSAGE, INT },
    { "INPARALLEL", CNF_INPARALLEL, INT },
    /*{ "TIMEOUT", CNF_TIMEOUT, INT },*/
    { "MAXDUMPS", CNF_MAXDUMPS, INT },
    { NULL }
};

char *getconf_byname(str)
char *str;
{
    char *p;
    static char tmpstr[256];
    struct byname *np;

    for(p = tmpstr; *str; p++, str++) {
	if(islower(*str)) *p = toupper(*str);
	else *p = *str;
    }
    *p = '\0';

    for(np = byname_table; np->name != NULL; np++)
	if(!strcmp(np->name, tmpstr)) break;

    if(np->name == NULL) return NULL;

    if(np->typ == INT) {
	ap_snprintf(tmpstr, sizeof(tmpstr), "%d", getconf_int(np->parm));
    } else if(np->typ == REAL) {
	ap_snprintf(tmpstr, sizeof(tmpstr), "%f", getconf_real(np->parm));
    } else {
	strncpy(tmpstr, getconf_str(np->parm), sizeof(tmpstr)-1);
	tmpstr[sizeof(tmpstr)-1] = '\0';
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
    case CNF_LOGFILE: return seen_logfile;
    case CNF_BUMPSIZE: return seen_bumpsize;
    case CNF_BUMPMULT: return seen_bumpmult;
    case CNF_BUMPDAYS: return seen_bumpdays;
    case CNF_TAPETYPE: return seen_tapetype;
    case CNF_DUMPCYCLE: return seen_dumpcycle;
    /*case CNF_MAXCYCLE: return seen_maxcycle;*/
    case CNF_TAPECYCLE: return seen_tapecycle;
    /*case CNF_DISKSIZE: return seen_disksize;*/
    case CNF_NETUSAGE: return seen_netusage;
    case CNF_INPARALLEL: return seen_inparallel;
    /*case CNF_TIMEOUT: return seen_timeout;*/
    case CNF_INDEXDIR: return seen_indexdir;
    default: return 0;
    }
}

int getconf_int(parm)
confparm_t parm;
{
    int r = 0;

    switch(parm) {

    case CNF_DUMPCYCLE: r = conf_dumpcycle.i; break;
    case CNF_TAPECYCLE: r = conf_tapecycle.i; break;
    case CNF_RUNTAPES: r = conf_runtapes.i; break;
    /*case CNF_DISKSIZE: r = conf_disksize.i; break;*/
    case CNF_BUMPSIZE: r = conf_bumpsize.i; break;
    case CNF_BUMPDAYS: r = conf_bumpdays.i; break;
    case CNF_NETUSAGE: r = conf_netusage.i; break;
    case CNF_INPARALLEL: r = conf_inparallel.i; break;
    /*case CNF_TIMEOUT: r = conf_timeout.i; break;*/
    case CNF_MAXDUMPS: r = conf_maxdumps.i; break;

    default:
	assert(0);
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
	assert(0);
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
    case CNF_TAPEDEV: r = conf_tapedev.s; break;
    case CNF_TPCHANGER: r = conf_tpchanger.s; break;
    case CNF_CHNGRDEV: r = conf_chngrdev.s; break;
    case CNF_CHNGRFILE: r = conf_chngrfile.s; break;
    case CNF_LABELSTR: r = conf_labelstr.s; break;
    case CNF_TAPELIST: r = conf_tapelist.s; break;
    case CNF_INFOFILE: r = conf_infofile.s; break;
    case CNF_LOGFILE: r = conf_logfile.s; break;
    case CNF_DISKFILE: r = conf_diskfile.s; break;
    /*case CNF_DISKDIR: r = conf_diskdir.s; break;*/
    case CNF_TAPETYPE: r = conf_tapetype.s; break;
    case CNF_INDEXDIR: r = conf_indexdir.s; break;

    default:
	assert(0);
	/* NOTREACHED */
    }
    return r;
}

dumptype_t *lookup_dumptype(str)
char *str;
{
    dumptype_t *p;

    for(p = dumplist; p != NULL; p = p->next) {
	if(!strcmp(p->name, str)) return p;
    }
    return NULL;
}

tapetype_t *lookup_tapetype(str)
char *str;
{
    tapetype_t *p;

    for(p = tapelist; p != NULL; p = p->next) {
	if(!strcmp(p->name, str)) return p;
    }
    return NULL;
}

interface_t *lookup_interface(str)
char *str;
{
    interface_t *p;

    for(p = interface_list; p != NULL; p = p->next) {
	if(!strcmp(p->name, str)) return p;
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
    /* defaults for exported variables */

    conf_org.s = newstralloc(conf_org.s,
#ifdef DEFAULT_CONFIG
		DEFAULT_CONFIG
#else
		"YOUR ORG"
#endif
		);
    conf_mailto.s = newstralloc(conf_mailto.s, "operators");
    conf_dumpuser.s = newstralloc(conf_dumpuser.s,
#ifdef CLIENT_LOGIN
		CLIENT_LOGIN
#else
		"bin"
#endif
		);
    conf_tapedev.s = newstralloc(conf_tapedev.s,
#ifdef DEFAULT_TAPE_DEVICE
		DEFAULT_TAPE_DEVICE
#else
		"/dev/rmt8"
#endif
		);
    conf_tpchanger.s = newstralloc(conf_tpchanger.s, "");
    conf_chngrdev.s = newstralloc(conf_chngrdev.s,
#ifdef DEFAULT_CHANGER_DEVICE
				  DEFAULT_CHANGER_DEVICE
#else
				  "/dev/null"
#endif
				  );
    conf_chngrfile.s =
#ifdef LOG_DIR
			newstralloc2(conf_chngrfile.s,
				     LOG_DIR, "/changer-status"
#else
			newstralloc (conf_chngrfile.s,
				     "/usr/adm/amanda/changer-status"
#endif
				     );
    conf_labelstr.s = newstralloc(conf_labelstr.s, ".*");
    conf_tapelist.s = newstralloc(conf_tapelist.s, "tapelist");
    conf_infofile.s = newstralloc(conf_infofile.s, "/usr/adm/amanda/curinfo");
    conf_logfile.s =
#ifdef LOG_DIR
		     newstralloc2(conf_logfile.s,
				  LOG_DIR, "/log"
#else
		     newstralloc (conf_logfile.s,
				  "/usr/adm/amanda/log"
#endif
				  );
    conf_diskfile.s = newstralloc(conf_diskfile.s, "disklist");
    conf_diskdir.s  = newstralloc(conf_diskdir.s,  "/dumps/amanda");
    conf_tapetype.s = newstralloc(conf_tapetype.s, "EXABYTE");
    conf_indexdir.s = newstralloc(conf_indexdir.s, "/usr/adm/amanda/index");

    conf_dumpcycle.i	= 10;
    conf_tapecycle.i	= 15;
    conf_runtapes.i	= 1;
    conf_disksize.i	= 200*1024;
    conf_netusage.i	= 300;
    conf_inparallel.i	= 10;
    conf_maxdumps.i	= 1;
    conf_timeout.i	= 2;
    conf_bumpsize.i	= 10*1024;
    conf_bumpdays.i	= 2;
    conf_bumpmult.r	= 1.5;

    /* defaults for internal variables */

    seen_org = seen_mailto = seen_dumpuser = seen_tapedev = 0;
    seen_tpchanger = seen_chngrdev = seen_chngrfile = 0;
    seen_labelstr = seen_runtapes = seen_maxdumps = 0;
    seen_tapelist = seen_infofile = seen_diskfile = seen_diskdir = 0;
    seen_logfile = seen_bumpsize = seen_bumpmult = seen_bumpdays = 0;
    seen_tapetype = seen_dumpcycle = seen_maxcycle = seen_tapecycle = 0;
    seen_disksize = seen_netusage = seen_inparallel = seen_timeout = 0;
    seen_indexdir = 0;
    line_num = got_parserror = 0;
    allow_overwrites = 0;
    token_pushed = 0;

    while(holdingdisks != NULL) {
	holdingdisk_t *hp;

	hp = holdingdisks;
	holdingdisks = holdingdisks->next;
	free(hp);
    }
    num_holdingdisks = 0;

    /* free any previously declared dump, tape and interface types */

    while(dumplist != NULL) {
	dumptype_t *dp;

	dp = dumplist;
	dumplist = dumplist->next;
	free(dp);
    }
    while(tapelist != NULL) {
	tapetype_t *tp;

	tp = tapelist;
	tapelist = tapelist->next;
	free(tp);
    }
    while(interface_list != NULL) {
	interface_t *ip;

	ip = interface_list;
	interface_list = interface_list->next;
	free(ip);
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
    dpcur.name = "SRVCOMPRESS"; dpcur.seen = -1;
    dpcur.compress = COMP_SERV_FAST; dpcur.s_compress = -1;
    save_dumptype();

    init_dumptype_defaults();
    dpcur.name = "BSD-AUTH"; dpcur.seen = -1;
    dpcur.auth = AUTH_BSD; dpcur.s_auth = -1;
    save_dumptype();

    init_dumptype_defaults();
    dpcur.name = "KRB4-AUTH"; dpcur.seen = -1;
    dpcur.auth = AUTH_KRB4; dpcur.s_auth = -1;
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

    confname = stralloc(filename);

    if((conf = fopen(confname, "r")) == NULL)
	error("could not open conf file \"%s\": %s", confname, strerror(errno));

    line_num = 0;

    /* read_confline() can invoke us recursively via "includefile" */
    while(read_confline());
    fclose(conf);

    free(confname);

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
    { "DEFINE", DEFINE },
    { "DISKDIR", DISKDIR },	/* XXX - historical */
    { "DISKFILE", DISKFILE },
    { "DISKSIZE", DISKSIZE },	/* XXX - historical */
    { "DUMPCYCLE", DUMPCYCLE },
    { "DUMPTYPE", DUMPTYPE },
    { "DUMPUSER", DUMPUSER },
    { "HOLDINGDISK", HOLDING },
    { "INCLUDEFILE", INCLUDEFILE },
    { "INDEXDIR", INDEXDIR },
    { "INFOFILE", INFOFILE },
    { "INPARALLEL", INPARALLEL },
    { "INTERFACE", INTERFACE },
    { "LABELSTR", LABELSTR },
    { "LOGFILE", LOGFILE },
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
    case DUMPCYCLE: get_simple(&conf_dumpcycle, &seen_dumpcycle, INT);    break;
    case MAXCYCLE:  get_simple(&conf_maxcycle,  &seen_maxcycle,  INT);    break;
    case TAPECYCLE: get_simple(&conf_tapecycle, &seen_tapecycle, INT);    break;
    case RUNTAPES:  get_simple(&conf_runtapes,  &seen_runtapes,  INT);    break;
    case TAPEDEV:   get_simple(&conf_tapedev,   &seen_tapedev,   STRING); break;
    case TPCHANGER: get_simple(&conf_tpchanger, &seen_tpchanger, STRING); break;
    case CHNGRDEV:  get_simple(&conf_chngrdev,  &seen_chngrdev,  STRING); break;
    case CHNGRFILE: get_simple(&conf_chngrfile, &seen_chngrfile, STRING); break;
    case LABELSTR:  get_simple(&conf_labelstr,  &seen_labelstr,  STRING); break;
    case TAPELIST:  get_simple(&conf_tapelist,  &seen_tapelist,  STRING); break;
    case INFOFILE:  get_simple(&conf_infofile,  &seen_infofile,  STRING); break;
    case LOGFILE:   get_simple(&conf_logfile,   &seen_logfile,   STRING); break;
    case DISKFILE:  get_simple(&conf_diskfile,  &seen_diskfile,  STRING); break;
    case BUMPMULT:  get_simple(&conf_bumpmult,  &seen_bumpmult,  REAL);   break;
    case BUMPSIZE:  get_simple(&conf_bumpsize,  &seen_bumpsize,  INT);    break;
    case BUMPDAYS:  get_simple(&conf_bumpdays,  &seen_bumpdays,  INT);    break;
    case NETUSAGE:  get_simple(&conf_netusage,  &seen_netusage,  INT);    break;
    case INPARALLEL:get_simple(&conf_inparallel,&seen_inparallel,INT);    break;
    case TIMEOUT:   get_simple(&conf_timeout,   &seen_timeout,   INT);    break;
    case MAXDUMPS:  get_simple(&conf_maxdumps,  &seen_maxdumps,  INT);    break;
    case TAPETYPE:  get_simple(&conf_tapetype,  &seen_tapetype,  IDENT);  break;
    case INDEXDIR:  get_simple(&conf_indexdir,  &seen_indexdir,  STRING); break;

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

	    i = get_number();

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
	    get_simple((val_t *)&hdcur.disksize, &hdcur.s_size, INT);
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
    hdcur.comment = "";
    hdcur.diskdir = stralloc(conf_diskdir.s);
    hdcur.disksize = 0;

    hdcur.s_comment = 0;
    hdcur.s_disk = 0;
    hdcur.s_size = 0;

    hdcur.up = (void *)0;
}

static void save_holdingdisk()
{
    holdingdisk_t *hp;

    hp = alloc(sizeof(holdingdisk_t));
    *hp = hdcur;
    hp->next = holdingdisks;
    holdingdisks = hp;

    num_holdingdisks++;
}


keytab_t dumptype_keytable[] = {
    { "AUTH", AUTH },
    { "COMMENT", COMMENT },
    { "COMPRATE", COMPRATE },
    { "COMPRESS", COMPRESS },
    { "DUMPCYCLE", DUMPCYCLE },
    { "EXCLUDE", EXCLUDE },
    { "FREQUENCY", FREQUENCY },	/* XXX - historical */
    { "HOLDINGDISK", HOLDING },
    { "IGNORE", IGNORE },
    { "INDEX", INDEX },
    { "KENCRYPT", KENCRYPT },
    { "MAXCYCLE", MAXCYCLE },	/* XXX - historical */
    { "MAXDUMPS", MAXDUMPS },
    { "OPTIONS", OPTIONS },	/* XXX - historical */
    { "PRIORITY", PRIORITY },
    { "PROGRAM", PROGRAM },
    { "RECORD", RECORD },
    { "SKIP-FULL", SKIP_FULL },
    { "SKIP-INCR", SKIP_INCR },
    { "STARTTIME", STARTTIME },
    { "STRATEGY", STRATEGY },
    { NULL, IDENT }
};

static void get_dumptype()
{
    int done;
    int save_overwrites;
    keytab_t *save_kt;
    val_t tmpval;

    save_overwrites = allow_overwrites;
    allow_overwrites = 1;

    save_kt = keytable;
    keytable = dumptype_keytable;

    init_dumptype_defaults();

    get_conftoken(IDENT);
    dpcur.name = stralloc(tokenval.s);
    dpcur.seen = line_num;

    get_conftoken(LBRACE);
    get_conftoken(NL);

    done = 0;
    do {
	line_num += 1;
	get_conftoken(ANY);
	switch(tok) {

	case AUTH:
	    get_auth();
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
	case DUMPCYCLE:
	    get_simple((val_t *)&dpcur.dumpcycle, &dpcur.s_dumpcycle, INT);
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
	    break;
	case OPTIONS:
	    get_dumpopts();
	    break;
	case PRIORITY:
	    get_priority();
	    break;
	case PROGRAM:
	    get_simple((val_t *)&dpcur.program, &dpcur.s_program, STRING);
	    if(strcmp(dpcur.program, "DUMP")
	       && strcmp(dpcur.program, "GNUTAR"))
		parserror("backup program \"%s\" unknown", dpcur.program);
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

	case IDENT:
	    copy_dumptype();
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
	if(tok != NL && tok != END) get_conftoken(NL);
    } while(!done);

    /* XXX - there was a stupidity check in here for skip-incr and
    ** skip-full.  This check should probably be somewhere else. */

    save_dumptype();

    allow_overwrites = save_overwrites;
    keytable = save_kt;
}


static void init_dumptype_defaults()
{
    dpcur.comment = "";
    dpcur.program = "DUMP";
    dpcur.exclude = (char *)0;
    dpcur.exclude_list = 0;
    dpcur.priority = 1;
    dpcur.dumpcycle = conf_dumpcycle.i;
    dpcur.maxcycle = conf_maxcycle.i;
    dpcur.frequency = 1;
    dpcur.maxdumps = conf_maxdumps.i;
    dpcur.start_t = 0;

    dpcur.auth = AUTH_BSD;

    /* options */
    dpcur.record = 1;
    dpcur.strategy = DS_STANDARD;
    dpcur.compress = COMP_FAST;
    dpcur.comprate[0] = dpcur.comprate[1] = 0.50;
    dpcur.skip_incr = dpcur.skip_full = 0;
    dpcur.no_hold = 0;
    dpcur.kencrypt = 0;
    dpcur.ignore = 0;
    dpcur.index = 0;

    dpcur.s_comment = 0;
    dpcur.s_program = 0;
    dpcur.s_exclude = 0;
    dpcur.s_priority = 0;
    dpcur.s_dumpcycle = 0;
    dpcur.s_maxcycle = 0;
    dpcur.s_frequency = 0;
    dpcur.s_maxdumps = 0;
    dpcur.s_start_t = 0;
    dpcur.s_auth = 0;
    dpcur.s_record = 0;
    dpcur.s_strategy = 0;
    dpcur.s_compress = 0;
    dpcur.s_comprate = 0;
    dpcur.s_skip_incr = 0;
    dpcur.s_skip_full = 0;
    dpcur.s_no_hold = 0;
    dpcur.s_kencrypt = 0;
    dpcur.s_ignore = 0;
    dpcur.s_index = 0;
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

    dtcopy(comment, s_comment);
    dtcopy(program, s_program);
    dtcopy(exclude, s_exclude);
    dtcopy(exclude_list, s_exclude);
    dtcopy(priority, s_priority);
    dtcopy(dumpcycle, s_dumpcycle);
    dtcopy(maxcycle, s_maxcycle);
    dtcopy(frequency, s_frequency);
    dtcopy(maxdumps, s_maxdumps);
    dtcopy(start_t, s_start_t);
    dtcopy(auth, s_auth);
    dtcopy(record, s_record);
    dtcopy(strategy, s_strategy);
    dtcopy(compress, s_compress);
    dtcopy(skip_incr, s_skip_incr);
    dtcopy(skip_full, s_skip_full);
    dtcopy(no_hold, s_no_hold);
    dtcopy(kencrypt, s_kencrypt);
    dtcopy(ignore, s_ignore);
    dtcopy(index, s_index);
}

keytab_t tapetype_keytable[] = {
    { "COMMENT", COMMENT },
    { "FILEMARK", FILEMARK },
    { "LENGTH", LENGTH },
    { "SPEED", SPEED },
    { NULL, IDENT }
};

static void get_tapetype()
{
    int done;
    int save_overwrites;
    keytab_t *save_kt;

    save_overwrites = allow_overwrites;
    allow_overwrites = 1;

    save_kt = keytable;
    keytable = tapetype_keytable;

    init_tapetype_defaults();

    get_conftoken(IDENT);
    tpcur.name = stralloc(tokenval.s);
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
	case LENGTH:
	    get_simple((val_t *)&tpcur.length, &tpcur.s_length, INT);
	    break;
	case FILEMARK:
	    get_simple((val_t *)&tpcur.filemark, &tpcur.s_filemark, INT);
	    break;
	case SPEED:
	    get_simple((val_t *)&tpcur.speed, &tpcur.s_speed, INT);
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
    tpcur.comment = "";
    tpcur.length = 2000 * 1024;
    tpcur.filemark = 1000;
    tpcur.speed = 200;

    tpcur.s_comment = 0;
    tpcur.s_length = 0;
    tpcur.s_filemark = 0;
    tpcur.s_speed = 0;
}

static void save_tapetype()
{
    tapetype_t *tp;

    tp = lookup_tapetype(tpcur.name);

    if(tp != (tapetype_t *)0) {
	free(tpcur.name);
	parserror("tapetype %s already defined on line %d", tp->name, tp->seen);
	return;
    }

    tp = alloc(sizeof(tapetype_t));
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

    ttcopy(comment, s_comment);
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
    ifcur.comment = "";
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

    ifcopy(comment, s_comment);
    ifcopy(maxusage, s_maxusage);
}

keytab_t dumpopts_keytable[] = {
    { "COMPRESS", COMPRESS },
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
	case EXCLUDE_FILE:
	    ckseen(&dpcur.s_exclude);
	    get_conftoken(STRING);
	    dpcur.exclude = stralloc(tokenval.s);
	    dpcur.exclude_list = 0;
	    break;
	case EXCLUDE_LIST:
	    ckseen(&dpcur.s_exclude);
	    get_conftoken(STRING);
	    dpcur.exclude = stralloc(tokenval.s);
	    dpcur.exclude_list = 1;
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
}

keytab_t compress_keytable[] = {
    { "BEST", BEST },
    { "CLIENT", CLIENT },
    { "FAST", FAST },
    { "NONE", NONE },
    { "SERVER", SERVER },
    { NULL, IDENT }
};

static void get_compress()
{
    keytab_t *save_kt;
    int serv, clie, none, fast, best;
    int done;
    int comp;

    save_kt = keytable;
    keytable = compress_keytable;

    ckseen(&dpcur.s_compress);

    serv = clie = none = fast = best = 0;

    done = 0;
    do {
	get_conftoken(ANY);
	switch(tok) {
	case NONE:   none = 1; break;
	case FAST:   fast = 1; break;
	case BEST:   best = 1; break;
	case CLIENT: clie = 1; break;
	case SERVER: serv = 1; break;
	case NL:     done = 1; break;
	default:
	    done = 1;
	    serv = clie = 1; /* force an error */
	}
    } while(!done);

    if(serv + clie == 0) clie = 1;	/* default to client */
    if(none + fast + best == 0) fast = 1; /* default to fast */

    comp = -1;

    if(!serv && clie) {
	if(none && !fast && !best) comp = COMP_NONE;
	if(!none && fast && !best) comp = COMP_FAST;
	if(!none && !fast && best) comp = COMP_BEST;
    }

    if(serv && !clie) {
	if(none && !fast && !best) comp = COMP_NONE;
	if(!none && fast && !best) comp = COMP_SERV_FAST;
	if(!none && !fast && best) comp = COMP_SERV_BEST;
    }

    if(comp == -1) {
	parserror("NONE, FAST, BEST, SERVER FAST or SERVER BEST expected");
	comp = COMP_NONE;
    }

    dpcur.compress = comp;

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

keytab_t auth_keytable[] = {
    { "BSD", BSD_AUTH },
    { "KRB4", KRB4_AUTH },
    { NULL, IDENT }
};

static void get_auth()
{
    auth_t auth;
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = auth_keytable;

    ckseen(&dpcur.s_auth);

    get_conftoken(ANY);
    switch(tok) {
    case BSD_AUTH:
	auth = AUTH_BSD;
	break;
    case KRB4_AUTH:
	auth = AUTH_KRB4;
	break;
    default:
	parserror("BSD or KRB4 expected");
	auth = AUTH_BSD;
    }
    dpcur.auth = auth;

    keytable = save_kt;
}

keytab_t strategy_keytable[] = {
    { "HANOI", HANOI },
    { "NOFULL", NOFULL },
    { "NOINC", NOINC },
    { "SKIP", SKIP },
    { "STANDARD", STANDARD },
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
    default:
	parserror("STANDARD or NOFULL expected");
	strat = DS_STANDARD;
    }
    dpcur.strategy = strat;

    keytable = save_kt;
}

keytab_t exclude_keytable[] = {
    { "LIST", LIST },
    { NULL, IDENT }
};

static void get_exclude()
{
    char *excl;
    int excl_list;
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = exclude_keytable;

    ckseen(&dpcur.s_exclude);

    get_conftoken(ANY);
    switch(tok) {
    case STRING:
	excl = stralloc(tokenval.s);
	excl_list = 0;
	break;
    case LIST:
	get_conftoken(STRING);
	excl = stralloc(tokenval.s);
	excl_list = 1;
	break;
    default:
	parserror("a quoted string expected");
	excl = (char *)0;
	excl_list = 0;
    }

    dpcur.exclude = excl;
    dpcur.exclude_list = excl_list;

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
	var->s = stralloc(tokenval.s);
	break;
    case INT:
	var->i = get_number();
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
	assert(0);
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
    { "BYTE", MULT1 },
    { "BYTES", MULT1 },
    { "DAY", MULT1 },
    { "DAYS", MULT1 },
    { "INF", INFINITY },
    { "K", MULT1K },
    { "KB", MULT1K },
    { "KBPS", MULT1K },
    { "KBYTES", MULT1K },
    { "KILOBYTES", MULT1K },
    { "KPS", MULT1K },
    { "M", MULT1M },
    { "MB", MULT1M },
    { "MBPS", MULT1M },
    { "MBYTES", MULT1M },
    { "MEG", MULT1M },
    { "MEGABYTES", MULT1M },
    { "MPS", MULT1M },
    { "TAPE", MULT1 },
    { "TAPES", MULT1 },
    { "WEEK", MULT7 },
    { "WEEKS", MULT7 },
    { NULL, IDENT }
};

static int get_number()
{
    int val;
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = numb_keytable;

    get_conftoken(ANY);

    switch(tok) {
    case INT:
	val = tokenval.i;
	break;
    case INFINITY:
	val = BIGINT;
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
	val *= 7;
	break;
    case MULT1M:
	val *= 1024;
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

arglist_function(static void parserror, char *, format)
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
	if(!strcmp(kwp->keyword, str)) break;
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
    int ch, i, d;
    char *buf;

    if(token_pushed) {
	token_pushed = 0;
	tok = pushed_tok;

	/* If it looked like a key word before then look it
	** up again in the current keyword table. */
	switch(tok) {
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

	while(ch == ' ' || ch == '\t') ch = getc(conf);
	if(ch == '#') /* comment - eat everything but eol/eof */
	    do { ch = getc(conf); } while(ch != '\n' && ch != EOF);

	if(isalpha(ch)) {		/* identifier */
	    buf = tkbuf;
	    do {
		if(islower(ch)) *buf = toupper(ch);
		else *buf = ch;
		buf++;
		ch = getc(conf);
	    } while(isalnum(ch) || ch == '_' || ch == '-');

	    ungetc(ch, conf);
	    *buf = '\0';

	    tokenval.s = tkbuf;

	    if(exp == IDENT) tok = IDENT;
	    else tok = lookup_keyword(tokenval.s);
	}
	else if(isdigit(ch)) {	/* integer */
	    tokenval.i = 0;
	    do {
		tokenval.i = tokenval.i * 10 + (ch - '0');
		ch = getc(conf);
	    } while(isdigit(ch));
	    if(ch != '.') {
		if(exp != REAL)
		    tok = INT;
		else {
		    /* automatically convert to real when expected */
		    i = tokenval.i;
		    tokenval.r = (double) i;
		    tok = REAL;
		}
	    }
	    else {
		/* got a real number, not an int */
		i = tokenval.i;
		tokenval.r = (double) i;
		i=0; d=1;
		ch = getc(conf);
		while(isdigit(ch)) {
		    i = i * 10 + (ch - '0');
		    d = d * 10;
		    ch = getc(conf);
		};
		tokenval.r += ((double)i)/d;
		tok = REAL;
	    }
	    ungetc(ch,conf);
	}
	else switch(ch) {

	case '"':			/* string */
	    buf = tkbuf;
	    ch =  getc(conf);
	    while(ch != '"' && ch != '\n' && ch != EOF) {
		*buf++ = ch;
		ch = getc(conf);
	    }
	    if(ch != '"') {
		parserror("missing end quote");
		ungetc(ch, conf);
	    }
	    *buf = '\0';
	    tokenval.s = tkbuf;
	    tok = STRING;
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
dump_configuration(filename)
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
    printf("conf_tapedev = \"%s\"\n", getconf_str(CNF_TAPEDEV));
    printf("conf_tpchanger = \"%s\"\n", getconf_str(CNF_TPCHANGER));
    printf("conf_chngrdev = \"%s\"\n", getconf_str(CNF_CHNGRDEV));
    printf("conf_chngrfile = \"%s\"\n", getconf_str(CNF_CHNGRFILE));
    printf("conf_labelstr = \"%s\"\n", getconf_str(CNF_LABELSTR));
    printf("conf_tapelist = \"%s\"\n", getconf_str(CNF_TAPELIST));
    printf("conf_infofile = \"%s\"\n", getconf_str(CNF_INFOFILE));
    printf("conf_logfile = \"%s\"\n", getconf_str(CNF_LOGFILE));
    printf("conf_diskfile = \"%s\"\n", getconf_str(CNF_DISKFILE));
    printf("conf_tapetype = \"%s\"\n", getconf_str(CNF_TAPETYPE));

    printf("conf_dumpcycle = %d\n", getconf_int(CNF_DUMPCYCLE));
    printf("conf_runtapes = %d\n", getconf_int(CNF_RUNTAPES));
    printf("conf_tapecycle = %d\n", getconf_int(CNF_TAPECYCLE));
    printf("conf_bumpsize = %d\n", getconf_int(CNF_BUMPSIZE));
    printf("conf_bumpdays = %d\n", getconf_int(CNF_BUMPDAYS));
    printf("conf_bumpmult = %f\n", getconf_real(CNF_BUMPMULT));
    printf("conf_netusage = %d\n", getconf_int(CNF_NETUSAGE));
    printf("conf_inparallel = %d\n", getconf_int(CNF_INPARALLEL));
    /*printf("conf_timeout = %d\n", getconf_int(CNF_TIMEOUT));*/
    printf("conf_maxdumps = %d\n", getconf_int(CNF_MAXDUMPS));

    /*printf("conf_diskdir = \"%s\"\n", getconf_str(CNF_DISKDIR));*/
    /*printf("conf_disksize = %d\n", getconf_int(CNF_DISKSIZE));*/
    printf("conf_indexdir = \"%s\"\n", getconf_str(CNF_INDEXDIR));
    printf("num_holdingdisks = %d\n", num_holdingdisks);
    for(hp = holdingdisks; hp != NULL; hp = hp->next) {
	printf("\nHOLDINGDISK %s:\n", hp->name);
	printf("	COMMENT \"%s\"\n", hp->comment);
	printf("	DISKDIR \"%s\"\n", hp->diskdir);
	printf("	SIZE %d\n", hp->disksize);
    }

    for(tp = tapelist; tp != NULL; tp = tp->next) {
	printf("\nTAPETYPE %s:\n", tp->name);
	printf("	COMMENT \"%s\"\n", tp->comment);
	printf("	LENGTH %u\n", tp->length);
	printf("	FILEMARK %u\n", tp->filemark);
	printf("	SPEED %d\n", tp->speed);
    }

    for(dp = dumplist; dp != NULL; dp = dp->next) {
	printf("\nDUMPTYPE %s:\n", dp->name);
	printf("	COMMENT \"%s\"\n", dp->comment);
	printf("	PROGRAM \"%s\"\n", dp->program);
	printf("	PRIORITY %d\n", dp->priority);
	printf("	DUMPCYCLE %d\n", dp->dumpcycle);
	st = dp->start_t;
	if(st) {
	    stm = localtime(&st);
	    printf("	STARTTIME %d:%02d:%02d\n",
	      stm->tm_hour, stm->tm_min, stm->tm_sec);
	}
	if(dp->exclude) {
	    printf("	EXCLUDE");
	    if(dp->exclude_list) {
		printf("-LIST");
	    }
	    printf(" \"%s\"\n", dp->exclude);
	}
	printf("	FREQUENCY %d\n", dp->frequency);
	printf("	MAXDUMPS %d\n", dp->maxdumps);
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
	case COMP_SERV_FAST:
	    printf("SRVCOMP-FAST ");
	    break;
	case COMP_SERV_BEST:
	    printf("SRVCOMP-BEST ");
	    break;
	}

	if(!dp->record) printf("NO-");
	printf("RECORD");
	if(dp->auth == AUTH_BSD) printf(" BSD-AUTH");
	else if(dp->auth == AUTH_KRB4) printf(" KRB4-AUTH");
	else printf(" UNKNOWN-AUTH");
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

main(argc, argv)
int argc;
char *argv[];
{
  startclock();
  if (argc>1)
    chdir(argv[1]);
  read_conffile(CONFFILE_NAME);
  dump_configuration(CONFFILE_NAME);
}

char *pname = "conffile";

#endif /* TEST */
