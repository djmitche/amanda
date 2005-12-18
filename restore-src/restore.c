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
 * $Id: restore.c,v 1.8 2005/12/18 01:08:42 martinea Exp $
 *
 * retrieves files from an amanda tape
 */

#include "amanda.h"
#include "tapeio.h"
#include "util.h"
#include "restore.h"
#include "find.h"
#include "changer.h"
#include "logfile.h"
#include "fileheader.h"
#include <signal.h>

int file_number;

/* stuff we're stuck having global */
static long blocksize = -1;
static char *cur_tapedev = NULL;
static char *searchlabel = NULL;
static int backwards;
static int got_sigpipe;
static int exitassemble = 0;
static int tapefd, nslots;

char *rst_conf_logdir = NULL;
char *rst_conf_logfile = NULL;
char *curslot = NULL;

typedef struct open_output_s {
    struct open_output_s *next;
    dumpfile_t *file;
    int lastpartnum;
    pid_t comp_enc_pid;
    int outfd;
} open_output_t;


typedef struct dumplist_s {
    struct dumplist_s *next;
    dumpfile_t *file;
} dumplist_t;

static open_output_t *open_outputs = NULL;
static dumplist_t *alldumps_list = NULL;

static ssize_t get_block P((int tapefd, char *buffer, int isafile));
static void append_file_to_fd P((char *filename, int fd));
static int headers_equal P((dumpfile_t *file1, dumpfile_t *file2, int ignore_partnums));
static int already_have_dump P((dumpfile_t *file));

/* local functions */

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

static void handle_sigint(sig)
int sig;
/*
 * We might want to flush any open dumps and unmerged splits before exiting
 * on SIGINT, so do so.
 */
{
    flush_open_outputs(exitassemble, NULL);
    if(rst_conf_logfile) unlink(rst_conf_logfile);
    exit(0);
}

int lock_logfile()
{
    rst_conf_logdir = getconf_str(CNF_LOGDIR);
    if (*rst_conf_logdir == '/') {
	rst_conf_logdir = stralloc(rst_conf_logdir);
    } else {
	rst_conf_logdir = stralloc2(config_dir, rst_conf_logdir);
    }
    rst_conf_logfile = vstralloc(rst_conf_logdir, "/log", NULL);
    if (access(rst_conf_logfile, F_OK) == 0) {
	error("%s exists: amdump or amflush is already running, or you must run amcleanup", rst_conf_logfile);
    }
    log_add(L_INFO, get_pname());
    return 1;
}

/*
 * Return 1 if the two fileheaders match in name, disk, type, split chunk part
 * number, and datestamp, and 0 if not.  The part number can be optionally
 * ignored.
 */
int headers_equal (file1, file2, ignore_partnums)
dumpfile_t *file1, *file2;
int ignore_partnums;
{
    if(!file1 || !file2) return(0);
    
    if(file1->dumplevel == file2->dumplevel &&
	   file1->type == file2->type &&
	   !strcmp(file1->datestamp, file2->datestamp) &&
	   !strcmp(file1->name, file2->name) &&
	   !strcmp(file1->disk, file2->disk) &&
	   (ignore_partnums || file1->partnum == file2->partnum)){
	return(1);
    }
    return(0);
}


/*
 * See whether we're already pulled an exact copy of the given file (chunk
 * number and all).  Returns 0 if not, 1 if so.
 */
int already_have_dump(file)
dumpfile_t *file;
{
    dumplist_t *fileentry = NULL;

    if(!file) return(0);
    for(fileentry=alldumps_list;fileentry;fileentry=fileentry->next){
	if(headers_equal(file, fileentry->file, 0)) return(1);
    }
    return(0);
}

/*
 * Open the named file and append its contents to the (hopefully open) file
 * descriptor supplies.
 */
static void append_file_to_fd(filename, fd)
char *filename;
int fd;
{
    ssize_t bytes_read;
    int l, s, wc = 0, rc = 0;
    char *buffer;

    if(blocksize == -1)
	blocksize = DISK_BLOCK_BYTES;
    buffer = alloc(blocksize);

    if((tapefd = open(filename, O_RDONLY)) == -1) {
	error("can't open %s: %s", filename, strerror(errno));
    }

    do {
	bytes_read = get_block(tapefd, buffer, 1); /* same as isafile = 1 */
	if(bytes_read < 0) {
	    error("read error: %s", strerror(errno));
	}
	for(l = 0; l < bytes_read; l += s) {
	    if((s = write(fd, buffer + l, bytes_read - l)) < 0) {
		if(got_sigpipe) {
		    fprintf(stderr,"Error %d (%s) offset %d+%ld, wrote %d\n",
				   errno, strerror(errno), wc, (long)bytes_read, rc);
		    fprintf(stderr,  
			    "%s: pipe reader has quit in middle of file.\n",
			    get_pname());
		} else {
		    perror("restore: write error");
		}
		exit(2);
	    }
	}
	wc += bytes_read;
    } while (bytes_read > 0);

    amfree(buffer);
    aclose(tapefd);
}

/*
 * Tape changer support routines, stolen brazenly from amtape
 */
int scan_init(rc, ns, bk)
int rc, ns, bk;
{
    if(rc)
        error("could not get changer info: %s", changer_resultstr);

    nslots = ns;
    backwards = bk;

    return 0;
}
int loadlabel_slot(rc, slotstr, device)
int rc;
char *slotstr;
char *device;
{
    char *errstr;
    char *datestamp = NULL;
    char *label = NULL;


    if(rc > 1)
        error("could not load slot %s: %s", slotstr, changer_resultstr);
    else if(rc == 1)
        fprintf(stderr, "%s: slot %s: %s\n",
                get_pname(), slotstr, changer_resultstr);
    else if((errstr = tape_rdlabel(device, &datestamp, &label)) != NULL)
        fprintf(stderr, "%s: slot %s: %s\n", get_pname(), slotstr, errstr);
    else {
        fprintf(stderr, "%s: slot %s: date %-8s label %s",
                get_pname(), slotstr, datestamp, label);
        if(strcmp(label, FAKE_LABEL) != 0
           && strcmp(label, searchlabel) != 0)
            fprintf(stderr, " (wrong tape)\n");
        else {
            fprintf(stderr, " (exact label match)\n");
            if((errstr = tape_rewind(device)) != NULL) {
                fprintf(stderr,
                        "%s: could not rewind %s: %s",
                        get_pname(), device, errstr);
                amfree(errstr);
            }
	    if(cur_tapedev) amfree(cur_tapedev);
	    curslot = stralloc(slotstr);
	    if(!device) return(1);
	    cur_tapedev = stralloc(device);

            amfree(datestamp);
            amfree(label);
            return 1;
        }
    }
    amfree(datestamp);
    amfree(label);

    if(cur_tapedev) amfree(cur_tapedev);
    curslot = stralloc(slotstr);
    if(!device) return(1);
    cur_tapedev = stralloc(device);

    return 0;
}


/* non-local functions follow */



/*
 * Check whether we've read all of the preceding parts of a given split dump,
 * generally used to see if we're done and can close the thing.
 */
int have_all_parts (file, upto)
dumpfile_t *file;
int upto;
{
    int c;
    int *foundparts = NULL;
    dumplist_t *fileentry = NULL;

    if(!file || file->partnum < 1) return(0);

    if(upto < 1) upto = file->totalparts;

    foundparts = alloc(sizeof(int) * upto); 
    for(c = 0 ; c< upto; c++) foundparts[c] = 0;
    
    for(fileentry=alldumps_list;fileentry; fileentry=fileentry->next){
	dumpfile_t *cur_file = fileentry->file;
	if(headers_equal(file, cur_file, 1)){
	    if(cur_file->partnum > upto){
		amfree(foundparts);
		return(0);
	    }

	    foundparts[cur_file->partnum - 1] = 1;
	}
    }

    for(c = 0 ; c< upto; c++){
	if(!foundparts[c]){
	    amfree(foundparts);
	    return(0);
	}
    }
    
    amfree(foundparts);
    return(1);
}

/*
 * Free up the open filehandles and memory we were using to track in-progress
 * dumpfiles (generally for split ones we're putting back together).  If
 * applicable, also find the ones that are continuations of one another and
 * string them together.  If given an optional file header argument, flush
 * only that dump and do not flush/free any others.
 */
void flush_open_outputs(reassemble, only_file)
int reassemble;
dumpfile_t *only_file;
{
    open_output_t *cur_out = NULL, *prev = NULL;
    find_result_t *sorted_files = NULL;
    amwait_t compress_status;

    if(!only_file){
	fprintf(stderr, "\n");
    }

    /*
     * Deal with any split dumps we've been working on, appending pieces
     * that haven't yet been appended and closing filehandles we've been
     * holding onto.
     */
    if(reassemble){
	find_result_t *cur_find_res = NULL;
	int outfd = -1, lastpartnum = -1;
	dumpfile_t *main_file = NULL;
	cur_out = open_outputs;
	
	/* stick the dumpfile_t's into a list find_result_t's so that we can
	   abuse existing sort functionality */
	for(cur_out=open_outputs; cur_out; cur_out=cur_out->next){
	    find_result_t *cur_find_res = NULL;
	    dumpfile_t *cur_file = cur_out->file;
	    /* if we requested a particular file, do only that one */
	    if(only_file && !headers_equal(cur_file, only_file, 1)){
		continue;
	    }
	    cur_find_res = alloc(sizeof(find_result_t));
	    memset(cur_find_res, '\0', sizeof(find_result_t));
	    cur_find_res->datestamp = atoi(cur_file->datestamp);
	    cur_find_res->hostname = stralloc(cur_file->name);
	    cur_find_res->diskname = stralloc(cur_file->disk);
	    cur_find_res->level = cur_file->dumplevel;
	    if(cur_file->partnum < 1) cur_find_res->partnum = stralloc("--");
	    else{
		char part_str[NUM_STR_SIZE];
		snprintf(part_str, sizeof(part_str), "%d", cur_file->partnum);
		cur_find_res->partnum = stralloc(part_str);
	    }
	    cur_find_res->user_ptr = (void*)cur_out;

	    cur_find_res->next = sorted_files;
	    sorted_files = cur_find_res;
	}
	sort_find_result("hkdlp", &sorted_files);

	/* now we have an in-order list of the files we need to concatenate */
	cur_find_res = sorted_files;
	for(cur_find_res=sorted_files;
		cur_find_res;
		cur_find_res=cur_find_res->next){
	    dumpfile_t *cur_file = NULL;
	    cur_out = (open_output_t*)cur_find_res->user_ptr;
	    cur_file = cur_out->file;

	    /* if we requested a particular file, do only that one */
	    if(only_file && !headers_equal(cur_file, only_file, 1)){
		continue;
	    }

	    if(cur_file->type == F_SPLIT_DUMPFILE){
		/* is it a continuation of one we've been writing? */
		if(main_file && cur_file->partnum > lastpartnum &&
			headers_equal(cur_file, main_file, 1)){

		    /* effectively changing filehandles */
		    aclose(cur_out->outfd);
		    cur_out->outfd = outfd;

		    fprintf(stderr, "Merging %s with %s\n",
		            make_filename(cur_file), make_filename(main_file));
		    append_file_to_fd(make_filename(cur_file), outfd);
		    if(unlink(make_filename(cur_file)) < 0){
			fprintf(stderr, "Failed to unlink %s: %s\n",
			             make_filename(cur_file), strerror(errno));
		    }
		}
		/* or a new file? */
		else{
		    if(outfd >= 0) aclose(outfd);
		    if(main_file) amfree(main_file);
		    main_file = alloc(sizeof(dumpfile_t));
		    memcpy(main_file, cur_file, sizeof(dumpfile_t));
		    outfd = cur_out->outfd;
		    if(outfd < 0){
			if((outfd = open(make_filename(cur_file), O_RDWR|O_APPEND)) < 0){
			  error("Couldn't open %s for appending: %s\n",
			        make_filename(cur_file), strerror(errno));
			}
		    }
		}
		lastpartnum = cur_file->partnum;
	    }
	    else aclose(cur_out->outfd);
	}
	if(outfd >= 0) aclose(outfd);

	free_find_result(&sorted_files);
    }

    /*
     * Now that the split dump closure is done, free up resources we don't
     * need anymore.
     */
    for(cur_out=open_outputs; cur_out; cur_out=cur_out->next){
	dumpfile_t *cur_file = NULL;
	if(prev) amfree(prev);
	cur_file = cur_out->file;
	/* if we requested a particular file, do only that one */
	if(only_file && !headers_equal(cur_file, only_file, 1)){
	    continue;
	}
	if(!reassemble) aclose(cur_out->outfd);
	if(cur_out->comp_enc_pid > 0){
	    waitpid(cur_out->comp_enc_pid, &compress_status, 0);
	}
	amfree(cur_out->file);
	prev = cur_out;
    }

    open_outputs = NULL;
}

/*
 * Turn a fileheader into a string suited for use on the filesystem.
 */
char *make_filename(file)
dumpfile_t *file;
{
    char number[NUM_STR_SIZE];
    char part[NUM_STR_SIZE];
    char totalparts[NUM_STR_SIZE];
    char *sfn = NULL;
    char *fn = NULL;
    char *pad = NULL;
    int padlen = 0;

    snprintf(number, sizeof(number), "%d", file->dumplevel);
    snprintf(part, sizeof(part), "%d", file->partnum);

    if(file->totalparts < 0){
	snprintf(totalparts, sizeof(totalparts), "UNKNOWN");
    }
    else{
	snprintf(totalparts, sizeof(totalparts), "%d", file->totalparts);
    }
    padlen = strlen(totalparts) + 1 - strlen(part);
    pad = alloc(padlen);
    memset(pad, '0', padlen);
    pad[padlen - 1] = '\0';

    snprintf(part, sizeof(part), "%s%d", pad, file->partnum);

    sfn = sanitise_filename(file->disk);
    fn = vstralloc(file->name,
		   ".",
		   sfn, 
		   ".",
		   file->datestamp,
		   ".",
		   number,
		   NULL);
    if(file->partnum > 0){
	fn = vstralloc(fn, ".", part, NULL);
    }
    amfree(sfn);
    amfree(pad);
    return fn;
}


/*
XXX Making this thing a lib functiong broke a lot of assumptions everywhere,
but I think I've found them all.  Maybe.  Damn globals all over the place.
*/
static ssize_t get_block(tapefd, buffer, isafile)
int tapefd, isafile;
char *buffer;
{
    if(isafile)
	return (fullread(tapefd, buffer, blocksize));

    return(tapefd_read(tapefd, buffer, blocksize));
}

int disk_match(file, datestamp, hostname, diskname, level)
dumpfile_t *file;
char *datestamp, *hostname, *diskname, *level;
/*
 * Returns 1 if the current dump file matches the hostname and diskname
 * regular expressions given on the command line, 0 otherwise.  As a 
 * special case, empty regexs are considered equivalent to ".*": they 
 * match everything.
 */
{
    char level_str[NUM_STR_SIZE];
    snprintf(level_str, sizeof(level_str), "%d", file->dumplevel);

    if(file->type != F_DUMPFILE && file->type != F_SPLIT_DUMPFILE) return 0;

    if((*hostname == '\0' || match_host(hostname, file->name)) &&
       (*diskname == '\0' || match_disk(diskname, file->disk)) &&
       (*datestamp == '\0' || match_datestamp(datestamp, file->datestamp)) &&
       (*level == '\0' || match_level(level, level_str)))
	return 1;
    else
	return 0;
}


ssize_t read_file_header(file, tapefd, isafile, flags)
dumpfile_t *file;
int isafile;
int tapefd;
rst_flags_t *flags;
/*
 * Reads the first block of a tape file.
 */
{
    ssize_t bytes_read;
    char *buffer;
  
    if(flags->blocksize > 0)
	blocksize = flags->blocksize;
    else if(blocksize == -1)
	blocksize = DISK_BLOCK_BYTES;
    buffer = alloc(blocksize);

    bytes_read = get_block(tapefd, buffer, isafile);
    if(bytes_read < 0) {
	error("error reading file header: %s", strerror(errno));
    } else if(bytes_read < blocksize) {
	if(bytes_read == 0) {
	    fprintf(stderr, "%s: missing file header block\n", get_pname());
	} else {
	    fprintf(stderr, "%s: short file header block: %ld byte%s\n",
		    get_pname(), (long)bytes_read, (bytes_read == 1) ? "" : "s");
	}
	file->type = F_UNKNOWN;
    } else {
	parse_file_header(buffer, file, bytes_read);
    }
    amfree(buffer);
    return bytes_read;
}


void drain_file(tapefd, flags)
int tapefd;
rst_flags_t *flags;
{
    ssize_t bytes_read;
    char *buffer;

    if(flags->blocksize)
	blocksize = flags->blocksize;
    else if(blocksize == -1)
	blocksize = DISK_BLOCK_BYTES;
    buffer = alloc(blocksize);

    do {
       bytes_read = get_block(tapefd, buffer, 0);
       if(bytes_read < 0) {
           error("read error: %s", strerror(errno));
       }
    } while (bytes_read > 0);

    amfree(buffer);
}

ssize_t restore(file, filename, tapefd, isafile, flags)
dumpfile_t *file;
char *filename;
int tapefd;
int isafile;
rst_flags_t *flags;
/*
 * Restore the current file from tape.  Depending on the settings of
 * the command line flags, the file might need to be compressed or
 * uncompressed.  If so, a pipe through compress or uncompress is set
 * up.  The final output usually goes to a file named host.disk.date.lev,
 * but with the -p flag the output goes to stdout (and presumably is
 * piped to restore).
 */
{
    int rc = 0, dest = -1, out;
    int wc;
    int l, s;
    int file_is_compressed;
    int is_continuation = 0;
    int check_for_aborted = 0;
    char *tmp_filename = NULL, *final_filename = NULL;
    struct stat statinfo;
    open_output_t *myout = NULL, *oldout = NULL;
    dumplist_t *tempdump = NULL, *fileentry = NULL;
    struct sigaction act, oact;
    char *buffer;
    int need_compress=0, need_uncompress=0, need_decrypt=0;
    int stage = 0;
    ssize_t bytes_read;
    struct pipeline {
        int	pipe[2];
    } pipes[3];

    if(flags->blocksize)
	blocksize = flags->blocksize;
    else if(blocksize == -1)
	blocksize = DISK_BLOCK_BYTES;

    /* set our sigpipe handler, saving the old one */
    act.sa_handler = handle_sigpipe;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if(sigaction(SIGPIPE, &act, &oact) != 0){
	error("error setting SIGPIPE handler: %s", strerror(errno));
    }

    if(already_have_dump(file)){
	fprintf(stderr, " *** Duplicate file %s, one is probably an aborted write\n", make_filename(file));
	check_for_aborted = 1;
    }

    /* store a shorthand record of this dump */
    tempdump = alloc(sizeof(dumplist_t));
    tempdump->file = alloc(sizeof(dumpfile_t));
    tempdump->next = NULL;
    memcpy(tempdump->file, file, sizeof(dumpfile_t));

    /*
     * If we're appending chunked files to one another, and if this is a
     * continuation of a file we just restored, and we've still got the
     * output handle from that previous restore, we're golden.  Phew.
     */
    if(flags->inline_assemble && file->type == F_SPLIT_DUMPFILE){
	myout = open_outputs;
	while(myout != NULL){
	    if(myout->file->type == F_SPLIT_DUMPFILE &&
		    headers_equal(file, myout->file, 1)){
		if(file->partnum == myout->lastpartnum + 1){
		    is_continuation = 1;
		    break;
		}
	    }
	    myout = myout->next;
	}
	if(myout != NULL) myout->lastpartnum = file->partnum;
	else if(file->partnum != 1){
	    fprintf(stderr, "%s:      Chunk out of order, will save to disk and append to output.\n", get_pname());
	    flags->pipe_to_fd = -1;
	    flags->compress = 0;
	    flags->leave_comp = 1;
	}
	if(myout == NULL){
	    myout = alloc(sizeof(open_output_t));
	    memset(myout, 0, sizeof(open_output_t));
	}
    }
    else{
      myout = alloc(sizeof(open_output_t));
      memset(myout, 0, sizeof(open_output_t));
    }


    if(is_continuation && flags->pipe_to_fd == -1){
	fprintf(stderr, "%s:      appending to %s\n", get_pname(),
		    make_filename(myout->file));
    }

    /* adjust compression flag */
    file_is_compressed = file->compressed;
    if(!flags->compress && file_is_compressed && !known_compress_type(file)) {
	fprintf(stderr, 
		"%s: unknown compression suffix %s, can't uncompress\n",
		get_pname(), file->comp_suffix);
	flags->compress = 1;
    }

    /* set up final destination file */

    if(is_continuation && myout != NULL) {
      out = myout->outfd;
    } else {
      if(flags->pipe_to_fd != -1) {
  	  dest = flags->pipe_to_fd;	/* standard output */
      } else {
  	  char *filename_ext = NULL;
  
  	  if(flags->compress) {
  	      filename_ext = file_is_compressed ? file->comp_suffix
  	  				      : COMPRESS_SUFFIX;
  	  } else if(flags->raw) {
  	      filename_ext = ".RAW";
  	  } else {
  	      filename_ext = "";
  	  }
  	  filename_ext = stralloc2(filename, filename_ext);
	  tmp_filename = stralloc(filename_ext); 
	  if(flags->restore_dir != NULL)
	      tmp_filename = vstralloc(flags->restore_dir, "/",
	                               tmp_filename, NULL);
	   
	  final_filename = stralloc(tmp_filename); 
	  tmp_filename = vstralloc(tmp_filename, ".tmp", NULL);
  	  if((dest = creat(tmp_filename, CREAT_MODE)) < 0)
  	      error("could not create output file %s: %s",
	                                       tmp_filename, strerror(errno));
  	  amfree(filename_ext);
      }
  
      out = dest;
    }

    /*
     * If -r or -h, write the header before compress or uncompress pipe.
     * Only write DISK_BLOCK_BYTES, regardless of how much was read.
     * This makes the output look like a holding disk image, and also
     * makes it easier to remove the header (e.g. in amrecover) since
     * it has a fixed size.
     */
    if(flags->raw || (flags->headers && !is_continuation)) {
	int w;
	char *cont_filename;
	dumpfile_t tmp_hdr;

	if(flags->compress && !file_is_compressed) {
	    file->compressed = 1;
	    snprintf(file->uncompress_cmd, sizeof(file->uncompress_cmd),
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

	memcpy(&tmp_hdr, file, sizeof(dumpfile_t));

	/* remove CONT_FILENAME from header */
	cont_filename = stralloc(file->cont_filename);
	memset(file->cont_filename,'\0',sizeof(file->cont_filename));
	file->blocksize = DISK_BLOCK_BYTES;

	/*
	 * Dumb down split file headers as well, so that older versions of
	 * things like amrecover won't gag on them.
	 */
	if(file->type == F_SPLIT_DUMPFILE && flags->mask_splits){
	    file->type = F_DUMPFILE;
	}

	buffer = alloc(DISK_BLOCK_BYTES);
	build_header(buffer, file, DISK_BLOCK_BYTES);

	if((w = fullwrite(out, buffer, DISK_BLOCK_BYTES)) != DISK_BLOCK_BYTES) {
	    if(w < 0) {
		error("write error: %s", strerror(errno));
	    } else {
		error("write error: %d instead of %d", w, DISK_BLOCK_BYTES);
	    }
	}
	amfree(buffer);
	/* add CONT_FILENAME to header */
#if 0
//	strncpy(file->cont_filename, cont_filename, sizeof(file->cont_filename));
#endif
	memcpy(file, &tmp_hdr, sizeof(dumpfile_t));
    }
 
    /* find out if compression or uncompression is needed here */
    if(flags->compress && !file_is_compressed && !is_continuation
	  && !flags->leave_comp
	  && (flags->inline_assemble || file->type != F_SPLIT_DUMPFILE))
       need_compress=1;
       
    if(!flags->raw && !flags->compress && file_is_compressed
	  && !is_continuation && !flags->leave_comp && (flags->inline_assemble
	  || file->type != F_SPLIT_DUMPFILE))
       need_uncompress=1;   

    if(!flags->raw && file->encrypted)
       need_decrypt=1;
   
    /* Setup pipes for decryption / compression / uncompression  */
    stage = 0;
    if (need_decrypt) {
      if (pipe(&pipes[stage].pipe[0]) < 0) 
        error("error [pipe[%d]: %s]", stage, strerror(errno));
      stage++;
    }

    if (need_compress || need_uncompress) {
      if (pipe(&pipes[stage].pipe[0]) < 0) 
        error("error [pipe[%d]: %s]", stage, strerror(errno));
      stage++;
    }
    pipes[stage].pipe[0] = -1; 
    pipes[stage].pipe[1] = out; 

    stage = 0;

    /* decrypt first if it's encrypted and no -r */
    if(need_decrypt) {
      switch(myout->comp_enc_pid = fork()) {
      case -1:
	error("could not fork for decrypt: %s", strerror(errno));
      default:
	aclose(pipes[stage].pipe[0]);
	aclose(pipes[stage+1].pipe[1]);
        stage++;
	break;
      case 0:
	if(dup2(pipes[stage].pipe[0], 0) == -1)
	    error("error decrypt stdin [dup2 %d %d: %s]", stage,
	        pipes[stage].pipe[0], strerror(errno));

	if(dup2(pipes[stage+1].pipe[1], 1) == -1)
	    error("error decrypt stdout [dup2 %d %d: %s]", stage + 1,
	        pipes[stage+1].pipe[1], strerror(errno));

	safe_fd(-1, 0);
	if (*file->srv_encrypt) {
	  (void) execlp(file->srv_encrypt, file->srv_encrypt,
			file->srv_decrypt_opt, NULL);
	  error("could not exec %s: %s", file->srv_encrypt, strerror(errno));
	}  else if (*file->clnt_encrypt) {
	  (void) execlp(file->clnt_encrypt, file->clnt_encrypt,
			file->clnt_decrypt_opt, NULL);
	  error("could not exec %s: %s", file->clnt_encrypt, strerror(errno));
	}
      }
    }

    if (need_compress) {
        /*
         * Insert a compress pipe
         */
	switch(myout->comp_enc_pid = fork()) {
	case -1: error("could not fork for %s: %s",
		       COMPRESS_PATH, strerror(errno));
	default:
	    aclose(pipes[stage].pipe[0]);
	    aclose(pipes[stage+1].pipe[1]);
            stage++;
	    break;
	case 0:
	    if(dup2(pipes[stage].pipe[0], 0) == -1)
		error("error compress stdin [dup2 %d %d: %s]", stage,
		  pipes[stage].pipe[0], strerror(errno));

	    if(dup2(pipes[stage+1].pipe[1], 1) == -1)
		error("error compress stdout [dup2 %d %d: %s]", stage + 1,
		  pipes[stage+1].pipe[1], strerror(errno));

	    if (*flags->comp_type == '\0') {
		flags->comp_type = NULL;
	    }

	    safe_fd(-1, 0);
	    (void) execlp(COMPRESS_PATH, COMPRESS_PATH, flags->comp_type, (char *)0);
	    error("could not exec %s: %s", COMPRESS_PATH, strerror(errno));
	}
    } else if(need_uncompress) {
        /*
         * If not -r, -c, -l, and file is compressed, and split reassembly 
         * options are sane, insert uncompress pipe
         */

	/* 
	 * XXX for now we know that for the two compression types we
	 * understand, .Z and optionally .gz, UNCOMPRESS_PATH will take
	 * care of both.  Later, we may need to reference a table of
	 * possible uncompress programs.
	 */ 
	switch(myout->comp_enc_pid = fork()) {
	case -1: 
	    error("could not fork for %s: %s",
		  UNCOMPRESS_PATH, strerror(errno));
	default:
	    aclose(pipes[stage].pipe[0]);
	    aclose(pipes[stage+1].pipe[1]);
            stage++;
	    break;
	case 0:
	    if(dup2(pipes[stage].pipe[0], 0) == -1)
		error("error uncompress stdin [dup2 %d %d: %s]", stage,
		  pipes[stage].pipe[0], strerror(errno));

	    if(dup2(pipes[stage+1].pipe[1], 1) == -1)
		error("error uncompress stdout [dup2 %d %d: %s]", stage + 1,
		  pipes[stage+1].pipe[1], strerror(errno));

	    safe_fd(-1, 0);
	    if (*file->srvcompprog) {
	      (void) execlp(file->srvcompprog, file->srvcompprog, "-d", NULL);
	      error("could not exec %s: %s", file->srvcompprog, strerror(errno));
	    } else if (*file->clntcompprog) {
	      (void) execlp(file->clntcompprog, file->clntcompprog, "-d", NULL);
	      error("could not exec %s: %s", file->clntcompprog, strerror(errno));
	    } else {
	      (void) execlp(UNCOMPRESS_PATH, UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
			  UNCOMPRESS_OPT,
#endif
			  (char *)0);
	      error("could not exec %s: %s", UNCOMPRESS_PATH, strerror(errno));
	    }
	}
    }

    /* copy the rest of the file from tape to the output */
    got_sigpipe = 0;
    wc = 0;
    if(flags->blocksize > 0)
	blocksize = flags->blocksize;
    else if(blocksize == -1)
	blocksize = DISK_BLOCK_BYTES;
    buffer = alloc(blocksize);

    do {
	bytes_read = get_block(tapefd, buffer, isafile);
	if(bytes_read < 0) {
	    error("read error: %s", strerror(errno));
	}

	if(bytes_read == 0 && isafile) {
	    /*
	     * See if we need to switch to the next file in a holding restore
	     */
	    if(file->cont_filename[0] == '\0') {
		break;				/* no more files */
	    }
	    close(tapefd);
	    if((tapefd = open(file->cont_filename, O_RDONLY)) == -1) {
		char *cont_filename = strrchr(file->cont_filename,'/');
		if(cont_filename) {
		    cont_filename++;
		    if((tapefd = open(cont_filename,O_RDONLY)) == -1) {
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
	    bytes_read = read_file_header(file, tapefd, isafile, flags);
	    if(file->type != F_DUMPFILE && file->type != F_CONT_DUMPFILE
		    && file->type != F_SPLIT_DUMPFILE) {
		fprintf(stderr, "unexpected header type: ");
		print_header(stderr, file);
		exit(2);
	    }
	    continue;
	}

	for(l = 0; l < bytes_read; l += s) {
	    if((s = write(pipes[0].pipe[1], buffer+l, bytes_read-l)) < 0) {
		if(got_sigpipe) {
		    fprintf(stderr,"Error %d (%s) offset %d+%ld, wrote %d\n",
				   errno, strerror(errno), wc, (long)bytes_read, rc);
		    fprintf(stderr,  
			    "%s: pipe reader has quit in middle of file.\n",
			    get_pname());
		} else {
		    perror("restore: write error");
		}
		exit(2);
	    }
	}
	wc += bytes_read;
    } while (bytes_read > 0);
    aclose(pipes[0].pipe[1]);

    amfree(buffer);

    if(!flags->inline_assemble){
       if(out != dest) aclose(out);
    }
    if(!is_continuation){
	if(tmp_filename && stat(tmp_filename, &statinfo) < 0){
	    error("Can't stat the file I just created (%s)!\n", tmp_filename);
	}
	if(check_for_aborted){
	    char *old_dump = final_filename;
	    struct stat oldstat;
	    if(stat(old_dump, &oldstat) >= 0){
		if(oldstat.st_size <= statinfo.st_size){
		    dumplist_t *prev_fileentry = NULL;
		    open_output_t *prev_out = NULL;
		    fprintf(stderr, "Newer restore is larger, using that\n");
		    /* nuke the old dump's entry in alldump_list */
		    for(fileentry=alldumps_list;
			    fileentry->next;
			    fileentry=fileentry->next){
			if(headers_equal(file, fileentry->file, 0)){
			    if(prev_fileentry){
				prev_fileentry->next = fileentry->next;
			    }
			    else alldumps_list = fileentry->next;
			    amfree(fileentry);
			    break;
			}
			prev_fileentry = fileentry;
		    }
		    myout = open_outputs;
		    while(myout != NULL){
			if(headers_equal(file, myout->file, 0)){
			    if(myout->outfd >= 0) aclose(myout->outfd);
			    if(prev_out){
				prev_out->next = myout->next;
			    }
			    else open_outputs = myout->next;
			    amfree(myout);
			    break;
			}
			prev_out = myout;
			myout = myout->next;
		    }
		}
		else{
		    fprintf(stderr, "Older restore is larger, using that\n");
		    unlink(tmp_filename);
		    amfree(tempdump->file);
		    amfree(tempdump);
		    amfree(tmp_filename);
		    amfree(final_filename);
		    /* put sigpipe back the way we found it */
		    if(sigaction(SIGPIPE, &oact, &act) != 0){
			error("error resetting SIGPIPE handler: %s",
				strerror(errno));
		    }
                    return (bytes_read);
		}
	    }
	}
	if(tmp_filename && final_filename &&
		rename(tmp_filename, final_filename) < 0){
	    error("Can't rename %s to %s: %s\n", tmp_filename, final_filename,
					     strerror(errno));
	}
    }
    if(tmp_filename) amfree(tmp_filename);
    if(final_filename) amfree(final_filename);


    /*
     * actually insert tracking data for this file into our various
     * structures (we waited in case we needed to give up)
     */
    if(!is_continuation){
        oldout = alloc(sizeof(open_output_t));
        oldout->file = alloc(sizeof(dumpfile_t));
        memcpy(oldout->file, file, sizeof(dumpfile_t));
        if(flags->inline_assemble) oldout->outfd = out; 
	else oldout->outfd = -1;
        oldout->comp_enc_pid = -1;
        oldout->lastpartnum = file->partnum;
        oldout->next = open_outputs;
        open_outputs = oldout;
    }
    if(alldumps_list){
	for(fileentry=alldumps_list;fileentry->next;fileentry=fileentry->next);
	fileentry->next = tempdump;
    }
    else alldumps_list = tempdump;

    /* put sigpipe back the way we found it */
    if(sigaction(SIGPIPE, &oact, &act) != 0){
	error("error resetting SIGPIPE handler: %s", strerror(errno));
    }
    return (bytes_read);
}



/* 
 * Take a pattern of dumps and restore it blind, a la amrestore.  In addition,
 * be smart enough to change tapes and continue with minimal operator
 * intervention, and write out a record of what was found on tapes in the
 * the regular logging format.  Can take a tapelist with a specific set of
 * tapes to search (rather than "everything I can find"), which in turn can
 * optionally list specific files to restore.
 */
void search_tapes(prompt_out, use_changer, tapelist, match_list, flags)
FILE *prompt_out;
int use_changer;
tapelist_t *tapelist;
match_list_t *match_list;
rst_flags_t *flags;
{
    struct stat stat_tape;
    char *err;
    int have_changer = 1;
    int slot_num = -1;
    int slots = -1;
    int filenum;
    FILE *logstream = NULL;
    dumplist_t *fileentry = NULL;
    tapelist_t *desired_tape = NULL;
    struct sigaction act, oact;
    int newtape = 1;
    ssize_t bytes_read;

    struct seentapes{
	struct seentapes *next;
	char *slotstr;
	char *label;
	dumplist_t *files;
    } *seentapes = NULL;

    if(!prompt_out) prompt_out = stderr;

    if(flags->blocksize) blocksize = flags->blocksize;
    else if(blocksize == -1) blocksize = DISK_BLOCK_BYTES;

    /* catch SIGINT with something that'll flush unmerged splits */
    act.sa_handler = handle_sigint;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    if(sigaction(SIGINT, &act, &oact) != 0){
	error("error setting SIGINT handler: %s", strerror(errno));
    }
    if(flags->delay_assemble || flags->inline_assemble) exitassemble = 1;
    else exitassemble = 0;

    /* if given a log file, print an inventory of stuff found */
    if(flags->inventory_log){
	if(!strcmp(flags->inventory_log, "-")) logstream = stdout;
	else if((logstream = fopen(flags->inventory_log, "w+")) == NULL){
	    error("Couldn't open log file %s for writing: %s\n",
		  flags->inventory_log, strerror(errno));
	}
    }

    /* Suss what tape device we're using, whether there's a changer, etc. */
    if((have_changer = changer_init()) == 0 || !use_changer) {
	if(flags->alt_tapedev) cur_tapedev = stralloc(flags->alt_tapedev);
	else if(!cur_tapedev) cur_tapedev = getconf_str(CNF_TAPEDEV);
	/* XXX oughta complain if no config is loaded */
	fprintf(stderr, "%s: Using tapedev %s\n", get_pname(), cur_tapedev);
    } else if (have_changer != 1) {
	error("changer initialization failed: %s", strerror(errno));
    }
    else{ /* good, the changer works, see what it can do */
	changer_info(&slots, &curslot, &backwards);
    }

    if(tapelist){
      slots = num_entries(tapelist);
      /*
	Spit out a list of expected tapes, so people with manual changers know
	what to load
      */
      fprintf(prompt_out, "The following tapes are needed:");
      for(desired_tape = tapelist;
          desired_tape != NULL;
	  desired_tape = desired_tape->next){
	fprintf(prompt_out, " %s", desired_tape->label);
      }
      fprintf(prompt_out, "\n");
      fflush(prompt_out);
      if(flags->wait_tape_prompt){
	char *input = NULL;
        fprintf(prompt_out,"Press enter when ready\n");
	fflush(prompt_out);
        input = agets(stdin);
 	amfree(input);
	fprintf(prompt_out, "\n");
      }
    }
    desired_tape = tapelist;

    /*
     * If we're not given a tapelist, iterate over everything our changer can
     * find.  If there's no changer, we'll prompt to be handfed tapes.
     *
     * If we *are* given a tapelist, restore from those tapes in the order in
     * which they're listed.  Unless the changer (if we have one) can't go
     * backwards, in which case check every tape we see and restore from it if
     * appropriate.
     *
     * (obnoxious, isn't this?)
     */
    slot_num = 0;
    curslot = stralloc("<none>");
    while(desired_tape || ((slot_num < slots || !have_changer) && !tapelist)){
	char *label = NULL;
	struct seentapes *tape_seen = NULL;
	dumpfile_t file, tapestart, prev_rst_file;
	char *logline = NULL;
	int tapefile_idx = -1;
	int wrongtape = 0;
	int isafile = 0;

	/*
	 * Deal with instances where we're being asked to restore from a file
	 */
	if(desired_tape && desired_tape->isafile){
	    isafile = 1;
	    if ((tapefd = open(desired_tape->label, 0)) == -1){
		fprintf(stderr, "could not open %s: %s\n",
		      desired_tape->label, strerror(errno));
	        continue;
	    }
	    fprintf(stderr, "Reading %s to fd %d\n", desired_tape->label, tapefd);

	    bytes_read = read_file_header(&file, tapefd, 1, flags);
	    label = stralloc(desired_tape->label);
	}
	/*
	 * Make sure we can read whatever tape is loaded, then grab the label.
	 */
	else if(cur_tapedev && newtape){
	    if(tape_stat(cur_tapedev,&stat_tape)!=0) {
		error("could not stat %s: %s", cur_tapedev, strerror(errno));
	    }

	    if((err = tape_rewind(cur_tapedev)) != NULL) {
	        fprintf(stderr, "Could not rewind device '%s': %s\n",
	                    cur_tapedev, err);
	         continue;
	    }
	    if((tapefd = tape_open(cur_tapedev, 0)) < 0){
		fprintf(stderr, "could not open tape device %s: %s\n",
	                    cur_tapedev, strerror(errno));
		continue;
	    }
	
            bytes_read = read_file_header(&file, tapefd, 0, flags);

            if(file.type != F_TAPESTART) {
	        fprintf(stderr, "Not an amanda tape\n");
	        tapefd_close(tapefd);
  	        continue;
            }
            memcpy(&tapestart, &file, sizeof(dumpfile_t));

	    label = stralloc(file.name);
	}
	else if(newtape) wrongtape = 1; /* nothing loaded */

	/*
	 * Skip this tape if we did it already.  Note that this would let
	 * duplicate labels through, so long as they were in the same slot.
	 * I'm over it, are you?
	 */
	if(label && newtape && !isafile && !wrongtape){
	    for(tape_seen = seentapes; tape_seen; tape_seen = tape_seen->next){
		if(!strcmp(tape_seen->label, label) &&
			!strcmp(tape_seen->slotstr, curslot)){
		    fprintf(stderr, "Saw repeat tape %s in slot %s\n", label, curslot);
		    wrongtape = 1;
		    break;
		}
	    }
	}

	/*
	 * See if we've got the tape we were looking for, if we were looking
	 * for something specific.
	 */
	if((desired_tape || !cur_tapedev) && newtape && !isafile && !wrongtape){
	    if(!label || (flags->check_labels &&
		    desired_tape && strcmp(label, desired_tape->label) != 0)){
		if(label){
		    fprintf(stderr, "Label mismatch, got %s and expected %s\n", label, desired_tape->label);
		    if(have_changer && !backwards){
		        fprintf(stderr, "Changer can't go backwards, restoring anyway\n");
		    }
		    else wrongtape = 1;
		}
		else fprintf(stderr, "No tape device initialized yet\n");
	    }
	}
	    

	/*
	 * If we have an incorrect tape loaded, go try to find the right one
	 * (or just see what the next available one is).
	 */
	if((wrongtape || !newtape) && !isafile){
	    if(desired_tape){
		tapefd_close(tapefd);
		if(have_changer){
		    fprintf(stderr,"Looking for tape %s...\n", desired_tape->label);
		    if(backwards){
			searchlabel = desired_tape->label; 
			changer_find(scan_init, loadlabel_slot, desired_tape->label);
		    }
		    else{
			changer_loadslot("next", &curslot, &cur_tapedev);
		    }
		    while(have_changer && !cur_tapedev){
		        fprintf(stderr, "Changer did not set the tape device (slot empty or changer misconfigured?)\n");
			changer_loadslot("next", &curslot, &cur_tapedev);
		    }
		}
		else{
		    char *input = NULL;
                    fprintf(prompt_out,"Insert tape labeled %s and press return\n", desired_tape->label);
		    fflush(prompt_out);
                    input = agets(stdin);
 		    amfree(input);
		}
	    }
	    else{
		if(have_changer){
		    if(slot_num == 0)
			changer_loadslot("first", &curslot, &cur_tapedev);
		    else
			changer_loadslot("next", &curslot, &cur_tapedev);
		    if(have_changer && !cur_tapedev)
			error("Changer did not set the tape device, probably misconfigured");
		}
		else{
		    /* XXX need a condition for ending processing? */
		    char *input = NULL;
                    fprintf(prompt_out,"Insert a tape to search and press enter, ^D to finish reading tapes\n");
		    fflush(prompt_out);
                    if((input = agets(stdin)) == NULL) break;
		    amfree(input);
		}
	    }
	    newtape = 1;
	    continue;
	}

	newtape = 0;

	slot_num++;

	if(!isafile){
	    fprintf(stderr, "Scanning %s (slot %s)\n", label, curslot);
	    fflush(stderr);
	}

	tape_seen = alloc(sizeof(struct seentapes));
	memset(tape_seen, '\0', sizeof(struct seentapes));

	tape_seen->label = label;
	tape_seen->slotstr = stralloc(curslot);
	tape_seen->next = seentapes;
	tape_seen->files = NULL;
	seentapes = tape_seen;

	/*
	 * Start slogging through the tape itself.  If our tapelist (if we
	 * have one) contains a list of files to restore, obey that instead
	 * of checking for matching headers on all files.
	 */
	filenum = 0;
	if(desired_tape && desired_tape->numfiles > 0) tapefile_idx = 0;

	/* if we know where we're going, fastforward there */
	if(flags->fsf && !isafile){
	    int fsf_by = 0;

	    /* If we have a tapelist entry, filenums will be store there */
	    if(tapefile_idx >= 0)
		fsf_by = desired_tape->files[tapefile_idx]; 
	    /*
	     * older semantics assume we're restoring one file,	with the fsf
	     * flag being the filenum on tape for said file
	     */
	    else fsf_by = flags->fsf;

	    if(fsf_by > 0){
	        if(tapefd_rewind(tapefd) < 0) {
		    error("Could not rewind device %s: %s", cur_tapedev,
							  strerror(errno));
	        }

		if(tapefd_fsf(tapefd, fsf_by) < 0) {
		    error("Could not fsf device %s by %d: %s", cur_tapedev, fsf_by,
							   strerror(errno));
		}
		else filenum = fsf_by;

		bytes_read = read_file_header(&file, tapefd, isafile, flags);
	    }
	}

	while((file.type == F_TAPESTART || file.type == F_DUMPFILE ||
	      file.type == F_SPLIT_DUMPFILE) &&
	      (tapefile_idx < 0 || tapefile_idx < desired_tape->numfiles)) {
	    char *filename = make_filename(&file);
	    int found_match = 0;
	    match_list_t *me;
	    dumplist_t *tempdump = NULL;

	    /* store record of this dump for inventorying purposes */
	    tempdump = alloc(sizeof(dumplist_t));
	    tempdump->file = alloc(sizeof(dumpfile_t));
	    tempdump->next = NULL;
	    memcpy(tempdump->file, &file, sizeof(dumpfile_t));
	    if(tape_seen->files){
		for(fileentry=tape_seen->files;
			fileentry->next;
			fileentry=fileentry->next);
		fileentry->next = tempdump;
	    }
	    else tape_seen->files = tempdump;

	    /* see if we need to restore the thing */
	    if(isafile) found_match = 1;
	    else if(tapefile_idx >= 0){ /* do it by explicit file #s */
		if(filenum == desired_tape->files[tapefile_idx]){
		    found_match = 1;
	   	    tapefile_idx++;
	        }
	    }
	    else{ /* search and match headers */
		for(me = match_list; me; me = me->next) {
		    if(disk_match(&file, me->datestamp, me->hostname,
				me->diskname, me->level) != 0){
			found_match = 1;
			break;
		    }
		}
	    }

	    if(found_match){
		fprintf(stderr, "%s: %3d: restoring ", get_pname(), filenum);
		print_header(stderr, &file);
		bytes_read = restore(&file, filename, tapefd, isafile, flags);
		filenum ++;
	    }

	    /* advance to the next file, fast-forwarding where reasonable */
	    if(bytes_read == 0 && !isafile) {
		tapefd_close(tapefd);
		if((tapefd = tape_open(cur_tapedev, 0)) < 0) {
		    error("could not open %s: %s",
		          cur_tapedev, strerror(errno));
		}
	    } else if(!isafile){
		/* cheat and jump ahead to where we're going if we can */
		if (!found_match && flags->fsf) {
		    drain_file(tapefd, flags);
		    filenum ++;
		} else if(tapefile_idx >= 0 && 
			  tapefile_idx < desired_tape->numfiles &&
			  flags->fsf){
		    int fsf_by = desired_tape->files[tapefile_idx] - filenum;
		    if(fsf_by > 0){
			if(tapefd_fsf(tapefd, fsf_by) < 0) {
			    error("Could not fsf device %s by %d: %s", cur_tapedev, fsf_by,
				  strerror(errno));
			}
			else filenum = desired_tape->files[tapefile_idx];
		    }
		} else if (!found_match && flags->fsf) {
		    /* ... or fsf by 1, whatever */
		    if(tapefd_fsf(tapefd, 1) < 0) {
			error("could not fsf device %s: %s",
			      cur_tapedev, strerror(errno));
		    } else {
			filenum ++;
		    }
		}
	    }


	    memcpy(&prev_rst_file, &file, sizeof(dumpfile_t));

	      
	    if(isafile)
                break;
            bytes_read = read_file_header(&file, tapefd, isafile, flags);

	    /* only restore a single dump, if piping to stdout */
	    if(!headers_equal(&prev_rst_file, &file, 1) &&
	       flags->pipe_to_fd == fileno(stdout)) break;
	} /* while we keep seeing headers */

	if(!isafile){
	    if(bytes_read == 0) {
		/* XXX is this dain-bramaged? */
		aclose(tapefd);
		if((tapefd = tape_open(cur_tapedev, 0)) < 0) {
		    error("could not open %s: %s",
			cur_tapedev, strerror(errno));
		}
	    } else{
		if(tapefd_fsf(tapefd, 1) < 0) {
		    error("could not fsf %s: %s",
			cur_tapedev, strerror(errno));
		}
	    }
	}
        tapefd_close(tapefd);

	/* spit out our accumulated list of dumps, if we're inventorying */
	if(logstream){
            logline = log_genstring(L_START, "taper",
                                   "datestamp %s label %s tape %d",
            		       tapestart.datestamp, tapestart.name, slot_num);
            fprintf(logstream, logline);
            for(fileentry=tape_seen->files; fileentry; fileentry=fileentry->next){
                logline = NULL;
                switch(fileentry->file->type){
		    case F_DUMPFILE:
			logline = log_genstring(L_SUCCESS, "taper",
            			       "%s %s %s %d [faked log entry]",
            	                       fileentry->file->name,
            	                       fileentry->file->disk,
            	                       fileentry->file->datestamp,
            	                       fileentry->file->dumplevel);
            	    break;
            	case F_SPLIT_DUMPFILE:
            	    logline = log_genstring(L_CHUNK, "taper", 
            			       "%s %s %s %d %d [faked log entry]",
            	                       fileentry->file->name,
            	                       fileentry->file->disk,
            	                       fileentry->file->datestamp,
            	                       fileentry->file->partnum,
            	                       fileentry->file->dumplevel);
            	    break;
            	default:
            	    break;
                }
                if(logline){
		    fprintf(logstream, logline);
		    amfree(logline);
		    fflush(logstream);
                }
            }
	}
	fprintf(stderr, "%s: Search of %s complete\n",
			get_pname(), tape_seen->label);
	if(desired_tape) desired_tape = desired_tape->next;

	/* only restore a single dump, if piping to stdout */
	if(!headers_equal(&prev_rst_file, &file, 1) &&
	  flags->pipe_to_fd == fileno(stdout)) break;
    }

    if(logstream && logstream != stderr && logstream != stdout){
	fclose(logstream);
    }
    if(flags->delay_assemble || flags->inline_assemble){
	flush_open_outputs(1, NULL);
    }
    else flush_open_outputs(0, NULL);
}

/*
 * Create a new, clean set of restore flags with some sane default values.
 */
rst_flags_t *new_rst_flags()
{
    rst_flags_t *flags = alloc(sizeof(rst_flags_t));

    memset(flags, 0, sizeof(rst_flags_t));

    flags->fsf = 1;
    flags->comp_type = COMPRESS_FAST_OPT;
    flags->inline_assemble = 1;
    flags->pipe_to_fd = -1;
    flags->check_labels = 1;

    return(flags);
}

/*
 * Make sure the set of restore options given is sane.  Print errors for
 * things that're odd, and return -1 for fatal errors.
 */
int check_rst_flags(rst_flags_t *flags)
{
    int ret = 0;	
    
    if(!flags) return(-1);

    if(flags->compress && flags->leave_comp){
	fprintf(stderr, "Cannot specify 'compress output' and 'leave compression alone' together\n");
	ret = -1;
    }

    if(flags->restore_dir != NULL){
	struct stat statinfo;

	if(flags->pipe_to_fd != -1){
	    fprintf(stderr, "Specifying output directory and piping output are mutually exclusive\n");
	    ret = -1;
	}
	if(stat(flags->restore_dir, &statinfo) < 0){
	    fprintf(stderr, "Cannot stat restore target dir '%s': %s\n",
		      flags->restore_dir, strerror(errno));
	    ret = -1;
	}
	if((statinfo.st_mode & S_IFMT) != S_IFDIR){
	    fprintf(stderr, "'%s' is not a directory\n", flags->restore_dir);
	    ret = -1;
	}
    }

    if((flags->pipe_to_fd != -1 || flags->compress) &&
	    (flags->delay_assemble || !flags->inline_assemble)){
	fprintf(stderr, "Split dumps *must* be automatically reassembled when piping output or compressing/uncompressing\n");
	ret = -1;
    }

    if(flags->delay_assemble && flags->inline_assemble){
	fprintf(stderr, "Inline split assembling and delayed assembling are mutually exclusive\n");
	ret = -1;
    }

    return(ret);
}

/*
 * Clean up after a rst_flags_t
 */
void free_rst_flags(flags)
rst_flags_t *flags;
{
    if(!flags) return;

    if(flags->restore_dir) amfree(flags->restore_dir);
    if(flags->alt_tapedev) amfree(flags->alt_tapedev);
    if(flags->inventory_log) amfree(flags->inventory_log);

    amfree(flags);
}


/*
 * Clean up after a match_list_t
 */
void free_match_list(match_list)
match_list_t *match_list;
{
    match_list_t *me;
    match_list_t *prev = NULL;
  
    for(me = match_list; me; me = me->next){
	/* XXX freeing these is broken? can't work out why */
/*	if(me->hostname) amfree(me->hostname);
	if(me->diskname) amfree(me->diskname);
	if(me->datestamp) amfree(me->datestamp);
	if(me->level) amfree(me->level); */
	if(prev) amfree(prev);
	prev = me;
    }
    if(prev) amfree(prev);
}
