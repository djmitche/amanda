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
 * amrestore.c  - retrieves files from an amanda tape
 *
 * usage: amrestore [-p] [-r|-c] tape-device [hostname [diskname]]
 *
 * Pulls all files from the tape that match the hostname and diskname regular
 * expressions.  For example, specifying "rz1" as the diskname matches "rz1a",
 * "rz1g" etc on the tape.
 *
 * Command line options:
 *	-p   put output on stdout
 *	-c   write compressed
 *	-r   raw, write file as is on tape (with header, possibly compressed)
 */

#include "amanda.h"
#include "regex.h"
#include "tapeio.h"

#define BLOCK_SIZE	32768
#define STRMAX		256
#define CREAT_MODE	0640

typedef char string_t[STRMAX];
typedef enum {
    F_UNKNOWN, F_WEIRD, F_TAPESTART, F_TAPEEND, F_DUMPFILE 
} filetype_t;

typedef struct file_s {
    filetype_t type;
    int datestamp;
    int dumplevel;
    int compressed;
    string_t comp_suffix;
    string_t name;	/* hostname or label */
    string_t disk;
} dumpfile_t;

char *pname = "amrestore";
dumpfile_t file;
string_t filename, line;
char buffer[BLOCK_SIZE];
char *tapename, *hostname, *diskname;

int compflag, rawflag, pipeflag;
int buflen, tapedev, got_sigpipe, file_number;

/* local functions */

void errexit P((void));
char *eatword P((char **linep));
char *diskname2filename P((char *dname));
void read_file_header P((void));
void print_header P((FILE *outf));
int disk_match P((void));
void handle_sigpipe P((int sig));
int known_compress_type P((void));
void restore P((void));
void usage P((void));
int main P((int argc, char **argv));

void errexit()
/*
 * Do exit(2) after an error, rather than exit(1).
 */
{
    exit(2);
}


char *eatword(linep)
char **linep;
/*
 * Given a pointer into a character string, eatword advances the pointer
 * past the next word (possibly preceeded by whitespace) in the string.
 * The first space after the word is set to '\0'.  A pointer to the start
 * of the word is returned.
 */
{
    char *str, *lp;

    lp = *linep;

    while(*lp && isspace(*lp)) lp++;	/* eat leading whitespace */

    str = lp;
    while(*lp && !isspace(*lp)) lp++;	/* eat word */

    if(*lp) {				/* zero char after word, if needed */
	*lp = '\0';
	lp++;
    }
    *linep = lp;
    return str;
}


/* XXX dup from driver.c */
char *diskname2filename(dname)
char *dname;
{
    static char filename[256];
    char *s, *d;

    for(s = dname, d = filename; *s != '\0'; s++, d++) {
	if(*s == '/') *d = '_';
	else *d = *s;
    }
    *d = '\0';
    return filename;
}


void read_file_header()
/*
 * Reads the first block of a tape file and parses the amanda header line
 * contained therein, setting up the file structure and filename string.
 */
{
    char *lp, *bp, *str;
    int nchars;

    buflen = tapefd_read(tapedev, buffer, BLOCK_SIZE);
    if(buflen < 0)
	error("error reading tape: %s", strerror(errno));

    else if(buflen < BLOCK_SIZE) {
	fprintf(stderr, "amrestore: short block %d bytes\n", buflen);
	  /* GH: no simple return, but set file.type to F_TAPEEND, so that
	     the loop in main() stops... */
	if (buflen == 0) {
	    file.type = F_TAPEEND;
	    return;
	}
    }

    /* isolate first line */

    nchars = buflen<sizeof(line)? buflen : sizeof(line) - 1;
    for(lp=line, bp=buffer; bp < buffer+nchars; bp++, lp++) {
	*lp = *bp;
	if(*bp == '\n') {
	    *lp = '\0';
	    break;
	}
    }
    line[STRMAX-1] = '\0';


    lp = line;
    str = eatword(&lp);
    if(strcmp(str, "NETDUMP:") && strcmp(str,"AMANDA:")) {
	file.type = F_UNKNOWN;
	return;
    }
    
    str = eatword(&lp);
    if(!strcmp(str, "TAPESTART")) {
	file.type = F_TAPESTART;
	eatword(&lp);				/* ignore "DATE" */
	file.datestamp = atoi(eatword(&lp));
	eatword(&lp);				/* ignore "TAPE" */
	strcpy(file.name, eatword(&lp));
    }
    else if(!strcmp(str, "FILE")) {
	file.type = F_DUMPFILE;
	file.datestamp = atoi(eatword(&lp));
	strcpy(file.name, eatword(&lp));
	strcpy(file.disk, eatword(&lp));
	eatword(&lp);				/* ignore "lev" */
	file.dumplevel = atoi(eatword(&lp));
	eatword(&lp);				/* ignore "comp" */
	strcpy(file.comp_suffix, eatword(&lp));
	file.compressed = strcmp(file.comp_suffix, "N");
	/* compatibility with pre-2.2 amanda */
	if(!strcmp(file.comp_suffix, "C"))
	    strcpy(file.comp_suffix, ".Z");

	sprintf(filename, "%s.%s.%d.%d", file.name, 
		diskname2filename(file.disk),
		file.datestamp, file.dumplevel);
    }
    else if(!strcmp(str, "TAPEEND")) {
	file.type = F_TAPEEND;
	eatword(&lp);				/* ignore "DATE" */
	file.datestamp = atoi(eatword(&lp));
    }
    else {
	fprintf(stderr, "amrestore: strange amanda header: \"%s\"\n", line);
	file.type = F_WEIRD;
	return;
    }
}

void print_header(outf)
FILE *outf;
/*
 * Prints the contents of the file structure.
 */
{
    switch(file.type) {
    case F_UNKNOWN:
	fprintf(outf, "UNKNOWN file\n");
	break;
    case F_WEIRD:
	fprintf(outf, "WEIRD file\n");
	break;
    case F_TAPESTART:
	fprintf(outf, "start of tape: date %d label %s\n",
	       file.datestamp, file.name);
	break;
    case F_DUMPFILE:
	fprintf(outf, "dumpfile: date %d host %s disk %s lev %d comp %s\n",
		file.datestamp, file.name, file.disk, file.dumplevel, 
		file.comp_suffix);
	break;
    case F_TAPEEND:
	fprintf(outf, "end of tape: date %d\n", file.datestamp);
	break;
    }
}


int disk_match()
/*
 * Returns 1 if the current dump file matches the hostname and diskname
 * regular expressions given on the command line, 0 otherwise.  As a 
 * special case, empty regexs are considered equivalent to ".*": they 
 * match everything.
 */
{
    if(file.type != F_DUMPFILE) return 0;

    if(*hostname == '\0' || match(hostname, file.name)) {
	return (*diskname == '\0' || match(diskname, file.disk));
    }
    return 0;
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

int known_compress_type()
{
    if(!strcmp(file.comp_suffix, ".Z"))
	return 1;
#ifdef HAVE_GZIP
    if(!strcmp(file.comp_suffix, ".gz"))
	return 1;
#endif
    return 0;
}


void restore()
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

    if(!compflag && file.compressed && !known_compress_type()) {
	fprintf(stderr, 
		"amrestore: unknown compression suffix %s, can't uncompress\n",
		file.comp_suffix);
	compflag = 1;
    }

    /* set up final destination file */

    if(pipeflag)
	dest = 1;		/* standard output */
    else {
	if(compflag) 
	    strcat(filename, 
		   file.compressed? file.comp_suffix : COMPRESS_SUFFIX);
	else if(rawflag) 
	    strcat(filename, ".RAW");

	if((dest = creat(filename, CREAT_MODE)) < 0)
	    error("could not create output file: %s", strerror(errno));
    }

    out = dest;

    /* if -c and file not compressed, insert compress pipe */

    if(compflag && !file.compressed) {
	pipe(outpipe);
	out = outpipe[1];
	switch(fork()) {
	case -1: error("could not fork for %s", COMPRESS_PATH,strerror(errno));
	default:
	    close(outpipe[0]);
	    close(dest);
	    break;
	case 0:
	    close(outpipe[1]);
	    if(dup2(outpipe[0], 0) == -1)
		error("error [dup2 pipe: %s]", strerror(errno));
	    if(dup2(dest, 1) == -1)
		error("error [dup2 dest: %s]", strerror(errno));
	    execlp(COMPRESS_PATH, COMPRESS_PATH, (char *)0);
	    error("could not exec %s: %s", COMPRESS_PATH, strerror(errno));
	}
    }

    /* if not -r or -c, and file is compressed, insert uncompress pipe */

    else if(!rawflag && !compflag && file.compressed) {
	/* 
	 * XXX for now we know that for the two compression types we
	 * understand, .Z and optionally .gz, UNCOMPRESS_PATH will take
	 * care of both.  Later, we may need to reference a table of
	 * possible uncompress programs.
	 */
	if(pipe(outpipe) < 0) error("error [pipe: %s]", strerror(errno));
	out = outpipe[1];
	switch(fork()) {
	case -1: 
	    error("amrestore: could not fork for %s: %s", 
		  UNCOMPRESS_PATH, strerror(errno));
	default:
	    close(outpipe[0]);
	    close(dest);
	    break;
	case 0:
	    close(outpipe[1]);
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

    /* if -r, write the header too */
    
    if(rawflag) {
	if((rc = write(out, buffer, buflen)) < buflen)
	    error("short write: %s", strerror(errno));
    }
    

    /* copy the rest of the file from tape to the output */
    got_sigpipe = 0;
    wc = 0;
    while((buflen = tapefd_read(tapedev, buffer, BLOCK_SIZE)) > 0) {
	if((rc = write(out, buffer, buflen)) < buflen) {
	    if(got_sigpipe) {
		fprintf(stderr,"Error %d offset %d+%d, wrote %d\n",
			errno, wc, buflen, rc);
		fprintf(stderr,  
		       "amrestore: pipe reader has quit in middle of file.\n");
		fprintf(stderr,
	"amrestore: skipping ahead to start of next file, please wait...\n");
		if(tapefd_fsf(tapedev, 1) == -1)
		    error("fast-forward: %s", strerror(errno));
	    }
	    else perror("amrestore: short write");

	    exit(2);
	}
	wc += buflen;
    }
    if(buflen < 0)
	error("read error: %s", strerror(errno));
    close(out);
}


void usage()
/*
 * Print usage message and terminate.
 */
{
    error("Usage: amrestore [-r|-c] [-p] tapedev [host [disk]]");
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
    int opt, last_match, this_match;
    char *errstr;

    erroutput_type = ERR_INTERACTIVE;

    onerror(errexit);
    signal(SIGPIPE, handle_sigpipe);

    /* handle options */
    while( (opt = getopt(argc, argv, "crpk")) != -1) {
	switch(opt) {
	case 'c': compflag = 1; break;
	case 'r': rawflag = 1; break;
	case 'p': pipeflag = 1; break;
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
	fprintf(stderr, "amrestore: Must specify tape device\n");
	usage();
    }
    else tapename = argv[optind++];
    
    if((tapedev = tape_open(tapename, 0)) < 0)
	error("could not open tape %s: %s", tapename, strerror(errno));

    if(optind >= argc) hostname = "";
    else {
	hostname = argv[optind++];
	if((errstr=re_comp(hostname)) != NULL) {
	    fprintf(stderr, "amrestore: bad hostname regex \"%s\": %s\n",
		    hostname, errstr);
	    usage();
	}
    }

    if(optind >= argc) diskname = "";
    else {
	diskname = argv[optind++];
	if((errstr=re_comp(diskname)) != NULL) {
	    fprintf(stderr, "amrestore: bad diskname regex \"%s\": %s\n",
		    diskname, errstr);
	    usage();
	}
    }

    last_match = 0;
    file_number = 0;
    read_file_header();

    if(file.type != F_TAPESTART)
	fprintf(stderr,
    "amrestore: WARNING: not at start of tape, file numbers will be offset\n");

    while(file.type != F_TAPEEND) {
	if((this_match = disk_match()) != 0) {
	    fprintf(stderr, "amrestore: %3d: restoring %s\n", 
		    file_number, filename);
	    restore();
	    if(pipeflag) break;
	      /* GH: close and reopen the tape device, so that the
		 read_file_header() below reads the next file on the
		 tape an does not report: short block... */
	    tapefd_close(tapedev);
	    if((tapedev = tape_open(tapename, 0)) < 0)
		error("could not open tape %s: %s", tapename, strerror(errno));
	}
	else {
	    fprintf(stderr, "amrestore: %3d: skipping ", file_number);
	    if(file.type != F_DUMPFILE) print_header(stderr);
	    else fprintf(stderr, "%s\n", filename);
	    if(tapefd_fsf(tapedev, 1) == -1)
		error("fast-forward: %s", strerror(errno));
	}
	last_match = this_match;
	file_number += 1;
	read_file_header();
    }
    tapefd_close(tapedev);

    if(file.type == F_TAPEEND) {
	fprintf(stderr, "amrestore: %3d: reached ", file_number);
	print_header(stderr);
	return 1;
    }
    return 0;
}
