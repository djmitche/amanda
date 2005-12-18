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
 * $Id: amfetchdump.c,v 1.4 2005/12/18 00:42:10 martinea Exp $
 *
 * retrieves specific dumps from a set of amanda tapes
 */

#include "amanda.h"
#include "tapeio.h"
#include "fileheader.h"
#include "util.h"
#include "restore.h"
#include "diskfile.h"
#include "tapefile.h"
#include "find.h"
#include "changer.h"
#include "logfile.h"

#define CREAT_MODE	0640

static int got_sigpipe;
extern char *rst_conf_logdir;
extern char *rst_conf_logfile;
extern char *config_dir;
int get_lock = 0;

typedef struct needed_tapes_s {
    char *label;
    int isafile;
    find_result_t *files;
    struct needed_tapes_s *next;
    struct needed_tapes_s *prev;
} needed_tape_t;

/* local functions */

void errexit P((void));
void handle_sigpipe P((int sig));
tapelist_t *list_needed_tapes P((match_list_t *match_list));
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


void usage()
/*
 * Print usage message and terminate.
 */
{
    fprintf(stderr, "Usage: amfetchdump [options] config hostname [diskname [datestamp [level [hostname [diskname [datestamp [level ... ]]]]]]]\n\n");
    fprintf(stderr, "Goes and grabs a dump from tape, moving tapes around and assembling parts as\n");
    fprintf(stderr, "necessary.  Files are restored to the current directory, unless otherwise\nspecified.\n\n");
    fprintf(stderr, "  -p Pipe exactly *one* complete dumpfile to stdout, instead of to disk.\n");
    fprintf(stderr, "  -o <output dir> Restore files to this directory.\n");
    fprintf(stderr, "  -d <device> Force restoration from a particular tape device.\n");
    fprintf(stderr, "  -c Compress output, fastest method available.\n");
    fprintf(stderr, "  -C Compress output, best filesize method available.\n");
    fprintf(stderr, "  -l Leave dumps (un)compressed, whichever way they were originally on tape.\n");
    fprintf(stderr, "  -a Assume all tapes are available via changer, do not prompt for initial load.\n");
    fprintf(stderr, "  -i <dst_file> Search through tapes and write out an inventory while we\n     restore.  Useful only if normal logs are unavailable.\n");
    fprintf(stderr, "  -w Wait to put split dumps together until all chunks have been restored.\n");
    fprintf(stderr, "  -n Do not reassemble split dumpfiles.\n");
    fprintf(stderr, "  -k Skip the rewind/label read when reading a new tape.\n");
    fprintf(stderr, "  -s Do not use fast forward to skip files we won't restore.  Use only if fsf\n     causes your tapes to skip too far.\n");
    fprintf(stderr, "  -b <blocksize> Force a particular block size (default is 32kb).\n");
    exit(1);
}

/*
 * Build the list of tapes we'll be wanting, and include data about the
 * files we want from said tapes while we're at it (the whole find_result
 * should do fine)
 */
tapelist_t *list_needed_tapes(match_list)
match_list_t *match_list;
{
    needed_tape_t *needed_tapes = NULL, *curtape = NULL;
    disklist_t diskqp;
    match_list_t *me = NULL;
    find_result_t *alldumps = NULL;
    tapelist_t *tapes = NULL;
    int numtapes = 0;
    char *conf_diskfile, *conf_tapelist;

    /* For disks and tape lists */
    conf_diskfile = getconf_str(CNF_DISKFILE);
    conf_tapelist = getconf_str(CNF_TAPELIST);
    if (*conf_diskfile == '/') {
        conf_diskfile = stralloc(conf_diskfile);
    } else {
        conf_diskfile = stralloc2(config_dir, conf_diskfile);
    }
    if(read_diskfile(conf_diskfile, &diskqp) != 0) {
        error("could not load disklist \"%s\"", conf_diskfile);
    }
    if (*conf_tapelist == '/') {
        conf_tapelist = stralloc(conf_tapelist);
    } else {
        conf_tapelist = stralloc2(config_dir, conf_tapelist);
    }
    if(read_tapelist(conf_tapelist)) {
        error("could not load tapelist \"%s\"", conf_tapelist);
    }
    amfree(conf_diskfile);
    amfree(conf_tapelist);

    /* Grab a find_output_t of all logged dumps */
    alldumps = find_dump(1, &diskqp); 
    free_disklist(&diskqp);
    if(alldumps == NULL){
        fprintf(stderr, "No dump records found\n");
        exit(1);
    }
 
    /* Compare all known dumps to our match list, note what we'll need */
    for(me = match_list; me; me = me->next) {
	find_result_t *curmatch = NULL;	
	find_result_t *matches = NULL;	

	matches = dumps_match(alldumps, me->hostname, me->diskname,
	                         me->datestamp, me->level, 1);
	sort_find_result("Dhklp", &matches);
	for(curmatch = matches; curmatch; curmatch = curmatch->next){
	    int havetape = 0;
	    if(strcmp("OK", curmatch->status)){
		fprintf(stderr,"Dump %d %s %s %d had status '%s', skipping\n",
		                 curmatch->datestamp, curmatch->hostname,
				 curmatch->diskname, curmatch->level,
				 curmatch->status);
		continue;
	    }
	    for(curtape = needed_tapes; curtape; curtape = curtape->next) {
		if(!strcmp(curtape->label, curmatch->label)){
		    find_result_t *rsttemp = NULL;
		    find_result_t *rstfile = alloc(sizeof(find_result_t));
		    int keep = 1;

		    memcpy(rstfile, curmatch, sizeof(find_result_t));

		    havetape = 1;

		    for(rsttemp = curtape->files;
			    rsttemp;
			    rsttemp=rsttemp->next){
			if(rstfile->filenum == rsttemp->filenum){
			    fprintf(stderr, "Seeing multiple entries for tape %s file %d, using most recent\n", curtape->label, rstfile->filenum);
			    keep = 0;
			}
		    }
		    if(!keep){
			amfree(rstfile);
			break;
		    }
		    rstfile->next = curtape->files;

		    if(curmatch->filenum < 1) curtape->isafile = 1;
		    else curtape->isafile = 0;
		    curtape->files = rstfile;
		    break;
		}
	    }
	    if(!havetape){
		find_result_t *rstfile = alloc(sizeof(find_result_t));
		needed_tape_t *newtape =
		                          alloc(sizeof(needed_tape_t));
		memcpy(rstfile, curmatch, sizeof(find_result_t));
		rstfile->next = NULL;
		newtape->files = rstfile;
		if(curmatch->filenum < 1) newtape->isafile = 1;
		else newtape->isafile = 0;
		newtape->label = curmatch->label;
		if(needed_tapes){
		    needed_tapes->prev->next = newtape;
		    newtape->prev = needed_tapes->prev;
		    needed_tapes->prev = newtape;
		}
		else{
		    needed_tapes = newtape;
		    needed_tapes->prev = needed_tapes;
		}
		newtape->next = NULL;
		numtapes++;
#if 0
//		free_find_result(rstfile);
#endif
	    } /* if(!havetape) */

	} /* for(curmatch = matches ... */
    } /* for(me = match_list ... */

    if(numtapes == 0){
      fprintf(stderr, "No matching dumps found\n");
      exit(1);
    }

    /* stick that list in a structure that librestore will understand */
    for(curtape = needed_tapes; curtape; curtape = curtape->next) {
	find_result_t *curfind = NULL;
	for(curfind = curtape->files; curfind; curfind = curfind->next) {
	    tapes = append_to_tapelist(tapes, curtape->label,
				       curfind->filenum, curtape->isafile);
	}
    }

    fprintf(stderr, "%d tape(s) needed for restoration\n", numtapes);
    return(tapes);
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
    match_list_t *match_list = NULL;
    match_list_t *me = NULL;
    int fd;
    char *config_name = NULL;
    char *conffile = NULL;
    tapelist_t *needed_tapes = NULL;
    char *e;
    int arg_state;
    rst_flags_t *rst_flags;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    set_pname("amfetchdump");

    erroutput_type = ERR_INTERACTIVE;

    onerror(errexit);
    signal(SIGPIPE, handle_sigpipe);

    if(argc <= 1) usage();

    rst_flags = new_rst_flags();
    rst_flags->wait_tape_prompt = 1;
    
    /* handle options */
    while( (opt = getopt(argc, argv, "alht:scCpb:nwi:d:o:")) != -1) {
	switch(opt) {
	case 'b': rst_flags->compress = 1; break;
            rst_flags->blocksize = strtol(optarg, &e, 10);
            if(*e == 'k' || *e == 'K') {
	        rst_flags->blocksize *= 1024;
	    } else if(*e == 'm' || *e == 'M') {
	        rst_flags->blocksize *= 1024 * 1024;
	    } else if(*e != '\0') {
	        error("invalid blocksize value \"%s\"", optarg);
	    }
	    if(rst_flags->blocksize < DISK_BLOCK_BYTES) {
	        error("minimum block size is %dk", DISK_BLOCK_BYTES / 1024);
	    }
	    break;
	case 'c': rst_flags->compress = 1; break;
	case 'o': rst_flags->restore_dir = stralloc(optarg) ; break;
	case 'd': rst_flags->alt_tapedev = stralloc(optarg) ; break;
	case 'C':
	    rst_flags->compress = 1;
	    rst_flags->comp_type = COMPRESS_BEST_OPT;
	    break;
	case 'p': rst_flags->pipe_to_fd = fileno(stdout); break;
	case 's': rst_flags->fsf = 0; break;
	case 'l': rst_flags->leave_comp = 1; break;
	case 'i': rst_flags->inventory_log = stralloc(optarg); break;
	case 'n': rst_flags->inline_assemble = 0; break;
	case 'w': rst_flags->delay_assemble = 1; break;
	case 'a': rst_flags->wait_tape_prompt = 0; break;
	case 'h': rst_flags->headers = 1; break;
	default:
	    usage();
	}
    }

    /* Check some flags that affect inventorying */
    if(rst_flags->inventory_log){
	if(rst_flags->inline_assemble) rst_flags->delay_assemble = 1;
	rst_flags->inline_assemble = 0;
	rst_flags->leave_comp = 1;
	if(rst_flags->compress){
	    error("Cannot force compression when doing inventory/search");
	}
	fprintf(stderr, "Doing inventory/search, dumps will not be uncompressed or assembled on-the-fly.\n");
    }
    else{
	if(rst_flags->delay_assemble){
	    fprintf(stderr, "Using -w, split dumpfiles will *not* be automatically uncompressed.\n");
	}
    }

    /* make sure our options all make sense otherwise */
    if(check_rst_flags(rst_flags) == -1) usage();

    config_name = argv[optind++];
    config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if (read_conffile(conffile)) {
	error("errors processing config file \"%s\"", conffile);
    }
    amfree(conffile);


    if((argc - optind) < 1 && !rst_flags->inventory_log){
	fprintf(stderr, "Not enough arguments\n\n");
	usage();
    }

#define ARG_GET_HOST 0
#define ARG_GET_DISK 1
#define ARG_GET_DATE 2
#define ARG_GET_LEVL 3

    arg_state = ARG_GET_HOST;
    while(optind < argc) {
        switch(arg_state) {
        case ARG_GET_HOST:
            /*
             * New host/disk/date/level set, so allocate a match_list.
             */
            me = alloc(sizeof(*me));
            me->hostname = argv[optind++];
            me->diskname = "";
            me->datestamp = "";
            me->level = "";
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
            arg_state = ARG_GET_LEVL;
            break;
        case ARG_GET_LEVL:
            me->level = argv[optind++];
            if(me->level[0] != '\0'
               && (errstr=validate_regexp(me->level)) != NULL) {
                fprintf(stderr, "%s: bad level regex \"%s\": %s\n",
                        get_pname(), me->level, errstr);
                usage();
            }
        }
    }

    /* XXX I don't think this can happen */
    if(match_list == NULL && !rst_flags->inventory_log) {
        match_list = alloc(sizeof(*match_list));
        match_list->hostname = "";
        match_list->diskname = "";
        match_list->datestamp = "";
        match_list->level = "";
        match_list->next = NULL;
    }

    /*
     * We've been told explicitly to go and search through the tapes the hard
     * way.
     */
    if(rst_flags->inventory_log){
	fprintf(stderr, "Beginning tape-by-tape search.\n");
	search_tapes(stderr, 1, NULL, match_list, rst_flags);
	exit(0);
    }


    /* Decide what tapes we'll need */
    needed_tapes = list_needed_tapes(match_list);
    
    get_lock = lock_logfile(); /* config is loaded, should be ok here */
    search_tapes(NULL, 1, needed_tapes, match_list, rst_flags);
    if(get_lock) unlink(rst_conf_logfile);

    free_match_list(match_list);

    if(rst_flags->inline_assemble || rst_flags->delay_assemble)
	flush_open_outputs(1, NULL);
    else flush_open_outputs(0, NULL);

    free_rst_flags(rst_flags);

    return(0);
}
