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
/* $Id: dumper.c,v 1.137 2001/03/05 23:52:39 martinea Exp $
 *
 * requests remote amandad processes to dump filesystems
 */
#include "amanda.h"
#include "amindex.h"
#include "arglist.h"
#include "clock.h"
#include "conffile.h"
#include "event.h"
#include "logfile.h"
#include "packet.h"
#include "protocol.h"
#include "security.h"
#include "stream.h"
#include "token.h"
#include "version.h"
#include "fileheader.h"
#include "server_util.h"
#include "util.h"

#ifndef SEEK_SET
#define SEEK_SET 0
#endif

#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif

#define CONNECT_TIMEOUT	5*60

#define STARTUP_TIMEOUT 60

struct databuf {
    int fd;			/* file to flush to */
    const char *filename;	/* name of what fd points to */
    char buf[TAPE_BLOCK_BYTES];
    char *dataptr;		/* data buffer markers */
    int spaceleft;
    pid_t compresspid;		/* valid if fd is pipe to compress */
};

int interactive;
char *handle = NULL;

char *errstr = NULL;
int abort_pending;
static long dumpsize, headersize, origsize;

static comp_t srvcompress = COMP_NONE;

static FILE *errf = NULL;
char *hostname = NULL;
char *diskname = NULL;
char *options = NULL;
char *progname = NULL;
int level;
char *dumpdate = NULL;
char *datestamp;
char *config_name = NULL;
char *config_dir = NULL;
int conf_dtimeout;

static dumpfile_t file;

static struct {
    const char *name;
    security_stream_t *fd;
} streams[] = {
#define	DATAFD	0
    { "DATA", NULL },
#define	MESGFD	1
    { "MESG", NULL },
#define	INDEXFD	2
    { "INDEX", NULL },
};
#define	NSTREAMS	(sizeof(streams) / sizeof(streams[0]))

/* local functions */
int main P((int, char **));
static int do_dump P((struct databuf *));
void check_options P((char *));
static void finish_tapeheader P((dumpfile_t *));
static int write_tapeheader P((int, dumpfile_t *));
static void databuf_init P((struct databuf *, int, const char *));
static int databuf_write P((struct databuf *, const void *, int));
static int databuf_flush P((struct databuf *));
static void process_dumpeof P((void));
static void process_dumpline P((const char *));
static void add_msg_data P((const char *, size_t));
static void parse_info_line P((char *));
static void log_msgout P((logtype_t));

static int runcompress P((int, pid_t *, comp_t));

static void sendbackup_response P((void *, pkt_t *, security_handle_t *));
static int startup_dump P((const char *, const char *, int, const char *,
		    const char *, const char *));
static void stop_dump P((void));

static void read_indexfd P((void *, void *, ssize_t));
static void read_datafd P((void *, void *, ssize_t));
static void read_mesgfd P((void *, void *, ssize_t));
static void timeout P((int));
static void timeout_callback P((void *));

void check_options(options)
char *options;
{
    if (strstr(options, "srvcomp-best;") != NULL)
      srvcompress = COMP_BEST;
    else if (strstr(options, "srvcomp-fast;") != NULL)
      srvcompress = COMP_FAST;
    else
      srvcompress = COMP_NONE;
}


int
main(main_argc, main_argv)
    int main_argc;
    char **main_argv;
{
    static struct databuf db;
    struct cmdargs cmdargs;
    cmd_t cmd;
    int outfd, taper_port, rc;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    char *conffile;
    char *q = NULL;

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

    if (main_argc > 1) {
	config_name = stralloc(main_argv[1]);
	config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
    } else {
	char my_cwd[STR_SIZE];

	if (getcwd(my_cwd, sizeof(my_cwd)) == NULL) {
	    error("cannot determine current working directory");
	}
	config_dir = stralloc2(my_cwd, "/");
	if ((config_name = strrchr(my_cwd, '/')) != NULL) {
	    config_name = stralloc(config_name + 1);
	}
    }

    safe_cd();

    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if(read_conffile(conffile)) {
	error("could not find config file \"%s\"", conffile);
    }
    amfree(conffile);

    /*
     * Make our effective uid nonprivlidged, but keep our real uid as root
     * in case we need to get back (to bind privlidged ports, etc).
     */
    if(geteuid() == 0) {
	uid_t ruid = getuid();
	setuid(0);
	seteuid(ruid);
	setgid(getgid());
    }
#ifdef BSD_SECURITY
    else error("must be run setuid root to communicate correctly");
#endif

    fprintf(stderr,
	    "%s: pid %ld executable %s version %s\n",
	    get_pname(), (long) getpid(),
	    main_argv[0], version());
    fflush(stderr);

    /* now, make sure we are a valid user */

    if (getpwuid(getuid()) == NULL)
	error("can't get login name for my uid %ld", (long)getuid());

    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    interactive = isatty(0);

    datestamp = construct_datestamp();
    conf_dtimeout = getconf_int(CNF_DTIMEOUT);

    protocol_init();

    do {
	cmd = getcmd(&cmdargs);

	switch(cmd) {
	case QUIT:
	    break;

	case PORT_DUMP:
	    /*
	     * PORT-DUMP handle port host disk level dumpdate progname options
	     */
	    if (cmdargs.argc != 9)
		error("error [dumper PORT-DUMP argc != 9: %d]", cmdargs.argc);
	    handle = newstralloc(handle, cmdargs.argv[2]);
	    taper_port = atoi(cmdargs.argv[3]);
	    hostname = newstralloc(hostname, cmdargs.argv[4]);
	    diskname = newstralloc(diskname, cmdargs.argv[5]);
	    level = atoi(cmdargs.argv[6]);
	    dumpdate = newstralloc(dumpdate, cmdargs.argv[7]);
	    progname = newstralloc(progname, cmdargs.argv[8]);
	    options = newstralloc(options, cmdargs.argv[9]);

	    /* connect outf to taper port */

	    outfd = stream_client("localhost", taper_port,
				  STREAM_BUFSIZE, DEFAULT_SIZE, NULL, 0);
	    if (outfd == -1) {
		q = squotef("[taper port open: %s]", strerror(errno));
		putresult(FAILED, "%s %s\n", handle, q);
		amfree(q);
		break;
	    }
	    databuf_init(&db, outfd, "<output program>");

	    check_options(options);

	    rc = startup_dump(hostname, diskname, level, dumpdate, progname,
		options);
	    if (rc != 0) {
		q = squote(errstr);
		putresult(rc == 2? FAILED : TRY_AGAIN, "%s %s\n",
		    handle, q);
		if (rc == 2)
		    log_add(L_FAIL, "%s %s %d [%s]", hostname, diskname, level,
			errstr);
		amfree(q);
	    } else {
		abort_pending = 0;
		if (do_dump(&db)) {
		}
		if (abort_pending)
		    putresult(ABORT_FINISHED, "%s\n", handle);
	    }
	    break;

	default:
	    q = squote(cmdargs.argv[1]);
	    putresult(BAD_COMMAND, "%s\n", q);
	    amfree(q);
	    break;
	}

	if (outfd != -1)
	    aclose(outfd);

	while (wait(NULL) != -1)
	    continue;
    } while(cmd != QUIT);

    amfree(errstr);
    amfree(datestamp);
    amfree(handle);
    amfree(hostname);
    amfree(diskname);
    amfree(dumpdate);
    amfree(progname);
    amfree(options);
    amfree(config_dir);
    amfree(config_name);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if (malloc_size_1 != malloc_size_2)
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);

    exit(0);
}


/*
 * Initialize a databuf.  Takes a writeable file descriptor.
 */
static void
databuf_init(db, fd, filename)
    struct databuf *db;
    int fd;
    const char *filename;
{

    db->fd = fd;
    db->filename = filename;
    db->dataptr = db->buf;
    db->spaceleft = sizeof(db->buf);
    db->compresspid = -1;
}


/*
 * Updates the buffer pointer for the input data buffer.  The buffer is
 * written if it is full, or the remainder is zeroed if at eof.
 * Returns negative on error, else 0.
 */
static int
databuf_write(db, buf, size)
    struct databuf *db;
    const void *buf;
    int size;
{
    int nwritten;

    while (size > 0) {
	nwritten = min(size, db->spaceleft);
	memcpy(db->dataptr, buf, nwritten);
	size -= nwritten;
	db->spaceleft -= nwritten;
	db->dataptr += nwritten;
	(char *)buf += nwritten;

	/* If the buffer is full, write it */
	if (db->spaceleft == 0 && databuf_flush(db) < 0)
	    return (-1);
    }

    return (0);
}

/*
 * Write out the buffer to the backing file
 */
static int
databuf_flush(db)
    struct databuf *db;
{
    struct cmdargs cmdargs;
    int written, off;
    cmd_t cmd;

    /*
     * If there's no data, do nothing.
     */
    if (db->dataptr == db->buf)
	return (0);

    /*
     * If buffer isn't full, zero out the rest.
     */
    if (db->spaceleft > 0) {
	memset(db->dataptr, 0, db->spaceleft);
	db->spaceleft = 0;
    }

    /*
     * Write out the buffer
     */
    off = 0;
    do {
	written = write(db->fd, db->buf + off,
	    sizeof(db->buf) - db->spaceleft);
	if (written > 0) {
	    db->spaceleft += written;
	    off += written;
	    continue;
	} else if (written < 0 && errno != ENOSPC) {
	    errstr = squotef("data write: %s", strerror(errno));
	    return (-1);
	}

	putresult(NO_ROOM, "%s\n", handle);
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
    return (0);
}

static int dump_result;
static int status;
#define	GOT_INFO_ENDLINE	(1 << 0)
#define	GOT_SIZELINE		(1 << 1)
#define	GOT_ENDLINE		(1 << 2)
#define	HEADER_DONE		(1 << 3)


static void
process_dumpeof()
{
    /* process any partial line in msgbuf? !!! */
    add_msg_data(NULL, 0);
    if(!ISSET(status, GOT_SIZELINE) && dump_result < 2) {
	/* make a note if there isn't already a failure */
	fprintf(errf,
		"? %s: strange [missing size line from sendbackup]\n",
		get_pname());
	dump_result = max(dump_result, 1);
    }

    if(!ISSET(status, GOT_ENDLINE) && dump_result < 2) {
	fprintf(errf,
		"? %s: strange [missing end line from sendbackup]\n",
		get_pname());
	dump_result = max(dump_result, 1);
    }
}

/*
 * Parse an information line from the client.
 * We ignore unknown parameters and only remember the last
 * of any duplicates.
 */
static void
parse_info_line(str)
    char *str;
{
    static const struct {
	const char *name;
	char *value;
	size_t len;
    } fields[] = {
	{ "BACKUP", file.program, sizeof(file.program) },
	{ "RECOVER_CMD", file.recover_cmd, sizeof(file.recover_cmd) },
	{ "COMPRESS_SUFFIX", file.comp_suffix, sizeof(file.comp_suffix) },
    };
    char *name, *value;
    int i;

    if (strcmp(str, "end") == 0) {
	SET(status, GOT_INFO_ENDLINE);
	return;
    }

    name = strtok(str, "=");
    if (name == NULL)
	return;
    value = strtok(NULL, "");
    if (value == NULL)
	return;

    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
	if (strcmp(name, fields[i].name) == 0) {
	    strncpy(fields[i].value, value, fields[i].len - 1);
	    fields[i].value[fields[i].len - 1] = '\0';
	    break;
	}
    }
}

static void
process_dumpline(str)
    const char *str;
{
    char *buf, *tok;

    buf = stralloc(str);

    switch (*buf) {
    case '|':
	/* normal backup output line */
	break;
    case '?':
	/* sendbackup detected something strange */
	dump_result = max(dump_result, 1);
	break;
    case 's':
	/* a sendbackup line, just check them all since there are only 5 */
	tok = strtok(buf, " ");
	if (tok == NULL || strcmp(tok, "sendbackup:") != 0)
	    goto bad_line;

	tok = strtok(NULL, " ");
	if (tok == NULL)
	    goto bad_line;

	if (strcmp(tok, "start") == 0)
	    break;

	if (strcmp(tok, "size") == 0) {
	    tok = strtok(NULL, "");
	    if (tok != NULL) {
		origsize = (long)atof(tok);
		SET(status, GOT_SIZELINE);
	    }
	    break;
	}

	if (strcmp(tok, "end") == 0) {
	    SET(status, GOT_ENDLINE);
	    break;
	}

	if (strcmp(tok, "error") == 0) {
	    SET(status, GOT_ENDLINE);
	    dump_result = max(dump_result, 2);

	    tok = strtok(NULL, "");
	    if (tok == NULL || *tok != '[') {
		errstr = newvstralloc(errstr, "bad remote error: ", str, NULL);
	    } else {
		char *enderr;

		tok++;	/* skip over '[' */
		if ((enderr = strchr(tok, ']')) != NULL)
		    *enderr = '\0';
		errstr = newstralloc(errstr, tok);
	    }
	    break;
	}

	if (strcmp(tok, "info") == 0) {
	    tok = strtok(NULL, "");
	    if (tok != NULL)
		parse_info_line(tok);
	    break;
	}
	/* else we fall through to bad line */
    default:
bad_line:
	/* prefix with ?? */
	fprintf(errf, "??");
	dump_result = max(dump_result, 1);
	break;
    }
    fprintf(errf, "%s\n", str);
    amfree(buf);
}

static void
add_msg_data(str, len)
    const char *str;
    size_t len;
{
    static struct {
	char *buf;	/* buffer holding msg data */
	size_t size;	/* size of alloced buffer */
    } msg = { NULL, 0 };
    char *line, *nl;
    size_t buflen;

    if (msg.buf != NULL)
	buflen = strlen(msg.buf);
    else
	buflen = 0;

    /*
     * If our argument is NULL, then we need to flush out any remaining
     * bits and return.
     */
    if (str == NULL) {
	if (buflen == 0)
	    return;
	fprintf(errf,"? %s: error [partial line in msgbuf: %ld bytes]\n",
	    get_pname(), (long)buflen);
	fprintf(errf,"? %s: error [partial line in msgbuf: \"%s\"]\n",
	    get_pname(), msg.buf);
	msg.buf[0] = '\0';
	return;
    }

    /*
     * Expand the buffer if it can't hold the new contents.
     */
    if (buflen + len + 1 > msg.size) {
	char *newbuf;
	size_t newsize;

/* round up to next y, where y is a power of 2 */
#define	ROUND(x, y)	(((x) + (y) - 1) & ~((y) - 1))

	newsize = ROUND(buflen + len + 1, 256);
	newbuf = alloc(newsize);

	if (msg.buf != NULL) {
	    strcpy(newbuf, msg.buf);
	    amfree(msg.buf);
	} else
	    newbuf[0] = '\0';
	msg.buf = newbuf;
	msg.size = newsize;
    }

    /*
     * If there was a partial line from the last call, then
     * append the new data to the end.
     */
    strncat(msg.buf, str, len);

    /*
     * Process all lines in the buffer
     */
    for (line = msg.buf;;) {
	/*
	 * If there's no newline, then we've only got a partial line.
	 * We go back for more.
	 */
	if ((nl = strchr(line, '\n')) == NULL)
	    break;
	*nl = '\0';
	process_dumpline(line);
	line = nl + 1;
    }

    /*
     * If we did not process all of the data, move it to the front
     * of the buffer so it is there next time.
     */
    if (*line != '\0') {
	buflen = strlen(line);
	memmove(msg.buf, line, buflen + 1);
    } else {
	msg.buf[0] = '\0';
    }
}


static void
log_msgout(typ)
    logtype_t typ;
{
    char *line;

    fflush(errf);
    (void) fseek(errf, 0L, SEEK_SET);
    while ((line = agets(errf)) != NULL) {
	log_add(typ, "%s", line);
	amfree(line);
    }

    afclose(errf);
}

/* ------------- */

/*
 * Fill in the rest of the tape header
 */
static void
finish_tapeheader(file)
    dumpfile_t *file;
{

    assert(ISSET(status, HEADER_DONE));

    file->type = F_DUMPFILE;
    strncpy(file->datestamp, datestamp, sizeof(file->datestamp) - 1);
    strncpy(file->name, hostname, sizeof(file->name) - 1);
    strncpy(file->disk, diskname, sizeof(file->disk) - 1);
    file->dumplevel = level;

    /*
     * If we're doing the compression here, we need to override what
     * sendbackup told us the compression was.
     */
    if (srvcompress != COMP_NONE) {
	file->compressed = 1;
#ifndef UNCOMPRESS_OPT
#define	UNCOMPRESS_OPT	""
#endif
	snprintf(file->uncompress_cmd, sizeof(file->uncompress_cmd),
	    " %s %s |", UNCOMPRESS_PATH, UNCOMPRESS_OPT);
	strncpy(file->comp_suffix, COMPRESS_SUFFIX,sizeof(file->comp_suffix)-1);
	file->comp_suffix[sizeof(file->comp_suffix)-1] = '\0';
    } else {
	if (file->comp_suffix[0] == '\0') {
	    file->compressed = 0;
	    assert(sizeof(file->comp_suffix) >= 2);
	    strcpy(file->comp_suffix, "N");
	} else {
	    file->compressed = 1;
	}
    }
}

/*
 * Send an Amanda dump header to the output file.
 */
static int
write_tapeheader(outfd, file)
    int outfd;
    dumpfile_t *file;
{
    char buffer[TAPE_BLOCK_BYTES];
    int written;

    write_header(buffer, file, sizeof(buffer));

    written = write(outfd, buffer, sizeof(buffer));
    if(written == sizeof(buffer)) return 0;
    if(written < 0) return written;
    errno = ENOSPC;
    return -1;
}

static int
do_dump(db)
    struct databuf *db;
{
    char *indexfile_tmp = NULL;
    char *indexfile_real = NULL;
    char level_str[NUM_STR_SIZE];
    char *fn;
    char *q;
    times_t runtime;
    double dumptime;	/* Time dump took in secs */
    char *errfname = NULL;
    int indexout;
    pid_t indexpid;

    startclock();

    dumpsize = headersize = origsize = dump_result = 0;
    status = 0;
    fh_init(&file);

    snprintf(level_str, sizeof(level_str), "%d", level);
    fn = sanitise_filename(diskname);
    errfname = newvstralloc(errfname,
			    AMANDA_TMPDIR,
			    "/", hostname,
			    ".", fn,
			    ".", level_str,
			    ".errout",
			    NULL);
    amfree(fn);
    if((errf = fopen(errfname, "w+")) == NULL) {
	errstr = newvstralloc(errstr,
			      "errfile open \"", errfname, "\": ",
			      strerror(errno),
			      NULL);
	amfree(errfname);
	goto failed;
    } else {
	unlink(errfname);			/* so it goes away on close */
    }
    amfree(errfname);

    indexpid = -1;
    if (streams[INDEXFD].fd != NULL) {
	indexfile_real = getindexfname(hostname, diskname, datestamp, level);
	indexfile_tmp = stralloc2(indexfile_real, ".tmp");

	if (mkpdir(indexfile_tmp, 02755, (uid_t)-1, (gid_t)-1) == -1) {
	   errstr = newvstralloc(errstr,
				 "err create ",
				 indexfile_tmp,
				 ": ",
				 strerror(errno),
				 NULL);
	   amfree(indexfile_real);
	   amfree(indexfile_tmp);
	   goto failed;
	}
	indexout = open(indexfile_tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (indexout == -1) {
	    errstr = newvstralloc(errstr, "err open ", indexfile_tmp, ": ",
		strerror(errno), NULL);
	    goto failed;
	} else {
	    if (runcompress(indexout, &indexpid, COMP_BEST) < 0) {
		aclose(indexout);
		goto failed;
	    }
	}
	/*
	 * Schedule the indexfd for relaying to the index file
	 */
	security_stream_read(streams[INDEXFD].fd, read_indexfd, &indexout);
    }

    /*
     * We only need to process messages initially.  Once we have done
     * the header, we will start processing data too.
     */
    security_stream_read(streams[MESGFD].fd, read_mesgfd, db);

    /*
     * Setup a read timeout
     */
    timeout(conf_dtimeout);

    /*
     * Start the event loop.  This will exit when all three events
     * (read the mesgfd, read the datafd, and timeout) are removed.
     */
    event_loop(0);

    if (dump_result > 1)
	goto failed;

    runtime = stopclock();
    dumptime = runtime.r.tv_sec + runtime.r.tv_usec/1000000.0;

    dumpsize -= headersize;		/* don't count the header */
    if (dumpsize < 0) dumpsize = 0;	/* XXX - maybe this should be fatal? */

    errstr = alloc(128);
    snprintf(errstr, 128, "sec %s kb %ld kps %3.1f orig-kb %ld",
	walltime_str(runtime), dumpsize,
	dumptime ? dumpsize / dumptime : 0.0, origsize);
    q = squotef("[%s]", errstr);
    putresult(DONE, "%s %ld %ld %ld %s\n", handle, origsize, dumpsize,
	      (long)(dumptime+0.5), q);
    amfree(q);

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

    if (errf) afclose(errf);

    if (indexfile_tmp) {
	amwait_t index_status;

	aclose(indexout);
	waitpid(indexpid,&index_status,0);
	if (rename(indexfile_tmp, indexfile_real) != 0) {
	    log_add(L_WARNING, "could not rename \"%s\" to \"%s\": %s",
		    indexfile_tmp, indexfile_real, strerror(errno));
	}
	amfree(indexfile_tmp);
	amfree(indexfile_real);
    }

    return 1;

failed:
    if (!abort_pending) {
	q = squotef("[%s]", errstr);
	putresult(FAILED, "%s %s\n", handle, q);
	amfree(q);
    }

    /* kill all child process */
    if (db->compresspid != -1) {
	fprintf(stderr,"%s: kill compress command\n",get_pname());
	if (kill(db->compresspid, SIGTERM) < 0) {
	    if (errno != ESRCH)
		fprintf(stderr,"%s: can't kill compress command: %s\n", 
		    get_pname(), strerror(errno));
	}
    }

    if (indexpid != -1) {
	fprintf(stderr,"%s: kill index command\n",get_pname());
	if (kill(indexpid, SIGTERM) < 0) {
	    if (errno != ESRCH)
		fprintf(stderr,"%s: can't kill index command: %s\n", 
		    get_pname(),strerror(errno));
	}
    }

    if(!abort_pending) {
	log_start_multiline();
	log_add(L_FAIL, "%s %s %d [%s]", hostname, diskname, level, errstr);
	if (errf) {
	    log_msgout(L_FAIL);
	}
	log_end_multiline();
    }

    if (errf) afclose(errf);

    if (indexfile_tmp) {
	unlink(indexfile_tmp);
	amfree(indexfile_tmp);
	amfree(indexfile_real);
    }

    return 0;
}

/*
 * Callback for reads on the mesgfd stream
 */
static void
read_mesgfd(cookie, buf, size)
    void *cookie, *buf;
    ssize_t size;
{
    struct databuf *db = cookie;

    assert(db != NULL);

    switch (size) {
    case -1:
	errstr = newstralloc2(errstr, "mesg read: ",
	    security_stream_geterror(streams[MESGFD].fd));
	dump_result = 2;
	stop_dump();
	return;
    case 0:
	/*
	 * EOF.  Just shut down the mesg stream.
	 */
	process_dumpeof();
	security_stream_close(streams[MESGFD].fd);
	streams[MESGFD].fd = NULL;
	/*
	 * If the data fd and index fd has also shut down, then we're done.
	 */
	if (streams[DATAFD].fd == NULL && streams[INDEXFD].fd == NULL)
	    stop_dump();
	return;
    default:
	assert(buf != NULL);
	add_msg_data(buf, size);
	security_stream_read(streams[MESGFD].fd, read_mesgfd, cookie);
	break;
    }

    /*
     * Reset the timeout for future reads
     */
    timeout(conf_dtimeout);

    if (ISSET(status, GOT_INFO_ENDLINE) && !ISSET(status, HEADER_DONE)) {
	SET(status, HEADER_DONE);
	/* time to do the header */
	finish_tapeheader(&file);
	if (write_tapeheader(db->fd, &file)) {
	    errstr = newstralloc2(errstr, "write_tapeheader: ", 
				  strerror(errno));
	    dump_result = 2;
	    stop_dump();
	    return;
	}
	dumpsize += TAPE_BLOCK_SIZE;
	headersize += TAPE_BLOCK_SIZE;

	/*
	 * Now, setup the compress for the data output, and start
	 * reading the datafd.
	 */
	if (srvcompress != COMP_NONE) {
	    if (runcompress(db->fd, &db->compresspid, srvcompress) < 0) {
		dump_result = 2;
		stop_dump();
		return;
	    }
	}
	security_stream_read(streams[DATAFD].fd, read_datafd, db);
    }
}

/*
 * Callback for reads on the datafd stream
 */
static void
read_datafd(cookie, buf, size)
    void *cookie, *buf;
    ssize_t size;
{
    struct databuf *db = cookie;

    assert(db != NULL);

    /*
     * The read failed.  Error out
     */
    if (size < 0) {
	errstr = newstralloc2(errstr, "data read: ",
	    security_stream_geterror(streams[DATAFD].fd));
	dump_result = 2;
	stop_dump();
	return;
    }

    /*
     * Reset the timeout for future reads
     */
    timeout(conf_dtimeout);

    /* The header had better be written at this point */
    assert(ISSET(status, HEADER_DONE));

    /*
     * EOF.  Stop and return.
     */
    if (size == 0) {
	databuf_flush(db);
	security_stream_close(streams[DATAFD].fd);
	streams[DATAFD].fd = NULL;
	/*
	 * If the mesg fd and index fd has also shut down, then we're done.
	 */
	if (streams[MESGFD].fd == NULL && streams[INDEXFD].fd == NULL)
	    stop_dump();
	return;
    }

    /*
     * We read something.  Add it to the databuf and reschedule for
     * more data.
     */
    assert(buf != NULL);
    if (databuf_write(db, buf, size) < 0) {
	dump_result = 2;
	stop_dump();
    }
    security_stream_read(streams[DATAFD].fd, read_datafd, cookie);
}

/*
 * Callback for reads on the index stream
 */
static void
read_indexfd(cookie, buf, size)
    void *cookie, *buf;
    ssize_t size;
{
    int fd;

    assert(cookie != NULL);
    fd = *(int *)cookie;

    if (size < 0) {
	errstr = newstralloc2(errstr, "index read: ",
	    security_stream_geterror(streams[INDEXFD].fd));
	dump_result = 2;
	stop_dump();
	return;
    }

    /*
     * EOF.  Stop and return.
     */
    if (size == 0) {
	security_stream_close(streams[INDEXFD].fd);
	streams[INDEXFD].fd = NULL;
	/*
	 * If the mesg fd has also shut down, then we're done.
	 */
	if (streams[DATAFD].fd == NULL && streams[MESGFD].fd == NULL)
	    stop_dump();
	return;
    }

    assert(buf != NULL);

    /*
     * If we get an error while writing to the index file, just
     * return without scheduling another read.
     */
    if (fullwrite(fd, buf, size) < 0)
	return;
    security_stream_read(streams[INDEXFD].fd, read_indexfd, cookie);
}

/*
 * Startup a timeout in the event handler.  If the arg is 0,
 * then remove the timeout.
 */
static void
timeout(seconds)
    int seconds;
{
    static event_handle_t *ev_timeout = NULL;

    /*
     * First, remove a timeout if one is active.
     */
    if (ev_timeout != NULL) {
	event_release(ev_timeout);
	ev_timeout = NULL;
    }

    /*
     * Now, schedule a new one if 'seconds' is greater than 0
     */
    if (seconds > 0)
	ev_timeout = event_register(seconds, EV_TIME, timeout_callback, NULL);
}

/*
 * This is the callback for timeout().  If this is reached, then we
 * have a data timeout.
 */
static void
timeout_callback(unused)
    void *unused;
{
    assert(unused == NULL);
    errstr = newstralloc(errstr, "data timeout");
    dump_result = 2;
    stop_dump();
}

/*
 * This is called when everything needs to shut down so event_loop()
 * will exit.
 */
static void
stop_dump()
{
    int i;

    for (i = 0; i < NSTREAMS; i++) {
	if (streams[i].fd != NULL) {
	    security_stream_close(streams[i].fd);
	    streams[i].fd = NULL;
	}
    }
    timeout(0);
}


/*
 * Runs compress with the first arg as its stdout.  Returns
 * 0 on success or negative if error, and it's pid via the second
 * argument.  The outfd arg is dup2'd to the pipe to the compress
 * process.
 */
static int
runcompress(outfd, pid, comptype)
    int outfd;
    pid_t *pid;
    comp_t comptype;
{
    int outpipe[2], tmpfd, rval;

    assert(outfd >= 0);
    assert(pid != NULL);

    /* outpipe[0] is pipe's stdin, outpipe[1] is stdout. */
    if (pipe(outpipe) < 0) {
	errstr = newstralloc2(errstr, "pipe: ", strerror(errno));
	return (-1);
    }

    switch (*pid = fork()) {
    case -1:
	errstr = newstralloc2(errstr, "couldn't fork: ", strerror(errno));
	aclose(outpipe[0]);
	aclose(outpipe[1]);
	return (-1);
    default:
	rval = dup2(outpipe[1], outfd);
	if (rval < 0)
	    errstr = newstralloc2(errstr, "couldn't dup2: ", strerror(errno));
	aclose(outpipe[1]);
	aclose(outpipe[0]);
	return (rval);
    case 0:
	if (dup2(outpipe[0], 0) < 0)
	    error("err dup2 in: %s", strerror(errno));
	if (dup2(outfd, 1) == -1)
	    error("err dup2 out: %s", strerror(errno));
	for (tmpfd = 3; tmpfd <= FD_SETSIZE; ++tmpfd)
	    close(tmpfd);
	execlp(COMPRESS_PATH, COMPRESS_PATH, (comptype == COMP_BEST ?
	    COMPRESS_BEST_OPT : COMPRESS_FAST_OPT), NULL);
	error("error: couldn't exec %s: %s", COMPRESS_PATH, strerror(errno));
    }
    /* NOTREACHED */
}

/* -------------------- */

static void
sendbackup_response(datap, pkt, sech)
    void *datap;
    pkt_t *pkt;
    security_handle_t *sech;
{
    int ports[NSTREAMS], *response_error = datap, i;
    char *tok;

    assert(response_error != NULL);
    assert(sech != NULL);

    if (pkt == NULL) {
	errstr = newvstralloc(errstr, "[request failed: ",
	    security_geterror(sech), "]", NULL);
	*response_error = 1;
	return;
    }

    if (pkt->type == P_NAK) {
/*    fprintf(stderr, "got nak response:\n----\n%s----\n\n", pkt->body);*/

	tok = strtok(pkt->body, " ");
	if (tok == NULL || strcmp(tok, "ERROR") != 0)
	    goto bad_nak;

	tok = strtok(NULL, "\n");
	if (tok != NULL) {
	    errstr = newvstralloc(errstr, "NAK: ", tok, NULL);
	    *response_error = 1;
	} else {
bad_nak:
	    errstr = newstralloc(errstr, "request NAK");
	    *response_error = 2;
	}
	return;
    }

    if (pkt->type != P_REP) {
	errstr = newvstralloc(errstr, "received strange packet type ",
	    pkt_type2str(pkt->type), ": ", pkt->body, NULL);
	*response_error = 1;
	return;
    }

/*     fprintf(stderr, "got response:\n----\n%s----\n\n", pkt->body); */

    /*
     * Get the first word out of the packet
     */
    tok = strtok(pkt->body, " ");
    if (tok == NULL)
	goto parse_error;

    /*
     * Error response packets have "ERROR" followed by the error message
     * followed by a newline.
     */
    if (strcmp(tok, "ERROR") == 0) {
	tok = strtok(NULL, "\n");
	if (tok == NULL)
	    tok = "[bogus error packet]";
	errstr = newstralloc(errstr, tok);
	*response_error = 2;
	return;
    }

    /*
     * Regular packets have CONNECT followed by three streams
     */
    if (strcmp(tok, "CONNECT") != 0)
	goto parse_error;

    /*
     * Parse the three stream specifiers out of the packet.
     */
    for (i = 0; i < NSTREAMS; i++) {
	tok = strtok(NULL, " ");
	if (tok == NULL || strcmp(tok, streams[i].name) != 0)
	    goto parse_error;
	tok = strtok(NULL, " \n");
	if (tok == NULL || sscanf(tok, "%d", &ports[i]) != 1)
	    goto parse_error;
    }

    /*
     * OPTIONS [options string] '\n'
     */
    tok = strtok(NULL, " ");
    if (tok == NULL || strcmp(tok, "OPTIONS") != 0)
	goto parse_error;

    tok = strtok(NULL, "\n");
    if (tok == NULL)
	goto parse_error;
    /* we do nothing with the options right now */

    /*
     * Connect the streams to their remote ports
     */
    for (i = 0; i < NSTREAMS; i++) {
	if (ports[i] == -1)
	    continue;
	streams[i].fd = security_stream_client(sech, ports[i]);
	if (streams[i].fd == NULL) {
	    errstr = newvstralloc(errstr,
		"[could not connect ", streams[i].name, " stream: ",
		security_geterror(sech), "]", NULL);
	    goto connect_error;
	}
    }

    /*
     * Authenticate the streams
     */
    for (i = 0; i < NSTREAMS; i++) {
	if (streams[i].fd == NULL)
	    continue;
#ifdef KRB4_SECURITY
	/*
	 * XXX krb4 historically never authenticated the index stream!
	 * We need to reproduce this lossage here to preserve compatibility
	 * with old clients.
	 * It is wrong to delve into sech, but we have no choice here.
	 */
	if (strcasecmp(sech->driver->name, "krb4") != 0 && i == INDEXFD)
	    continue;
#endif
	if (security_stream_auth(streams[i].fd) < 0) {
	    errstr = newvstralloc(errstr,
		"[could not authenticate ", streams[i].name, " stream: ",
		security_stream_geterror(streams[i].fd), "]", NULL);
	    goto connect_error;
	}
    }

    /*
     * The MESGFD and DATAFD streams are mandatory.  If we didn't get
     * them, complain.
     */
    if (streams[MESGFD].fd == NULL || streams[DATAFD].fd == NULL) {
	errstr = newstralloc(errstr, "[couldn't open MESG or INDEX streams]");
	goto connect_error;
    }

    /* everything worked */
    *response_error = 0;
    return;

parse_error:
    errstr = newstralloc(errstr, "[parse of reply message failed]");
    *response_error = 2;
    return;

connect_error:
    stop_dump();
    *response_error = 1;
}

static int
startup_dump(hostname, disk, level, dumpdate, progname, options)
    const char *hostname, *disk, *dumpdate, *progname, *options;
    int level;
{
    char level_string[NUM_STR_SIZE];
    char *req = NULL;
    char *authopt, *endauthopt, authoptbuf[64];
    int response_error;
    const security_driver_t *secdrv;

    /*
     * Default to bsd authentication if none specified.  This is gross.
     *
     * Options really need to be pre-parsed into some sort of structure
     * much earlier, and then flattened out again before transmission.
     */
    if ((authopt = strstr(options, "auth=")) == NULL ||
	(endauthopt = strchr(authopt, ';')) == NULL ||
	(sizeof(authoptbuf) - 1 < endauthopt - authopt)) {
	authopt = "BSD";
    } else {
	authopt += strlen("auth=");
	strncpy(authoptbuf, authopt, endauthopt - authopt);
	authoptbuf[endauthopt - authopt] = '\0';
	authopt = authoptbuf;
    }

    snprintf(level_string, sizeof(level_string), "%d", level);
    if(strncmp(progname,"DUMP",4) == 0 || strncmp(progname,"GNUTAR",6) == 0)
	req = vstralloc("SERVICE sendbackup\n",
		        "OPTIONS ",
		        "hostname=", hostname, ";",
		        "\n",
			progname, " ", disk, " ", level_string, " ", 
			dumpdate, " ",
		        "OPTIONS ", options,
			/* compat: if auth=krb4, send krb4-auth */
			(strcasecmp(authopt, "krb4") ? "" : "krb4-auth"),
		        "\n",
		        NULL);
    else
	req = vstralloc("SERVICE sendbackup\n",
		        "OPTIONS ",
		        "hostname=", hostname, ";",
		        "\n",
			"DUMPER ", progname, " ", disk, " ", level_string, " ",
			dumpdate, " ",
		        "OPTIONS ", options,
			/* compat: if auth=krb4, send krb4-auth */
			(strcasecmp(authopt, "krb4") ? "" : "krb4-auth"),
		        "\n",
		        NULL);

    secdrv = security_getdriver(authopt);
    if (secdrv == NULL) {
	error("no '%s' security driver available for host '%s'",
	    authopt, hostname);
    }

    protocol_sendreq(hostname, secdrv, req, STARTUP_TIMEOUT,
	sendbackup_response, &response_error);

    amfree(req);

    protocol_run();
    return (response_error);
}
