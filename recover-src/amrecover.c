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
 * $Id: amrecover.c,v 1.4 1997/08/27 08:12:28 amcore Exp $
 *
 * an interactive program for recovering backed-up files
 */

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
int get_line ()
{
    int l;
    int r;
    
    l = 0;
    do
    {
	if ((r = read(server_socket, server_line+l, 1)) != 1)
	{
	    while (--l >= 0
		   && (server_line[l] == '\r' || server_line[l] == '\n'))
	    {
		server_line[l] = '\0';
	    }
	    if (l > 0)
	    {
		int save_errno;

		save_errno = errno;
		fputs(server_line, stderr);
		fputc('\n', stderr);
		errno = save_errno;
	    }
	    if (r < 0)
	    {
		perror("amrecover: Error reading line from server");
	    }
	    else
	    {
		fprintf(stderr, "amrecover: Unexpected server end of file\n");
	    }
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
int print_reply ()
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
int grab_reply ()
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
int get_reply_line ()
{
    if (get_line() == -1)
	return -1;
    if (server_line[3] == '-')
	return 1;
    return 0;
}


/* returns pointer to returned line */
char *reply_line ()
{
    return server_line;
}
    


/* returns 0 if server returned an error code (ie code starting with 5)
   and non-zero otherwise */
int server_happy ()
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

    (void)send_command("QUIT");
    exit(1);
}

    

/* try and guess the disk the user is currently on.
   Return -1 if error, 0 if disk not local, 1 if disk local,
   2 if disk local but can't guess name */
/* do this by looking for the longest mount point which matches the
   current directory */
int guess_disk (cwd, dn_guess, mpt_guess)
char *cwd, *dn_guess, *mpt_guess;
{
    int longest_match = 0;
    int current_length;
    int cwd_length;
    int local_disk = 0;
    generic_fsent_t fsent;
    char fsname[LINE_LENGTH];

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
	    strcpy(mpt_guess, fsent.mntdir);
	    strcpy(fsname, (fsent.fsname+strlen(DEV_PREFIX)));
	    local_disk = is_local_fstype(&fsent);
	}
    }
    close_fstab();

    if (longest_match == 0)
	return -1;			/* ? at least / should match */

    if (!local_disk)
	return 0;

    /* have mount point now */
    /* disk name may be specified by mount point (logical name) or
       device name, have to determine */
    sprintf(line, "DISK %s", mpt_guess); /* try logical name */
    if (exchange(line) == -1)
	exit(1);
    if (server_happy())
    {
	strcpy(dn_guess, mpt_guess);	/* logical is okay */
	return 1;
    }
    sprintf(line, "DISK %s", fsname); /* try device name */
    if (exchange(line) == -1)
	exit(1);
    if (server_happy())
    {
	strcpy(dn_guess, fsname);	/* device name is okay */
	return 1;
    }

    /* neither is okay */
    return 1;
}


void quit ()
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
    char cwd[LINE_LENGTH], dn_guess[LINE_LENGTH], mpt_guess[LINE_LENGTH];

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
	exit(1);
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
	exit(1);
    }
    
    printf("AMRECOVER Version 1.1. Contacting server on %s ...\n",
	   server_name);  
    if ((sp = getservbyname("amandaidx" SERVICE_SUFFIX, "tcp")) == NULL)
    {
	perror("amrecover: amandaidx/tcp unknown protocol");
	dbprintf(("amandaidx/tcp unknown protocol\n"));
	dbclose();
	exit(1);
    }
    if ((hp = gethostbyname(server_name)) == NULL)
    {
	(void)fprintf(stderr, "amrecover: %s is an unknown host\n",
		      server_name);
	dbprintf(("%s is an unknown host\n", server_name));
	dbclose();
	exit(1);
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
	exit(1);
    }

    if (connect(server_socket, (struct sockaddr *)&server, sizeof(server))
	== -1)
    {
	perror("amrecover: Error connecting to server");
	dbprintf(("Error connecting to server\n"));
	dbclose();
	exit(1);
    }
    
    /* get server's banner */
    if (print_reply() == -1)
	exit(1);
    if (!server_happy())
    {
	dbclose();
	close(server_socket);
	exit(1);
    }
    
    /* set the date of extraction to be today */
    (void)time(&timer);
    strftime(dump_date, LINE_LENGTH, "%Y-%m-%d", localtime(&timer));
    printf("Setting restore date to today (%s)\n", dump_date);
    sprintf(line, "DATE %s", dump_date);
    if (converse(line) == -1)
	exit(1);

    sprintf(line, "SCNF %s", config);
    if (converse(line) == -1)
	exit(1);
    
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
#ifndef USE_FQDN
	    /* trim domain off name */
	    for (i = 0; i < strlen(dump_hostname); i++)
		if (dump_hostname[i] == '.')
		{
		    dump_hostname[i] = '\0';
		    break;
		}
#endif
	    sprintf(line, "HOST %s", dump_hostname);
	    if (converse(line) == -1)
		exit(1);
	}

	if (server_happy())
	{
            /* get a starting disk and directory based on where
	       we currently are */
	    switch (guess_disk(cwd, dn_guess, mpt_guess))
	    {
		case 1:
		    /* okay, got a guess. Set disk accordingly */
		    printf("$CWD '%s' is on disk '%s' mounted at '%s'.\n",
			   cwd, dn_guess, mpt_guess);
		    set_disk(dn_guess, mpt_guess);
		    set_directory(cwd);
		    break;

		case 0:
		    printf("$CWD '%s' is on a network mounted disk\n",
			   cwd);
		    printf("so you must 'sethost' to the server\n");
		    /* fake an unhappy server */
		    server_line[0] = '5';
		    break;

		case 2:
		case -1:
		default:
		    printf("Can't determine disk and mount point from $CWD\n");
		    /* fake an unhappy server */
		    server_line[0] = '5';
		    break;
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
