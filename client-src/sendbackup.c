/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1999 University of Maryland at College Park
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
/* 
 * $Id: sendbackup.c,v 1.57 2002/02/13 14:47:47 martinea Exp $
 *
 * common code for the sendbackup-* programs.
 */

#include "amanda.h"
#include "sendbackup.h"
#include "pipespawn.h"
#include "amandad.h"
#include "arglist.h"
#include "getfsent.h"
#include "version.h"

#define TIMEOUT 30

int comppid = -1;
int dumppid = -1;
int tarpid = -1;
int encpid = -1;
int indexpid = -1;
char *errorstr = NULL;

int datafd;
int mesgfd;
int indexfd;

option_t *options;

long dump_size = -1;

backup_program_t *program = NULL;

/* local functions */
int main P((int argc, char **argv));
char *optionstr P((option_t *options));
char *childstr P((int pid));
int check_status P((int pid, amwait_t w));

int pipefork P((void (*func) P((void)), char *fname, int *stdinfd,
		int stdoutfd, int stderrfd));
void parse_backup_messages P((int mesgin));
static void process_dumpline P((char *str));
static void index_closed P((int));
static void save_fd P((int *, int));

char *optionstr(options)
option_t *options;
{
    static char *optstr = NULL;
    char *compress_opt = "";
    char *record_opt = "";
    char *bsd_opt = "";
    char *index_opt = "";
    char *exclude_file_opt;
    char *exclude_list_opt;
    char *exc = NULL;
    sle_t *excl;

    if(options->compress == COMPR_BEST)
	compress_opt = "compress-best;";
    else if(options->compress == COMPR_FAST)
	compress_opt = "compress-fast;";
    if(options->no_record) record_opt = "no-record;";
    if(options->bsd_auth) bsd_opt = "bsd-auth;";
    if(options->createindex) index_opt = "index;";

    exclude_file_opt = stralloc("");
    if(options->exclude_file) {
	for(excl = options->exclude_file->first; excl != NULL; excl=excl->next){
	    exc = newvstralloc(exc, "exclude-file=", excl->name, ";", NULL);
	    strappend(exclude_file_opt, exc);
	}
    }
    exclude_list_opt = stralloc("");
    if(options->exclude_list) {
	for(excl = options->exclude_list->first; excl != NULL; excl=excl->next){
	    exc = newvstralloc(exc, "exclude-list=", excl->name, ";", NULL);
	    strappend(exclude_list_opt, exc);
	}
    }
    optstr = newvstralloc(optstr,
			  ";",
			  compress_opt,
			  record_opt,
			  bsd_opt,
			  index_opt,
			  exclude_file_opt,
			  exclude_list_opt,
			  NULL);
    return optstr;
}


int main(argc, argv)
int argc;
char **argv;
{
    int interactive = 0;
    int level, mesgpipe[2];
    char *prog, *disk, *dumpdate, *stroptions;
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
	 *
	 * Skip over the file descriptors that are passed to us from
	 * amandad.
	 */
	if (fd == DATA_FD_OFFSET)
	    fd += DATA_FD_COUNT;
	close(fd);
    }

    safe_cd();

    set_pname("sendbackup");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    interactive = (argc > 1 && strcmp(argv[1],"-t") == 0);
    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);
    dbopen();
    dbprintf(("%s: version %s\n", argv[0], version()));

    if(interactive) {
	/*
	 * In interactive (debug) mode, the backup data is sent to
	 * /dev/null and none of the network connections back to driver
	 * programs on the tape host are set up.  The index service is
	 * run and goes to stdout.
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

	amfree(line);
	if((line = agets(stdin)) == NULL) {
	    err_extra = "no request after OPTIONS";
	    goto err;
	}
    }

    dbprintf(("%s: got input request: %s\n", get_pname(), line));

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
    stroptions = s - 1;

    dbprintf(("  parsed request as: program `%s'\n", prog));
    dbprintf(("                     disk `%s'\n", disk));
    dbprintf(("                     lev %d\n", level));
    dbprintf(("                     since %s\n", dumpdate));
    dbprintf(("                     opt `%s'\n", stroptions));

    {
      int i;
      for(i = 0; programs[i]; ++i)
	if (strcmp(programs[i]->name, prog) == 0)
	  break;
      if (programs[i]) {
	program = programs[i];
      } else {
	error("ERROR [%s: unknown program %s]", get_pname(), prog);
      }
    }

    options = parse_options(stroptions, disk, 0);

    if(!interactive) {
	datafd = DATA_FD_OFFSET + 0;
	mesgfd = DATA_FD_OFFSET + 1;
	indexfd = DATA_FD_OFFSET + 2;
    }
    if (!options->createindex)
	indexfd = -1;

    printf("CONNECT DATA %d MESG %d INDEX %d\n",
	   datafd, mesgfd, indexfd);
    printf("OPTIONS %s\n", optionstr(options));
    freopen("/dev/null","w",stdout);

    if(interactive) {
      if((datafd = open("/dev/null", O_RDWR)) < 0) {
	s = strerror(errno);
	error("ERROR [%s: open of /dev/null for debug data stream: %s]\n",
		  get_pname(), s);
      }
      mesgfd = 2;
      indexfd = 1;
    }

    if(!interactive) {
      if(datafd == -1 || mesgfd == -1 || (options->createindex && indexfd == -1)) {
        dbclose();
        exit(1);
      }
    }

    dbprintf(("  got all connections\n"));

    if(!interactive) {
      /* redirect stderr */
      if(dup2(mesgfd, 2) == -1) {
	  dbprintf(("error redirecting stderr to fd %d: %s\n", mesgfd,
	      strerror(errno)));
	  dbclose();
	  exit(1);
      }
    }

    if(pipe(mesgpipe) == -1) {
      error("error [opening mesg pipe: %s]", strerror(errno));
    }

    program->start_backup(host, disk, level, dumpdate, datafd, mesgpipe[1],
			  indexfd);
    dbprintf(("%s: started backup\n", get_pname()));
    parse_backup_messages(mesgpipe[0]);
    dbprintf(("%s: parsed backup messages\n", get_pname()));

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
    int ret, sig, rc;
    char number[NUM_STR_SIZE];

    str = childstr(pid);

    if(WIFSIGNALED(w)) {
	ret = 0;
	rc = sig = WTERMSIG(w);
    } else {
	sig = 0;
	rc = ret = WEXITSTATUS(w);
    }

    if(pid == indexpid) {
	/*
	 * Treat an index failure (other than signal) as a "STRANGE"
	 * rather than an error so the dump goes ahead and gets processed
	 * but the failure is noted.
	 */
	if(ret != 0) {
	    fprintf(stderr, "? %s returned %d\n", str, ret);
	    rc = 0;
	}
    }

#ifndef HAVE_GZIP
    if(pid == comppid) {
	/*
	 * compress returns 2 sometimes, but it is ok.
	 */
	if(ret == 2) {
	    rc = 0;
	}
    }
#endif

#ifdef DUMP_RETURNS_1
    if(pid == dumppid && tarpid == -1) {
        /*
	 * Ultrix dump returns 1 sometimes, but it is ok.
	 */
        if(ret == 1) {
	    rc = 0;
	}
    }
#endif

#ifdef IGNORE_TAR_ERRORS
    if(pid == tarpid) {
	/*
	 * tar bitches about active filesystems, but we do not care.
	 */
        if(ret == 2) {
	    rc = 0;
	}
    }
#endif

    if(rc == 0) {
	return 0;				/* normal exit */
    }

    if(ret == 0) {
	snprintf(number, sizeof(number), "%d", sig);
	thiserr = vstralloc(str, " got signal ", number, NULL);
    } else {
	snprintf(number, sizeof(number), "%d", ret);
	thiserr = vstralloc(str, " returned ", number, NULL);
    }

    if(errorstr) {
	strappend(errorstr, ", ");
	strappend(errorstr, thiserr);
	amfree(thiserr);
    } else {
	errorstr = thiserr;
	thiserr = NULL;
    }
    return 1;
}


/* Send header info to the message file.
*/
void info_tapeheader()
{
    fprintf(stderr, "%s: info BACKUP=%s\n", get_pname(), program->backup_name);

    fprintf(stderr, "%s: info RECOVER_CMD=", get_pname());
    if (options->compress)
	fprintf(stderr, "%s %s |", UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
		UNCOMPRESS_OPT
#else
		""
#endif
		);

    fprintf(stderr, "%s -f... -\n", program->restore_name);

    if (options->compress)
	fprintf(stderr, "%s: info COMPRESS_SUFFIX=%s\n",
			get_pname(), COMPRESS_SUFFIX);

    fprintf(stderr, "%s: info end\n", get_pname());
}

int pipefork(func, fname, stdinfd, stdoutfd, stderrfd)
void (*func) P((void));
char *fname;
int *stdinfd;
int stdoutfd, stderrfd;
{
    int pid, inpipe[2];

    dbprintf(("%s: forking function %s in pipeline\n", get_pname(), fname));

    if(pipe(inpipe) == -1) {
	error("error [open pipe to %s: %s]", fname, strerror(errno));
    }

    switch(pid = fork()) {
    case -1:
	error("error [fork %s: %s]", fname, strerror(errno));
    default:	/* parent process */
	aclose(inpipe[0]);	/* close input side of pipe */
	*stdinfd = inpipe[1];
	break;
    case 0:		/* child process */
	aclose(inpipe[1]);	/* close output side of pipe */

	if(dup2(inpipe[0], 0) == -1) {
	    error("error [fork %s: dup2(%d, in): %s]",
		  fname, inpipe[0], strerror(errno));
	}
	if(dup2(stdoutfd, 1) == -1) {
	    error("error [fork %s: dup2(%d, out): %s]",
		  fname, stdoutfd, strerror(errno));
	}
	if(dup2(stderrfd, 2) == -1) {
	    error("error [fork %s: dup2(%d, err): %s]",
		  fname, stderrfd, strerror(errno));
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
    amfree(errorstr);

    for(; (line = areads(mesgin)) != NULL; free(line)) {
	process_dumpline(line);
    }

    if(errno) {
	error("error [read mesg pipe: %s]", strerror(errno));
    }

    while((wpid = wait(&retstat)) != -1) {
	if(check_status(wpid, retstat)) goterror = 1;
    }

    if(errorstr) {
	error("error [%s]", errorstr);
    } else if(dump_size == -1) {
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

static void save_fd(fd, min)
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
  int exitcode;

  if (!createindex)
    return;

  if (pipe(pipefd) != 0) {
    error("creating index pipe: %s", strerror(errno));
  }

  switch(indexpid = fork()) {
  case -1:
    error("forking index tee process: %s", strerror(errno));

  default:
    aclose(pipefd[0]);
    if (dup2(pipefd[1], input) == -1) {
      error("dup'ping index tee output: %s", strerror(errno));
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
    error("couldn't set index SIGPIPE handler [%s]", strerror(errno));
  }

  if ((pipe_fp = popen(cmd, "w")) == NULL) {
    error("couldn't start index creator [%s]", strerror(errno));
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
      error("index tee cannot read [%s]", strerror(errno));
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
	      dbprintf(("index tee cannot write to index creator [%s]\n",
			strerror(errno)));
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
	error("index tee cannot write [%s]", strerror(errno));
      } else {
	bytes_written += just_written;
	ptr += just_written;
      }
    }
  }

  aclose(pipefd[1]);

  /* finished */
  /* check the exit code of the pipe and moan if not 0 */
  if ((exitcode = pclose(pipe_fp)) != 0) {
    dbprintf(("%s: index pipe returned %d\n", get_pname(), exitcode));
  } else {
    dbprintf(("%s: index created successfully\n", get_pname()));
  }
  pipe_fp = NULL;

  exit(exitcode);
}

extern backup_program_t dump_program, gnutar_program;

backup_program_t *programs[] = {
  &dump_program, &gnutar_program, NULL
};
