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
 * $Id: debug.c,v 1.8 1997/11/07 10:08:30 george Exp $
 *
 * debug log subroutines
 */

#include "amanda.h"
#include "arglist.h"

int debug = 1;
extern char *pname;

#ifdef DEBUG_CODE
  int db_file = -1;
#else
  int db_file = 2;
#endif

#ifndef DEBUG_DIR
#  define DEBUG_DIR "/tmp"
#endif

arglist_function(void debug_printf, char *, format)
/*
 * Formats and writes a debug message to the process's debug file.
 */
{
    va_list argp;
    char linebuf[16384];

    if(db_file == -1) return;

    /* format error message */

    arglist_start(argp, format);
    vsprintf(linebuf, format, argp);
    arglist_end(argp);

    /*
     * write it with no stdio buffering, since we want the output
     * to appear immediately.
     */

    write(db_file, linebuf, strlen(linebuf));

}

void debug_open()
{
    time_t curtime;
    int saved_debug;
    int maxtries;
    char dbfilename[256];
    struct passwd *pwent;
    uid_t uid;
    gid_t gid;

    if((pwent = getpwnam(CLIENT_LOGIN)) != NULL) {
	uid = pwent->pw_uid;
	gid = pwent->pw_gid;
    }
    else {
	uid = (uid_t)-1;
	gid = (gid_t)-1;
    }

#ifdef DEBUG_FILE_WITH_PID
    sprintf(dbfilename, "%s/%s.%ld.debug", DEBUG_DIR, pname, (long)getpid());
#else
    sprintf(dbfilename, "%s/%s.debug", DEBUG_DIR, pname);
#endif

    if(mkpdir(dbfilename, 0700, uid, gid) == -1)
        error("open debug file \"%s\": %s", dbfilename, strerror(errno));

    maxtries = 50;
    do {
	if (--maxtries == 0)
	    error("open debug file \"%s\": %s", dbfilename, strerror(errno));
	unlink(dbfilename);
	db_file = open(dbfilename, O_WRONLY|O_CREAT|O_EXCL|O_APPEND, 0600);
    } while(db_file == -1);

    chown(dbfilename, uid, gid);

    time(&curtime);
    saved_debug = debug; debug = 1;
    debug_printf("%s: debug %d pid %ld ruid %ld euid %ld start time %s",
		 pname, debug, (long)getpid(), (long)getuid(),
		 (long)geteuid(), ctime(&curtime));
    debug = saved_debug;
}

void debug_close()
{
    time_t curtime;

    time(&curtime);
    debug = 1;
    debug_printf("%s: pid %ld finish time %s", pname, (long)getpid(),
		 ctime(&curtime));

    if(close(db_file) == -1)
	error("close debug file: %s", strerror(errno));
}
