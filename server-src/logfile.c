/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998 University of Maryland at College Park
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
 * $Id: logfile.c,v 1.17 1998/07/04 00:20:07 oliva Exp $
 *
 * common log file writing routine
 */
#include "amanda.h"
#include "arglist.h"
#include "conffile.h"

#include "logfile.h"

char *logtype_str[] = {
    "BOGUS",
    "FATAL",		/* program died for some reason, used by error() */
    "ERROR", "WARNING",	"INFO", "SUMMARY",	 /* information messages */
    "START", "FINISH",				   /* start/end of a run */
    "SUCCESS", "FAIL", "STRANGE",		    /* the end of a dump */
    "STATS",						   /* statistics */
    "MARKER",					  /* marker for reporter */
    "CONT"				   /* continuation line; special */
};

char *program_str[] = {
    "UNKNOWN", "planner", "driver", "reporter", "dumper", "taper", "amflush"
};

int curlinenum;
logtype_t curlog;
program_t curprog;
char *curstr;

int multiline = -1;
static char *logfile;
static int logfd = -1;

 /*
  * Note that technically we could use two locks, a read lock
  * from 0-EOF and a write-lock from EOF-EOF, thus leaving the
  * beginning of the file open for read-only access.  Doing so
  * would open us up to some race conditions unless we're pretty
  * careful, and on top of that the functions here are so far
  * the only accesses to the logfile, so keep things simple.
  */

/* local functions */
static void open_log P((void));
static void close_log P((void));

void logerror(msg)
char *msg;
{
    log_add(L_FATAL, "%s", msg);
}

arglist_function(void log_add, logtype_t, typ)
{
    va_list argp;
    char *format;
    int saved_errout;
    char *leader = NULL;
    char linebuf[STR_SIZE];
    int l, n, s;


    /* format error message */

    if((int)typ <= (int)L_BOGUS || (int)typ > (int)L_MARKER) typ = L_BOGUS;

    if(multiline > 0) {
	leader = stralloc("  ");		/* continuation line */
    } else {
	leader = vstralloc(logtype_str[(int)typ], " ", get_pname(), " ", NULL);
    }

    arglist_start(argp, typ);
    format = arglist_val(argp, char *);
    ap_vsnprintf(linebuf, sizeof(linebuf)-1, format, argp);
						/* -1 to allow for '\n' */
    arglist_end(argp);

    /* avoid recursive call from error() */

    saved_errout = erroutput_type;
    erroutput_type &= ~ERR_AMANDALOG;

    /* append message to the log file */

    if(multiline == -1) open_log();

    for(l = 0, n = strlen(leader); l < n; l += s) {
	if((s = write(logfd, leader + l, n - l)) < 0) {
	    error("log file write error: %s", strerror(errno));
	}
    }

    amfree(leader);

    n = strlen(linebuf);
    if(n == 0 || linebuf[n-1] != '\n') linebuf[n++] = '\n';
    linebuf[n] = '\0';

    for(l = 0; l < n; l += s) {
	if((s = write(logfd, linebuf + l, n - l)) < 0) {
	    error("log file write error: %s", strerror(errno));
	}
    }

    if(multiline != -1) multiline++;
    else close_log();

    erroutput_type = saved_errout;
}

void log_start_multiline()
{
    assert(multiline == -1);

    multiline = 0;
    open_log();
}


void log_end_multiline()
{
    assert(multiline != -1);
    multiline = -1;
    close_log();
}


void log_rename(datestamp)
char *datestamp;
{
    char *logfile;
    char *fname = NULL;
    char seq_str[NUM_STR_SIZE];
    unsigned int seq;
    struct stat statbuf;

    if(datestamp == NULL) datestamp = "error";

    logfile = vstralloc(getconf_str(CNF_LOGDIR), "/log", NULL);

    for(seq = 0; 1; seq++) {	/* if you've got MAXINT files in your dir... */
	ap_snprintf(seq_str, sizeof(seq_str), "%d", seq);
	fname = newvstralloc(fname,
			     logfile,
			     ".", datestamp,
			     ".", seq_str,
			     NULL);
	if(stat(fname, &statbuf) == -1 && errno == ENOENT) break;
    }

    if(rename(logfile, fname) == -1)
	error("could not rename log file to `%s': %s", fname, strerror(errno));

    amfree(fname);
    amfree(logfile);
}


static void open_log()
{
    logfile = vstralloc(getconf_str(CNF_LOGDIR), "/log", NULL);

    logfd = open(logfile, O_WRONLY|O_CREAT|O_APPEND, 0600);

    if(logfd == -1)
	error("could not open log file %s: %s", logfile, strerror(errno));

    if(amflock(logfd, "log") == -1)
	error("could not lock log file %s: %s", logfile, strerror(errno));
}


static void close_log()
{
    if(amfunlock(logfd, "log") == -1)
	error("could not unlock log file %s: %s", logfile, strerror(errno));

    if(close(logfd) == -1)
	error("close log file: %s", strerror(errno));

    logfd = -1;
    amfree(logfile);
}


int get_logline(logf)
FILE *logf;
{
    static char *logline = NULL;
    char *logstr, *progstr;
    char *s;
    int ch;

    amfree(logline);
    if((logline = agets(logf)) == NULL) return 0;
    curlinenum++;
    s = logline;
    ch = *s++;

    /* continuation lines are special */

    if(logline[0] == ' ' && logline[1] == ' ') {
	curlog = L_CONT;
	/* curprog stays the same */
	skip_whitespace(s, ch);
	curstr = s-1;
	return 1;
    }

    /* isolate logtype field */

    skip_whitespace(s, ch);
    logstr = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    /* isolate program name field */

    skip_whitespace(s, ch);
    progstr = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    /* rest of line is logtype dependent string */

    skip_whitespace(s, ch);
    curstr = s - 1;

    /* lookup strings */

    for(curlog = L_MARKER; curlog != L_BOGUS; curlog--)
	if(strcmp(logtype_str[curlog], logstr) == 0) break;

    for(curprog = P_LAST; curprog != P_UNKNOWN; curprog--)
	if(strcmp(program_str[curprog], progstr) == 0) break;

    return 1;
}
