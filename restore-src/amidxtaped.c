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
 * Authors: the Amanda Development Team.  Its members are listed in a
 * file named AUTHORS, in the root directory of this distribution.
 */
/* $Id: amidxtaped.c,v 1.25.2.3.2.1 2001/05/15 21:02:13 jrjackson Exp $
 *
 * This daemon extracts a dump image off a tape for amrecover and
 * returns it over the network. It basically, reads a number of
 * arguments from stdin (it is invoked via inet), one per line,
 * preceeded by the number of them, and forms them into an argv
 * structure, then execs amrestore
 */

#include "amanda.h"
#include "version.h"

#include "tapeio.h"

static char *get_client_line P((void));

/* get a line from client - line terminated by \r\n */
static char *
get_client_line()
{
    static char *line = NULL;
    char *part = NULL;
    int len;

    amfree(line);
    while(1) {
	if((part = agets(stdin)) == NULL) {
	    if(errno != 0) {
		dbprintf(("read error: %s\n", strerror(errno)));
	    } else {
		dbprintf(("EOF reached\n"));
	    }
	    if(line) {
		dbprintf(("unprocessed input:\n"));
		dbprintf(("-----\n"));
		dbprintf(("%s\n", line));
		dbprintf(("-----\n"));
	    }
	    amfree(line);
	    amfree(part);
	    dbclose();
	    exit(1);
	    /* NOTREACHED */
	}
	if(line) {
	    strappend(line, part);
	    amfree(part);
	} else {
	    line = part;
	    part = NULL;
	}
	if((len = strlen(line)) > 0 && line[len-1] == '\r') {
	    line[len-1] = '\0';		/* zap the '\r' */
	    break;
	}
	/*
	 * Hmmm.  We got a "line" from agets(), which means it saw
	 * a '\n' (or EOF, etc), but there was not a '\r' before it.
	 * Put a '\n' back in the buffer and loop for more.
	 */
	strappend(line, "\n");
    }
    dbprintf(("> %s\n", line));
    return line;
}

int main(argc, argv)
int argc;
char **argv;
{
    int amrestore_nargs;
    char **amrestore_args;
    char *buf = NULL;
    int i;
    char *amrestore_path;
    pid_t pid;
    int isafile;
    struct stat stat_tape;
    char *tapename = NULL;
    int fd;
    char *s, *fp;
    int ch;
    char *errstr = NULL;
    struct sockaddr_in addr;
    amwait_t status;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    safe_cd();

    set_pname("amidxtaped");

#ifdef FORCE_USERID

    /* we'd rather not run as root */

    if(geteuid() == 0) {
	if(client_uid == (uid_t) -1) {
	    error("error [cannot find user %s in passwd file]\n", CLIENT_LOGIN);
	}

	initgroups(CLIENT_LOGIN, client_gid);
	setgid(client_gid);
	setuid(client_uid);
    }

#endif	/* FORCE_USERID */

    /* initialize */
    /* close stderr first so that debug file becomes it - amrestore
       chats to stderr, which we don't want going to client */
    /* if no debug file, ship to bit bucket */
    (void)close(STDERR_FILENO);
#ifdef DEBUG_CODE
    dbopen();
    dbprintf(("%s: version %s\n", argv[0], version()));
    if(dbfd() != -1 && dbfd() != STDERR_FILENO)
    {
	if(dup2(dbfd(),STDERR_FILENO) != STDERR_FILENO)
	{
	    perror("amidxtaped can't redirect stderr to the debug file");
	    dbprintf(("amidxtaped can't redirect stderr to the debug file"));
	    return 1;
	}
    }
#else
    if ((i = open("/dev/null", O_WRONLY)) == -1 ||
	(i != STDERR_FILENO &&
	 (dup2(i, STDERR_FILENO) != STDERR_FILENO ||
	  close(i) != 0))) {
	perror("amidxtaped can't redirect stderr");
	return 1;
    }
#endif

    i = sizeof (addr);
    if (getpeername(0, (struct sockaddr *)&addr, &i) == -1)
	error("getpeername: %s", strerror(errno));
    if (addr.sin_family != AF_INET || ntohs(addr.sin_port) == 20) {
	error("connection rejected from %s family %d port %d",
	      inet_ntoa(addr.sin_addr), addr.sin_family, htons(addr.sin_port));
    }

    /* do the security thing */
    amfree(buf);
    buf = stralloc(get_client_line());
    s = buf;
    ch = *s++;

    skip_whitespace(s, ch);
    if (ch == '\0')
    {
	error("cannot parse SECURITY line");
    }
    fp = s-1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    if (strcmp(fp, "SECURITY") != 0)
    {
	error("cannot parse SECURITY line");
    }
    skip_whitespace(s, ch);
    if (!security_ok(&addr, s-1, 0, &errstr)) {
	error("security check failed: %s", errstr);
    }

    /* get the number of arguments */
    amfree(buf);
    buf = stralloc(get_client_line());
    amrestore_nargs = atoi(buf);
    dbprintf(("amrestore_nargs=%d\n", amrestore_nargs));

    if ((amrestore_args = (char **)malloc((amrestore_nargs+2)*sizeof(char *)))
	== NULL) 
    {
	dbprintf(("Couldn't malloc amrestore_args\n"));
	dbclose();
	return 1;
    }

    i = 0;
    amrestore_args[i++] = "amrestore";
    while (i <= amrestore_nargs)
    {
	amrestore_args[i++] = stralloc(get_client_line());
    }
    amrestore_args[i] = NULL;

    amrestore_path = vstralloc(sbindir, "/", "amrestore", NULL);

    /* so got all the arguments, now ready to execv */
    dbprintf(("Ready to execv amrestore with:\n"));
    dbprintf(("path = %s\n", amrestore_path));
    for (i = 0; amrestore_args[i] != NULL; i++)
    {
	dbprintf(("argv[%d] = \"%s\"\n", i, amrestore_args[i]));
    }

    if ((pid = fork()) == 0)
    {
	/* child */
	(void)execv(amrestore_path, amrestore_args);

	/* only get here if exec failed */
	dbprintf(("Child could not exec %s: %s\n",
		  amrestore_path,
		  strerror(errno)));
	return 1;
	/*NOT REACHED*/
    }

    /* this is the parent */
    if (pid == -1)
    {
	dbprintf(("Error forking child: %s\n", strerror(errno)));
	dbclose();
	return 1;
    }

    /* wait for the child to do the restore */
    if (waitpid(pid, &status, 0) == -1)
    {
	dbprintf(("Error waiting for child"));
	dbclose();
	return 1;
    }
    /* amrestore often sees the pipe reader (ie restore) quit in the middle
       of the file because it has extracted all of the files needed. This
       results in an exit status of 2. This unfortunately is the exit
       status returned by many errors. Only when the exit status is 1 is it
       guaranteed that an error existed. In all cases we should rewind the
       tape if we can so that a retry starts from the correct place */
    if (WIFEXITED(status) != 0)
    {

	dbprintf(("amidxtaped: amrestore terminated normally with status: %d\n",
		  WEXITSTATUS(status)));
    }
    else
    {
	dbprintf(("amidxtaped: amrestore terminated abnormally.\n"));
    }

    /* rewind tape */
    /* the first non-option argument is the tape device */
    for (i = 1; i <= amrestore_nargs; i++)
	if (amrestore_args[i][0] != '-')
	    break;
    if (i > amrestore_nargs)
    {
	dbprintf(("Couldn't find tape in arguments\n"));
	dbclose();
	return 1;
    }

    tapename = stralloc(amrestore_args[i]);
    if (tape_stat(tapename, &stat_tape) != 0) {
        error("could not stat %s", tapename);
    }
    isafile = S_ISREG((stat_tape.st_mode));
    if (!isafile) {
	char *errstr = NULL;

	dbprintf(("Rewinding tape: "));
	errstr = tape_rewind(tapename);

	if (errstr != NULL) {
	    dbprintf(("%s\n", errstr));
	    amfree(errstr);
	} else {
	    dbprintf(("done\n"));
	}
    }
    amfree(tapename);
    dbclose();
    return 0;
}
