/***************************************************************************
*
* File:          $RCSfile: amidxtaped.c,v $
*
* Revision:      $Revision: 1.3 $
* Last Edited:   $Date: 1997/08/26 02:16:37 $
* Author:        $Author: amcore $
*
* History:       $Log: amidxtaped.c,v $
* History:       Revision 1.3  1997/08/26 02:16:37  amcore
* History:       Fix dangling references to bindir.  by John R. Jackson
* History:
* History:       Revision 1.2  1997/07/03 07:48:12  amcore
* History:       Added MT_FILE_FLAG to {ac,}config.h, so that amidxtaped uses the
* History:       switch determined by configure for invoking mt.
* History:
* History:       Changed sendsize so that it sends SIGKILL instead of SIGTERM to
* History:       xfsdump.  The previous approach caused hangs on some releases of IRIX
* History:       6.x.
* History:
* History:       Removed a duplicated invocation of start_index in the vxdump support
* History:       in sendbackup-dump.
* History:
* History:       Revision 1.1  1997/05/14 08:29:29  amcore
* History:       Moved library files and its dependencies into appropriate directories.
* History:
* History:       Now --without-amrecover does not build recover, --without-restore does
* History:       not build amrestore nor amidxtaped.
* History:
* History:       Created new directory tape-src, to contain the tapeio library.  It is
* History:       used by both server-src and restore-src.  If neither of them is built,
* History:       the tape library is not build either.
* History:
* History:       Moved amidxtaped.c from server-src to restore-src.
* History:
* History:       Moved amrestore.c from recover-src to restore-src.
* History:
* History:       Moved add_exclude.c amandates.c amandates.h check_exclude.c fnmatch.c
* History:       fnmatch.h getfsent.c getfsent.h unctime.c from common-src to
* History:       client-src.
* History:
* History:       Renamed log_dummy.c to nolog.c.
* History:
* History:       Created new library nolog, with nolog.c, to be used in subdirectories
* History:       supposed to be run without the server library.
* History:
* History:       Moved amindex.c amindex.h changer.c changer.h clock.c clock.h
* History:       conffile.c conffile.h diskfile.c diskfile.h infofile.c infofile.h
* History:       logfile.c logfile.h tapefile.c tapefile.h from common-src to server.c.
* History:
* History:       Moved tapeio.c tapeio.h from common-src to tape-src.
* History:
* History:       Revision 1.1.1.1  1997/03/15 21:30:10  amcore
* History:       Mass import of 2.3.0.4 as-is.  We can remove generated files later.
* History:
* History:       Revision 1.4  1997/01/29 08:08:18  alan
* History:       better handling of return status from amrestore
* History:
* History:       Revision 1.3  1997/01/29 07:31:49  alan
* History:       removed ejection of tape - this is probably not what most people want
* History:
* History:       Revision 1.2  1996/12/19 08:56:37  alan
* History:       first go at file extraction
* History:
* History:       Revision 1.1  1996/12/16 07:53:42  alan
* History:       Initial revision
* History:
*
***************************************************************************/

/* This daemon extracts a dump image off a tape for amrecover and returns
   it over the network. It basically, reads a number of arguments from stdin
   (it is invoked via inet), one per line, preceeded by the number of them,
   and forms them into an argv structure, then execs amrestore.
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
    sprintf(buf, "mt "
#ifdef MT_FILE_FLAG
	    MT_FILE_FLAG
#else
	    "-f"
#endif
	    " %s rewind", amrestore_args[i]);
    dbprintf(("Rewinding tape: %s\n", buf));
    system(buf);
    
    dbclose();
    return 0;
}
