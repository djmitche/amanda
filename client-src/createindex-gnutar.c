/*
  createindex-dump. Creates an index file from the output of
  dump. It is assumed that the output of dump will be available on
  stdin. This is copied to stdout without alteration (so that it acts
  as a filter). The stuff from stdin is run through "gtar t" to
  get a table of contents and the file names stripped out. These are
  then compressed with gzip before being stored in the file generated
  from the arguments.
*/

/* Dr Alan M. McIvor, 13 April 1996 */

/* This program owes a lot to tee.c from GNU sh-utils and dumptee.c
   from the DeeJay backup package. */

#include "amanda.h"
#include "indexfilename.h"
#include "version.h"

char *pname = "createindex-gnutar";

volatile int pipe_finished = 0;

void pipe_closed(sig)
int sig;
{
    pipe_finished = 1;
}


int main(argc, argv)
int argc;
char **argv;
{
#ifndef GNUTAR
    dbopen("/tmp/createindex-gnutar.debug");
    dbprintf(("%s: version %s\n", argv[0], version()));
    dbprintf(("%s: gnutar not available on this system.\n", argv[0]));
    dbclose();
    fprintf(stderr, "%s: gnutar not available on this system.\n", argv[0]);
    return 1;
#else
    char cmd[BUFSIZ];
    struct sigaction act, oact;
    char buffer[BUFSIZ];
    int bytes_read;
    FILE *pipe_fp;
    int bytes_written;
    char *ptr;
    char *host, *disk, *datestamp;
    int level;
    
    if (argc != 5)
    {
	(void)fprintf(stderr, "Usage: %s host disk level datestamp\n",
		      argv[0]);
	return 1;
    }
    host      = argv[1];
    disk      = argv[2];
    level     = atoi(argv[3]);
    datestamp = argv[4];

    dbopen("/tmp/createindex-gnutar.debug");
    dbprintf(("%s: version %s\n", argv[0], version()));
    dbprintf(("%s %s %s %s %s started.\n",
	      argv[0], argv[1], argv[2], argv[3], argv[4]));

    /* set up a signal handler for SIGPIPE for when the pipe is finished
       creating the index file */
    /* at that point we obviously want to stop writing to it */
    act.sa_handler = pipe_closed;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGPIPE, &act, &oact) != 0)
    {
	(void)fprintf(stderr, "%s: Couldn't set SIGPIPE handler.\n", argv[0]);
	dbprintf(("Couldn't set SIGPIPE handler.\n"));
	dbclose();
	return 1;
    }
    
    /* this is the command for the pipe */
    /* make sure that the chat from restore doesn't go to stderr cause this
       goes back to amanda which doesn't expect to see it. Thus we need to
       make sure that the pipe executed and write our own error message if
       not */
    (void)sprintf(cmd,
		  "%s tf - 2>/dev/null | cut -c2- | %s > %s",
		  GNUTAR, COMPRESS_PATH,
		  indexfilename(host, disk, level, datestamp));

    umask(066);
    /* open pipe to command */
    if ((pipe_fp = popen(cmd, "w")) == NULL)
    {
	(void)fprintf(stderr, "%s: Can't fork restore cmd \"%s\".\n",
		      argv[0], cmd);
	dbprintf(("Can't fork restore cmd \"%s\".\n", cmd));
	dbclose();
	return 1;
    }

    dbprintf(("Forked cmd \"%s\".\n", cmd));
    while (1)
    {
	bytes_read = read(0, buffer, BUFSIZ);
	if ((bytes_read < 0) && (errno == EINTR))
	    continue;

	if (bytes_read < 0)
	{
	    (void)fprintf(stderr, "%s: Error reading stdin.\n", argv[0]);
	    dbprintf(("Error reading stdin.\n"));
	    dbclose();
	    return 1;
	}

	if (bytes_read == 0)
	    break;			/* no more stuff to read - finished */

	/* write the stuff to the subprocess */
	/* we are likely to be interrupted part way through one write by
	   the subprocess finishing. But we don't care at that point */
	if (!pipe_finished)
	    (void)fwrite(buffer, sizeof(char), BUFSIZ, pipe_fp);
	
	/* write the stuff to stdout, ensuring none lost when interrupt
	   occurs */
	ptr = buffer;
	while (bytes_read > 0)
	{
	    bytes_written = write(1, ptr, bytes_read);
	    if ((bytes_written < 0) && (errno == EINTR))
		continue;
	    if (bytes_written < 0)
	    {
	        (void)fprintf(stderr, "%s: error writing to stdout.\n",
			      argv[0]);
		dbprintf(("Error writing to stdout.\n"));
		return 1;
	    }
	    bytes_read -= bytes_written;
	    ptr += bytes_written;
	}
    }
    
    /* finished */
    /* check the exit code of the pipe and moan if not 0 */
    if ((bytes_read = pclose(pipe_fp)) != 0) 
    {
	(void)fprintf(stderr, "%s: pipe returned %d\n", argv[0], bytes_read);
	dbprintf(("Pipe return %d\n", bytes_read));
	dbclose();
	return 1;
    }

    dbprintf(("Ready.\n"));
    dbclose();
    return 0;
#endif
}
