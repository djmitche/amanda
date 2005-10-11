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
/*
 * $Id: amrestore.c,v 1.52 2005/10/11 01:17:00 vectro Exp $
 *
 * retrieves files from an amanda tape
 */
/*
 * Pulls all files from the tape that match the hostname, diskname and
 * datestamp regular expressions.
 *
 * If the header is output, only up to DISK_BLOCK_BYTES worth of it is
 * sent, regardless of the tape blocksize.  This makes the disk image
 * look like a holding disk image, and also makes it easier to remove
 * the header (e.g. in amrecover) since it has a fixed size.
 */

#include "amanda.h"
#include "util.h"
#include "tapeio.h"
#include "fileheader.h"
#include "restore.h"

#define CREAT_MODE	0640

extern int bytes_read;

static int got_sigpipe, file_number;
static pid_t compress_pid = -1;
static int tapedev;
static long filefsf = -1;

/* local functions */

static void errexit P((void));
static void handle_sigpipe P((int sig));
static void usage P((void));
int main P((int argc, char **argv));

static void errexit()
/*
 * Do exit(2) after an error, rather than exit(1).
 */
{
    exit(2);
}


static void handle_sigpipe(sig)
int sig;
/*
 * Signal handler for the SIGPIPE signal.  Just sets a flag and returns.
 * The act of catching the signal causes the pipe write() to fail with
 * EINTR.
 */
{
    got_sigpipe++;
}

static void usage()
/*
 * Print usage message and terminate.
 */
{
    error("Usage: amrestore [-b blocksize] [-r|-c] [-p] [-h] [-f fileno] [-l label] tape-device|holdingfile [hostname [diskname [datestamp [hostname [diskname [datestamp ... ]]]]]]");
}

int main(argc, argv)
int argc;
char **argv;
/*
 * Parses command line, then loops through all files on tape, restoring
 * files that match the command line criteria.
 */
{
    extern int optind;
    int opt;
    char *errstr;
    int isafile;
    struct stat stat_tape;
    dumpfile_t file;
    char *filename = NULL;
    char *tapename = NULL;
    struct match_list {
	char *hostname;
	char *diskname;
	char *datestamp;
	struct match_list *next;
    } *match_list = NULL, *me = NULL;
    int found_match;
    int arg_state;
    amwait_t compress_status;
    int r = 0;
    char *e;
    char *err;
    char *label = NULL;
    rst_flags_t *rst_flags;
    int count_error;

    safe_fd(-1, 0);

    set_pname("amrestore");

    erroutput_type = ERR_INTERACTIVE;

    onerror(errexit);
    signal(SIGPIPE, handle_sigpipe);

    rst_flags = new_rst_flags();
    rst_flags->inline_assemble = 0;

    /* handle options */
    while( (opt = getopt(argc, argv, "b:cCd:rphf:l:")) != -1) {
	switch(opt) {
	case 'b':
	    rst_flags->blocksize = strtol(optarg, &e, 10);
	    if(*e == 'k' || *e == 'K') {
		rst_flags->blocksize *= 1024;
	    } else if(*e == 'm' || *e == 'M') {
		rst_flags->blocksize *= 1024 * 1024;
	    } else if(*e != '\0') {
		error("invalid rst_flags->blocksize value \"%s\"", optarg);
	    }
	    if(rst_flags->blocksize < DISK_BLOCK_BYTES) {
		error("minimum block size is %dk", DISK_BLOCK_BYTES / 1024);
	    }
	    break;
	case 'c': rst_flags->compress = 1; break;
	case 'C':
	    rst_flags->compress = 1;
	    rst_flags->comp_type = COMPRESS_BEST_OPT;
	    break;
	case 'r': rst_flags->raw = 1; break;
	case 'p': rst_flags->pipe_to_fd = fileno(stdout); break;
	case 'h': rst_flags->headers = 1; break;
	case 'f':
	    filefsf = strtol(optarg, &e, 10);
	    if(*e != '\0') {
		error("invalid fileno value \"%s\"", optarg);
	    }
	    break;
	case 'l':
	    label = stralloc(optarg);
	    break;
	default:
	    usage();
	}
    }

    if(rst_flags->compress && rst_flags->raw) {
	fprintf(stderr, 
		"Cannot specify both -r (raw) and -c (compressed) output.\n");
	usage();
    }

    if(optind >= argc) {
	fprintf(stderr, "%s: Must specify tape-device or holdingfile\n",
			get_pname());
	usage();
    }

    tapename = argv[optind++];

#define ARG_GET_HOST 0
#define ARG_GET_DISK 1
#define ARG_GET_DATE 2

    arg_state = ARG_GET_HOST;
    while(optind < argc) {
	switch(arg_state) {
	case ARG_GET_HOST:
	    /*
	     * This is a new host/disk/date triple, so allocate a match_list.
	     */
	    me = alloc(sizeof(*me));
	    me->hostname = argv[optind++];
	    me->diskname = "";
	    me->datestamp = "";
	    me->next = match_list;
	    match_list = me;
	    if(me->hostname[0] != '\0'
	       && (errstr=validate_regexp(me->hostname)) != NULL) {
	        fprintf(stderr, "%s: bad hostname regex \"%s\": %s\n",
		        get_pname(), me->hostname, errstr);
	        usage();
	    }
	    arg_state = ARG_GET_DISK;
	    break;
	case ARG_GET_DISK:
	    me->diskname = argv[optind++];
	    if(me->diskname[0] != '\0'
	       && (errstr=validate_regexp(me->diskname)) != NULL) {
	        fprintf(stderr, "%s: bad diskname regex \"%s\": %s\n",
		        get_pname(), me->diskname, errstr);
	        usage();
	    }
	    arg_state = ARG_GET_DATE;
	    break;
	case ARG_GET_DATE:
	    me->datestamp = argv[optind++];
	    if(me->datestamp[0] != '\0'
	       && (errstr=validate_regexp(me->datestamp)) != NULL) {
	        fprintf(stderr, "%s: bad datestamp regex \"%s\": %s\n",
		        get_pname(), me->datestamp, errstr);
	        usage();
	    }
	    arg_state = ARG_GET_HOST;
	    break;
	}
    }
    if(match_list == NULL) {
	match_list = alloc(sizeof(*match_list));
	match_list->hostname = "";
	match_list->diskname = "";
	match_list->datestamp = "";
	match_list->next = NULL;
    }

    if(tape_stat(tapename,&stat_tape)!=0) {
	error("could not stat %s: %s", tapename, strerror(errno));
    }
    isafile=S_ISREG((stat_tape.st_mode));

    if(label) {
	if(isafile) {
	    fprintf(stderr,"%s: ignoring -l flag when restoring from a file.\n",
		    get_pname());
	}
	else {
	    if((err = tape_rewind(tapename)) != NULL) {
		error("Could not rewind device '%s': %s", tapename, err);
	    }
	    tapedev = tape_open(tapename, 0);
	    read_file_header(&file, tapedev, isafile, rst_flags);
	    if(file.type != F_TAPESTART) {
		fprintf(stderr,"Not an amanda tape\n");
		exit (1);
	    }
	    if(strcmp(label, file.name) != 0) {
		fprintf(stderr,"Wrong label: '%s'\n", file.name);
		exit (1);
	    }
	    tapefd_close(tapedev);
	    if((err = tape_rewind(tapename)) != NULL) {
		error("Could not rewind device '%s': %s", tapename, err);
	    }
	}
    }
    file_number = 0;
    if(filefsf != -1) {
	if(isafile) {
	    fprintf(stderr,"%s: ignoring -f flag when restoring from a file.\n",
		    get_pname());
	}
	else {
	    if((err = tape_rewind(tapename)) != NULL) {
		error("Could not rewind device '%s': %s", tapename, err);
	    }
	    if((err = tape_fsf(tapename,filefsf)) != NULL) {
		error("Could not fsf device '%s': %s", tapename, err);
	    }
	    file_number = filefsf;
	}
    }

    if(isafile) {
	tapedev = open(tapename, 0);
    } else {
	tapedev = tape_open(tapename, 0);
    }
    if(tapedev < 0) {
	error("could not open %s: %s", tapename, strerror(errno));
    }

    read_file_header(&file, tapedev, isafile, rst_flags);

    if(file.type != F_TAPESTART && !isafile && filefsf == -1) {
	fprintf(stderr, "%s: WARNING: not at start of tape, file numbers will be offset\n",
			get_pname());
    }

    count_error=0;
    while(count_error < 10) {
	if(file.type == F_TAPEEND) break;
	found_match = 0;
	if(file.type == F_DUMPFILE || file.type == F_SPLIT_DUMPFILE) {
	    amfree(filename);
	    filename = make_filename(&file);
	    for(me = match_list; me; me = me->next) {
		if(disk_match(&file,me->datestamp,me->hostname,me->diskname,"") != 0) {
		    found_match = 1;
		    break;
		}
	    }
	    fprintf(stderr, "%s: %3d: %s ",
			    get_pname(),
			    file_number,
			    found_match ? "restoring" : "skipping");
	    if(file.type != F_DUMPFILE  && file.type != F_SPLIT_DUMPFILE) {
		print_header(stderr, &file);
	    } else {
		fprintf(stderr, "%s\n", filename);
	    }
	}
	if(found_match) {
	    restore(&file, filename, tapedev, isafile, rst_flags);
	    if(compress_pid > 0) {
		waitpid(compress_pid, &compress_status, 0);
		compress_pid = -1;
	    }
	    if(rst_flags->pipe_to_fd != -1) {
		file_number++;			/* for the last message */
		break;
	    }
	}
	if(isafile) {
	    break;
	}
	/*
	 * Note that at this point we know we are working with a tape,
	 * not a holding disk file, so we can call the tape functions
	 * without checking.
	 */
	if(bytes_read == 0) {
	    /*
	     * If the last read got EOF, how to get to the next
	     * file depends on how the tape device driver is acting.
	     * If it is BSD-like, we do not really need to do anything.
	     * If it is Sys-V-like, we need to either fsf or close/open.
	     * The good news is, a close/open works in either case,
	     * so that's what we do.
	     */
	    tapefd_close(tapedev);
	    if((tapedev = tape_open(tapename, 0)) < 0) {
		error("could not open %s: %s", tapename, strerror(errno));
	    }
	    count_error++;
	} else {
	    /*
	     * If the last read got something (even an error), we can
	     * do an fsf to get to the next file.
	     */
	    if(tapefd_fsf(tapedev, 1) < 0) {
		error("could not fsf %s: %s", tapename, strerror(errno));
	    }
	    count_error=0;
	}
	file_number++;
	read_file_header(&file, tapedev, isafile, rst_flags);
    }
    if(isafile) {
	close(tapedev);
    } else {
	/*
	 * See the notes above about advancing to the next file.
	 */
	if(bytes_read == 0) {
	    tapefd_close(tapedev);
	    if((tapedev = tape_open(tapename, 0)) < 0) {
		error("could not open %s: %s", tapename, strerror(errno));
	    }
	} else {
	    if(tapefd_fsf(tapedev, 1) < 0) {
		error("could not fsf %s: %s", tapename, strerror(errno));
	    }
	}
	tapefd_close(tapedev);
    }

    if((bytes_read <= 0 || file.type == F_TAPEEND) && !isafile) {
	fprintf(stderr, "%s: %3d: reached ", get_pname(), file_number);
	if(bytes_read <= 0) {
	    fprintf(stderr, "end of information\n");
	} else {
	    print_header(stderr,&file);
	}
	r = 1;
    }
    return r;
}
