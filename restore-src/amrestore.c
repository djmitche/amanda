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
 * $Id: amrestore.c,v 1.28.2.4.4.3.2.6 2002/10/30 22:18:02 martinea Exp $
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
#include "tapeio.h"
#include "fileheader.h"
#include "util.h"

#define CREAT_MODE	0640

char *buffer = NULL;

int compflag, rawflag, pipeflag, headerflag;
int got_sigpipe, file_number;
pid_t compress_pid = -1;
char *compress_type = COMPRESS_FAST_OPT;
int tapedev;
int bytes_read;
long blocksize = -1;
long filefsf = -1;

/* local functions */

void errexit P((void));
void handle_sigpipe P((int sig));
int disk_match P((dumpfile_t *file, char *datestamp, 
		  char *hostname, char *diskname));
char *make_filename P((dumpfile_t *file));
void read_file_header P((dumpfile_t *file, int isafile));
static int get_block P((int isafile));
void restore P((dumpfile_t *file, char *filename, int isafile));
void usage P((void));
int main P((int argc, char **argv));

void errexit()
/*
 * Do exit(2) after an error, rather than exit(1).
 */
{
    exit(2);
}


void handle_sigpipe(sig)
int sig;
/*
 * Signal handler for the SIGPIPE signal.  Just sets a flag and returns.
 * The act of catching the signal causes the pipe write() to fail with
 * EINTR.
 */
{
    got_sigpipe++;
}

int disk_match(file, datestamp, hostname, diskname)
dumpfile_t *file;
char *datestamp, *hostname, *diskname;

/*
 * Returns 1 if the current dump file matches the hostname and diskname
 * regular expressions given on the command line, 0 otherwise.  As a 
 * special case, empty regexs are considered equivalent to ".*": they 
 * match everything.
 */
{
    if(file->type != F_DUMPFILE) return 0;

    if((*hostname == '\0' || match_host(hostname, file->name)) &&
       (*diskname == '\0' || match_disk(diskname, file->disk)) &&
       (*datestamp== '\0' || match_datestamp(datestamp, file->datestamp)))
	return 1;
    else
	return 0;
}


char *make_filename(file)
dumpfile_t *file;
{
    char number[NUM_STR_SIZE];
    char *sfn;
    char *fn;

    ap_snprintf(number, sizeof(number), "%d", file->dumplevel);
    sfn = sanitise_filename(file->disk);
    fn = vstralloc(file->name,
		   ".",
		   sfn, 
		   ".",
		   file->datestamp,
		   ".",
		   number,
		   NULL);
    amfree(sfn);
    return fn;
}


static int get_block(isafile)
int isafile;
{
    static int test_blocksize = 1;
    int buflen;

    /*
     * If this is the first call, set the blocksize if it was not on
     * the command line.  Allocate the I/O buffer in any case.
     *
     * For files, the blocksize is always DISK_BLOCK_BYTES.  For tapes,
     * we allocate a large buffer and set the size to the length of the
     * first (successful) record.
     */
    buflen = blocksize;
    if(test_blocksize) {
	if(blocksize < 0) {
	    if(isafile) {
		blocksize = buflen = DISK_BLOCK_BYTES;
	    } else {
		buflen = MAX_TAPE_BLOCK_BYTES;
	    }
	}
	buffer = newalloc(buffer, buflen);
    }
    if(isafile) {
	bytes_read = fullread(tapedev, buffer, buflen);
    } else {
	bytes_read = tapefd_read(tapedev, buffer, buflen);
	if(blocksize < 0 && bytes_read > 0 && bytes_read < buflen) {
	    char *new_buffer;

	    blocksize = bytes_read;
	    new_buffer = alloc(blocksize);
	    memcpy(new_buffer, buffer, bytes_read);
	    amfree(buffer);
	    buffer = new_buffer;
	}
    }
    if(blocksize > 0) {
	test_blocksize = 0;
    }
    return bytes_read;
}


void read_file_header(file, isafile)
dumpfile_t *file;
int isafile;
/*
 * Reads the first block of a tape file.
 */
{
    bytes_read = get_block(isafile);
    if(bytes_read < 0) {
	error("error reading file header: %s", strerror(errno));
    } else if(bytes_read < blocksize) {
	if(bytes_read == 0) {
	    fprintf(stderr, "%s: missing file header block\n", get_pname());
	} else {
	    fprintf(stderr, "%s: short file header block: %d byte%s\n",
		    get_pname(), bytes_read, (bytes_read == 1) ? "" : "s");
	}
	file->type = F_UNKNOWN;
    } else {
	parse_file_header(buffer, file, bytes_read);
    }
    return;
}


void restore(file, filename, isafile)
dumpfile_t *file;
char *filename;
int isafile;
/*
 * Restore the current file from tape.  Depending on the settings of
 * the command line flags, the file might need to be compressed or
 * uncompressed.  If so, a pipe through compress or uncompress is set
 * up.  The final output usually goes to a file named host.disk.date.lev,
 * but with the -p flag the output goes to stdout (and presumably is
 * piped to restore).
 */
{
    int rc = 0, dest, out, outpipe[2];
    int wc;
    int l, s;
    int file_is_compressed;

    /* adjust compression flag */

    file_is_compressed = file->compressed;
    if(!compflag && file_is_compressed && !known_compress_type(file)) {
	fprintf(stderr, 
		"%s: unknown compression suffix %s, can't uncompress\n",
		get_pname(), file->comp_suffix);
	compflag = 1;
    }

    /* set up final destination file */

    if(pipeflag)
	dest = 1;		/* standard output */
    else {
	char *filename_ext = NULL;

	if(compflag) {
	    filename_ext = file_is_compressed ? file->comp_suffix
					      : COMPRESS_SUFFIX;
	} else if(rawflag) {
	    filename_ext = ".RAW";
	} else {
	    filename_ext = "";
	}
	filename_ext = stralloc2(filename, filename_ext);

	if((dest = creat(filename, CREAT_MODE)) < 0)
	    error("could not create output file: %s", strerror(errno));
	amfree(filename_ext);
    }

    out = dest;

    /*
     * If -r or -h, write the header before compress or uncompress pipe.
     * Only write DISK_BLOCK_BYTES, regardless of how much was read.
     * This makes the output look like a holding disk image, and also
     * makes it easier to remove the header (e.g. in amrecover) since
     * it has a fixed size.
     */
    if(rawflag || headerflag) {
	int w;
	char *cont_filename;

	if(compflag && !file_is_compressed) {
	    file->compressed = 1;
	    ap_snprintf(file->uncompress_cmd, sizeof(file->uncompress_cmd),
		        " %s %s |", UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
		        UNCOMPRESS_OPT
#else
		        ""
#endif
		        );
	    strncpy(file->comp_suffix,
		    COMPRESS_SUFFIX,
		    sizeof(file->comp_suffix)-1);
	    file->comp_suffix[sizeof(file->comp_suffix)-1] = '\0';
	}

	/* remove CONT_FILENAME from header */
	cont_filename = stralloc(file->cont_filename);
	memset(file->cont_filename,'\0',sizeof(file->cont_filename));
	file->blocksize = DISK_BLOCK_BYTES;
	build_header(buffer, file, bytes_read);

	if((w = fullwrite(out, buffer, DISK_BLOCK_BYTES)) != DISK_BLOCK_BYTES) {
	    if(w < 0) {
		error("write error: %s", strerror(errno));
	    } else {
		error("write error: %d instead of %d", w, DISK_BLOCK_BYTES);
	    }
	}
	/* add CONT_FILENAME to header */
	strncpy(file->cont_filename, cont_filename, sizeof(file->cont_filename));
    }

    /* if -c and file not compressed, insert compress pipe */

    if(compflag && !file_is_compressed) {
	if(pipe(outpipe) < 0) error("error [pipe: %s]", strerror(errno));
	out = outpipe[1];
	switch(compress_pid = fork()) {
	case -1: error("could not fork for %s: %s",
		       COMPRESS_PATH, strerror(errno));
	default:
	    aclose(outpipe[0]);
	    aclose(dest);
	    break;
	case 0:
	    aclose(outpipe[1]);
	    if(outpipe[0] != 0) {
		if(dup2(outpipe[0], 0) == -1)
		    error("error [dup2 pipe: %s]", strerror(errno));
		aclose(outpipe[0]);
	    }
	    if(dest != 1) {
		if(dup2(dest, 1) == -1)
		    error("error [dup2 dest: %s]", strerror(errno));
		aclose(dest);
	    }
	    if (*compress_type == '\0') {
		compress_type = NULL;
	    }
	    execlp(COMPRESS_PATH, COMPRESS_PATH, compress_type, (char *)0);
	    error("could not exec %s: %s", COMPRESS_PATH, strerror(errno));
	}
    }

    /* if not -r or -c, and file is compressed, insert uncompress pipe */

    else if(!rawflag && !compflag && file_is_compressed) {
	/* 
	 * XXX for now we know that for the two compression types we
	 * understand, .Z and optionally .gz, UNCOMPRESS_PATH will take
	 * care of both.  Later, we may need to reference a table of
	 * possible uncompress programs.
	 */
	if(pipe(outpipe) < 0) error("error [pipe: %s]", strerror(errno));
	out = outpipe[1];
	switch(compress_pid = fork()) {
	case -1: 
	    error("could not fork for %s: %s",
		  UNCOMPRESS_PATH, strerror(errno));
	default:
	    aclose(outpipe[0]);
	    aclose(dest);
	    break;
	case 0:
	    aclose(outpipe[1]);
	    if(outpipe[0] != 0) {
		if(dup2(outpipe[0], 0) < 0)
		    error("dup2 pipe: %s", strerror(errno));
		aclose(outpipe[0]);
	    }
	    if(dest != 1) {
		if(dup2(dest, 1) < 0)
		    error("dup2 dest: %s", strerror(errno));
		aclose(dest);
	    }
	    (void) execlp(UNCOMPRESS_PATH, UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
			  UNCOMPRESS_OPT,
#endif
			  (char *)0);
	    error("could not exec %s: %s", UNCOMPRESS_PATH, strerror(errno));
	}
    }


    /* copy the rest of the file from tape to the output */
    got_sigpipe = 0;
    wc = 0;
    do {
	bytes_read = get_block(isafile);
	if(bytes_read < 0) {
	    error("read error: %s", strerror(errno));
	}
	if(bytes_read == 0 && isafile) {
	    /*
	     * See if we need to switch to the next file.
	     */
	    if(file->cont_filename[0] == '\0') {
		break;				/* no more files */
	    }
	    close(tapedev);
	    if((tapedev = open(file->cont_filename, O_RDONLY)) == -1) {
		char *cont_filename = strrchr(file->cont_filename,'/');
		if(cont_filename) {
		    cont_filename++;
		    if((tapedev = open(cont_filename,O_RDONLY)) == -1) {
			error("can't open %s: %s", file->cont_filename,
			      strerror(errno));
		    }
		    else {
			fprintf(stderr, "cannot open %s: %s\n",
				file->cont_filename, strerror(errno));
			fprintf(stderr, "using %s\n",
				cont_filename);
		    }
		}
		else {
		    error("can't open %s: %s", file->cont_filename,
			  strerror(errno));
		}
	    }
	    read_file_header(file, isafile);
	    if(file->type != F_DUMPFILE && file->type != F_CONT_DUMPFILE) {
		fprintf(stderr, "unexpected header type: ");
		print_header(stderr, file);
		exit(2);
	    }
	    continue;
	}
	for(l = 0; l < bytes_read; l += s) {
	    if((s = write(out, buffer + l, bytes_read - l)) < 0) {
		if(got_sigpipe) {
		    fprintf(stderr,"Error %d (%s) offset %d+%d, wrote %d\n",
				   errno, strerror(errno), wc, bytes_read, rc);
		    fprintf(stderr,  
			    "%s: pipe reader has quit in middle of file.\n",
			    get_pname());
		} else {
		    perror("amrestore: write error");
		}
		exit(2);
	    }
	}
	wc += bytes_read;
    } while (bytes_read > 0);
    if(pipeflag) {
	if(out != dest) {
	    aclose(out);
	}
    } else {
	aclose(out);
    }
}


void usage()
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
    int fd;
    int r = 0;
    char *e;
    char *err;
    char *label = NULL;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    set_pname("amrestore");

    erroutput_type = ERR_INTERACTIVE;

    onerror(errexit);
    signal(SIGPIPE, handle_sigpipe);

    /* handle options */
    while( (opt = getopt(argc, argv, "b:cCd:rpkhf:l:")) != -1) {
	switch(opt) {
	case 'b':
	    blocksize = strtol(optarg, &e, 10);
	    if(e == NULL) {
		error("cannot convert \"%s\"", optarg);
	    }
	    if(strcasecmp(e, "k") == 0) {
		blocksize *= 1024;
	    } else if(strcasecmp(e, "m") == 0) {
		blocksize *= 1024 * 1024;
	    }
	    if(blocksize < DISK_BLOCK_BYTES) {
		error("minimum block size is %dk", DISK_BLOCK_BYTES / 1024);
	    }
	    break;
	case 'c': compflag = 1; break;
	case 'C': compflag = 1; compress_type = COMPRESS_BEST_OPT; break;
	case 'r': rawflag = 1; break;
	case 'p': pipeflag = 1; break;
	case 'h': headerflag = 1; break;
	case 'f':
	    filefsf = strtol(optarg, &e, 10);
	    if(e != NULL && *e != '\0') {
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

    if(compflag && rawflag) {
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
	error("could not stat %s",tapename);
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
	    read_file_header(&file, isafile);
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

    read_file_header(&file, isafile);

    if(file.type != F_TAPESTART && !isafile && filefsf == -1) {
	fprintf(stderr, "%s: WARNING: not at start of tape, file numbers will be offset\n",
			get_pname());
    }

    while(file.type == F_TAPESTART || file.type == F_DUMPFILE) {
	amfree(filename);
	filename = make_filename(&file);
	found_match = 0;
	for(me = match_list; me; me = me->next) {
	    if(disk_match(&file,me->datestamp,me->hostname,me->diskname) != 0) {
		found_match = 1;
		break;
	    }
	}
	fprintf(stderr, "%s: %3d: %s ",
			get_pname(),
			file_number,
			found_match ? "restoring" : "skipping");
	if(file.type != F_DUMPFILE) {
	    print_header(stderr, &file);
	} else {
	    fprintf(stderr, "%s\n", filename);
	}
	if(found_match) {
	    restore(&file, filename, isafile);
	    if(compress_pid > 0) {
		waitpid(compress_pid, &compress_status, 0);
		compress_pid = -1;
	    }
	    if(pipeflag) {
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
	} else {
	    /*
	     * If the last read got something (even an error), we can
	     * do an fsf to get to the next file.
	     */
	    if(tapefd_fsf(tapedev, 1) < 0) {
		error("could not fsf %s: %s", tapename, strerror(errno));
	    }
	}
	file_number++;
	read_file_header(&file, isafile);
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
