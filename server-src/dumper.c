/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998 University of Maryland at College Park
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
/* $Id: dumper.c,v 1.85 1998/12/10 23:39:58 kashmir Exp $
 *
 * requests remote amandad processes to dump filesystems
 */
#include "amanda.h"
#include "amindex.h"
#include "arglist.h"
#include "clock.h"
#include "conffile.h"
#include "logfile.h"
#include "protocol.h"
#include "stream.h"
#include "token.h"
#include "version.h"
#include "fileheader.h"

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
#define MAX_ARGS	10
/* Note: This must be kept in sync with the DATABUF_SIZE defined in
 * client-src/sendbackup-krb4.c, or kerberos encryption won't work...
 *	- Chris Ross (cross@uu.net)  4-Jun-1998
 */
#define DATABUF_SIZE	TAPE_BLOCK_BYTES
#define MESGBUF_SIZE	4*1024

#define STARTUP_TIMEOUT 60

typedef enum { BOGUS, FILE_DUMP, PORT_DUMP, CONTINUE, ABORT, QUIT } cmd_t;

struct cmdargs {
    int argc;
    char *argv[MAX_ARGS + 1];
};

struct databuf {
    int fd;
    char buf[DATABUF_SIZE];
    char *dataptr;		/* data buffer markers */
    int spaceleft;
};

int interactive;
char *handle = NULL;

char mesgbuf[MESGBUF_SIZE+1];
char *errstr = NULL;
int abort_pending;
long dumpsize, origsize;
int nb_header_block;
static enum { srvcomp_none, srvcomp_fast, srvcomp_best } srvcompress;

char *errfname = NULL;
FILE *errf = NULL;
char *filename = NULL;
char *hostname = NULL;
char *diskname = NULL;
char *options = NULL;
char *progname = NULL;
int level;
char *dumpdate = NULL;
long chunksize;
char *datestamp;
char *backup_name = NULL;
char *recover_cmd = NULL;
char *compress_suffix = NULL;
int conf_dtimeout;

dumpfile_t file;
int filename_seq;
long split_size;

int datafd = -1;
int mesgfd = -1;
int indexfd = -1;
int amanda_port;

/* local functions */
int main P((int main_argc, char **main_argv));
static cmd_t getcmd P((struct cmdargs *));
static void putresult P((char *format, ...))
    __attribute__ ((format (printf, 1, 2)));
int do_dump P((int mesgfd, int datafd, int indexfd, int outfd));
void check_options P((char *options));
void service_ports_init P((void));
static char *construct_datestamp P((void));
int write_tapeheader P((int outfd, dumpfile_t *type));
int update_dataptr P((struct databuf *, int size));
static void process_dumpeof P((void));
static void process_dumpline P((char *str));
static void add_msg_data P((char *str, int len));
static void log_msgout P((logtype_t typ));
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
	log_add(L_WARNING, "no %s/udp service, using default port %d",
	        AMANDA_SERVICE_NAME, AMANDA_SERVICE_DEFAULT);
    }
    else
	amanda_port = ntohs(amandad->s_port);

#ifdef KRB4_SECURITY
    if((amandad = getservbyname(KAMANDA_SERVICE_NAME, "udp")) == NULL) {
	kamanda_port = KAMANDA_SERVICE_DEFAULT;
	log_add(L_WARNING, "no %s/udp service, using default port %d",
	        KAMANDA_SERVICE_NAME, KAMANDA_SERVICE_DEFAULT);
    }
    else
	kamanda_port = ntohs(amandad->s_port);
#endif
}

static char *construct_datestamp()
{
    struct tm *tm;
    time_t timestamp;
    char datestamp[3*NUM_STR_SIZE];

    timestamp = time((time_t *)NULL);
    tm = localtime(&timestamp);
    ap_snprintf(datestamp, sizeof(datestamp),
		"%04d%02d%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
    return stralloc(datestamp);
}


int
main(main_argc, main_argv)
    int main_argc;
    char **main_argv;
{
    struct cmdargs cmdargs;
    cmd_t cmd;
    int outfd, protocol_port, taper_port, rc;
    dgram_t *msg;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    char *q = NULL;
    char *tmp_filename = NULL;

    for (outfd = 3; outfd < FD_SETSIZE; outfd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(outfd);
    }

    set_pname("dumper");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    erroutput_type = (ERR_AMANDALOG|ERR_INTERACTIVE);
    set_logerror(logerror);

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
	    get_pname(), (long) getpid(),
	    main_argv[0], version(), protocol_port);
    fflush(stderr);

    /* now, make sure we are a valid user */

    if (getpwuid(getuid()) == NULL)
	error("can't get login name for my uid %ld", (long)getuid());

    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    interactive = isatty(0);

    datestamp = construct_datestamp();
    conf_dtimeout = getconf_int(CNF_DTIMEOUT);

    service_ports_init();
    proto_init(msg->socket, time(0), 16);

    do {
	cmd = getcmd(&cmdargs);

	switch(cmd) {
	case QUIT:
	    break;

	case FILE_DUMP:
	    /*
	     * FILE-DUMP handle filename host disk level dumpdate chunksize
	     *   progname options
	     */
	    if (cmdargs.argc != 10)
		error("error [dumper FILE-DUMP argc != 10: %d]", cmdargs.argc);
	    handle = newstralloc(handle, cmdargs.argv[2]);
	    filename = newstralloc(filename, cmdargs.argv[3]);
	    hostname = newstralloc(hostname, cmdargs.argv[4]);
	    diskname = newstralloc(diskname, cmdargs.argv[5]);
	    level = atoi(cmdargs.argv[6]);
	    dumpdate = newstralloc(dumpdate, cmdargs.argv[7]);
	    chunksize = atoi(cmdargs.argv[8]);
	    chunksize = (chunksize/TAPE_BLOCK_SIZE)*TAPE_BLOCK_SIZE;
	    progname = newstralloc(progname, cmdargs.argv[9]);
	    options = newstralloc(options, cmdargs.argv[10]);

	    tmp_filename = newvstralloc(tmp_filename, filename, ".tmp", NULL);
	    if((outfd = open(tmp_filename, O_WRONLY|O_CREAT, 0666)) == -1) {
		q = squotef("[holding file \"%s\": %s]",
			    tmp_filename, strerror(errno));
		putresult("FAILED %s %s\n", handle, q);
		amfree(q);
		break;
	    }
	    filename_seq = 0;

	    check_options(options);

	    rc = startup_dump(hostname, diskname, level, dumpdate, progname,
		options);
	    if (rc != 0) {
		q = squote(errstr);
		putresult("%s %s %s\n", rc == 2 ? "FAILED" : "TRY-AGAIN",
		    handle, q);
		if (rc == 2)
		    log_add(L_FAIL, "%s %s %d [%s]", hostname, diskname, level,
			errstr);
		amfree(q);
	    } else {
		abort_pending = 0;
		split_size = chunksize;
		if (do_dump(mesgfd, datafd, indexfd, outfd)) {
		}
		if (abort_pending)
		    putresult("ABORT-FINISHED %s\n", handle);
	    }
	    break;

	case PORT_DUMP:
	    /*
	     * PORT-DUMP handle port host disk level dumpdate progname options
	     */
	    if (cmdargs.argc != 9)
		error("error [dumper PORT-DUMP argc != 9: %d]", cmdargs.argc);
	    handle = newstralloc(handle, cmdargs.argv[2]);
	    taper_port = atoi(cmdargs.argv[3]);
	    filename = newstralloc(filename, "<taper program>");
	    hostname = newstralloc(hostname, cmdargs.argv[4]);
	    diskname = newstralloc(diskname, cmdargs.argv[5]);
	    level = atoi(cmdargs.argv[6]);
	    dumpdate = newstralloc(dumpdate, cmdargs.argv[7]);
	    progname = newstralloc(progname, cmdargs.argv[8]);
	    options = newstralloc(options, cmdargs.argv[9]);

	    /* connect outf to taper port */

	    outfd = stream_client("localhost", taper_port,
				  DATABUF_SIZE, DEFAULT_SIZE, NULL);
	    if (outfd == -1) {
		q = squotef("[taper port open: %s]", strerror(errno));
		putresult("FAILED %s %s\n", handle, q);
		amfree(q);
		break;
	    }
	    filename_seq = 0;

	    check_options(options);

	    rc = startup_dump(hostname, diskname, level, dumpdate, progname,
		options);
	    if (rc != 0) {
		q = squote(errstr);
		putresult("%s %s %s\n", rc == 2? "FAILED" : "TRY-AGAIN",
		    handle, q);
		if (rc == 2)
		    log_add(L_FAIL, "%s %s %d [%s]", hostname, diskname, level,
			errstr);
		amfree(q);
	    } else {
		abort_pending = 0;
		split_size = -1;
		if (do_dump(mesgfd, datafd, indexfd, outfd)) {
		}
		if (abort_pending)
		    putresult("ABORT-FINISHED %s\n", handle);
	    }
	    break;

	default:
	    q = squote(cmdargs.argv[1]);
	    putresult("BAD-COMMAND %s\n", q);
	    amfree(q);
	    break;
	}

	while (wait(NULL) != -1)
	    continue;

	if (outfd != -1)
	    aclose(outfd);
	if (datafd != -1)
	    aclose(datafd);
	if (mesgfd != -1)
	    aclose(mesgfd);
	if (indexfd != -1)
	    aclose(indexfd);
    } while(cmd != QUIT);

    amfree(errstr);
    amfree(msg);
    amfree(datestamp);
    amfree(backup_name);
    amfree(recover_cmd);
    amfree(compress_suffix);
    amfree(handle);
    amfree(filename);
    amfree(hostname);
    amfree(diskname);
    amfree(dumpdate);
    amfree(progname);
    amfree(options);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if (malloc_size_1 != malloc_size_2)
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);

    exit(0);
}

static cmd_t
getcmd(cmdargs)
    struct cmdargs *cmdargs;
{
    static const struct {
	const char str[12];
	cmd_t cmd;
    } cmdtab[] = {
	{ "FILE-DUMP", FILE_DUMP },
	{ "PORT-DUMP", PORT_DUMP },
	{ "CONTINUE", CONTINUE },
	{ "ABORT", ABORT },
	{ "QUIT", QUIT },
    };
    char *line;
    int i;

    assert(cmdargs != NULL);

    if (interactive) {
	printf("%s> ", get_pname());
	fflush(stdout);
    }

    if ((line = agets(stdin)) == NULL)
	return (QUIT);

    cmdargs->argc = split(line, cmdargs->argv,
	sizeof(cmdargs->argv) / sizeof(cmdargs->argv[0]), " ");
    amfree(line);

#if DEBUG
    printf("argc = %d\n", cmdargs->argc);
    for (i = 0; i < cmdargs->argc; i++)
	printf("argv[%d] = \"%s\"\n", i, cmdargs->argv[i]);
#endif

    if (cmdargs->argc < 1)
	return (BOGUS);

    for (i = 0; i < sizeof(cmdtab) / sizeof(cmdtab[0]); i++)
	if (strcmp(cmdargs->argv[1], cmdtab[i].str) == 0)
	    return (cmdtab[i].cmd);
    return (BOGUS);
}


arglist_function(static void putresult, char *, format)
{
    va_list argp;

    arglist_start(argp, format);
    vprintf(format, argp);
    fflush(stdout);
    arglist_end(argp);
}


/*
 * Updates the buffer pointer for the input data buffer.  The buffer is
 * written if it is full, or the remainder is zeroed if at eof.
 */
int
update_dataptr(db, size)
    struct databuf *db;
    int size;
{
    struct cmdargs cmdargs;
    int written;
    cmd_t cmd;

    if (size == 0) {
	/* eof, zero rest of buffer */
	memset(db->dataptr, 0, db->spaceleft);
	db->spaceleft = 0;
    } else { 
	db->spaceleft -= size;
	db->dataptr += size;
    }

    if (db->spaceleft == 0) {	/* buffer is full, write it */

	NAUGHTY_BITS;

	if (split_size > 0 && dumpsize >= split_size) {
	    char *tmp_filename;
	    int fd;

	    /*
	     * First, update the header of the current file to point
	     * to the next chunk, and then close it.
	     */
	    fd = db->fd;
	    if (lseek(fd, (off_t)0, 0) < 0) {
		errstr = squotef("lseek holding file: %s", strerror(errno));
		return (-1);
	    }
	    ap_snprintf(file.cont_filename, sizeof(file.cont_filename),
		"%s.%d", filename, ++filename_seq);
	    write_tapeheader(fd, &file);
	    aclose(fd);

	    /*
	     * Now, open the new chunk file, and give it a new header
	     * that has no cont_filename pointer.
	     */
	    tmp_filename = vstralloc(file.cont_filename, ".tmp", NULL);
	    if ((fd = open(tmp_filename, O_WRONLY|O_CREAT, 0666)) == -1) {
		errstr = squotef("holding file \"%s\": %s",
			    tmp_filename, strerror(errno));
		amfree(tmp_filename);
		return (-1);
	    }
	    file.type = F_CONT_DUMPFILE;
	    file.cont_filename[0] = '\0';
	    write_tapeheader(fd, &file);
	    amfree(tmp_filename);

	    /*
	     * Now put give the new file the old file's descriptor
	     */
	    if (fd != db->fd) {
		if (dup2(fd, db->fd) == -1) {
		    errstr = squotef("can't dup2: %s", strerror(errno));
		    return (-1);
		}
		close(fd);
	    }

	    /*
	     * Update when we need to chunk again
	     */
	    split_size += chunksize;
	}

	/*
	 * Write out the buffer
	 */
	do {
	    written = write(db->fd, db->buf + db->spaceleft,
	    sizeof(db->buf) - db->spaceleft);
	    if (written > 0) {
		db->spaceleft += written;
		continue;
	    } else if (written < 0 && errno != ENOSPC) {
		errstr = squotef("data write: %s", strerror(errno));
		return (-1);
	    }

	    putresult("NO-ROOM %s\n", handle);
	    cmd = getcmd(&cmdargs);
	    switch (cmd) {
	    case ABORT:
		abort_pending = 1;
		errstr = "ERROR";
		return (-1);
	    case CONTINUE:
		continue;
	    default:
		error("error [bad command after NO-ROOM: %d]", cmd);
	    }
	} while (db->spaceleft != sizeof(db->buf));
	db->dataptr = db->buf;
	dumpsize += (sizeof(db->buf) / 1024);
    }
    return (0);
}


static char *msgbuf = NULL;
int got_info_endline;
int got_sizeline;
int got_endline;
int dump_result;
#define max(a,b) ((a)>(b)?(a):(b))

static void process_dumpeof()
{
    /* process any partial line in msgbuf? !!! */
    if(msgbuf != NULL) {
	fprintf(errf,"? dumper: error [partial line in msgbuf: %ld bytes]\n",
		(long int) strlen(msgbuf));
	fprintf(errf,"? dumper: error [partial line in msgbuf: \"%s\"]\n",
		msgbuf);
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
    if(strcmp(str, "end") == 0) {
	got_info_endline = 1;
	return;
    }

#define sc "BACKUP="
    if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	backup_name = newstralloc(backup_name, str + sizeof(sc)-1);
	return;
    }
#undef sc

#define sc "RECOVER_CMD="
    if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	recover_cmd = newstralloc(recover_cmd, str + sizeof(sc)-1);
	return;
    }
#undef sc

#define sc "COMPRESS_SUFFIX="
    if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	compress_suffix = newstralloc(compress_suffix, str + sizeof(sc)-1);
	return;
    }
#undef sc
}

static void process_dumpline(str)
char *str;
{
    char *s, *fp;
    int ch;

    s = str;
    ch = *s++;

    switch(ch) {
    case '|':
	/* normal backup output line */
	break;
    case '?':
	/* sendbackup detected something strange */
	dump_result = max(dump_result, 1);
	break;
    case 's':
	/* a sendbackup line, just check them all since there are only 5 */
#define sc "sendbackup: start"
	if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	    break;
	}
#undef sc
#define sc "sendbackup: size"
	if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	    s += sizeof(sc)-1;
	    ch = s[-1];
	    skip_whitespace(s, ch);
	    if(ch) {
		origsize = (long)atof(str + sizeof(sc)-1);
		got_sizeline = 1;
		break;
	    }
	}
#undef sc
#define sc "sendbackup: end"
	if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	    got_endline = 1;
	    break;
	}
#undef sc
#define sc "sendbackup: error"
	if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	    s += sizeof(sc)-1;
	    ch = s[-1];
#undef sc
	    got_endline = 1;
	    dump_result = max(dump_result, 2);
	    skip_whitespace(s, ch);
	    if(ch == '\0' || ch != '[') {
		errstr = newvstralloc(errstr,
				      "bad remote error: ", str,
				      NULL);
	    } else {
		ch = *s++;
		fp = s - 1;
		while(ch && ch != ']') ch = *s++;
		s[-1] = '\0';
		errstr = newstralloc(errstr, fp);
		s[-1] = ch;
	    }
	    break;
	}
#define sc "sendbackup: info"
	if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	    s += sizeof(sc)-1;
	    ch = s[-1];
	    skip_whitespace(s, ch);
	    parse_info_line(s - 1);
	    break;
	}
#undef sc
	/* else we fall through to bad line */
    default:
	fprintf(errf, "??%s", str);
	dump_result = max(dump_result, 1);
	return;
    }
    fprintf(errf, "%s\n", str);
}

static void add_msg_data(str, len)
char *str;
int len;
{
    char *t;
    char *nl;

    while(len > 0) {
	if((nl = strchr(str, '\n')) != NULL) {
	    *nl = '\0';
	}
	if(msgbuf) {
	    t = stralloc2(msgbuf, str);
	    amfree(msgbuf);
	    msgbuf = t;
	} else if(nl == NULL) {
	    msgbuf = stralloc(str);
	} else {
	    msgbuf = str;
	}
	if(nl == NULL) break;
	process_dumpline(msgbuf);
	if(msgbuf != str) free(msgbuf);
	msgbuf = NULL;
	len -= nl + 1 - str;
	str = nl + 1;
    }
}


static void log_msgout(typ)
logtype_t typ;
{
    char *line = NULL;

    if((errf = fopen(errfname, "r")) == NULL)
	error("opening msg output: %s", strerror(errno));

    for(; (line = agets(errf)) != NULL; free(line)) {
	log_add(typ, "%s", line);
    }
    afclose(errf);
}

/* ------------- */

void make_tapeheader(file, type)
dumpfile_t *file;
filetype_t type;
{
    fh_init(file);
    file->type = type;
    strncpy(file->datestamp  , datestamp  , sizeof(file->datestamp)-1);
    file->datestamp[sizeof(file->datestamp)-1] = '\0';
    strncpy(file->name       , hostname   , sizeof(file->name)-1);
    file->name[sizeof(file->name)-1] = '\0';
    strncpy(file->disk       , diskname   , sizeof(file->disk)-1);
    file->disk[sizeof(file->disk)-1] = '\0';
    file->dumplevel = level;
    strncpy(file->program    , backup_name, sizeof(file->program)-1);
    file->program[sizeof(file->program)-1] = '\0';
    strncpy(file->recover_cmd, recover_cmd, sizeof(file->recover_cmd)-1);
    file->recover_cmd[sizeof(file->recover_cmd)-1] = '\0';

    if (srvcompress) {
	file->compressed=1;
	ap_snprintf(file->uncompress_cmd, sizeof(file->uncompress_cmd),
		    " %s %s |", UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
		    UNCOMPRESS_OPT
#else
		    ""
#endif
		    );
	strncpy(file->comp_suffix, COMPRESS_SUFFIX,sizeof(file->comp_suffix)-1);
	file->comp_suffix[sizeof(file->comp_suffix)-1] = '\0';
    }
    else {
	file->uncompress_cmd[0] = '\0';
	file->compressed=compress_suffix!=NULL;
	if(compress_suffix) {
	    strncpy(file->comp_suffix, compress_suffix,
		    sizeof(file->comp_suffix)-1);
	    file->comp_suffix[sizeof(file->comp_suffix)-1] = '\0';
	} else {
	    strncpy(file->comp_suffix, "N", sizeof(file->comp_suffix)-1);
	    file->comp_suffix[sizeof(file->comp_suffix)-1] = '\0';
	}
    }
    file->cont_filename[0] = '\0';
}

/* Send an Amanda dump header to the output file.
*/

int
write_tapeheader(outfd, file)
    int outfd;
    dumpfile_t *file;
{
    char buffer[TAPE_BLOCK_BYTES];

    write_header(buffer, file, sizeof(buffer));
    write(outfd, buffer, sizeof(buffer));
    return (0);
}


int do_dump(mesgfd, datafd, indexfd, outfd)
int mesgfd, datafd, indexfd, outfd;
{
    static struct databuf db;
    int maxfd, nfound, size1, size2, eof1, eof2;
    fd_set readset, selectset;
    struct timeval timeout;
    int outpipe[2];
    int header_done;	/* flag - header has been written */
    char *indexfile = NULL;
    char level_str[NUM_STR_SIZE];
    char kb_str[NUM_STR_SIZE];
    char kps_str[NUM_STR_SIZE];
    char orig_kb_str[NUM_STR_SIZE];
    char *fn;
    char *q;
    times_t runtime;
    double dumptime;	/* Time dump took in secs */
    int compresspid, indexpid, killerr;

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

    db.fd = outfd;
    db.dataptr = db.buf;
    db.spaceleft = sizeof(db.buf);
    dumpsize = origsize = dump_result = 0;
    dumpsize = TAPE_BLOCK_SIZE;
    nb_header_block = 1;
    got_info_endline = got_sizeline = got_endline = 0;
    header_done = 0;
    amfree(backup_name);
    amfree(recover_cmd);
    amfree(compress_suffix);

    ap_snprintf(level_str, sizeof(level_str), "%d", level);
    fn = sanitise_filename(diskname);
    errfname = newvstralloc(errfname,
			    "/tmp",
			    "/", hostname,
			    ".", fn,
			    ".", level_str,
			    ".errout",
			    NULL);
    amfree(fn);
    if((errf = fopen(errfname, "w")) == NULL) {
	errstr = newvstralloc(errstr,
			      "errfile open \"", errfname, "\": ",
			      strerror(errno),
			      NULL);
	amfree(errfname);
	goto failed;
    }

    /* insert pipe in the *READ* side, if server-side compression is desired */
    compresspid = -1;
    if (srvcompress) {
	int tmpfd;

	tmpfd = datafd;
	pipe(outpipe); /* outpipe[0] is pipe's stdin, outpipe[1] is stdout. */
	datafd = outpipe[0];
	if(datafd < 0 || datafd >= FD_SETSIZE) {
	    aclose(outpipe[0]);
	    aclose(outpipe[1]);
	    errstr = newstralloc(errstr, "descriptor out of range");
	    errno = EMFILE;
	    goto failed;
	}
	switch(compresspid=fork()) {
	case -1:
	    errstr = newstralloc2(errstr, "couldn't fork: ", strerror(errno));
	    goto failed;
	default:
	    aclose(outpipe[1]);
	    aclose(tmpfd);
	    break;
	case 0:
	    aclose(outpipe[0]);
	    /* child acts on stdin/stdout */
	    if (dup2(outpipe[1],1) == -1)
		fprintf(stderr, "err dup2 out: %s\n", strerror(errno));
	    if (dup2(tmpfd, 0) == -1)
		fprintf(stderr, "err dup2 in: %s\n", strerror(errno));
	    for(tmpfd = 3; tmpfd <= FD_SETSIZE; ++tmpfd) {
		close(tmpfd);
	    }
	    /* now spawn gzip -1 to take care of the rest */
	    execlp(COMPRESS_PATH, COMPRESS_PATH,
		   (srvcompress == srvcomp_best ? COMPRESS_BEST_OPT
						: COMPRESS_FAST_OPT),
		   (char *)0);
	    error("error: couldn't exec %s.\n", COMPRESS_PATH);
	}
	/* Now the pipe has been inserted. */
    }

    indexpid = -1;
    if (indexfd != -1) {
	int tmpfd;

	indexfile = vstralloc(getconf_str(CNF_INDEXDIR),
			      "/",
			      getindexfname(hostname, diskname,
					    datestamp, level),
			      ".tmp",
			      NULL);

	if (mkpdir(indexfile, 0755, (uid_t)-1, (gid_t)-1) == -1) {
	   errstr = newvstralloc(errstr,
				 "err create ",
				 indexfile,
				 ": ",
				 strerror(errno),
				 NULL);
	   amfree(indexfile);
	   goto failed;
	}

	switch(indexpid=fork()) {
	case -1:
	    errstr = newstralloc2(errstr, "couldn't fork: ", strerror(errno));
	    goto failed;
	default:
	    aclose(indexfd);
	    indexfd = -1;			/* redundant */
	    break;
	case 0:
	    if (dup2(indexfd, 0) == -1)
		error("err dup2 in: %s", strerror(errno));
	    indexfd = open(indexfile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	    if (indexfd == -1)
		error("err open %s: %s", indexfile, strerror(errno));
	    if (dup2(indexfd,1) == -1)
		error("err dup2 out: %s", strerror(errno));
	    for(tmpfd = 3; tmpfd <= FD_SETSIZE; ++tmpfd) {
		close(tmpfd);
	    }
	    execlp(COMPRESS_PATH, COMPRESS_PATH, COMPRESS_BEST_OPT, (char *)0);
	    error("error: couldn't exec %s.", COMPRESS_PATH);
	}
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

	timeout.tv_sec = conf_dtimeout;
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
	    timeout.tv_sec = conf_dtimeout;
	    timeout.tv_usec = 0;
	    memcpy(&selectset, &readset, sizeof(fd_set));
	    nfound = select(maxfd, (SELECT_ARG_TYPE *)(&selectset), NULL, NULL, &timeout);
	}
#endif

	if(nfound == 0)  {
	    errstr = newstralloc(errstr, "data timeout");
	    goto failed;
	}
	if(nfound == -1) {
	    errstr = newstralloc2(errstr, "select: ", strerror(errno));
	    goto failed;
	}

	/* read/write any data */

	if(datafd >= 0 && FD_ISSET(datafd, &selectset)) {
	    size1 = read(datafd, db.dataptr, db.spaceleft);
	    switch(size1) {
	    case -1:
		errstr = newstralloc2(errstr, "data read: ", strerror(errno));
		goto failed;
	    case 0:
		if(update_dataptr(&db, size1)) goto failed;
		eof1 = 1;
		FD_CLR(datafd, &readset);
		aclose(datafd);
		break;
	    default:
		if(update_dataptr(&db, size1)) goto failed;
	    }
	}

	if(mesgfd >= 0 && FD_ISSET(mesgfd, &selectset)) {
	    size2 = read(mesgfd, mesgbuf, sizeof(mesgbuf)-1);
	    switch(size2) {
	    case -1:
		errstr = newstralloc2(errstr, "mesg read: ", strerror(errno));
		goto failed;
	    case 0:
		eof2 = 1;
		process_dumpeof();
		FD_CLR(mesgfd, &readset);
		aclose(mesgfd);
		break;
	    default:
		mesgbuf[size2] = '\0';
		add_msg_data(mesgbuf, size2);
	    }

	    if (got_info_endline && !header_done) { /* time to do the header */
		make_tapeheader(&file, F_DUMPFILE);
		if (write_tapeheader(db.fd, &file)) {
		    errstr = newstralloc2(errstr, "write_tapeheader: ", 
					  strerror(errno));
		    goto failed;
		}
		header_done = 1;

		if (datafd != -1)
		    FD_SET(datafd, &readset);	/* now we can read the data */
	    }
	}
    } /* end while */

    if(dump_result > 1) goto failed;

    runtime = stopclock();
    dumptime = runtime.r.tv_sec + runtime.r.tv_usec/1000000.0;

    dumpsize -= (nb_header_block * TAPE_BLOCK_SIZE);/* don't count the header */
    if (dumpsize < 0) dumpsize = 0;	/* XXX - maybe this should be fatal? */

    ap_snprintf(kb_str, sizeof(kb_str), "%ld", dumpsize);
    ap_snprintf(kps_str, sizeof(kps_str),
		"%3.1f",
		dumptime ? dumpsize / dumptime : 0.0);
    ap_snprintf(orig_kb_str, sizeof(orig_kb_str), "%ld", origsize);
    errstr = newvstralloc(errstr,
			  "sec ", walltime_str(runtime),
			  " ", "kb ", kb_str,
			  " ", "kps ", kps_str,
			  " ", "orig-kb ", orig_kb_str,
			  NULL);
    q = squotef("[%s]", errstr);
    putresult("DONE %s %ld %ld %ld %s\n", handle, origsize, dumpsize,
	      (long)(dumptime+0.5), q);
    amfree(q);

    afclose(errf);

    switch(dump_result) {
    case 0:
	log_add(L_SUCCESS, "%s %s %s %d [%s]", hostname, diskname, datestamp, level, errstr);

	break;

    case 1:
	log_start_multiline();
	log_add(L_STRANGE, "%s %s %d [%s]", hostname, diskname, level, errstr);
	log_msgout(L_STRANGE);
	log_end_multiline();

	break;
    }

    unlink(errfname);
    amfree(errfname);

    if (indexfile) {
	char *tmpname = NULL;
	int len;

	if((len = strlen(indexfile)) < 4) {
	    errstr = newstralloc2(errstr, "bad indexfile name: ", indexfile);
	    goto log_failed;
	}
	tmpname = stralloc(indexfile);
	indexfile[len-4] = '\0';
	unlink(indexfile);
	rename(tmpname, indexfile);
	amfree(tmpname);
	amfree(indexfile);
    }

    return 1;

 failed:

    if(!abort_pending) {
	q = squotef("[%s]", errstr);
	putresult("FAILED %s %s\n", handle, q);
	amfree(q);
    }

    if(errf) afclose(errf);

    /* kill all child process */
    if(compresspid != -1) {
	killerr = kill(compresspid,SIGTERM);
	if(killerr == 0) {
	    fprintf(stderr,"%s: kill compress command\n",get_pname());
	}
	else if ( killerr == -1 ) {
	    if(errno != ESRCH)
		fprintf(stderr,"%s: can't kill compress command: %s\n", 
			       get_pname(), strerror(errno));
	}
    }

    if(indexpid != -1) {
	killerr = kill(indexpid,SIGTERM);
	if(killerr == 0) {
	    fprintf(stderr,"%s: kill index command\n",get_pname());
	}
	else if ( killerr == -1 ) {
	    if(errno != ESRCH)
		fprintf(stderr,"%s: can't kill index command: %s\n", 
			       get_pname(),strerror(errno));
	}
    }

 log_failed:

    if(!abort_pending) {
	log_start_multiline();
	log_add(L_FAIL, "%s %s %d [%s]", hostname, diskname, level, errstr);
	if (errfname) {
	    log_msgout(L_FAIL);
	}
	log_end_multiline();
    }
    if (errfname) {
	unlink(errfname);
	amfree(errfname);
    }

    if (indexfile)
	unlink(indexfile);

    return 0;
}

/* -------------------- */

char *hostname, *disk;
int response_error;

void sendbackup_response(p, pkt)
proto_t *p;
pkt_t *pkt;
{
    char *optionstr = NULL;
    int data_port, mesg_port, index_port;
    char *s;
    int ch;
    char *nl;

    if(p->state == S_FAILED) {
	if(pkt == NULL) {
	    errstr = newstralloc(errstr, "[request timeout]");
	    response_error = 1;
	    return;
	}
	else {
/*	    fprintf(stderr, "got nak response:\n----\n%s----\n\n", pkt->body);*/
#define sc "ERROR"
	    if(strncmp(pkt->body, sc, sizeof(sc)-1) != 0) {
		goto request_NAK;
	    }
	    s = pkt->body+sizeof(sc)-1;
	    ch = *s++;
#undef sc
	    skip_whitespace(s, ch);
	    if(ch == '\0') {
		goto request_NAK;
	    }
	    errstr = newvstralloc(errstr, "nak error:", s - 1, NULL);
	    if(errstr[strlen(errstr)-1] == '\n' )
		errstr[strlen(errstr)-1] = '\0';
	    response_error = 1;
	    return;
	}
    }

/*     fprintf(stderr, "got response:\n----\n%s----\n\n", pkt->body); */

#ifdef KRB4_SECURITY
    if(krb4_auth && !check_mutual_authenticator(&cred.session, pkt, p)) {
	errstr = newstralloc(errstr, "[mutual-authentication failed]");
	response_error = 2;
	return;
    }
#endif

#define sc "ERROR"
    if(strncmp(pkt->body, sc, sizeof(sc)-1) == 0) {
	/* this is an error response packet */
	s = pkt->body+sizeof(sc)-1;
	ch = *s++;
#undef sc
	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto bogus_error_packet;
	}
	errstr = newstralloc(errstr, s - 1);
	response_error = 2;
	return;
    }

    if((nl = strchr(pkt->body, '\n')) == NULL) {
	goto parse_of_reply_message_failed;
    }
    *nl = '\0';

    s = pkt->body;
    ch = *s++;

    skip_whitespace(s, ch);
#define sc "CONNECT"
    if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	goto parse_of_reply_message_failed;
    }
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc

    skip_whitespace(s, ch);
#define sc "DATA"
    if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	goto parse_of_reply_message_failed;
    }
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc

    skip_whitespace(s, ch);
    if(ch == '\0' || sscanf(s - 1, "%d", &data_port) != 1) {
	goto parse_of_reply_message_failed;
    }
    skip_integer(s, ch);

    skip_whitespace(s, ch);
#define sc "MESG"
    if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	goto parse_of_reply_message_failed;
    }
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc

    skip_whitespace(s, ch);
    if(ch == '\0' || sscanf(s - 1, "%d", &mesg_port) != 1) {
	goto parse_of_reply_message_failed;
    }
    skip_integer(s, ch);

    skip_whitespace(s, ch);
#define sc "INDEX"
    if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	goto parse_of_reply_message_failed;
    }
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc

    skip_whitespace(s, ch);
    if(ch == '\0' || sscanf(s - 1, "%d", &index_port) != 1) {
	goto parse_of_reply_message_failed;
    }
    skip_integer(s, ch);

    skip_whitespace(s, ch);
    if(ch != '\0') {
	goto parse_of_reply_message_failed;
    }

    s = nl + 1;
    ch = *s++;
    *nl = '\n';
    nl = NULL;

    skip_whitespace(s, ch);
#define sc "OPTIONS"
    if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	goto parse_of_reply_message_failed;
    }
    s += sizeof(sc)-1;
    ch = s[-1];

    skip_whitespace(s, ch);
    if(ch == '\0') {
	goto parse_of_reply_message_failed;
    }
    if ((nl = strchr(s - 1, '\n')) != NULL) {
	*nl = '\0';
	if(nl == s - 1) {
	    goto parse_of_reply_message_failed;
	}
    }
    optionstr = stralloc(s - 1);
    if(nl) {
	*nl = '\n';
	nl = NULL;
    }

    datafd = stream_client(hostname, data_port,
			   DEFAULT_SIZE, DEFAULT_SIZE, NULL);
    if(datafd == -1) {
	errstr = newvstralloc(errstr,
			      "[could not connect to data port: ",
			      strerror(errno),
			      "]",
			      NULL);
	response_error = 1;
	amfree(optionstr);
	return;
    }
    mesgfd = stream_client(hostname, mesg_port,
			   DEFAULT_SIZE, DEFAULT_SIZE, NULL);
    if(mesgfd == -1) {
	errstr = newvstralloc(errstr,
			      "[could not connect to mesg port: ",
			      strerror(errno),
			      "]",
			      NULL);
	aclose(datafd);
	datafd = -1;				/* redundant */
	response_error = 1;
	amfree(optionstr);
	return;
    }

    if (index_port != -1) {
	indexfd = stream_client(hostname, index_port,
				DEFAULT_SIZE, DEFAULT_SIZE, NULL);
	if (indexfd == -1) {
	    errstr = newvstralloc(errstr,
				  "[could not connect to index port: ",
				  strerror(errno),
				  "]",
				  NULL);
	    aclose(datafd);
	    aclose(mesgfd);
	    datafd = mesgfd = -1;		/* redundant */
	    response_error = 1;
	    amfree(optionstr);
	    return;
	}
    }

    /* everything worked */

#ifdef KRB4_SECURITY
    if(krb4_auth && kerberos_handshake(datafd, cred.session) == 0) {
	errstr = newstralloc(errstr,
			     "[mutual authentication in data stream failed]");
	aclose(datafd);
	aclose(mesgfd);
	if (indexfd != -1)
	    aclose(indexfd);
	response_error = 1;
	amfree(optionstr);
	return;
    }
    if(krb4_auth && kerberos_handshake(mesgfd, cred.session) == 0) {
	errstr = newstralloc(errstr,
			     "[mutual authentication in mesg stream failed]");
	aclose(datafd);
	if (indexfd != -1)
	    aclose(indexfd);
	aclose(mesgfd);
	response_error = 1;
	amfree(optionstr);
	return;
    }
#endif
    response_error = 0;
    amfree(optionstr);
    return;

 request_NAK:

/*  fprintf(stderr, "dumper: got strange NAK: %s", pkt->body); */
    errstr = newstralloc(errstr, "[request NAK]");
    response_error = 2;
    amfree(optionstr);
    return;

 bogus_error_packet:

    errstr = newstralloc(errstr, "[bogus error packet]");
    response_error = 2;
    amfree(optionstr);
    return;

 parse_of_reply_message_failed:

    if(nl) *nl = '\n';
    errstr = newstralloc(errstr, "[parse of reply message failed]");
    response_error = 2;
    amfree(optionstr);
    return;
}

int startup_dump(hostname, disk, level, dumpdate, dumpname, options)
char *hostname, *disk, *dumpdate, *dumpname, *options;
int level;
{
    char level_string[NUM_STR_SIZE];
    char rc_str[NUM_STR_SIZE];
    char *req = NULL;
    int rc;

    ap_snprintf(level_string, sizeof(level_string), "%d", level);
    req = vstralloc("SERVICE sendbackup\n",
		    "OPTIONS ",
		    "hostname=", hostname, ";",
		    "\n",
		    progname, " ", disk, " ", level_string, " ", dumpdate, " ",
		    "OPTIONS ", options,
		    "\n",
		    NULL);

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
		ap_snprintf(rc_str, sizeof(rc_str), "%d", rc);
		errstr = newvstralloc(errstr,
				      "[host ", hostname,
				      ": krb4 error (krb_get_cred) ",
				      rc_str,
				      ": ", krb_err_txt[rc],
				      NULL);
		amfree(req);
		return 2;
	    }
	}
	if(rc > 0) {
	    ap_snprintf(rc_str, sizeof(rc_str), "%d", rc);
	    errstr = newvstralloc(errstr,
				  "[host ", hostname,
				  ": krb4 error (make_krb_req) ",
				  rc_str,
				  ": ", krb_err_txt[rc],
				  NULL);
	    amfree(req);
	    return 2;
	}
    } else
#endif
	rc = make_request(hostname, amanda_port, req, NULL,
			  STARTUP_TIMEOUT, sendbackup_response);

    req = NULL;					/* do not own this any more */

    if(rc) {
	ap_snprintf(rc_str, sizeof(rc_str), "%d", rc);
	errstr = newvstralloc(errstr,
			      "[could not resolve name \"", hostname, "\"",
			      ": error ", rc_str, "]",
			      NULL);
	return 2;
    }
    run_protocol();
    return response_error;
}
