/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1999 University of Maryland at College Park
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
 * Authors: the Amanda Development Team.  Its members are listed in a
 * file named AUTHORS, in the root directory of this distribution.
 */
/*
 * $Id: server_util.h,v 1.1.2.1.4.2.2.2 2002/04/13 19:24:17 jrjackson Exp $
 *
 */
#ifndef SERVER_UTIL_H
#define	SERVER_UTIL_H

#include "util.h"

#define MAX_ARGS 32

typedef enum {
    BOGUS, QUIT, QUITTING, DONE,
    FILE_DUMP, PORT_DUMP, CONTINUE, ABORT,		/* dumper cmds */
    FAILED, TRYAGAIN, NO_ROOM, RQ_MORE_DISK,		/* dumper results */
    ABORT_FINISHED, FATAL_TRYAGAIN, BAD_COMMAND,	/* dumper results */
    START_TAPER, FILE_WRITE, PORT_WRITE,		/* taper cmds */
    PORT, TAPE_ERROR, TAPER_OK,				/* taper results */
    LAST_TOK
} cmd_t;
extern const char *cmdstr[];

struct cmdargs {
    int argc;
    char *argv[MAX_ARGS + 1];
};

cmd_t getcmd P((struct cmdargs *cmdargs));
void putresult P((cmd_t result, const char *, ...))
     __attribute__ ((format (printf, 2, 3)));

#endif	/* SERVER_UTIL_H */
