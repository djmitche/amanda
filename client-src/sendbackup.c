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
 * $Id: sendbackup.c,v 1.19 1997/11/20 19:58:28 jrj Exp $
 *
 * common code for the sendbackup-* programs.
 */

#include "sendbackup.h"
#include "stream.h"
#include "arglist.h"
#include "../tape-src/tapeio.h"

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

int comppid = -1;
int dumppid = -1;
int encpid = -1;
int indexpid = -1;
char thiserr[80], errorstr[256];

int data_socket, data_port, dataf;
int mesg_socket, mesg_port, mesgf;
int index_socket, index_port, indexf;

extern int db_file;

char efile[256],estr[256];
char line[MAX_LINE];
char *pname = "sendbackup";
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
int check_status P((int pid, int w));

int pipefork P((void (*func) P((void)), char *fname, int *stdinfd,
		int stdoutfd, int stderrfd));
void parse_backup_messages P((int mesgin));
void add_msg_data P((char *str, int len));

void parse_options(str)
char *str;
{
    char *i,*j,*k,f[256];
    /* only a few options, no need to get fancy */

    if(strstr(str, "compress") != NULL) {
	if(strstr(str, "compress-best") != NULL)
	    compress = COMPR_BEST;
	else
	    compress = COMPR_FAST;	/* the default */
    }

    if((i=strstr(str, "exclude"))){
	memset(estr,0,sizeof(estr));
	memset(efile,0,sizeof(efile));
	memset(f,0,sizeof(f));
	j = strchr(i, '=') + 1;
	k = strchr(i, ';');
	strncpy(estr,i,abs(k-i)+1);
	strncpy(f,j,abs(k-j));
	sprintf(efile, "--exclude%s=%s",
		strncmp("exclude-list",estr,strlen("exclude-list")) ? 
		"" : "-from", f);
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
    static char optstr[256];

    strcpy(optstr,";");
    if(compress == COMPR_BEST)
	strcat(optstr, "compress-best;");
    else if(compress == COMPR_FAST)
	strcat(optstr, "compress-fast;");

    if(no_record) strcat(optstr, "no-record;");
    if(bsd_auth) strcat(optstr, "bsd-auth;");
#ifdef KRB4_SECURITY
    if(krb4_auth) strcat(optstr, "krb4-auth;");
    if(kencrypt) strcat(optstr, "kencrypt;");
#endif
    if (createindex) strcat(optstr, "index;");
    if(*estr) strcat(optstr, estr);

    return optstr;
}

int main(argc, argv)
int argc;
char **argv;
{
    int level, mesgpipe[2];
    char prog[80], disk[1024], options[4096];
    char dumpdate[256];
    char host[MAX_HOSTNAME_LENGTH];	/* my hostname from the server */

    /* initialize */

    chdir("/tmp");
    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);
    umask(0);
    dbopen();

    gethostname(host, sizeof(host)-1);

    /* parse dump request */

    if(fgets(line, MAX_LINE, stdin) == NULL)
	goto err;

    if(!strncmp(line, "OPTIONS", 7)) {
	char *str;

	str = strstr(line, "hostname=");
	if(str != NULL)
	    sscanf(str, "hostname=%[^;]", host);

	if(fgets(line, MAX_LINE, stdin) == NULL)
	    goto err;
    }

    dbprintf(("%s: got input request: %s", argv[0], line));
    if(sscanf(line, "%s %s %d %s OPTIONS %[^\n]\n", 
	      prog, disk, &level, dumpdate, options) != 5)
	goto err;
    dbprintf(("  parsed request as: program `%s' disk `%s' lev %d since %s opt `%s'\n",
	      prog, disk, level, dumpdate, options));

    {
      int i;
      for(i = 0; programs[i]; ++i)
	if (strcmp(programs[i]->name, prog) == 0)
	  break;
      if (programs[i])
	program = programs[i];
      else {
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

    data_socket = stream_server(&data_port);
    mesg_socket = stream_server(&mesg_port);
    if (createindex)
      index_socket = stream_server(&index_port);
    else
      index_port = -1;

    switch(fork()) {
    case -1:	/* fork error */
      dbprintf(("ERROR [%s: could not fork: %s]\n", argv[0], strerror(errno)));
      error("ERROR [%s: could not fork: %s]\n", argv[0], strerror(errno));
    default:	/* parent */
      printf("CONNECT DATA %d MESG %d INDEX %d\n",
	     data_port, mesg_port, index_port);
      printf("OPTIONS %s\n", optionstr());
      exit(0);
    case 0:	/* child, keep going */
      break;
    }

    if (createindex)
      dbprintf(("  waiting for connect on %d, then %d, then %d\n",
		data_port, mesg_port, index_port));
    else
      dbprintf(("  waiting for connect on %d, then %d\n",
		data_port, mesg_port));
    
    dataf = stream_accept(data_socket, TIMEOUT, DEFAULT_SIZE, DEFAULT_SIZE);
    if(dataf == -1)
      dbprintf(("%s: timeout on data port %d\n", argv[0], data_port));
    mesgf = stream_accept(mesg_socket, TIMEOUT, DEFAULT_SIZE, DEFAULT_SIZE);
    if(mesgf == -1)
      dbprintf(("%s: timeout on mesg port %d\n", argv[0], mesg_port));
    if (createindex) {
      indexf = stream_accept(index_socket, TIMEOUT, DEFAULT_SIZE, DEFAULT_SIZE);
      if (indexf == -1)
	dbprintf(("%s: timeout on index port %d\n", argv[0], index_port));
    }

    if(dataf == -1 || mesgf == -1 || (createindex && indexf == -1)) {
      dbclose();
      exit(1);
    }

    dbprintf(("  got all connections\n"));

#ifdef KRB4_SECURITY
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
#endif
	
    /* redirect stderr */
    if(dup2(mesgf, 2) == -1) {
	dbprintf(("error redirecting stderr: %s\n", strerror(errno)));
	dbclose();
	exit(1);
    }

    if(pipe(mesgpipe) == -1) {
      dbprintf(("error [opening mesg pipe: %s]\n", strerror(errno)));
      error("error [opening mesg pipe: %s]", strerror(errno));
    }

    program->start_backup(host, disk, level, dumpdate, dataf, mesgpipe[1],
			  indexf);
    parse_backup_messages(mesgpipe[0]);

    dbclose();
    return 0;

 err:
    printf("FORMAT ERROR IN REQUEST PACKET\n");
    dbprintf(("REQ packet is bogus\n"));
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
int pid, w;
/*
 * Determine if the child return status really indicates an error.
 * If so, add the error message to the error string; more than one
 * child can have an error.
 */
{
    char *str;
    int ret, sig;

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

    if(ret == 0) sprintf(thiserr, "%s got signal %d", str, sig);
    else sprintf(thiserr, "%s returned %d", str, ret);

    if(*errorstr != '\0') strcat(errorstr, ", ");
    strcat(errorstr, thiserr);
    return 1;
}


/* Send header info to the message file.
*/
void write_tapeheader()
{
    fprintf(stderr, "%s: info BACKUP=%s\n", pname, program->backup_name);

    fprintf(stderr, "%s: info RECOVER_CMD=", pname);
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
	fprintf(stderr, "%s: info COMPRESS_SUFFIX=%s\n", pname, COMPRESS_SUFFIX);

    fprintf(stderr, "%s: info end\n", pname);
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
    char *argv[16];
    int pid, i, inpipe[2];

    dbprintf(("%s: spawning \"%s\" in pipeline\n", pname, prog));

    if(pipe(inpipe) == -1) {
      dbprintf(("error [open pipe to %s: %s]\n", prog, strerror(errno)));
      error("error [open pipe to %s: %s]", prog, strerror(errno));
    }

    switch(pid = fork()) {
    case -1:
      dbprintf(("error [fork %s: %s]\n", prog, strerror(errno)));
      error("error [fork %s: %s]", prog, strerror(errno));
    default:	/* parent process */
	close(inpipe[0]);	/* close input side of pipe */
	*stdinfd = inpipe[1];
	break;
    case 0:		/* child process */
	close(inpipe[1]);	/* close output side of pipe */

	if(dup2(inpipe[0], 0) == -1) {
	  dbprintf(("error [spawn %s: dup2 in: %s]\n", prog, strerror(errno)));
	  error("error [spawn %s: dup2 in: %s]", prog, strerror(errno));
	}
	if(dup2(stdoutfd, 1) == -1) {
	  dbprintf(("error [spawn %s: dup2 out: %s]\n", prog, strerror(errno)));
	  error("error [spawn %s: dup2 out: %s]", prog, strerror(errno));
	}
	if(dup2(stderrfd, 2) == -1) {
	  dbprintf(("error [spawn %s: dup2 err: %s]\n", prog, strerror(errno)));
	  error("error [spawn %s: dup2 err: %s]", prog, strerror(errno));
	}

	arglist_start(ap, stderrfd);		/* setup argv */
	for(i=0; i<16 && (argv[i]=arglist_val(ap, char *)) != (char *)0; i++);
	if(i == 16) argv[15] = (char *)0;
	arglist_end(ap);

	execve(prog, argv, safe_env());
	dbprintf(("error [exec %s: %s]\n", prog, strerror(errno)));
	error("error [exec %s: %s]", prog, strerror(errno));
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

    dbprintf(("%s: forking function %s in pipeline\n", pname, fname));

    if(pipe(inpipe) == -1) {
      dbprintf(("error [open pipe to %s: %s]\n", fname, strerror(errno)));
      error("error [open pipe to %s: %s]", fname, strerror(errno));
    }

    switch(pid = fork()) {
    case -1:
      dbprintf(("error [fork %s: %s]\n", fname, strerror(errno)));
      error("error [fork %s: %s]", fname, strerror(errno));
    default:	/* parent process */
	close(inpipe[0]);	/* close input side of pipe */
	*stdinfd = inpipe[1];
	break;
    case 0:		/* child process */
	close(inpipe[1]);	/* close output side of pipe */

	if(dup2(inpipe[0], 0) == -1) {
	  dbprintf(("error [fork %s: dup2 in: %s]\n", fname, strerror(errno)));
	  error("error [fork %s: dup2 in: %s]", fname, strerror(errno));
	}
	if(dup2(stdoutfd, 1) == -1) {
	  dbprintf(("error [fork %s: dup2 out: %s]\n", fname, strerror(errno)));
	  error("error [fork %s: dup2 out: %s]", fname, strerror(errno));
	}
	if(dup2(stderrfd, 2) == -1) {
	  dbprintf(("error [fork %s: dup2 err: %s]\n", fname, strerror(errno)));
	  error("error [fork %s: dup2 err: %s]", fname, strerror(errno));
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
    int goterror, size, wpid, retstat;

    goterror = 0;
    *errorstr = '\0';

    do {
	size = read(mesgin, line, MAX_LINE);
	switch(size) {
	case -1:
	    dbprintf(("error [read mesg pipe: %s]\n", strerror(errno)));
	    error("error [read mesg pipe: %s]", strerror(errno));
	case 0:
	    close(mesgin);
	    break;
	default:
	    add_msg_data(line, size);
	    break;
	}
    } while(size != 0);

    while((wpid = wait(&retstat)) != -1) {
	/* we know that it exited, so we don't have to check WIFEXITED */
	if((WIFSIGNALED(retstat) || WEXITSTATUS(retstat)) &&
	   check_status(wpid, retstat))
	    goterror = 1;
    }

    if(goterror) {
      dbprintf(("error [%s]\n", errorstr));
      error("error [%s]", errorstr);
    } else if(dump_size == -1) {
      dbprintf(("error [no backup size line]\n"));
      error("error [no backup size line]");
    }

    program->end_backup(goterror);

    fprintf(stderr, "%s: size %ld\n", pname, dump_size);
    fprintf(stderr, "%s: end\n", pname);
}


double first_num P((char *str));
dmpline_t parse_dumpline P((char *str));
static void process_dumpline P((char *str));

double first_num(str)
char *str;
/*
 * Returns the value of the first integer in a string.
 */
{
    char tmp[16], *tp;

    tp = tmp;
    while(*str && !isdigit(*str)) str++;
    while(*str && (isdigit(*str) || (*str == '.'))) *tp++ = *str++;
    *tp = '\0';

    return atof(tmp);
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


static char msgbuf[MAX_LINE];
static int msgofs = 0;

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
    fprintf(stderr, "%c %s", startchr, str);
}


void add_msg_data(str, len)
char *str;
int len;
{
    char *nlpos;
    int len1, got_newline;

    while(len) {

	/* find a newline, if any */
	for(nlpos = str; nlpos < str + len; nlpos++)
	    if(*nlpos == '\n') break;

	/* copy up to newline (or whole string if none) into buffer */
	if(nlpos < str+len && *nlpos == '\n') {
	    got_newline = 1;
	    len1 = nlpos - str + 1;
	}
	else {
	    got_newline = 0;
	    len1 = len;
	}

	/* but don't overwrite the buffer */
	if(len1 + msgofs >= MAX_LINE) {
	    len1 = MAX_LINE-1 - msgofs;
	    str[len1-1] = '\n';			/* force newline */
	    got_newline = 1;
	}
	strncpy(msgbuf + msgofs, str, len1);
	msgofs += len1;
	msgbuf[msgofs] = '\0';

	if(got_newline) {
	    process_dumpline(msgbuf);
	    msgofs = 0;
	}
	len -= len1;
	str += len1;
    }
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

  if (!createindex)
    return;
    
  if (pipe(pipefd) != 0) {
    dbprintf(("creating index pipe: %s\n", strerror(errno)));
    error("creating index pipe: %s", strerror(errno));
  }

  switch(indexpid = fork()) {
  case -1:
    dbprintf(("forking index tee process: %s\n", strerror(errno)));
    error("forking index tee process: %s", strerror(errno));

  default:
    close(pipefd[0]);
    if (dup2(pipefd[1], input) == -1) {
      dbprintf(("dup'ping index tee output: %s", strerror(errno)));
      error("dup'ping index tee output: %s", strerror(errno));
    }
    close(pipefd[1]);
    return;

  case 0:
    break;
  }

  /* now in a child process */
  save_fd(&db_file, 4);
  save_fd(&pipefd[0], 4);
  save_fd(&index, 4);
  save_fd(&mesg, 4);
  save_fd(&input, 4);
  dup2(pipefd[0], 0);
  dup2(index, 1);
  dup2(mesg, 2);
  dup2(input, 3);
  for(index = 4; index <= 255; index++)
    if (index != db_file)
      close(index);

  /* set up a signal handler for SIGPIPE for when the pipe is finished
     creating the index file */
  /* at that point we obviously want to stop writing to it */
  act.sa_handler = index_closed;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  if (sigaction(SIGPIPE, &act, &oact) != 0) {
    dbprintf(("couldn't set index SIGPIPE handler [%s]\n", strerror(errno)));
    error("couldn't set index SIGPIPE handler [%s]", strerror(errno));
  }

  if ((pipe_fp = popen(cmd, "w")) == NULL) {
    dbprintf(("couldn't start index creator [%s]\n", strerror(errno)));
    error("couldn't start index creator [%s]", strerror(errno));
  }

  dbprintf(("%s: started index creator: \"%s\"\n", pname, cmd));
  while(1) {
    char buffer[BUFSIZ], *ptr;
    int bytes_read;
    int bytes_written;

    bytes_read = read(0, buffer, BUFSIZ);
    if ((bytes_read < 0) && (errno == EINTR))
      continue;

    if (bytes_read < 0) {
      dbprintf(("index tee cannot read [%s]\n", strerror(errno)));
      error("index tee cannot read [%s]", strerror(errno));
    }

    if (bytes_read == 0)
      break; /* finished */

    /* write the stuff to the subprocess */
    /* we are likely to be interrupted part way through one write by
       the subprocess finishing. But we don't care at that point */
    if (!index_finished)
      fwrite(buffer, sizeof(char), BUFSIZ, pipe_fp);

    /* write the stuff to stdout, ensuring none lost when interrupt
       occurs */
    ptr = buffer;
    while (bytes_read > 0) {
      bytes_written = write(3, ptr, bytes_read);
      if ((bytes_written < 0) && (errno == EINTR))
	continue;
      if (bytes_written < 0) {
	dbprintf(("index tee cannot write [%s]\n", strerror(errno)));
	error("index tee cannot write [%s]", strerror(errno));
      }
      bytes_read -= bytes_written;
      ptr += bytes_written;
    }
  }
    
  close(pipefd[1]);

  /* finished */
  /* check the exit code of the pipe and moan if not 0 */
  if ((pipefd[0] = pclose(pipe_fp)) != 0) {
    dbprintf(("index pipe returned %d [%s]\n", pipefd[0], strerror(errno)));
    error("index pipe returned %d [%s]", pipefd[0], strerror(errno));
  }

  dbprintf(("%s: index created successfully\n", pname));
  exit(0);
}

extern backup_program_t dump_program, backup_program;

backup_program_t *programs[] = {
  &dump_program, &backup_program, NULL
};

#ifdef KRB4_SECURITY
#include "sendbackup-krb4.c"
#endif
