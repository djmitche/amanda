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
 * $Id: debug.c,v 1.17.4.3.4.1 2001/03/20 00:25:22 jrjackson Exp $
 *
 * debug log subroutines
 */

#include "amanda.h"
#include "util.h"
#include "arglist.h"

int debug = 1;

#define	MIN_DB_FD			10

#ifdef DEBUG_CODE
int db_fd = 2;				/* default is stderr */
#else
int db_fd = -1;				/* default is to throw away */
#endif
static FILE *db_file = NULL;		/* stderr may not be a constant */

#ifndef AMANDA_DBGDIR
#  define AMANDA_DBGDIR		AMANDA_TMPDIR
#endif

arglist_function(void debug_printf, char *, format)
/*
 * Formats and writes a debug message to the process's debug file.
 */
{
    va_list argp;

    if(db_file == NULL && db_fd == 2) {
	db_file = stderr;
    }
    if(db_file != NULL) {
	arglist_start(argp, format);
	vfprintf(db_file, format, argp);
	fflush(db_file);
	arglist_end(argp);
    }
}

/*
 * Generate a debug file name.  The name is based on the program name,
 * followed by a timestamp, an optional sequence number, and ".debug".
 */
static char *
get_debug_name(t, n)
    time_t t;
    int n;
{
    char number[NUM_STR_SIZE];
    char *ts;
    char *result;

    if(n < 0 || n > 1000) {
	return NULL;
    }
    ts = construct_timestamp(&t);
    if(n == 0) {
	number[0] = '\0';
    } else {
	ap_snprintf(number, sizeof(number), "%03d", n - 1);
    }
    result = vstralloc(get_pname(), ".", ts, number, ".debug", NULL);
    amfree(ts);
    return result;
}

void debug_open()
{
    time_t curtime;
    int saved_debug;
    char *dbgdir = NULL;
    char *e = NULL;
    char *s = NULL;
    char *dbfilename = NULL;
    DIR *d;
    struct dirent *entry;
    char *pname;
    int pname_len;
    int do_rename;
    char *test_name = NULL;
    int test_name_len;
    int fd = -1;
    int i;
    int fd_close[MIN_DB_FD+1];
    struct passwd *pwent;
    struct stat sbuf;

    pname = get_pname();
    pname_len = strlen(pname);

    if(client_uid == (uid_t) -1 && (pwent = getpwnam(CLIENT_LOGIN)) != NULL) {
	client_uid = pwent->pw_uid;
	client_gid = pwent->pw_gid;
    }

    /*
     * Create the debug directory if it does not yet exist.
     */
    dbgdir = stralloc2(AMANDA_DBGDIR, "/");
    if(mkpdir(dbgdir, 02700, client_uid, client_gid) == -1) {
        error("create debug directory \"%s\": %s",
	      AMANDA_DBGDIR, strerror(errno));
    }

    /*
     * Clean out old debug files.  We also rename files with old style
     * names (XXX.debug or XXX.$PID.debug) into the new name format.
     * We assume no system has 17 digit PID-s :-) and that there will
     * not be a conflict between an old and new name.
     */
    if((d = opendir(AMANDA_DBGDIR)) == NULL) {
        error("open debug directory \"%s\": %s",
	      AMANDA_DBGDIR, strerror(errno));
    }
    time(&curtime);
    test_name = get_debug_name(curtime - (AMANDA_DEBUG_DAYS * 24 * 60 * 60), 0);
    test_name_len = strlen(test_name);
    while((entry = readdir(d)) != NULL) {
	if(is_dot_or_dotdot(entry->d_name)) {
	    continue;
	}
	if(strncmp(entry->d_name, pname, pname_len) != 0
	   || entry->d_name[pname_len] != '.') {
	    continue;				/* not one of our files */
	}
	e = newvstralloc(e, dbgdir, entry->d_name, NULL);
	if(strlen(entry->d_name) < test_name_len) {
	    /*
	     * Create a "pretend" name based on the last modification
	     * time.  This name will be used to decide if the real name
	     * should be removed.  If not, it will be used to rename the
	     * real name.
	     */
	    if(stat(e, &sbuf) != 0) {
		continue;			/* ignore errors */
	    }
	    amfree(dbfilename);
	    dbfilename = get_debug_name((time_t)sbuf.st_mtime, 0);
	    do_rename = 1;
	} else {
	    dbfilename = newstralloc(dbfilename, entry->d_name);
	    do_rename = 0;
	}
	if(strcmp(dbfilename, test_name) < 0) {
	    (void) unlink(e);			/* get rid of old file */
	    continue;
	}
	if(do_rename) {
	    i = 0;
	    while(dbfilename != NULL
		  && (s = newvstralloc(s, dbgdir, dbfilename, NULL)) != NULL
		  && rename(e, s) != 0) {
		amfree(dbfilename);
		dbfilename = get_debug_name((time_t)sbuf.st_mtime, ++i);
	    }
	    if(dbfilename == NULL) {
		error("cannot rename old debug file \"%s\"", entry->d_name);
	    }
	}
    }
    amfree(dbfilename);
    amfree(s);
    amfree(test_name);
    closedir(d);

    /*
     * Create the new file.
     */
    for(i = 0;
	(dbfilename = get_debug_name(curtime, i)) != NULL
	&& (s = newvstralloc(s, dbgdir, dbfilename, NULL)) != NULL
	&& (fd = open(s, O_WRONLY|O_CREAT|O_EXCL|O_APPEND, 0600)) < 0;
	i++, free(dbfilename)) {}
    if(dbfilename == NULL) {
	error("cannot create %s debug file", get_pname());
    }
    (void) chown(s, client_uid, client_gid);
    amfree(dbgdir);
    amfree(dbfilename);
    amfree(s);

    /*
     * Move the file descriptor up high so it stays out of the way
     * of other processing, e.g. sendbackup.
     */
    i = 0;
    fd_close[i++] = fd;
    while((db_fd = dup(fd)) < MIN_DB_FD) {
	fd_close[i++] = db_fd;
    }
    while(--i >= 0) {
	close(fd_close[i]);
    }
    db_file = fdopen(db_fd, "a");

    /*
     * Make the first debug log file entry.
     */
    saved_debug = debug; debug = 1;
    debug_printf("%s: debug %d pid %ld ruid %ld euid %ld start time %s",
		 pname, saved_debug, (long)getpid(),
		 (long)getuid(), (long)geteuid(),
		 ctime(&curtime));
    debug = saved_debug;
}

void debug_close()
{
    time_t curtime;

    time(&curtime);
    debug = 1;
    debug_printf("%s: pid %ld finish time %s", get_pname(), (long)getpid(),
		 ctime(&curtime));

    if(fclose(db_file) == EOF)
	error("close debug file: %s", strerror(errno));
    db_fd = -1;
    db_file = NULL;
}

int debug_fd()
{
    return db_fd;
}

FILE *debug_fp()
{
    return db_file;
}
