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
/* $Id: amidxtaped.c,v 1.11 1998/01/01 01:13:42 jrj Exp $
 *
 * This daemon extracts a dump image off a tape for amrecover and
 * returns it over the network. It basically, reads a number of
 * arguments from stdin (it is invoked via inet), one per line,
 * preceeded by the number of them, and forms them into an argv
 * structure, then execs amrestore
 */

#include "amanda.h"
#include "version.h"

char *pname = "amidxtaped";

static char *get_client_line P((void));

/* get a line from client - line terminated by \r\n */
static char *
get_client_line()
{
    static char *line = NULL;
    char *part = NULL;
    int len;

    afree(line);
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
	    afree(line);
	    afree(part);
	    dbclose();
	    exit(1);
	    /* NOTREACHED */
	}
	if(line) {
	    strappend(line, part);
	    afree(part);
	} else {
	    line = part;
	    part = NULL;
	}
	if((len = strlen(line)) > 0 && line[len-1] == '\r') {
	    line[len-1] = '\0';		/* zap the '\r' */
	    break;
	}
	/*
	 * Hmmm.  We got a "line" from areads(), which means it saw
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
#ifdef FORCE_USERID
    char *pwname;
    struct passwd *pwptr;
#endif	/* FORCE_USERID */
    int amrestore_nargs;
    char **amrestore_args;
    char *buf = NULL;
    int i;
    char *amrestore_path;
    pid_t pid;
    int isafile;
    struct stat stat_tape;
    char *tapename;

#ifdef FORCE_USERID

    /* we'd rather not run as root */

    if(geteuid() == 0) {
	pwname = CLIENT_LOGIN;
	if((pwptr = getpwnam(pwname)) == NULL)
	    error("error [cannot find user %s in passwd file]\n", pwname);

	initgroups(pwname, pwptr->pw_gid);
	setgid(pwptr->pw_gid);
	setuid(pwptr->pw_uid);
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
#else
    if ((i = open("/dev/null", O_WRONLY)) != STDERR_FILENO)
    {
	perror("amidxtaped can't redirect stderr");
	return 1;
    }
#endif

    /* get the number of arguments */
    afree(buf);
    buf = get_client_line();
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
	dbprintf(("Child couldn't exec amrestore\n"));
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
    if (waitpid(pid, &i, 0) == -1)
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
    if (WIFEXITED(i) != 0)
    {

	dbprintf(("amidxtaped: amrestore terminated normally with status: %d\n",
		  WEXITSTATUS(i)));
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
    if (stat(tapename, &stat_tape) != 0)
      error("could not stat %s", tapename);
    isafile = S_ISREG((stat_tape.st_mode));
    if (!isafile) {
	char *cmd;

	cmd = vstralloc("mt",
#ifdef MT_FILE_FLAG
			" ", MT_FILE_FLAG,
#else
			" -f",
#endif
			" ", tapename,
			" ", "rewind",
			NULL);
	dbprintf(("Rewinding tape: %s\n", cmd));
	system(cmd);
	afree(cmd);
    }
    afree(tapename);
    dbclose();
    return 0;
}
