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
 * $Id: amrestore.c,v 1.31 1999/06/08 14:50:55 kashmir Exp $
 *
 * retrieves files from an amanda tape
 */
/*
 * usage: amrestore [-p] [-h] [-r|-c] tape-device [hostname [diskname [datestamp [hostname [diskname [datestamp ... ]]]]]]
 *
 * Pulls all files from the tape that match the hostname, diskname and
 * datestamp regular expressions.
 *
 * For example, specifying "rz1" as the diskname matches "rz1a",
 * "rz1g" etc on the tape.
 *
 * Command line options:
 *	-p   put output on stdout
 *	-c   write compressed
 *	-r   raw, write file as is on tape (with header, possibly compressed)
 *	-h   write the header too
 */

#include "amanda.h"
#include "util.h"
#include "tapeio.h"
#include "fileheader.h"

#define CREAT_MODE	0640

char buffer[TAPE_BLOCK_BYTES];


int compflag, rawflag, pipeflag, headerflag;
int buflen, got_sigpipe, file_number;
pid_t compress_pid = -1;

/* local functions */

void errexit P((void));
void handle_sigpipe P((int sig));
int disk_match P((dumpfile_t *file, char *datestamp, 
		  char *hostname, char *diskname));
char *make_filename P((dumpfile_t *file));
int read_file_header P((char *buffer, dumpfile_t *file,
			int buflen, int tapedev));
void restore P((dumpfile_t *file, char *filename,
		int tapedev, int isafile));
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

    if((*hostname == '\0' || match(hostname, file->name)) &&
       (*diskname == '\0' || match(diskname, file->disk)) &&
       (*datestamp == '\0' || match(datestamp, file->datestamp)))
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

    snprintf(number, sizeof(number), "%d", file->dumplevel);
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


int read_file_header(buffer, file, buflen, tapedev)
char *buffer;
dumpfile_t *file;
int buflen;
int tapedev;
/*
 * Reads the first block of a tape file.
 */
{
    int bytes_read;
    bytes_read = tapefd_read(tapedev, buffer, buflen);
    if(bytes_read < 0) {
	error("error reading file header: %s", strerror(errno));
    }
    else if(bytes_read < buflen) {
	if(bytes_read == 0) {
	    fprintf(stderr, "%s: missing file header block\n", get_pname());
	} else {
	    fprintf(stderr, "%s: short file header block: %d byte%s\n",
		    get_pname(), bytes_read, (bytes_read == 1) ? "" : "s");
	}
	file->type = F_TAPEEND;
    }
    else {
	parse_file_header(buffer, file, bytes_read);
    }
    return(bytes_read);
}


void restore(file, filename, tapedev, isafile)
dumpfile_t *file;
char *filename;
int tapedev;
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
    int rc, dest, out, outpipe[2];
    int wc;

    /* adjust compression flag */

    if(!compflag && file->compressed && !known_compress_type(file)) {
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
	    filename_ext = file->compressed ? file->comp_suffix
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

    /* if -r or -h, write the header before compress or uncompress pipe */
    if(rawflag || headerflag) {
	char *cont_filename;

	/* remove CONT_FILENAME from header */
	cont_filename = stralloc(file->cont_filename);
	memset(file->cont_filename,'\0',sizeof(file->cont_filename));
	write_header(buffer,file,buflen);

	if (fullwrite(out, buffer, buflen) < 0)
	    error("write error: %s", strerror(errno));
	/* add CONT_FILENAME to header */
	strncpy(file->cont_filename, cont_filename, sizeof(file->cont_filename));
    }

    /* if -c and file not compressed, insert compress pipe */

    if(compflag && !file->compressed) {
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
	    execlp(COMPRESS_PATH, COMPRESS_PATH, (char *)0);
	    error("could not exec %s: %s", COMPRESS_PATH, strerror(errno));
	}
    }

    /* if not -r or -c, and file is compressed, insert uncompress pipe */

    else if(!rawflag && !compflag && file->compressed) {
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
	    error("%s: could not fork for %s: %s", get_pname(), 
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
	buflen = fullread(tapedev, buffer, sizeof(buffer));
	if(buflen == 0 && isafile) { /* switch to next file */
	    int save_tapedev;

	    save_tapedev = tapedev;
	    close(tapedev);
	    if(file->cont_filename[0] == '\0') break; /* no more file */
	    if((tapedev = open(file->cont_filename,O_RDONLY)) == -1) {
		error("can't open %s: %s",file->cont_filename,strerror(errno));
	    }
	    if(tapedev != save_tapedev) {
		if(dup2(tapedev, save_tapedev) == -1) {
		    error("can't dup2: %s",strerror(errno));
		}
		close(tapedev);
		tapedev = save_tapedev;
	    }
	    buflen=read_file_header(buffer, file, sizeof(buffer), tapedev);
/* should be validated */
	    
	    buflen = fullread(tapedev, buffer, sizeof(buffer));
	}
	if(buflen == 0 && !isafile) break; /* EOF */

	if(buflen < 0) break;

	if (fullwrite(out, buffer, buflen) < 0) {
	    if(got_sigpipe) {
		fprintf(stderr,"Error %d (%s) offset %d+%d, wrote %d\n",
			       errno, strerror(errno), wc, buflen, rc);
		fprintf(stderr,  
			"%s: pipe reader has quit in middle of file.\n",
			get_pname());
		fprintf(stderr,
			"%s: skipping ahead to start of next file, please wait...\n",
			get_pname());
		if(!isafile) {
		    if(tapefd_fsf(tapedev, 1) == -1) {
			error("fast-forward: %s", strerror(errno));
		    }
		}
	    } else {
		perror("amrestore: write error");
	    }
	    exit(2);
	}
	wc += buflen;
    } while (buflen > 0);
    if(buflen < 0)
	error("read error: %s", strerror(errno));
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
    error("Usage: amrestore [-r|-c] [-h] [-p] tapedev [host [disk [date ... ]]]");
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
    char *tapename;
    struct match_list {
	char *hostname;
	char *diskname;
	char *datestamp;
	struct match_list *next;
    } *match_list = NULL, *me;
    int found_match;
    int arg_state;
    amwait_t compress_status;
    int tapedev;
    int fd;

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
    while( (opt = getopt(argc, argv, "cd:rpkh")) != -1) {
	switch(opt) {
	case 'c': compflag = 1; break;
	case 'r': rawflag = 1; break;
	case 'p': pipeflag = 1; break;
	case 'h': headerflag = 1; break;
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
	fprintf(stderr, "%s: Must specify tape device\n", get_pname());
	usage();
    }
    else tapename = argv[optind++];

    if((tapedev = tape_open(tapename, 0)) < 0)
	error("could not open tape %s: %s", tapename, strerror(errno));

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

    if(stat(tapename,&stat_tape)!=0)
	error("could not stat %s",tapename);
    isafile=S_ISREG((stat_tape.st_mode));
    file_number = 0;
    buflen=read_file_header(buffer, &file, sizeof(buffer), tapedev);

    if(file.type != F_TAPESTART && !isafile)
	fprintf(stderr,
    "%s: WARNING: not at start of tape, file numbers will be offset\n",
    get_pname());

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
	if(found_match) {
	    fprintf(stderr, "%s: %3d: restoring %s\n", 
		    get_pname(), file_number, filename);
	    restore(&file, filename, tapedev, isafile);
	    if(pipeflag) break;
	      /* GH: close and reopen the tape device, so that the
		 read_file_header() below reads the next file on the
		 tape and does not report: short block... */
	    tapefd_close(tapedev);
	    /* DB: wait for (un)compress, otherwise
                   reopening the tape might fail */
	    if (compress_pid > 0) {
	      waitpid(compress_pid, &compress_status, 0);
	      compress_pid = -1;
	    }
	    if((tapedev = tape_open(tapename, 0)) < 0)
		error("could not open tape %s: %s", tapename, strerror(errno));
	}
	else {
	    fprintf(stderr, "%s: %3d: skipping ", get_pname(), file_number);
	    if(file.type != F_DUMPFILE) print_header(stderr,&file);
	    else fprintf(stderr, "%s\n", filename);
	    if(tapefd_fsf(tapedev, 1) == -1)
		error("fast-forward: %s", strerror(errno));
	}
	file_number += 1;
	if(isafile)
	    file.type = F_TAPEEND;
	else {
	    buflen=read_file_header(buffer, &file, sizeof(buffer), tapedev);
	}
    }
    tapefd_close(tapedev);

    if(file.type == F_TAPEEND && !isafile) {
	fprintf(stderr, "%s: %3d: reached ", get_pname(), file_number);
	print_header(stderr,&file);
	return 1;
    }
    return 0;
}
