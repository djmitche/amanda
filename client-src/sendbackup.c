/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1994,1997 University of Maryland
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
 * $Id: sendbackup.c,v 1.38 1998/03/14 07:06:43 amcore Exp $
 *
 * common code for the sendbackup-* programs.
 */

#include "sendbackup.h"
#include "stream.h"
#include "arglist.h"
#include "../tape-src/tapeio.h"

#define TIMEOUT 30

int comppid = -1;
int dumppid = -1;
int encpid = -1;
int indexpid = -1;
char *errorstr = NULL;

int data_socket, data_port, dataf;
int mesg_socket, mesg_port, mesgf;
int index_socket, index_port, indexf;

char *efile = NULL;
char *estr = NULL;
int compress, no_record, bsd_auth;
int createindex;
#define COMPR_FAST 1
#define COMPR_BEST 2

#ifdef KRB4_SECURITY
#include "sendbackup-krb4.h"
#else					/* I'd tell you what this does */
#define NAUGHTY_BITS			/* but then I'd have to kill you */
#endif

long dump_size = -1;

backup_program_t *program = NULL;

/* local functions */
int main P((int argc, char **argv));
void parse_options P((char *str));
char *optionstr P((void));
char *childstr P((int pid));
int check_status P((int pid, amwait_t w));

int pipefork P((void (*func) P((void)), char *fname, int *stdinfd,
		int stdoutfd, int stderrfd));
void parse_backup_messages P((int mesgin));
static void process_dumpline P((char *str));

void parse_options(str)
char *str;
{
    char *e, *i, *j, *k;
    int ch;

    /* only a few options, no need to get fancy */

    if(strstr(str, "compress") != NULL) {
	if(strstr(str, "compress-best") != NULL)
	    compress = COMPR_BEST;
	else
	    compress = COMPR_FAST;	/* the default */
    }

    if((i=strstr(str, "exclude")) != NULL) {
	if((j = strchr(i, '=')) != NULL && (k = strchr(j, ';')) != NULL) {
	    j++;				/* advance to after the '=' */

	    ch = k[1];				/* character after ';' */
	    k[1] = '\0';
	    estr = newstralloc(estr, i);	/* save the whole option */
	    k[1] = ch;

	    k[0] = '\0';			/* zap the ';' */

#define sc "exclude-list"
	    if(strncmp(sc, estr, sizeof(sc)-1) == 0) {
		e = "-from";
	    } else {
		e = "";
	    }
#undef sc
	    if (*e != '\0' && access(j, F_OK) != 0) {
		/* if exclude list file does not exist, ignore it.
		 * Should not test for R_OK, because the file may be
		 * readable by root only! */
		dbprintf(("%s: exclude list file \"%s\" does not exist, ignoring\n",
			  get_pname(), j));
	        afree(efile);
	    } else
	        efile = newvstralloc(efile, "--exclude", e, "=", j, NULL);
	    k[0] = ';';
	}
    }

    no_record = strstr(str, "no-record") != NULL;
    bsd_auth = strstr(str, "bsd-auth") != NULL;
#ifdef KRB4_SECURITY
    krb4_auth = strstr(str, "krb4-auth") != NULL;
    kencrypt = strstr(str, "kencrypt") != NULL;
#endif
    createindex = strstr(str, "index") != NULL;
}

char *optionstr()
{
    static char *optstr = NULL;
    char *compress_opt = "";
    char *record_opt = "";
    char *bsd_opt = "";
    char *krb4_opt = "";
    char *kencrypt_opt = "";
    char *index_opt = "";

    if(compress == COMPR_BEST)
	compress_opt = "compress-best;";
    else if(compress == COMPR_FAST)
	compress_opt = "compress-fast;";
    if(no_record) record_opt = "no-record;";
    if(bsd_auth) bsd_opt = "bsd-auth;";
#ifdef KRB4_SECURITY
    if(krb4_auth) krb4_opt = "krb4-auth;";
    if(kencrypt) kencrypt_opt = "kencrypt;";
#endif
    if(createindex) index_opt = "index;";

    optstr = newvstralloc(optstr,
			  ";",
			  compress_opt,
			  record_opt,
			  bsd_opt,
			  krb4_opt,
			  kencrypt_opt,
			  index_opt,
			  estr ? estr : "",
			  NULL);
    return optstr;
}

int main(argc, argv)
int argc;
char **argv;
{
    int interactive = 0;
    int level, mesgpipe[2];
    char *prog, *disk, *dumpdate, *options;
    char *host;				/* my hostname from the server */
    char *line = NULL;
    char *err_extra = NULL;
    char *s, *fp;
    int ch;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    int fd;

    /* initialize */

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
#ifdef KRB4_SECURITY
	if (fd != KEY_PIPE)	/* XXX interface needs to be fixed */
#endif
		close(fd);
    }

    set_pname("sendbackup");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    chdir("/tmp");
    interactive = (argc > 1 && strcmp(argv[1],"-t") == 0);
    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);
    umask(0007);
    dbopen();

    if(interactive) {
	/*
	 * In interactive (debug) mode, the backup data is sent to
	 * /dev/null, the program does not fork and none of the network
	 * connections back to driver programs on the tape host are set up.
	 * Index service is run and goes to stdout.
	 */
	fprintf(stderr, "%s: running in interactive test mode\n", get_pname());
	fflush(stderr);
    }

    host = alloc(MAX_HOSTNAME_LENGTH+1);
    gethostname(host, MAX_HOSTNAME_LENGTH);
    host[MAX_HOSTNAME_LENGTH] = '\0';

    /* parse dump request */

    if(interactive) {
	fprintf(stderr, "%s> ", get_pname());
	fflush(stderr);
    }

    if((line = agets(stdin)) == NULL) {
	err_extra = "no input";
	goto err;
    }

#define sc "OPTIONS"
    if(strncmp(line, sc, sizeof(sc)-1) == 0) {
#undef sc
#define sc "hostname="
	s = strstr(line, sc);
	if(s != NULL) {
	    s += sizeof(sc)-1;
	    ch = *s++;
#undef sc
	    fp = s-1;
	    while(ch != '\0' && ch != ';') ch = *s++;
	    s[-1] = '\0';
	    host = newstralloc(host, fp);
	}

	afree(line);
	if((line = agets(stdin)) == NULL) {
	    err_extra = "no request after OPTIONS";
	    goto err;
	}
    }

    dbprintf(("%s: got input request: %s\n", argv[0], line));

    s = line;
    ch = *s++;

    skip_whitespace(s, ch);			/* find the program name */
    if(ch == '\0') {
	err_extra = "no program name";
	goto err;
    }
    prog = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    skip_whitespace(s, ch);			/* find the disk name */
    if(ch == '\0') {
	err_extra = "no disk name";
	goto err;
    }
    disk = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    skip_whitespace(s, ch);			/* find the level number */
    if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	err_extra = "bad level";
	goto err;				/* bad level */
    }
    skip_integer(s, ch);

    skip_whitespace(s, ch);			/* find the dump date */
    if(ch == '\0') {
	err_extra = "no dump date";
	goto err;				/* no dump date */
    }
    dumpdate = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    skip_whitespace(s, ch);			/* find the options keyword */
    if(ch == '\0') {
	err_extra = "no options";
	goto err;				/* no options */
    }
#define sc "OPTIONS"
    if(strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	err_extra = "no OPTIONS keyword";
	goto err;				/* no options */
    }
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc
    skip_whitespace(s, ch);			/* find the options string */
    if(ch == '\0') {
	err_extra = "bad options string";
	goto err;				/* no options */
    }
    options = s - 1;

    dbprintf(("  parsed request as: program `%s' disk `%s' lev %d since %s opt `%s'\n",
	      prog, disk, level, dumpdate, options));

    {
      int i;
      for(i = 0; programs[i]; ++i)
	if (strcmp(programs[i]->name, prog) == 0)
	  break;
      if (programs[i]) {
	program = programs[i];
      } else {
	dbprintf(("ERROR [%s: unknown program %s]\n", argv[0], prog));
	error("ERROR [%s: unknown program %s]\n", argv[0], prog);
      }
    }

    parse_options(options);

#ifdef KRB4_SECURITY
    if(krb4_auth) {
	if(read(KEY_PIPE, session_key, sizeof session_key) 
	   != sizeof session_key) {
	  dbprintf(("ERROR [%s: could not read session key]\n", argv[0]));
	  error("ERROR [%s: could not read session key]\n", argv[0]);
	}
    }
#endif

    if(!interactive) {
      data_socket = stream_server(&data_port);
      mesg_socket = stream_server(&mesg_port);
    }
    if (!interactive && createindex) {
      index_socket = stream_server(&index_port);
    } else {
      index_port = -1;
    }

    if(!interactive) {
      switch(fork()) {
      case -1:	/* fork error */
	s = strerror(errno);
        dbprintf(("ERROR [%s: could not fork: %s]\n", argv[0], s));
        error("ERROR [%s: could not fork: %s]\n", argv[0], s);
      default:	/* parent */
        printf("CONNECT DATA %d MESG %d INDEX %d\n",
	       data_port, mesg_port, index_port);
        printf("OPTIONS %s\n", optionstr());
        exit(0);
      case 0:	/* child, keep going */
        break;
      }
    }

    if (createindex)
      dbprintf(("  waiting for connect on %d, then %d, then %d\n",
		data_port, mesg_port, index_port));
    else
      dbprintf(("  waiting for connect on %d, then %d\n",
		data_port, mesg_port));

    if(interactive) {
      if((dataf = open("/dev/null", O_RDWR)) < 0) {
	s = strerror(errno);
	dbprintf(("ERROR [%s: open of /dev/null for debug data stream: %s]\n",
		  argv[0], s));
	error("ERROR [%s: open of /dev/null for debug data stream: %s]\n",
		  argv[0], s);
      }
      mesgf = 2;
    } else {
      dataf = stream_accept(data_socket, TIMEOUT, DEFAULT_SIZE, DEFAULT_SIZE);
      if(dataf == -1) {
        dbprintf(("%s: timeout on data port %d\n", argv[0], data_port));
      }
      mesgf = stream_accept(mesg_socket, TIMEOUT, DEFAULT_SIZE, DEFAULT_SIZE);
      if(mesgf == -1) {
        dbprintf(("%s: timeout on mesg port %d\n", argv[0], mesg_port));
      }
    }
    if(interactive) {
      indexf = 1;
    } else if (createindex) {
      indexf = stream_accept(index_socket,
			     TIMEOUT, DEFAULT_SIZE, DEFAULT_SIZE);
      if (indexf == -1) {
	dbprintf(("%s: timeout on index port %d\n", argv[0], index_port));
      }
    }

    if(!interactive) {
      if(dataf == -1 || mesgf == -1 || (createindex && indexf == -1)) {
        dbclose();
        exit(1);
      }
    }

    dbprintf(("  got all connections\n"));

#ifdef KRB4_SECURITY
    if(!interactive) {
      if (krb4_auth) {
        if(kerberos_handshake(dataf, session_key) == 0) {
	    dbprintf(("kerberos_handshake on data socket failed\n"));
	    dbclose();
	    exit(1);
        }

        if(kerberos_handshake(mesgf, session_key) == 0) {
	    dbprintf(("kerberos_handshake on mesg socket failed\n"));
	    dbclose();
	    exit(1);
        }

        dbprintf(("%s: kerberos handshakes succeeded!\n", argv[0]));
      }
    }
#endif

    if(!interactive) {
      /* redirect stderr */
      if(dup2(mesgf, 2) == -1) {
	  dbprintf(("error redirecting stderr: %s\n", strerror(errno)));
	  dbclose();
	  exit(1);
      }
    }

    if(pipe(mesgpipe) == -1) {
      s = strerror(errno);
      dbprintf(("error [opening mesg pipe: %s]\n", s));
      error("error [opening mesg pipe: %s]", s);
    }

    program->start_backup(host, disk, level, dumpdate, dataf, mesgpipe[1],
			  indexf);
    parse_backup_messages(mesgpipe[0]);

    dbclose();

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    return 0;

 err:
    printf("FORMAT ERROR IN REQUEST PACKET\n");
    if(err_extra) {
	dbprintf(("REQ packet is bogus: %s\n", err_extra));
    } else {
	dbprintf(("REQ packet is bogus\n"));
    }
    dbclose();
    return 1;
}

char *childstr(pid)
int pid;
/*
 * Returns a string for a child process.  Checks the saved dump and
 * compress pids to see which it is.
 */
{
    if(pid == dumppid) return program->backup_name;
    if(pid == comppid) return "compress";
    if(pid == encpid)  return "kencrypt";
    if(pid == indexpid) return "index";
    return "unknown";
}


int check_status(pid, w)
int pid;
amwait_t w;
/*
 * Determine if the child return status really indicates an error.
 * If so, add the error message to the error string; more than one
 * child can have an error.
 */
{
    char *thiserr = NULL;
    char *str;
    int ret, sig;
    char number[NUM_STR_SIZE];

    if(WIFSIGNALED(w))	ret = 0, sig = WTERMSIG(w);
    else sig = 0, ret = WEXITSTATUS(w);

    str = childstr(pid);

#ifndef HAVE_GZIP
    if(pid == comppid) 	/* compress returns 2 sometimes; it's ok */
	if(ret == 2) return 0;
#endif

#ifdef DUMP_RETURNS_1
    if(pid == dumppid) /* Ultrix dump returns 1 sometimes; it's ok too */
        if(ret == 1) return 0;
#endif

    if(ret == 0) {
	ap_snprintf(number, sizeof(number), "%d", sig);
	thiserr = vstralloc(str, " got signal ", number, NULL);
    } else {
	ap_snprintf(number, sizeof(number), "%d", ret);
	thiserr = vstralloc(str, " returned ", number, NULL);
    }

    if(errorstr) {
	strappend(errorstr, ", ");
	strappend(errorstr, thiserr);
	afree(thiserr);
    } else {
	errorstr = thiserr;
	thiserr = NULL;
    }
    return 1;
}


/* Send header info to the message file.
*/
void write_tapeheader()
{
    fprintf(stderr, "%s: info BACKUP=%s\n", get_pname(), program->backup_name);

    fprintf(stderr, "%s: info RECOVER_CMD=", get_pname());
    if (compress)
	fprintf(stderr, "%s %s |", UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
		UNCOMPRESS_OPT
#else
		""
#endif
		);

    fprintf(stderr, "%s -f... -\n", program->restore_name);

    if (compress)
	fprintf(stderr, "%s: info COMPRESS_SUFFIX=%s\n",
			get_pname(), COMPRESS_SUFFIX);

    fprintf(stderr, "%s: info end\n", get_pname());
}

#ifdef STDC_HEADERS
int pipespawn(char *prog, int *stdinfd, int stdoutfd, int stderrfd, ...)
#else
int pipespawn(prog, stdinfd, stdoutfd, stderrfd, va_alist)
char *prog;
int *stdinfd;
int stdoutfd, stderrfd;
va_dcl
#endif
{
    va_list ap;
#define MAX_PIPESPAWN_ARGS	32
    char *argv[MAX_PIPESPAWN_ARGS+1];
    int pid, i, inpipe[2];
    char *e;

    dbprintf(("%s: spawning \"%s\" in pipeline\n", get_pname(), prog));
    dbprintf(("%s: argument list:", get_pname()));
    arglist_start(ap, stderrfd);		/* setup argv */
    for(i = 0; i < MAX_PIPESPAWN_ARGS; i++) {
	char *arg = arglist_val(ap, char *);
	if (arg == NULL)
	    break;
	dbprintf((" \"%s\"", arg));
    }
    arglist_end(ap);
    dbprintf(("\n"));

    if(pipe(inpipe) == -1) {
      e = strerror(errno);
      dbprintf(("error [open pipe to %s: %s]\n", prog, e));
      error("error [open pipe to %s: %s]", prog, e);
    }

    switch(pid = fork()) {
    case -1:
      e = strerror(errno);
      dbprintf(("error [fork %s: %s]\n", prog, e));
      error("error [fork %s: %s]", prog, e);
    default:	/* parent process */
	aclose(inpipe[0]);	/* close input side of pipe */
	*stdinfd = inpipe[1];
	break;
    case 0:		/* child process */
	aclose(inpipe[1]);	/* close output side of pipe */

	if(dup2(inpipe[0], 0) == -1) {
	  e = strerror(errno);
	  dbprintf(("error [spawn %s: dup2 in: %s]\n", prog, e));
	  error("error [spawn %s: dup2 in: %s]", prog, e);
	}
	if(dup2(stdoutfd, 1) == -1) {
	  e = strerror(errno);
	  dbprintf(("error [spawn %s: dup2 out: %s]\n", prog, e));
	  error("error [spawn %s: dup2 out: %s]", prog, e);
	}
	if(dup2(stderrfd, 2) == -1) {
	  e = strerror(errno);
	  dbprintf(("error [spawn %s: dup2 err: %s]\n", prog, e));
	  error("error [spawn %s: dup2 err: %s]", prog, e);
	}

	arglist_start(ap, stderrfd);		/* setup argv */
	for(i = 0; i < MAX_PIPESPAWN_ARGS; i++) {
            if ((argv[i] = arglist_val(ap, char *)) == NULL) {
		break;
	    }
	}
	argv[i] = NULL;
	arglist_end(ap);

	execve(prog, argv, safe_env());
	e = strerror(errno);
	dbprintf(("error [exec %s: %s]\n", prog, e));
	error("error [exec %s: %s]", prog, e);
	/* NOTREACHED */
    }
    return pid;
}


int pipefork(func, fname, stdinfd, stdoutfd, stderrfd)
void (*func) P((void));
char *fname;
int *stdinfd;
int stdoutfd, stderrfd;
{
    int pid, inpipe[2];
    char *e;

    dbprintf(("%s: forking function %s in pipeline\n", get_pname(), fname));

    if(pipe(inpipe) == -1) {
      e = strerror(errno);
      dbprintf(("error [open pipe to %s: %s]\n", fname, e));
      error("error [open pipe to %s: %s]", fname, e);
    }

    switch(pid = fork()) {
    case -1:
      e = strerror(errno);
      dbprintf(("error [fork %s: %s]\n", fname, e));
      error("error [fork %s: %s]", fname, e);
    default:	/* parent process */
	aclose(inpipe[0]);	/* close input side of pipe */
	*stdinfd = inpipe[1];
	break;
    case 0:		/* child process */
	aclose(inpipe[1]);	/* close output side of pipe */

	if(dup2(inpipe[0], 0) == -1) {
	  e = strerror(errno);
	  dbprintf(("error [fork %s: dup2 in: %s]\n", fname, e));
	  error("error [fork %s: dup2 in: %s]", fname, e);
	}
	if(dup2(stdoutfd, 1) == -1) {
	  e = strerror(errno);
	  dbprintf(("error [fork %s: dup2 out: %s]\n", fname, e));
	  error("error [fork %s: dup2 out: %s]", fname, e);
	}
	if(dup2(stderrfd, 2) == -1) {
	  e = strerror(errno);
	  dbprintf(("error [fork %s: dup2 err: %s]\n", fname, e));
	  error("error [fork %s: dup2 err: %s]", fname, e);
	}

	func();
	exit(0);
	/* NOTREACHED */
    }
    return pid;
}

void parse_backup_messages(mesgin)
int mesgin;
{
    int goterror, wpid;
    amwait_t retstat;
    char *line;

    goterror = 0;
    afree(errorstr);

    for(; (line = areads(mesgin)) != NULL; free(line)) {
	process_dumpline(line);
    }

    if(errno) {
	char *s = strerror(errno);

	dbprintf(("error [read mesg pipe: %s]\n", s));
	error("error [read mesg pipe: %s]", s);
	/* NOTREACHED */
    }

    while((wpid = wait(&retstat)) != -1) {
	/* we know that it exited, so we don't have to check WIFEXITED */
	if((WIFSIGNALED(retstat) || WEXITSTATUS(retstat)) &&
	   check_status(wpid, retstat))
	    goterror = 1;
    }

    if(goterror) {
      dbprintf(("error [%s]\n", errorstr ? errorstr : "(empty errorstr?)"));
      error("error [%s]", errorstr ? errorstr : "(empty errorstr?)");
    } else if(dump_size == -1) {
      dbprintf(("error [no backup size line]\n"));
      error("error [no backup size line]");
    }

    program->end_backup(goterror);

    fprintf(stderr, "%s: size %ld\n", get_pname(), dump_size);
    fprintf(stderr, "%s: end\n", get_pname());
}


double first_num P((char *str));
dmpline_t parse_dumpline P((char *str));

double first_num(str)
char *str;
/*
 * Returns the value of the first integer in a string.
 */
{
    char *num;
    int ch;
    double d;

    ch = *str++;
    while(ch && !isdigit(ch)) ch = *str++;
    num = str - 1;
    while(isdigit(ch) || ch == '.') ch = *str++;
    str[-1] = '\0';
    d = atof(num);
    str[-1] = ch;
    return d;
}

dmpline_t parse_dumpline(str)
char *str;
/*
 * Checks the dump output line in str against the regex table.
 */
{
    regex_t *rp;

    /* check for error match */
    for(rp = program->re_table; rp->regex != NULL; rp++) {
	if(match(rp->regex, str))
	    break;
    }
    if(rp->typ == DMP_SIZE) 
	dump_size = (long)((first_num(str) * rp->scale + 1023.0)/1024.0);
    return rp->typ;	
}


static void process_dumpline(str)
char *str;
{
    char startchr;
    dmpline_t typ;

    typ = parse_dumpline(str);
    switch(typ) {
    case DMP_NORMAL:
    case DMP_SIZE:
	startchr = '|';
	break;
    default:
    case DMP_STRANGE:
	startchr = '?';
	break;
    }
    fprintf(stderr, "%c %s\n", startchr, str);
}


/* start_index.  Creates an index file from the output of dump/tar.
   It arranges that input is the fd to be written by the dump process.
   If createindex is not enabled, it does nothing.  If it is not, a
   new process will be created that tees input both to a pipe whose
   read fd is dup2'ed input and to a program that outputs an index
   file to `index'.

   make sure that the chat from restore doesn't go to stderr cause
   this goes back to amanda which doesn't expect to see it
   (2>/dev/null should do it)

   Originally by Alan M. McIvor, 13 April 1996

   Adapted by Alexandre Oliva, 1 May 1997

   This program owes a lot to tee.c from GNU sh-utils and dumptee.c
   from the DeeJay backup package.

*/

static volatile int index_finished = 0;

static void index_closed(sig)
int sig;
{
  index_finished = 1;
}

void save_fd(fd, min)
int *fd, min;
{
  int origfd = *fd;

  while (*fd >= 0 && *fd < min) {
    int newfd = dup(*fd);
    if (newfd == -1)
      dbprintf(("unable to save file descriptor [%s]\n", strerror(errno)));
    *fd = newfd;
  }
  if (origfd != *fd)
    dbprintf(("dupped file descriptor %i to %i\n", origfd, *fd));
}

void start_index(createindex, input, mesg, index, cmd)
int createindex, input, mesg, index;
char *cmd;
{
  struct sigaction act, oact;
  int pipefd[2];
  FILE *pipe_fp;
  char *e;

  if (!createindex)
    return;

  if (pipe(pipefd) != 0) {
    e = strerror(errno);
    dbprintf(("creating index pipe: %s\n", e));
    error("creating index pipe: %s", e);
  }

  switch(indexpid = fork()) {
  case -1:
    e = strerror(errno);
    dbprintf(("forking index tee process: %s\n", e));
    error("forking index tee process: %s", e);

  default:
    aclose(pipefd[0]);
    if (dup2(pipefd[1], input) == -1) {
      e = strerror(errno);
      dbprintf(("dup'ping index tee output: %s", e));
      error("dup'ping index tee output: %s", e);
    }
    aclose(pipefd[1]);
    return;

  case 0:
    break;
  }

  /* now in a child process */
  save_fd(&pipefd[0], 4);
  save_fd(&index, 4);
  save_fd(&mesg, 4);
  save_fd(&input, 4);
  dup2(pipefd[0], 0);
  dup2(index, 1);
  dup2(mesg, 2);
  dup2(input, 3);
  for(index = 4; index < FD_SETSIZE; index++) {
    extern int db_fd;

    if (index != db_fd) {
      close(index);
    }
  }

  /* set up a signal handler for SIGPIPE for when the pipe is finished
     creating the index file */
  /* at that point we obviously want to stop writing to it */
  act.sa_handler = index_closed;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  if (sigaction(SIGPIPE, &act, &oact) != 0) {
    e = strerror(errno);
    dbprintf(("couldn't set index SIGPIPE handler [%s]\n", e));
    error("couldn't set index SIGPIPE handler [%s]", e);
  }

  if ((pipe_fp = popen(cmd, "w")) == NULL) {
    e = strerror(errno);
    dbprintf(("couldn't start index creator [%s]\n", e));
    error("couldn't start index creator [%s]", e);
  }

  dbprintf(("%s: started index creator: \"%s\"\n", get_pname(), cmd));
  while(1) {
    char buffer[BUFSIZ], *ptr;
    int bytes_read;
    int bytes_written;
    int just_written;

    bytes_read = read(0, buffer, sizeof(buffer));
    if ((bytes_read < 0) && (errno == EINTR))
      continue;

    if (bytes_read < 0) {
      e = strerror(errno);
      dbprintf(("index tee cannot read [%s]\n", e));
      error("index tee cannot read [%s]", e);
    }

    if (bytes_read == 0)
      break; /* finished */

    /* write the stuff to the subprocess */
    ptr = buffer;
    bytes_written = 0;
    while (bytes_read > bytes_written && !index_finished) {
      just_written = write(fileno(pipe_fp), ptr, bytes_read - bytes_written);
      if (just_written < 0) {
	  /* the signal handler may have assigned to index_finished
	   * just as we waited for write() to complete. */
	  if (!index_finished) {
	      e = strerror(errno);
	      dbprintf(("index tee cannot write to index creator [%s]\n", e));
#if 0
	      /* only write debugging info for write error to index
	       * pipe. */
	      error("index tee cannot write to index creator [%s]", e);
#endif
	      index_finished = 1;
	}
      } else {
	bytes_written += just_written;
	ptr += just_written;
      }
    }

    /* write the stuff to stdout, ensuring none lost when interrupt
       occurs */
    ptr = buffer;
    bytes_written = 0;
    while (bytes_read > bytes_written) {
      just_written = write(3, ptr, bytes_read - bytes_written);
      if ((just_written < 0) && (errno == EINTR))
	continue;
      if (just_written < 0) {
	e = strerror(errno);
	dbprintf(("index tee cannot write [%s]\n", e));
	error("index tee cannot write [%s]", e);
      } else {
	bytes_written += just_written;
	ptr += just_written;
      }
    }
  }

  aclose(pipefd[1]);

  /* finished */
  /* check the exit code of the pipe and moan if not 0 */
  if ((pipefd[0] = pclose(pipe_fp)) != 0) {
    e = strerror(errno);
    dbprintf(("index pipe returned %d [%s]\n", pipefd[0], e));
    error("index pipe returned %d [%s]", pipefd[0], e);
  }
  pipe_fp = NULL;

  dbprintf(("%s: index created successfully\n", get_pname()));
  exit(0);
}

extern backup_program_t dump_program, backup_program;

backup_program_t *programs[] = {
  &dump_program, &backup_program, NULL
};

#ifdef KRB4_SECURITY
#include "sendbackup-krb4.c"
#endif
