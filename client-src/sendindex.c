/*
  sendindex: reads the index file from where it was stored by
  createindex-*, uncompresses it and returns it to the server

  This code is based on sendbackup-*
  
*/

/* Dr Alan M. McIvor, 5 February 1997 */

#include "amanda.h"
#include "indexfilename.h"
#include "stream.h"
#include "arglist.h"
#include "tapeio.h"

#define MAX_LINE 1024
#define TIMEOUT 30

/*
 * If we don't have the new-style wait access functions, use our own,
 * compatible with old-style BSD systems at least.  Note that we don't
 * care about the case w_stopval == WSTOPPED since we don't ask to see
 * stopped processes, so should never get them from wait.
 */
#ifndef WEXITSTATUS
#   define WEXITSTATUS(r)	(((union wait *) &(r))->w_retcode)
#   define WTERMSIG(r)		(((union wait *) &(r))->w_termsig)

#   undef  WIFSIGNALED
#   define WIFSIGNALED(r)	(((union wait *) &(r))->w_termsig != 0)
#endif

static int data_socket, data_port, dataf;

static char line[MAX_LINE];
char *pname = "sendindex";

/* local functions */
int main P((int argc, char **argv));
void start_index P((char *index_filename, int dataf));

int main(argc, argv)
int argc;
char **argv;
{
    int level;
    char disk[256], datestamp[80];
    char index_filename[1024];
    char host[MAX_HOSTNAME_LENGTH], *domain;
    pid_t pid;
    int child_stat;
    
    /* initialize */

    chdir("/tmp");
    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);
    umask(0);
    dbopen("/tmp/sendindex.debug");
    {
	extern int db_file;
	dup2(db_file, 2);
    }

    /* parse dump request */

    if(fgets(line, MAX_LINE, stdin) == NULL)
	goto err;
    dbprintf(("%s: got input request: %s", argv[0], line));
    if(sscanf(line, "%s %d DATESTAMP %s\n", 
	      disk, &level, datestamp) != 3)
	goto err;
    dbprintf(("  parsed request as: disk `%s' level %d datestamp `%s'\n",
	      disk, level, datestamp));

    data_socket = stream_server(&data_port);

    /* form the path of the index file */
    if(gethostname(host, sizeof(host)-1) == -1)
	error("error [gethostname: %s]", strerror(errno));
    if((domain = strchr(host, '.'))) *domain++ = '\0';
    strcpy(index_filename, indexfilename(host, disk, level, datestamp));
	
    /* check that the index file exists */
    if (access(index_filename, R_OK) == -1)
    {
	printf("ERROR [%s: could not access '%s': %s]\n",
	       argv[0], index_filename, strerror(errno));
	dbprintf(("No index file '%s'\n", index_filename));
	dbclose();
	return 1;
    }
    
    dbprintf(("Returning index file '%s'\n", index_filename));

    switch(fork())
    {
	case -1:	/* fork error */
	    printf("ERROR [%s: could not fork: %s]\n", argv[0],
		   strerror(errno));
	    dbclose();
	    return 1;
	default:	/* parent */
	    /* this message goes back to calling process (on amanda server)
	       so it knows where to connect to get the data */
	    printf("CONNECT DATA %d\n", data_port);
	    return 0;
	case 0:	/* child, keep going */
	    break;
    }

    dbprintf(("  waiting for connect on %d\n", data_port));
    
    dataf = stream_accept(data_socket, TIMEOUT, DEFAULT_SIZE, DEFAULT_SIZE);
    if(dataf == -1) 
    {
	dbprintf(("%s: timeout on data port %d\n", argv[0], data_port));
	dbclose();
	return 1;
    }

    dbprintf(("  got connection\n"));

    /* fork a child process to send the index file */
    start_index(index_filename, dataf);

    /* wait for the child to complete */
    if ((pid = waitpid(-1, &child_stat, 0)) == (pid_t)-1)
    {
	error("error waiting for child");
    }
    if ((WIFEXITED(child_stat) != 0) && (WEXITSTATUS(child_stat) != 0))
    {
	dbprintf(("ERROR - uncompress child returned non-zero status: %d\n",
		  WEXITSTATUS(child_stat)));
	dbclose();
	error("uncompress child returned non-zero status: %d\n",
	      WEXITSTATUS(child_stat));
    }

    /* delete the index file */
    (void)remove(index_filename);

    dbclose();
    return 0;

 err:
    printf("FORMAT ERROR IN REQUEST PACKET\n");
    dbprintf(("REQ packet is bogus\n"));
    dbclose();
    return 1;
}
    

void start_index(index_filename, dataf)
char *index_filename;
int dataf;
{
    switch(fork())
    {
	case -1:	/* fork error */
	    printf("ERROR [sendindex: could not do second fork: %s]\n",
		   strerror(errno));
	    dbclose();
	    exit(1);
	default:	/* parent */
	    return;
	case 0:	/* child, keep going */
	    break;
    }

    /* only the child gets to here. set up routine to uncompress index file
       and write to dataf */

    /* turn stdout into dataf */
    if (dup2(dataf, STDOUT_FILENO) == -1)
    {
	perror("sendindex - uncompress client");
	exit(1);
    }
    (void)execlp(UNCOMPRESS_PATH, UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
		 UNCOMPRESS_OPT,
#endif
		 index_filename, (char *)0);
    /* only get here if exec failed */
    error("sendindex could not exec %s: %s", UNCOMPRESS_PATH, strerror(errno));
    exit(1);
    /*NOT REACHED*/
}
