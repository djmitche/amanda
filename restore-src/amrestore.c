/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1993 University of Maryland
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
 * $Id: amrestore.c,v 1.15 1998/01/02 01:05:29 jrj Exp $
 *
 * retrieves files from an amanda tape
 */
/*
 * usage: amrestore [-p] [-h] [-r|-c] tape-device [hostname [diskname]]
 *
 * Pulls all files from the tape that match the hostname and diskname regular
 * expressions.  For example, specifying "rz1" as the diskname matches "rz1a",
 * "rz1g" etc on the tape.
 *
 * Command line options:
 *	-p   put output on stdout
 *	-c   write compressed
 *	-r   raw, write file as is on tape (with header, possibly compressed)
 *	-h   write the header too
 */

#include "amanda.h"
#include "tapeio.h"
#include "fileheader.h"

#define CREAT_MODE	0640

char *pname = "amrestore";
char buffer[TAPE_BLOCK_BYTES];


int compflag, rawflag, pipeflag, headerflag;
int buflen, got_sigpipe, file_number;
pid_t compress_pid = -1;

/* local functions */

void errexit P((void));
void handle_sigpipe P((int sig));
int disk_match P((dumpfile_t *file, char *hostname, char *diskname));
char *make_filename P((dumpfile_t *file));
int read_file_header P((char *buffer, dumpfile_t *file,
			int buflen, int tapedev));
void restore P((dumpfile_t *file, string_t filename,
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

int disk_match(file, hostname, diskname)
dumpfile_t *file;
char *hostname, *diskname;

/*
 * Returns 1 if the current dump file matches the hostname and diskname
 * regular expressions given on the command line, 0 otherwise.  As a 
 * special case, empty regexs are considered equivalent to ".*": they 
 * match everything.
 */
{
    if(file->type != F_DUMPFILE) return 0;

    if(*hostname == '\0' || match(hostname, file->name)) {
	return (*diskname == '\0' || match(diskname, file->disk));
    }
    return 0;
}


char *make_filename(file)
dumpfile_t *file;
{
    char number[NUM_STR_SIZE];

    ap_snprintf(number, sizeof(number), "%d", file->dumplevel);
    return vstralloc(file->name,
		     ".",
		     sanitise_filename(file->disk), 
		     ".",
		     file->datestamp,
		     ".",
		     number,
		     NULL);
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
	error("error reading tape: %s", strerror(errno));
    }
    else if(bytes_read < buflen) {
	fprintf(stderr, "%s: short block %d bytes\n", pname, bytes_read);
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
    int l, n, s;

    /* adjust compression flag */

    if(!compflag && file->compressed && !known_compress_type(file)) {
	fprintf(stderr, 
		"%s: unknown compression suffix %s, can't uncompress\n",
		pname, file->comp_suffix);
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
	afree(filename_ext);
    }

    out = dest;

    /* if -r or -h, write the header before compress or uncompress pipe */
    if(rawflag || headerflag) {
	int l, s;

	for(l = 0; l < buflen; l += s) {
	    if((s = write(out, buffer + l, buflen - l)) < 0) {
		error("write error: %s", strerror(errno));
	    }
	}
    }

    /* if -c and file not compressed, insert compress pipe */

    if(compflag && !file->compressed) {
	pipe(outpipe);
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
	    if(dup2(outpipe[0], 0) == -1)
		error("error [dup2 pipe: %s]", strerror(errno));
	    if(dup2(dest, 1) == -1)
		error("error [dup2 dest: %s]", strerror(errno));
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
	    error("%s: could not fork for %s: %s", pname, 
		  UNCOMPRESS_PATH, strerror(errno));
	default:
	    aclose(outpipe[0]);
	    aclose(dest);
	    break;
	case 0:
	    aclose(outpipe[1]);
	    if(dup2(outpipe[0], 0) < 0)
		error("dup2 pipe: %s", strerror(errno));
	    if(dup2(dest, 1) < 0)
		error("dup2 dest: %s", strerror(errno));
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
    while((buflen = tapefd_read(tapedev, buffer, sizeof(buffer))) > 0) {
	for(l = 0; l < buflen; l += s) {
	    if((s = write(out, buffer + l, buflen - l)) < 0) {
		if(got_sigpipe) {
		    fprintf(stderr,"Error %d (%s) offset %d+%d, wrote %d\n",
				   errno, strerror(errno), wc, buflen, rc);
		    fprintf(stderr,  
			    "%s: pipe reader has quit in middle of file.\n",
			    pname);
		    fprintf(stderr,
			    "%s: skipping ahead to start of next file, please wait...\n",
			    pname);
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
	}
	wc += buflen;
    }
    if(buflen < 0)
	error("read error: %s", strerror(errno));
    aclose(out);
}


void usage()
/*
 * Print usage message and terminate.
 */
{
    error("Usage: amrestore [-r|-c] [-h] [-p] tapedev [host [disk]]");
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
    char *tapename, *hostname, *diskname;
    int compress_status;
    int tapedev;

    erroutput_type = ERR_INTERACTIVE;

    onerror(errexit);
    signal(SIGPIPE, handle_sigpipe);

    /* handle options */
    while( (opt = getopt(argc, argv, "crpkh")) != -1) {
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
	fprintf(stderr, "%s: Must specify tape device\n", pname);
	usage();
    }
    else tapename = argv[optind++];

    if((tapedev = tape_open(tapename, 0)) < 0)
	error("could not open tape %s: %s", tapename, strerror(errno));

    if(optind >= argc) hostname = "";
    else {
	hostname = argv[optind++];
	if((errstr=validate_regexp(hostname)) != NULL) {
	    fprintf(stderr, "%s: bad hostname regex \"%s\": %s\n",
		    pname, hostname, errstr);
	    usage();
	}
    }

    if(optind >= argc) diskname = "";
    else {
	diskname = argv[optind++];
	if((errstr=validate_regexp(diskname)) != NULL) {
	    fprintf(stderr, "%s: bad diskname regex \"%s\": %s\n",
		    pname, diskname, errstr);
	    usage();
	}
    }

    if(stat(tapename,&stat_tape)!=0)
	error("could not stat %s",tapename);
    isafile=S_ISREG((stat_tape.st_mode));
    file_number = 0;
    buflen=read_file_header(buffer, &file, sizeof(buffer), tapedev);

    if(file.type != F_TAPESTART && !isafile)
	fprintf(stderr,
    "%s: WARNING: not at start of tape, file numbers will be offset\n", pname);

    while(file.type == F_TAPESTART || file.type == F_DUMPFILE) {
	afree(filename);
	filename = make_filename(&file);
	if(disk_match(&file,hostname,diskname) != 0) {
	    fprintf(stderr, "%s: %3d: restoring %s\n", 
		    pname, file_number, filename);
	    restore(&file, filename, tapedev, isafile);
	    if(pipeflag) break;
	      /* GH: close and reopen the tape device, so that the
		 read_file_header() below reads the next file on the
		 tape an does not report: short block... */
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
	    fprintf(stderr, "%s: %3d: skipping ", pname, file_number);
	    if(file.type != F_DUMPFILE) print_header(stderr,&file);
	    else fprintf(stderr, "%s\n", filename);
	    if(tapefd_fsf(tapedev, 1) == -1)
		error("fast-forward: %s", strerror(errno));
	}
	file_number += 1;
	if(isafile)
	    file.type = F_TAPEEND;
	else
	    buflen=read_file_header(buffer, &file, sizeof(buffer), tapedev);
    }
    tapefd_close(tapedev);

    if(file.type == F_TAPEEND && !isafile) {
	fprintf(stderr, "%s: %3d: reached ", pname, file_number);
	print_header(stderr,&file);
	return 1;
    }
    return 0;
}
