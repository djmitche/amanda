/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1994 University of Maryland
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
 * sendbackup-common.c - common code for the sendbackup-* programs.
 */

#include "sendbackup-common.h"
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

int comppid = -1;
int dumppid = -1;
int encpid = -1;
int indexpid = -1;
char thiserr[80], errorstr[256];

int data_socket, data_port, dataf;
int mesg_socket, mesg_port, mesgf;

char efile[256],estr[256];
char line[MAX_LINE];
char *pname = "sendbackup";
int compress, no_record, bsd_auth;
int createindex;
static int srvcompress=0;
#define COMPR_FAST 1
#define COMPR_BEST 2

#ifdef KRB4_SECURITY
#include "sendbackup-krb4.h"
#else					/* I'd tell you what this does */
#define NAUGHTY_BITS			/* but then I'd have to kill you */
#endif

long dump_size = -1;

/* local functions */
int main P((int argc, char **argv));
void parse_options P((char *str));
char *optionstr P((void));
char *childstr P((int pid));
int check_status P((int pid, int w));

int pipefork P((void (*func) P((void)), char *fname, int *stdinfd, \
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
    if(strstr(str, "srvcompress") != NULL) {
	srvcompress = 1;
	compress = 0; /* don't compress it twice :) */
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
    if (srvcompress)
	strcat(optstr, "srvcompress");

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
    char disk[1024], options[4096], datestamp[80];

    /* initialize */

    chdir("/tmp");
    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);
    umask(0);
    dbopen("/tmp/sendbackup.debug");
    {
	extern int db_file;
	dup2(db_file, 2);
    }

    /* parse dump request */

    if(fgets(line, MAX_LINE, stdin) == NULL)
	goto err;
    dbprintf(("%s: got input request: %s", argv[0], line));
    if(sscanf(line, "%s %d DATESTAMP %s OPTIONS %[^\n]\n", 
	      disk, &level, datestamp, options) != 4)
	goto err;
    dbprintf(("  parsed request as: disk `%s' lev %d stamp `%s' opt `%s'\n",
	      disk, level, datestamp, options));

    parse_options(options);

#ifdef KRB4_SECURITY
    if(krb4_auth) {
	if(read(KEY_PIPE, session_key, sizeof session_key) 
	   != sizeof session_key) {
	    printf("ERROR [%s: could not read session key]\n", argv[0]);
	    exit(0);
	}
    }
#endif

    data_socket = stream_server(&data_port);
    mesg_socket = stream_server(&mesg_port);

    switch(fork()) {
    case -1:	/* fork error */
	printf("ERROR [%s: could not fork: %s]\n", argv[0], strerror(errno));
	exit(0);
    default:	/* parent */
	printf("CONNECT DATA %d MESG %d\n", data_port, mesg_port);
	printf("OPTIONS %s\n", optionstr());
	exit(0);
    case 0:	/* child, keep going */
	break;
    }

    dbprintf(("  waiting for connect on %d, then %d\n", data_port, mesg_port));
    
    dataf = stream_accept(data_socket, TIMEOUT, DEFAULT_SIZE, DEFAULT_SIZE);
    if(dataf == -1)
	dbprintf(("%s: timeout on data port %d\n", argv[0], data_port));
    mesgf = stream_accept(mesg_socket, TIMEOUT, DEFAULT_SIZE, DEFAULT_SIZE);
    if(mesgf == -1)
	dbprintf(("%s: timeout on mesg port %d\n", argv[0], mesg_port));

    if(dataf == -1 || mesgf == -1) {
	dbclose();
	exit(1);
    }

    dbprintf(("  got both connections\n"));

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

    if(pipe(mesgpipe) == -1)
	error("error [opening mesg pipe: %s]", strerror(errno));


    start_backup(disk, level, datestamp, dataf, mesgpipe[1]);
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
    if(pid == dumppid) return backup_program_name;
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


void write_tapeheader(host, disk, level, compress, datestamp, outf)
char *host, *disk, *datestamp;
int outf, compress, level;
/*
 * writes an Amanda tape header onto the output file.
 */
{
    char line[128], unc[256], buffer[BUFFER_SIZE];
    int len, rc;

    /* if doing server compress, have the header say the dump is compressed
     * even though it isn't (yet)
     */
    sprintf(buffer, "AMANDA: FILE %s %s %s lev %d comp %s program %s\n",
            datestamp, host, disk, level, 
	    (compress||srvcompress)? COMPRESS_SUFFIX : "N", amanda_backup_program);

    strcat(buffer,"To restore, position tape at start of file and run:\n");

    if (compress||srvcompress)
	sprintf(unc, " %s %s |", UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
		UNCOMPRESS_OPT
#else
		""
#endif
		);
    else
	strcpy(unc, "");

    sprintf(line,"\tdd if=<tape> bs=%dk skip=1 |%s %s -f... -\n\014\n",
            BUFFER_SIZE/1024, unc, restore_program_name);
    strcat(buffer, line);

    len = strlen(buffer);
    memset(buffer+len, '\0', BUFFER_SIZE-len);
    if((rc = write(outf, buffer, BUFFER_SIZE)) < BUFFER_SIZE)
	error("error [write header: %s]", strerror(errno));
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
    char *environ[1], *argv[16];
    int pid, i, inpipe[2];

    dbprintf(("%s: spawning \"%s\" in pipeline\n", pname, prog));

    if(pipe(inpipe) == -1) 
	error("error [open pipe to %s: %s]", prog, strerror(errno));

    switch(pid = fork()) {
    case -1: error("error [fork %s: %s]", prog, strerror(errno));
    default:	/* parent process */
	close(inpipe[0]);	/* close input side of pipe */
	*stdinfd = inpipe[1];
	break;
    case 0:		/* child process */
	close(inpipe[1]);	/* close output side of pipe */

	if(dup2(inpipe[0], 0) == -1)
	    error("error [spawn %s: dup2 in: %s]", prog, strerror(errno));
	if(dup2(stdoutfd, 1) == -1)
	    error("error [spawn %s: dup2 out: %s]", prog, strerror(errno));
	if(dup2(stderrfd, 2) == -1)
	    error("error [spawn %s: dup2 err: %s]", prog, strerror(errno));

	arglist_start(ap, stderrfd);		/* setup argv */
	for(i=0; i<16 && (argv[i]=arglist_val(ap, char *)) != (char *)0; i++);
	if(i == 16) argv[15] = (char *)0;
	arglist_end(ap);

	environ[0] = (char *)0;	/* pass empty environment */

	execve(prog, argv, environ);
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

    if(pipe(inpipe) == -1) 
	error("error [open pipe to %s: %s]", fname, strerror(errno));

    switch(pid = fork()) {
    case -1: error("error [fork %s: %s]", fname, strerror(errno));
    default:	/* parent process */
	close(inpipe[0]);	/* close input side of pipe */
	*stdinfd = inpipe[1];
	break;
    case 0:		/* child process */
	close(inpipe[1]);	/* close output side of pipe */

	if(dup2(inpipe[0], 0) == -1)
	    error("error [fork %s: dup2 in: %s]", fname, strerror(errno));
	if(dup2(stdoutfd, 1) == -1)
	    error("error [fork %s: dup2 out: %s]", fname, strerror(errno));
	if(dup2(stderrfd, 2) == -1)
	    error("error [fork %s: dup2 err: %s]", fname, strerror(errno));

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

    if(goterror)
	error("error [%s]", errorstr);
    else if(dump_size == -1)
	error("error [no backup size line]");

    end_backup(goterror);

    /*
     * XXX Amanda 2.2 protocol compatibility requires that the dump
     * size be sent in bytes.  In order to handle filesystems larger
     * than 2 GB on 32-bit systems, we are recording the size in Kbytes.
     * To get to bytes here, we make the value a double.  In the future,
     * the protocol should be changed to send the size in kbytes.
     */
    fprintf(stderr, "%s: size %1.0f\n", pname, ((double)dump_size)*1024.0);
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
    while(*str && isdigit(*str)) *tp++ = *str++;
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
    for(rp = re_table; rp->regex != NULL; rp++) {
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
