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
/* $Id: chunker.c,v 1.1 2000/04/18 00:23:16 martinea Exp $
 *
 * requests remote amandad processes to dump filesystems
 */
#include "amanda.h"
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
#define MAX_ARGS	10
#define DATABUF_SIZE	TAPE_BLOCK_BYTES

#define STARTUP_TIMEOUT 60

typedef enum { BOGUS, PORT_WRITE, CONTINUE, ABORT, QUIT } cmd_t;

struct cmdargs {
    int argc;
    char *argv[MAX_ARGS + 1];
};

struct databuf {
    int fd;			/* file to flush to */
    const char *filename;	/* name of what fd points to */
    int filename_seq;		/* for chunking */
    long split_size;		/* when to chunk */
    long chunk_size;		/* size of each chunk */
    char buf[DATABUF_SIZE];
    char *dataptr;		/* data buffer markers */
    int spaceleft;
    pid_t compresspid;		/* valid if fd is pipe to compress */
};

int interactive;
char *handle = NULL;

char *errstr = NULL;
int abort_pending;
static long dumpsize, headersize;

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

/* local functions */
int main P((int, char **));
static cmd_t getcmd P((struct cmdargs *));
static void putresult P((const char *, ...))
    __attribute__ ((format (printf, 1, 2)));
static int write_tapeheader P((int, dumpfile_t *));
static void databuf_init P((struct databuf *, int, const char *, long));
static int databuf_write P((struct databuf *, const void *, int));
static int databuf_flush P((struct databuf *));

static int startup_chunker P((const char *, long, struct databuf *));
static int do_chunk P((int, struct databuf *));


int
main(main_argc, main_argv)
    int main_argc;
    char **main_argv;
{
    static struct databuf db;
    struct cmdargs cmdargs;
    cmd_t cmd;
    int infd, outfd;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    char *conffile;
    char *q = NULL;
    char *filename;
    long chunksize;
    times_t runtime;

    for (outfd = 3; outfd < FD_SETSIZE; outfd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(outfd);
    }

    set_pname("chunker");

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

/*    do {*/
	cmd = getcmd(&cmdargs);

	switch(cmd) {
	case QUIT:
	    break;

	case PORT_WRITE:
	    /*
	     * FILE-DUMP handle filename host disk level dumpdate chunksize
	     *   progname options
	     */
	    if (cmdargs.argc != 10)
		error("error [dumper FILE-DUMP argc != 10: %d]", cmdargs.argc);
	    handle = newstralloc(handle, cmdargs.argv[2]);
	    filename = cmdargs.argv[3];
	    hostname = newstralloc(hostname, cmdargs.argv[4]);
	    diskname = newstralloc(diskname, cmdargs.argv[5]);
	    level = atoi(cmdargs.argv[6]);
	    dumpdate = newstralloc(dumpdate, cmdargs.argv[7]);
	    chunksize = atoi(cmdargs.argv[8]);
	    chunksize = (chunksize/TAPE_BLOCK_SIZE)*TAPE_BLOCK_SIZE;
	    progname = newstralloc(progname, cmdargs.argv[9]);
	    options = newstralloc(options, cmdargs.argv[10]);

	    /*
	     * We start a subprocess to do the chunking, and pipe our
	     * data to it.  This allows us to insert a compress in between
	     * if srvcompress is set.
	     */
	    infd = startup_chunker(filename, chunksize, &db);
	    if (infd < 0) {
		q = squotef("[chunker startup failed: %s]",
		    strerror(errno));
		putresult("FAILED %s %s\n", handle, q);
		amfree(q);
		break;
	    }
	    if(do_chunk(infd, &db)) {
		char kb_str[NUM_STR_SIZE];
		char kps_str[NUM_STR_SIZE];
		double rt;

		runtime = stopclock();
		rt = runtime.r.tv_sec+runtime.r.tv_usec/1000000.0;
		snprintf(kb_str, sizeof(kb_str), "%ld", dumpsize - headersize);
		snprintf(kps_str, sizeof(kps_str), "%3.1f",
				rt ? dumpsize / rt : 0.0);
		errstr = newvstralloc(errstr,
				      "sec ", walltime_str(runtime),
				      " kb ", kb_str,
				      " kps ", kps_str,
				      NULL);
		q = squotef("[%s]",errstr);
		putresult("DONE %s %ld %s\n",
			  handle, dumpsize - headersize, q);
		log_add(L_SUCCESS, "%s %s %s %d [%s]",
				hostname, diskname, datestamp, level, errstr);
	    }
	    break;

	default:
	    q = squote(cmdargs.argv[1]);
	    putresult("BAD-COMMAND %s\n", q);
	    amfree(q);
	    break;
	}

	if (outfd != -1)
	    aclose(outfd);

/*    } while(cmd != QUIT); */

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
 * Returns a file descriptor to the incoming port
 * on success, or -1 on error.
 */
static int
startup_chunker(filename, chunksize, db)
    const char *filename;
    long chunksize;
    struct databuf *db;
{
    int infd, outfd;
    char *tmp_filename;
    int data_port, data_socket;

    data_port = 0;
    data_socket = stream_server(&data_port, DEFAULT_SIZE, DATABUF_SIZE);

    if(data_socket < 0) {
	error("AA");
    }

    putresult("PORT %d\n", data_port);

    if((infd = stream_accept(data_socket, CONNECT_TIMEOUT,
		DEFAULT_SIZE, TAPE_BLOCK_BYTES)) == -1) {
	error("BB");
    }

    tmp_filename = vstralloc(filename, ".tmp", NULL);
    if ((outfd = open(tmp_filename, O_WRONLY|O_CREAT|O_TRUNC, 0600)) < 0) {
	putresult("FAILED %s [holding file \"%s\": %s]", handle,
	    tmp_filename, strerror(errno));
	amfree(tmp_filename);
	exit(1);
    }
    amfree(tmp_filename);
    databuf_init(db, outfd, filename, chunksize);
    return infd;
}

static int
do_chunk(infd, db)
    int infd;
    struct databuf *db;
{
    int nread;
    char buf[TAPE_BLOCK_BYTES];

    startclock();

    /*
     * The first thing we should receive is the file header, which we
     * need to save into "file", as well as write out.  Later, the
     * chunk code will rewrite it.
     */
    if ((nread = read(infd, buf, sizeof(buf))) <= 0)
	exit(1);
    parse_file_header(buf, &file, nread);
    databuf_write(db, buf, nread);
    headersize = dumpsize;

    /*
     * We've written the file header.  Now, just write data until the
     * end.
     */
    while ((nread = read(infd, buf, sizeof(buf))) > 0) {
	databuf_write(db, buf, nread);
    }
    databuf_flush(db);
    return 1;
}

static cmd_t
getcmd(cmdargs)
    struct cmdargs *cmdargs;
{
    static const struct {
	const char str[12];
	cmd_t cmd;
    } cmdtab[] = {
	{ "PORT-WRITE", PORT_WRITE },
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


arglist_function(static void putresult, const char *, format)
{
    va_list argp;

    arglist_start(argp, format);
    vprintf(format, argp);
    fflush(stdout);
    arglist_end(argp);
}

/*
 * Initialize a databuf.  Takes a writeable file descriptor.
 */
static void
databuf_init(db, fd, filename, split_size)
    struct databuf *db;
    int fd;
    const char *filename;
    long split_size;
{

    db->fd = fd;
    db->filename = filename;
    db->filename_seq = 0;
    db->chunk_size = db->split_size = split_size;
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
    int fd, written, off;
    cmd_t cmd;
    char *tmp_filename;

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
     * See if we need to split this file.
     */
    if (db->split_size > 0 && dumpsize >= db->split_size) {
	/*
	 * First, update the header of the current file to point
	 * to the next chunk, and then close it.
	 */
	fd = db->fd;
	if (lseek(fd, (off_t)0, SEEK_SET) < 0) {
	    errstr = squotef("lseek holding file: %s", strerror(errno));
	    return (-1);
	}
	snprintf(file.cont_filename, sizeof(file.cont_filename),
	    "%s.%d", db->filename, ++db->filename_seq);
	write_tapeheader(fd, &file);
	aclose(fd);

	/*
	 * Now, open the new chunk file, and give it a new header
	 * that has no cont_filename pointer.
	 */
	tmp_filename = vstralloc(file.cont_filename, ".tmp", NULL);
	if ((fd = open(tmp_filename, O_WRONLY|O_CREAT|O_TRUNC, 0600)) == -1) {
	    errstr = squotef("holding file \"%s\": %s",
			tmp_filename, strerror(errno));
	    amfree(tmp_filename);
	    return (-1);
	}
	file.type = F_CONT_DUMPFILE;
	file.cont_filename[0] = '\0';
	write_tapeheader(fd, &file);
	dumpsize += TAPE_BLOCK_SIZE;
	/*
	 * XXX this is bogus - this is being updated in the chunker process
	 * and therefore will never be seen by the dumper.
	 */
	headersize += TAPE_BLOCK_SIZE;
	amfree(tmp_filename);

	/*
	 * Now put give the new file the old file's descriptor
	 */
	if (fd != db->fd) {
	    if (dup2(fd, db->fd) == -1) {
		errstr = squotef("can't dup2: %s", strerror(errno));
		return (-1);
	    }
	    aclose(fd);
	}

	/*
	 * Update when we need to chunk again
	 */
	db->split_size += db->chunk_size;
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
    return (0);
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

    write_header(buffer, file, sizeof(buffer));
    write(outfd, buffer, sizeof(buffer));
    return (0);
}


