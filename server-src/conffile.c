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
 * $Id: conffile.c,v 1.135 2006/05/25 15:08:58 martinea Exp $
 *
 * read configuration file
 */
/*
 * XXX - I'm not happy *at all* with this implementation, but I don't
 * think YACC would be any easier.  A more table based implementation
 * would be better.  Also clean up memory leaks.
 */
#include "amanda.h"
#include "arglist.h"
#include "util.h"
#include "conffile.h"
#include "diskfile.h"
#include "driverio.h"
#include "clock.h"
#include <limits.h>

/*
 * this corresponds to the normal output of amanda, but may
 * be adapted to any spacing as you like.
 */
ColumnInfo ColumnData[] = {
    { "HostName",   0, 12, 12, 0, "%-*.*s", "HOSTNAME" },
    { "Disk",       1, 11, 11, 0, "%-*.*s", "DISK" },
    { "Level",      1, 1,  1,  0, "%*.*d",  "L" },
    { "OrigKB",     1, 7,  0,  0, "%*.*lf",  "ORIG-KB" },
    { "OutKB",      1, 7,  0,  0, "%*.*lf",  "OUT-KB" },
    { "Compress",   1, 6,  1,  0, "%*.*lf",  "COMP%" },
    { "DumpTime",   1, 7,  7,  0, "%*.*s",  "MMM:SS" },
    { "DumpRate",   1, 6,  1,  0, "%*.*lf",  "KB/s" },
    { "TapeTime",   1, 6,  6,  0, "%*.*s",  "MMM:SS" },
    { "TapeRate",   1, 6,  1,  0, "%*.*lf",  "KB/s" },
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
static val_t conf_amrecover_check_label;
static val_t conf_amrecover_do_fsf;
static val_t conf_autoflush;
static val_t conf_bumpdays;
static val_t conf_bumppercent;
static val_t conf_bumpsize;
static val_t conf_displayunit;
static val_t conf_dumpcycle;
static val_t conf_inparallel;
static val_t conf_krb5keytab;
static val_t conf_krb5principal;
static val_t conf_label_new_tapes;
static val_t conf_maxcycle;
static val_t conf_maxdumps;
static val_t conf_maxdumpsize;
static val_t conf_netusage;
static val_t conf_reserve;
static val_t conf_runspercycle;
static val_t conf_runtapes;
static val_t conf_tapebufs;
static val_t conf_tapecycle;
static val_t conf_taperalgo;
static val_t conf_usetimestamps;

/* longs */

/* sizes */

/* times */
static val_t conf_ctimeout;
static val_t conf_dtimeout;
static val_t conf_etimeout;
static val_t conf_timeout;

/* reals */
static val_t conf_bumpmult;

/* am64s */
static val_t conf_disksize;

/* other internal variables */
static holdingdisk_t hdcur;

static tapetype_t tpcur;

static dumptype_t dpcur;

static interface_t ifcur;

static int seen_amrecover_changer;
static int seen_amrecover_check_label;
static int seen_amrecover_do_fsf;
static int seen_autoflush;
static int seen_bumpdays;
static int seen_bumpmult;
static int seen_bumppercent;
static int seen_bumpsize;
static int seen_chngrdev;
static int seen_chngrfile;
static int seen_columnspec;
static int seen_ctimeout;
static int seen_diskdir;
static int seen_diskfile;
static int seen_disksize;
static int seen_displayunit;
static int seen_dtimeout;
static int seen_dumpcycle;
static int seen_dumporder;
static int seen_dumpuser;
static int seen_etimeout;
static int seen_indexdir;
static int seen_infofile;
static int seen_inparallel;
static int seen_krb5keytab;
static int seen_krb5principal;
static int seen_label_new_tapes;
static int seen_labelstr;
static int seen_logdir;
static int seen_mailto;
static int seen_maxcycle;
static int seen_maxdumps;
static int seen_maxdumpsize;
static int seen_netusage;
static int seen_org;
static int seen_printer;
static int seen_rawtapedev;
static int seen_reserve;
static int seen_runspercycle;
static int seen_runtapes;
static int seen_tapebufs;
static int seen_tapecycle;
static int seen_tapedev;
static int seen_tapelist;
static int seen_taperalgo;
static int seen_tapetype;
static int seen_timeout;
static int seen_tpchanger;
static int seen_usetimestamps;

static dumptype_t *dumplist = NULL;
static tapetype_t *tapelist = NULL;
static interface_t *interface_list = NULL;

/* predeclare local functions */

static int read_confline(void);

static void copy_dumptype(void);
static void copy_interface(void);
static void copy_tapetype(void);

static void get_comprate(void);
static void get_compress(void);
static void get_dumpopts(void);
static void get_dumptype(void);
static void get_encrypt(void);
static void get_estimate(void);
static void get_exclude(void);
static void get_holdingdisk(void);
static void get_include(void);
static void get_interface(void);
static void get_priority(void);
static void get_strategy(void);
static void get_taperalgo(val_t *c_taperalgo, int *s_taperalgo);
static void get_tapetype(void);

static void init_defaults(void);
static void init_dumptype_defaults(void);
static void init_holdingdisk_defaults(void);
static void init_interface_defaults(void);
static void init_tapetype_defaults(void);

static void read_conffile_recursively(char *filename);
static void read_conffile_recursively(char *filename);

static void save_dumptype(void);
static void save_holdingdisk(void);
static void save_interface(void);
static void save_tapetype(void);

/*
** ------------------------
**  External entry points
** ------------------------
*/

int
read_conffile(
    char *filename)
{
    interface_t *ip;

    init_defaults();

    /* We assume that conf_confname & conf are initialized to NULL above */
    read_conffile_recursively(filename);

    if(got_parserror != -1 ) {
	if(lookup_tapetype(conf_tapetype.s) == NULL) {
	    char *save_confname = conf_confname;

	    conf_confname = filename;
	    if(!seen_tapetype)
		conf_parserror("default tapetype %s not defined", conf_tapetype.s);
	    else {
		conf_line_num = seen_tapetype;
		conf_parserror("tapetype %s not defined", conf_tapetype.s);
	    }
	    conf_confname = save_confname;
	}
    }

    ip = alloc(SIZEOF(interface_t));
    malloc_mark(ip);
    ip->name = "";
    ip->seen = seen_netusage;
    ip->comment = "implicit from NETUSAGE";
    ip->maxusage = (unsigned long)conf_netusage.l;
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
    { "ORG",			CNF_ORG,			CONF_STRING },
    { "MAILTO",			CNF_MAILTO,			CONF_STRING },
    { "DUMPUSER",		CNF_DUMPUSER,			CONF_STRING },
    { "PRINTER",		CNF_PRINTER,			CONF_STRING },
    { "TAPEDEV",		CNF_TAPEDEV,			CONF_STRING },
    { "TPCHANGER",		CNF_TPCHANGER,			CONF_STRING },
    { "CHANGERDEV",		CNF_CHNGRDEV,			CONF_STRING },
    { "CHANGERFILE",		CNF_CHNGRFILE,			CONF_STRING },
    { "LABELSTR",		CNF_LABELSTR,			CONF_STRING },
    { "TAPELIST",		CNF_TAPELIST,			CONF_STRING },
    { "DISKFILE",		CNF_DISKFILE,			CONF_STRING },
    { "INFOFILE",		CNF_INFOFILE,			CONF_STRING },
    { "LOGDIR",			CNF_LOGDIR,			CONF_STRING },
    { "INDEXDIR",		CNF_INDEXDIR,			CONF_STRING },
    { "TAPETYPE",		CNF_TAPETYPE,			CONF_STRING },
    { "DUMPCYCLE",		CNF_DUMPCYCLE,			CONF_INT },
    { "RUNSPERCYCLE",		CNF_RUNSPERCYCLE,		CONF_INT },
    { "MINCYCLE", 		CNF_DUMPCYCLE,			CONF_INT },
    { "RUNTAPES",  		CNF_RUNTAPES,			CONF_INT },
    { "TAPECYCLE",		CNF_TAPECYCLE,			CONF_INT },
    { "BUMPDAYS",		CNF_BUMPDAYS,			CONF_INT },
    { "BUMPSIZE",		CNF_BUMPSIZE,			CONF_INT },
    { "BUMPPERCENT",		CNF_BUMPPERCENT,		CONF_INT },
    { "BUMPMULT",		CNF_BUMPMULT,			CONF_REAL },
    { "NETUSAGE",		CNF_NETUSAGE,			CONF_LONG },
    { "INPARALLEL",		CNF_INPARALLEL,			CONF_INT },
    { "DUMPORDER",		CNF_DUMPORDER,			CONF_STRING },
    /*{ "TIMEOUT",		CNF_TIMEOUT,			CONF_TIME },*/
    { "MAXDUMPS",		CNF_MAXDUMPS,			CONF_INT },
    { "ETIMEOUT",		CNF_ETIMEOUT,			CONF_TIME },
    { "DTIMEOUT",		CNF_DTIMEOUT,			CONF_TIME },
    { "CTIMEOUT",		CNF_CTIMEOUT,			CONF_TIME },
    { "TAPEBUFS",		CNF_TAPEBUFS,			CONF_INT },
    { "RAWTAPEDEV",		CNF_RAWTAPEDEV,			CONF_STRING },
    { "COLUMNSPEC",		CNF_COLUMNSPEC,			CONF_STRING },
    { "AMRECOVER_DO_FSF",	CNF_AMRECOVER_DO_FSF,		CONF_BOOL },
    { "AMRECOVER_CHECK_LABEL",	CNF_AMRECOVER_CHECK_LABEL,	CONF_BOOL },
    { "AMRECOVER_CHANGER",	CNF_AMRECOVER_CHANGER,		CONF_STRING },
    { "TAPERALGO",		CNF_TAPERALGO,			CONF_INT },
    { "DISPLAYUNIT",		CNF_DISPLAYUNIT,		CONF_STRING },
    { "AUTOFLUSH",		CNF_AUTOFLUSH,			CONF_BOOL },
    { "RESERVE",		CNF_RESERVE,			CONF_INT },
    { "MAXDUMPSIZE",		CNF_MAXDUMPSIZE,		CONF_AM64 },
    { "KRB5KEYTAB",		CNF_KRB5KEYTAB,			CONF_STRING },
    { "KRB5PRINCIPAL",		CNF_KRB5PRINCIPAL,		CONF_STRING },
    { "LABEL_NEW_TAPES",	CNF_LABEL_NEW_TAPES,		CONF_STRING },
    { "USETIMESTAMPS",		CNF_USETIMESTAMPS,		CONF_BOOL },
    { NULL, 0, 0 }
};

char *
getconf_byname(
    char *str)
{
    static char *tmpstr;
    char number[NUM_STR_SIZE];
    struct byname *np;
    char *s;
    char ch;

    tmpstr = stralloc(str);
    s = tmpstr;
    while((ch = *s++) != '\0') {
	if(islower((int)ch))
	    s[-1] = (char)toupper(ch);
    }

    for(np = byname_table; np->name != NULL; np++)
	if(strcmp(np->name, tmpstr) == 0) break;

    if(np->name == NULL) return NULL;

    if(np->typ == CONF_INT) {
	snprintf(number, SIZEOF(number), "%d",
		getconf_int(np->parm));
	tmpstr = newstralloc(tmpstr, number);
    } else if(np->typ == CONF_LONG) {
	snprintf(number, SIZEOF(number), "%ld",
		getconf_long(np->parm));
	tmpstr = newstralloc(tmpstr, number);
    } else if(np->typ == CONF_TIME) {
	snprintf(number, SIZEOF(number), TIME_T_FMT,
		(TIME_T_FMT_TYPE)getconf_time(np->parm));
	tmpstr = newstralloc(tmpstr, number);
    } else if(np->typ == CONF_SIZE) {
	snprintf(number, SIZEOF(number), SIZE_T_FMT,
		(SIZE_T_FMT_TYPE)getconf_size(np->parm));
	tmpstr = newstralloc(tmpstr, number);
    } else if(np->typ == CONF_BOOL) {
	if(getconf_int(np->parm) == 0) {
	    tmpstr = newstralloc(tmpstr, "off");
	}
	else {
	    tmpstr = newstralloc(tmpstr, "on");
	}
    } else if(np->typ == CONF_REAL) {
	snprintf(number, SIZEOF(number), "%lf", getconf_real(np->parm));
	tmpstr = newstralloc(tmpstr, number);
    } else if(np->typ == CONF_AM64){
	snprintf(number, sizeof(number), OFF_T_FMT,
		(OFF_T_FMT_TYPE)getconf_am64(np->parm));
	tmpstr = newstralloc(tmpstr, number);
    } else {
	tmpstr = newstralloc(tmpstr, getconf_str(np->parm));
    }

    return tmpstr;
}

int
getconf_seen(
    confparm_t parm)
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
    case CNF_LOGDIR: return seen_logdir;
    case CNF_BUMPPERCENT: return seen_bumppercent;
    case CNF_BUMPSIZE: return seen_bumpsize;
    case CNF_BUMPMULT: return seen_bumpmult;
    case CNF_BUMPDAYS: return seen_bumpdays;
    case CNF_TAPETYPE: return seen_tapetype;
    case CNF_DUMPCYCLE: return seen_dumpcycle;
    case CNF_RUNSPERCYCLE: return seen_runspercycle;
    case CNF_TAPECYCLE: return seen_tapecycle;
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

long
getconf_long(
    confparm_t parm)
{
    long r = 0;

    switch(parm) {
    case CNF_NETUSAGE: r = conf_netusage.l; break;

    default:
	error("error [unknown getconf_int parm: %d]", parm);
	/*NOTREACHED*/
    }
    return r;
}

ssize_t
getconf_size(
    confparm_t parm)
{
    ssize_t r = 0;

    switch(parm) {
    default:
	error("error [unknown getconf_size parm: %d]", parm);
	/*NOTREACHED*/
    }
    return r;
}

int
getconf_int(
    confparm_t parm)
{
    int r = 0;

    switch(parm) {

    case CNF_DUMPCYCLE: r = conf_dumpcycle.i; break;
    case CNF_RUNSPERCYCLE: r = conf_runspercycle.i; break;
    case CNF_TAPECYCLE: r = conf_tapecycle.i; break;
    case CNF_RUNTAPES: r = conf_runtapes.i; break;
    case CNF_BUMPPERCENT: r = conf_bumppercent.i; break;
    case CNF_BUMPDAYS: r = conf_bumpdays.i; break;
    case CNF_INPARALLEL: r = conf_inparallel.i; break;
    case CNF_MAXDUMPS: r = conf_maxdumps.i; break;
    case CNF_TAPEBUFS: r = conf_tapebufs.i; break;
    case CNF_AUTOFLUSH: r = conf_autoflush.i; break;
    case CNF_RESERVE: r = conf_reserve.i; break;
    case CNF_AMRECOVER_DO_FSF: r = conf_amrecover_do_fsf.i; break;
    case CNF_AMRECOVER_CHECK_LABEL: r = conf_amrecover_check_label.i; break;
    case CNF_TAPERALGO: r = conf_taperalgo.i; break;
    case CNF_USETIMESTAMPS: r = conf_usetimestamps.i; break;

    default:
	error("error [unknown getconf_int parm: %d]", parm);
	/*NOTREACHED*/
    }
    return r;
}

time_t
getconf_time(
    confparm_t parm)
{
    time_t r = 0;

    switch(parm) {
    /*case CNF_TIMEOUT:	r = conf_timeout.t;	break;*/
    case CNF_ETIMEOUT:	r = conf_etimeout.t;	break;
    case CNF_DTIMEOUT:	r = conf_dtimeout.t;	break;
    case CNF_CTIMEOUT:	r = conf_ctimeout.t;	break;
    default:
	error("error [unknown getconf_time parm: %d]", parm);
	/*NOTREACHED*/
    }
    return r;
}

am64_t
getconf_am64(
    confparm_t parm)
{
    am64_t r = 0;

    switch(parm) {
    case CNF_MAXDUMPSIZE:
	r = conf_maxdumpsize.am64;
	break;

    case CNF_BUMPSIZE:
	r = conf_bumpsize.am64;
	break;

    default:
	error("error [unknown getconf_am64 parm: %d]", parm);
	/*NOTREACHED*/
    }
    return r;
}

double
getconf_real(
    confparm_t parm)
{
    double r = 0;

    switch(parm) {
    case CNF_BUMPMULT:
	r = conf_bumpmult.r;
	break;

    default:
	error("error [unknown getconf_real parm: %d]", parm);
	/*NOTREACHED*/
    }
    return r;
}

char *
getconf_str(
    confparm_t parm)
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
    case CNF_DISKFILE: r = conf_diskfile.s; break;
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
	/*NOTREACHED*/
    }
    return r;
}

holdingdisk_t *
getconf_holdingdisks(void)
{
    return holdingdisks;
}

dumptype_t *
lookup_dumptype(
    char *str)
{
    dumptype_t *p;

    for(p = dumplist; p != NULL; p = p->next) {
	if(strcasecmp(p->name, str) == 0)
	    return p;
    }
    return NULL;
}

tapetype_t *
lookup_tapetype(
    char *str)
{
    tapetype_t *p;

    for(p = tapelist; p != NULL; p = p->next) {
	if(strcmp(p->name, str) == 0) return p;
    }
    return NULL;
}

interface_t *
lookup_interface(
    char *str)
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


static void
init_defaults(void)
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
    conf_disksize.am64	= (am64_t)0;
    conf_netusage.l	= 300;
    conf_inparallel.i	= 10;
    conf_maxdumps.i	= 1;
    conf_timeout.t	= 2;
    conf_bumppercent.i	= 0;
    conf_bumpsize.am64	= (am64_t)(10*1024);
    conf_bumpdays.i	= 2;
    conf_bumpmult.r	= 1.5;
    conf_etimeout.t     = 300;
    conf_dtimeout.t     = 1800;
    conf_ctimeout.t     = 30;
    conf_tapebufs.i     = 20;
    conf_autoflush.i	= 0;
    conf_reserve.i	= 100;
    conf_maxdumpsize.am64 = (am64_t)-1;
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

    conf_line_num = got_parserror = 0;
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
    dpcur.name = "NO-HOLD";
    dpcur.seen = -1;
    dpcur.no_hold = 1;
    dpcur.s_no_hold = -1;
    save_dumptype();

    init_dumptype_defaults();
    dpcur.name = "NO-FULL"; dpcur.seen = -1;
    dpcur.strategy = DS_NOFULL; dpcur.s_strategy = -1;
    save_dumptype();
}

static void
read_conffile_recursively(
    char *filename)
{
    /* Save globals used in read_confline(), elsewhere. */
    int		rc;
    int		save_line_num = conf_line_num;
    FILE *	save_conf     = conf_conf;
    char *	save_confname = conf_confname;

    if (*filename == '/' || config_dir == NULL) {
	conf_confname = stralloc(filename);
    } else {
	conf_confname = stralloc2(config_dir, filename);
    }

    if((conf_conf = fopen(conf_confname, "r")) == NULL) {
	fprintf(stderr, "could not open conf file \"%s\": %s\n", conf_confname,
		strerror(errno));
	amfree(conf_confname);
	got_parserror = -1;
	return;
    }

    conf_line_num = 0;

    /* read_confline() can invoke us recursively via "includefile" */
    do {
	rc = read_confline();
    } while (rc != 0);

    afclose(conf_conf);

    amfree(conf_confname);

    /* Restore globals */
    conf_line_num = save_line_num;
    conf_conf     = save_conf;
    conf_confname = save_confname;
}


/* ------------------------ */


keytab_t main_keytable[] = {
    { "BUMPDAYS", CONF_BUMPDAYS },
    { "BUMPMULT", CONF_BUMPMULT },
    { "BUMPSIZE", CONF_BUMPSIZE },
    { "BUMPPERCENT", CONF_BUMPPERCENT },
    { "DEFINE", CONF_DEFINE },
    { "DISKDIR", CONF_DISKDIR },	/* XXX - historical */
    { "DISKFILE", CONF_DISKFILE },
    { "DISKSIZE", CONF_DISKSIZE },	/* XXX - historical */
    { "DUMPCYCLE", CONF_DUMPCYCLE },
    { "RUNSPERCYCLE", CONF_RUNSPERCYCLE },
    { "DUMPTYPE", CONF_DUMPTYPE },
    { "DUMPUSER", CONF_DUMPUSER },
    { "PRINTER", CONF_PRINTER },
    { "HOLDINGDISK", CONF_HOLDING },
    { "INCLUDEFILE", CONF_INCLUDEFILE },
    { "INDEXDIR", CONF_INDEXDIR },
    { "INFOFILE", CONF_INFOFILE },
    { "INPARALLEL", CONF_INPARALLEL },
    { "DUMPORDER", CONF_DUMPORDER },
    { "INTERFACE", CONF_INTERFACE },
    { "LABELSTR", CONF_LABELSTR },
    { "LOGDIR", CONF_LOGDIR },
    { "LOGFILE", CONF_LOGFILE },	/* XXX - historical */
    { "MAILTO", CONF_MAILTO },
    { "MAXCYCLE", CONF_MAXCYCLE },	/* XXX - historical */
    { "MAXDUMPS", CONF_MAXDUMPS },
    { "MINCYCLE", CONF_DUMPCYCLE },	/* XXX - historical */
    { "NETUSAGE", CONF_NETUSAGE },	/* XXX - historical */
    { "ORG", CONF_ORG },
    { "RUNTAPES", CONF_RUNTAPES },
    { "TAPECYCLE", CONF_TAPECYCLE },
    { "TAPEDEV", CONF_TAPEDEV },
    { "TAPELIST", CONF_TAPELIST },
    { "TAPETYPE", CONF_TAPETYPE },
    { "TIMEOUT", CONF_TIMEOUT },	/* XXX - historical */
    { "TPCHANGER", CONF_TPCHANGER },
    { "CHANGERDEV", CONF_CHNGRDEV },
    { "CHANGERFILE", CONF_CHNGRFILE },
    { "ETIMEOUT", CONF_ETIMEOUT },
    { "DTIMEOUT", CONF_DTIMEOUT },
    { "CTIMEOUT", CONF_CTIMEOUT },
    { "TAPEBUFS", CONF_TAPEBUFS },
    { "RAWTAPEDEV", CONF_RAWTAPEDEV },
    { "AUTOFLUSH", CONF_AUTOFLUSH },
    { "RESERVE", CONF_RESERVE },
    { "MAXDUMPSIZE", CONF_MAXDUMPSIZE },
    { "COLUMNSPEC", CONF_COLUMNSPEC },
    { "AMRECOVER_DO_FSF", CONF_AMRECOVER_DO_FSF },
    { "AMRECOVER_CHECK_LABEL", CONF_AMRECOVER_CHECK_LABEL },
    { "AMRECOVER_CHANGER", CONF_AMRECOVER_CHANGER },
    { "TAPERALGO", CONF_TAPERALGO },
    { "DISPLAYUNIT", CONF_DISPLAYUNIT },
    { "KRB5KEYTAB", CONF_KRB5KEYTAB },
    { "KRB5PRINCIPAL", CONF_KRB5PRINCIPAL },
    { "LABEL_NEW_TAPES", CONF_LABEL_NEW_TAPES },
    { "USETIMESTAMPS", CONF_USETIMESTAMPS },
    { NULL, CONF_IDENT }
};

static int
read_confline(void)
{
    keytable = main_keytable;

    conf_line_num += 1;
    get_conftoken(CONF_ANY);
    switch(tok) {
    case CONF_INCLUDEFILE: {
	    char *fn;
	    char *cname;

	    get_conftoken(CONF_STRING);
	    fn = tokenval.s;
	    if (*fn == '/' || config_dir == NULL) {
	      cname = stralloc(fn);
	    } else {
	      cname = stralloc2(config_dir, fn);
	    }
	    if ( cname != NULL &&  (access(cname, R_OK) == 0)) {
	      read_conffile_recursively(cname);
	      amfree(cname);
	    }
	    else {
	      amfree(cname);
	      conf_parserror("cannot open %s: %s\n", fn, strerror(errno));
	    }
	}
	break;

    case CONF_ORG:
	get_simple(&conf_org, &seen_org, CONF_STRING);
	break;

    case CONF_MAILTO:
	get_simple(&conf_mailto, &seen_mailto, CONF_STRING);
	break;

    case CONF_DUMPUSER:
	get_simple(&conf_dumpuser, &seen_dumpuser, CONF_STRING);
	break;

    case CONF_PRINTER:
	get_simple(&conf_printer, &seen_printer, CONF_STRING);
	break;

    case CONF_DUMPCYCLE:
	get_simple(&conf_dumpcycle, &seen_dumpcycle, CONF_INT);
	if(conf_dumpcycle.i < 0) {
	    conf_parserror("dumpcycle must be positive");
	}
	break;

    case CONF_RUNSPERCYCLE:
	get_simple(&conf_runspercycle, &seen_runspercycle, CONF_INT);
	if(conf_runspercycle.i < -1) {
	    conf_parserror("runspercycle must be >= -1");
	}
	break;

    case CONF_MAXCYCLE:
	get_simple(&conf_maxcycle, &seen_maxcycle, CONF_INT);
	break;

    case CONF_TAPECYCLE:
	get_simple(&conf_tapecycle, &seen_tapecycle, CONF_INT);
	if(conf_tapecycle.i < 1) {
	    conf_parserror("tapecycle must be positive");
	}
	break;

    case CONF_RUNTAPES:
	get_simple(&conf_runtapes, &seen_runtapes, CONF_INT);
	if(conf_runtapes.i < 0) {
	    conf_parserror("runtapes must be positive");
	}
	break;

    case CONF_TAPEDEV:
	get_simple(&conf_tapedev, &seen_tapedev, CONF_STRING);
	break;

    case CONF_RAWTAPEDEV:
	get_simple(&conf_rawtapedev,&seen_rawtapedev,CONF_STRING);
	break;

    case CONF_TPCHANGER:
	get_simple(&conf_tpchanger, &seen_tpchanger, CONF_STRING);
	break;

    case CONF_CHNGRDEV:
	get_simple(&conf_chngrdev, &seen_chngrdev, CONF_STRING);
	break;

    case CONF_CHNGRFILE:
	get_simple(&conf_chngrfile, &seen_chngrfile, CONF_STRING);
	break;

    case CONF_LABELSTR:
	get_simple(&conf_labelstr, &seen_labelstr, CONF_STRING);
	break;

    case CONF_TAPELIST:
	get_simple(&conf_tapelist, &seen_tapelist, CONF_STRING);
	break;

    case CONF_INFOFILE:
	get_simple(&conf_infofile, &seen_infofile, CONF_STRING);
	break;

    case CONF_LOGDIR:
	get_simple(&conf_logdir, &seen_logdir, CONF_STRING);
	break;

    case CONF_DISKFILE:
	get_simple(&conf_diskfile, &seen_diskfile, CONF_STRING);
	break;

    case CONF_BUMPMULT:
	get_simple(&conf_bumpmult, &seen_bumpmult, CONF_REAL);
	if(conf_bumpmult.r < 0.999) {
	     conf_parserror("bumpmult must be positive");
	}
	break;

    case CONF_BUMPPERCENT:
	get_simple(&conf_bumppercent, &seen_bumppercent, CONF_INT);
	if(conf_bumppercent.i < 0 || conf_bumppercent.i > 100) {
	    conf_parserror("bumppercent must be between 0 and 100");
	}
	break;

    case CONF_BUMPSIZE:
	get_simple(&conf_bumpsize, &seen_bumpsize, CONF_AM64);
	if(conf_bumpsize.am64 < 1) {
	    conf_parserror("bumpsize must be positive");
	}
	break;

    case CONF_BUMPDAYS:
	get_simple(&conf_bumpdays, &seen_bumpdays, CONF_INT);
	if(conf_bumpdays.i < 1) {
	    conf_parserror("bumpdays must be positive");
	}
	break;

    case CONF_NETUSAGE:
	get_simple(&conf_netusage, &seen_netusage, CONF_LONG);
	if(conf_netusage.l < 1) {
	    conf_parserror("netusage must be positive");
	}
	break;

    case CONF_INPARALLEL:
	get_simple(&conf_inparallel,&seen_inparallel,CONF_INT);
	if(conf_inparallel.i < 1 || conf_inparallel.i >MAX_DUMPERS){
	    conf_parserror("inparallel must be between 1 and MAX_DUMPERS (%d)",
		      MAX_DUMPERS);
	}
	break;

    case CONF_DUMPORDER:
	get_simple(&conf_dumporder, &seen_dumporder, CONF_STRING);
	break;

    case CONF_TIMEOUT:
	get_simple(&conf_timeout, &seen_timeout, CONF_TIME);
	break;

    case CONF_MAXDUMPS:
	get_simple(&conf_maxdumps, &seen_maxdumps, CONF_INT);
	if(conf_maxdumps.i < 1) {
	    conf_parserror("maxdumps must be positive");
	}
	break;

    case CONF_TAPETYPE:
	get_simple(&conf_tapetype, &seen_tapetype, CONF_IDENT);
	break;

    case CONF_INDEXDIR:
	get_simple(&conf_indexdir, &seen_indexdir, CONF_STRING);
	break;

    case CONF_ETIMEOUT:
	get_simple(&conf_etimeout, &seen_etimeout, CONF_TIME);
	break;

    case CONF_DTIMEOUT:
	get_simple(&conf_dtimeout, &seen_dtimeout, CONF_TIME);
	if(conf_dtimeout.t < 1) {
	    conf_parserror("dtimeout must be positive");
	}
	break;

    case CONF_CTIMEOUT:
	get_simple(&conf_ctimeout, &seen_ctimeout, CONF_TIME);
	if(conf_ctimeout.t < 1) {
	    conf_parserror("ctimeout must be positive");
	}
	break;

    case CONF_TAPEBUFS:
	get_simple(&conf_tapebufs, &seen_tapebufs, CONF_INT);
	if(conf_tapebufs.i < 1) {
	    conf_parserror("tapebufs must be positive");
	}
	break;

    case CONF_AUTOFLUSH:
	get_simple(&conf_autoflush, &seen_autoflush, CONF_BOOL);
	break;

    case CONF_RESERVE:
	get_simple(&conf_reserve, &seen_reserve, CONF_INT);
	if(conf_reserve.i < 0 || conf_reserve.i > 100) {
	    conf_parserror("reserve must be between 0 and 100");
	}
	break;

    case CONF_MAXDUMPSIZE:
	get_simple(&conf_maxdumpsize, &seen_maxdumpsize, CONF_AM64);
	break;

    case CONF_COLUMNSPEC:
	get_simple(&conf_columnspec, &seen_columnspec, CONF_STRING);
	break;

    case CONF_AMRECOVER_DO_FSF:
	get_simple(&conf_amrecover_do_fsf, &seen_amrecover_do_fsf, CONF_BOOL);
	break;
    case CONF_AMRECOVER_CHECK_LABEL:
	get_simple(&conf_amrecover_check_label, &seen_amrecover_check_label, CONF_BOOL);
	break;
    case CONF_AMRECOVER_CHANGER:
	get_simple(&conf_amrecover_changer, &seen_amrecover_changer, CONF_STRING);
	break;

    case CONF_TAPERALGO:
	 get_taperalgo(&conf_taperalgo, &seen_taperalgo);
	 break;

    case CONF_DISPLAYUNIT:
	get_simple(&conf_displayunit,&seen_displayunit, CONF_STRING);
	if(strcmp(conf_displayunit.s,"k") == 0 ||
		strcmp(conf_displayunit.s,"K") == 0) {
	    conf_displayunit.s[0] = (char)toupper(conf_displayunit.s[0]);
	    unit_divisor=1;
	} else if(strcmp(conf_displayunit.s,"m") == 0 ||
		strcmp(conf_displayunit.s,"M") == 0) {
	    conf_displayunit.s[0] = (char)toupper(conf_displayunit.s[0]);
	    unit_divisor=1024;
	} else if(strcmp(conf_displayunit.s,"g") == 0 ||
		strcmp(conf_displayunit.s,"G") == 0) {
	    conf_displayunit.s[0] = (char)toupper(conf_displayunit.s[0]);
	    unit_divisor=1024*1024;
	} else if(strcmp(conf_displayunit.s,"t") == 0 ||
		strcmp(conf_displayunit.s,"T") == 0) {
	    conf_displayunit.s[0] = (char)toupper(conf_displayunit.s[0]);
	    unit_divisor=1024*1024*1024;
	} else {
	    conf_parserror("displayunit must be k,m,g or t.");
	}
	break;

    /* kerberos 5 bits.  only useful when kerberos 5 built in... */
    case CONF_KRB5KEYTAB:
	get_simple(&conf_krb5keytab, &seen_krb5keytab, CONF_STRING);
	break;

    case CONF_KRB5PRINCIPAL:
	get_simple(&conf_krb5principal, &seen_krb5principal, CONF_STRING);
	break;

    case CONF_LOGFILE: /* XXX - historical */
	/* truncate the filename part and pretend he said "logdir" */
	{
	    char *p;

	    get_simple(&conf_logdir, &seen_logdir, CONF_STRING);

	    p = strrchr(conf_logdir.s, '/');
	    if (p != (char *)0) *p = '\0';
	}
	break;

    case CONF_DISKDIR:
	{
	    char *s;

	    get_conftoken(CONF_STRING);
	    s = tokenval.s;

	    if(!seen_diskdir) {
		conf_diskdir.s = newstralloc(conf_diskdir.s, s);
		seen_diskdir = conf_line_num;
	    }

	    init_holdingdisk_defaults();
	    hdcur.name = "default from DISKDIR";
	    hdcur.seen = conf_line_num;
	    hdcur.diskdir = stralloc(s);
	    hdcur.s_disk = conf_line_num;
	    hdcur.disksize = (off_t)conf_disksize.am64;
	    hdcur.s_size = seen_disksize;
	    save_holdingdisk();
	}
	break;

    case CONF_DISKSIZE:
	{
	    am64_t i;

	    i = (am64_t)get_am64_t();
	    i = (i / (am64_t)DISK_BLOCK_KB) * (am64_t)DISK_BLOCK_KB;

	    if(!seen_disksize) {
		conf_disksize.am64 = i;
		seen_disksize = conf_line_num;
	    }

	    if(holdingdisks != NULL)
		holdingdisks->disksize = (off_t)i;
	}
	break;

    case CONF_HOLDING:
	get_holdingdisk();
	break;

    case CONF_DEFINE:
	get_conftoken(CONF_ANY);
	if(tok == CONF_DUMPTYPE)
	    get_dumptype();
	else if(tok == CONF_TAPETYPE)
	    get_tapetype();
	else if(tok == CONF_INTERFACE)
	    get_interface();
	else
	    conf_parserror("DUMPTYPE, INTERFACE or TAPETYPE expected");
	break;

    case CONF_LABEL_NEW_TAPES:
	get_simple(&conf_label_new_tapes, &seen_label_new_tapes, CONF_STRING);
	break;

    case CONF_USETIMESTAMPS:
        get_simple(&conf_usetimestamps, &seen_usetimestamps, CONF_BOOL); break;

    case CONF_NL:	/* empty line */
	break;

    case CONF_END:	/* end of file */
	return 0;

    default:
	conf_parserror("configuration keyword expected");
    }
    if(tok != CONF_NL) get_conftoken(CONF_NL);
    return 1;
}

keytab_t holding_keytable[] = {
    { "DIRECTORY", CONF_DIRECTORY },
    { "COMMENT", CONF_COMMENT },
    { "USE", CONF_USE },
    { "CHUNKSIZE", CONF_CHUNKSIZE },
    { NULL, CONF_IDENT }
};

static void
get_holdingdisk(void)
{
    int done;
    int save_overwrites;
    keytab_t *save_kt;
    val_t tmpval;

    save_overwrites = allow_overwrites;
    allow_overwrites = 1;

    save_kt = keytable;
    keytable = holding_keytable;

    init_holdingdisk_defaults();

    get_conftoken(CONF_IDENT);
    hdcur.name = stralloc(tokenval.s);
    malloc_mark(hdcur.name);
    hdcur.seen = conf_line_num;

    get_conftoken(CONF_LBRACE);
    get_conftoken(CONF_NL);

    done = 0;
    do {
	/* Clear out any bogus string pointers */
	memset(&tmpval, 0, SIZEOF(tmpval));
	conf_line_num += 1;
	get_conftoken(CONF_ANY);
	switch(tok) {
	case CONF_COMMENT:
	    amfree(hdcur.comment);
	    get_simple(&tmpval, &hdcur.s_comment, CONF_STRING);
	    hdcur.comment = tmpval.s;
	    break;

	case CONF_DIRECTORY:
	    amfree(hdcur.diskdir);
	    get_simple(&tmpval, &hdcur.s_disk, CONF_STRING);
	    hdcur.diskdir = tmpval.s;
	    break;

	case CONF_USE:
	    get_simple(&tmpval, &hdcur.s_size, CONF_AM64);
	    hdcur.disksize = am_floor((off_t)tmpval.am64, (off_t)DISK_BLOCK_KB);
	    break;

	case CONF_CHUNKSIZE:
	    get_simple(&tmpval, &hdcur.s_csize, CONF_AM64);
	    hdcur.chunksize = (off_t)tmpval.am64;
	    if(hdcur.chunksize == (off_t)0) {
	        hdcur.chunksize = (off_t)((INT_MAX / 1024)-(2 * DISK_BLOCK_KB));
	    } else if(hdcur.chunksize < (off_t)0) {
		conf_parserror("Negative chunksize (" OFF_T_FMT ") is no longer supported",
			  (OFF_T_FMT_TYPE)hdcur.chunksize);
	    }
	    hdcur.chunksize = am_floor(hdcur.chunksize, (off_t)DISK_BLOCK_KB);
	    break;

	case CONF_RBRACE:
	    done = 1;
	    break;

	case CONF_NL:	/* empty line */
	    break;

	case CONF_END:	/* end of file */
	    done = 1;
	    /*FALLTHROUGH*/

	default:
	    conf_parserror("holding disk parameter expected");
	}
	if(tok != (tok_t)CONF_NL && tok != (tok_t)CONF_END)
	    get_conftoken(CONF_NL);
    } while(!done);

    save_holdingdisk();

    allow_overwrites = save_overwrites;
    keytable = save_kt;
}

static void
init_holdingdisk_defaults(void)
{
    hdcur.comment = stralloc("");
    hdcur.diskdir = stralloc(conf_diskdir.s);
    malloc_mark(hdcur.diskdir);
    hdcur.disksize = (off_t)0;
    hdcur.chunksize = (off_t)(1024*1024); /* 1 Gb = 1M counted in 1Kb blocks */

    hdcur.s_comment = 0;
    hdcur.s_disk = 0;
    hdcur.s_size = 0;
    hdcur.s_csize = 0;

    hdcur.up = (void *)0;
}

static void
save_holdingdisk(void)
{
    holdingdisk_t *hp;

    hp = alloc(SIZEOF(holdingdisk_t));
    malloc_mark(hp);
    *hp = hdcur;
    hp->next = holdingdisks;
    holdingdisks = hp;

    num_holdingdisks++;
}


keytab_t dumptype_keytable[] = {
    { "AUTH", CONF_AUTH },
    { "BUMPDAYS", CONF_BUMPDAYS },
    { "BUMPMULT", CONF_BUMPMULT },
    { "BUMPSIZE", CONF_BUMPSIZE },
    { "BUMPPERCENT", CONF_BUMPPERCENT },
    { "COMMENT", CONF_COMMENT },
    { "COMPRATE", CONF_COMPRATE },
    { "COMPRESS", CONF_COMPRESS },
    { "ENCRYPT", CONF_ENCRYPT },
    { "SERVER_DECRYPT_OPTION", CONF_SRV_DECRYPT_OPT },
    { "CLIENT_DECRYPT_OPTION", CONF_CLNT_DECRYPT_OPT },
    { "DUMPCYCLE", CONF_DUMPCYCLE },
    { "EXCLUDE", CONF_EXCLUDE },
    { "FREQUENCY", CONF_FREQUENCY },	/* XXX - historical */
    { "HOLDINGDISK", CONF_HOLDING },
    { "IGNORE", CONF_IGNORE },
    { "INCLUDE", CONF_INCLUDE },
    { "INDEX", CONF_INDEX },
    { "KENCRYPT", CONF_KENCRYPT },
    { "MAXCYCLE", CONF_MAXCYCLE },	/* XXX - historical */
    { "MAXDUMPS", CONF_MAXDUMPS },
    { "MAXPROMOTEDAY", CONF_MAXPROMOTEDAY },
    { "OPTIONS", CONF_OPTIONS },	/* XXX - historical */
    { "PRIORITY", CONF_PRIORITY },
    { "PROGRAM", CONF_PROGRAM },
    { "RECORD", CONF_RECORD },
    { "SKIP-FULL", CONF_SKIP_FULL },
    { "SKIP-INCR", CONF_SKIP_INCR },
    { "STARTTIME", CONF_STARTTIME },
    { "STRATEGY", CONF_STRATEGY },
    { "TAPE_SPLITSIZE", CONF_TAPE_SPLITSIZE },
    { "SPLIT_DISKBUFFER", CONF_SPLIT_DISKBUFFER },
    { "FALLBACK_SPLITSIZE", CONF_FALLBACK_SPLITSIZE },
    { "ESTIMATE", CONF_ESTIMATE },
    { "SERVER_CUSTOM_COMPRESS", CONF_SRVCOMPPROG },
    { "CLIENT_CUSTOM_COMPRESS", CONF_CLNTCOMPPROG },
    { "SERVER_ENCRYPT", CONF_SRV_ENCRYPT },
    { "CLIENT_ENCRYPT", CONF_CLNT_ENCRYPT },
    { "AMANDAD_PATH", CONF_AMANDAD_PATH },
    { "CLIENT_USERNAME", CONF_CLIENT_USERNAME },
    { "SSH_KEYS", CONF_SSH_KEYS },
    { NULL, CONF_IDENT }
};

dumptype_t *
read_dumptype(
    char *name,
    FILE *from,
    char *fname,
    int *linenum)
{
    int done;
    int save_overwrites;
    keytab_t *save_kt;
    val_t tmpval;
    FILE *saved_conf = NULL;
    char *saved_fname = NULL;
    time_t st = (time_t)start_time.r.tv_sec;
    struct tm *stm;

    if (from) {
	saved_conf = conf_conf;
	conf_conf = from;
    }

    if (fname) {
	saved_fname = conf_confname;
	conf_confname = fname;
    }

    if (linenum)
	conf_line_num = *linenum;

    save_overwrites = allow_overwrites;
    allow_overwrites = 1;

    save_kt = keytable;
    keytable = dumptype_keytable;

    init_dumptype_defaults();

    if (name) {
	dpcur.name = name;
    } else {
	get_conftoken(CONF_IDENT);
	dpcur.name = stralloc(tokenval.s);
	malloc_mark(dpcur.name);
    }

    dpcur.seen = conf_line_num;

    if (! name) {
	get_conftoken(CONF_LBRACE);
	get_conftoken(CONF_NL);
    }

    done = 0;
    do {
	/* Clear out any bogus string pointers */
	memset(&tmpval, 0, SIZEOF(tmpval));

	conf_line_num += 1;
	get_conftoken(CONF_ANY);
	switch(tok) {
	case CONF_AUTH:
	    amfree(dpcur.security_driver);
	    get_simple(&tmpval, &dpcur.s_security_driver, CONF_STRING);
	    dpcur.security_driver = tmpval.s;
	    break;

	case CONF_COMMENT:
	    amfree(dpcur.comment);
	    get_simple(&tmpval, &dpcur.s_comment, CONF_STRING);
	    dpcur.comment = tmpval.s;
	    break;

	case CONF_COMPRATE:
	    get_comprate();
	    break;

	case CONF_COMPRESS:
	    get_compress();
	    break;

	case CONF_ENCRYPT:
	    get_encrypt();
	    break;

	case CONF_SRV_DECRYPT_OPT:
	    amfree(dpcur.srv_decrypt_opt);
	    get_simple(&tmpval, &dpcur.s_srv_decrypt_opt, CONF_STRING);
	    dpcur.srv_decrypt_opt = tmpval.s;
	    break;

	case CONF_CLNT_DECRYPT_OPT:
	    amfree(dpcur.clnt_decrypt_opt);
	    get_simple(&tmpval, &dpcur.s_clnt_decrypt_opt, CONF_STRING);
	    dpcur.clnt_decrypt_opt = tmpval.s;
	    break;

	case CONF_DUMPCYCLE:
	    get_simple(&tmpval, &dpcur.s_dumpcycle, CONF_INT);
	    dpcur.dumpcycle = tmpval.i;
	    if(dpcur.dumpcycle < 0) {
		conf_parserror("dumpcycle must be positive");
	    }
	    break;

	case CONF_EXCLUDE:
	    get_exclude();
	    break;

	case CONF_FREQUENCY:
	    get_simple(&tmpval, &dpcur.s_frequency, CONF_INT);
	    dpcur.frequency = tmpval.i;
	    break;

	case CONF_HOLDING:
	    get_simple(&tmpval, &dpcur.s_no_hold, CONF_BOOL);
	    if (tmpval.i != 0)
		dpcur.no_hold = 1;
	    else
		dpcur.no_hold = 0;
	    break;

	case CONF_IGNORE:
	    get_simple(&tmpval, &dpcur.s_ignore, CONF_BOOL);
	    if (tmpval.i != 0)
		dpcur.ignore = 1;
	    else
		dpcur.ignore = 0;
	    break;

	case CONF_INCLUDE:
	    get_include();
	    break;

	case CONF_INDEX:
	    get_simple(&tmpval, &dpcur.s_index, CONF_BOOL);
	    if (tmpval.i != 0)
		dpcur.index = 1;
	    else
		dpcur.index = 0;
	    break;

	case CONF_KENCRYPT:
	    get_simple(&tmpval, &dpcur.s_kencrypt, CONF_BOOL);
	    if (tmpval.i != 0)
		dpcur.kencrypt = 1;
	    else
		dpcur.kencrypt = 0;
	    break;

	case CONF_MAXCYCLE:
	    get_simple(&tmpval, &dpcur.s_maxcycle, CONF_INT);
	    dpcur.maxcycle = tmpval.i;
	    break;

	case CONF_MAXDUMPS:
	    get_simple(&tmpval, &dpcur.s_maxdumps, CONF_INT);
	    dpcur.maxdumps = tmpval.i;
	    if(dpcur.maxdumps < 1) {
		conf_parserror("maxdumps must be positive");
	    }
	    break;

	case CONF_MAXPROMOTEDAY:
	    get_simple(&tmpval, &dpcur.s_maxpromoteday, CONF_INT);
	    dpcur.maxpromoteday = tmpval.i;
	    if(dpcur.maxpromoteday < 0) {
		conf_parserror("dpcur.maxpromoteday must be >= 0");
	    }
	    break;

	case CONF_BUMPPERCENT:
	    get_simple(&tmpval,  &dpcur.s_bumppercent,  CONF_INT);
	    dpcur.bumppercent = tmpval.i;
	    if(dpcur.bumppercent < 0 || dpcur.bumppercent > 100) {
		conf_parserror("bumppercent must be between 0 and 100");
	    }
	    break;

	case CONF_BUMPSIZE:
	    get_simple(&tmpval,  &dpcur.s_bumpsize,  CONF_AM64);
	    dpcur.bumpsize = (off_t)tmpval.am64;
	    if(dpcur.bumpsize < (off_t)1) {
		conf_parserror("bumpsize must be positive");
	    }
	    break;

	case CONF_BUMPDAYS:
	    get_simple(&tmpval,  &dpcur.s_bumpdays,  CONF_INT);
	    dpcur.bumpdays = tmpval.i;
	    if(dpcur.bumpdays < 1) {
		conf_parserror("bumpdays must be positive");
	    }
	    break;

	case CONF_BUMPMULT:
	    get_simple(&tmpval,  &dpcur.s_bumpmult,  CONF_REAL);
	    dpcur.bumpmult = tmpval.r;
	    if(dpcur.bumpmult < 0.999) {
		conf_parserror("bumpmult must be positive (%lf)",dpcur.bumpmult);
	    }
	    break;

	case CONF_OPTIONS:
	    get_dumpopts();
	    break;

	case CONF_PRIORITY:
	    get_priority();
	    break;

	case CONF_PROGRAM:
	    amfree(dpcur.program);
	    get_simple(&tmpval,  &dpcur.s_program,  CONF_STRING);
	    dpcur.program = tmpval.s;
	    break;

	case CONF_RECORD:
	    get_simple(&tmpval, &dpcur.s_record, CONF_BOOL);
	    if (tmpval.i != 0)
		dpcur.record = 1;
	    else
		dpcur.record = 0;
	    break;

	case CONF_SKIP_FULL:
	    get_simple(&tmpval, &dpcur.s_skip_full, CONF_BOOL);
	    if (tmpval.i != 0)
		dpcur.skip_full = 1;
	    else
		dpcur.skip_full = 0;
	    break;

	case CONF_SKIP_INCR:
	    get_simple(&tmpval, &dpcur.s_skip_incr, CONF_BOOL);
	    if (tmpval.i != 0)
		dpcur.skip_incr = 1;
	    else
		dpcur.skip_incr = 0;
	    break;

	case CONF_STARTTIME:
	    get_simple(&tmpval,  &dpcur.s_start_t,  CONF_TIME);
	    stm = localtime(&st);
	    st -= stm->tm_sec + 60 * (stm->tm_min + 60 * stm->tm_hour);
	    st += ((tmpval.t / 100 * 60) + tmpval.t % 100) * 60;
	    if ((st - start_time.r.tv_sec) < -43200)
		st += 86400;
	    dpcur.start_t = st;
	    break;

	case CONF_STRATEGY:
	    get_strategy();
	    break;

	case CONF_ESTIMATE:
	    get_estimate();
	    break;

	case CONF_IDENT:
	case CONF_STRING:
	    copy_dumptype();
	    break;

	case CONF_TAPE_SPLITSIZE:
	    get_simple(&tmpval,  &dpcur.s_tape_splitsize,  CONF_AM64);
	    dpcur.tape_splitsize = (off_t)tmpval.am64;
	    if(dpcur.tape_splitsize < (off_t)0) {
	      conf_parserror("tape_splitsize must be >= 0");
	    }
	    break;

	case CONF_SPLIT_DISKBUFFER:
	    amfree(dpcur.split_diskbuffer);
	    get_simple(&tmpval,  &dpcur.s_split_diskbuffer,  CONF_STRING);
	    dpcur.split_diskbuffer = tmpval.s;
	    break;

	case CONF_FALLBACK_SPLITSIZE:
	    get_simple(&tmpval,  &dpcur.s_fallback_splitsize,  CONF_AM64);
	    dpcur.fallback_splitsize = (off_t)tmpval.am64;
	    if(dpcur.fallback_splitsize < (off_t)0) {
		conf_parserror("fallback_splitsize must be >= 0");
	    }
	    break;

	case CONF_SRVCOMPPROG:
	    amfree(dpcur.srvcompprog);
	    get_simple(&tmpval,  &dpcur.s_srvcompprog,  CONF_STRING);
	    dpcur.srvcompprog = tmpval.s;
	    break;

	case CONF_CLNTCOMPPROG:
	    amfree(dpcur.clntcompprog);
	    get_simple(&tmpval,  &dpcur.s_clntcompprog,  CONF_STRING);
	    dpcur.clntcompprog = tmpval.s;
	    break;

	case CONF_SRV_ENCRYPT:
	    amfree(dpcur.srv_encrypt);
	    get_simple(&tmpval,  &dpcur.s_srv_encrypt,  CONF_STRING);
	    dpcur.srv_encrypt = tmpval.s;
	    break;

	case CONF_CLNT_ENCRYPT:
	    amfree(dpcur.clnt_encrypt);
	    get_simple(&tmpval,  &dpcur.s_clnt_encrypt,  CONF_STRING);
	    dpcur.clnt_encrypt = tmpval.s;
	    break;

        case CONF_AMANDAD_PATH:
	    amfree(dpcur.amandad_path);
	    get_simple(&tmpval, &dpcur.s_amandad_path, CONF_STRING);
	    dpcur.amandad_path = tmpval.s;
	    break;

        case CONF_CLIENT_USERNAME:
	    amfree(dpcur.client_username);
	    get_simple(&tmpval, &dpcur.s_client_username, CONF_STRING);
	    dpcur.client_username = tmpval.s;
	    break;

	case CONF_RBRACE:
	    done = 1;
	    break;

	case CONF_NL:	/* empty line */
	    break;

	case CONF_END:	/* end of file */
	    done = 1;
	    /*FALLTHROUGH*/

	default:
	    conf_parserror("dumptype parameter expected");
	}
	if(tok != (tok_t)CONF_NL && tok != (tok_t)CONF_END &&
	   /* When a name is specified, we shouldn't consume the CONF_NL
	      after the CONF_RBRACE.  */
	   (tok != (tok_t)CONF_RBRACE || name == 0))
	    get_conftoken(CONF_NL);
    } while(!done);

    /* XXX - there was a stupidity check in here for skip-incr and
    ** skip-full.  This check should probably be somewhere else. */

    save_dumptype();

    allow_overwrites = save_overwrites;
    keytable = save_kt;

    if (linenum)
	*linenum = conf_line_num;

    if (fname)
	conf_confname = saved_fname;

    if (from)
	conf_conf = saved_conf;

    return lookup_dumptype(dpcur.name);
}

static void
get_dumptype(void)
{
    read_dumptype(NULL, NULL, NULL, NULL);
}

static void
init_dumptype_defaults(void)
{
    dpcur.comment = stralloc("");
    dpcur.program = stralloc("DUMP");
    dpcur.srvcompprog = stralloc("");
    dpcur.clntcompprog = stralloc("");
    dpcur.srv_encrypt = stralloc("");
    dpcur.clnt_encrypt = stralloc("");
    dpcur.amandad_path = stralloc("X");
    dpcur.client_username = stralloc("X");
    dpcur.ssh_keys = stralloc("X");
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
    dpcur.bumpsize = (off_t)conf_bumpsize.am64;
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
    dpcur.skip_incr = 0;
    dpcur.skip_full = 0;
    dpcur.no_hold = 0;
    dpcur.kencrypt = 0;
    dpcur.ignore = 0;
    dpcur.index = 0;
    dpcur.tape_splitsize = (off_t)0;
    dpcur.split_diskbuffer = NULL;
    dpcur.fallback_splitsize = (off_t)(10 * 1024);

    dpcur.s_comment = 0;
    dpcur.s_program = 0;
    dpcur.s_srvcompprog = 0;
    dpcur.s_clntcompprog = 0;
    dpcur.s_clnt_encrypt= 0;
    dpcur.s_srv_encrypt= 0;
    dpcur.s_amandad_path= 0;
    dpcur.s_client_username= 0;
    dpcur.s_ssh_keys= 0;

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

static void
save_dumptype(void)
{
    dumptype_t *dp;

    dp = lookup_dumptype(dpcur.name);

    if(dp != (dumptype_t *)0) {
	conf_parserror("dumptype %s already defined on line %d", dp->name, dp->seen);
	return;
    }

    dp = alloc(SIZEOF(dumptype_t));
    malloc_mark(dp);
    *dp = dpcur;
    dp->next = dumplist;
    dumplist = dp;
}

static void
copy_dumptype(void)
{
    dumptype_t *dt;

    dt = lookup_dumptype(tokenval.s);
    if(dt == NULL) {
	conf_parserror("dumptype parameter expected");
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
    if(dt->s_ssh_keys) {
	dpcur.ssh_keys = newstralloc(dpcur.ssh_keys, dt->ssh_keys);
	dpcur.s_ssh_keys = dt->s_ssh_keys;
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
    { "COMMENT", CONF_COMMENT },
    { "LBL-TEMPL",	CONF_LBL_TEMPL },
    { "BLOCKSIZE",	CONF_BLOCKSIZE },
    { "FILE-PAD",	CONF_FILE_PAD },
    { "FILEMARK",	CONF_FILEMARK },
    { "LENGTH",		CONF_LENGTH },
    { "SPEED",		CONF_SPEED },
    { NULL,		CONF_IDENT }
};

static void
get_tapetype(void)
{
    int done;
    int save_overwrites;
    val_t tmpval;

    keytab_t *save_kt;

    save_overwrites = allow_overwrites;
    allow_overwrites = 1;

    save_kt = keytable;
    keytable = tapetype_keytable;

    init_tapetype_defaults();

    get_conftoken(CONF_IDENT);
    tpcur.name = stralloc(tokenval.s);
    malloc_mark(tpcur.name);
    tpcur.seen = conf_line_num;

    get_conftoken(CONF_LBRACE);
    get_conftoken(CONF_NL);

    done = 0;
    do {
	/* Clear out any bogus string pointers */
	memset(&tmpval, 0, SIZEOF(tmpval));

	conf_line_num += 1;
	get_conftoken(CONF_ANY);
	switch(tok) {
	case CONF_RBRACE:
	    done = 1;
	    break;

	case CONF_COMMENT:
	    amfree(tpcur.comment);
	    get_simple(&tmpval,  &tpcur.s_comment,  CONF_STRING);
	    tpcur.comment = tmpval.s;
	    break;

	case CONF_LBL_TEMPL:
	    amfree(tpcur.lbl_templ);
	    get_simple(&tmpval,  &tpcur.s_lbl_templ,  CONF_STRING);
	    tpcur.lbl_templ = tmpval.s;
	    break;

	case CONF_BLOCKSIZE:
	    get_simple(&tmpval,  &tpcur.s_blocksize,  CONF_SIZE);
	    tpcur.blocksize = tmpval.size;
	    if(tpcur.blocksize < DISK_BLOCK_KB) {
		conf_parserror("Tape blocksize must be at least %d KBytes",
			  DISK_BLOCK_KB);
	    } else if(tpcur.blocksize > MAX_TAPE_BLOCK_KB) {
		conf_parserror("Tape blocksize must not be larger than %d KBytes",
			  MAX_TAPE_BLOCK_KB);
	    }
	    break;

	case CONF_FILE_PAD:
	    get_simple(&tmpval, &tpcur.s_file_pad, CONF_BOOL);
	    if (tmpval.i != 0)
		tpcur.file_pad = 1;
	    else
		tpcur.file_pad = 0;
	    break;

	case CONF_LENGTH:
	    get_simple(&tmpval,  &tpcur.s_length, CONF_AM64);
	    if(tpcur.length < 0) {
		conf_parserror("Tape length must be positive");
	    }
	    tpcur.length = (off_t)tmpval.am64;
	    break;

	case CONF_FILEMARK:
	    get_simple(&tmpval, &tpcur.s_filemark, CONF_SIZE);
	    if(tmpval.size < 0) {
		conf_parserror("Tape file mark size must be positive");
	    }
	    tpcur.filemark = tmpval.size;
	    break;

	case CONF_SPEED:
	    get_simple(&tmpval,  &tpcur.s_speed, CONF_INT);
	    tpcur.speed = tmpval.i;
	    if(tpcur.speed < 0) {
		conf_parserror("Speed must be positive");
	    }
	    break;

	case CONF_IDENT:
	case CONF_STRING:
	    copy_tapetype();
	    break;

	case CONF_NL:	/* empty line */
	    break;

	case CONF_END:	/* end of file */
	    done = 1;

	default:
	    conf_parserror("tape type parameter expected");
	}
	if(tok != (tok_t)CONF_NL && tok != (tok_t)CONF_END)
	    get_conftoken(CONF_NL);
    } while(!done);

    save_tapetype();

    allow_overwrites = save_overwrites;
    keytable = save_kt;
}

static void
init_tapetype_defaults(void)
{
    tpcur.comment = stralloc("");
    tpcur.lbl_templ = stralloc("");
    tpcur.blocksize = (ssize_t)DISK_BLOCK_KB;
    tpcur.file_pad = 1;
    tpcur.length = (off_t)(2000 * 1024);
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

static void
save_tapetype(void)
{
    tapetype_t *tp;

    tp = lookup_tapetype(tpcur.name);

    if(tp != (tapetype_t *)0) {
	amfree(tpcur.name);
	conf_parserror("tapetype %s already defined on line %d", tp->name, tp->seen);
	return;
    }

    tp = alloc(SIZEOF(tapetype_t));
    malloc_mark(tp);
    *tp = tpcur;
    tp->next = tapelist;
    tapelist = tp;
}

static void
copy_tapetype(void)
{
    tapetype_t *tp;

    tp = lookup_tapetype(tokenval.s);

    if(tp == NULL) {
	conf_parserror("tape type parameter expected");
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
    { "COMMENT", CONF_COMMENT },
    { "USE", CONF_USE },
    { NULL, CONF_IDENT }
};

static void
get_interface(void)
{
    int done;
    int save_overwrites;
    keytab_t *save_kt;
    val_t tmpval;

    save_overwrites = allow_overwrites;
    allow_overwrites = 1;

    save_kt = keytable;
    keytable = interface_keytable;

    init_interface_defaults();

    get_conftoken(CONF_IDENT);
    ifcur.name = stralloc(tokenval.s);
    malloc_mark(ifcur.name);
    ifcur.seen = conf_line_num;

    get_conftoken(CONF_LBRACE);
    get_conftoken(CONF_NL);

    done = 0;
    do {
	/* Clear out any bogus string pointers */
	memset(&tmpval, 0, SIZEOF(tmpval));

	conf_line_num += 1;
	get_conftoken(CONF_ANY);
	switch(tok) {
	case CONF_RBRACE:
	    done = 1;
	    break;

	case CONF_COMMENT:
	    amfree(ifcur.comment);
	    get_simple(&tmpval,  &ifcur.s_comment,  CONF_STRING);
	    ifcur.comment = tmpval.s;
	    break;

	case CONF_USE:
	    get_simple(&tmpval,  &ifcur.s_maxusage, CONF_LONG);
	    ifcur.maxusage = (unsigned long)tmpval.l;
	    if(ifcur.maxusage < 1) {
		conf_parserror("use must be positive");
	    }
	    break;

	case CONF_IDENT:
	case CONF_STRING:
	    copy_interface();
	    break;

	case CONF_NL:	/* empty line */
	    break;

	case CONF_END:	/* end of file */
	    done = 1;

	default:
	    conf_parserror("interface parameter expected");
	}
	if(tok != (tok_t)CONF_NL && tok != (tok_t)CONF_END)
	    get_conftoken(CONF_NL);
    } while(!done);

    save_interface();

    allow_overwrites = save_overwrites;
    keytable = save_kt;

    return;
}

static void
init_interface_defaults(void)
{
    ifcur.comment = stralloc("");
    ifcur.maxusage = 300L;

    ifcur.s_comment = 0;
    ifcur.s_maxusage = 0;

    ifcur.curusage = 0;
}

static void
save_interface(void)
{
    interface_t *ip;

    ip = lookup_interface(ifcur.name);

    if(ip != (interface_t *)0) {
	conf_parserror("interface %s already defined on line %d",
			ip->name, ip->seen);
	return;
    }

    ip = alloc(SIZEOF(interface_t));
    malloc_mark(ip);
    *ip = ifcur;
    ip->next = interface_list;
    interface_list = ip;
}

static void
copy_interface(void)
{
    interface_t *ip;

    ip = lookup_interface(tokenval.s);

    if(ip == NULL) {
	conf_parserror("interface parameter expected");
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
    { "COMPRESS", CONF_COMPRESS },
    { "ENCRYPT", CONF_ENCRYPT },
    { "INDEX", CONF_INDEX },
    { "EXCLUDE-FILE", CONF_EXCLUDE_FILE },
    { "EXCLUDE-LIST", CONF_EXCLUDE_LIST },
    { "KENCRYPT", CONF_KENCRYPT },
    { "SKIP-FULL", CONF_SKIP_FULL },
    { "SKIP-INCR", CONF_SKIP_INCR },
    { NULL, CONF_IDENT }
};

/* XXX - for historical compatability */
static void
get_dumpopts(void)
{
    int done;
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = dumpopts_keytable;

    done = 0;
    do {
	get_conftoken(CONF_ANY);
	switch(tok) {
	case CONF_COMPRESS:   ckseen(&dpcur.s_compress);  dpcur.compress = COMP_FAST; break;
	case CONF_ENCRYPT:   ckseen(&dpcur.s_encrypt);  dpcur.encrypt = ENCRYPT_NONE; break;
	case CONF_EXCLUDE_FILE:
	    ckseen(&dpcur.s_exclude_file);
	    get_conftoken(CONF_STRING);
	    dpcur.exclude_file = append_sl(dpcur.exclude_file, tokenval.s);
	    break;
	case CONF_EXCLUDE_LIST:
	    ckseen(&dpcur.s_exclude_list);
	    get_conftoken(CONF_STRING);
	    dpcur.exclude_list = append_sl(dpcur.exclude_list, tokenval.s);
	    break;
	case CONF_KENCRYPT:   ckseen(&dpcur.s_kencrypt);  dpcur.kencrypt = 1; break;
	case CONF_SKIP_INCR:  ckseen(&dpcur.s_skip_incr); dpcur.skip_incr= 1; break;
	case CONF_SKIP_FULL:  ckseen(&dpcur.s_skip_full); dpcur.skip_full= 1; break;
	case CONF_INDEX:      ckseen(&dpcur.s_index);     dpcur.index    = 1; break;
	case CONF_IDENT:
	    copy_dumptype();
	    break;
	case CONF_NL: done = 1; break;
	case CONF_COMMA: break;
	case CONF_END:
	    done = 1;
	default:
	    conf_parserror("dump option expected");
	}
    } while(!done);

    keytable = save_kt;
}

static void
get_comprate(void)
{
    val_t var;

    get_simple(&var, &dpcur.s_comprate, CONF_REAL);
    dpcur.comprate[0] = dpcur.comprate[1] = var.r;
    if(dpcur.comprate[0] < 0) {
	conf_parserror("full compression rate must be >= 0");
    }

    get_conftoken(CONF_ANY);
    switch(tok) {
    case CONF_NL:
	return;
    case CONF_COMMA:
	break;
    default:
	unget_conftoken();
    }

    get_conftoken(CONF_REAL);
    dpcur.comprate[1] = tokenval.r;
    if(dpcur.comprate[1] < 0) {
	conf_parserror("incremental compression rate must be >= 0");
    }
}

keytab_t compress_keytable[] = {
    { "BEST", CONF_BEST },
    { "CLIENT", CONF_CLIENT },
    { "FAST", CONF_FAST },
    { "NONE", CONF_NONE },
    { "SERVER", CONF_SERVER },
    { "CUSTOM", CONF_CUSTOM },
    { NULL, CONF_IDENT }
};

static void
get_compress(void)
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
	get_conftoken(CONF_ANY);
	switch(tok) {
	case CONF_NONE:   none = 1; break;
	case CONF_FAST:   fast = 1; break;
	case CONF_BEST:   best = 1; break;
	case CONF_CLIENT: clie = 1; break;
	case CONF_SERVER: serv = 1; break;
	case CONF_CUSTOM: custom=1; break;
	case CONF_NL:     done = 1; break;
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
	conf_parserror("NONE, CLIENT FAST, CLIENT BEST, CLIENT CUSTOM, SERVER FAST, SERVER BEST or SERVER CUSTOM expected");
	comp = COMP_NONE;
    }

    dpcur.compress = comp;

    keytable = save_kt;
}

keytab_t encrypt_keytable[] = {
    { "NONE", CONF_NONE },
    { "CLIENT", CONF_CLIENT },
    { "SERVER", CONF_SERVER },
    { NULL, CONF_IDENT }
};

static void
get_encrypt(void)
{
   keytab_t *save_kt;
   int encrypt;

   save_kt = keytable;
   keytable = encrypt_keytable;

   ckseen(&dpcur.s_encrypt);

   get_conftoken(CONF_ANY);
   switch(tok) {
   case CONF_NONE:  
     encrypt = ENCRYPT_NONE; 
     break;
   case CONF_CLIENT:  
     encrypt = ENCRYPT_CUST;
     break;
   case CONF_SERVER: 
     encrypt = ENCRYPT_SERV_CUST;
     break;
   default:
     conf_parserror("NONE, CLIENT or SERVER expected");
     encrypt = ENCRYPT_NONE;
   }

   dpcur.encrypt = encrypt;
   keytable = save_kt;	
}

keytab_t taperalgo_keytable[] = {
    { "FIRST", CONF_FIRST },
    { "FIRSTFIT", CONF_FIRSTFIT },
    { "LARGEST", CONF_LARGEST },
    { "LARGESTFIT", CONF_LARGESTFIT },
    { "SMALLEST", CONF_SMALLEST },
    { "LAST", CONF_LAST },
    { NULL, CONF_IDENT }
};

static void
get_taperalgo(
    val_t *c_taperalgo,
    int *s_taperalgo)
{
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = taperalgo_keytable;

    ckseen(s_taperalgo);

    get_conftoken(CONF_ANY);
    switch(tok) {
    case CONF_FIRST:      c_taperalgo->i = ALGO_FIRST;      break;
    case CONF_FIRSTFIT:   c_taperalgo->i = ALGO_FIRSTFIT;   break;
    case CONF_LARGEST:    c_taperalgo->i = ALGO_LARGEST;    break;
    case CONF_LARGESTFIT: c_taperalgo->i = ALGO_LARGESTFIT; break;
    case CONF_SMALLEST:   c_taperalgo->i = ALGO_SMALLEST;   break;
    case CONF_LAST:       c_taperalgo->i = ALGO_LAST;       break;
    default:
	conf_parserror("FIRST, FIRSTFIT, LARGEST, LARGESTFIT, SMALLEST or LAST expected");
    }

    keytable = save_kt;
}

keytab_t priority_keytable[] = {
    { "HIGH", CONF_HIGH },
    { "LOW", CONF_LOW },
    { "MEDIUM", CONF_MEDIUM },
    { NULL, CONF_IDENT }
};

static void
get_priority(void)
{
    int pri;
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = priority_keytable;

    ckseen(&dpcur.s_priority);

    get_conftoken(CONF_ANY);
    switch(tok) {
    case CONF_LOW: pri = 0; break;
    case CONF_MEDIUM: pri = 1; break;
    case CONF_HIGH: pri = 2; break;
    case CONF_INT: pri = tokenval.i; break;
    default:
	conf_parserror("LOW, MEDIUM, HIGH or integer expected");
	pri = 0;
    }
    dpcur.priority = pri;

    keytable = save_kt;
}

keytab_t strategy_keytable[] = {
    { "HANOI", CONF_HANOI },
    { "NOFULL", CONF_NOFULL },
    { "NOINC", CONF_NOINC },
    { "SKIP", CONF_SKIP },
    { "STANDARD", CONF_STANDARD },
    { "INCRONLY", CONF_INCRONLY },
    { NULL, CONF_IDENT }
};

static void
get_strategy(void)
{
    int strat;
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = strategy_keytable;

    ckseen(&dpcur.s_strategy);

    get_conftoken(CONF_ANY);
    switch(tok) {
    case CONF_SKIP:
	strat = DS_SKIP;
	break;
    case CONF_STANDARD:
	strat = DS_STANDARD;
	break;
    case CONF_NOFULL:
	strat = DS_NOFULL;
	break;
    case CONF_NOINC:
	strat = DS_NOINC;
	break;
    case CONF_HANOI:
	strat = DS_HANOI;
	break;
    case CONF_INCRONLY:
	strat = DS_INCRONLY;
	break;
    default:
	conf_parserror("STANDARD or NOFULL expected");
	strat = DS_STANDARD;
    }
    dpcur.strategy = strat;

    keytable = save_kt;
}

keytab_t estimate_keytable[] = {
    { "CLIENT", CONF_CLIENT },
    { "SERVER", CONF_SERVER },
    { "CALCSIZE", CONF_CALCSIZE }
};

static void
get_estimate(void)
{
    int estime;
    keytab_t *save_kt;

    save_kt = keytable;
    keytable = estimate_keytable;

    ckseen(&dpcur.s_estimate);

    get_conftoken(CONF_ANY);
    switch(tok) {
    case CONF_CLIENT:
	estime = ES_CLIENT;
	break;
    case CONF_SERVER:
	estime = ES_SERVER;
	break;
    case CONF_CALCSIZE:
	estime = ES_CALCSIZE;
	break;
    default:
	conf_parserror("CLIENT, SERVER or CALCSIZE expected");
	estime = ES_CLIENT;
    }
    dpcur.estimate = estime;

    keytable = save_kt;
}

keytab_t exclude_keytable[] = {
    { "LIST", CONF_LIST },
    { "FILE", CONF_EFILE },
    { "APPEND", CONF_APPEND },
    { "OPTIONAL", CONF_OPTIONAL },
    { NULL, CONF_IDENT }
};

static void
get_exclude(void)
{
    int list, got_one = 0;
    keytab_t *save_kt;
    sl_t *exclude;
    int optional = 0;
    int append;

    save_kt = keytable;
    keytable = exclude_keytable;

    get_conftoken(CONF_ANY);
    if(tok == CONF_LIST) {
	list = 1;
	exclude = dpcur.exclude_list;
	ckseen(&dpcur.s_exclude_list);
	get_conftoken(CONF_ANY);
    }
    else {
	list = 0;
	exclude = dpcur.exclude_file;
	ckseen(&dpcur.s_exclude_file);
	if(tok == CONF_EFILE) get_conftoken(CONF_ANY);
    }

    if(tok == CONF_OPTIONAL) {
	get_conftoken(CONF_ANY);
	optional = 1;
    }

    if(tok == CONF_APPEND) {
	get_conftoken(CONF_ANY);
	append = 1;
    }
    else {
	free_sl(exclude);
	exclude = NULL;
	append = 0;
    }

    while(tok == CONF_STRING) {
	exclude = append_sl(exclude, tokenval.s);
	got_one = 1;
	get_conftoken(CONF_ANY);
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


static void
get_include(void)
{
    int list, got_one = 0;
    keytab_t *save_kt;
    sl_t *include;
    int optional = 0;
    int append;

    save_kt = keytable;
    keytable = exclude_keytable;

    get_conftoken(CONF_ANY);
    if(tok == CONF_LIST) {
	list = 1;
	include = dpcur.include_list;
	ckseen(&dpcur.s_include_list);
	get_conftoken(CONF_ANY);
    }
    else {
	list = 0;
	include = dpcur.include_file;
	ckseen(&dpcur.s_include_file);
	if(tok == CONF_EFILE) get_conftoken(CONF_ANY);
    }

    if(tok == CONF_OPTIONAL) {
	get_conftoken(CONF_ANY);
	optional = 1;
    }

    if(tok == CONF_APPEND) {
	get_conftoken(CONF_ANY);
	append = 1;
    }
    else {
	free_sl(include);
	include = NULL;
	append = 0;
    }

    while(tok == CONF_STRING) {
	include = append_sl(include, tokenval.s);
	got_one = 1;
	get_conftoken(CONF_ANY);
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


int
ColumnDataCount(void)
{
    return (int)(sizeof(ColumnData) / sizeof(ColumnData[0]));
}

/* conversion from string to table index
 */
int
StringToColumn(
    char *s)
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
LastChar(
    char *s)
{
    return s[strlen(s)-1];
}

int
SetColumDataFromString(
    ColumnInfo* ci,
    char *s,
    char **errstr)
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

    (void)ci;	/* Quiet unused parameter warning */

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
	ColumnData[cn].PrefixSpace = (char)Space;
	if (LastChar(ColumnData[cn].Format) == 's') {
	    if (Width < 0)
		ColumnData[cn].MaxWidth = 1;
	    else
		if (Width > ColumnData[cn].Precision)
		    ColumnData[cn].Precision = (char)Width;
	}
	else if (Width < ColumnData[cn].Precision)
	    ColumnData[cn].Precision = (char)Width;
	s= strchr(eon+1, ',');
	if (s != NULL)
	    s++;
    }
    return 0;
}


char *
taperalgo2str(
    int taperalgo)
{
    if(taperalgo == ALGO_FIRST) return "FIRST";
    if(taperalgo == ALGO_FIRSTFIT) return "FIRSTFIT";
    if(taperalgo == ALGO_LARGEST) return "LARGEST";
    if(taperalgo == ALGO_LARGESTFIT) return "LARGESTFIT";
    if(taperalgo == ALGO_SMALLEST) return "SMALLEST";
    if(taperalgo == ALGO_LAST) return "LAST";
    return "UNKNOWN";
}

long
getconf_unit_divisor(void)
{
    return unit_divisor;
}

/* ------------------------ */


#ifdef TEST

void dump_configuration(char *filename);

void
dump_configuration(
    char *filename)
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
    printf("conf_bumpsize = " OFF_T_FMT "\n",
		(OFF_T_FMT_TYPE)getconf_int(CNF_BUMPSIZE));
    printf("conf_bumpdays = %d\n", getconf_int(CNF_BUMPDAYS));
    printf("conf_bumpmult = %lf\n", getconf_real(CNF_BUMPMULT));
    printf("conf_netusage = %ld\n", getconf_long(CNF_NETUSAGE));
    printf("conf_inparallel = %d\n", getconf_int(CNF_INPARALLEL));
    printf("conf_dumporder = \"%s\"\n", getconf_str(CNF_DUMPORDER));
    /*printf("conf_timeout = " TIME_T_FMT "\n",
			(TIME_T_FMT_TYPE)getconf_time(CNF_TIMEOUT));*/
    printf("conf_maxdumps = %d\n", getconf_int(CNF_MAXDUMPS));
    printf("conf_etimeout = " TIME_T_FMT "\n",
			(TIME_T_FMT_TYPE)getconf_time(CNF_ETIMEOUT));
    printf("conf_dtimeout = " TIME_T_FMT "\n",
			(TIME_T_FMT_TYPE)getconf_time(CNF_DTIMEOUT));
    printf("conf_ctimeout = " TIME_T_FMT "\n",
			(TIME_T_FMT_TYPE)getconf_time(CNF_CTIMEOUT));
    printf("conf_tapebufs = %d\n", getconf_int(CNF_TAPEBUFS));
    printf("conf_autoflush  = %d\n", getconf_int(CNF_AUTOFLUSH));
    printf("conf_reserve  = %d\n", getconf_int(CNF_RESERVE));
    printf("conf_maxdumpsize  = " OFF_T_FMT "\n",
		(OFF_T_FMT_TYPE)getconf_am64(CNF_MAXDUMPSIZE));
    printf("conf_amrecover_do_fsf  = " OFF_T_FMT "\n",
		(OFF_T_FMT_TYPE)getconf_am64(CNF_AMRECOVER_DO_FSF));
    printf("conf_amrecover_check_label  = %d\n", getconf_int(CNF_AMRECOVER_CHECK_LABEL));
    printf("conf_amrecover_changer = \"%s\"\n", getconf_str(CNF_AMRECOVER_CHANGER));
    printf("conf_taperalgo  = %s\n", taperalgo2str(getconf_int(CNF_TAPERALGO)));
    printf("conf_displayunit  = %s\n", getconf_str(CNF_DISPLAYUNIT));

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
	printf("	SIZE " OFF_T_FMT "\n", (OFF_T_FMT_TYPE)hp->disksize);
	printf("	CHUNKSIZE " OFF_T_FMT "\n", (OFF_T_FMT_TYPE)hp->chunksize);
    }

    for(tp = tapelist; tp != NULL; tp = tp->next) {
	printf("\nTAPETYPE %s:\n", tp->name);
	printf("	COMMENT \"%s\"\n", tp->comment);
	printf("	LBL_TEMPL %s\n", tp->lbl_templ);
	printf("	BLOCKSIZE " SSIZE_T_FMT "\n", tp->blocksize);
	printf("	FILE_PAD %s\n", (tp->file_pad) ? "YES" : "NO");
	printf("	LENGTH " OFF_T_FMT "\n", (OFF_T_FMT_TYPE)tp->length);
	printf("	FILEMARK " SSIZE_T_FMT "\n", tp->filemark);
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
	printf("	SSH_KEYS \"%s\"\n", dp->ssh_keys);
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
	printf("	COMPRATE %lf, %lf\n", dp->comprate[0], dp->comprate[1]);

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
	printf("	USE %ld\n", ip->maxusage);
    }
}

int
main(
    int argc,
    char *argv[])
{
  char *conffile;
  char *diskfile;
  disklist_t lst;
  int result;
  unsigned long malloc_hist_1, malloc_size_1;
  unsigned long malloc_hist_2, malloc_size_2;

  safe_fd(-1, 0);

  set_pname("conffile");

  dbopen();

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

    if (getcwd(my_cwd, SIZEOF(my_cwd)) == NULL) {
      error("cannot determine current working directory");
      /*NOTREACHED*/
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
generic_get_security_conf(
    char *string,
    void *arg)
{
	(void)arg;	/* Quiet unused parameter warning */

	if(!string || !*string)
		return(NULL);

	if(strcmp(string, "krb5principal")==0) {
		return(getconf_str(CNF_KRB5PRINCIPAL));
	} else if(strcmp(string, "krb5keytab")==0) {
		return(getconf_str(CNF_KRB5KEYTAB));
	}
	return(NULL);
}
