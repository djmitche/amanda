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
 * $Id: server_util.c,v 1.5 2001/03/05 23:52:39 martinea Exp $
 *
 */

#include "amanda.h"
#include "server_util.h"
#include "arglist.h"

const char *cmdstr[] = {
    "BOGUS", "QUIT", "QUITTING", "DONE",
    "FILE-DUMP", "PORT-DUMP", "CONTINUE", "ABORT",	/* dumper cmds */
    "FAILED", "TRY-AGAIN", "NO-ROOM", "RQ-MORE-DISK",	/* dumper results */
    "ABORT-FINISHED", "FAIL-OUTPUT", "BAD-COMMAND",	/* dumper results */
    "START-TAPER", "FILE-WRITE", "PORT-WRITE",		/* taper cmds */
    "PORT", "TAPE-ERROR", "TAPER-OK",			/* taper results */
    NULL
};

char *construct_datestamp()
{
    struct tm *tm;
    char datestamp[3*NUM_STR_SIZE];
    time_t today;

    today = time((time_t *)NULL);
    tm = localtime(&today);
    snprintf(datestamp, sizeof(datestamp),
                "%04d%02d%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
    return stralloc(datestamp);
}


cmd_t getcmd(cmdargs)
struct cmdargs *cmdargs;
{
    char *line;
    int i;

    assert(cmdargs != NULL);

    if (isatty(0)) {
	printf("%s> ", get_pname());
	fflush(stdout);
    }

    if ((line = agets(stdin)) == NULL)
	return (QUIT);

    cmdargs->argc = split(line, cmdargs->argv,
	sizeof(cmdargs->argv) / sizeof(cmdargs->argv[0]), " ");
    amfree(line);

#if DEBUG
    printf("argc = %d\n", cmdargs->argc);
    for (i = 0; i < cmdargs->argc; i++)
	printf("argv[%d] = \"%s\"\n", i, cmdargs->argv[i]);
#endif

    if (cmdargs->argc < 1)
	return (BOGUS);

    for(i=0; cmdstr[i] != NULL; i++)
	if(strcmp(cmdargs->argv[1], cmdstr[i]) == 0)
	    return (i);
    return (BOGUS);
}


arglist_function1(void putresult, cmd_t, result, const char *, format)
{
    va_list argp;

    arglist_start(argp, format);
    printf("%s ",cmdstr[result]);
    vprintf(format, argp);
    fflush(stdout);
    arglist_end(argp);
}


