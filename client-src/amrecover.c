/***************************************************************************
*
* File:          $RCSfile: amrecover.c,v $
* Module:        
* Part of:       
*
* Revision:      $Revision: 1.1 $
* Last Edited:   $Date: 1997/03/15 21:29:58 $
* Author:        $Author: amcore $
*
* Notes:         
* Private Func:  
* History:       $Log: amrecover.c,v $
* History:       Revision 1.1  1997/03/15 21:29:58  amcore
* History:       Initial revision
* History:
* History:       Revision 1.19  1996/12/31 09:32:00  alan
* History:       Removed inclusion of mntent.h and use of MAXMNTSTR since this was
* History:       non-portable, as pointed out by Izzy Ergas <erga00@nbhd.org>.
* History:
* History:       Revision 1.18  1996/12/19 08:53:48  alan
* History:       first go at file extraction
* History:
* History:       Revision 1.17  1996/12/17 09:47:06  alan
* History:       *** empty log message ***
* History:
* History:       Revision 1.16  1996/11/06 08:24:54  alan
* History:       changed initial ordering so client sees something if INDEX_DIR doesn't
* History:       exist.
* History:
* History:       Revision 1.15  1996/10/29 08:30:57  alan
* History:       Pete Geenhuizen inspired changes to support logical disk names etc
* History:
* History:       Revision 1.14  1996/10/03 05:45:01  alan
* History:       updated version number
* History:
* History:       Revision 1.13  1996/10/02 18:34:22  alan
* History:       synchronization with Blair's changes
* History:
* History:       Revision 1.12  1996/09/10 10:30:41  alan
* History:       guess_disk() got disk_path wrong if mount_point not /
* History:
* History:       Revision 1.11  1996/07/29 10:23:48  alan
* History:       due to problems on SunOS changed get_line() to strip off \r\n
* History:
* History:       Revision 1.10  1996/07/29 09:50:20  alan
* History:       ?
* History:
* History:       Revision 1.9  1996/07/23 10:55:13  alan
* History:       added clause to handle broken SunOS .h files
* History:
* History:       Revision 1.8  1996/06/26 10:06:49  alan
* History:       added signal handler for SIGINT
* History:
* History:       Revision 1.7  1996/06/20 10:57:16  alan
* History:       make guess_disk OS independent and detect network disks
* History:
* History:       Revision 1.6  1996/05/23 10:09:22  alan
* History:       changed optional arguments to match recover
* History:
* History:       Revision 1.5  1996/05/22 09:29:27  alan
* History:       added defaults for config and host
* History:
* History:       Revision 1.4  1996/05/16 11:00:09  alan
* History:       added access to server reply directly
* History:
* History:       Revision 1.3  1996/05/16 09:52:45  alan
* History:       handled reply properly
* History:
* History:       Revision 1.2  1996/05/13 09:20:54  alan
* History:       changes
* History:
* History:       Revision 1.1  1996/05/12 10:05:50  alan
* History:       Initial revision
* History:
*
***************************************************************************/

#include "amanda.h"
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include "amrecover.h"
#include "getfsent.h"

#ifdef HAVE_LIBREADLINE
#  ifdef HAVE_READLINE_READLINE_H
#    include <readline/readline.h>
#    ifdef HAVE_READLINE_HISTORY_H
#      include <readline/history.h>
#    endif
#  else
#    ifdef HAVE_READLINE_H
#      include <readline.h>
#      ifdef HAVE_HISTORY_H
#        include <history.h>
#      endif
#    else
#      undef HAVE_LIBREADLINE
#    endif
#  endif
#endif

char *pname = "amrecover";

extern int process_line P((char *line));

#define USAGE "Usage: amrecover [-C <config>] [-s <index-server>] [-t <tape-server>] [-d <tape-device>]\n"

char config[LINE_LENGTH];
char server_name[LINE_LENGTH];
int server_socket;
char server_line[LINE_LENGTH];
char line[LINE_LENGTH];
char dump_hostname[MAX_HOSTNAME_LENGTH];/* which machine we are restoring */
char disk_name[LINE_LENGTH];		/* disk we are restoring */
char mount_point[LINE_LENGTH];		/* where disk was mounted */
char disk_path[LINE_LENGTH];		/* path relative to mount point */
char dump_date[LINE_LENGTH];		/* date on which we are restoring */
int quit_prog;				/* set when time to exit parser */
char tape_server_name[LINE_LENGTH];
int tape_server_socket;
char tape_device_name[LINE_LENGTH];

#ifndef HAVE_LIBREADLINE
/*
 * simple readline() replacements
 */

static char readline_buffer[1024];

char *
readline(prompt)
char *prompt;
{
    printf("%s",prompt);
    fflush(stdout);
    return fgets(readline_buffer, sizeof(readline_buffer), stdin);
}

#define add_history(x)                /* add_history((x)) */

#endif

/* gets a "line" from server and put in server_line */
/* server_line is terminated with \0, \r\n is striped */
/* returns -1 if error */
/* NOTE server sends at least 6 chars: 3 for code, 1 for cont, 2 for \r\n */
int get_line P((void))
{
    int l;
    
    l = 0;
    do
    {
	if (read(server_socket, server_line+l, 1) != 1)
	{
	    perror("amrecover: Error reading line from server");
	    return -1;
	}
	l++;
    }
    while ((l < 6)
	   || (server_line[l-2] != '\r') || (server_line[l-1] != '\n'));

    server_line[l-2] = '\0';

    return 0;
}
    

/* get reply from server and print to screen */
/* handle multi-line reply */
/* return -1 if error */
/* return code returned by server always occupies first 3 bytes of global
   variable server_line */
int print_reply P((void))
{
    do
    {
	if (get_line() == -1)
	    return -1;
	printf("%s\n", server_line);
    }
    while (server_line[3] == '-');
    fflush(stdout);

    return 0;
}


/* same as print_reply() but doesn't print reply on stdout */
/* NOTE: reply still written to server_line[] */
int grab_reply P((void))
{
    do
    {
	if (get_line() == -1)
	    return -1;
    }
    while (server_line[3] == '-');

    return 0;
}


/* get 1 line of reply */
/* returns -1 if error, 0 if last (or only) line, 1 if more to follow */
int get_reply_line P((void))
{
    if (get_line() == -1)
	return -1;
    if (server_line[3] == '-')
	return 1;
    return 0;
}


/* returns pointer to returned line */
char *reply_line P((void))
{
    return server_line;
}
    


/* returns 0 if server returned an error code (ie code starting with 5)
   and non-zero otherwise */
int server_happy P((void))
{
    if (server_line[0] == '5')
	return 0;
    return 1;
}

    
int send_command(cmd)
char *cmd;
{
    int l, n, s;

    
    for (l = 0, n = strlen(cmd); l < n; l += s)
	if ((s = write(server_socket, cmd + l, n - l)) == -1)
	{
	    perror("amrecover: Error writing to server");
	    return -1;
	}
    if (write(server_socket, "\r\n", 2) == -1)
    {
	perror("amrecover: Error writing to server");
	return -1;
    }
    return 0;
}


/* send a command to the server, get reply and print to screen */
int converse(cmd)
char *cmd;
{
    if (send_command(cmd) == -1)
	return -1;
    if (print_reply() == -1)
	return -1;
    return 0;
}


/* same as converse() but reply not echoed to stdout */
int exchange(cmd)
char *cmd;
{
    if (send_command(cmd) == -1)
	return -1;
    if (grab_reply() == -1)
	return -1;
    return 0;
}


/* basic interrupt handler for when user presses ^C */
/* Bale out, letting server know before doing so */
void sigint_handler(signum)
int signum;
{
    if (extract_restore_child_pid != -1)
	(void)kill(extract_restore_child_pid, SIGKILL);
    extract_restore_child_pid = -1;
    if (extract_compress_child_pid != -1)
	(void)kill(extract_compress_child_pid, SIGKILL);
    extract_compress_child_pid = -1;

    (void)send_command("QUIT");
    exit(1);
}

    

/* try and guess the disk the user is currently on.
   Return -1 if error, 0 if disk not local, 1 if disk local */
/* do this by looking for the longest mount point which matches the
   current directory */
int guess_disk P((void))
{
    int longest_match = 0;
    int current_length;
    int cwd_length;
    int local_disk = 0;
    generic_fsent_t fsent;
    char dir[LINE_LENGTH];
    char fsname[LINE_LENGTH];
    char cwd[LINE_LENGTH];

    if (getcwd(cwd, LINE_LENGTH) == NULL)
	return -1;
    cwd_length = strlen(cwd);
    
    if (open_fstab() == 0)
	return -1;

    while (get_fstab_nextentry(&fsent))
    {
	current_length = fsent.mntdir ? strlen(fsent.mntdir) : 0;
	if ((current_length > longest_match)
	    && (current_length <= cwd_length)
	    && (strncmp(fsent.mntdir, cwd, current_length) == 0))
	{
	    longest_match = current_length;
	    strcpy(dir, fsent.mntdir);
	    strcpy(fsname, (fsent.fsname+strlen(DEV_PREFIX)));
	    local_disk = is_local_fstype(&fsent);
	}
    }
    close_fstab();

    if (longest_match == 0)
	return -1;			/* ? at least / should match */

    if (!local_disk)
	return 0;

    strcpy(mount_point, dir);
    strcpy(disk_name, fsname);
    if (strcmp(mount_point, "/") == 0)
	strcpy(disk_path, cwd);
    else
	strcpy(disk_path, cwd+longest_match);

    return 1;
}


void quit P((void))
{
    quit_prog = 1;
    (void)converse("QUIT");
}


int main(argc, argv)
int argc;
char **argv;
{
    struct sockaddr_in server;
    struct servent *sp;
    struct hostent *hp;
    int i;
    time_t timer;
    char *lineread;
    struct sigaction act, oact;
    extern char *optarg;
    extern int optind;

    strcpy(config, DEFAULT_CONFIG);
    strcpy(server_name, DEFAULT_SERVER);
#ifdef RECOVER_DEFAULT_TAPE_SERVER
    strcpy(tape_server_name, RECOVER_DEFAULT_TAPE_SERVER);
    strcpy(tape_device_name, RECOVER_DEFAULT_TAPE_DEVICE);
#else
    tape_server_name[0] = '\0';
    tape_device_name[0] = '\0';
#endif
    while ((i = getopt(argc, argv, "C:s:t:d:U")) != EOF)
    {
	switch (i)
	{
	    case 'C':
		strcpy(config, optarg);
		break;

	    case 's':
		strcpy(server_name, optarg);
		break;

	    case 't':
		strcpy(tape_server_name, optarg);
		break;

	    case 'd':
		strcpy(tape_device_name, optarg);
		break;

	    case 'U':
	    case '?':
		(void)printf(USAGE);
		return 0;
	}
    }
    if (optind != argc)
    {
	(void)fprintf(stderr, USAGE);
	return 1;
    }

    dbopen("/tmp/amrecover.debug");
    
    disk_name[0] = '\0';
    mount_point[0] = '\0';
    disk_path[0] = '\0';
    dump_date[0] = '\0';
    
    /* set up signal handler */
    act.sa_handler = sigint_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if (sigaction(SIGINT, &act, &oact) != 0)
    {
	perror("amrecover: Error setting signal handler");
	dbprintf(("Error setting signal handler\n"));
	dbclose();
	return 1;
    }
    
    printf("AMRECOVER Version 1.0. Contacting server on %s ...\n",
	   server_name);  
    if ((sp = getservbyname("amandaidx", "tcp")) == NULL)
    {
	perror("amrecover: amandaidx/tcp unknown protocol");
	dbprintf(("amandaidx/tcp unknown protocol\n"));
	dbclose();
	return 1;
    }
    if ((hp = gethostbyname(server_name)) == NULL)
    {
	(void)fprintf(stderr, "amrecover: %s is an unknown host\n",
		      server_name);
	dbprintf(("%s is an unknown host\n", server_name));
	dbclose();
	return 1;
    }
    memset((char *)&server, 0, sizeof(server));
    memcpy((char *)&server.sin_addr, hp->h_addr, hp->h_length);
    server.sin_family = hp->h_addrtype;
    server.sin_port = sp->s_port;
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)

    {
	perror("amrecover: Error creating socket");
	dbprintf(("Error creating socket\n"));
	dbclose();
	return 1;
    }

    if (connect(server_socket, (struct sockaddr *)&server, sizeof(server))
	== -1)
    {
	perror("amrecover: Error connecting to server");
	dbprintf(("Error connecting to server\n"));
	dbclose();
	return 1;
    }
    
    /* get server's banner */
    if (print_reply() == -1)
	return 1;
    if (!server_happy())
    {
	dbclose();
	close(server_socket);
	return 0;
    }
    
    /* set the date of extraction to be today */
    (void)time(&timer);
    strftime(dump_date, LINE_LENGTH, "%Y-%m-%d", localtime(&timer));
    printf("Setting restore date to today (%s)\n", dump_date);
    sprintf(line, "DATE %s", dump_date);
    if (converse(line) == -1)
	return 1;

    sprintf(line, "SCNF %s", config);
    if (converse(line) == -1)
	return 1;
    
    if (server_happy())
    {
	/* set host we are restoring to this host by default */
	/* dump_hostname[sizeof(dump_hostname)-1] = '\0'; */ /* static var */
	if (gethostname(dump_hostname, sizeof(dump_hostname)-1) == -1)
	{
	    perror("amrecover: Can't get local host name");
	    /* fake an unhappy server */
	    server_line[0] = '5';
	}
	else
	{
	    /* trim domain off name */
	    for (i = 0; i < strlen(dump_hostname); i++)
		if (dump_hostname[i] == '.')
		{
		    dump_hostname[i] = '\0';
		    break;
		}
	    sprintf(line, "HOST %s", dump_hostname);
	    if (converse(line) == -1)
		return 1;
	}

	if (server_happy())
	{
            /* get a starting disk and directory based on where
	       we currently are */
	    if ((i = guess_disk()) == 1)
	    {
		/* okay, got a guess. Set disk accordingly */
		printf("Divided $CWD into directory %s on disk %s mounted at %s.\n",
		   disk_path, disk_name, mount_point);
		sprintf(line, "DISK %s", disk_name);
		if (converse(line) == -1)
		    return 1;
	    }
	    else
	    {
		if (i == 0)
		    (void)printf("$CWD is on a network mounted disk. You must restore from its server\n");
		else
		    (void)fprintf(stderr,
				  "Can't determine disk and mount point from $CWD\n");
		/* fake an unhappy server */
		server_line[0] = '5';
	    }
	}
    }

    quit_prog = 0;
    do
    {
	if ((lineread = readline("amrecover> ")) == NULL)
	    break;
	if (lineread[0] != '\0') 
	{
	    
	    add_history(lineread);
	    process_line(lineread);	/* act on line's content */
	}
	free(lineread);
    }
    while (!quit_prog);

    dbclose();

    close(server_socket);
    return 0;
}
