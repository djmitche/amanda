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
 *  logfile.c - common log file writing routine
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

int multiline = -1;
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
    log(L_FATAL, "%s", msg);
}

arglist_function(void log, logtype_t, typ)
{
    va_list argp;
    char *format;
    extern char *pname;
    int rc, len, saved_errout;
    char linebuf[1024];


    /* format error message */

    if((int)typ <= (int)L_BOGUS || (int)typ > (int)L_MARKER) typ = L_BOGUS;

    if(multiline > 0) strcpy(linebuf, "  ");	/* continuation line */
    else sprintf(linebuf, "%s %s ", logtype_str[(int)typ], pname);

    arglist_start(argp, typ);
    format = arglist_val(argp, char *);
    vsprintf(linebuf+strlen(linebuf), format, argp);
    arglist_end(argp);

    len = strlen(linebuf);
    linebuf[len++] = '\n';
    linebuf[len]   = '\0';

    /* avoid recursive call from error() */

    saved_errout = erroutput_type;
    erroutput_type &= ~ERR_AMANDALOG;

    /* append message to the log file */

    if(multiline == -1) open_log();

    if((rc = write(logfd, linebuf, len)) < len)
	error("short write to log file: %s", strerror(errno));

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
    char fname[1024];
    unsigned int seq;
    struct stat statbuf;

    for(seq = 0; 1; seq++) {	/* if you've got MAXINT files in your dir... */
	sprintf(fname, "%s.%s.%d", getconf_str(CNF_LOGFILE), datestamp, seq);
	if(stat(fname, &statbuf) == -1 && errno == ENOENT) break;
    }
    if(rename(getconf_str(CNF_LOGFILE), fname) == -1)
	error("could not rename log file to `%s': %s", fname, strerror(errno));
}


static void open_log()
{
    logfd = open(getconf_str(CNF_LOGFILE), O_WRONLY|O_CREAT|O_APPEND, 0666);
    if(logfd == -1)
	error("could not open log file %s: %s",
	      getconf_str(CNF_LOGFILE),strerror(errno));
    if(amflock(logfd, "log") == -1)
	error("could not lock log file %s: %s", getconf_str(CNF_LOGFILE),
	      strerror(errno));
}


static void close_log()
{
    if(amfunlock(logfd, "log") == -1)
	error("could not unlock log file %s: %s", getconf_str(CNF_LOGFILE),
	      strerror(errno));
    if(close(logfd) == -1)
	error("close log file: %s", strerror(errno));
}
