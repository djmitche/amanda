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
/* $Id: chunker.c,v 1.12 2001/12/30 17:42:07 martinea Exp $
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

#define STARTUP_TIMEOUT 60

struct databuf {
    int fd;			/* file to flush to */
    char *filename;		/* name of what fd points to */
    int filename_seq;		/* for chunking */
    long split_size;		/* when to chunk */
    long chunk_size;		/* size of each chunk */
    long use;			/* size to use on this disk */
    char buf[DISK_BLOCK_BYTES];
    char *datain;		/* data buffer markers */
    char *dataout;
    char *datalimit;
};

int interactive;
char *handle = NULL;

char *errstr = NULL;
int abort_pending;
static long dumpsize, headersize;
static long dumpbytes;
static long filesize;

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
static int write_tapeheader P((int, dumpfile_t *));
static void databuf_init P((struct databuf *, int, char *, long, long));
static int databuf_flush P((struct databuf *));

static int startup_chunker P((char *, long, long, struct databuf *));
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
    long chunksize, use;
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
	error("errors processing config file \"%s\"", conffile);
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

    datestamp = construct_datestamp(NULL);

/*    do {*/
	cmd = getcmd(&cmdargs);

	switch(cmd) {
	case QUIT:
	    break;

	case PORT_WRITE:
	    /*
	     * PORT-WRITE handle filename host disk level dumpdate chunksize
	     *   progname use options
	     */
	    if (cmdargs.argc != 11)
		error("error [dumper PORT-WRITE argc != 11: %d]", cmdargs.argc);
	    handle = newstralloc(handle, cmdargs.argv[2]);
	    filename = cmdargs.argv[3];
	    hostname = newstralloc(hostname, cmdargs.argv[4]);
	    diskname = newstralloc(diskname, cmdargs.argv[5]);
	    level = atoi(cmdargs.argv[6]);
	    dumpdate = newstralloc(dumpdate, cmdargs.argv[7]);
	    chunksize = am_floor(atoi(cmdargs.argv[8]), DISK_BLOCK_KB);
	    progname = newstralloc(progname, cmdargs.argv[9]);
	    use = am_floor(atoi(cmdargs.argv[10]), DISK_BLOCK_KB);
	    options = newstralloc(options, cmdargs.argv[11]);

	    while((infd = startup_chunker(filename, use, chunksize, &db)) < 0) {
		q = squotef("[chunker startup failed: %s]", errstr);
		if(infd == -2) {
		    putresult(TRYAGAIN, "%s %s\n", handle, q);
		}
	    }
	    if(infd >= 0 && do_chunk(infd, &db)) {
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
		q = squotef("[%s]", errstr);
		putresult(DONE, "%s %ld %s\n",
			  handle, dumpsize - headersize, q);
		log_add(L_SUCCESS, "%s %s %s %d [%s]",
				hostname, diskname, datestamp, level, errstr);
	    } else if(infd != -2) {
		if(!abort_pending) {
		    if(q == NULL) {
			q = squotef("[%s]", errstr);
		    }
		    putresult(FAILED, "%s %s\n", handle, q);
		    amfree(q);
		}
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
startup_chunker(filename, use, chunksize, db)
    char *filename;
    long use;
    long chunksize;
    struct databuf *db;
{
    int infd, outfd;
    char *tmp_filename;
    int data_port, data_socket;

    data_port = 0;
    data_socket = stream_server(&data_port, -1, STREAM_BUFSIZE);

    if(data_socket < 0) {
	errstr = stralloc2("error creating stream server: ", strerror(errno));
	return -1;
    }

    putresult(PORT, "%d\n", data_port);

    infd = stream_accept(data_socket, CONNECT_TIMEOUT, -1, NETWORK_BLOCK_BYTES);
    if(infd == -1) {
	errstr = stralloc2("error accepting stream: ", strerror(errno));
	return -1;
    }

    tmp_filename = vstralloc(filename, ".tmp", NULL);
    if ((outfd = open(tmp_filename, O_RDWR|O_CREAT|O_TRUNC, 0600)) < 0) {
	int save_errno = errno;

	errstr = squotef("holding file \"%s\": %s",
			 tmp_filename,
			 strerror(errno));
	amfree(tmp_filename);
	aclose(infd);
	if(save_errno == ENOSPC) {
	    putresult(NO_ROOM, "%s %lu", handle, use);
	    return -2;
	} else {
	    return -1;
	}
    }
    amfree(tmp_filename);
    databuf_init(db, outfd, filename, use, chunksize);
    db->filename_seq++;
    return infd;
}

static int
do_chunk(infd, db)
    int infd;
    struct databuf *db;
{
    int nread;
    char header_buf[DISK_BLOCK_BYTES];

    startclock();

    dumpsize = headersize = dumpbytes = filesize = 0;

    /*
     * The first thing we should receive is the file header, which we
     * need to save into "file", as well as write out.  Later, the
     * chunk code will rewrite it.
     */
    nread = fullread(infd, header_buf, sizeof(header_buf));
    if (nread != DISK_BLOCK_BYTES) {
	char number1[NUM_STR_SIZE];
	char number2[NUM_STR_SIZE];

	if(nread < 0) {
	    errstr = stralloc2("cannot read header: ", strerror(errno));
	} else {
	    snprintf(number1, sizeof(number1), "%d", nread);
	    snprintf(number2, sizeof(number2), "%d", DISK_BLOCK_BYTES);
	    errstr = vstralloc("cannot read header: got ",
			       number1,
			       " instead of ",
			       number2,
			       NULL);
	}
	return 0;
    }
    parse_file_header(header_buf, &file, nread);
    if(write_tapeheader(db->fd, &file)) {
	int save_errno = errno;

	errstr = squotef("write_tapeheader file \"%s\": %s",
			 db->filename, strerror(errno));
	if(save_errno == ENOSPC) {
	    putresult(NO_ROOM, "%s %lu\n", handle, 
		      db->use+db->split_size-dumpsize);
	}
	return 0;
    }
    dumpsize += DISK_BLOCK_KB;
    filesize = DISK_BLOCK_KB;
    headersize += DISK_BLOCK_KB;

    /*
     * We've written the file header.  Now, just write data until the
     * end.
     */
    while ((nread = fullread(infd, db->buf, db->datalimit - db->datain)) > 0) {
	db->datain += nread;
	while(db->dataout < db->datain) {
	    if(!databuf_flush(db)) {
		return 0;
	    }
	}
    }
    while(db->dataout < db->datain) {
	if(!databuf_flush(db)) {
	    return 0;
	}
    }
    if(dumpbytes > 0) {
	dumpsize++;			/* count partial final KByte */
	filesize++;
    }
    return 1;
}

/*
 * Initialize a databuf.  Takes a writeable file descriptor.
 */
static void
databuf_init(db, fd, filename, use, chunk_size)
    struct databuf *db;
    int fd;
    char *filename;
    long use;
    long chunk_size;
{
    db->fd = fd;
    db->filename = stralloc(filename);
    db->filename_seq = 0;
    db->chunk_size = chunk_size;
    db->split_size = (db->chunk_size > use) ? use : db->chunk_size;
    db->use = (use>db->split_size) ? use - db->split_size : 0;
    db->datain = db->dataout = db->buf;
    db->datalimit = db->buf + sizeof(db->buf);
}


/*
 * Write out the buffer to the backing file
 */
static int
databuf_flush(db)
    struct databuf *db;
{
    struct cmdargs cmdargs;
    int rc = 1;
    int w, written;
    long left_in_chunk;
    char *new_filename = NULL;
    char *tmp_filename = NULL;
    char sequence[NUM_STR_SIZE];
    int newfd;
    filetype_t save_type;

    /*
     * If there's no data, do nothing.
     */
    if (db->dataout >= db->datain) {
	goto common_exit;
    }

    /*
     * See if we need to split this file.
     */
    while (db->split_size > 0 && dumpsize >= db->split_size) {
	if( db->use == 0 ) {
	    /*
	     * Probably no more space on this disk.  Request some more.
	     */
	    cmd_t cmd;

	    putresult(RQ_MORE_DISK, "%s\n", handle);
	    cmd = getcmd(&cmdargs);
	    if(cmd == CONTINUE) {
		/* CONTINUE filename chunksize use */
		db->chunk_size = am_floor(atoi(cmdargs.argv[3]), DISK_BLOCK_KB);
		db->use = atoi(cmdargs.argv[4]);
		if(strcmp( db->filename, cmdargs.argv[2]) == 0) {
		    /*
		     * Same disk, so use what room is left up to the
		     * next chunk boundary or the amount we were given,
		     * whichever is less.
		     */
		    left_in_chunk = db->chunk_size - filesize;
		    if(left_in_chunk > db->use) {
			db->split_size += db->use;
			db->use = 0;
		    } else {
			db->split_size += left_in_chunk;
			db->use -= left_in_chunk;
		    }
		    if(left_in_chunk > 0) {
			/*
			 * We still have space in this chunk.
			 */
			break;
		    }
		} else {
		    /*
		     * Different disk, so use new file.
		     */
		    db->filename = newstralloc(db->filename, cmdargs.argv[2]);
		}
	    } else if(cmd == ABORT) {
		abort_pending = 1;
		errstr = newstralloc(errstr, "ERROR");
		putresult(ABORT_FINISHED, "%s\n", handle);
		rc = 0;
		goto common_exit;
	    } else {
		error("error [bad command after RQ-MORE-DISK: %d]", cmd);
	    }
	}

	/*
	 * Time to use another file.
	 */

	/*
	 * First, open the new chunk file, and give it a new header
	 * that has no cont_filename pointer.
	 */
	snprintf(sequence, sizeof(sequence), "%d", db->filename_seq);
	new_filename = newvstralloc(new_filename,
				    db->filename,
				    ".",
				    sequence,
				    NULL);
	tmp_filename = newvstralloc(tmp_filename,
				    new_filename,
				    ".tmp",
				    NULL);
	newfd = open(tmp_filename, O_RDWR|O_CREAT|O_TRUNC, 0600);
	if (newfd == -1) {
	    int save_errno = errno;

	    if(save_errno == ENOSPC) {
		putresult(NO_ROOM, "%s %lu\n",
			  handle, 
			  db->use+db->split_size-dumpsize);
		db->use = 0;			/* force RQ_MORE_DISK */
		db->split_size = dumpsize;
		continue;
	    }
	    errstr = squotef("creating chunk holding file \"%s\": %s",
			     tmp_filename,
			     strerror(errno));
	    aclose(db->fd);
	    rc = 0;
	    goto common_exit;
	}
	save_type = file.type;
	file.type = F_CONT_DUMPFILE;
	file.cont_filename[0] = '\0';
	if(write_tapeheader(newfd, &file)) {
	    int save_errno = errno;

	    aclose(newfd);
	    if(save_errno == ENOSPC) {
		putresult(NO_ROOM, "%s %lu\n",
			  handle, 
			  db->use+db->split_size-dumpsize);
		db->use = 0;			/* force RQ_MORE DISK */
		db->split_size = dumpsize;
		continue;
	    }
	    errstr = squotef("write_tapeheader file \"%s\": %s",
			     tmp_filename,
			     strerror(errno));
	    rc = 0;
	    goto common_exit;
	}

	/*
	 * Now, update the header of the current file to point
	 * to the next chunk, and then close it.
	 */
	if (lseek(db->fd, (off_t)0, SEEK_SET) < 0) {
	    errstr = squotef("lseek holding file \"%s\": %s",
			     db->filename,
			     strerror(errno));
	    aclose(newfd);
	    rc = 0;
	    goto common_exit;
	}

	file.type = save_type;
	strncpy(file.cont_filename, new_filename, sizeof(file.cont_filename));
	file.cont_filename[sizeof(file.cont_filename)] = '\0';
	if(write_tapeheader(db->fd, &file)) {
	    errstr = squotef("write_tapeheader file \"%s\": %s",
			     db->filename,
			     strerror(errno));
	    aclose(newfd);
	    unlink(tmp_filename);
	    rc = 0;
	    goto common_exit;
	}
	file.type = F_CONT_DUMPFILE;

	/*
	 * Now shift the file descriptor.
	 */
	aclose(db->fd);
	db->fd = newfd;
	newfd = -1;

	/*
	 * Update when we need to chunk again
	 */
	if(db->use <= DISK_BLOCK_KB) {
	    /*
	     * Cheat and use one more block than allowed so we can make
	     * some progress.
	     */
	    db->split_size += 2 * DISK_BLOCK_KB;
	    db->use = 0;
	} else if(db->chunk_size > db->use) {
	    db->split_size += db->use;
	    db->use = 0;
	} else {
	    db->split_size += db->chunk_size;
	    db->use -= db->chunk_size;
	}


	amfree(tmp_filename);
	amfree(new_filename);
	dumpsize += DISK_BLOCK_KB;
	filesize = DISK_BLOCK_KB;
	headersize += DISK_BLOCK_KB;
	db->filename_seq++;
    }

    /*
     * Write out the buffer
     */
    written = w = 0;
    while (db->dataout < db->datain) {
	if ((w = write(db->fd, db->dataout, db->datain - db->dataout)) < 0) {
	    break;
	}
	db->dataout += w;
	written += w;
    }
    dumpbytes += written;
    dumpsize += (dumpbytes / 1024);
    filesize += (dumpbytes / 1024);
    dumpbytes %= 1024;
    if (w < 0) {
	if (errno != ENOSPC) {
	    errstr = squotef("data write: %s", strerror(errno));
	    rc = 0;
	    goto common_exit;
	}

	/*
	 * NO-ROOM is informational only.  Later, RQ_MORE_DISK will be
	 * issued to use another holding disk.
	 */
	putresult(NO_ROOM, "%s %lu\n", handle, db->use+db->split_size-dumpsize);
	db->use = 0;				/* force RQ_MORE_DISK */
	db->split_size = dumpsize;
	goto common_exit;
    }
    if (db->datain == db->dataout) {
	/*
	 * We flushed the whole buffer so reset to use it all.
	 */
	db->datain = db->dataout = db->buf;
    }

common_exit:

    amfree(new_filename);
    amfree(tmp_filename);
    return rc;
}


/*
 * Send an Amanda dump header to the output file.
 */
static int
write_tapeheader(outfd, file)
    int outfd;
    dumpfile_t *file;
{
    char buffer[DISK_BLOCK_BYTES];
    int written;

    build_header(buffer, file, sizeof(buffer), sizeof(buffer));

    written = fullwrite(outfd, buffer, sizeof(buffer));
    if(written == sizeof(buffer)) return 0;
    if(written < 0) return written;
    errno = ENOSPC;
    return -1;
}
