
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
 * conffile.c - read configuration file
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

struct tapeseen_s {
    int comment;
    int length;
    int filemark;
    int speed;
};

struct dumpseen_s {
    int comment;
    int program;
    int priority;
    int dumpcycle;
    int maxcycle;
    int frequency;
    int maxdumps;
    int start_t;
    /* flag options */
    int compress;
    int record;
    int skip_incr;
    int skip_full;
    int no_full;
    int no_hold;
    int auth;
    int kencrypt;
    int index;
};

typedef enum { 
    UNKNOWN, ANY, INT, REAL, STRING, TIME, IDENT, COMMA, LBRACE,
    RBRACE, NL, END, ORG, MAILTO, DUMPUSER, DUMPCYCLE, MAXCYCLE,
    TAPECYCLE, TAPEDEV, LABELSTR,
    BUMPSIZE, BUMPDAYS, BUMPMULT,
    TAPELIST, DISKFILE, INCLUDEFILE, INFOFILE, LOGFILE,
    DISKDIR, DISKSIZE, INDEXDIR, NETUSAGE, INPARALLEL, TIMEOUT, DEFINE,
    TAPETYPE, TPCHANGER, RUNTAPES, COMMENT, LENGTH, FILEMARK, SPEED,
    DUMPTYPE, OPTIONS, PRIORITY, FREQUENCY, PROGRAM, MAXDUMPS,
    STARTTIME, NO_COMPRESS, COMPR, COMPR_BEST, COMPR_FAST, SRVCOMPRESS,
    SKIP_INCR, SKIP_FULL, NO_FULL, NO_RECORD, NO_HOLD,
    KRB4_AUTH, BSD_AUTH, KENCRYPT,
    LOW, MEDIUM, HIGH, 
    INFINITY,
    MULT1, MULT7, MULT1K, MULT1M,
    INDEX
} tok_t;

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
static val_t conf_labelstr;
static val_t conf_tapelist;
static val_t conf_infofile;
static val_t conf_logfile;
static val_t conf_diskfile;
static val_t conf_diskdir;
static val_t tapetype_id;
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
static tapetype_t tpcur;
static struct tapeseen_s tpseen;

static dumptype_t dpcur;
static struct dumpseen_s dpseen;

static int seen_org, seen_mailto, seen_dumpuser, seen_tapedev, seen_tpchanger;
static int seen_labelstr, seen_runtapes, seen_maxdumps;
static int seen_tapelist, seen_infofile, seen_diskfile, seen_diskdir;
static int seen_logfile, seen_bumpsize, seen_bumpmult, seen_bumpdays;
static int seen_tapetype, seen_dumpcycle, seen_maxcycle, seen_tapecycle;
static int seen_disksize, seen_netusage, seen_inparallel, seen_timeout;
static int seen_indexdir;

static tok_t tok;
static val_t tokenval;

static int line_num, got_parserror;
static dumptype_t *dumplist = NULL;
static tapetype_t *tapelist = NULL;
static FILE *conf = (FILE *)NULL;
static char *confname = NULL;

/* predeclare Local Functions */

static void init_defaults P((void));
static void init_string P((char **ptrp, char *str));

static int read_confline P((void));
static void get_dumptype P((void));
static void init_dumpdefaults P((void));
static void get_tapetype P((void));
static void init_tapedefaults P((void));
static void get_simple P((val_t *var, int *seen, tok_t type));
static void get_time P((val_t *var, int *seen));

static void parserror P((char *format, ...));
static void get_dumpopts P((void));
static void get_priority P((void));
static void ckseen P((int *seen));
static int get_number P((void));
static tok_t lookup_keyword P((char *str));
static void get_conftoken P((tok_t exp));


void read_conffile_recursively(filename)
char *filename;
{
    extern int errno;

    /* Save globals used in read_confline(), elsewhere. */
    int  save_line_num  = line_num;
    FILE *save_conf     = conf;
    char *save_confname = confname;

    confname = NULL;	/* Protect it from free() in init_string() */
    init_string(&confname, filename);

    if((conf = fopen(filename, "r")) == NULL)
       error("could not open conf file \"%s\": %s", filename, strerror(errno));

    line_num = 0;

    /* read_confline() can invoke us recursively via "includefile" */
    while(read_confline());
    fclose(conf);

    /* Restore globals */
    line_num = save_line_num;
    conf     = save_conf;
    free(confname);	/* Free what was allocated by init_string() above */
    confname = save_confname;
}    


int read_conffile(filename)
char *filename;
{
    init_defaults();

    /* We assume that conffile & conf are initialized to NULL above */
    read_conffile_recursively(filename);

    if(lookup_tapetype(tapetype_id.s) == NULL) {
	if(!seen_tapetype)
	    parserror("default tapetype %s not defined", tapetype_id.s);
	else {
	    line_num = seen_tapetype;
	    parserror("tapetype %s not defined", tapetype_id.s);
	}
    }
    return got_parserror;
}


/* ------------------------ */


static void init_defaults()
{
    dumptype_t *dp;
    tapetype_t *tp;
    holdingdisk_t *hp;

    /* defaults for exported variables */

    init_string(&conf_org.s,
#ifdef DEFAULT_CONFIG
		DEFAULT_CONFIG
#else
		"YOUR ORG"
#endif
		);
    init_string(&conf_mailto.s, "operators");
    init_string(&conf_dumpuser.s,
#ifdef CLIENT_LOGIN
		CLIENT_LOGIN
#else
		"bin"
#endif
		);
    init_string(&conf_tapedev.s,
#ifdef DEFAULT_TAPE_DEVICE
		DEFAULT_TAPE_DEVICE
#else
		"/dev/rmt8"
#endif
		);
    init_string(&conf_tpchanger.s, "");
    init_string(&conf_labelstr.s, ".*");
    init_string(&conf_tapelist.s, "tapelist");
    init_string(&conf_infofile.s,
#ifdef DB_DIR
		DB_DIR "/curinfo"
#else
		"/usr/adm/amanda/curinfo"
#endif
		);
    init_string(&conf_logfile.s,
#ifdef LOG_DIR
		LOG_DIR "/log"
#else
		"/usr/adm/amanda/log"
#endif
		);
    init_string(&conf_diskfile.s, "disklist");
    init_string(&conf_diskdir.s, "/dumps/amanda");
    init_string(&tapetype_id.s, "EXABYTE");
    init_string(&conf_indexdir.s,
#ifdef INDEX_DIR
		INDEX_DIR
#else
		"/usr/adm/amanda/index"
#endif
		);

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

    hp = alloc(sizeof(holdingdisk_t));
    hp->disksize = conf_disksize.i;
    hp->diskdir = stralloc(conf_diskdir.s);
    hp->next = NULL;

    holdingdisks = hp;
    num_holdingdisks = 1;

    /* defaults for internal variables */

    seen_org = seen_mailto = seen_dumpuser = seen_tapedev = 0;
    seen_tpchanger = seen_labelstr = seen_runtapes = seen_maxdumps = 0;
    seen_tapelist = seen_infofile = seen_diskfile = seen_diskdir = 0;
    seen_logfile = seen_bumpsize = seen_bumpmult = seen_bumpdays = 0;
    seen_tapetype = seen_dumpcycle = seen_maxcycle = seen_tapecycle = 0;
    seen_disksize = seen_netusage = seen_inparallel = seen_timeout = 0;
    seen_indexdir = 0;
    line_num = got_parserror = 0;

    /* free any previously declared dump and tape types */

    while(dumplist != NULL) {
	dp = dumplist;
	dumplist = dumplist->next;
	free(dp);
    }
    while(tapelist != NULL) {
	tp = tapelist;
	tapelist = tapelist->next;
	free(tp);
    }
}   

static void init_string(ptrp, str)
char *str, **ptrp;
{
    if(*ptrp) free(*ptrp);
    *ptrp = stralloc(str);
}


/* ------------------------ */

static int read_confline()
{
    line_num += 1;
    get_conftoken(ANY);
    switch(tok) {
    case INCLUDEFILE:
        {
	  char *fn;

	  get_conftoken(STRING);
	  fn = tokenval.s;
	  get_conftoken(NL);
	  read_conffile_recursively(fn);
        }
        break;

    case ORG:	    get_simple(&conf_org,       &seen_org,       STRING);break;
    case MAILTO:    get_simple(&conf_mailto,    &seen_mailto,    STRING);break;
    case DUMPUSER:  get_simple(&conf_dumpuser,  &seen_dumpuser,  STRING);break;
    case DUMPCYCLE: get_simple(&conf_dumpcycle, &seen_dumpcycle, INT);	 break;
    case MAXCYCLE:  get_simple(&conf_maxcycle,  &seen_maxcycle,  INT);	 break;
    case TAPECYCLE: get_simple(&conf_tapecycle, &seen_tapecycle, INT);	 break;
    case RUNTAPES:  get_simple(&conf_runtapes,  &seen_runtapes,  INT);	 break;
    case TAPEDEV:   get_simple(&conf_tapedev,   &seen_tapedev,   STRING);break;
    case TPCHANGER: get_simple(&conf_tpchanger, &seen_tpchanger, STRING);break;
    case LABELSTR:  get_simple(&conf_labelstr,  &seen_labelstr,  STRING);break;
    case TAPELIST:  get_simple(&conf_tapelist,  &seen_tapelist,  STRING);break;
    case INFOFILE:  get_simple(&conf_infofile,  &seen_infofile,  STRING);break;
    case LOGFILE:   get_simple(&conf_logfile,   &seen_logfile,   STRING);break;
    case DISKFILE:  get_simple(&conf_diskfile,  &seen_diskfile,  STRING);break;
    case BUMPMULT:  get_simple(&conf_bumpmult,  &seen_bumpmult,  REAL);  break;
    case BUMPSIZE:  get_simple(&conf_bumpsize,  &seen_bumpsize,  INT);   break;
    case BUMPDAYS:  get_simple(&conf_bumpdays,  &seen_bumpdays,  INT);   break;
    case NETUSAGE:  get_simple(&conf_netusage,  &seen_netusage,  INT);	 break;
    case INPARALLEL:get_simple(&conf_inparallel,&seen_inparallel,INT);	 break;
    case TIMEOUT:   get_simple(&conf_timeout,   &seen_timeout,   INT);	 break;
    case MAXDUMPS:  get_simple(&conf_maxdumps,  &seen_maxdumps,   INT);	 break;
    case TAPETYPE:  get_simple(&tapetype_id,    &seen_tapetype,  IDENT); break;
    case INDEXDIR:  get_simple(&conf_indexdir,  &seen_indexdir,  STRING);break;


    case DISKDIR:
	assert(holdingdisks != NULL);

	get_conftoken(STRING);
	if(!seen_diskdir) {
	    /* for the first one, replace the prev allocated disk rec */
	    holdingdisks->diskdir = conf_diskdir.s = stralloc(tokenval.s);
	    seen_diskdir = 1;
	}
	else {
	    /* for subsequent disks, make a new disk rec */
	    holdingdisk_t *hp;

	    hp = alloc(sizeof(holdingdisk_t));
	    hp->diskdir = stralloc(tokenval.s);
	    hp->disksize = holdingdisks->disksize;
	    hp->next = holdingdisks;
	    holdingdisks = hp;
	    num_holdingdisks++;
	}
	get_conftoken(NL);
	break;
    case DISKSIZE:  
	assert(holdingdisks != NULL);
	holdingdisks->disksize = get_number();

	if(!seen_disksize) {
	    conf_disksize.i = holdingdisks->disksize;
	    seen_disksize = 1;
	}

	if(tok != NL) get_conftoken(NL);
	break;

    case DEFINE:
	get_conftoken(ANY);
	if(tok == DUMPTYPE) get_dumptype();
	else if(tok == TAPETYPE) get_tapetype();
	else parserror("DUMPTYPE or TAPETYPE expected");
	break;
    case NL:	
	/* empty line */
	break;
    case END:
	/* end of file */
	return 0;
    default:
	parserror("configuration keyword expected");
    }
    return 1;
}

static void get_dumptype()
{
    int done = 0;
    dumptype_t *p;

    init_dumpdefaults();

    get_conftoken(IDENT);
    dpcur.name = stralloc(tokenval.s);
    dpcur.seen = line_num;

    get_conftoken(LBRACE);
    get_conftoken(NL);

    do {
	line_num += 1;
	get_conftoken(ANY);
	switch(tok) {

	case RBRACE:
	    done = 1;
	    break;
	case COMMENT:
	    get_simple((val_t *)&dpcur.comment, &dpseen.comment, STRING);
	    break;
	case OPTIONS:
	    get_dumpopts();
	    break;
	case PRIORITY:
	    get_priority();
	    break;
	case DUMPCYCLE:
	    get_simple((val_t *)&dpcur.dumpcycle, &dpseen.dumpcycle, INT);
	    break;
	case MAXCYCLE:
	    get_simple((val_t *)&conf_maxcycle, &dpseen.maxcycle, INT);
	    break;
	case FREQUENCY:
	    get_simple((val_t *)&dpcur.frequency, &dpseen.frequency, INT);
	    break;
	case MAXDUMPS:
	    get_simple((val_t *)&dpcur.maxdumps, &dpseen.maxdumps, INT);
	    break;
	case STARTTIME:
	    get_time((val_t *)&dpcur.start_t, &dpseen.start_t);
	    break;
	case PROGRAM:
	    get_simple((val_t *)&dpcur.program, &dpseen.program, STRING);
	    if(strcmp(dpcur.program, "DUMP")
	       && strcmp(dpcur.program, "GNUTAR"))
		parserror("backup program \"%s\" unknown", dpcur.program);
	    break;
	case NL:
	    /* empty line */
	    break;
	case END:	/* end of file */
	    done = 1;
	default:
	    parserror("dump type parameter expected");
	}
	if(tok != NL) get_conftoken(NL);
    } while(!done);

    /* check results and save on dump list */
    if((p = lookup_dumptype(dpcur.name)) != NULL) {
	parserror("dumptype %s already defined on line %d", p->name, p->seen);
    }
    else if(dpcur.skip_incr + dpcur.skip_full + dpcur.no_full > 1)
	parserror("only one of SKIP-INCR, SKIP-FULL, NO-FULL allowed");
    else {
	/* save on list */
	p = alloc(sizeof(dumptype_t));
	*p = dpcur;
	p->next = dumplist;
	dumplist = p;
    }
}


static void init_dumpdefaults()
{
    dpcur.comment = "";
    dpcur.program = "DUMP";
    dpcur.priority = 1;
    dpcur.dumpcycle = conf_dumpcycle.i;
    dpcur.frequency = 1;
    dpcur.maxdumps = conf_maxdumps.i;
    dpcur.start_t = 0;

    dpcur.auth = AUTH_BSD;

    /* options */
    dpcur.compress_fast = dpcur.record = 1;
    dpcur.srvcompress = dpcur.compress_best = 0;
    dpcur.skip_incr = dpcur.skip_full = dpcur.no_full = dpcur.no_hold = 0;
    dpcur.kencrypt = 0;
    dpcur.index = 0;
    memset(&dpseen, 0, sizeof(dpseen));
}


static void get_tapetype()
{
    int done = 0;
    tapetype_t *p;

    init_tapedefaults();

    get_conftoken(IDENT);
    tpcur.name = stralloc(tokenval.s);
    tpcur.seen = line_num;

    get_conftoken(LBRACE);
    get_conftoken(NL);

    do {
	line_num += 1;
	get_conftoken(ANY);
	switch(tok) {

	case RBRACE:
	    done = 1;
	    break;
	case COMMENT:
	    get_simple((val_t *)&tpcur.comment, &tpseen.comment, STRING);
	    break;
	case LENGTH:
	    get_simple((val_t *)&tpcur.length, &tpseen.length, INT);
	    break;
	case FILEMARK:
	    get_simple((val_t *)&tpcur.filemark, &tpseen.filemark, INT);
	    break;
	case SPEED:
	    get_simple((val_t *)&tpcur.speed, &tpseen.speed, INT);
	    break;

	case NL:
	    /* empty line */
	    break;
	case END:	/* end of file */
	    done = 1;
	default:
	    parserror("tape type parameter expected");
	}
	if(tok != NL) get_conftoken(NL);
    } while(!done);

    /* check results and save on dump list */
    if((p = lookup_tapetype(tpcur.name)) != NULL) {
	free(tpcur.name);
	parserror("tapetype %s already defined on line %d", p->name, p->seen);
    }
    else {
	/* save on list */
	p = alloc(sizeof(tapetype_t));
	*p = tpcur;
	p->next = tapelist;
	tapelist = p;
    }
}


static void init_tapedefaults()
{
    tpcur.comment = "";
    tpcur.length = 2000 * 1024;
    tpcur.filemark = 1000;
    tpcur.speed = 200;
    memset(&tpseen, 0, sizeof(tpseen));
}


static void get_simple(var, seen, type)
val_t *var;
int *seen;
tok_t type;
{
    assert(type == STRING || type == IDENT || type == INT || type == REAL);
    
    ckseen(seen);

    if(type == STRING || type == IDENT) {
	get_conftoken(type);
	var->s = stralloc(tokenval.s);
    }
    else if(type == INT)
	var->i = get_number();
    else if(type == REAL) {
	get_conftoken(REAL);
	var->r = tokenval.r;
    }

    if(tok != NL) get_conftoken(NL);
}

static void get_time(var, seen)
val_t *var;
int *seen;
{
    time_t st = start_time.r.tv_sec;
    struct tm *stm;
    int hhmm = get_number();

    ckseen(seen);
    stm = localtime(&st);
    st -= stm->tm_sec + 60 * (stm->tm_min + 60 * stm->tm_hour);
    st += ((hhmm/100*60) + hhmm%100)*60;

    if (st-start_time.r.tv_sec<-43200)
	st += 86400;
    var->i = st;
    if(tok != NL) get_conftoken(NL);
}


/* ------------------------ */


int getconf_int(parm)
confparm_t parm;
{
    int r = 0;

    switch(parm) {

    case CNF_DUMPCYCLE: r = conf_dumpcycle.i; break;
    case CNF_TAPECYCLE: r = conf_tapecycle.i; break;
    case CNF_RUNTAPES: r = conf_runtapes.i; break;
    case CNF_DISKSIZE: r = conf_disksize.i; break;
    case CNF_BUMPSIZE: r = conf_bumpsize.i; break;
    case CNF_BUMPDAYS: r = conf_bumpdays.i; break;
    case CNF_NETUSAGE: r = conf_netusage.i; break;
    case CNF_INPARALLEL: r = conf_inparallel.i; break;
    case CNF_TIMEOUT: r = conf_timeout.i; break;
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
    case CNF_LABELSTR: r = conf_labelstr.s; break;
    case CNF_TAPELIST: r = conf_tapelist.s; break;
    case CNF_INFOFILE: r = conf_infofile.s; break;
    case CNF_LOGFILE: r = conf_logfile.s; break;
    case CNF_DISKFILE: r = conf_diskfile.s; break;
    case CNF_DISKDIR: r = conf_diskdir.s; break;
    case CNF_TAPETYPE: r = tapetype_id.s; break;
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


static void get_dumpopts()
{
    int done = 0;
    char efile[256];

    do {
	get_conftoken(ANY);
	switch(tok) {
	case COMPR_BEST: 
	    ckseen(&dpseen.compress);
	    dpcur.srvcompress = dpcur.compress_fast = 0;
	    dpcur.compress_best = 1; break;
	case COMPR:
	case COMPR_FAST: 
	    ckseen(&dpseen.compress);  
	    dpcur.compress_fast = 1;
	    dpcur.srvcompress = dpcur.compress_best = 0; break;
	case NO_COMPRESS:
	    ckseen(&dpseen.compress);
	    dpcur.srvcompress = dpcur.compress_fast =
	      dpcur.compress_best = 0; break;
	case SRVCOMPRESS:
	    ckseen(&dpseen.compress);
	    dpcur.srvcompress = 1;
	    dpcur.compress_fast = dpcur.compress_best = 0; break;
	case KRB4_AUTH:  ckseen(&dpseen.auth);  dpcur.auth = AUTH_KRB4;break;
	case BSD_AUTH:   ckseen(&dpseen.auth);   dpcur.auth = AUTH_BSD;break;
	case KENCRYPT:   ckseen(&dpseen.kencrypt);  dpcur.kencrypt = 1;break;
	case SKIP_INCR:  ckseen(&dpseen.skip_incr); dpcur.skip_incr= 1;break;
	case SKIP_FULL:  ckseen(&dpseen.skip_full); dpcur.skip_full= 1;break;
	case NO_FULL:    ckseen(&dpseen.no_full);   dpcur.no_full  = 1;break;
	case NO_HOLD:    ckseen(&dpseen.no_hold);   dpcur.no_hold  = 1;break;
	case NO_RECORD:  ckseen(&dpseen.record);    dpcur.record   = 0;break;
 	case IDENT:
		if(!strcmp("EXCLUDE-FILE", tokenval.s)){
		    get_conftoken(STRING);
		    sprintf(efile, "exclude-file=%s;", tokenval.s);
		    dpcur.exclude = stralloc(efile);
		}
		else if (!strcmp("EXCLUDE-LIST", tokenval.s)){
		    get_conftoken(STRING);
		    sprintf(efile, "exclude-list=%s;", tokenval.s);
		    dpcur.exclude = stralloc(efile);
		}
		else
		    parserror("dump option expected");
 		break;
  	case NL: done = 1; break;
	case INDEX:      ckseen(&dpseen.index);     dpcur.index    = 1;break;
	case COMMA: break;
	case END:
	    done = 1;
	default:
	    parserror("dump option expected");
	}
    } while(!done);
}


static void get_priority()
{
    int pri;

    ckseen(&dpseen.priority);

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
}


static void ckseen(seen)
int *seen;
{
    if(*seen) {
	parserror("duplicate parameter, prev def on line %d", *seen);
    }
    *seen = line_num;
}


static int get_number()
{
    int val;

    get_conftoken(ANY);
    if(tok == INT)
	val = tokenval.i;
    else if(tok == INFINITY)
	val = BIGINT;
    else {
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
    default:
	parserror("multiplier or eol expected");
	val = 0;
    }
    return val;
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

static struct keytab_s {
    char *keyword;
    tok_t token;
} keytable[] = {
    { "B", MULT1 },
    { "BSD-AUTH", BSD_AUTH },
    { "BUMPDAYS", BUMPDAYS },
    { "BUMPMULT", BUMPMULT },
    { "BUMPSIZE", BUMPSIZE },
    { "BYTE", MULT1 },
    { "BYTES", MULT1 },
    { "COMMENT", COMMENT },
    { "COMPRESS", COMPR },
    { "COMPRESS-BEST", COMPR_BEST },
    { "COMPRESS-FAST", COMPR_FAST },
    { "SRVCOMPRESS", SRVCOMPRESS },
    { "DAY", MULT1 },
    { "DAYS", MULT1 },
    { "DEFINE", DEFINE },
    { "DISKDIR", DISKDIR },
    { "DISKFILE", DISKFILE },
    { "DISKSIZE", DISKSIZE },
    { "DUMPCYCLE", DUMPCYCLE },
    { "DUMPTYPE", DUMPTYPE },
    { "DUMPUSER", DUMPUSER },
    { "FILEMARK", FILEMARK },
    { "FREQUENCY", FREQUENCY },
    { "HIGH", HIGH },
    { "INCLUDEFILE", INCLUDEFILE },
    { "INDEX", INDEX },
    { "INDEXDIR", INDEXDIR },
    { "INF", INFINITY },
    { "INFOFILE", INFOFILE },
    { "INPARALLEL", INPARALLEL },
    { "K", MULT1K },
    { "KB", MULT1K },
    { "KBYTES", MULT1K },
    { "KENCRYPT", KENCRYPT },
    { "KILOBYTES", MULT1K },
    { "KRB4-AUTH", KRB4_AUTH },
    { "LABELSTR", LABELSTR },
    { "LENGTH", LENGTH },
    { "LOGFILE", LOGFILE },
    { "LOW", LOW },
    { "M", MULT1M },
    { "MAILTO", MAILTO },
    { "MAXCYCLE", MAXCYCLE },
    { "MAXDUMPS", MAXDUMPS },
    { "MB", MULT1M },
    { "MBYTES", MULT1M },
    { "MEDIUM", MEDIUM },
    { "MEG", MULT1M },
    { "MEGABYTES", MULT1M },
    { "MINCYCLE", DUMPCYCLE },
    { "NETUSAGE", NETUSAGE },
    { "NO-COMPRESS", NO_COMPRESS },
    { "NO-FULL", NO_FULL },
    { "NO-HOLD", NO_HOLD },
    { "NO-RECORD", NO_RECORD },
    { "RUNTAPES", RUNTAPES },
    { "OPTIONS", OPTIONS },
    { "ORG", ORG  },
    { "PRIORITY", PRIORITY },
    { "PROGRAM", PROGRAM },
    { "SKIP-FULL", SKIP_FULL },
    { "SKIP-INCR", SKIP_INCR },
    { "SPEED", SPEED },
    { "STARTTIME", STARTTIME },
    { "TAPE", MULT1 },
    { "TAPECYCLE", TAPECYCLE },
    { "TAPEDEV", TAPEDEV },
    { "TAPELIST", TAPELIST },
    { "TAPES", MULT1 },
    { "TAPETYPE", TAPETYPE },
    { "TIMEOUT", TIMEOUT },
    { "TPCHANGER", TPCHANGER },
    { "WEEK", MULT7 },
    { "WEEKS", MULT7 },
    { NULL, IDENT }
};

static tok_t lookup_keyword(str)
char *str;
{
    struct keytab_s *kwp;

    /* switch to binary search if performance warrants */

    for(kwp = keytable; kwp->keyword != NULL; kwp++) {
	if(!strcmp(kwp->keyword, str)) break;
    }
    return kwp->token;
}

static char tkbuf[4096];

static void get_conftoken(exp)
tok_t exp;
{
    int ch, i, d;
    char *buf;

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

    if(exp != ANY && tok != exp) {
	char *str;
	struct keytab_s *kwp;

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
}

/* -------- */

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
    { "LABELSTR", CNF_LABELSTR, STRING },
    { "TAPELIST", CNF_TAPELIST, STRING },
    { "DISKFILE", CNF_DISKFILE, STRING },
    { "INFOFILE", CNF_INFOFILE, STRING },
    { "LOGFILE", CNF_LOGFILE, STRING },
    { "DISKDIR", CNF_DISKDIR, STRING },
    { "INDEXDIR", CNF_INDEXDIR, STRING },
    { "TAPETYPE", CNF_TAPETYPE, STRING },
    { "DUMPCYCLE", CNF_DUMPCYCLE, INT },
    { "MINCYCLE",  CNF_DUMPCYCLE, INT },
    { "RUNTAPES",   CNF_RUNTAPES, INT },
    { "TAPECYCLE", CNF_TAPECYCLE, INT },
    { "DISKSIZE", CNF_DISKSIZE, INT },
    { "BUMPDAYS", CNF_BUMPDAYS, INT },
    { "BUMPSIZE", CNF_BUMPSIZE, INT },
    { "BUMPMULT", CNF_BUMPMULT, REAL },
    { "NETUSAGE", CNF_NETUSAGE, INT },
    { "INPARALLEL", CNF_INPARALLEL, INT },
    { "TIMEOUT", CNF_TIMEOUT, INT },
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

    if(np->typ == INT) sprintf(tmpstr,"%d", getconf_int(np->parm));
    else if(np->typ == REAL) sprintf(tmpstr,"%f", getconf_real(np->parm));
    else strcpy(tmpstr, getconf_str(np->parm));

    return tmpstr;
}

/* -------- */

#ifdef TEST
dump_configuration()
{
    tapetype_t *tp;
    dumptype_t *dp;
    holdingdisk_t *hp;

    if(confname == NULL) {
	printf("NO AMANDA CONFIGURATION READ YET\n");
	return;
    }

    printf("AMANDA CONFIGURATION FROM FILE \"%s\":\n\n", confname);

    printf("conf_org = \"%s\"\n", getconf_str(CNF_ORG));
    printf("conf_mailto = \"%s\"\n", getconf_str(CNF_MAILTO));
    printf("conf_dumpuser = \"%s\"\n", getconf_str(CNF_DUMPUSER));
    printf("conf_tapedev = \"%s\"\n", getconf_str(CNF_TAPEDEV));
    printf("conf_tpchanger = \"%s\"\n", getconf_str(CNF_TPCHANGER));
    printf("conf_labelstr = \"%s\"\n", getconf_str(CNF_LABELSTR));
    printf("conf_tapelist = \"%s\"\n", getconf_str(CNF_TAPELIST));
    printf("conf_infofile = \"%s\"\n", getconf_str(CNF_INFOFILE));
    printf("conf_logfile = \"%s\"\n", getconf_str(CNF_LOGFILE));
    printf("conf_diskfile = \"%s\"\n", getconf_str(CNF_DISKFILE));
    printf("tapetype_id = \"%s\"\n", getconf_str(CNF_TAPETYPE));

    printf("conf_dumpcycle = %d\n", getconf_int(CNF_DUMPCYCLE));
    printf("conf_runtapes = %d\n", getconf_int(CNF_RUNTAPES));
    printf("conf_tapecycle = %d\n", getconf_int(CNF_TAPECYCLE));
    printf("conf_bumpsize = %d\n", getconf_int(CNF_BUMPSIZE));
    printf("conf_bumpdays = %d\n", getconf_int(CNF_BUMPDAYS));
    printf("conf_bumpmult = %f\n", getconf_real(CNF_BUMPMULT));
    printf("conf_netusage = %d\n", getconf_int(CNF_NETUSAGE));
    printf("conf_inparallel = %d\n", getconf_int(CNF_INPARALLEL));
    printf("conf_timeout = %d\n", getconf_int(CNF_TIMEOUT));
    printf("conf_maxdumps = %d\n", getconf_int(CNF_MAXDUMPS));

    printf("conf_diskdir = \"%s\"\n", getconf_str(CNF_DISKDIR));
    printf("conf_disksize = %d\n", getconf_int(CNF_DISKSIZE));
    printf("conf_indexdir = \"%s\"\n", getconf_str(CNF_INDEXDIR));
    printf("num_holdingdisks = %d\n", num_holdingdisks);
    for(hp = holdingdisks; hp != NULL; hp = hp->next)
	printf("  holddisk: dir \"%s\" size %d\n", hp->diskdir, hp->disksize);

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
	printf("	FREQUENCY %d\n", dp->frequency);
	printf("	MAXDUMPS %d\n", dp->maxdumps);
	printf("	OPTIONS: ");
	if(dp->srvcompress) printf("SRVCOMPRESS ");
	else if(!dp->compress_fast && !dp->compress_best)
	    printf("NO-COMPRESS ");
	else if(dp->compress_best) printf("COMPRESS-BEST ");
	else printf("COMPRESS-FAST ");
	if(!dp->record) printf("NO-");
	printf("RECORD");
	if(dp->auth == AUTH_BSD) printf(" BSD-AUTH");
	else if(dp->auth == AUTH_KRB4) printf(" KRB4-AUTH");
	else printf(" UNKNOWN-AUTH");
	if(dp->skip_incr) printf(" SKIP-INCR");
	if(dp->skip_full) printf(" SKIP-FULL");
	if(dp->no_full) printf(" NO-FULL");
	if(dp->no_hold) printf(" NO-HOLD");
	if(dp->kencrypt) printf(" KENCRYPT");
	if(dp->index) printf(" INDEX");
	putchar('\n');
    }
}
#endif /* TEST */
