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
 * $Id: reporter.c,v 1.8 1997/11/17 12:41:33 amcore Exp $
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

#define MAX_LINE 2048

/* don't have (or need) a skipped type except internally to reporter */
#define L_SKIPPED	L_MARKER

typedef enum program_e {
    P_UNKNOWN, P_PLANNER, P_DRIVER, P_REPORTER, P_DUMPER, P_TAPER, P_AMFLUSH
} program_t;
#define P_LAST P_AMFLUSH

char *program_str[] = {
    "UNKNOWN", "planner", "driver", "reporter", "dumper", "taper", "amflush"
};

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
char logline[MAX_LINE], tmpstr[MAX_LINE];
extern char datestamp[];
char tape_labels[256];
int last_run_tapes = 0;
int degraded_mode = 0;
int normal_run = 0;
int amflush_run = 0;
int testing = 0;
int got_finish = 0;

char tapestart_error[80], subj_str[256];

FILE *logf, *mailf;

disklist_t *diskq;
disklist_t sortq;

line_t *errsum = NULL;
line_t *errdet = NULL;
line_t *notes = NULL;

/* local functions */
int get_logline P((void));
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

int get_logline()
{
    char *cp, *logstr, *progstr;

    if(fgets(logline, MAX_LINE, logf) == NULL)
	return 0;
    curlinenum++;
    cp = logline;

    /* continuation lines are special */

    if(*cp == ' ') {
	curlog = L_CONT;
	/* curprog stays the same */
	curstr = cp + 2;
	return 1;
    }

    /* isolate logtype field */

    logstr = cp;
    while(*cp && *cp != ' ') cp++;
    if(*cp) *cp++ = '\0';

    /* isolate program name field */

    while(*cp == ' ') cp++;	/* skip blanks */
    progstr = cp;
    while(*cp && *cp != ' ') cp++;
    if(*cp) *cp++ = '\0';

    /* rest of line is logtype dependent string */

    curstr = cp;

    /* lookup strings */

    for(curlog = L_MARKER; curlog != L_BOGUS; curlog--)
	if(!strcmp(logtype_str[curlog], logstr)) break;

    for(curprog = P_LAST; curprog != P_UNKNOWN; curprog--)
	if(!strcmp(program_str[curprog], progstr)) break;

    return 1;
}

int contline_next()
{
    int ch = getc(logf);
    ungetc(ch, logf);

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
    char *logfname, str[256];

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

    if(!testing) logfname = getconf_str(CNF_LOGFILE);

    if((logf = fopen(logfname, "r")) == NULL)
	error("could not open log %s: %s", logfname, strerror(errno));

    setup_data();    /* setup per-disk data */

    while(get_logline()) {
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
    fclose(logf);
    close_infofile();
    if(!amflush_run)
	generate_missing();

    sprintf(subj_str, "%s %s MAIL REPORT FOR %s",
	    getconf_str(CNF_ORG),
	    amflush_run? "AMFLUSH" : "AMANDA",
	    nicedate(atoi(datestamp)));

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
	sprintf(str, "%s -s \"%s\" %s", MAILER,
		subj_str, getconf_str(CNF_MAILTO));
	if((mailf = popen(str, "w")) == NULL)
	    error("could not open pipe to \"%s\": %s", str, strerror(errno));
    }

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
	fclose(mailf);
    else {
	pclose(mailf);
	log_rename(datestamp);
    }
    return 0;
}

/* ----- */

#define mb(f)	((f)/1024.0)		/* kbytes -> mbytes */
#define pct(f)	((f)*100.0)		/* percent */
#define hrmn(f) ((int)(f))/3600, ((((int)(f)) % 3600)+30)/60
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
		    last_run_tapes == 1? "" : "s", tape_labels);
	else
	    fprintf(mailf, "These dumps were to tape%s %s.\n",
		    last_run_tapes == 1? "" : "s", tape_labels);

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
	fputs(lp->str,f);
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
 * is returned, so it must be strcpy'ed to other storage (or just printed)
 * before calling nicedate() again.
 */
{
    static char nice[20];
    static char *months[13] = { "BogusMonth",
	"January", "February", "March", "April", "May", "June",
	"July", "August", "September", "October", "November", "December"
    };
    int year, month, day;

    year  = datestamp / 10000;
    day   = datestamp % 100;
    month = (datestamp / 100) % 100;

    sprintf(nice, "%s %d, %d", months[month], day, year);

    return nice;
}

void handle_start()
{
    static int started = 0;
    char label[80];
    int rc;

    switch(curprog) {
    case P_TAPER:
	sscanf(curstr, "datestamp %s label %s", datestamp, label);
	if(last_run_tapes > 0)
	    strcat(tape_labels, ", ");

	last_run_tapes++;
	strcat(tape_labels, label);
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
	rc = sscanf(curstr, "date %s", datestamp);
	if(rc == 0) return;	/* ignore bogus line */
	started = 1;
    }
    if(amflush_run && normal_run) {
	amflush_run = 0;
	addline(&notes,
     "  reporter: both amflush and driver output in log, ignoring amflush.\n");
    }
}


void handle_finish()
{
    if(curprog == P_DRIVER || curprog == P_AMFLUSH) {
	got_finish = 1;
	sscanf(curstr,"date %*s time %f", &total_time);
    }
}

void handle_stats()
{
    if(curprog == P_DRIVER) {
	sscanf(curstr,"startup time %f", &startup_time);
    }
}


void handle_note()
{
    sprintf(tmpstr, "  %s: %s", program_str[curprog], curstr);
    addline(&notes, tmpstr);
}


/* ----- */

void handle_error()
{
    int rc;

    if(curlog == L_ERROR && curprog == P_TAPER) {
	rc = sscanf(curstr, "no-tape %[^\n]", tapestart_error);
	if(rc == 1) {
	    degraded_mode = 1;
	    return;
	}
	/* else some other tape error, handle like other errors */
    }
    sprintf(tmpstr, "  %s: %s %s", program_str[curprog],
	    logtype_str[curlog], curstr);
    addline(&errsum, tmpstr);
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

char hostname[80], diskname[80];
int level;

void handle_success()
{
    disk_t *dp;
    float sec, kps, kbytes;
    struct timedata_s *sp;
    info_t inf;
    int i;

    if(curprog != P_TAPER && curprog != P_DUMPER && curprog != P_PLANNER) {
	bogus_line();
	return;
    }

    sscanf(curstr,"%s %s %d [sec %f kb %f kps %f",
	   hostname, diskname, &level, &sec, &kbytes, &kps);
    dp = lookup_disk(hostname, diskname);
    if(dp == NULL) {
	sprintf(tmpstr,"  %-10.10s %s lev %d ERROR [not in disklist]\n",
		hostname, diskname, level);
	addline(&errsum,tmpstr);
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
    handle_success();

    sprintf(tmpstr, "  %-10.10s %s lev %d STRANGE\n",
	    hostname, diskname, level);
    addline(&errsum, tmpstr);

    addline(&errdet,"\n");
    sprintf(tmpstr, "/-- %-10.10s %s lev %d STRANGE\n",
	    hostname, diskname, level);
    addline(&errdet, tmpstr);

    while(contline_next()) {
	get_logline();
	addline(&errdet, curstr);
    }
    addline(&errdet,"\\--------\n");
}

void handle_failed()
{
    disk_t *dp;
    char hostname[80], diskname[80], errstr[MAX_LINE];
    int level;

    sscanf(curstr,"%s %s %d %[^\n]", hostname, diskname, &level, errstr);
    dp = lookup_disk(hostname, diskname);
    if(dp == NULL) {
	sprintf(tmpstr, "  %-10.10s %s lev %d ERROR [not in disklist]\n",
		hostname, diskname, level);
	addline(&errsum, tmpstr);
    }
    else {
	if(data(dp)->result != L_SUCCESS) {
	    data(dp)->result = L_FAIL;
	    data(dp)->level = level;
	}
    }

    sprintf(tmpstr, "  %-10.10s %s lev %d FAILED %s\n",
	    hostname, diskname, level, errstr);
    addline(&errsum, tmpstr);

    if(curprog != P_DUMPER)
	return;

    addline(&errdet,"\n");
    sprintf(tmpstr, "/-- %-10.10s %s lev %d FAILED %s\n",
	    hostname, diskname, level, errstr);
    addline(&errdet,tmpstr);
    while(contline_next()) {
	get_logline();
	addline(&errdet, curstr);
    }
    addline(&errdet,"\\--------\n");
}

void generate_missing()
{
    disk_t *dp;

    for(dp = diskq->head; dp != NULL; dp = dp->next) {
	if(data(dp)->result == L_BOGUS) {
	    sprintf(tmpstr, "  %-10.10s %s  RESULTS MISSING\n",
		dp->host->hostname, dp->name);
	    addline(&errsum, tmpstr);
	}
    }
}
