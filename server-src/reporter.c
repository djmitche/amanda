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
 * $Id: reporter.c,v 1.16.2.2 1998/03/02 15:51:46 amcore Exp $
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

typedef struct repdata_s {
    logtype_t result;
    int level;
    float origsize, outsize;
    struct timedata_s {
	int success;
	float sec, kps;
    } taper, dumper;
} repdata_t;

#define data(dp) ((repdata_t *)(dp)->up)

struct cumulative_stats {
    int disks;
    double taper_time, dumper_time;
    double outsize, origsize;
    double coutsize, corigsize;
} stats[3];

int disks[10];	/* by-level breakdown of disk count */

float total_time, startup_time;
char *pname = "reporter";

int curlinenum;
logtype_t curlog;
program_t curprog;
char *curstr;
extern char *datestamp;
char *tape_labels = NULL;
int last_run_tapes = 0;
static int degraded_mode = 0; /* defined in driverio too */
int normal_run = 0;
int amflush_run = 0;
int testing = 0;
int got_finish = 0;

char *tapestart_error = NULL;

FILE *logfile, *mailf;

disklist_t *diskq;
disklist_t sortq;

line_t *errsum = NULL;
line_t *errdet = NULL;
line_t *notes = NULL;

/* local functions */
int contline_next P((void));
void addline P((line_t **lp, char *str));
int main P((int argc, char **argv));

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



int main(argc, argv)
int argc;
char **argv;
{
    char *logfname, *subj_str = NULL;
    int fd;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    /* open input log file */

    erroutput_type = ERR_INTERACTIVE;
    logfname = NULL;

    if(argc == 2) {
	testing = 1;
	logfname = argv[1];
    }
    else if(argc > 1)
	error("Usage: reporter [<logfile>]");
    else
	erroutput_type |= ERR_AMANDALOG;

    /* read configuration files */

    if(read_conffile(CONFFILE_NAME))
	error("could not read amanda config file\n");
    if((diskq = read_diskfile(getconf_str(CNF_DISKFILE))) == NULL)
	error("could not read disklist file\n");
    if(read_tapelist(getconf_str(CNF_TAPELIST)))
	error("parse error in %s", getconf_str(CNF_TAPELIST));
    if(open_infofile(getconf_str(CNF_INFOFILE)))
	error("could not read info database file\n");

    if(!testing) {
	logfname = vstralloc(getconf_str(CNF_LOGDIR), "/log", NULL);
    }

    if((logfile = fopen(logfname, "r")) == NULL)
	error("could not open log %s: %s", logfname, strerror(errno));

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

   /* open pipe to mailer */

    if(testing) {
	/* just output to a temp file when testing */
	/* if((mailf = fopen("/tmp/reporter.out","w")) == NULL) */
	if((mailf = fdopen(1, "w")) == NULL)
	    error("could not open tmpfile: %s", strerror(errno));
	fprintf(mailf, "To: %s\n", getconf_str(CNF_MAILTO));
	fprintf(mailf, "Subject: %s\n\n", subj_str);
    }
    else {
	char *cmd = NULL;

	cmd = vstralloc(MAILER,
			" -s", " \"", subj_str, "\"",
			" ", getconf_str(CNF_MAILTO),
			NULL);
	if((mailf = popen(cmd, "w")) == NULL)
	    error("could not open pipe to \"%s\": %s", cmd, strerror(errno));
	afree(cmd);
    }
    afree(subj_str);

    if(!got_finish) fputs("*** THE DUMPS DID NOT FINISH PROPERLY!\n\n", mailf);

    output_tapeinfo();

    if(errsum) {
	fprintf(mailf,"\nFAILURE AND STRANGE DUMP SUMMARY:\n");
	output_lines(errsum, mailf);
    }
    fputs("\n\n", mailf);
    if(!(amflush_run && degraded_mode)) {
	output_stats();
    }
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

    if(testing)
	afclose(mailf);
    else {
	apclose(mailf);
	log_rename(datestamp);
    }
    return 0;
}

/* ----- */

#define mb(f)	((f)/1024.0)		/* kbytes -> mbytes */
#define pct(f)	((f)*100.0)		/* percent */
#define hrmn(f) ((int)(f)+30)/3600, (((int)(f)+30)%3600)/60
#define mnsc(f) ((int)(f+0.5))/60, ((int)(f+0.5)) % 60

#define divzero(fp,a,b)	((b) == 0.0? \
			 fprintf(fp,"  -- ") : \
			 fprintf(fp, "%5.1f",(a)/(b)))
#define divzero_wide(fp,a,b)	((b) == 0.0? \
				 fprintf(fp,"    -- ") : \
				 fprintf(fp, "%7.1f",(a)/(b)))

void output_stats()
{
    double idle_time;
    tapetype_t *tp = lookup_tapetype(getconf_str(CNF_TAPETYPE));
    int tapesize, marksize, lv, first;

    tapesize = tp->length;
    marksize = tp->filemark;

    stats[2].disks       = stats[0].disks       + stats[1].disks;
    stats[2].outsize     = stats[0].outsize     + stats[1].outsize;
    stats[2].origsize    = stats[0].origsize    + stats[1].origsize;
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
	    "Dump Time (hrs:min)       %2d:%02d      %2d:%02d      %2d:%02d",
	    hrmn(total_time), hrmn(stats[0].taper_time),
	    hrmn(stats[1].taper_time));

    fprintf(mailf,"   (%d:%02d start", hrmn(startup_time));

    if(!got_finish) fputs(")\n", mailf);
    else fprintf(mailf,", %d:%02d idle)\n", hrmn(idle_time));

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
    putc('\n', mailf);

    fprintf(mailf, "Tape Used (%%)             ");
    divzero(mailf, pct(stats[2].outsize+marksize*stats[2].disks),tapesize);
    fputs("      ", mailf);
    divzero(mailf, pct(stats[0].outsize+marksize*stats[0].disks),tapesize);
    fputs("      ", mailf);
    divzero(mailf, pct(stats[1].outsize+marksize*stats[1].disks),tapesize);

    if(stats[1].disks > 0) fputs("   (level:#disks ...)", mailf);
    putc('\n', mailf);

    fprintf(mailf,
	    "Filesystems Dumped         %4d       %4d       %4d",
	    stats[2].disks, stats[0].disks, stats[1].disks);

    if(stats[1].disks > 0) {
	first = 1;
	for(lv = 1; lv < 10; lv++) if(disks[lv]) {
	    fputs(first?"   (":" ", mailf);
	    first = 0;
	    fprintf(mailf, "%d:%d", lv, disks[lv]);
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

    fprintf(mailf, "Avg Tp Write Rate (k/s) ");
    divzero_wide(mailf, stats[2].outsize,stats[2].taper_time);
    fputs("    ", mailf);
    divzero_wide(mailf, stats[0].outsize,stats[0].taper_time);
    fputs("    ", mailf);
    divzero_wide(mailf, stats[1].outsize,stats[1].taper_time);
    putc('\n', mailf);
}

/* ----- */

void output_tapeinfo()
{
    tape_t *tp;
    int run_tapes, pos;

    if(degraded_mode) {
	fprintf(mailf,
		"*** A TAPE ERROR OCCURRED: %s.\n", tapestart_error);

	if(amflush_run) {
	    fputs("*** COULD NOT FLUSH DUMPS.  TRY AGAIN!\n\n", mailf);
	    fputs("Flush these dumps onto", mailf);
	}
	else {
	    fputs(
	"*** PERFORMED ALL DUMPS AS INCREMENTAL DUMPS TO HOLDING DISK.\n\n",
		  mailf);
	    fputs("THESE DUMPS WERE TO DISK.  Flush them onto", mailf);
	}

	tp = lookup_tapepos(getconf_int(CNF_TAPECYCLE));
	if(tp != NULL) fprintf(mailf, " tape %s or", tp->label);
	fputs(" a new tape.\n", mailf);

	pos = getconf_int(CNF_TAPECYCLE)-1;
    }
    else {
	if(amflush_run)
	    fprintf(mailf, "The dumps were flushed to tape%s %s.\n",
		    last_run_tapes == 1 ? "" : "s",
		    tape_labels ? tape_labels : "");
	else
	    fprintf(mailf, "These dumps were to tape%s %s.\n",
		    last_run_tapes == 1 ? "" : "s",
		    tape_labels ? tape_labels : "");

	pos = getconf_int(CNF_TAPECYCLE);
    }

    run_tapes = getconf_int(CNF_RUNTAPES);

    fprintf(mailf, "Tonight's dumps should go onto %d tape%s: ", run_tapes,
	    run_tapes == 1? "" : "s");

    while(run_tapes > 0) {
	tp = lookup_tapepos(pos);
	if(tp != NULL)
	    fprintf(mailf, "%s", tp->label);
	else
	    fputs("a new tape", mailf);

	if(run_tapes > 1) fputs(", ", mailf);

	run_tapes -= 1, pos -= 1;
    }
    fputs(".\n", mailf);
}

/* ----- */

void output_lines(lp, f)
line_t *lp;
FILE *f;
{
    while(lp) {
	fputs(lp->str, f);
	fputc('\n', f);
	lp = lp->next;
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


void output_summary()
{
    disk_t *dp;
    float f;
    int len;
    char *dname;

    fprintf(mailf,
 "                                      DUMPER STATS                  TAPER STATS\n");
    fprintf(mailf,
 "HOSTNAME  DISK           L  ORIG-KB   OUT-KB COMP%%  MMM:SS   KB/s  MMM:SS   KB/s\n");
    fprintf(mailf,
 "-------------------------- -------------------------------------- --------------\n");
    for(dp = sortq.head; dp != NULL; dp = dp->next) {
#if 0
	/* should we skip missing dumps for amflush? */
	if(amflush_run && data(dp)->result == L_BOGUS)
	    continue;
#endif

	/* print rightmost chars of names that are too long to fit */
	if(((len = strlen(dp->name)) > 14) && (*(dp->name) == '/')) {
	    dname = &(dp->name[len - 13]);
	    fprintf(mailf,"%-9.9s -%-13.13s ",dp->host->hostname, dname);
	} else
	    fprintf(mailf,"%-9.9s %-14.14s ",dp->host->hostname, dp->name);

	if(data(dp)->result == L_BOGUS) {
	    if(amflush_run)
		fprintf(mailf,
		 "   NO FILE TO FLUSH -----------------------------------\n");
	    else
		fprintf(mailf,
		 "   MISSING --------------------------------------------\n");
	    continue;
	}
	if(data(dp)->result == L_SKIPPED) {
	    fprintf(mailf,
		"%1d  SKIPPED --------------------------------------------\n",
		    data(dp)->level);
	    continue;
	}
	else if(data(dp)->result == L_FAIL) {
	    fprintf(mailf,
		"%1d   FAILED --------------------------------------------\n",
		    data(dp)->level);
	    continue;
	}

	fprintf(mailf,"%1d %8.0f %8.0f ",
		data(dp)->level, data(dp)->origsize, data(dp)->outsize);

	if(dp->compress == COMP_NONE)
	    f = 0.0;
	else
	    f = data(dp)->origsize;
	divzero(mailf, pct(data(dp)->outsize), f);

	if(!amflush_run)
	    fprintf(mailf, " %4d:%02d %6.1f",
		    mnsc(data(dp)->dumper.sec), data(dp)->dumper.kps);
	else
	    fprintf(mailf, "    N/A    N/A ");

	if(data(dp)->taper.success)
	    fprintf(mailf, " %4d:%02d %6.1f",
		    mnsc(data(dp)->taper.sec), data(dp)->taper.kps);
	else if(degraded_mode)
	    fprintf(mailf,"    N/A    N/A");
	else
	    fprintf(mailf,"  FAILED ------");

	putc('\n',mailf);
    }
}

/* ----- */

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
	    afree(tape_labels);
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
    afree(str);
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
    afree(s);
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

char *hostname = NULL, *diskname = NULL;
int level;

void handle_success()
{
    disk_t *dp;
    float sec, kps, kbytes;
    struct timedata_s *sp;
    info_t inf;
    int i;
    char *s, *fp;
    int ch;

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
    if(sscanf(s - 1,"%d [sec %f kb %f kps %f",
	      &level, &sec, &kbytes, &kps) != 4) {
	bogus_line();
	return;
    }

    dp = lookup_disk(hostname, diskname);
    if(dp == NULL) {
	char *str = NULL;

	str = vstralloc("  ", prefix(hostname, diskname, level),
			" ", "ERROR [not in disklist]",
			NULL);
	addline(&errsum, str);
	afree(str);
	return;
    }

    data(dp)->level = level;

    if(curprog == P_PLANNER) {
	data(dp)->result = L_SKIPPED;
	return;
    }
    data(dp)->result = L_SUCCESS;

    if(curprog == P_TAPER) sp = &(data(dp)->taper);
    else sp = &(data(dp)->dumper);

    sp->success = 1;
    sp->sec = sec;
    sp->kps = kps;

    i = level > 0;
    if(curprog == P_TAPER) stats[i].taper_time += sec;

    if(amflush_run || curprog == P_DUMPER) {
	data(dp)->outsize = kbytes;

	if(curprog == P_DUMPER) stats[i].dumper_time += sec;
	disks[level] += 1;
	stats[i].disks += 1;
	stats[i].outsize += kbytes;
	if(dp->compress == COMP_NONE)
	    data(dp)->origsize = kbytes;
	else {
	    /* grab original size from record */
	    get_info(hostname, diskname, &inf);
	    data(dp)->origsize = (double)inf.inf[level].size;

	    stats[i].coutsize += kbytes;
	    stats[i].corigsize += data(dp)->origsize;
	}
	stats[i].origsize += data(dp)->origsize;
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
    afree(str);

    addline(&errdet,"");
    str = vstralloc("/-- ", prefix(hostname, diskname, level),
		    " ", "STRANGE",
		    NULL);
    addline(&errdet, str);
    afree(str);

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
	afree(str);
    } else {
	if(data(dp)->result != L_SUCCESS) {
	    data(dp)->result = L_FAIL;
	    data(dp)->level = level;
	}
    }

    str = vstralloc("  ", prefix(hostname, diskname, level),
		    " ", "FAILED",
		    " ", errstr,
		    NULL);
    addline(&errsum, str);
    afree(str);

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
	    afree(str);
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
