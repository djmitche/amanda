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
 * $Id: reporter.c,v 1.86 2004/02/13 13:42:22 martinea Exp $
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
#include "util.h"

/* don't have (or need) a skipped type except internally to reporter */
#define L_SKIPPED	L_MARKER

typedef struct line_s {
    struct line_s *next;
    char *str;
} line_t;

typedef struct timedata_s {
    logtype_t result;
    float origsize, outsize;
    char *datestamp;
    float sec, kps;
    int filenum;
    char *tapelabel;
} timedata_t;

typedef struct repdata_s {
    disk_t *disk;
    char *datestamp;
    timedata_t taper;
    timedata_t dumper;
    timedata_t chunker;
    int level;
    struct repdata_s *next;
} repdata_t;

#define data(dp) ((repdata_t *)(dp)->up)

static struct cumulative_stats {
    int dumpdisks, tapedisks;
    double taper_time, dumper_time;
    double outsize, origsize, tapesize;
    double coutsize, corigsize;			/* compressed dump only */
} stats[3];

static int dumpdisks[10], tapedisks[10];	/* by-level breakdown of disk count */

typedef struct taper_s {
    char *label;
    double taper_time;
    double coutsize, corigsize;
    int tapedisks;
    struct taper_s *next;
} taper_t;

static taper_t *stats_by_tape = NULL;
static taper_t *current_tape = NULL;

static float total_time, startup_time;

/* count files to tape */
static int tapefcount = 0;

static char *run_datestamp;
static char *today_datestamp;
static char *tape_labels = NULL;
static int last_run_tapes = 0;
static int degraded_mode = 0; /* defined in driverio too */
static int normal_run = 0;
static int amflush_run = 0;
static int got_finish = 0;

static char *tapestart_error = NULL;

static FILE *logfile, *mailf;

static FILE *postscript;
static char *printer;

static disklist_t diskq;
static disklist_t sortq;

static line_t *errsum = NULL;
static line_t *errdet = NULL;
static line_t *notes = NULL;

static char MaxWidthsRequested = 0;	/* determined via config data */

/*
char *hostname = NULL, *diskname = NULL;
*/

/* local functions */
static int contline_next P((void));
static void addline P((line_t **lp, char *str));
static void usage P((void));
int main P((int argc, char **argv));

static void copy_template_file P((char *lbl_templ));
static void do_postscript_output P((void));
static void handle_start P((void));
static void handle_finish P((void));
static void handle_note P((void));
static void handle_summary P((void));
static void handle_stats P((void));
static void handle_error P((void));
static void handle_disk P((void));
static repdata_t *handle_success P((void));
static void handle_partial P((void));
static void handle_strange P((void));
static void handle_failed P((void));
static void generate_missing P((void));
static void output_tapeinfo P((void));
static void output_lines P((line_t *lp, FILE *f));
static void output_stats P((void));
static void output_summary P((void));
static void sort_disks P((void));
static int sort_by_name P((disk_t *a, disk_t *b));
static void bogus_line P((void));
static char *nicedate P((int datestamp));
static char *prefix P((char *host, char *disk, int level));
static repdata_t *find_repdata P((disk_t *dp, char *datestamp, int level));


static int
ColWidth(From, To)
    int From, To;
{
    int i, Width= 0;
    for (i=From; i<=To && ColumnData[i].Name != NULL; i++) {
    	Width+= ColumnData[i].PrefixSpace + ColumnData[i].Width;
    }
    return Width;
}

static char *
Rule(From, To)
    int From, To;
{
    int i, ThisLeng;
    int Leng= ColWidth(0, ColumnDataCount());
    char *RuleSpace= alloc(Leng+1);
    ThisLeng= ColWidth(From, To);
    for (i=0;i<ColumnData[From].PrefixSpace; i++)
    	RuleSpace[i]= ' ';
    for (; i<ThisLeng; i++)
    	RuleSpace[i]= '-';
    RuleSpace[ThisLeng]= '\0';
    return RuleSpace;
}

static char *
TextRule(From, To, s)
    int From, To;
    char *s;
{
    ColumnInfo *cd= &ColumnData[From];
    int leng, nbrules, i, txtlength;
    int RuleSpaceSize= ColWidth(0, ColumnDataCount());
    char *RuleSpace= alloc(RuleSpaceSize), *tmp;

    leng= strlen(s);
    if(leng >= (RuleSpaceSize - cd->PrefixSpace))
	leng = RuleSpaceSize - cd->PrefixSpace - 1;
    snprintf(RuleSpace, RuleSpaceSize, "%*s%*.*s ", cd->PrefixSpace, "", 
	     leng, leng, s);
    txtlength = cd->PrefixSpace + leng + 1;
    nbrules = ColWidth(From,To) - txtlength;
    for(tmp=RuleSpace + txtlength, i=nbrules ; i>0; tmp++,i--)
	*tmp='-';
    *tmp = '\0';
    return RuleSpace;
}

static char *
sDivZero(a, b, cn)
    double a, b;
    int cn;
{
    ColumnInfo *cd= &ColumnData[cn];
    static char PrtBuf[256];
    if (b == 0.0)
    	snprintf(PrtBuf, sizeof(PrtBuf),
	  "%*s", cd->Width, "-- ");
    else
    	snprintf(PrtBuf, sizeof(PrtBuf),
	  cd->Format, cd->Width, cd->Precision, a/b);
    return PrtBuf;
}

static int
contline_next()
{
    int ch;

    ch = getc(logfile);
    ungetc(ch, logfile);

    return ch == ' ';
}

static void
addline(lp, str)
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

static void
usage()
{
    error("Usage: amreport conf [-f output-file] [-l logfile] [-p postscript-file]");
}

int
main(argc, argv)
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
    char *ColumnSpec = "";
    char *errstr = NULL;
    int cn;

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
                    psfname = vstralloc(my_cwd, "/", optarg, NULL);
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

#if !defined MAILER
    if(!outfname) {
	printf("You must run amreport with '-f <output file>' because configure\n");
	printf("didn't find a mailer.\n");
	exit (1);
    }
#endif

    safe_cd();

    /* read configuration files */

    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if(read_conffile(conffile)) {
        error("errors processing config file \"%s\"", conffile);
    }
    amfree(conffile);
    conf_diskfile = getconf_str(CNF_DISKFILE);
    if (*conf_diskfile == '/') {
	conf_diskfile = stralloc(conf_diskfile);
    } else {
	conf_diskfile = stralloc2(config_dir, conf_diskfile);
    }
    if(read_diskfile(conf_diskfile, &diskq) < 0) {
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

    today_datestamp = construct_datestamp(NULL);

    ColumnSpec = getconf_str(CNF_COLUMNSPEC);
    if(SetColumDataFromString(ColumnData, ColumnSpec, &errstr) < 0) {
	curlog = L_ERROR;
	curprog = P_REPORTER;
	curstr = errstr;
	handle_error();
        amfree(errstr);
	curstr = NULL;
	ColumnSpec = "";		/* use the default */
	if(SetColumDataFromString(ColumnData, ColumnSpec, &errstr) < 0) {
	    curlog = L_ERROR;
	    curprog = P_REPORTER;
	    curstr = errstr;
	    handle_error();
            amfree(errstr);
	    curstr = NULL;
	}
    }
    for (cn = 0; ColumnData[cn].Name != NULL; cn++) {
	if (ColumnData[cn].MaxWidth) {
	    MaxWidthsRequested = 1;
	    break;
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
	curlog = L_ERROR;
	curprog = P_REPORTER;
	curstr = vstralloc("could not open log ",
			   logfname,
			   ": ",
			   strerror(errno),
			   NULL);
	handle_error();
	amfree(curstr);
    }

    while(logfile && get_logline(logfile)) {
	switch(curlog) {
	case L_START:   handle_start(); break;
	case L_FINISH:  handle_finish(); break;

	case L_INFO:    handle_note(); break;
	case L_WARNING: handle_note(); break;

	case L_SUMMARY: handle_summary(); break;
	case L_STATS:   handle_stats(); break;

	case L_ERROR:   handle_error(); break;
	case L_FATAL:   handle_error(); break;

	case L_DISK:    handle_disk(); break;

	case L_SUCCESS: handle_success(); break;
	case L_PARTIAL: handle_partial(); break;
	case L_STRANGE: handle_strange(); break;
	case L_FAIL:    handle_failed(); break;

	default:
	    curlog = L_ERROR;
	    curprog = P_REPORTER;
	    curstr = stralloc2("unexpected log line: ", curstr);
	    handle_error();
	    amfree(curstr);
	}
    }
    afclose(logfile);
    close_infofile();
    if(!amflush_run)
	generate_missing();

    subj_str = vstralloc(getconf_str(CNF_ORG),
			 " ", amflush_run ? "AMFLUSH" : "AMANDA",
			 " ", "MAIL REPORT FOR",
			 " ", nicedate(run_datestamp ? atoi(run_datestamp) : 0),
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
#ifdef MAILER
	mail_cmd = vstralloc(MAILER,
			     " -s", " \"", subj_str, "\"",
			     " ", getconf_str(CNF_MAILTO),
			     NULL);
	if((mailf = popen(mail_cmd, "w")) == NULL)
	    error("could not open pipe to \"%s\": %s",
		  mail_cmd, strerror(errno));
#endif
    }

    /* open pipe to print spooler if necessary) */

    if(psfname) {
	/* if the postscript_label_template (tp->lbl_templ) field is not */
	/* the empty string (i.e. it is set to something), open the      */
	/* postscript debugging file for writing.                        */
	if ((strcmp(tp->lbl_templ, "")) != 0) {
	    if ((postscript = fopen(psfname, "w")) == NULL) {
		curlog = L_ERROR;
		curprog = P_REPORTER;
		curstr = vstralloc("could not open ",
				   psfname,
				   ": ",
				   strerror(errno),
				   NULL);
		handle_error();
		amfree(curstr);
	    }
	}
    } else {
#ifdef LPRCMD
	if (strcmp(printer, "") != 0)	/* alternate printer is defined */
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

	if ((strcmp(tp->lbl_templ, "")) != 0) {
#ifdef LPRCMD
	    if ((postscript = popen(printer_cmd, "w")) == NULL) {
		curlog = L_ERROR;
		curprog = P_REPORTER;
		curstr = vstralloc("could not open pipe to ",
				   printer_cmd,
				   ": ",
				   strerror(errno),
				   NULL);
		handle_error();
		amfree(curstr);
	    }
#else
	    curlog = L_ERROR;
	    curprog = P_REPORTER;
	    curstr = stralloc("no printer command defined");
	    handle_error();
	    amfree(curstr);
#endif
	}
    }

    amfree(subj_str);


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

    if (postscript) {
	do_postscript_output();
    }


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

    amfree(run_datestamp);
    amfree(tape_labels);
    amfree(config_dir);
    amfree(config_name);
    amfree(printer_cmd);
    amfree(mail_cmd);
    amfree(logfname);

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

static void
output_stats()
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
	    "Output Size (meg)      %8.1f   %8.1f   %8.1f\n",
	    mb(stats[2].outsize), mb(stats[0].outsize), mb(stats[1].outsize));

    fprintf(mailf,
	    "Original Size (meg)    %8.1f   %8.1f   %8.1f\n",
	    mb(stats[2].origsize), mb(stats[0].origsize),
	    mb(stats[1].origsize));

    fprintf(mailf, "Avg Compressed Size (%%)   ");
    divzero(mailf, pct(stats[2].coutsize),stats[2].corigsize);
    fputs("      ", mailf);
    divzero(mailf, pct(stats[0].coutsize),stats[0].corigsize);
    fputs("      ", mailf);
    divzero(mailf, pct(stats[1].coutsize),stats[1].corigsize);

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
	    "Tape Size (meg)        %8.1f   %8.1f   %8.1f\n",
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

    if(stats_by_tape) {
	int label_length = strlen(stats_by_tape->label) + 5;
	fprintf(mailf,"\nUSAGE BY TAPE:\n");
	fprintf(mailf,"  %-*s  Time      Size      %%    Nb\n",
		label_length, "Label");
	for(current_tape = stats_by_tape; current_tape != NULL;
	    current_tape = current_tape->next) {
	    fprintf(mailf, "  %-*s", label_length, current_tape->label);
	    fprintf(mailf, " %2d:%02d", hrmn(current_tape->taper_time));
	    fprintf(mailf, " %9.1f  ", mb(current_tape->coutsize));
	    divzero(mailf, pct(current_tape->coutsize + 
			       marksize * current_tape->tapedisks),
			   tapesize);
	    fprintf(mailf, "  %4d\n", current_tape->tapedisks);
	}
    }

}

/* ----- */

static void
output_tapeinfo()
{
    tape_t *tp, *lasttp;
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

    lasttp = lookup_tapepos(lookup_nb_tape());
    run_tapes = getconf_int(CNF_RUNTAPES);
    if(lasttp && run_tapes > 0 && lasttp->datestamp == 0) {
	int c = 0;
	while(lasttp && run_tapes > 0 && lasttp->datestamp == 0) {
	    c++;
	    lasttp = lasttp->prev;
	    run_tapes--;
	}
	lasttp = lookup_tapepos(lookup_nb_tape());
	if(c == 1) {
	    fprintf(mailf, "The next new tape already labelled is: %s.\n",
		    lasttp->label);
	}
	else {
	    fprintf(mailf, "The next %d new tapes already labelled are: %s", c,
		    lasttp->label);
	    lasttp = lasttp->prev;
	    c--;
	    while(lasttp && c > 0 && lasttp->datestamp == 0) {
		fprintf(mailf, ", %s", lasttp->label);
		lasttp = lasttp->prev;
		c--;
	    }
	    fprintf(mailf, ".\n");
	}
    }
}

/* ----- */

static void
output_lines(lp, f)
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

static int
sort_by_name(a, b)
    disk_t *a, *b;
{
    int rc;

    rc = strcmp(a->host->hostname, b->host->hostname);
    if(rc == 0) rc = strcmp(a->name, b->name);
    return rc;
}

static void
sort_disks()
{
    disk_t *dp;

    sortq.head = sortq.tail = NULL;
    while(!empty(diskq)) {
	dp = dequeue_disk(&diskq);
	if(data(dp) == NULL) { /* create one */
	    find_repdata(dp, run_datestamp, 0);
	}
	insert_disk(&sortq, dp, sort_by_name);
    }
}

static void
CheckStringMax(cd, s)
    ColumnInfo *cd;
    char *s;
{
    if (cd->MaxWidth) {
	int l= strlen(s);
	if (cd->Width < l)
	    cd->Width= l;
    }
}

static void
CheckIntMax(cd, n)
    ColumnInfo *cd;
    int n;
{
    if (cd->MaxWidth) {
    	char testBuf[200];
    	int l;
	snprintf(testBuf, sizeof(testBuf),
	  cd->Format, cd->Width, cd->Precision, n);
	l= strlen(testBuf);
	if (cd->Width < l)
	    cd->Width= l;
    }
}

static void
CheckFloatMax(cd, d)
    ColumnInfo *cd;
    double d;
{
    if (cd->MaxWidth) {
    	char testBuf[200];
	int l;
	snprintf(testBuf, sizeof(testBuf),
	  cd->Format, cd->Width, cd->Precision, d);
	l= strlen(testBuf);
	if (cd->Width < l)
	    cd->Width= l;
    }
}

static int HostName;
static int Disk;
static int Level;
static int OrigKB;
static int OutKB;
static int Compress;
static int DumpTime;
static int DumpRate;
static int TapeTime;
static int TapeRate;

static void
CalcMaxWidth()
{
    /* we have to look for columspec's, that require the recalculation.
     * we do here the same loops over the sortq as is done in
     * output_summary. So, if anything is changed there, we have to
     * change this here also.
     *							ElB, 1999-02-24.
     */
    disk_t *dp;
    float f;
    repdata_t *repdata;

    for(dp = sortq.head; dp != NULL; dp = dp->next) {
      if(dp->todo) {
	for(repdata = data(dp); repdata != NULL; repdata = repdata->next) {
	    ColumnInfo *cd;
	    char TimeRateBuffer[40];

	    CheckStringMax(&ColumnData[HostName], dp->host->hostname);
	    CheckStringMax(&ColumnData[Disk], dp->name);
	    if (repdata->dumper.result == L_BOGUS && 
		repdata->taper.result == L_BOGUS)
		continue;
	    CheckIntMax(&ColumnData[Level], repdata->level);
	    if(repdata->dumper.result == L_SUCCESS) {
		CheckFloatMax(&ColumnData[OrigKB], repdata->dumper.origsize);
		CheckFloatMax(&ColumnData[OutKB], repdata->dumper.outsize);
		if(dp->compress == COMP_NONE)
		    f = 0.0;
		else 
		    f = repdata->dumper.origsize;
		CheckStringMax(&ColumnData[Disk], 
			sDivZero(pct(repdata->dumper.outsize), f, Compress));

		if(!amflush_run)
		    snprintf(TimeRateBuffer, sizeof(TimeRateBuffer),
				"%3d:%02d", mnsc(repdata->dumper.sec));
		else
		    snprintf(TimeRateBuffer, sizeof(TimeRateBuffer),
				"N/A ");
		CheckStringMax(&ColumnData[DumpTime], TimeRateBuffer);

		CheckFloatMax(&ColumnData[DumpRate], repdata->dumper.kps); 
	    }

	    cd= &ColumnData[TapeTime];
	    if(repdata->taper.result == L_FAIL) {
		CheckStringMax(cd, "FAILED");
		continue;
	    }
	    if(repdata->taper.result == L_SUCCESS)
		snprintf(TimeRateBuffer, sizeof(TimeRateBuffer), 
		  "%3d:%02d", mnsc(repdata->taper.sec));
	    else
		snprintf(TimeRateBuffer, sizeof(TimeRateBuffer),
		  "N/A ");
	    CheckStringMax(cd, TimeRateBuffer);

	    cd= &ColumnData[TapeRate];
	    if(repdata->taper.result == L_SUCCESS)
		CheckFloatMax(cd, repdata->taper.kps);
	    else
		CheckStringMax(cd, "N/A ");
	}
      }
    }
}

static void
output_summary()
{
    disk_t *dp;
    repdata_t *repdata;
    char *ds="DUMPER STATS";
    char *ts=" TAPER STATS";
    char *tmp;

    int i, h, w1, wDump, wTape;
    float outsize, origsize;
    float f;

    HostName = StringToColumn("HostName");
    Disk = StringToColumn("Disk");
    Level = StringToColumn("Level");
    OrigKB = StringToColumn("OrigKB");
    OutKB = StringToColumn("OutKB");
    Compress = StringToColumn("Compress");
    DumpTime = StringToColumn("DumpTime");
    DumpRate = StringToColumn("DumpRate");
    TapeTime = StringToColumn("TapeTime");
    TapeRate = StringToColumn("TapeRate");

    /* at first determine if we have to recalculate our widths */
    if (MaxWidthsRequested)
	CalcMaxWidth();

    /* title for Dumper-Stats */
    w1= ColWidth(HostName, Level);
    wDump= ColWidth(OrigKB, DumpRate);
    wTape= ColWidth(TapeTime, TapeRate);

    /* print centered top titles */
    h= strlen(ds);
    if (h > wDump) {
	h= 0;
    } else {
	h= (wDump-h)/2;
    }
    fprintf(mailf, "%*s", w1+h, "");
    fprintf(mailf, "%-*s", wDump-h, ds);
    h= strlen(ts);
    if (h > wTape) {
	h= 0;
    } else {
	h= (wTape-h)/2;
    }
    fprintf(mailf, "%*s", h, "");
    fprintf(mailf, "%-*s", wTape-h, ts);
    fputc('\n', mailf);

    /* print the titles */
    for (i=0; ColumnData[i].Name != NULL; i++) {
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

    for(dp = sortq.head; dp != NULL; dp = dp->next) {
      if(dp->todo) {
    	ColumnInfo *cd;
	char TimeRateBuffer[40];
	for(repdata = data(dp); repdata != NULL; repdata = repdata->next) {
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
	    if (repdata->dumper.result == L_BOGUS &&
		repdata->taper.result  == L_BOGUS) {
	      if(amflush_run){
		fprintf(mailf, "%*s%s\n", cd->PrefixSpace+cd->Width, "",
			tmp=TextRule(OrigKB, TapeRate, "NO FILE TO FLUSH"));
	      } else {
		fprintf(mailf, "%*s%s\n", cd->PrefixSpace+cd->Width, "",
			tmp=TextRule(OrigKB, TapeRate, "MISSING"));
	      }
	      amfree(tmp);
	      continue;
	    }
	    
	    fprintf(mailf, "%*s", cd->PrefixSpace, "");
	    fprintf(mailf, cd->Format, cd->Width, cd->Precision,repdata->level);

	    if (repdata->dumper.result == L_SKIPPED) {
		fprintf(mailf, "%s\n",
			tmp=TextRule(OrigKB, TapeRate, "SKIPPED"));
		amfree(tmp);
		continue;
	    }
	    if (repdata->dumper.result == L_FAIL && (repdata->chunker.result != L_PARTIAL && repdata->taper.result  != L_PARTIAL)) {
		fprintf(mailf, "%s\n",
			tmp=TextRule(OrigKB, TapeRate, "FAILED"));
		amfree(tmp);
		continue;
	    }

	    if(repdata->dumper.result == L_SUCCESS)
		origsize = repdata->dumper.origsize;
	    else if(repdata->taper.result == L_SUCCESS ||
		    repdata->taper.result == L_PARTIAL)
		origsize = repdata->taper.origsize;
	    else
		origsize = repdata->chunker.origsize;

	    if(repdata->taper.result == L_SUCCESS ||
	       repdata->taper.result == L_PARTIAL)
		outsize  = repdata->taper.outsize;
	    else if(repdata->chunker.result == L_SUCCESS ||
		    repdata->chunker.result == L_PARTIAL)
		outsize  = repdata->chunker.outsize;
	    else
		outsize  = repdata->dumper.outsize;

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
	    if(repdata->dumper.result == L_SUCCESS)
		snprintf(TimeRateBuffer, sizeof(TimeRateBuffer),
		  "%3d:%02d", mnsc(repdata->dumper.sec));
	    else
		snprintf(TimeRateBuffer, sizeof(TimeRateBuffer),
		  "N/A ");
	    fprintf(mailf, cd->Format, cd->Width, cd->Width, TimeRateBuffer);

	    cd= &ColumnData[DumpRate];
	    fprintf(mailf, "%*s", cd->PrefixSpace, "");
	    if(repdata->dumper.result == L_SUCCESS)
		fprintf(mailf, cd->Format, cd->Width, cd->Precision, repdata->dumper.kps);
	    else
		fprintf(mailf, "%*s", cd->Width, "N/A ");

	    cd= &ColumnData[TapeTime];
	    fprintf(mailf, "%*s", cd->PrefixSpace, "");
	    if(repdata->taper.result == L_FAIL) {
		fprintf(mailf, "%s\n",
			tmp=TextRule(TapeTime, TapeRate, "FAILED "));
		amfree(tmp);
		continue;
	    }

	    if(repdata->taper.result == L_SUCCESS || repdata->taper.result == L_PARTIAL)
		snprintf(TimeRateBuffer, sizeof(TimeRateBuffer),
		  "%3d:%02d", mnsc(repdata->taper.sec));
	    else
		snprintf(TimeRateBuffer, sizeof(TimeRateBuffer),
		  "N/A ");
	    fprintf(mailf, cd->Format, cd->Width, cd->Width, TimeRateBuffer);

	    cd= &ColumnData[TapeRate];
	    fprintf(mailf, "%*s", cd->PrefixSpace, "");
	    if(repdata->taper.result == L_SUCCESS || repdata->taper.result == L_PARTIAL)
		fprintf(mailf, cd->Format, cd->Width, cd->Precision, repdata->taper.kps);
	    else
		fprintf(mailf, "%*s", cd->Width, "N/A ");

	    if(repdata->chunker.result == L_PARTIAL ||
	       repdata->taper.result == L_PARTIAL) {
		fprintf(mailf, " PARTIAL");
	    }
	    fputc('\n', mailf);
	}
      }
    }
}

static void
bogus_line()
{
    printf("line %d of log is bogus\n", curlinenum);
}


static char *
nicedate(datestamp)
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

    snprintf(nice, sizeof(nice), "%s %d, %d", months[month], day, year);

    return nice;
}

static void
handle_start()
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
	run_datestamp = newstralloc(run_datestamp, fp);
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
	fp = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	label = stralloc(fp);
	s[-1] = ch;

	if(tape_labels) {
	    fp = vstralloc(tape_labels, ", ", label, NULL);
	    amfree(tape_labels);
	    tape_labels = fp;
	} else {
	    tape_labels = stralloc(label);
	}

	last_run_tapes++;

	if(stats_by_tape == NULL) {
	    stats_by_tape = current_tape = (taper_t *)alloc(sizeof(taper_t));
	}
	else {
	    current_tape->next = (taper_t *)alloc(sizeof(taper_t));
	    current_tape = current_tape->next;
	}
	current_tape->label = label;
	current_tape->taper_time = 0.0;
	current_tape->coutsize = 0.0;
	current_tape->corigsize = 0.0;
	current_tape->tapedisks = 0;
	current_tape->next = NULL;
	tapefcount = 0;

	return;
    case P_PLANNER:
	normal_run = 1;
	break;
    case P_DRIVER:
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
	run_datestamp = newstralloc(run_datestamp, fp);
	s[-1] = ch;

	started = 1;
    }
    if(amflush_run && normal_run) {
	amflush_run = 0;
	addline(&notes,
     "  reporter: both amflush and planner output in log, ignoring amflush.");
    }
}


static void
handle_finish()
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

static void
handle_stats()
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


static void
handle_note()
{
    char *str = NULL;

    str = vstralloc("  ", program_str[curprog], ": ", curstr, NULL);
    addline(&notes, str);
    amfree(str);
}


/* ----- */

static void
handle_error()
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

static void
handle_summary()
{
    bogus_line();
}

/* ----- */

static int nb_disk=0;
static void
handle_disk()
{
    disk_t *dp;
    char *s, *fp;
    int ch;
    char *hostname = NULL, *diskname = NULL;

    if(curprog != P_PLANNER && curprog != P_AMFLUSH) {
	bogus_line();
	return;
    }

    if(nb_disk==0) {
	for(dp = diskq.head; dp != NULL; dp = dp->next)
	    dp->todo = 0;
    }
    nb_disk++;

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

    dp = lookup_disk(hostname, diskname);
    if(dp == NULL) {
	dp = add_disk(&diskq, hostname, diskname);
    }

    amfree(hostname);
    amfree(diskname);
    dp->todo = 1;
}

static repdata_t *
handle_success()
{
    disk_t *dp;
    float sec, kps, kbytes, origkb;
    timedata_t *sp;
    int i;
    char *s, *fp;
    int ch;
    char *hostname = NULL;
    char *diskname = NULL;
    repdata_t *repdata;
    int level;
    char *datestamp;

    if(curprog != P_TAPER && curprog != P_DUMPER && curprog != P_PLANNER &&
       curprog != P_CHUNKER) {
	bogus_line();
	return NULL;
    }

    s = curstr;
    ch = *s++;

    skip_whitespace(s, ch);
    if(ch == '\0') {
	bogus_line();
	amfree(hostname);
	amfree(hostname);
	amfree(diskname);
	return NULL;
    }
    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    hostname = stralloc(fp);
    s[-1] = ch;

    skip_whitespace(s, ch);
    if(ch == '\0') {
	bogus_line();
	return NULL;
    }
    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    diskname = stralloc(fp);
    s[-1] = ch;

    skip_whitespace(s, ch);
    if(ch == '\0') {
	bogus_line();
	return NULL;
    }
    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    datestamp = stralloc(fp);
    s[-1] = ch;

    level = atoi(datestamp);
    if(level < 100)  {
	datestamp = newstralloc(datestamp, run_datestamp);
    }
    else {
	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	    bogus_line();
	    amfree(hostname);
	    amfree(diskname);
	    amfree(datestamp);
	    return NULL;
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
	        amfree(hostname);
	        amfree(diskname);
	        amfree(datestamp);
		return NULL;
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
	amfree(hostname);
	amfree(diskname);
	amfree(datestamp);
	return NULL;
    }

    repdata = find_repdata(dp, datestamp, level);

    if(curprog == P_PLANNER) {
	repdata->dumper.result = L_SKIPPED;
	amfree(hostname);
	amfree(diskname);
	amfree(datestamp);
	return repdata;
    }

    if(curprog == P_TAPER)
	sp = &(repdata->taper);
    else if(curprog == P_DUMPER)
	sp = &(repdata->dumper);
    else sp = &(repdata->chunker);

    i = level > 0;

    if(origkb == -1) {
	info_t inf;
	struct tm *tm;
	int Idatestamp;

	get_info(hostname, diskname, &inf);
        tm = localtime(&inf.inf[level].date);
        Idatestamp = 10000*(tm->tm_year+1900) +
                      100*(tm->tm_mon+1) + tm->tm_mday;

	if(atoi(datestamp) == Idatestamp) {
	    /* grab original size from record */
	    origkb = (double)inf.inf[level].size;
	}
	else
	    origkb = 0.0;
    }
    amfree(hostname);
    amfree(diskname);
    amfree(datestamp);

    sp->result = L_SUCCESS;
    sp->datestamp = repdata->datestamp;
    sp->sec = sec;
    sp->kps = kps;
    sp->origsize = origkb;
    sp->outsize = kbytes;

    if(curprog == P_TAPER) {
	if(current_tape == NULL) {
	    error("current_tape == NULL");
	}
	stats[i].taper_time += sec;
	sp->filenum = ++tapefcount;
	sp->tapelabel = current_tape->label;
	tapedisks[level] +=1;
	stats[i].tapedisks +=1;
	stats[i].tapesize += kbytes;
	sp->outsize = kbytes;
	if(repdata->chunker.outsize == 0.0) /* dump to tape OR flush */
	    stats[i].outsize += kbytes;
	current_tape->taper_time += sec;
	current_tape->coutsize += kbytes;
	current_tape->corigsize += origkb;
	current_tape->tapedisks += 1;
    }

    if(curprog == P_DUMPER) {
	stats[i].dumper_time += sec;
	if(dp->compress == COMP_NONE) {
	    sp->origsize = kbytes;
	}
	else {
	    stats[i].coutsize += kbytes;
	    stats[i].corigsize += sp->origsize;
	}
	dumpdisks[level] +=1;
	stats[i].dumpdisks +=1;
	stats[i].origsize += sp->origsize;
    }

    if(curprog == P_CHUNKER) {
	sp->outsize = kbytes;
	stats[i].outsize += kbytes;
    }
    return repdata;
}

static void
handle_partial()
{
    repdata_t *repdata;
    timedata_t *sp;

    repdata = handle_success();

    if(curprog == P_TAPER)
	sp = &(repdata->taper);
    else if(curprog == P_DUMPER)
	sp = &(repdata->dumper);
    else sp = &(repdata->chunker);

    sp->result = L_PARTIAL;
}

static void
handle_strange()
{
    char *str = NULL;
    repdata_t *repdata;

    repdata = handle_success();

    str = vstralloc("  ", prefix(repdata->disk->host->hostname, 
				 repdata->disk->name, repdata->level),
		    " ", "STRANGE",
		    NULL);
    addline(&errsum, str);
    amfree(str);

    addline(&errdet,"");
    str = vstralloc("/-- ", prefix(repdata->disk->host->hostname, 
				   repdata->disk->name, repdata->level),
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

static void
handle_failed()
{
    disk_t *dp;
    char *hostname;
    char *diskname;
    char *datestamp;
    char *errstr;
    int level;
    char *s, *fp;
    int ch;
    char *str = NULL;
    repdata_t *repdata;
    timedata_t *sp;

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
    if(ch == '\0') {
	bogus_line();
	return;
    }
    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    datestamp = stralloc(fp);

    if(strlen(datestamp) < 3) { /* there is no datestamp, it's the level */
	level = atoi(datestamp);
	datestamp = newstralloc(datestamp, run_datestamp);
    }
    else { /* read the level */
	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	    bogus_line();
	    amfree(datestamp);
	    return;
	}
	skip_integer(s, ch);
    }

    skip_whitespace(s, ch);
    if(ch == '\0') {
	bogus_line();
	amfree(datestamp);
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
	repdata = find_repdata(dp, datestamp, level);

	if(curprog == P_TAPER)
	    sp = &(repdata->taper);
	else sp = &(repdata->dumper);

	if(sp->result != L_SUCCESS)
	    sp->result = L_FAIL;
    }
    amfree(datestamp);

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
	amfree(str);
	while(contline_next()) {
	    get_logline(logfile);
	    addline(&errdet, curstr);
	}
	addline(&errdet,"\\--------");
    }
    return;
}

static void
generate_missing()
{
    disk_t *dp;
    char *str = NULL;

    for(dp = diskq.head; dp != NULL; dp = dp->next) {
	if(dp->todo && data(dp) == NULL) {
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

    snprintf(number, sizeof(number), "%d", level);
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

static void
copy_template_file(lbl_templ)
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
    curlog = L_ERROR;
    curprog = P_REPORTER;
    curstr = vstralloc("could not open PostScript template file ",
		       lbl_templ,
		       ": ",
		       strerror(errno),
		       NULL);
    handle_error();
    amfree(curstr);
    afclose(postscript);
    return;
  }
  while ((numread = read(fd, buf, sizeof(buf))) > 0) {
    if (fwrite(buf, numread, 1, postscript) != 1) {
      curlog = L_ERROR;
      curprog = P_REPORTER;
      curstr = vstralloc("error copying PostScript template file ",
		         lbl_templ,
		         ": ",
		         strerror(errno),
		         NULL);
      handle_error();
      amfree(curstr);
      afclose(postscript);
      return;
    }
  }
  if (numread < 0) {
    curlog = L_ERROR;
    curprog = P_REPORTER;
    curstr = vstralloc("error reading PostScript template file ",
		       lbl_templ,
		       ": ",
		       strerror(errno),
		       NULL);
    handle_error();
    amfree(curstr);
    afclose(postscript);
    return;
  }
  close(fd);
  amfree(lbl_templ);
}

static repdata_t *
find_repdata(dp, datestamp, level)
    disk_t *dp;
    char *datestamp;
    int level;
{
    repdata_t *repdata, *prev;

    if(!datestamp)
	datestamp = run_datestamp;
    prev = NULL;
    for(repdata = data(dp); repdata != NULL && (repdata->level != level || strcmp(repdata->datestamp,datestamp)!=0); repdata = repdata->next) {
	prev = repdata;
    }
    if(!repdata) {
	repdata = (repdata_t *)alloc(sizeof(repdata_t));
	memset(repdata, '\0',sizeof(repdata_t));
	repdata->disk = dp;
	repdata->datestamp = stralloc(datestamp ? datestamp : "");
	repdata->level = level;
	repdata->dumper.result = L_BOGUS;
	repdata->taper.result = L_BOGUS;
	repdata->next = NULL;
	if(prev)
	    prev->next = repdata;
	else
	    dp->up = (void *)repdata;
    }
    return repdata;
}


void do_postscript_output()
{
    tapetype_t *tp = lookup_tapetype(getconf_str(CNF_TAPETYPE));
    disk_t *dp;
    repdata_t *repdata;
    float outsize, origsize;
    int tapesize, marksize;

    tapesize = tp->length;
    marksize = tp->filemark;

    for(current_tape = stats_by_tape; current_tape != NULL;
	    current_tape = current_tape->next) {

	if (current_tape->label == NULL) {
	    break;
	}

	copy_template_file(tp->lbl_templ);

	/* generate a few elements */
	fprintf(postscript,"(%s) DrawDate\n\n",
		    nicedate(run_datestamp ? atoi(run_datestamp) : 0));
	fprintf(postscript,"(Amanda Version %s) DrawVers\n",version());
	fprintf(postscript,"(%s) DrawTitle\n", current_tape->label);

	/* Stats */
	fprintf(postscript, "(Total Size:        %6.1f MB) DrawStat\n",
	      mb(current_tape->coutsize));
	fprintf(postscript, "(Tape Used (%%)       ");
	divzero(postscript, pct(current_tape->coutsize + 
				marksize * current_tape->tapedisks),
				tapesize);
	fprintf(postscript," %%) DrawStat\n");
	fprintf(postscript, "(Compression Ratio:  ");
	divzero(postscript, pct(current_tape->coutsize),current_tape->corigsize);
	fprintf(postscript," %%) DrawStat\n");
	fprintf(postscript,"(Filesystems Taped: %4d) DrawStat\n",
		  current_tape->tapedisks);

	/* Summary */

	fprintf(postscript,
	      "(-) (%s) (-) (  0) (      32) (      32) DrawHost\n",
	      current_tape->label);

	for(dp = sortq.head; dp != NULL; dp = dp->next) {
	    if (dp->todo == 0) {
		 continue;
	    }
	    for(repdata = data(dp); repdata != NULL; repdata = repdata->next) {

		if(repdata->taper.tapelabel != current_tape->label) {
		    continue;
		}

		if(repdata->dumper.result == L_SUCCESS ||
		   repdata->dumper.result == L_PARTIAL)
		    origsize = repdata->dumper.origsize;
		else
		    origsize = repdata->taper.origsize;

		if(repdata->taper.result == L_SUCCESS ||
		   repdata->taper.result == L_PARTIAL)
		    outsize  = repdata->taper.outsize;
		else
		    outsize  = repdata->dumper.outsize;

		if (repdata->taper.result == L_SUCCESS ||
		    repdata->taper.result == L_PARTIAL) {
		    if(origsize != 0.0) {
			fprintf(postscript,"(%s) (%s) (%d) (%3.0d) (%8.0f) (%8.0f) DrawHost\n",
			    dp->host->hostname, dp->name, repdata->level,
			    repdata->taper.filenum, origsize, 
			    outsize);
		    }
		    else {
			fprintf(postscript,"(%s) (%s) (%d) (%3.0d) (%8s) (%8.0f) DrawHost\n",
			    dp->host->hostname, dp->name, repdata->level,
			    repdata->taper.filenum, "N/A", 
			    outsize);
		    }
		}
	    }
	}
	
	fprintf(postscript,"\nshowpage\n");
    }
}
