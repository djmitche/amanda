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
 * $Id: error.c,v 1.16 2002/03/31 21:02:00 jrjackson Exp $
 *
 * error handling common to Amanda programs
 */
#include "amanda.h"
#include "arglist.h"

#define MAXFUNCS 8

typedef void (*voidfunc) P((void));
static voidfunc onerr[MAXFUNCS] = 
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

int erroutput_type = ERR_INTERACTIVE;

static char *pname = "unknown";

static void (*logerror) P((char *)) = NULL;

void set_pname(p)
char *p;
{
    pname = p;
}

char *get_pname()
{
    return pname;
}

void set_logerror(f)
void (*f) P((char *));
{
    logerror = f;
}


static void output_error_message(msg)
char *msg;
{
    /* print and/or log message */

    if((erroutput_type & ERR_AMANDALOG) != 0 && logerror != NULL) {
	(*logerror)(msg);
    }

    if(erroutput_type & ERR_SYSLOG) {
#ifdef LOG_AUTH
	openlog(get_pname(), LOG_PID, LOG_AUTH);
#else
	openlog(get_pname(), LOG_PID);
#endif
	syslog(LOG_NOTICE, "%s", msg);
	closelog();
    }

    if(erroutput_type & ERR_INTERACTIVE) {
	/*
	 * If the message starts with "ERROR [" it should be passed back
	 * to the caller as is.  Otherwise we prepend our program name.
	 */
#define sc "ERROR ["
	if(strncmp(msg, sc, sizeof(sc)-1) != 0) {
#undef sc
	    fprintf(stderr, "%s: ", get_pname());
	}
	fprintf(stderr, "%s\n", msg);
	fflush(stderr);
    }

    if(dbfp() != NULL) {
	dbprintf(("%s: %s\n", debug_prefix_time(NULL), msg));
	dbclose();
    }
}


/*
 * Prints an error message, calls the functions installed via onerror(),
 * then exits.
 */
printf_arglist_function(void error, const char *, format)
{
    va_list argp;
    int i;
    char linebuf[STR_SIZE];


    /* format and output the error message */

    arglist_start(argp, format);
    vsnprintf(linebuf, sizeof(linebuf), format, argp);
    arglist_end(argp);
    output_error_message(linebuf);

    /* traverse function list, calling in reverse order */

    for(i=MAXFUNCS-1; i >= 0; i--) {
	if(onerr[i] != NULL) (*onerr[i])();
    }

    /* terminate */
    exit(1);
}


/*
 * Prints an error message, calls the functions installed via onerror(),
 * then calls abort() to drop core.
 */
printf_arglist_function(void errordump, const char *, format)
{
    va_list argp;
    int i;
    char linebuf[STR_SIZE];

    /* format error message */

    arglist_start(argp, format);
    vsnprintf(linebuf, sizeof(linebuf), format, argp);
    arglist_end(argp);
    output_error_message(linebuf);

    /* traverse function list, calling in reverse order */

    for(i=MAXFUNCS-1; i >= 0; i--) {
	if(onerr[i] != NULL) (*onerr[i])();
    }

    /* terminate and drop core */
    abort();
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
