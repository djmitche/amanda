/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998, 2000 University of Maryland at College Park
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
 * $Id: reporter.c,v 1.44.2.16 2000/09/26 00:18:41 martinea Exp $
 *
 * nightly Amanda Report generator
 */
/*
report format
    tape label message
    error messages
    summary stats
    details for errors
    notes
    success summary
*/

#include "amanda.h"
#include "conffile.h"
#include "tapefile.h"
#include "diskfile.h"
#include "infofile.h"
#include "logfile.h"
#include "version.h"

/* don't have (or need) a skipped type except internally to reporter */
#define L_SKIPPED	L_MARKER

typedef struct line_s {
    struct line_s *next;
    char *str;
} line_t;

#define	MAX_TAPER 31

typedef struct repdata_s {
    logtype_t result;
    int nb_taper;
    struct timedata_s {
	int success;
	float origsize, outsize;
	int datestamp;
	float sec, kps;
	int level, filenum;
    } taper[MAX_TAPER], dumper;
} repdata_t;

#define data(dp) ((repdata_t *)(dp)->up)

struct cumulative_stats {
    int dumpdisks, tapedisks;
    double taper_time, dumper_time;
    double outsize, origsize, tapesize;
    double coutsize, corigsize;			/* compressed dump only */
} stats[3];

int dumpdisks[10], tapedisks[10];	/* by-level breakdown of disk count */

float total_time, startup_time;

/* count files to tape */
int tapefcount = 0;

char *datestamp;
char *tape_labels = NULL;
int last_run_tapes = 0;
static int degraded_mode = 0; /* defined in driverio too */
int normal_run = 0;
int amflush_run = 0;
int got_finish = 0;

char *config_name = NULL;
char *config_dir = NULL;

char *tapestart_error = NULL;

FILE *logfile, *mailf;

FILE *postscript;
char *printer;

disklist_t *diskq;
disklist_t sortq;

line_t *errsum = NULL;
line_t *errdet = NULL;
line_t *notes = NULL;

char *hostname = NULL, *diskname = NULL;

/* local functions */
int contline_next P((void));
void addline P((line_t **lp, char *str));
void usage P((void));
int main P((int argc, char **argv));

void copy_template_file P((char *lbl_templ));
void setup_data P((void));
void handle_start P((void));
void handle_finish P((void));
void handle_note P((void));
void handle_summary P((void));
void handle_stats P((void));
void handle_error P((void));
void handle_success P((void));
void handle_strange P((void));
void handle_failed P((void));
void generate_missing P((void));
void output_tapeinfo P((void));
void output_lines P((line_t *lp, FILE *f));
void output_stats P((void));
void output_summary P((void));
void sort_disks P((void));
int sort_by_time P((disk_t *a, disk_t *b));
int sort_by_name P((disk_t *a, disk_t *b));
void bogus_line P((void));
char *nicedate P((int datestamp));
void setup_disk P((disk_t *dp));
static char *prefix P((char *host, char *disk, int level));

/* enumeration of our reporter columns */
typedef enum {
    HostName,
    Disk,
    Level,
    OrigKB,
    OutKB,
    Compress,
    DumpTime,
    DumpRate,
    TapeTime,
    TapeRate,
    ColumnNameCount
} ColumnName;

/* the corresponding strings for the above enumeration
 */
static char *ColumnNameStrings[ColumnNameCount] = {
    "HostName",
    "Disk",
    "Level",
    "OrigKB",
    "OutKB",
    "Compress",
    "DumpTime",
    "DumpRate",
    "TapeTime",
    "TapeRate"
};

/* conversion from string to enumeration
 */
static int StringToColumnName(char *s) {
    ColumnName cn;
    for (cn= 0; cn<ColumnNameCount; cn++) {
    	if (strcmp(s, ColumnNameStrings[cn]) == 0) {
	    return cn;
	}
    }
    return -1;
}

char LastChar(char *s) {
    return s[strlen(s)-1];
}

/* for each column we define some values on how to
 * format this column element
 */
typedef struct {
    char PrefixSpace;	/* the blank space to print before this
   			 * column. It is used to get the space
			 * between the colums
			 */
    char Width;		/* the widht of the column itself */
    char Precision;	/* the precision if its a float */
    char MaxWidth;	/* if set, Width will be recalculated
    			 * to the space needed */
    char *Format;	/* the printf format string for this
   			 * column element
			 */
    char *Title;	/* the title to use for this column */
} ColumnInfo;

/* this corresponds to the normal output of amanda, but may
 * be adapted to any spacing as you like.
 */
ColumnInfo ColumnData[ColumnNameCount] = {
    /*HostName */	{  0, 12, 12, 0, "%-*.*s", "HOSTNAME" },
    /*Disk     */	{  1, 11, 11, 0, "%-*.*s", "DISK" },
    /*Level    */	{  1, 1,  1,  0, "%*.*d",  "L" },
    /*OrigKB   */	{  1, 7,  0,  0, "%*.*f",  "ORIG-KB" },
    /*OutKB    */	{  0, 7,  0,  0, "%*.*f",  "OUT-KB" },
    /*Compress */	{  0, 6,  1,  0, "%*.*f",  "COMP%" },
    /*DumpTime */	{  0, 7,  7,  0, "%*.*s",  "MMM:SS" },
    /*DumpRate */       {  0, 6,  1,  0, "%*.*f",  "KB/s" },
    /*TapeTime */	{  1, 6,  6,  0, "%*.*s",  "MMM:SS" },
    /*TapeRate */	{  0, 6,  1,  0, "%*.*f",  "KB/s" }
};
static char *ColumnSpec="";		/* filled from config */
static char MaxWidthsRequested=0;	/* determined via config data */

static int SetColumDataFromString(ColumnInfo* ci, char *s) {
    /* Convert from a Columspec string to our internal format
     * of columspec. The purpose is to provide this string
     * as configuration paramter in the amanda.conf file or
     * (maybe) as environment variable.
     * 
     * This text should go as comment into the sample amanda.conf
     *
     * The format for such a ColumnSpec string s is a ',' seperated
     * list of triples. Each triple consists of
     *   -the name of the column (as in ColumnNameStrings)
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
    static char *myname= "SetColumDataFromString";

    while (s && *s) {
	int Space, Width;
	ColumnName cn;
    	char *eon= strchr(s, '=');

	if (eon == NULL) {
	    fprintf(stderr, "%s: invalid columnspec: %s\n", myname, s);
	    return -1;
	}
	*eon= '\0';
	if ((cn=StringToColumnName(s)) < 0) {
	    fprintf(stderr, "%s: invalid ColumnName: %s\n", myname, s);
	    return -1;
	}
	if (sscanf(eon+1, "%d:%d", &Space, &Width) != 2) {
	    fprintf(stderr, "%s: invalid format: %s\n", myname, eon+1);
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

static int ColWidth(ColumnName From, ColumnName To) {
    int i, Width= 0;
    for (i=From; i<=To; i++)
    	Width+= ColumnData[i].PrefixSpace + ColumnData[i].Width;
    return Width;
}

static char *Rule(ColumnName From, ColumnName To) {
    int i, ThisLeng;
    int Leng= ColWidth(0, ColumnNameCount-1);
    char *RuleSpace= alloc(Leng+1);
    ThisLeng= ColWidth(From, To);
    for (i=0;i<ColumnData[From].PrefixSpace; i++)
    	RuleSpace[i]= ' ';
    for (; i<ThisLeng; i++)
    	RuleSpace[i]= '-';
    RuleSpace[ThisLeng]= '\0';
    return RuleSpace;
}

static char *TextRule(ColumnName From, ColumnName To, char *s) {
    ColumnInfo *cd= &ColumnData[From];
    int leng, nbrules, i, txtlength;
    int RuleSpaceSize= ColWidth(0, ColumnNameCount-1)+1;
    char *RuleSpace= alloc(RuleSpaceSize), *tmp;

    leng= strlen(s);
    if(leng >= (RuleSpaceSize - cd->PrefixSpace))
	leng = RuleSpaceSize - cd->PrefixSpace - 1;
    ap_snprintf(RuleSpace, RuleSpaceSize, "%*s%*.*s ", cd->PrefixSpace, "", 
		leng, leng, s);
    txtlength = cd->PrefixSpace + leng + 1;
    nbrules = ColWidth(From,To) - txtlength;
    for(tmp=RuleSpace + txtlength, i=nbrules ; i>0; tmp++,i--)
	*tmp='-';
    *tmp = '\0';
    return RuleSpace;
}

char *sDivZero(float a, float b, ColumnName cn) {
    ColumnInfo *cd= &ColumnData[cn];
    static char PrtBuf[256];
    if (b == 0.0)
    	ap_snprintf(PrtBuf, sizeof(PrtBuf),
	  "%*s", cd->Width, "-- ");
    else
    	ap_snprintf(PrtBuf, sizeof(PrtBuf),
	  cd->Format, cd->Width, cd->Precision, a/b);
    return PrtBuf;
}



int contline_next()
{
    int ch = getc(logfile);
    ungetc(ch, logfile);

    return ch == ' ';
}

void addline(lp, str)
line_t **lp;
char *str;
{
    line_t *new, *p, *q;

    /* allocate new line node */
    new = (line_t *) alloc(sizeof(line_t));
    new->next = NULL;
    new->str = stralloc(str);

    /* add to end of list */
    for(p = *lp, q = NULL; p != NULL; q = p, p = p->next);
    if(q == NULL) *lp = new;
    else q->next = new;
}

void usage()
{
    error("Usage: amreport conf [-f output-file] [-l logfile] [-p postscript-file]");
}

int main(argc, argv)
int argc;
char **argv;
{
    char *conffile;
    char *conf_diskfile;
    char *conf_tapelist;
    char *conf_infofile;
    char *logfname, *psfname, *outfname, *subj_str = NULL;
    tapetype_t *tp;
    int fd, opt;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    char *mail_cmd = NULL, *printer_cmd = NULL;
    extern int optind;
    char my_cwd[STR_SIZE];

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    set_pname("amreport");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    /* Process options */
    
    erroutput_type = ERR_INTERACTIVE;
    outfname = NULL;
    psfname = NULL;
    logfname = NULL;

    if (getcwd(my_cwd, sizeof(my_cwd)) == NULL) {
	error("cannot determine current working directory");
    }

    if (argc < 2) {
	config_dir = stralloc2(my_cwd, "/");
	if ((config_name = strrchr(my_cwd, '/')) != NULL) {
	    config_name = stralloc(config_name + 1);
	}
    } else {
	if (argv[1][0] == '-') {
	    usage();
	    return 1;
	}
	config_name = stralloc(argv[1]);
	config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
	--argc; ++argv;
	while((opt = getopt(argc, argv, "f:l:p:")) != EOF) {
	    switch(opt) {
            case 'f':
		if (outfname != NULL) {
		    error("you may specify at most one -f");
		}
		if (*optarg == '/') {
                    outfname = stralloc(optarg);
		} else {
                    outfname = vstralloc(my_cwd, "/", optarg, NULL);
		}
                break;
            case 'l':
		if (logfname != NULL) {
		    error("you may specify at most one -l");
		}
		if (*optarg == '/') {
		    logfname = stralloc(optarg);
		} else {
                    logfname = vstralloc(my_cwd, "/", optarg, NULL);
		}
                break;
            case 'p':
		if (psfname != NULL) {
		    error("you may specify at most one -p");
		}
		if (*optarg == '/') {
                    psfname = stralloc(optarg);
		} else {
                    psfname = stralloc2(my_cwd, optarg);
		}
                break;
            case '?':
            default:
		usage();
		return 1;
	    }
	}

	argc -= optind;
	argv += optind;

	if (argc > 1) {
	    usage();
	    return 1;
	}
    }

    safe_cd();

    /* read configuration files */

    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if(read_conffile(conffile)) {
        error("could not read config file \"%s\"", conffile);
    }
    amfree(conffile);
    conf_diskfile = getconf_str(CNF_DISKFILE);
    if (*conf_diskfile == '/') {
	conf_diskfile = stralloc(conf_diskfile);
    } else {
	conf_diskfile = stralloc2(config_dir, conf_diskfile);
    }
    if((diskq = read_diskfile(conf_diskfile)) == NULL) {
	error("could not load disklist \"%s\"", conf_diskfile);
    }
    amfree(conf_diskfile);
    conf_tapelist = getconf_str(CNF_TAPELIST);
    if (*conf_tapelist == '/') {
	conf_tapelist = stralloc(conf_tapelist);
    } else {
	conf_tapelist = stralloc2(config_dir, conf_tapelist);
    }
    if(read_tapelist(conf_tapelist)) {
	error("could not read tapelist \"%s\"", conf_tapelist);
    }
    amfree(conf_tapelist);
    conf_infofile = getconf_str(CNF_INFOFILE);
    if (*conf_infofile == '/') {
	conf_infofile = stralloc(conf_infofile);
    } else {
	conf_infofile = stralloc2(config_dir, conf_infofile);
    }
    if(open_infofile(conf_infofile)) {
	error("could not open info db \"%s\"", conf_infofile);
    }
    amfree(conf_infofile);

    ColumnSpec = getconf_str(CNF_COLUMNSPEC);
    if(SetColumDataFromString(ColumnData, ColumnSpec) < 0) {
        error("wrong column specification");
    } else {
    	int cn;

    	for (cn = 0; cn < ColumnNameCount; cn++) {
	    if (ColumnData[cn].MaxWidth) {
	    	MaxWidthsRequested = 1;
		break;
	    }
	}
    }

    if(!logfname) {
	char *conf_logdir;

	conf_logdir = getconf_str(CNF_LOGDIR);
	if (*conf_logdir == '/') {
	    conf_logdir = stralloc(conf_logdir);
	} else {
	    conf_logdir = stralloc2(config_dir, conf_logdir);
	}
	logfname = vstralloc(conf_logdir, "/", "log", NULL);
	amfree(conf_logdir);
    }

    if((logfile = fopen(logfname, "r")) == NULL) {
	error("could not open log %s: %s", logfname, strerror(errno));
    }

    setup_data();    /* setup per-disk data */

    while(get_logline(logfile)) {
	switch(curlog) {
	case L_START: handle_start(); break;
	case L_FINISH: handle_finish(); break;

	case L_INFO: handle_note(); break;
	case L_WARNING: handle_note(); break;

	case L_SUMMARY: handle_summary(); break;
	case L_STATS: handle_stats(); break;

	case L_ERROR: handle_error(); break;
	case L_FATAL: handle_error(); break;

	case L_SUCCESS: handle_success(); break;
	case L_STRANGE: handle_strange(); break;
	case L_FAIL:    handle_failed(); break;

	default:
	    printf("reporter: unexpected log line\n");
	}
    }
    afclose(logfile);
    close_infofile();
    if(!amflush_run)
	generate_missing();

    subj_str = vstralloc(getconf_str(CNF_ORG),
			 " ", amflush_run ? "AMFLUSH" : "AMANDA",
			 " ", "MAIL REPORT FOR",
			 " ", nicedate(datestamp ? atoi(datestamp) : 0),
			 NULL);
	
    /* lookup the tapetype and printer type from the amanda.conf file. */
    tp = lookup_tapetype(getconf_str(CNF_TAPETYPE));
    printer = getconf_str(CNF_PRINTER);

    /* ignore SIGPIPE so if a child process dies we do not also go away */
    signal(SIGPIPE, SIG_IGN);

    /* open pipe to mailer */

    if(outfname) {
	/* output to a file */
	if((mailf = fopen(outfname,"w")) == NULL) {
	    error("could not open output file: %s %s", outfname, strerror(errno));
	}
	fprintf(mailf, "To: %s\n", getconf_str(CNF_MAILTO));
	fprintf(mailf, "Subject: %s\n\n", subj_str);

    } else {
	mail_cmd = vstralloc(MAILER,
			     " -s", " \"", subj_str, "\"",
			     " ", getconf_str(CNF_MAILTO),
			     NULL);
	if((mailf = popen(mail_cmd, "w")) == NULL)
	    error("could not open pipe to \"%s\": %s",
		  mail_cmd, strerror(errno));

    }

    /* open pipe to print spooler if necessary) */

    if(psfname) {
	/* if the postscript_label_template (tp->lbl_templ) field is not */
	/* the empty string (i.e. it is set to something), open the      */
	/* postscript debugging file for writing.                        */
	if ((strcmp(tp->lbl_templ,"")) != 0) {
	    if ((postscript = fopen(psfname,"w")) == NULL) {
		error("could not open %s: %s", psfname, strerror(errno));
	    }
	}
    } else {
#ifdef LPRCMD
	if (strcmp(printer,"") != 0)	/* alternate printer is defined */
	    /* print to the specified printer */
#ifdef LPRFLAG
	    printer_cmd = vstralloc(LPRCMD, " ", LPRFLAG, printer, NULL);
#else
	    printer_cmd = vstralloc(LPRCMD, NULL);
#endif
	else
	    /* print to the default printer */
	    printer_cmd = vstralloc(LPRCMD, NULL);
#endif

	if ((strcmp(tp->lbl_templ,"")) != 0)
#ifdef LPRCMD
	    if ((postscript = popen(printer_cmd,"w")) == NULL)
		error("could not open pipe to \"%s\": %s",
		      printer_cmd, strerror(errno));
#else
	    error("no printer command defined");
#endif
    }

    amfree(subj_str);

    if (postscript) {
      copy_template_file(tp->lbl_templ);
      /* generate a few elements */
      fprintf(postscript,"(%s) DrawDate\n\n",
	      nicedate(datestamp ? atoi(datestamp) : 0));
      fprintf(postscript,"(Amanda Version %s) DrawVers\n",version());
      fprintf(postscript,"(%s) DrawTitle\n",tape_labels ? tape_labels : "");
    }

    if(!got_finish) fputs("*** THE DUMPS DID NOT FINISH PROPERLY!\n\n", mailf);

    output_tapeinfo();

    if(errsum) {
	fprintf(mailf,"\nFAILURE AND STRANGE DUMP SUMMARY:\n");
	output_lines(errsum, mailf);
    }
    fputs("\n\n", mailf);

    output_stats();

    if(errdet) {
	fprintf(mailf,"\n\014\nFAILED AND STRANGE DUMP DETAILS:\n");
	output_lines(errdet, mailf);
    }
    if(notes) {
	fprintf(mailf,"\n\014\nNOTES:\n");
	output_lines(notes, mailf);
    }
    sort_disks();
    if(sortq.head != NULL) {
	fprintf(mailf,"\n\014\nDUMP SUMMARY:\n");
	output_summary();
    }
    fprintf(mailf,"\n(brought to you by Amanda version %s)\n",
	    version());

    if (postscript) fprintf(postscript,"\nshowpage\n");


    /* close postscript file */
    if (psfname && postscript) {
    	/* it may be that postscript is NOT opened */
	afclose(postscript);
    }
    else {
	if (postscript != NULL && pclose(postscript) != 0)
	    error("printer command failed: %s", printer_cmd);
	postscript = NULL;
    }

    /* close output file */
    if(outfname) {
        afclose(mailf);
    }
    else {
        if(pclose(mailf) != 0)
            error("mail command failed: %s", mail_cmd);
        mailf = NULL;
    }

    amfree(hostname);
    amfree(diskname);
    amfree(datestamp);
    amfree(tape_labels);
    amfree(config_dir);
    amfree(config_name);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    return 0;
}

/* ----- */

#define mb(f)	((f)/1024.0)		/* kbytes -> mbytes */
#define pct(f)	((f)*100.0)		/* percent */
#define hrmn(f) ((int)(f)+30)/3600, (((int)(f)+30)%3600)/60
#define mnsc(f) ((int)(f+0.5))/60, ((int)(f+0.5)) % 60

#define divzero(fp,a,b)	        	    \
    do {       	       	       	       	    \
	double q = (b);			    \
	if (q == 0.0)			    \
	    fprintf((fp),"  -- ");	    \
	else if ((q = (a)/q) >= 999.95)	    \
	    fprintf((fp), "###.#");	    \
	else				    \
	    fprintf((fp), "%5.1f",q);	    \
    } while(0)
#define divzero_wide(fp,a,b)	       	    \
    do {       	       	       	       	    \
	double q = (b);			    \
	if (q == 0.0)			    \
	    fprintf((fp),"    -- ");	    \
	else if ((q = (a)/q) >= 99999.95)   \
	    fprintf((fp), "#####.#");	    \
	else				    \
	    fprintf((fp), "%7.1f",q);	    \
    } while(0)

void output_stats()
{
    double idle_time;
    tapetype_t *tp = lookup_tapetype(getconf_str(CNF_TAPETYPE));
    int tapesize, marksize, lv, first;

    tapesize = tp->length;
    marksize = tp->filemark;

    stats[2].dumpdisks   = stats[0].dumpdisks   + stats[1].dumpdisks;
    stats[2].tapedisks   = stats[0].tapedisks   + stats[1].tapedisks;
    stats[2].outsize     = stats[0].outsize     + stats[1].outsize;
    stats[2].origsize    = stats[0].origsize    + stats[1].origsize;
    stats[2].tapesize    = stats[0].tapesize    + stats[1].tapesize;
    stats[2].coutsize    = stats[0].coutsize    + stats[1].coutsize;
    stats[2].corigsize   = stats[0].corigsize   + stats[1].corigsize;
    stats[2].taper_time  = stats[0].taper_time  + stats[1].taper_time;
    stats[2].dumper_time = stats[0].dumper_time + stats[1].dumper_time;

    if(!got_finish)	/* no driver finish line, estimate total run time */
	total_time = stats[2].taper_time + startup_time;

    idle_time = (total_time - startup_time) - stats[2].taper_time;
    if(idle_time < 0) idle_time = 0.0;

    fprintf(mailf,"STATISTICS:\n");
    fprintf(mailf,
	    "                          Total       Full      Daily\n");
    fprintf(mailf,
	    "                        --------   --------   --------\n");

    fprintf(mailf,
	    "Estimate Time (hrs:min)   %2d:%02d\n", hrmn(startup_time));

    fprintf(mailf,
	    "Run Time (hrs:min)        %2d:%02d\n", hrmn(total_time));

    fprintf(mailf,
	    "Dump Time (hrs:min)       %2d:%02d      %2d:%02d      %2d:%02d\n",
	    hrmn(stats[2].dumper_time), hrmn(stats[0].dumper_time),
	    hrmn(stats[1].dumper_time));

    fprintf(mailf,
	    "Output Size (meg)       %7.1f    %7.1f    %7.1f\n",
	    mb(stats[2].outsize), mb(stats[0].outsize), mb(stats[1].outsize));

    fprintf(mailf,
	    "Original Size (meg)     %7.1f    %7.1f    %7.1f\n",
	    mb(stats[2].origsize), mb(stats[0].origsize),
	    mb(stats[1].origsize));

    fprintf(mailf, "Avg Compressed Size (%%)   ");
    divzero(mailf, pct(stats[2].coutsize),stats[2].corigsize);
    fputs("      ", mailf);
    divzero(mailf, pct(stats[0].coutsize),stats[0].corigsize);
    fputs("      ", mailf);
    divzero(mailf, pct(stats[1].coutsize),stats[1].corigsize);
/*    putc('\n', mailf);*/

    if(stats[1].dumpdisks > 0) fputs("   (level:#disks ...)", mailf);
    putc('\n', mailf);

    fprintf(mailf,
	    "Filesystems Dumped         %4d       %4d       %4d",
	    stats[2].dumpdisks, stats[0].dumpdisks, stats[1].dumpdisks);

    if(stats[1].dumpdisks > 0) {
	first = 1;
	for(lv = 1; lv < 10; lv++) if(dumpdisks[lv]) {
	    fputs(first?"   (":" ", mailf);
	    first = 0;
	    fprintf(mailf, "%d:%d", lv, dumpdisks[lv]);
	}
	putc(')', mailf);
    }
    putc('\n', mailf);

    fprintf(mailf, "Avg Dump Rate (k/s)     ");
    divzero_wide(mailf, stats[2].outsize,stats[2].dumper_time);
    fputs("    ", mailf);
    divzero_wide(mailf, stats[0].outsize,stats[0].dumper_time);
    fputs("    ", mailf);
    divzero_wide(mailf, stats[1].outsize,stats[1].dumper_time);
    putc('\n', mailf);

    putc('\n', mailf);
    fprintf(mailf,
	    "Tape Time (hrs:min)       %2d:%02d      %2d:%02d      %2d:%02d\n",
	    hrmn(stats[2].taper_time), hrmn(stats[0].taper_time),
	    hrmn(stats[1].taper_time));

    fprintf(mailf,
	    "Tape Size (meg)         %7.1f    %7.1f    %7.1f\n",
	    mb(stats[2].tapesize), mb(stats[0].tapesize),
	    mb(stats[1].tapesize));

    fprintf(mailf, "Tape Used (%%)             ");
    divzero(mailf, pct(stats[2].tapesize+marksize*stats[2].tapedisks),tapesize);
    fputs("      ", mailf);
    divzero(mailf, pct(stats[0].tapesize+marksize*stats[0].tapedisks),tapesize);
    fputs("      ", mailf);
    divzero(mailf, pct(stats[1].tapesize+marksize*stats[1].tapedisks),tapesize);

    if(stats[1].tapedisks > 0) fputs("   (level:#disks ...)", mailf);
    putc('\n', mailf);

    fprintf(mailf,
	    "Filesystems Taped          %4d       %4d       %4d",
	    stats[2].tapedisks, stats[0].tapedisks, stats[1].tapedisks);

    if(stats[1].tapedisks > 0) {
	first = 1;
	for(lv = 1; lv < 10; lv++) if(tapedisks[lv]) {
	    fputs(first?"   (":" ", mailf);
	    first = 0;
	    fprintf(mailf, "%d:%d", lv, tapedisks[lv]);
	}
	putc(')', mailf);
    }
    putc('\n', mailf);

    fprintf(mailf, "Avg Tp Write Rate (k/s) ");
    divzero_wide(mailf, stats[2].tapesize,stats[2].taper_time);
    fputs("    ", mailf);
    divzero_wide(mailf, stats[0].tapesize,stats[0].taper_time);
    fputs("    ", mailf);
    divzero_wide(mailf, stats[1].tapesize,stats[1].taper_time);
    putc('\n', mailf);

    if (postscript) {
      fprintf(postscript, "(Total Size:        %6.1f MB) DrawStat\n",
	      mb(stats[2].outsize));
      fprintf(postscript, "(Tape Used (%%)       ");
      divzero(postscript, pct(stats[2].tapesize+marksize*stats[2].tapedisks),
	      tapesize);
      fprintf(postscript," %%) DrawStat\n");
      fprintf(postscript, "(Compression Ratio:  ");
      divzero(postscript, pct(stats[2].coutsize),stats[2].corigsize);
      fprintf(postscript," %%) DrawStat\n");
      fprintf(postscript,"(Filesystems Dumped: %4d) DrawStat\n",
	      stats[2].dumpdisks);
    }
}

/* ----- */

void output_tapeinfo()
{
    tape_t *tp;
    int run_tapes;
    int skip = 0;

    if (last_run_tapes > 0) {
	if(amflush_run)
	    fprintf(mailf, "The dumps were flushed to tape%s %s.\n",
		    last_run_tapes == 1 ? "" : "s",
		    tape_labels ? tape_labels : "");
	else
	    fprintf(mailf, "These dumps were to tape%s %s.\n",
		    last_run_tapes == 1 ? "" : "s",
		    tape_labels ? tape_labels : "");
    }

    if(degraded_mode) {
	fprintf(mailf,
		"*** A TAPE ERROR OCCURRED: %s.\n", tapestart_error);
	fputs("Some dumps may have been left in the holding disk.\n", mailf);
	fprintf(mailf,
		"Run amflush%s to flush them to tape.\n",
		amflush_run ? " again" : "");
    }

    tp = lookup_last_reusable_tape(skip);

    run_tapes = getconf_int(CNF_RUNTAPES);

    if (run_tapes <= 1)
	fputs("The next tape Amanda expects to use is: ", mailf);
    else
	fprintf(mailf, "The next %d tapes Amanda expects to used are: ",
		run_tapes);
    
    while(run_tapes > 0) {
	if(tp != NULL)
	    fprintf(mailf, "%s", tp->label);
	else
	    fputs("a new tape", mailf);

	if(run_tapes > 1) fputs(", ", mailf);

	run_tapes -= 1;
	skip++;
	tp = lookup_last_reusable_tape(skip);
    }
    fputs(".\n", mailf);
}

/* ----- */

void output_lines(lp, f)
line_t *lp;
FILE *f;
{
    line_t *next;

    while(lp) {
	fputs(lp->str, f);
	amfree(lp->str);
	fputc('\n', f);
	next = lp->next;
	amfree(lp);
	lp = next;
    }
}

/* ----- */

int sort_by_time(a, b)
disk_t *a, *b;
{
    return data(b)->dumper.sec - data(a)->dumper.sec;
}

int sort_by_name(a, b)
disk_t *a, *b;
{
    int rc;

    rc = strcmp(a->host->hostname, b->host->hostname);
    if(rc == 0) rc = strcmp(a->name, b->name);
    return rc;
}

void sort_disks()
{
    disk_t *dp;

    sortq.head = sortq.tail = NULL;
    while(!empty(*diskq)) {
	dp = dequeue_disk(diskq);
	if(data(dp) != NULL)
	    insert_disk(&sortq, dp, sort_by_name);
    }
}

void CheckStringMax(ColumnInfo *cd, char *s) {
    if (cd->MaxWidth) {
	int l= strlen(s);
	if (cd->Width < l)
	    cd->Width= l;
    }
}

void CheckIntMax(ColumnInfo *cd, int n) {
    if (cd->MaxWidth) {
    	char testBuf[200];
    	int l;
	ap_snprintf(testBuf, sizeof(testBuf),
	  cd->Format, cd->Width, cd->Precision, n);
	l= strlen(testBuf);
	if (cd->Width < l)
	    cd->Width= l;
    }
}

void CheckFloatMax(ColumnInfo *cd, double d) {
    if (cd->MaxWidth) {
    	char testBuf[200];
	int l;
	ap_snprintf(testBuf, sizeof(testBuf),
	  cd->Format, cd->Width, cd->Precision, d);
	l= strlen(testBuf);
	if (cd->Width < l)
	    cd->Width= l;
    }
}

void CalcMaxWidth() {
    /* we have to look for columspec's, that require the recalculation.
     * we do here the same loops over the sortq as is done in
     * output_summary. So, if anything is changed there, we have to
     * change this here also.
     *							ElB, 1999-02-24.
     */
    disk_t *dp;
    float f;

    for(dp = sortq.head; dp != NULL; dp = dp->next) {
	int i;
	for (i=0; i<data(dp)->nb_taper; i++) {
	    ColumnInfo *cd;
	    char TimeRateBuffer[40];

	    CheckStringMax(&ColumnData[HostName], dp->host->hostname);
	    CheckStringMax(&ColumnData[Disk], dp->name);
	    if (data(dp)->result == L_BOGUS)
		continue;
	    CheckIntMax(&ColumnData[Level], data(dp)->taper[i].level);
	    CheckIntMax(&ColumnData[Level], data(dp)->dumper.level);
	    if (data(dp)->result == L_SKIPPED)
		continue;
	    if (data(dp)->result == L_FAIL);
		continue;
	    if(!amflush_run || i == data(dp)->nb_taper-1) {
		CheckFloatMax(&ColumnData[OrigKB], data(dp)->dumper.origsize);
		CheckFloatMax(&ColumnData[OutKB], data(dp)->dumper.outsize);
	    }
	    if(dp->compress == COMP_NONE)
		f = 0.0;
	    else 
		f = data(dp)->dumper.origsize;
	    CheckStringMax(&ColumnData[Disk], 
	      sDivZero(pct(data(dp)->dumper.outsize), f, Compress));

	    if(!amflush_run)
		ap_snprintf(TimeRateBuffer, sizeof(TimeRateBuffer),
		  "%3d:%02d", mnsc(data(dp)->dumper.sec));
	    else
		ap_snprintf(TimeRateBuffer, sizeof(TimeRateBuffer),
		  "N/A ");
	    CheckStringMax(&ColumnData[DumpTime], TimeRateBuffer);

	    CheckFloatMax(&ColumnData[DumpRate], data(dp)->dumper.kps); 

	    cd= &ColumnData[TapeTime];
	    if(!data(dp)->taper[i].success && !degraded_mode) {
		CheckStringMax(cd, "FAILED");
		continue;
	    }
	    if(data(dp)->taper[i].success)
		ap_snprintf(TimeRateBuffer, sizeof(TimeRateBuffer), 
		  "%3d:%02d", mnsc(data(dp)->taper[i].sec));
	    else
		ap_snprintf(TimeRateBuffer, sizeof(TimeRateBuffer),
		  "N/A ");
	    CheckStringMax(cd, TimeRateBuffer);

	    cd= &ColumnData[TapeRate];
	    if(data(dp)->taper[i].success)
		CheckFloatMax(cd, data(dp)->taper[i].kps);
	    else
		CheckStringMax(cd, "N/A ");
	}
    }
}

void output_summary()
{
    disk_t *dp;
    char *ds="DUMPER STATS";
    char *ts=" TAPER STATS";
    char *tmp;

    int i, h, w1, wDump, wTape;
    float outsize, origsize;
    float f;

    /* at first determine if we have recalculate our widths */
    if (MaxWidthsRequested)
	CalcMaxWidth();

    /* title for Dumper-Stats */
    w1= ColWidth(HostName, Level);
    wDump= ColWidth(OrigKB, DumpRate);
    wTape= ColWidth(TapeTime, TapeRate);

    /* print centered top titles */
    h= (wDump-strlen(ds))/2;
    fprintf(mailf, "%*s", w1+h, "");
    fprintf(mailf, "%-*s", wDump-h, ds);
    h= (wTape-strlen(ts))/2;
    fprintf(mailf, "%*s", h, "");
    fprintf(mailf, "%-*s", wTape-h, ts);
    fputc('\n', mailf);

    /* print the titles */
    for (i=0; i<ColumnNameCount; i++) {
    	char *fmt;
    	ColumnInfo *cd= &ColumnData[i];
    	fprintf(mailf, "%*s", cd->PrefixSpace, "");
	if (cd->Format[1] == '-')
	    fmt= "%-*s";
	else
	    fmt= "%*s";
	fprintf(mailf, fmt, cd->Width, cd->Title);
    }
    fputc('\n', mailf);

    /* print the rules */
    fputs(tmp=Rule(HostName, Level), mailf); amfree(tmp);
    fputs(tmp=Rule(OrigKB, DumpRate), mailf); amfree(tmp);
    fputs(tmp=Rule(TapeTime, TapeRate), mailf); amfree(tmp);
    fputc('\n', mailf);

    /* print out postscript line for Amanda label file */
    if (postscript) {
	fprintf(postscript,
	  "(-) (%s) (-) (  0) (      32) (      32) DrawHost\n",
          tape_labels ? tape_labels : "");
    }

    for(dp = sortq.head; dp != NULL; free(dp->up), dp = dp->next) {
    	ColumnInfo *cd;
	char TimeRateBuffer[40];
	if (data(dp)->nb_taper == 0)
	    data(dp)->nb_taper= 1;
	for (i=0; i<data(dp)->nb_taper; i++) {
	    int devlen;

	    cd= &ColumnData[HostName];
	    fprintf(mailf, "%*s", cd->PrefixSpace, "");
	    fprintf(mailf, cd->Format, cd->Width, cd->Width, dp->host->hostname);

	    cd= &ColumnData[Disk];
	    fprintf(mailf, "%*s", cd->PrefixSpace, "");
	    devlen= strlen(dp->name);
	    if (devlen > cd->Width) {
	   	fputc('-', mailf); 
		fprintf(mailf, cd->Format, cd->Width-1, cd->Precision-1,
		  dp->name+devlen - (cd->Width-1) );
	    }
	    else
		fprintf(mailf, cd->Format, cd->Width, cd->Width, dp->name);

	    cd= &ColumnData[Level];
	    if (data(dp)->result == L_BOGUS) {
	      if(amflush_run){
		fprintf(mailf, "%*s%s\n", cd->PrefixSpace+cd->Width, "",
			tmp=TextRule(OrigKB, TapeRate, "NO FILE TO FLUSH"));
		amfree(tmp);
	      } else {
		fprintf(mailf, "%*s%s\n", cd->PrefixSpace+cd->Width, "",
			tmp=TextRule(OrigKB, TapeRate, "MISSING"));
		amfree(tmp);
	      }
	      continue;
	    }
	    
	    cd= &ColumnData[Level];
	    fprintf(mailf, "%*s", cd->PrefixSpace, "");
	    if(data(dp)->taper[i].success) {
		fprintf(mailf, cd->Format, cd->Width, cd->Precision, data(dp)->taper[i].level);
	    }
	    else {
		fprintf(mailf, cd->Format, cd->Width, cd->Precision, data(dp)->dumper.level);
	    }

	    if (data(dp)->result == L_SKIPPED) {
		fprintf(mailf, "%s\n",
			tmp=TextRule(OrigKB, TapeRate, "SKIPPED"));
		amfree(tmp);
		continue;
	    }
	    if (data(dp)->result == L_FAIL) {
		fprintf(mailf, "%s\n",
			tmp=TextRule(OrigKB, TapeRate, "FAILED"));
		amfree(tmp);
		continue;
	    }

	    if((data(dp)->dumper.success) &&
	       ((!data(dp)->taper[i].success) ||
	        (data(dp)->dumper.datestamp == data(dp)->taper[i].datestamp))) {
		origsize = data(dp)->dumper.origsize;
		outsize  = data(dp)->dumper.outsize;
	    }
	    else {
		origsize = data(dp)->taper[i].origsize;
		outsize  = data(dp)->taper[i].outsize;
	    }

	    cd= &ColumnData[OrigKB];
	    fprintf(mailf, "%*s", cd->PrefixSpace, "");
	    if(origsize != 0.0)
		fprintf(mailf, cd->Format, cd->Width, cd->Precision, origsize);
	    else
		fprintf(mailf, "%*.*s", cd->Width, cd->Width, "N/A");

	    cd= &ColumnData[OutKB];
	    fprintf(mailf, "%*s", cd->PrefixSpace, "");

	    fprintf(mailf, cd->Format, cd->Width, cd->Precision, outsize);
	    	
	    cd= &ColumnData[Compress];
	    fprintf(mailf, "%*s", cd->PrefixSpace, "");

	    if(dp->compress == COMP_NONE)
		f = 0.0;
	    else if(origsize < 1.0)
		f = 0.0;
	    else
		f = origsize;

	    fputs(sDivZero(pct(outsize), f, Compress), mailf);

	    cd= &ColumnData[DumpTime];
	    fprintf(mailf, "%*s", cd->PrefixSpace, "");
	    if((data(dp)->dumper.success) &&
	       ((!data(dp)->taper[i].success) ||
	        (data(dp)->dumper.datestamp == data(dp)->taper[i].datestamp)))
		ap_snprintf(TimeRateBuffer, sizeof(TimeRateBuffer),
		  "%3d:%02d", mnsc(data(dp)->dumper.sec));
	    else
		ap_snprintf(TimeRateBuffer, sizeof(TimeRateBuffer),
		  "N/A ");
	    fprintf(mailf, cd->Format, cd->Width, cd->Width, TimeRateBuffer);

	    cd= &ColumnData[DumpRate];
	    fprintf(mailf, "%*s", cd->PrefixSpace, "");
	    if((data(dp)->dumper.success) &&
	       ((!data(dp)->taper[i].success) ||
	        (data(dp)->dumper.datestamp == data(dp)->taper[i].datestamp)))
		fprintf(mailf, cd->Format, cd->Width, cd->Precision, data(dp)->dumper.kps);
	    else
		fprintf(mailf, "%*s", cd->Width, "N/A ");

	    cd= &ColumnData[TapeTime];
	    fprintf(mailf, "%*s", cd->PrefixSpace, "");
	    if(!data(dp)->taper[i].success && !degraded_mode) {
		fprintf(mailf, "%s\n",
			tmp=TextRule(TapeTime, TapeRate, "FAILED "));
		amfree(tmp);
		continue;
	    }
	    if(data(dp)->taper[i].success)
		ap_snprintf(TimeRateBuffer, sizeof(TimeRateBuffer),
		  "%3d:%02d", mnsc(data(dp)->taper[i].sec));
	    else
		ap_snprintf(TimeRateBuffer, sizeof(TimeRateBuffer),
		  "N/A ");
	    fprintf(mailf, cd->Format, cd->Width, cd->Width, TimeRateBuffer);

	    cd= &ColumnData[TapeRate];
	    fprintf(mailf, "%*s", cd->PrefixSpace, "");
	    if(data(dp)->taper[i].success)
		fprintf(mailf, cd->Format, cd->Width, cd->Precision, data(dp)->taper[i].kps);
	    else
		fprintf(mailf, "%*s", cd->Width, "N/A ");
	    fputc('\n', mailf);

	    if ((postscript) && (data(dp)->taper[i].success)) {
		if(origsize != 0.0) {
		    fprintf(postscript,"(%s) (%s) (%d) (%3.0d) (%8.0f) (%8.0f) DrawHost\n",
			dp->host->hostname, dp->name, data(dp)->taper[i].level,
                        data(dp)->taper[i].filenum, origsize, 
			outsize);
		}
		else {
		    fprintf(postscript,"(%s) (%s) (%d) (%3.0d) (%8s) (%8.0f) DrawHost\n",
			dp->host->hostname, dp->name, data(dp)->taper[i].level,
                        data(dp)->taper[i].filenum, "N/A", 
			outsize);
		}
	    }
	}
    }
}

void bogus_line()
{
    printf("line %d of log is bogus\n", curlinenum);
}


char *nicedate(datestamp)
int datestamp;
/*
 * Formats an integer of the form YYYYMMDD into the string
 * "Monthname DD, YYYY".  A pointer to the statically allocated string
 * is returned, so it must be copied to other storage (or just printed)
 * before calling nicedate() again.
 */
{
    static char nice[64];
    static char *months[13] = { "BogusMonth",
	"January", "February", "March", "April", "May", "June",
	"July", "August", "September", "October", "November", "December"
    };
    int year, month, day;

    year  = datestamp / 10000;
    day   = datestamp % 100;
    month = (datestamp / 100) % 100;

    ap_snprintf(nice, sizeof(nice), "%s %d, %d", months[month], day, year);

    return nice;
}

void handle_start()
{
    static int started = 0;
    char *label;
    char *s, *fp;
    int ch;

    switch(curprog) {
    case P_TAPER:
	s = curstr;
	ch = *s++;

	skip_whitespace(s, ch);
#define sc "datestamp"
	if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	    bogus_line();
	    return;
	}
	s += sizeof(sc)-1;
	ch = s[-1];
#undef sc
	skip_whitespace(s, ch);
	if(ch == '\0') {
	    bogus_line();
	    return;
	}
	fp = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';
	datestamp = newstralloc(datestamp, fp);
	s[-1] = ch;

	skip_whitespace(s, ch);
#define sc "label"
	if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	    bogus_line();
	    return;
	}
	s += sizeof(sc)-1;
	ch = s[-1];
#undef sc
	skip_whitespace(s, ch);
	if(ch == '\0') {
	    bogus_line();
	    return;
	}
	label = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	if(tape_labels) {
	    fp = vstralloc(tape_labels, ", ", label, NULL);
	    amfree(tape_labels);
	    tape_labels = fp;
	} else {
	    tape_labels = stralloc(label);
	}
	s[-1] = ch;

	last_run_tapes++;
	return;
    case P_PLANNER:
    case P_DRIVER:
	normal_run = 1;
	break;
    case P_AMFLUSH:
	amflush_run = 1;
	break;
    default:
	;
    }

    if(!started) {
	s = curstr;
	ch = *s++;

	skip_whitespace(s, ch);
#define sc "date"
	if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	    return;				/* ignore bogus line */
	}
	s += sizeof(sc)-1;
	ch = s[-1];
#undef sc
	skip_whitespace(s, ch);
	if(ch == '\0') {
	    bogus_line();
	    return;
	}
	fp = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';
	datestamp = newstralloc(datestamp, fp);
	s[-1] = ch;

	started = 1;
    }
    if(amflush_run && normal_run) {
	amflush_run = 0;
	addline(&notes,
     "  reporter: both amflush and driver output in log, ignoring amflush.");
    }
}


void handle_finish()
{
    char *s;
    int ch;

    if(curprog == P_DRIVER || curprog == P_AMFLUSH) {
	s = curstr;
	ch = *s++;

	skip_whitespace(s, ch);
#define sc "date"
	if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	    bogus_line();
	    return;
	}
	s += sizeof(sc)-1;
	ch = s[-1];
#undef sc

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    bogus_line();
	    return;
	}
	skip_non_whitespace(s, ch);	/* ignore the date string */

	skip_whitespace(s, ch);
#define sc "time"
	if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	    bogus_line();
	    return;
	}
	s += sizeof(sc)-1;
	ch = s[-1];
#undef sc

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    bogus_line();
	    return;
	}
	if(sscanf(s - 1, "%f", &total_time) != 1) {
	    bogus_line();
	    return;
	}

	got_finish = 1;
    }
}

void handle_stats()
{
    char *s;
    int ch;

    if(curprog == P_DRIVER) {
	s = curstr;
	ch = *s++;

	skip_whitespace(s, ch);
#define sc "startup time"
	if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	    bogus_line();
	    return;
	}
	s += sizeof(sc)-1;
	ch = s[-1];
#undef sc

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    bogus_line();
	    return;
	}
	if(sscanf(s - 1, "%f", &startup_time) != 1) {
	    bogus_line();
	    return;
	}
    }
}


void handle_note()
{
    char *str = NULL;

    str = vstralloc("  ", program_str[curprog], ": ", curstr, NULL);
    addline(&notes, str);
    amfree(str);
}


/* ----- */

void handle_error()
{
    char *s = NULL, *nl;
    int ch;

    if(curlog == L_ERROR && curprog == P_TAPER) {
	s = curstr;
	ch = *s++;

	skip_whitespace(s, ch);
#define sc "no-tape"
	if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	    bogus_line();
	    return;
	}
	s += sizeof(sc)-1;
	ch = s[-1];
#undef sc

	skip_whitespace(s, ch);
	if(ch != '\0') {
	    if((nl = strchr(s - 1, '\n')) != NULL) {
		*nl = '\0';
	    }
	    tapestart_error = newstralloc(tapestart_error, s - 1);
	    if(nl) *nl = '\n';
	    degraded_mode = 1;
	    return;
	}
	/* else some other tape error, handle like other errors */
    }
    s = vstralloc("  ", program_str[curprog], ": ",
		  logtype_str[curlog], " ", curstr, NULL);
    addline(&errsum, s);
    amfree(s);
}

/* ----- */

void handle_summary()
{
    bogus_line();
}

/* ----- */

void setup_disk(dp)
disk_t *dp;
{
    if(dp->up == NULL) {
	dp->up = (void *) alloc(sizeof(repdata_t));
	memset(dp->up, '\0', sizeof(repdata_t));
	data(dp)->result = L_BOGUS;
    }
}

void setup_data()
{
    disk_t *dp;
    for(dp = diskq->head; dp != NULL; dp = dp->next)
	setup_disk(dp);
}

int level;

void handle_success()
{
    disk_t *dp;
    float sec, kps, kbytes, origkb;
    struct timedata_s *sp;
    info_t inf;
    int i;
    char *s, *fp;
    int ch;
    int datestampI;


    if(curprog != P_TAPER && curprog != P_DUMPER && curprog != P_PLANNER) {
	bogus_line();
	return;
    }

    s = curstr;
    ch = *s++;

    skip_whitespace(s, ch);
    if(ch == '\0') {
	bogus_line();
	return;
    }
    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    hostname = newstralloc(hostname, fp);
    s[-1] = ch;

    skip_whitespace(s, ch);
    if(ch == '\0') {
	bogus_line();
	return;
    }
    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    diskname = newstralloc(diskname, fp);
    s[-1] = ch;

    skip_whitespace(s, ch);

    if(ch == '\0' || sscanf(s - 1, "%d", &datestampI) != 1) {
	bogus_line();
	return;
    }
    skip_integer(s, ch);

    if(datestampI < 100)  {
	level = datestampI;
	datestampI = atoi(datestamp);
    }
    else {
	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	    bogus_line();
	    return;
	}
	skip_integer(s, ch);
    }

    skip_whitespace(s, ch);
				/* Planner success messages (for skipped
				   dumps) do not contain statistics */
    if(curprog != P_PLANNER) {
	if(curprog != P_DUMPER ||
	   sscanf(s - 1,"[sec %f kb %f kps %f orig-kb %f", 
		  &sec, &kbytes, &kps, &origkb) != 4)  {
	    origkb = -1;
	    if(sscanf(s - 1,"[sec %f kb %f kps %f",
		      &sec, &kbytes, &kps) != 3) {
		bogus_line();
		return;
	    }
	}
	else {
	    if(origkb == 0.0) origkb = 0.1;
	}
    }


    dp = lookup_disk(hostname, diskname);
    if(dp == NULL) {
	char *str = NULL;

	str = vstralloc("  ", prefix(hostname, diskname, level),
			" ", "ERROR [not in disklist]",
			NULL);
	addline(&errsum, str);
	amfree(str);
	return;
    }

    if(curprog == P_PLANNER) {
	data(dp)->result = L_SKIPPED;
	return;
    }
    data(dp)->result = L_SUCCESS;

    if(curprog == P_TAPER)
	sp = &(data(dp)->taper[data(dp)->nb_taper++]);
    else sp = &(data(dp)->dumper);

    i = level > 0;

    if(origkb == -1) {
	struct tm *tm;
	int datestamp;
	/* grab original size from record */
	get_info(hostname, diskname, &inf);
	tm = localtime(&inf.inf[level].date);
	datestamp = 10000*(tm->tm_year+1900) + 
		      100*(tm->tm_mon+1) + tm->tm_mday;
	if(datestampI == datestamp)
	    origkb = (double)inf.inf[level].size;
	else
	    origkb = 0.0;
    }

    sp->success = 1;
    sp->datestamp = datestampI;
    sp->level = level;
    sp->sec = sec;
    sp->kps = kps;
    sp->origsize = origkb;
    sp->outsize = kbytes;

    if(curprog == P_TAPER) {
	stats[i].taper_time += sec;
        sp->filenum = ++tapefcount;
	tapedisks[level] +=1;
	stats[i].tapedisks +=1;
	stats[i].tapesize += kbytes;
    }

    if(curprog == P_DUMPER) {
	stats[i].dumper_time += sec;
	if(dp->compress == COMP_NONE) {
/*	    if(sp->origsize == -1 || sp->origsize == 0.0)*/
		sp->origsize = kbytes;
	}
	else {
	    stats[i].coutsize += kbytes;
	    stats[i].corigsize += sp->origsize;
	}
	dumpdisks[level] +=1;
	stats[i].dumpdisks +=1;
	stats[i].origsize += sp->origsize;
	stats[i].outsize += kbytes;
    }

}

void handle_strange()
{
    char *str = NULL;

    handle_success();

    str = vstralloc("  ", prefix(hostname, diskname, level),
		    " ", "STRANGE",
		    NULL);
    addline(&errsum, str);
    amfree(str);

    addline(&errdet,"");
    str = vstralloc("/-- ", prefix(hostname, diskname, level),
		    " ", "STRANGE",
		    NULL);
    addline(&errdet, str);
    amfree(str);

    while(contline_next()) {
	get_logline(logfile);
	addline(&errdet, curstr);
    }
    addline(&errdet,"\\--------");
}

void handle_failed()
{
    disk_t *dp;
    char *hostname;
    char *diskname;
    char *errstr;
    int level;
    char *s;
    int ch;
    char *str = NULL;

    hostname = NULL;
    diskname = NULL;

    s = curstr;
    ch = *s++;

    skip_whitespace(s, ch);
    if(ch == '\0') {
	bogus_line();
	return;
    }
    hostname = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    skip_whitespace(s, ch);
    if(ch == '\0') {
	bogus_line();
	return;
    }
    diskname = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    skip_whitespace(s, ch);
    if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	bogus_line();
	return;
    }
    skip_integer(s, ch);

    skip_whitespace(s, ch);
    if(ch == '\0') {
	bogus_line();
	return;
    }
    errstr = s - 1;
    if((s = strchr(errstr, '\n')) != NULL) {
	*s = '\0';
    }

    dp = lookup_disk(hostname, diskname);
    if(dp == NULL) {
	str = vstralloc("  ", prefix(hostname, diskname, level),
			" ", "ERROR [not in disklist]",
			NULL);
	addline(&errsum, str);
	amfree(str);
    } else {
	if(data(dp)->result != L_SUCCESS) {
	    data(dp)->result = L_FAIL;
	    data(dp)->dumper.level = level;
	}
    }

    str = vstralloc("  ", prefix(hostname, diskname, level),
		    " ", "FAILED",
		    " ", errstr,
		    NULL);
    addline(&errsum, str);
    amfree(str);

    if(curprog == P_DUMPER) {
        addline(&errdet,"");
	str = vstralloc("/-- ", prefix(hostname, diskname, level),
			" ", "FAILED",
			" ", errstr,
			NULL);
        addline(&errdet, str);
        while(contline_next()) {
	    get_logline(logfile);
	    addline(&errdet, curstr);
        }
        addline(&errdet,"\\--------");
    }
    return;
}

void generate_missing()
{
    disk_t *dp;
    char *str = NULL;

    for(dp = diskq->head; dp != NULL; dp = dp->next) {
	if(data(dp)->result == L_BOGUS) {
	    str = vstralloc("  ", prefix(dp->host->hostname, dp->name, -987),
			    " ", "RESULTS MISSING",
			    NULL);
	    addline(&errsum, str);
	    amfree(str);
	}
    }
}

static char *
prefix (host, disk, level)
    char *host;
    char *disk;
    int level;
{
    char h[10+1];
    int l;
    char number[NUM_STR_SIZE];
    static char *str = NULL;

    ap_snprintf(number, sizeof(number), "%d", level);
    if(host) {
	strncpy(h, host, sizeof(h)-1);
    } else {
	strncpy(h, "(host?)", sizeof(h)-1);
    }
    h[sizeof(h)-1] = '\0';
    for(l = strlen(h); l < sizeof(h)-1; l++) {
	h[l] = ' ';
    }
    str = newvstralloc(str,
		       h,
		       " ", disk ? disk : "(disk?)",
		       level != -987 ? " lev " : "",
		       level != -987 ? number : "",
		       NULL);
    return str;
}

void copy_template_file(lbl_templ)
char *lbl_templ;
{
  char buf[BUFSIZ];
  int fd;
  int numread;

  if (strchr(lbl_templ, '/') == NULL) {
    lbl_templ = stralloc2(config_dir, lbl_templ);
  } else {
    lbl_templ = stralloc(lbl_templ);
  }
  if ((fd = open(lbl_templ, 0)) < 0) {
    error("could not open template file \"%s\": %s",
	  lbl_templ, strerror(errno));
  }
  while ((numread = read(fd, buf, sizeof(buf))) > 0) {
    if (fwrite(buf, numread, 1, postscript) != 1) {
      error("error copying template file");
    }
  }
  if (numread < 0) {
    error("error reading template file: %s",strerror(errno));
  }
  close(fd);
  amfree(lbl_templ);
}
