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
 * $Id: amrecover.c,v 1.20 1998/01/12 22:32:35 blair Exp $
 *
 * an interactive program for recovering backed-up files
 */

#include "amanda.h"
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#include <netinet/in.h>
#ifdef HAVE_NETINET_IP_H
#include <netinet/ip.h>
#endif
#include "amrecover.h"
#include "getfsent.h"
#include "dgram.h"

#if defined(KRB4_SECURITY)
#include "krb4-security.h"
#endif

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

char *errstr = NULL;

extern int process_line P((char *line));
int guess_disk P((char *cwd, int cwd_len, char **dn_guess, char **mpt_guess));

#define USAGE "Usage: amrecover [[-C] <config>] [-s <index-server>] [-t <tape-server>] [-d <tape-device>]\n"

char *config = NULL;
char *server_name = NULL;
int server_socket;
char *server_line = NULL;
char *dump_hostname;			/* which machine we are restoring */
char *disk_name = NULL;			/* disk we are restoring */
char *mount_point = NULL;		/* where disk was mounted */
char *disk_path = NULL;			/* path relative to mount point */
char dump_date[STR_SIZE];		/* date on which we are restoring */
int quit_prog;				/* set when time to exit parser */
char *tape_server_name = NULL;
int tape_server_socket;
char *tape_device_name = NULL;

#ifndef HAVE_LIBREADLINE
/*
 * simple readline() replacements
 */

char *
readline(prompt)
char *prompt;
{
    printf("%s",prompt);
    fflush(stdout); fflush(stderr);
    return agets(stdin);
}

#define add_history(x)                /* add_history((x)) */

#endif

/* gets a "line" from server and put in server_line */
/* server_line is terminated with \0, \r\n is striped */
/* returns -1 if error */

int get_line ()
{
    char *line = NULL;
    char *part = NULL;
    int len;

    while(1) {
	if((part = areads(server_socket)) == NULL) {
	    int save_errno = errno;

	    if(server_line) {
		fputs(server_line, stderr);	/* show the last line read */
		fputc('\n', stderr);
		errno = save_errno;
	    }
	    if(errno != 0) {
		fprintf(stderr, "%s: ", pname);
		errno = save_errno;
		perror("Error reading line from server");
	    } else {
		fprintf(stderr, "%s: Unexpected server end of file\n", pname);
	    }
	    afree(line);
	    afree(server_line);
	    return -1;
	}
	if(line) {
	    strappend(line, part);
	    afree(part);
	} else {
	    line = part;
	    part = NULL;
	}
	if((len = strlen(line)) > 0 && line[len-1] == '\r') {
	    line[len-1] = '\0';
	    server_line = newstralloc(server_line, line);
	    afree(line);
	    return 0;
	}
	/*
	 * Hmmm.  We got a "line" from areads(), which means it saw
	 * a '\n' (or EOF, etc), but there was not a '\r' before it.
	 * Put a '\n' back in the buffer and loop for more.
	 */
	strappend(line, "\n");
    }
}


/* get reply from server and print to screen */
/* handle multi-line reply */
/* return -1 if error */
/* return code returned by server always occupies first 3 bytes of global
   variable server_line */
int grab_reply (show)
int show;
{
    do {
	if (get_line() == -1) {
	    return -1;
	}
	if(show) puts(server_line);
    } while (server_line[3] == '-');
    if(show) fflush(stdout);

    return 0;
}


/* get 1 line of reply */
/* returns -1 if error, 0 if last (or only) line, 1 if more to follow */
int get_reply_line ()
{
    if (get_line() == -1)
	return -1;
    return server_line[3] == '-';
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
    return server_line[0] != '5';
}


int send_command(cmd)
char *cmd;
{
    int l, n, s;
    char *end;

    /*
     * NOTE: this routine is called from sigint_handler, so we must be
     * **very** careful about what we do since there is no way to know
     * our state at the time the interrupt happened.  For instance,
     * do not use any stdio routines here.
     */
    for (l = 0, n = strlen(cmd); l < n; l += s)
	if ((s = write(server_socket, cmd + l, n - l)) < 0) {
	    perror("amrecover: Error writing to server");
	    return -1;
	}
    end = "\r\n";
    for (l = 0, n = strlen(end); l < n; l += s)
	if ((s = write(server_socket, end + l, n - l)) < 0) {
	    perror("amrecover: Error writing to server");
	    return -1;
	}
    return 0;
}


/* send a command to the server, get reply and print to screen */
int converse(cmd)
char *cmd;
{
    if (send_command(cmd) == -1) return -1;
    if (grab_reply(1) == -1) return -1;
    return 0;
}


/* same as converse() but reply not echoed to stdout */
int exchange(cmd)
char *cmd;
{
    if (send_command(cmd) == -1) return -1;
    if (grab_reply(0) == -1) return -1;
    return 0;
}


/* basic interrupt handler for when user presses ^C */
/* Bale out, letting server know before doing so */
void sigint_handler(signum)
int signum;
{
    /*
     * NOTE: we must be **very** careful about what we do here since there
     * is no way to know our state at the time the interrupt happened.
     * For instance, do not use any stdio routines here or in any called
     * routines.  Also, use _exit() instead of exit() to make sure stdio
     * buffer flushing is not attempted.
     */
    if (extract_restore_child_pid != -1)
	(void)kill(extract_restore_child_pid, SIGKILL);
    extract_restore_child_pid = -1;

    (void)send_command("QUIT");
    _exit(1);
}


void clean_pathname(s)
char *s;
{
    int length;
    length = strlen(s);

    /* remove "/" at end of path */
    if(s[length-1]=='/')
	s[length-1]='\0';

    /* change "/." to "/" */
    if(strcmp(s,"/.")==0)
	s[1]='\0';

    /* remove "/." at end of path */
    if(strcmp(&(s[length-2]),"/.")==0)
	s[length-2]='\0';
}


/* try and guess the disk the user is currently on.
   Return -1 if error, 0 if disk not local, 1 if disk local,
   2 if disk local but can't guess name */
/* do this by looking for the longest mount point which matches the
   current directory */
int guess_disk (cwd, cwd_len, dn_guess, mpt_guess)
char *cwd, **dn_guess, **mpt_guess;
int cwd_len;
{
    int longest_match = 0;
    int current_length;
    int cwd_length;
    int local_disk = 0;
    generic_fsent_t fsent;
    char *fsname = NULL;
    char *disk_try = NULL;

    *dn_guess = *mpt_guess = NULL;

    if (getcwd(cwd, cwd_len) == NULL)
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
	    afree(*mpt_guess);
	    *mpt_guess = stralloc(fsent.mntdir);
	    fsname = newstralloc(fsname, fsent.fsname+strlen(DEV_PREFIX));
	    local_disk = is_local_fstype(&fsent);
	}
    }
    close_fstab();

    if (longest_match == 0) {
	afree(*mpt_guess);
	afree(fsname);
	return -1;			/* ? at least / should match */
    }

    if (!local_disk) {
	afree(*mpt_guess);
	afree(fsname);
	return 0;
    }

    /* have mount point now */
    /* disk name may be specified by mount point (logical name) or
       device name, have to determine */
    disk_try = stralloc2("DISK ", *mpt_guess);		/* try logical name */
    if (exchange(disk_try) == -1)
	exit(1);
    afree(disk_try);
    if (server_happy())
    {
	*dn_guess = stralloc(*mpt_guess);		/* logical is okay */
	afree(fsname);
	return 1;
    }
    disk_try = stralloc2("DISK ", fsname);		/* try device name */
    if (exchange(disk_try) == -1)
	exit(1);
    afree(disk_try);
    if (server_happy())
    {
	*dn_guess = stralloc(fsname);			/* dev name is okay */
	afree(fsname);
	return 1;
    }

    /* neither is okay */
    afree(*mpt_guess);
    afree(fsname);
    return 2;
}


void quit ()
{
    quit_prog = 1;
    (void)converse("QUIT");
}

char *localhost = NULL;

int main(argc, argv)
int argc;
char **argv;
{
    struct sockaddr_in server;
    struct sockaddr_in myname;
    struct servent *sp;
    struct hostent *hp;
    int i;
    time_t timer;
    char *lineread = NULL;
    struct sigaction act, oact;
    extern char *optarg;
    extern int optind;
    char cwd[STR_SIZE], *dn_guess = NULL, *mpt_guess = NULL;
    char *service_name;
    char *line = NULL;
    int fd;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    localhost = alloc(MAX_HOSTNAME_LENGTH+1);
    if (gethostname(localhost, MAX_HOSTNAME_LENGTH) != 0) {
	error("cannot determine local host name\n");
    }
    localhost[MAX_HOSTNAME_LENGTH] = '\0';

    config = newstralloc(config, DEFAULT_CONFIG);
    server_name = newstralloc(server_name, DEFAULT_SERVER);
#ifdef DEFAULT_TAPE_SERVER
    tape_server_name = newstralloc(tape_server_name, DEFAULT_TAPE_SERVER);
    tape_device_name = newstralloc(tape_device_name, DEFAULT_TAPE_DEVICE);
#else
    afree(tape_server_name);
    afree(tape_device_name);
#endif
    if (argc > 1 && argv[1][0] != '-')
    {
	/*
	 * If the first argument is not an option flag, then we assume
	 * it is a configuration name to match the syntax of the other
	 * Amanda utilities.
	 */
	char **new_argv;

	new_argv = (char **) malloc ((argc + 1 + 1) * sizeof (*new_argv));
	if (new_argv == NULL)
	{
	    (void)fprintf(stderr, "%s: no memory for argument list\n",pname);
	    exit(1);
	}
	new_argv[0] = argv[0];
	new_argv[1] = "-C";
	for (i = 1; i < argc; i++)
	{
	    new_argv[i + 1] = argv[i];
	}
	new_argv[i + 1] = NULL;
	argc++;
	argv = new_argv;
    }
    while ((i = getopt(argc, argv, "C:s:t:d:U")) != EOF)
    {
	switch (i)
	{
	    case 'C':
		config = newstralloc(config, optarg);
		break;

	    case 's':
		server_name = newstralloc(server_name, optarg);
		break;

	    case 't':
		tape_server_name = newstralloc(tape_server_name, optarg);
		break;

	    case 'd':
		tape_device_name = newstralloc(tape_device_name, optarg);
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

    dbopen();

    afree(disk_name);
    afree(mount_point);
    afree(disk_path);
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

    service_name = stralloc2("amandaidx", SERVICE_SUFFIX);

    printf("AMRECOVER Version 1.1. Contacting server on %s ...\n",
	   server_name);  
    if ((sp = getservbyname(service_name, "tcp")) == NULL)
    {
	perror("amrecover: amandaidx/tcp unknown protocol");
	dbprintf(("%s/tcp unknown protocol\n", service_name));
	dbclose();
	exit(1);
    }
    if ((hp = gethostbyname(server_name)) == NULL)
    {
	(void)fprintf(stderr, "%s: %s is an unknown host\n",
		      pname, server_name);
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

    /*
     * Bind our end of the socket to a privileged port.
     */
    if ((hp = gethostbyname(localhost)) == NULL)
    {
	(void)fprintf(stderr, "%s: %s is an unknown host\n",
		      pname, localhost);
	dbprintf(("%s is an unknown host\n", localhost));
	dbclose();
	exit(1);
    }
    memset((char *)&myname, 0, sizeof(myname));
    memcpy((char *)&myname.sin_addr, hp->h_addr, hp->h_length);
    myname.sin_family = hp->h_addrtype;
    if (bind_reserved(server_socket, &myname) != 0)
    {
	int save_errno = errno;

	perror("amrecover: Error binding socket");
	dbprintf(("Error binding socket: %s\n", strerror(save_errno)));
	dbclose();
	exit(1);
    }

    /*
     * We may need root privilege again later for a reserved port to
     * the tape server, so we will drop down now but might have to
     * come back later.
     */
    setegid(getgid());
    seteuid(getuid());

    if (connect(server_socket, (struct sockaddr *)&server, sizeof(server))
	== -1)
    {
	perror("amrecover: Error connecting to server");
	dbprintf(("Error connecting to server\n"));
	dbclose();
	exit(1);
    }

    /* get server's banner */
    if (grab_reply(1) == -1)
	exit(1);
    if (!server_happy())
    {
	dbclose();
	aclose(server_socket);
	exit(1);
    }

    /* do the security thing */
#if defined(KRB4_SECURITY)
#if 0 /* not yet implemented */
    if(krb4_auth)
    {
	line = get_krb_security();
    } else
#endif /* 0 */
#endif
    {
	line = get_bsd_security();
    }
    if (converse(line) == -1)
	exit(1);
    if (!server_happy())
	exit(1);
    memset(line, '\0', strlen(line));
    afree(line);

    /* set the date of extraction to be today */
    (void)time(&timer);
    strftime(dump_date, sizeof(dump_date), "%Y-%m-%d", localtime(&timer));
    printf("Setting restore date to today (%s)\n", dump_date);
    line = stralloc2("DATE ", dump_date);
    if (converse(line) == -1)
	exit(1);
    afree(line);

    line = stralloc2("SCNF ", config);
    if (converse(line) == -1)
	exit(1);
    afree(line);

    if (server_happy())
    {
	/* set host we are restoring to this host by default */
	afree(dump_hostname);
	dump_hostname = alloc(MAX_HOSTNAME_LENGTH+1);
	if (gethostname(dump_hostname, MAX_HOSTNAME_LENGTH) == -1)
	{
	    perror("amrecover: Can't get local host name");
	    afree(dump_hostname);
	    /* fake an unhappy server */
	    server_line[0] = '5';
	}
	else
	{
	    dump_hostname[MAX_HOSTNAME_LENGTH] = '\0';
#ifndef USE_FQDN
	    /* trim domain off name */
	    for (i = 0; i < strlen(dump_hostname); i++)
		if (dump_hostname[i] == '.')
		{
		    dump_hostname[i] = '\0';
		    break;
		}
#endif
	    line = stralloc2("HOST ", dump_hostname);
	    if (converse(line) == -1)
		exit(1);
	    afree(line);
	}

	if (server_happy())
	{
            /* get a starting disk and directory based on where
	       we currently are */
	    switch (guess_disk(cwd, sizeof(cwd), &dn_guess, &mpt_guess))
	    {
		case 1:
		    /* okay, got a guess. Set disk accordingly */
		    printf("$CWD '%s' is on disk '%s' mounted at '%s'.\n",
			   cwd, dn_guess, mpt_guess);
		    set_disk(dn_guess, mpt_guess);
		    afree(mpt_guess);
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
	afree(lineread);
    } while (!quit_prog);

    dbclose();

    aclose(server_socket);
    return 0;
}
