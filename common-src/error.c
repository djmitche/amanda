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
 *  error.c  - error handling common to Amanda programs
 */
#include "amanda.h"
#include "logfile.h"
#include "arglist.h"

#define MAXFUNCS 8

typedef void (*voidfunc) P((void));
static voidfunc onerr[MAXFUNCS] = 
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

int erroutput_type = ERR_INTERACTIVE;


arglist_function(void error, char *, format)
/*
 * Prints an error message, calls the functions installed via onerror(),
 * then exits.
 */
{
    va_list argp;
    extern char *pname;
    int i;
    char linebuf[1024];

    /* format error message */

    arglist_start(argp, format);
    vsprintf(linebuf, format, argp);
    arglist_end(argp);

    /* print and/or log message */

    if(erroutput_type & ERR_AMANDALOG)	
	log(L_FATAL, "%s", linebuf);
    if(erroutput_type & ERR_SYSLOG) {
#ifdef LOG_AUTH
	openlog(pname, LOG_PID, LOG_AUTH);
#else
	openlog(pname, LOG_PID);
#endif
	syslog(LOG_NOTICE, "%s", linebuf);
	closelog();
    }
    if(erroutput_type & ERR_INTERACTIVE) {
	fprintf(stderr, "%s: %s\n", pname, linebuf);
	fflush(stderr);
    }

    /* traverse function list, calling in reverse order */

    for(i=MAXFUNCS-1; i >= 0; i--) {
	if(onerr[i] != NULL) (*onerr[i])();
    }
    exit(1);
}


int onerror(errf)
void (*errf) P((void));
/*
 * Register function to be called when error is called.  Up to MAXFUNCS
 * functions can be registered.  If there isn't room in the table, onerror
 * returns -1, otherwise it returns 0.
 *
 * The resemblance to atexit() is on purpose.  I wouldn't need onerror()
 * if everyone had atexit().  Bummer.
 */
{
    int i;

    for(i=0; i < MAXFUNCS; i++)		/* find empty slot */
	if(onerr[i] == NULL) {
	    onerr[i] = errf;
	    return 0;
	}

    return -1;				/* full table */
}
