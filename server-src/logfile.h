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
 * $Id: logfile.h,v 1.6.8.1.2.1 2002/02/10 03:31:53 jrjackson Exp $
 *
 * interface to logfile module
 */
#ifndef LOGFILE_H
#define LOGFILE_H

#include "amanda.h"

/*
 * L_FAIL is defined on m88k-motorola-sysv4 systems for multiprocessor
 * support, which we don't need, so it gets undefined here.
 */
#undef L_FAIL

typedef enum logtype_e {
    L_BOGUS,
    L_FATAL,		/* program died for some reason, used by error() */
    L_ERROR, L_WARNING,	L_INFO, L_SUMMARY,	 /* information messages */
    L_START, L_FINISH,				     /* start/end of run */
    L_DISK,							 /* disk */
    L_SUCCESS, L_FAIL, L_STRANGE,		    /* the end of a dump */
    L_STATS,						   /* statistics */
    L_MARKER,					  /* marker for reporter */
    L_CONT			 /* continuation line, used when reading */
} logtype_t;

typedef enum program_e {
    P_UNKNOWN, P_PLANNER, P_DRIVER, P_REPORTER, P_DUMPER, P_TAPER, P_AMFLUSH
} program_t;
#define P_LAST P_AMFLUSH

extern char *logtype_str[];

extern int curlinenum;
extern logtype_t curlog;
extern program_t curprog;
extern char *curstr;
extern char *program_str[];

void logerror P((char *));
void log_add P((logtype_t typ, char * format, ...))
    __attribute__ ((format (printf, 2, 3)));
void log_start_multiline P((void));
void log_end_multiline P((void));
void log_rename P((char *datestamp));
int get_logline P((FILE *));

#endif  /* ! LOGFILE_H */
