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
/* $Id: amidxtaped.c,v 1.5 1997/09/19 02:38:05 george Exp $
 *
 * This daemon extracts a dump image off a tape for amrecover and
 * returns it over the network. It basically, reads a number of
 * arguments from stdin (it is invoked via inet), one per line,
 * preceeded by the number of them, and forms them into an argv
 * structure, then execs amrestore
 */

#include "amanda.h"
#include "version.h"

#define BUF_LEN 1024
char *pname = "amidxtaped";

/* get a line from client - line terminated by \r\n */
void get_client_line(buf, len)
char *buf;
int len;
{
    int i, j;
    
    for (j = 0; j < len; j++)
    {
	if ((i = getchar()) == EOF)
	{
	    dbprintf(("EOF received\n"));
	    dbclose();
	    exit(1);		/* they hung up? */
	}
	
	if ((char)i == '\r') 
	{
	    if ((i = getchar()) == EOF)
	    {
		dbprintf(("EOF received\n"));
		dbclose();
		exit(1);		/* they hung up? */
	    }
	    if ((char)i == '\n')
	    {
		buf[j] = '\0';
		dbprintf(("> %s\n", buf));
		return;
	    }
	}

	buf[j] = (char)i;
    }

    dbprintf(("Buffer overflow in get_client_line()\n"));
    dbclose();
    exit(1);
    /*NOT REACHED*/
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
    char buf[BUF_LEN];
    int i;
    char amrestore_path[BUF_LEN];
    pid_t pid;

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
    dbopen("/tmp/amidxtaped.debug");
    dbprintf(("%s: version %s\n", argv[0], version()));
#else
    if ((i = open("/dev/null", O_WRONLY)) != STDERR_FILENO)
    {
	perror("amidxtaped can't redirect stderr");
	return 1;
    }
#endif
    
    /* get the number of arguments */
    get_client_line(buf, BUF_LEN);
    amrestore_nargs = atoi(buf);
    dbprintf(("amrestore_nargs=%d\n", amrestore_nargs));

    if ((amrestore_args = (char **)malloc((amrestore_nargs+2)*sizeof(char *)))
	== NULL) 
    {
	dbprintf(("Couldn't malloc amrestore_args\n"));
	dbclose();
	return 1;
    }

    if ((amrestore_args[0] = (char *)malloc(BUF_LEN)) == NULL)
    {
	dbprintf(("Couldn't malloc amrestore_args[0]\n"));
	dbclose();
	return 1;
    }
    (void)strcpy(amrestore_args[0], "amrestore");

    for (i = 1; i <= amrestore_nargs; i++)
    {
	if ((amrestore_args[i] = (char *)malloc(BUF_LEN)) == NULL)
	{
	    dbprintf(("Couldn't malloc amrestore_args[%d]\n", i));
	    dbclose();
	    return 1;
	}
	get_client_line(amrestore_args[i], BUF_LEN);
    }
    amrestore_args[amrestore_nargs+1] = NULL;

    sprintf(amrestore_path, "%s/%s", sbindir, "amrestore");
    
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
	dbprintf(("Error forking child\n"));
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

    sprintf(buf, "mt %s %s rewind",
#ifdef MT_FILE_FLAG
	MT_FILE_FLAG,
#else
	"-f",
#endif
	amrestore_args[i]);

    dbprintf(("Rewinding tape: %s\n", buf));
    system(buf);
    
    dbclose();
    return 0;
}
