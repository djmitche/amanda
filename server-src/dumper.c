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
/* $Id: dumper.c,v 1.28 1997/09/26 11:24:35 george Exp $
 *
 * requests remote amandad processes to dump filesystems
 */
#include "amanda.h"
#include "conffile.h"
#include "logfile.h"
#include "stream.h"
#include "clock.h"
#include "protocol.h"
#include "version.h"
#include "arglist.h"
#include "amindex.h"
#include "token.h"


#ifdef KRB4_SECURITY
#include "dumper-krb4.c"
#else
#define NAUGHTY_BITS_INITIALIZE		/* I'd tell you what these do */
#define NAUGHTY_BITS			/* but then I'd have to kill you */
#endif


#ifndef SEEK_SET
#define SEEK_SET 0
#endif

#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif

#define CONNECT_TIMEOUT	5*60
#define READ_TIMEOUT	30*60
#define MAX_LINE	1024
#define MAX_ARGS	10
#define DATABUF_SIZE	32*1024
#define MESGBUF_SIZE	4*1024

#define STARTUP_TIMEOUT 60

typedef enum { BOGUS, FILE_DUMP, PORT_DUMP, CONTINUE, ABORT, QUIT } cmd_t;

char *pname = "dumper";

char line[MAX_LINE];
char *argv[MAX_ARGS+1];
int argc;
int interactive;
char *handle = NULL;
char loginid[80];
char *indexfile;

char databuf[DATABUF_SIZE];
char mesgbuf[MESGBUF_SIZE];
char errstr[256];
char *dataptr;		/* data buffer markers */
int spaceleft, abort_pending;
pid_t pid;
long dumpsize, origsize;
times_t runtime;
double dumptime;	/* Time dump took in secs */
static enum { srvcomp_none, srvcomp_fast, srvcomp_best } srvcompress;

char errfname[MAX_LINE];
FILE *errf;
char *filename = NULL;
char *hostname = NULL;
char *diskname = NULL;
char *options = NULL;
char *progname = NULL;
int level;
char *dumpdate = NULL;
char datestamp[80];

int datafd = -1;
int mesgfd = -1;
int indexfd = -1;
int amanda_port;

/* local functions */
int main P((int main_argc, char **main_argv));
static cmd_t getcmd P((void));
static void putresult P((char *format, ...))
#ifdef __GNUC__
     __attribute__ ((format (printf, 1, 2)))
#endif
     ;
static void do_dump P((int mesgfd, int datafd, int indexfd, int outfd));
void check_options P((char *options));
void service_ports_init P((void));
static void construct_datestamp P((char *buf));
int update_dataptr P((int outf, int size));
static void process_dumpeof P((void));
static void process_dumpline P((char *str));
static void add_msg_data P((char *str, int len));
static void log_msgout P((logtype_t typ));
char *diskname2filename P((char *dname));
void sendbackup_response P((proto_t *p, pkt_t *pkt));
int startup_dump P((char *hostname, char *disk, int level, char *dumpdate,
		    char *dumpname, char *options));


void check_options(options)
char *options;
{
#ifdef KRB4_SECURITY
    krb4_auth = strstr(options, "krb4-auth;") != NULL;
    kencrypt = strstr(options, "kencrypt;") != NULL;
#endif
    if (strstr(options, "srvcomp-best;") != NULL)
      srvcompress = srvcomp_best;
    else if (strstr(options, "srvcomp-fast;") != NULL)
      srvcompress = srvcomp_fast;
    else
      srvcompress = srvcomp_none;
}

void service_ports_init()
{
    struct servent *amandad;

    if((amandad = getservbyname(AMANDA_SERVICE_NAME, "udp")) == NULL) {
	amanda_port = AMANDA_SERVICE_DEFAULT;
	log(L_WARNING, "no %s/udp service, using default port %d",
	    AMANDA_SERVICE_NAME, AMANDA_SERVICE_DEFAULT);
    }
    else
	amanda_port = ntohs(amandad->s_port);

#ifdef KRB4_SECURITY
    if((amandad = getservbyname(KAMANDA_SERVICE_NAME, "udp")) == NULL) {
	kamanda_port = KAMANDA_SERVICE_DEFAULT;
	log(L_WARNING, "no %s/udp service, using default port %d",
	    KAMANDA_SERVICE_NAME, KAMANDA_SERVICE_DEFAULT);
    }
    else
	kamanda_port = ntohs(amandad->s_port);
#endif
}

static void construct_datestamp(buf)
char *buf;
{
    struct tm *tm;
    time_t timestamp;

    timestamp = time((time_t *)NULL);
    tm = localtime(&timestamp);
    sprintf(buf, "%04d%02d%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
}


int main(main_argc, main_argv)
int main_argc;
char **main_argv;
{
    cmd_t cmd;
    int outfd, protocol_port, taper_port, rc;
    struct passwd *pwptr;
    dgram_t *msg;

    erroutput_type = (ERR_AMANDALOG|ERR_INTERACTIVE);

    if(read_conffile(CONFFILE_NAME))
	error("could not read conf file");

    /* set up dgram port first thing */

    msg = dgram_alloc();
    if(dgram_bind(msg, &protocol_port) == -1)
	error("could not bind result datagram port: %s", strerror(errno));

    if(geteuid() == 0) {
	/* set both real and effective uid's to real uid, likewise for gid */
	setgid(getgid());
	setuid(getuid());
    }
#ifdef BSD_SECURITY
    else error("must be run setuid root to communicate correctly");
#endif

    fprintf(stderr,
	    "%s: pid %ld executable %s version %s, using port %d\n",
	    pname, (long) getpid(), main_argv[0], version(), protocol_port);
    fflush(stderr);

    /* now, find out who I'm running as */

    if((pwptr = getpwuid(getuid())) == NULL)
	error("can't get login name for my uid %ld", (long)getuid());
    strcpy(loginid, pwptr->pw_name);

    signal(SIGPIPE, SIG_IGN);

    interactive = isatty(0);
    pid = getpid();

    construct_datestamp(datestamp);

    service_ports_init();
    proto_init(msg->socket, time(0), 16);

    do {
	cmd = getcmd();

	switch(cmd) {
	case QUIT:
	    break;
	case FILE_DUMP:
	    /* FILE-DUMP handle filename host disk level dumpdate progname options */

	    assert(argc == 9);
	    if(handle) free(handle);
	    if(filename) free(filename);
	    if(hostname) free(hostname);
	    if(diskname) free(diskname);
	    if(dumpdate) free(dumpdate);
	    if(progname) free(progname);
	    if(options) free(options);
	    handle = stralloc(argv[2]);
	    filename = stralloc(argv[3]);
	    hostname = stralloc(argv[4]);
	    diskname = stralloc(argv[5]);
	    level = atoi(argv[6]);
	    dumpdate = stralloc(argv[7]);
	    progname = stralloc(argv[8]);
	    options = stralloc(argv[9]);

	    if((outfd = open(filename, O_WRONLY|O_CREAT, 0666)) == -1) {
		putresult("FAILED %s %s\n", handle,
			  squotef("[holding file \"%s\": %s]",
				 filename, strerror(errno))
			 );
		break;
	    }

	    check_options(options);

	    rc = startup_dump(hostname, diskname, level, dumpdate, progname, options);
	    if(rc) {
		putresult("%s %s %s\n", rc == 2? "FAILED" : "TRY-AGAIN",
			  handle, squote(errstr));
		/* do need to close if TRY-AGAIN, doesn't hurt otherwise */
		close(mesgfd);
		close(datafd);
		close(indexfd);
		close(outfd);
		break;
	    }

	    abort_pending = 0;
	    do_dump(mesgfd, datafd, indexfd, outfd);
	    close(mesgfd);
	    close(datafd);
	    close(indexfd);
	    close(outfd);
	    if(abort_pending) putresult("ABORT-FINISHED %s\n", handle);
	    break;

	case PORT_DUMP:

	    /* PORT-DUMP handle port host disk level dumpdate progname options */
	    assert(argc == 9);
	    if(handle) free(handle);
	    if(hostname) free(hostname);
	    if(diskname) free(diskname);
	    if(dumpdate) free(dumpdate);
	    if(progname) free(progname);
	    if(options) free(options);
	    handle = stralloc(argv[2]);
	    taper_port = atoi(argv[3]);
	    hostname = stralloc(argv[4]);
	    diskname = stralloc(argv[5]);
	    level = atoi(argv[6]);
	    dumpdate = stralloc(argv[7]);
	    progname = stralloc(argv[8]);
	    options = stralloc(argv[9]);

	    /* connect outf to taper port */

	    outfd = stream_client("localhost", taper_port,
				  DATABUF_SIZE, DEFAULT_SIZE);
	    if(outfd == -1) {
		putresult("FAILED %s %s\n", handle,
			  squotef("[taper port open: %s]", strerror(errno)));
		break;
	    }

	    check_options(options);

	    rc = startup_dump(hostname, diskname, level, dumpdate, progname, options);
	    if(rc) {
		putresult("%s %s %s\n", rc == 2? "FAILED" : "TRY-AGAIN",
			  handle, squote(errstr));
		/* do need to close if TRY-AGAIN, doesn't hurt otherwise */
		close(mesgfd);
		close(datafd);
		close(indexfd);
		close(outfd);
		break;
	    }

	    do_dump(mesgfd, datafd, indexfd, outfd);
	    close(mesgfd);
	    close(datafd);
	    close(indexfd);
	    close(outfd);
	    break;

	default:
	    putresult("BAD-COMMAND %s\n", squote(argv[1]));
	}
    } while(cmd != QUIT);
    return 0;
}

static cmd_t getcmd()
{
    if(interactive) {
	printf("%s> ", pname);
	fflush(stdout);
    }

    if(fgets(line, MAX_LINE, stdin) == NULL)
	return QUIT;

    argc = split(line, argv, MAX_ARGS+1, " ");

#if DEBUG
    {
      int arg;
      printf("argc = %d\n", argc);
      for(arg = 0; arg < MAX_ARGS+1; arg++)
	printf("argv[%d] = \"%s\"\n", arg, argv[arg]);
    }
#endif

    /* not enough commands for a table lookup */

    if(argc < 1) return BOGUS;
    if(!strcmp(argv[1],"FILE-DUMP")) return FILE_DUMP;
    if(!strcmp(argv[1],"PORT-DUMP")) return PORT_DUMP;
    if(!strcmp(argv[1],"CONTINUE")) return CONTINUE;
    if(!strcmp(argv[1],"ABORT")) return ABORT;
    if(!strcmp(argv[1],"QUIT")) return QUIT;
    return BOGUS;
}


arglist_function(static void putresult, char *, format)
{
    va_list argp;
    char result[MAX_LINE];

    arglist_start(argp, format);
    vsprintf(result, format, argp);
    arglist_end(argp);
    write(1, result, strlen(result));
}


int update_dataptr(outf, size)
int outf, size;
/*
 * Updates the buffer pointer for the input data buffer.  The buffer is
 * written if it is full, or the remainder is zeroed if at eof.
 */
{
    int rc;
    cmd_t cmd;
    off_t pos;

    spaceleft -= size;
    dataptr += size;

    if(size == 0) {	/* eof, zero rest of buffer */
	memset(dataptr, '\0', spaceleft);
	spaceleft = 0;
    }

    if(spaceleft == 0) {	/* buffer is full, write it */

	NAUGHTY_BITS;

	pos = lseek(outf, 0L, SEEK_CUR);
	while((rc = write(outf, databuf, DATABUF_SIZE)) < DATABUF_SIZE) {
	    if(rc >= 0) {
		/*
		 * Assuming this means we ran out of space part way through,
		 * go back to start of block and try again.
		 *
		 * This assumption may be false if we are catching signals.
		 */
		lseek(outf, pos, SEEK_SET);

	    } else if(errno != ENOSPC) {
		putresult("FAILED %s %s\n", handle,
			  squotef("[data write: %s]", strerror(errno)));
		return 1;
	    }
	    putresult("NO-ROOM %s\n", handle);
	    cmd = getcmd();
	    assert(cmd == CONTINUE || cmd == ABORT);
	    if(cmd == CONTINUE) continue;
	    abort_pending = 1;
	    return 1;
	}
	spaceleft = DATABUF_SIZE;
	dataptr = databuf;
	dumpsize += (DATABUF_SIZE/1024);
    }
    return 0;
}


static char msgbuf[MAX_LINE];
static int msgofs = 0;
int got_info_endline;
int got_sizeline;
int got_endline;
int dump_result;
char *backup_name, *recover_cmd, *compress_suffix;
#define max(a,b) (a>b?a:b)

static void process_dumpeof()
{
    /* process any partial line in msgbuf? !!! */
    if(msgofs != 0) {
	fprintf(errf,"? dumper: error [partial line in msgbuf: %d bytes]\n",
		msgofs);
    }
    if(!got_sizeline && dump_result < 2) {
	/* make a note if there isn't already a failure */
	fputs("? dumper: strange [missing size line from sendbackup]\n",errf);
	dump_result = max(dump_result, 1);
    }

    if(!got_endline && dump_result < 2) {
	fputs("? dumper: strange [missing end line from sendbackup]\n",errf);
	dump_result = max(dump_result, 1);
    }
}

/* Parse an information line from the client.
** We ignore unknown parameters and only remember the last
** of any duplicates.
*/
static void parse_info_line(str)
char *str;
{
    if(!strcmp(str, "end\n")) {
	got_info_endline = 1;
	return;
    }

    if(!strncmp(str, "BACKUP=", 7)) {
	backup_name = stralloc(str + 7);
	backup_name[strlen(backup_name)-1] = '\0';
	return;
    }

    if(!strncmp(str, "RECOVER_CMD=", 12)) {
	recover_cmd = stralloc(str + 12);
	recover_cmd[strlen(recover_cmd)-1] = '\0';
	return;
    }

    if(!strncmp(str, "COMPRESS_SUFFIX=", 16)) {
	compress_suffix = stralloc(str + 16);
	compress_suffix[strlen(compress_suffix)-1] = '\0';
	return;
    }
}

static void process_dumpline(str)
char *str;
{
    switch(str[0]) {
    case '|':
	/* normal backup output line */
	break;
    case '?':
	/* sendbackup detected something strange */
	dump_result = max(dump_result, 1);
	break;
    case 's':
	/* a sendbackup line, just check them all since there are only 5 */
	if(!strncmp(str, "sendbackup: start", 17)) {
	    break;
	}
	if(!strncmp(str, "sendbackup: size", 16)) {
	    origsize = (long)atof(str + 16);
	    got_sizeline = 1;
	    break;
	}
	if(!strncmp(str, "sendbackup: end", 15)) {
	    got_endline = 1;
	    break;
	}
	if(!strncmp(str, "sendbackup: error", 17)) {
	    got_endline = 1;
	    dump_result = max(dump_result, 2);
	    if(sscanf(str+18, "[%[^]]]", errstr) != 1)
		sprintf(errstr, "bad remote error: %s", str);
	    break;
	}
	if(!strncmp(str, "sendbackup: info ", 17)) {
	    parse_info_line(str + 17);
	    break;
	}
	/* else we fall through to bad line */
    default:
	fprintf(errf, "??%s", str);
	dump_result = max(dump_result, 1);
	return;
    }
    fprintf(errf, "%s", str);
}

static void add_msg_data(str, len)
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


static void log_msgout(typ)
logtype_t typ;
{
    char str[MAX_LINE];

    if((errf = fopen(errfname, "r")) == NULL)
	error("opening msg output: %s", strerror(errno));

    while(fgets(str, MAX_LINE, errf)) {
	str[strlen(str)-1] = '\0';
	log(typ, "%s", str);
    }
    fclose(errf);
}

/* ------------- */

char *diskname2filename(dname)
char *dname;
{
    static char filename[256];
    char *s, *d;

    for(s = dname, d = filename; *s != '\0'; s++, d++) {
	switch(*s) {
	case '/': *d = ':'; break;
	    /* the : -> ::  gives us 1-1 mapping of strings */
	case ':': *d++ = ':'; *d = ':'; break;
	default:  *d = *s;
	}
    }
    *d = '\0';
    return filename;
}

/* Send an Amanda dump header to the output file.
*/
int write_tapeheader(outfd)
int outfd;
{
    char line[128], unc[256];
    char buffer[TAPE_BLOCK_BYTES], *bufptr;
    int len;
    char *comp_suf;
    int count;

    if (srvcompress) {
	sprintf(unc, " %s %s |", UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
		UNCOMPRESS_OPT
#else
		""
#endif
		);
	comp_suf = COMPRESS_SUFFIX;
    }
    else {
	unc[0] = '\0';
	if(compress_suffix)
	    comp_suf = compress_suffix;
	else
	    comp_suf = "N";
    }

    sprintf(buffer, "AMANDA: FILE %s %s %s lev %d comp %s program %s\n",
	    datestamp, hostname, diskname, level, comp_suf, backup_name);

    strcat(buffer,"To restore, position tape at start of file and run:\n");

    sprintf(line, "\tdd if=<tape> bs=%dk skip=1 |%s %s\n\014\n",
	    TAPE_BLOCK_SIZE, unc, recover_cmd);
    strcat(buffer, line);

    len = strlen(buffer);
    memset(buffer + len, '\0', sizeof(buffer) - len);

    bufptr = buffer;
    for (count = sizeof(buffer); count > 0; ) {
	len = count > spaceleft ? spaceleft : count;
	memcpy(dataptr, bufptr, len);

	if (update_dataptr(outfd, len)) return 1;

	bufptr += len;
	count -= len;
    }

    return 0;
}


static void do_dump(mesgfd, datafd, indexfd, outfd)
int mesgfd, datafd, indexfd, outfd;
{
    int maxfd, nfound, size1, size2, eof1, eof2;
    fd_set readset, selectset;
    struct timeval timeout;
    int outpipe[2];
    int header_done;	/* flag - header has been written */

#ifndef DUMPER_SOCKET_BUFFERING
#define DUMPER_SOCKET_BUFFERING 0
#endif

#if !defined(SO_RCVBUF) || !defined(SO_RCVLOWAT)
#undef  DUMPER_SOCKET_BUFFERING
#define DUMPER_SOCKET_BUFFERING 0
#endif

#if DUMPER_SOCKET_BUFFERING
    int lowat = DATABUF_SIZE;
    int recbuf = 0;
    int lowwatset = 0;
    int sizeof_recbuf = sizeof(recbuf);
#endif

    startclock();

    dataptr = databuf;
    spaceleft = DATABUF_SIZE;
    dumpsize = origsize = dump_result = msgofs = 0;
    got_info_endline = got_sizeline = got_endline = 0;
    header_done = 0;
    backup_name = recover_cmd = compress_suffix = (char *)0;

    /* insert pipe in the *READ* side, if server-side compression is desired */
    if (srvcompress) {
	int tmpfd;

	tmpfd = datafd;
	pipe(outpipe); /* outpipe[0] is pipe's stdin, outpipe[1] is stdout. */
	datafd = outpipe[0];
	switch(fork()) {
	    case -1: fprintf(stderr, "couldn't fork\n");
	    default:
		close(outpipe[1]);
		close(tmpfd);
		break;
	    case 0:
		close(outpipe[0]);
		/* child acts on stdin/stdout */
		if (dup2(outpipe[1],1) == -1)
		    fprintf(stderr, "err dup2 out: %s\n", strerror(errno));
		if (dup2(tmpfd, 0) == -1)
		    fprintf(stderr, "err dup2 in: %s\n", strerror(errno));
		for(tmpfd = 3; tmpfd <= 255; ++tmpfd)
		    close(tmpfd);
		/* now spawn gzip -1 to take care of the rest */
		execlp(COMPRESS_PATH, COMPRESS_PATH,
		       (srvcompress == srvcomp_best
			? COMPRESS_BEST_OPT
			: COMPRESS_FAST_OPT),
		       (char *)0);
		error("error: couldn't exec %s.\n", COMPRESS_PATH);
	}
	/* Now the pipe has been inserted. */
    }

    /* start the index reader */
    if (indexfd != -1) {
	int tmpfd;

	indexfile = getindexname(getconf_str(CNF_INDEXDIR),
				 hostname, diskname, datestamp, level);
	strcat(indexfile, ".tmp");
	switch(fork()) {
	case -1: fprintf(stderr, "couldn't fork\n");
	default:
	    close(indexfd);
	    indexfd = -1;
	    break;
	case 0:
	    if (dup2(indexfd, 0) == -1)
		error("err dup2 in: %s", strerror(errno));
	    indexfd = open(indexfile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	    if (indexfd == -1)
		error("err open %s: %s", indexfile, strerror(errno));
	    if (dup2(indexfd,1) == -1)
		error("err dup2 out: %s", strerror(errno));
	    for(tmpfd = 3; tmpfd <= 255; ++tmpfd)
		close(tmpfd);
	    execlp(COMPRESS_PATH, COMPRESS_PATH, COMPRESS_BEST_OPT, (char *)0);
	    error("error: couldn't exec %s.", COMPRESS_PATH);
	}
    }

    sprintf(errfname, "/tmp/%s.%s.%d.errout", hostname,
	    diskname2filename(diskname), level);
    if((errf = fopen(errfname, "w")) == NULL) {
	sprintf(errstr,"errfile open \"%s\": %s", errfname, strerror(errno));
	goto failed;
    }

    NAUGHTY_BITS_INITIALIZE;

    maxfd = max(mesgfd, datafd) + 1;
    eof1 = eof2 = 0;

    FD_ZERO(&readset);

    /* Just process messages for now.  Once we have done the header
    ** we will start processing data too.
    */
    FD_SET(mesgfd, &readset);

    if(datafd == -1) eof1 = 1;	/* fake eof on data */

#if DUMPER_SOCKET_BUFFERING

#ifndef EST_PACKET_SIZE
#define EST_PACKET_SIZE	512
#endif
#ifndef	EST_MIN_WINDOW
#define	EST_MIN_WINDOW	EST_PACKET_SIZE*4 /* leave room for 2k in transit */
#endif

    else {
	recbuf = DATABUF_SIZE*2;
	if (setsockopt(datafd, SOL_SOCKET, SO_RCVBUF,
		       (void *) &recbuf, sizeof_recbuf)) {
	    const int errornumber = errno;
	    fprintf(stderr, "dumper: pid %ld setsockopt(SO_RCVBUF): %s\n",
		    (long) getpid(), strerror(errornumber));
	}
	if (getsockopt(datafd, SOL_SOCKET, SO_RCVBUF,
		       (void *) &recbuf, (void *)&sizeof_recbuf)) {
	    const int errornumber = errno;
	    fprintf(stderr, "dumper: pid %ld getsockopt(SO_RCVBUF): %s\n",
		    (long) getpid(), strerror(errornumber));
	    recbuf = 0;
	}

	/* leave at least EST_MIN_WINDOW between lowwat and recbuf */
	if (recbuf-lowat < EST_MIN_WINDOW)
	    lowat = recbuf-EST_MIN_WINDOW;

	/* if lowwat < ~512, don't bother */
	if (lowat < EST_PACKET_SIZE)
	    recbuf = 0;
    }
#endif

    while(!(eof1 && eof2)) {

#if DUMPER_SOCKET_BUFFERING
	/* Set socket buffering */
	if (recbuf>0 && !lowwatset) {
	    if (setsockopt(datafd, SOL_SOCKET, SO_RCVLOWAT,
			   (void *) &lowat, sizeof(lowat))) {
		const int errornumber = errno;
		fprintf(stderr,
			"dumper: pid %ld setsockopt(SO_RCVLOWAT): %s\n",
			(long) getpid(), strerror(errornumber));
	    }
	    lowwatset = 1;
	}
#endif

	timeout.tv_sec = READ_TIMEOUT;
	timeout.tv_usec = 0;
	memcpy(&selectset, &readset, sizeof(fd_set));

	nfound = select(maxfd, (SELECT_ARG_TYPE *)(&selectset), NULL, NULL, &timeout);

	/* check for errors or timeout */

#if DUMPER_SOCKET_BUFFERING
	if (nfound==0 && lowwatset) {
	    const int zero = 0;
	    /* Disable socket buffering and ... */
	    if (setsockopt(datafd, SOL_SOCKET, SO_RCVLOWAT,
			   (void *) &zero, sizeof(zero))) {
		const int errornumber = errno;
		fprintf(stderr,
			"dumper: pid %ld setsockopt(SO_RCVLOWAT): %s\n",
			(long) getpid(), strerror(errornumber));
	    }
	    lowwatset = 0;

	    /* ... try once more */
	    timeout.tv_sec = READ_TIMEOUT;
	    timeout.tv_usec = 0;
	    memcpy(&selectset, &readset, sizeof(fd_set));
	    nfound = select(maxfd, (SELECT_ARG_TYPE *)(&selectset), NULL, NULL, &timeout);
	}
#endif

	if(nfound == 0)  {
	    strcpy(errstr,"data timeout");
	    goto failed;
	}
	if(nfound == -1) {
	    sprintf(errstr,  "select: %s", strerror(errno));
	    goto failed;
	}

	/* read/write any data */

	if(FD_ISSET(datafd, &selectset)) {
	    size1 = read(datafd, dataptr, spaceleft);
	    switch(size1) {
	    case -1:
		sprintf(errstr, "data read: %s", strerror(errno));
		goto failed;
	    case 0:
		if(update_dataptr(outfd, size1)) return;
		eof1 = 1;
		close(datafd);
		FD_CLR(datafd, &readset);
		break;
	    default:
		if(update_dataptr(outfd, size1)) return;
	    }
	}

	if(FD_ISSET(mesgfd, &selectset)) {
	    size2 = read(mesgfd, mesgbuf, MESGBUF_SIZE);
	    switch(size2) {
	    case -1:
		sprintf(errstr, "mesg read: %s", strerror(errno));
		goto failed;
	    case 0:
		eof2 = 1;
		process_dumpeof();
		close(mesgfd);
		FD_CLR(mesgfd, &readset);
		break;
	    default:
		add_msg_data(mesgbuf, size2);
	    }

	    if (got_info_endline && !header_done) { /* time to do the header */
		if (write_tapeheader(outfd)) return;
		header_done = 1;

		if (datafd != -1)
		    FD_SET(datafd, &readset);	/* now we can read the data */
	    }
	}
    } /* end while */

    if(dump_result > 1) goto failed;

    runtime = stopclock();
    dumptime = runtime.r.tv_sec + runtime.r.tv_usec/1000000.0;

    dumpsize -= TAPE_BLOCK_SIZE;	/* don't count the header */
    if (dumpsize < 0) dumpsize = 0;	/* XXX - maybe this should be fatal? */

    sprintf(errstr, "sec %s kb %ld kps %3.1f orig-kb %ld",
	    walltime_str(runtime), dumpsize,
	    dumpsize/dumptime, origsize);
    putresult("DONE %s %ld %ld %ld %s\n", handle, origsize, dumpsize,
	      (long)(dumptime+0.5), squotef("[%s]", errstr));

    fclose(errf);

    switch(dump_result) {
    case 0:
	log(L_SUCCESS, "%s %s %d [%s]", hostname, diskname, level, errstr);

	break;

    case 1:
	log_start_multiline();
	log(L_STRANGE, "%s %s %d [%s]", hostname, diskname, level, errstr);
	log_msgout(L_STRANGE);
	log_end_multiline();

	break;
    }

    unlink(errfname);

    if (indexfile) {
	char tmpname[1024];
	strcpy(tmpname, indexfile);
	indexfile[strlen(indexfile)-4] = 0;
	unlink(indexfile);
	rename(tmpname, indexfile);
    }

    return;

failed:
    putresult("FAILED %s %s\n", handle, squotef("[%s]", errstr));

    fclose(errf);

    log_start_multiline();
    log(L_FAIL, "%s %s %d [%s]", hostname, diskname, level, errstr);
    log_msgout(L_FAIL);
    log_end_multiline();

    unlink(errfname);

    if (indexfile)
	unlink(indexfile);

    return;
}

/* -------------------- */

char *hostname, *disk;
int response_error;

void sendbackup_response(p, pkt)
proto_t *p;
pkt_t *pkt;
{
    char optionstr[512];
    int data_port, mesg_port, index_port;

    if(p->state == S_FAILED) {
	if(pkt == NULL) {
	    sprintf(errstr, "[request timeout]");
	    response_error = 1;
	    return;
	}
	else {
/*	    fprintf(stderr, "got nak response:\n----\n%s----\n\n", pkt->body); */
	    if(sscanf(pkt->body, "ERROR %[^\n]", errstr) != 1) {
/*		fprintf(stderr, "dumper: got strange NAK: %s", pkt->body); */
		strcpy(errstr, "[request NAK]");
	    }
	    response_error = 2;
	    return;
	}
    }

/*     fprintf(stderr, "got response:\n----\n%s----\n\n", pkt->body); */

#ifdef KRB4_SECURITY
    if(krb4_auth && !check_mutual_authenticator(&cred.session, pkt, p)) {
	sprintf(errstr, "[mutual-authentication failed]");
	response_error = 2;
	return;
    }
#endif

    if(!strncmp(pkt->body, "ERROR", 5)) {
	/* this is an error response packet */
	if(sscanf(pkt->body, "ERROR %[^\n]", errstr) != 1)
	    sprintf(errstr, "[bogus error packet]");
	response_error = 2;
	return;
    }

    if(sscanf(pkt->body,
	      "CONNECT DATA %d MESG %d INDEX %d\nOPTIONS %[^\n]\n",
	      &data_port, &mesg_port, &index_port, optionstr) != 4) {
	sprintf(errstr, "[parse of reply message failed]");
	response_error = 2;
	return;
    }

    datafd = stream_client(hostname, data_port,
			   DEFAULT_SIZE, DEFAULT_SIZE);
    if(datafd == -1) {
	sprintf(errstr,
		"[could not connect to data port: %s]", strerror(errno));
	response_error = 1;
	return;
    }
    mesgfd = stream_client(hostname, mesg_port,
			   DEFAULT_SIZE, DEFAULT_SIZE);
    if(mesgfd == -1) {
	sprintf(errstr,
		"[could not connect to mesg port: %s]", strerror(errno));
	close(datafd);
	datafd = -1;
	response_error = 1;
	return;
    }

    if (index_port != -1) {
	indexfd = stream_client(hostname, index_port,
				DEFAULT_SIZE, DEFAULT_SIZE);
	if (indexfd == -1) {
	    sprintf(errstr,
		"[could not connect to index port: %s]", strerror(errno));
	    close(datafd);
	    close(mesgfd);
	    datafd = mesgfd = -1;
	    response_error = 1;
	    return;
	}
    }

    /* everything worked */

#ifdef KRB4_SECURITY
    if(krb4_auth && kerberos_handshake(datafd, cred.session) == 0) {
	sprintf(errstr,
		"[mutual authentication in data stream failed]");
	close(datafd);
	close(mesgfd);
	close(indexfd);
	response_error = 1;
	return;
    }
    if(krb4_auth && kerberos_handshake(mesgfd, cred.session) == 0) {
	sprintf(errstr,
		"[mutual authentication in mesg stream failed]");
	close(datafd);
	close(indexfd);
	close(mesgfd);
	response_error = 1;
	return;
    }
#endif
    response_error = 0;
}

int startup_dump(hostname, disk, level, dumpdate, dumpname, options)
char *hostname, *disk, *dumpdate, *dumpname, *options;
int level;
{
    char req[8192];
    int rc;

    sprintf(req,
	    "SERVICE sendbackup\nOPTIONS hostname=%s;\n%s %s %d %s OPTIONS %s\n",
	    hostname, progname, disk, level, dumpdate, options);

    datafd = mesgfd = indexfd = -1;

#ifdef KRB4_SECURITY
    if(krb4_auth) {
	rc = make_krb_request(hostname, kamanda_port, req, NULL,
			      STARTUP_TIMEOUT, sendbackup_response);
	if(!rc) {
	    char inst[256], realm[256];
#define HOSTNAME_INSTANCE inst
	    /*
	     * This repeats a lot of work with make_krb_request, but it's
	     * ultimately the kerberos library's fault: krb_mk_req calls
	     * krb_get_cred, but doesn't make the session key available!
	     * XXX: But admittedly, we could restructure a bit here and
	     * at least eliminate the duplicate gethostbyname().
	     */
	    if(host2krbname(hostname, inst, realm) == 0)
		rc = -1;
	    else
		rc = krb_get_cred(CLIENT_HOST_PRINCIPLE, CLIENT_HOST_INSTANCE,
				  realm, &cred);
	    if(rc > 0 ) {
		sprintf(errstr, "[host %s: krb4 error (krb_get_cred) %d: %s]",
			hostname, rc, krb_err_txt[rc]);
		return 2;
	    }
	}
	if(rc > 0) {
	    sprintf(errstr, "[host %s: krb4 error (make_krb_req) %d: %s]",
		    hostname, rc, krb_err_txt[rc]);
	    return 2;
	}
    } else
#endif
	rc = make_request(hostname, amanda_port, req, NULL,
			  STARTUP_TIMEOUT, sendbackup_response);
    if(rc) {
	sprintf(errstr, "[could not resolve name \"%s\": error %d]",
		hostname, rc);
	return 2;
    }
    run_protocol();
    return response_error;
}
