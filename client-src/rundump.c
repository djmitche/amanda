/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1996 University of Maryland at College Park
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
 * $Id: rundump.c,v 1.9 1997/09/19 02:37:55 george Exp $
 *
 * runs DUMP program as root
 */
#include "amanda.h"
#include "version.h"

char *pname = "rundump";

int main P((int argc, char **argv));

int main(argc, argv)
int argc;
char **argv;
{
    char *dump_program;
#ifdef USE_RUNDUMP
    char *noenv[1];
    int i;
    noenv[0] = (char *)0;
#endif /* USE_RUNDUMP */

    dbopen("/tmp/rundump.debug");
    dbprintf(("%s: version %s\n", argv[0], version()));

#if (!defined(DUMP) && !defined(XFSDUMP)) && !defined(VXDUMP) \
    || !defined(USE_RUNDUMP)

#if !defined(USE_RUNDUMP)
#define ERRMSG "rundump not enabled on this system.\n"
#else
#define ERRMSG "DUMP not available on this system.\n"
#endif

    fprintf(stderr, ERRMSG);
    dbprintf(("%s: %s", argv[0], ERRMSG));
    dbclose();
    return 1;

#else

    /* we should be invoked by CLIENT_LOGIN */
    {
	struct passwd *pwptr;
	char *pwname = CLIENT_LOGIN;
	if((pwptr = getpwnam(pwname)) == NULL)
	    error("error [cannot find user %s in passwd file]\n", pwname);

	chown("/tmp/rundump.debug", pwptr->pw_uid, getgid());

#ifdef FORCE_USERID
	if (getuid() != pwptr->pw_uid)
	    error("error [must be invoked by %s]\n", pwname);

	if (geteuid() != 0)
	    error("error [must be setuid root]\n");
#endif	/* FORCE_USERID */
    }

#ifdef XFSDUMP
    
    if (strcmp(argv[0], "xfsdump") == 0)
        dump_program = XFSDUMP;
    else /* strcmp(argv[0], "xfsdump") != 0 */

#endif

#ifdef VXDUMP

    if (strcmp(argv[0], "vxdump") == 0)
        dump_program = VXDUMP;
    else /* strcmp(argv[0], "vxdump") != 0 */

#endif

#if defined(DUMP)
        dump_program = DUMP;
#elif defined(XFSDUMP)
        dump_program = XFSDUMP;
#elif defined(VXDUMP)
	dump_program = VXDUMP;
#else
        dump_program = "dump";
#endif

    dbprintf(("running: %s: ",dump_program));
    for (i=0; argv[i]; i++)
	dbprintf(("%s ", argv[i]));
    dbprintf(("\n"));

    execve(dump_program, argv, noenv);

    dbprintf(("failed (errno=%d)\n",errno));
    dbclose();

    fprintf(stderr, "rundump: could not exec %s: %s\n",
	    dump_program, strerror(errno));
    return 1;
#endif
}
