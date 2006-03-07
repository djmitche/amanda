/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998, 2000 University of Maryland at College Park
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
/* $Id: taper.c,v 1.115 2006/03/07 21:52:03 martinea Exp $
 *
 * moves files from holding disk to tape, or from a socket to tape
 */

#include "amanda.h"
#include "util.h"
#include "conffile.h"
#include "tapefile.h"
#include "clock.h"
#include "stream.h"
#include "holding.h"
#include "logfile.h"
#include "tapeio.h"
#include "changer.h"
#include "version.h"
#include "arglist.h"
#include "token.h"
#include "amfeatures.h"
#include "fileheader.h"
#include "server_util.h"
#include "taperscan.c"

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#ifdef HAVE_LIBVTBLC
#include <vtblc.h>
#include <strings.h>
#include <math.h>


static int vtbl_no   = -1;
static int len       =  0;
static int offset    =  0;
static char *datestr = NULL;
static char start_datestr[20];
time_t raw_time;
struct tm tape_time;
struct tm backup_time;
struct tm *tape_timep = &tape_time;
typedef struct vtbl_lbls {
    u_int8_t  label[45];
    u_int8_t  date[20];
} vtbl_lbls;
static vtbl_lbls vtbl_entry[MAX_VOLUMES];
#endif /* HAVE_LIBVTBLC */
/*
 * XXX update stat collection/printing
 * XXX advance to next tape first in next_tape
 * XXX label is being read twice?
 */
long splitsize = 0; /* max size of dumpfile before split (Kb) */
char *splitbuf = NULL;
char *splitbuf_wr_ptr = NULL; /* the number of Kb we've written into splitbuf */
int orig_holdfile = -1;

/* NBUFS replaced by conf_tapebufs */
/* #define NBUFS		20 */
int conf_tapebufs;

off_t maxseek = (off_t)1 << ((sizeof(off_t) * 8) - 11);

char *holdfile_path = NULL;
char *holdfile_path_thischunk = NULL;
int num_holdfile_chunks = 0;
dumpfile_t holdfile_hdr;
dumpfile_t holdfile_hdr_thischunk;
off_t holdfile_offset_thischunk = (off_t)0;
int splitbuffer_fd = -1;
char *splitbuffer_path = NULL;

#define MODE_NONE 0
#define MODE_FILE_WRITE 1
#define MODE_PORT_WRITE 2

int mode = MODE_NONE;

/* This is now the number of empties, not full bufs */
#define THRESHOLD	1

#define CONNECT_TIMEOUT 2*60



#define EMPTY 1
#define FILLING 2
#define FULL 3

typedef struct buffer_s {
    long status;
    unsigned int size;
    char *buffer;
} buffer_t;

#define nextbuf(p)    ((p) == buftable+conf_tapebufs-1? buftable : (p)+1)
#define prevbuf(p)    ((p) == buftable? buftable+conf_tapebufs-1 : (p)-1)

/* major modules */
int main P((int main_argc, char **main_argv));
void file_reader_side P((int rdpipe, int wrpipe));
void tape_writer_side P((int rdpipe, int wrpipe));

/* shared-memory routines */
char *attach_buffers P((unsigned int size));
void detach_buffers P((char *bufp));
void destroy_buffers P((void));
#define REMOVE_SHARED_MEMORY() \
    detach_buffers(buffers); \
    if (strcmp(procname, "reader") == 0) { \
	destroy_buffers(); \
    }

/* synchronization pipe routines */
void syncpipe_init P((int rd, int wr));
char syncpipe_get P((int *intp));
int  syncpipe_getint P((void));
char *syncpipe_getstr P((void));
void syncpipe_put P((int ch, int intval));
void syncpipe_putint P((int i));
void syncpipe_putstr P((const char *str));

/* tape manipulation subsystem */
int first_tape P((char *new_datestamp));
int next_tape P((int writerr));
int end_tape P((int writerr));
int write_filemark P((void));

/* support crap */
int seek_holdfile P((int fd, buffer_t *bp, long kbytes));

/* signal handling */
static void install_signal_handlers P((void));
static void signal_handler P((int));

/* exit routine */
static void cleanup P((void));

/*
 * ========================================================================
 * GLOBAL STATE
 *
 */
int interactive;
int writerpid;
times_t total_wait;
#ifdef TAPER_DEBUG
int bufdebug = 1;
#else
int bufdebug = 0;
#endif

char *buffers = NULL;
buffer_t *buftable = NULL;
int err;

char *procname = "parent";

char *taper_datestamp = NULL;
char *label = NULL;
int filenum;
char *errstr = NULL;
int tape_fd = -1;
char *tapedev = NULL;
char *tapetype = NULL;
tapetype_t *tt = NULL;
long tt_blocksize;
long tt_blocksize_kb;
long buffer_size;
int tt_file_pad;
static unsigned long malloc_hist_1, malloc_size_1;
static unsigned long malloc_hist_2, malloc_size_2;
dumpfile_t file;
dumpfile_t *save_holdfile = NULL;
long cur_span_chunkstart = 0; /* start of current split dump chunk (Kb) */
char *holdfile_name;
int num_splits = 0;
int expected_splits = 0;
int num_holdfiles = 0;
times_t curdump_rt;

am_feature_t *their_features = NULL;

int runtapes, cur_tape, have_changer, tapedays;
char *labelstr, *conf_tapelist;
#ifdef HAVE_LIBVTBLC
char *rawtapedev;
int first_seg, last_seg;
#endif /* HAVE_LIBVTBLC */

/*
 * ========================================================================
 * MAIN PROGRAM
 *
 */
int main(main_argc, main_argv)
int main_argc;
char **main_argv;
{
    int p2c[2], c2p[2];		/* parent-to-child, child-to-parent pipes */
    char *conffile;
    unsigned int size;
    int i;
    int j;
    int page_size;
    char *first_buffer;

    safe_fd(-1, 0);

    set_pname("taper");

    /* Don't die when child closes pipe */
    signal(SIGPIPE, SIG_IGN);

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    fprintf(stderr, "%s: pid %ld executable %s version %s\n",
	    get_pname(), (long) getpid(), main_argv[0], version());
    fflush(stderr);

    if (main_argc > 1 && main_argv[1][0] != '-') {
	config_name = stralloc(main_argv[1]);
	config_dir = vstralloc(CONFIG_DIR, "/", main_argv[1], "/", NULL);
	main_argc--;
	main_argv++;
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

    install_signal_handlers();
    atexit(cleanup);

    /* print prompts and debug messages if running interactive */

    interactive = (main_argc > 1 && strcmp(main_argv[1],"-t") == 0);
    if(interactive) {
	erroutput_type = ERR_INTERACTIVE;
    } else {
	erroutput_type = ERR_AMANDALOG;
	set_logerror(logerror);
    }

    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if(read_conffile(conffile)) {
	error("errors processing config file \"%s\"", conffile);
    }
    amfree(conffile);

    conf_tapelist = getconf_str(CNF_TAPELIST);
    if (*conf_tapelist == '/') {
	conf_tapelist = stralloc(conf_tapelist);
    } else {
	conf_tapelist = stralloc2(config_dir, conf_tapelist);
    }
    if(read_tapelist(conf_tapelist)) {
	error("could not load tapelist \"%s\"", conf_tapelist);
    }

    tapedev	= getconf_str(CNF_TAPEDEV);
    tapetype    = getconf_str(CNF_TAPETYPE);
    tt		= lookup_tapetype(tapetype);
#ifdef HAVE_LIBVTBLC
    rawtapedev = getconf_str(CNF_RAWTAPEDEV);
#endif /* HAVE_LIBVTBLC */
    tapedays	= getconf_int(CNF_TAPECYCLE);
    labelstr	= getconf_str(CNF_LABELSTR);

    runtapes	= getconf_int(CNF_RUNTAPES);
    cur_tape	= 0;

    conf_tapebufs = getconf_int(CNF_TAPEBUFS);

    tt_blocksize_kb = tt->blocksize;
    tt_blocksize = tt_blocksize_kb * 1024;
    tt_file_pad = tt->file_pad;

    if(interactive) {
	fprintf(stderr,"taper: running in interactive test mode\n");
	fflush(stderr);
    }

    /* create read/write syncronization pipes */

    if(pipe(p2c) || pipe(c2p))
	error("creating sync pipes: %s", strerror(errno));

    /* create shared memory segment */

#if defined(HAVE_GETPAGESIZE)
    page_size = getpagesize();
    fprintf(stderr, "%s: page size is %d\n", get_pname(), page_size);
#else
    page_size = 1024;
    fprintf(stderr, "%s: getpagesize() not available, using %d\n",
	    get_pname(),
	    page_size);
#endif
    buffer_size = am_round(tt_blocksize, page_size);
    fprintf(stderr, "%s: buffer size is %ld\n", get_pname(), buffer_size);
    while(conf_tapebufs > 0) {
	size  = page_size;
	size += conf_tapebufs * buffer_size;
	size += conf_tapebufs * sizeof(buffer_t);
	if((buffers = attach_buffers(size)) != NULL) {
	    break;
	}
	log_add(L_INFO, "attach_buffers: (%d tapebuf%s: %d bytes) %s",
			conf_tapebufs,
			(conf_tapebufs == 1) ? "" : "s",
			size,
			strerror(errno));
	conf_tapebufs--;
    }
    if(buffers == NULL) {
	error("cannot allocate shared memory");
    }
    i = (buffers - (char *)0) & (page_size - 1);  /* page boundary offset */
    if(i != 0) {
	first_buffer = buffers + page_size - i;
	fprintf(stderr, "%s: shared memory at %p, first buffer at %p\n",
		get_pname(),
		buffers,
		first_buffer);
    } else {
	first_buffer = buffers;
    }
    buftable = (buffer_t *)(first_buffer + conf_tapebufs * buffer_size);
    memset(buftable, 0, conf_tapebufs * sizeof(buffer_t));
    if(conf_tapebufs < 10) {
	j = 1;
    } else if(conf_tapebufs < 100) {
	j = 2;
    } else {
	j = 3;
    }
    for(i = 0; i < conf_tapebufs; i++) {
	buftable[i].buffer = first_buffer + i * buffer_size;
	fprintf(stderr, "%s: buffer[%0*d] at %p\n",
		get_pname(),
		j, i,
		buftable[i].buffer);
    }
    fprintf(stderr, "%s: buffer structures at %p for %d bytes\n",
	    get_pname(),
	    buftable,
	    (int)(conf_tapebufs * sizeof(buffer_t)));

    /* fork off child writer process, parent becomes reader process */

    switch(writerpid = fork()) {
    case -1:
	error("fork: %s", strerror(errno));

    case 0:	/* child */
	aclose(p2c[1]);
	aclose(c2p[0]);

	tape_writer_side(p2c[0], c2p[1]);
	error("tape writer terminated unexpectedly");

    default:	/* parent */
	aclose(p2c[0]);
	aclose(c2p[1]);

	file_reader_side(c2p[0], p2c[1]);
	error("file reader terminated unexpectedly");
    }

    /* NOTREACHED */
    return 0;
}


/*
 * ========================================================================
 * FILE READER SIDE
 *
 */
int read_file P((int fd, char *handle,
		  char *host, char *disk, char *datestamp, 
		  int level));
int taper_fill_buffer P((int fd, buffer_t *bp, int buflen));
void dumpbufs P((char *str1));
void dumpstatus P((buffer_t *bp));
int get_next_holding_file P((int fd, buffer_t *bp, char *strclosing, int rc));
int predict_splits P((char *filename));
void create_split_buffer P((char *split_diskbuffer, long fallback_splitsize, char *id_string));
void free_split_buffer P(());


/*
 * Create a buffer, either in an mmapped file or in memory, where PORT-WRITE
 * dumps can buffer the current split chunk in case of retry.
 */
void create_split_buffer(split_diskbuffer, fallback_splitsize, id_string)
char *split_diskbuffer;
long fallback_splitsize;
char *id_string;
{
    char *buff_err = NULL;
    void *nulls = NULL;
    int c;
    
    /* don't bother if we're not actually splitting */
    if(splitsize <= 0){
	splitbuf = NULL;
	splitbuf_wr_ptr = NULL;
	return;
    }

#ifdef HAVE_MMAP
#ifdef HAVE_SYS_MMAN_H
    if(strcmp(split_diskbuffer, "NULL")){
	splitbuffer_path = vstralloc(split_diskbuffer,
				     "/splitdump_buffer_XXXXXX",
				     NULL);
#ifdef HAVE_MKSTEMP
	splitbuffer_fd = mkstemp(splitbuffer_path);
#else
	log_add(L_INFO, "mkstemp not available, using plain open() for split buffer- make sure %s has safe permissions", split_diskbuffer);
	splitbuffer_fd = open(splitbuffer_path, O_RDWR|O_CREAT, 0600);
#endif
	if(splitbuffer_fd == -1){
	    buff_err = newvstralloc(buff_err, "mkstemp/open of ", 
				    splitbuffer_path, "failed (",
				    strerror(errno), ")", NULL);
	    goto fallback;
	}
	nulls = alloc(1024); /* lame */
	memset(nulls, 0, 1024);
	for(c = 0; c < splitsize ; c++) {
	    if(fullwrite(splitbuffer_fd, nulls, 1024) < 1024){
		buff_err = newvstralloc(buff_err, "write to ", splitbuffer_path,
					"failed (", strerror(errno), ")", NULL);
		free_split_buffer();
		goto fallback;
	    }
	}
	amfree(nulls);

        splitbuf = mmap(NULL, (size_t)splitsize*1024, PROT_READ|PROT_WRITE,
			MAP_SHARED, splitbuffer_fd, (off_t)0);
	if(splitbuf == (char*)-1){
	    buff_err = newvstralloc(buff_err, "mmap failed (", strerror(errno),
				    ")", NULL);
	    free_split_buffer();
	    goto fallback;
	}
	fprintf(stderr,
		"taper: r: buffering %ldkb split chunks in mmapped file %s\n",
		splitsize, splitbuffer_path);
	splitbuf_wr_ptr = splitbuf;
	return;
    }
    else{
	buff_err = stralloc("no split_diskbuffer specified");
    }
#else
    buff_err = stralloc("mman.h not available");
    goto fallback;
#endif
#else
    buff_err = stralloc("mmap not available");
    goto fallback;
#endif

    /*
      Buffer split dumps in memory, if we can't use a file.
    */
    fallback:
        splitsize = fallback_splitsize;
	log_add(L_INFO,
	        "%s: using fallback split size of %dkb to buffer %s in-memory",
		buff_err, splitsize, id_string);
	splitbuf = alloc(splitsize * 1024);
	splitbuf_wr_ptr = splitbuf;
}

/*
 * Free up resources that create_split_buffer eats.
 */
void free_split_buffer()
{
    if(splitbuffer_fd != -1){
#ifdef HAVE_MMAP
#ifdef HAVE_SYS_MMAN_H
	if(splitbuf != NULL) munmap(splitbuf, splitsize);
#endif
#endif
	aclose(splitbuffer_fd);
	splitbuffer_fd = -1;

	if(unlink(splitbuffer_path) == -1){
	    log_add(L_WARNING, "Failed to unlink %s: %s",
	            splitbuffer_path, strerror(errno));
	}
	amfree(splitbuffer_path);
	splitbuffer_path = NULL;
    }
    else if(splitbuf){
	amfree(splitbuf);
	splitbuf = NULL;
    }
}


void file_reader_side(rdpipe, wrpipe)
int rdpipe, wrpipe;
{
    cmd_t cmd;
    struct cmdargs cmdargs;
    char *handle = NULL;
    char *filename = NULL;
    char *hostname = NULL;
    char *diskname = NULL;
    char *result = NULL;
    char *datestamp = NULL;
    char *split_diskbuffer = NULL;
    char *id_string = NULL;
    char tok;
    char *q = NULL;
    int level, fd, data_port, data_socket, wpid;
    char level_str[64];
    struct stat stat_file;
    int tape_started;
    int a;
    long fallback_splitsize = 0;
    int tmpint;

    procname = "reader";
    syncpipe_init(rdpipe, wrpipe);

    /* must get START_TAPER before beginning */

    startclock();
    cmd = getcmd(&cmdargs);
    total_wait = stopclock();

    if(cmd != START_TAPER || cmdargs.argc != 2) {
	error("error [file_reader_side cmd %d argc %d]", cmd, cmdargs.argc);
    }

    /* pass start command on to tape writer */

    taper_datestamp = newstralloc(taper_datestamp, cmdargs.argv[2]);

    tape_started = 0;
    syncpipe_put('S', 0);
    syncpipe_putstr(taper_datestamp);

    /* get result of start command */

    tok = syncpipe_get(&tmpint);
    switch(tok) {
    case 'S':
	putresult(TAPER_OK, "\n");
	tape_started = 1;
	/* start is logged in writer */
	break;
    case 'E':
	/* no tape, bail out */
	result = syncpipe_getstr();
	q = squotef("[%s]", result ? result : "(null)");
	putresult(TAPE_ERROR, "%s\n", q);
	amfree(q);
	log_add(L_ERROR,"no-tape [%s]", "No writable valid tape found");
	amfree(result);
	syncpipe_put('e', 0);			/* ACK error */
	break;
    default:
	error("expected 'S' or 'E' for START-TAPER, got '%c'", tok);
    }

    /* process further commands */

    while(1) {
	startclock();
	cmd = getcmd(&cmdargs);
	if(cmd != QUIT && !tape_started) {
	    error("error [file_reader_side cmd %d without tape ready]", cmd);
	}
	total_wait = timesadd(total_wait, stopclock());

	switch(cmd) {
	case PORT_WRITE:
	    /*
	     * PORT-WRITE
	     *   handle
	     *   hostname
	     *   features
	     *   diskname
	     *   level
	     *   datestamp
	     *   splitsize
	     *   split_diskbuffer
	     */
	    mode = MODE_PORT_WRITE;
	    cmdargs.argc++;			/* true count of args */
	    a = 2;

	    if(a >= cmdargs.argc) {
		error("error [taper PORT-WRITE: not enough args: handle]");
	    }
	    handle = newstralloc(handle, cmdargs.argv[a++]);

	    if(a >= cmdargs.argc) {
		error("error [taper PORT-WRITE: not enough args: hostname]");
	    }
	    hostname = newstralloc(hostname, cmdargs.argv[a++]);

	    if(a >= cmdargs.argc) {
		error("error [taper PORT-WRITE: not enough args: features]");
	    }
	    am_release_feature_set(their_features);
	    their_features = am_string_to_feature(cmdargs.argv[a++]);

	    if(a >= cmdargs.argc) {
		error("error [taper PORT-WRITE: not enough args: diskname]");
	    }
	    diskname = newstralloc(diskname, cmdargs.argv[a++]);

	    if(a >= cmdargs.argc) {
		error("error [taper PORT-WRITE: not enough args: level]");
	    }
	    level = atoi(cmdargs.argv[a++]);

	    if(a >= cmdargs.argc) {
		error("error [taper PORT-WRITE: not enough args: datestamp]");
	    }
	    datestamp = newstralloc(datestamp, cmdargs.argv[a++]);

	    if(a >= cmdargs.argc) {
		error("error [taper PORT-WRITE: not enough args: splitsize]");
	    }
	    splitsize = atoi(cmdargs.argv[a++]);

	    if(a >= cmdargs.argc) {
		error("error [taper PORT-WRITE: not enough args: split_diskbuffer]");
	    }
	    split_diskbuffer = newstralloc(split_diskbuffer, cmdargs.argv[a++]);

	    if(a >= cmdargs.argc) {
		error("error [taper PORT-WRITE: not enough args: fallback_splitsize]");
	    }
	    fallback_splitsize = atoi(cmdargs.argv[a++]);

	    if(a != cmdargs.argc) {
		error("error [taper file_reader_side PORT-WRITE: too many args: %d != %d]",
		      cmdargs.argc, a);
	    }

	    snprintf(level_str, sizeof(level_str), "%d", level);
	    id_string = newvstralloc(id_string, hostname, ":", diskname, ".",
				     level_str, NULL);

	    create_split_buffer(split_diskbuffer, fallback_splitsize, id_string);
	    amfree(id_string);

	    data_port = 0;
	    data_socket = stream_server(&data_port, -1, STREAM_BUFSIZE);	
	    if(data_socket < 0) {
		char *m;

		m = vstralloc("[port create failure: ",
			      strerror(errno),
			      "]",
			      NULL);
		q = squote(m);
		putresult(TAPE_ERROR, "%s %s\n", handle, q);
		amfree(m);
		break;
	    }
	    putresult(PORT, "%d\n", data_port);

	    if((fd = stream_accept(data_socket, CONNECT_TIMEOUT,
				   -1, NETWORK_BLOCK_BYTES)) == -1) {
		q = squote("[port connect timeout]");
		putresult(TAPE_ERROR, "%s %s\n", handle, q);
		aclose(data_socket);
		break;
	    }
	    expected_splits = -1;

	    while(read_file(fd,handle,hostname,diskname,datestamp,level));

	    aclose(data_socket);
	    free_split_buffer();
	    break;

	case FILE_WRITE:
	    /*
	     * FILE-WRITE
	     *   handle
	     *   filename
	     *   hostname
	     *   features
	     *   diskname
	     *   level
	     *   datestamp
	     *   splitsize
	     */
	    mode = MODE_FILE_WRITE;
	    cmdargs.argc++;			/* true count of args */
	    a = 2;

	    if(a >= cmdargs.argc) {
		error("error [taper FILE-WRITE: not enough args: handle]");
	    }
	    handle = newstralloc(handle, cmdargs.argv[a++]);

	    if(a >= cmdargs.argc) {
		error("error [taper FILE-WRITE: not enough args: filename]");
	    }
	    filename = NULL; /* where is this getting freed? */
	    filename = newstralloc(filename, cmdargs.argv[a++]);

	    if(a >= cmdargs.argc) {
		error("error [taper FILE-WRITE: not enough args: hostname]");
	    }
	    hostname = newstralloc(hostname, cmdargs.argv[a++]);

	    if(a >= cmdargs.argc) {
		error("error [taper FILE-WRITE: not enough args: features]");
	    }
	    am_release_feature_set(their_features);
	    their_features = am_string_to_feature(cmdargs.argv[a++]);

	    if(a >= cmdargs.argc) {
		error("error [taper FILE-WRITE: not enough args: diskname]");
	    }
	    diskname = newstralloc(diskname, cmdargs.argv[a++]);

	    if(a >= cmdargs.argc) {
		error("error [taper FILE-WRITE: not enough args: level]");
	    }
	    level = atoi(cmdargs.argv[a++]);

	    if(a >= cmdargs.argc) {
		error("error [taper FILE-WRITE: not enough args: datestamp]");
	    }
	    datestamp = newstralloc(datestamp, cmdargs.argv[a++]);

	    if(a >= cmdargs.argc) {
		error("error [taper FILE-WRITE: not enough args: splitsize]");
	    }
	    splitsize = atoi(cmdargs.argv[a++]);

	    if(a != cmdargs.argc) {
		error("error [taper file_reader_side FILE-WRITE: too many args: %d != %d]",
		      cmdargs.argc, a);
	    }
	    if(holdfile_name != NULL) {
		amfree(filename);
		filename = holdfile_name;
	    }

	    if((expected_splits = predict_splits(stralloc(filename))) < 0) {
		break;
	    }
	    if(stat(filename, &stat_file)!=0) {
		q = squotef("[%s]", strerror(errno));
		putresult(TAPE_ERROR, "%s %s\n", handle, q);
		break;
	    }
	    if((fd = open(filename, O_RDONLY)) == -1) {
		q = squotef("[%s]", strerror(errno));
		putresult(TAPE_ERROR, "%s %s\n", handle, q);
		break;
	    }
	    holdfile_path = stralloc(filename);
	    holdfile_path_thischunk = stralloc(filename);
	    holdfile_offset_thischunk = (off_t)0;

	    while(read_file(fd,handle,hostname,diskname,datestamp,level)){
		if(splitsize > 0 && holdfile_path_thischunk)
		    filename = stralloc(holdfile_path_thischunk);
		if((fd = open(filename, O_RDONLY)) == -1) {
		    q = squotef("[%s]", strerror(errno));
		    putresult(TAPE_ERROR, "%s %s\n", handle, q);
		    break;
		}
	    }

	    break;

	case QUIT:
	    putresult(QUITTING, "\n");
	    fprintf(stderr,"taper: DONE [idle wait: %s secs]\n",
		    walltime_str(total_wait));
	    fflush(stderr);
	    syncpipe_put('Q', 0);	/* tell writer we're exiting gracefully */
	    aclose(wrpipe);

	    if((wpid = wait(NULL)) != writerpid) {
		fprintf(stderr,
			"taper: writer wait returned %d instead of %d: %s\n",
			wpid, writerpid, strerror(errno));
		fflush(stderr);
	    }

	    if (datestamp != NULL)
		amfree(datestamp);
	    amfree(label);
	    amfree(errstr);
	    amfree(changer_resultstr);
	    amfree(tapedev);
	    amfree(conf_tapelist);
	    amfree(config_dir);
	    amfree(config_name);
	    if(holdfile_name != NULL) amfree(holdfile_name);

	    malloc_size_2 = malloc_inuse(&malloc_hist_2);

	    if(malloc_size_1 != malloc_size_2) {
		malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
	    }

	    exit(0);

	default:
	    if(cmdargs.argc >= 1) {
		q = squote(cmdargs.argv[1]);
	    } else if(cmdargs.argc >= 0) {
		q = squote(cmdargs.argv[0]);
	    } else {
		q = stralloc("(no input?)");
	    }
	    putresult(BAD_COMMAND, "%s\n", q);
	    break;
	}
    }
    amfree(q);
    amfree(handle);
    am_release_feature_set(their_features);
    amfree(hostname);
    amfree(diskname);
    fprintf(stderr, "TAPER AT END OF READER SIDE\n");
}

void dumpbufs(str1)
char *str1;
{
    int i,j;
    long v;

    fprintf(stderr, "%s: state", str1);
    for(i = j = 0; i < conf_tapebufs; i = j+1) {
	v = buftable[i].status;
	for(j = i; j < conf_tapebufs && buftable[j].status == v; j++);
	j--;
	if(i == j) fprintf(stderr, " %d:", i);
	else fprintf(stderr, " %d-%d:", i, j);
	switch(v) {
	case FULL:	fputc('F', stderr); break;
	case FILLING:	fputc('f', stderr); break;
	case EMPTY:	fputc('E', stderr); break;
	default:
	    fprintf(stderr, "%ld", v);
	    break;
	}

    }
    fputc('\n', stderr);
    fflush(stderr);
}

void dumpstatus(bp)
buffer_t *bp;
{
    char pn[2];
    char bt[NUM_STR_SIZE];
    char status[NUM_STR_SIZE + 1];
    char *str = NULL;

    pn[0] = procname[0];
    pn[1] = '\0';
    snprintf(bt, sizeof(bt), "%d", (int)(bp-buftable));

    switch(bp->status) {
    case FULL:		snprintf(status, sizeof(status), "F%d", bp->size);
			break;
    case FILLING:	status[0] = 'f'; status[1] = '\0'; break;
    case EMPTY:		status[0] = 'E'; status[1] = '\0'; break;
    default:
	snprintf(status, sizeof(status), "%ld", bp->status);
	break;
    }

    str = vstralloc("taper: ", pn, ": [buf ", bt, ":=", status, "]", NULL);
    dumpbufs(str);
    amfree(str);
}

/*
  Handle moving to the next chunk of holding file, if any.  Returns -1 for
  errors, 0 if there's no more file, or a positive integer for the amount of
  stuff read that'll go into 'rc' (XXX That's fugly, maybe that should just
  be another global.  What is rc anyway, 'read count?' I keep thinking it
  should be 'return code')
*/
int get_next_holding_file(fd, bp, strclosing, rc)
     int fd;
     buffer_t *bp;
     char *strclosing;
{
    int save_fd, rc1;
    struct stat stat_file;
    int ret = -1;
    
    save_fd = fd;
    close(fd);
    
    /* see if we're fresh out of file */
    if(file.cont_filename[0] == '\0') {
 	err = 0;
 	ret = 0;
    } else if(stat(file.cont_filename, &stat_file) != 0) {
 	err = errno;
 	ret = -1;
 	strclosing = newvstralloc(strclosing,"can't stat: ",file.cont_filename,NULL);
    } else if((fd = open(file.cont_filename,O_RDONLY)) == -1) {
 	err = errno;
 	ret = -1;
 	strclosing = newvstralloc(strclosing,"can't open: ",file.cont_filename,NULL);
    } else if((fd != save_fd) && dup2(fd, save_fd) == -1) {
 	err = errno;
 	ret = -1;
 	strclosing = newvstralloc(strclosing,"can't dup2: ",file.cont_filename,NULL);
    } else {
 	buffer_t bp1;
 	holdfile_path = stralloc(file.cont_filename);
	
 	fprintf(stderr, "taper: r: switching to next holding chunk '%s'\n", file.cont_filename); 
 	num_holdfile_chunks++;
	
 	bp1.status = EMPTY;
 	bp1.size = DISK_BLOCK_BYTES;
 	bp1.buffer = malloc(DISK_BLOCK_BYTES);
	
 	if(fd != save_fd) {
 	    close(fd);
 	    fd = save_fd;
 	}
	
 	rc1 = taper_fill_buffer(fd, &bp1, DISK_BLOCK_BYTES);
 	if(rc1 <= 0) {
 	    amfree(bp1.buffer);
 	    err = (rc1 < 0) ? errno : 0;
 	    ret = -1;
 	    strclosing = newvstralloc(strclosing,
 				      "Can't read header: ",
 				      file.cont_filename,
 				      NULL);
 	} else {
 	    parse_file_header(bp1.buffer, &file, rc1);
	    
 	    amfree(bp1.buffer);
 	    bp1.buffer = bp->buffer + rc;
	    
 	    rc1 = taper_fill_buffer(fd, &bp1, tt_blocksize - rc);
 	    if(rc1 <= 0) {
 		err = (rc1 < 0) ? errno : 0;
 		ret = -1;
 		if(rc1 < 0) {
 	    	    strclosing = newvstralloc(strclosing,
 					      "Can't read data: ",
					      file.cont_filename,
 					      NULL);
 		}
 	    }
 	    else {
 		ret = rc1;
 		num_holdfiles++;
 	    }
 	}
    }
    
    return(ret);
}


int read_file(fd, handle, hostname, diskname, datestamp, level)
    int fd, level;
    char *handle, *hostname, *diskname, *datestamp;
{
    buffer_t *bp;
    char tok;
    int rc, opening, closing, bufnum, need_closing, nexting;
    long filesize;
    times_t runtime;
    char *strclosing = NULL;
    char seekerrstr[STR_SIZE];
    char *str;
    int header_written = 0;
    int buflen;
    dumpfile_t first_file;
    dumpfile_t cur_holdfile;
    long kbytesread = 0;
    int header_read = 0;
    char *cur_filename = NULL;
    int retry_from_splitbuf = 0;
    char *splitbuf_rd_ptr = NULL;

    char *q = NULL;

#ifdef HAVE_LIBVTBLC
    static char desc[45];
    static char vol_date[20];
    static char vol_label[45];
#endif /* HAVE_LIBVTBLC */


    /* initialize */

    filesize = 0;
    closing = 0;
    need_closing = 0;
    nexting = 0;
    err = 0;

    /* don't break this if we're still on the same file as a previous init */
    if(cur_span_chunkstart <= 0){
    fh_init(&file);
      header_read = 0;
    }
    else if(mode == MODE_FILE_WRITE){
      memcpy(&file, save_holdfile, sizeof(dumpfile_t));
      memcpy(&cur_holdfile, save_holdfile, sizeof(dumpfile_t));
    }

    if(bufdebug) {
	fprintf(stderr, "taper: r: start file\n");
	fflush(stderr);
    }

    for(bp = buftable; bp < buftable + conf_tapebufs; bp++) {
	bp->status = EMPTY;
    }

    bp = buftable;
    if(interactive || bufdebug) dumpstatus(bp);

    if(cur_span_chunkstart >= 0 && splitsize > 0){
        /* We're supposed to start at some later part of the file, not read the
	   whole thing. "Seek" forward to where we want to be. */
	if(label) putresult(SPLIT_CONTINUE, "%s %s\n", handle, label);
        if(mode == MODE_FILE_WRITE && cur_span_chunkstart > 0){
	    fprintf(stderr, "taper: r: seeking %s to " OFF_T_FMT " kb\n",
	                    holdfile_path_thischunk, holdfile_offset_thischunk);
	    fflush(stderr);

	    if(holdfile_offset_thischunk > maxseek){
	      snprintf(seekerrstr, sizeof(seekerrstr), "Can't seek by " OFF_T_FMT " kb (compiled for %d-bit file offsets), recompile with large file support or set holdingdisk chunksize to <%ld Mb", holdfile_offset_thischunk, (int)(sizeof(off_t) * 8), (long)(maxseek/1024));
	      log_add(L_ERROR, "%s", seekerrstr);
	      fprintf(stderr, "taper: r: FATAL: %s\n", seekerrstr);
	      fflush(stderr);
	      syncpipe_put('X', 0);
	      return -1;
	    }
	    if(lseek(fd, holdfile_offset_thischunk*1024, SEEK_SET) == (off_t)-1){
	      fprintf(stderr, "taper: r: FATAL: seek_holdfile lseek error while seeking into %s by " OFF_T_FMT "kb: %s\n", holdfile_path_thischunk, holdfile_offset_thischunk, strerror(errno));
	      fflush(stderr);
	      syncpipe_put('X', 0);
	      return -1;
	    }
        }
        else if(mode == MODE_PORT_WRITE){
	    fprintf(stderr, "taper: r: re-reading split dump piece from buffer\n");
	    fflush(stderr);
	    retry_from_splitbuf = 1;
	    splitbuf_rd_ptr = splitbuf;
	    if(splitbuf_rd_ptr >= splitbuf_wr_ptr) retry_from_splitbuf = 0;
        }
        if(cur_span_chunkstart > 0) header_read = 1; /* really initialized in prior run */
    }

    /* tell writer to open tape */

    opening = 1;
    syncpipe_put('O', 0);
    syncpipe_putstr(datestamp);
    syncpipe_putstr(hostname);
    syncpipe_putstr(diskname);
    syncpipe_putint(level);

    startclock();
    
    /* read file in loop */
    
    while(1) {
	tok = syncpipe_get(&bufnum);
	switch(tok) {
	    
	case 'O':
	    assert(opening);
	    opening = 0;
	    err = 0;
	    break;
	    
	case 'R':
	    if(bufdebug) {
		fprintf(stderr, "taper: r: got R%d\n", bufnum);
		fflush(stderr);
	    }
	    
	    if(need_closing) {
		syncpipe_put('C', 0);
		closing = 1;
		need_closing = 0;
		break;
	    }
	    
	    if(closing) break;	/* ignore extra read tokens */
	    
	    assert(!opening);
	    if(bp->status != EMPTY || bufnum != bp-buftable) {
		/* XXX this SHOULD NOT HAPPEN.  Famous last words. */
		fprintf(stderr,"taper: panic: buffer mismatch at ofs %ld:\n",
			filesize);
		if(bufnum != bp-buftable) {
		    fprintf(stderr, "    my buf %d but writer buf %d\n",
			    (int)(bp-buftable), bufnum);
		}
		else {
		    fprintf(stderr,"buf %d state %s (%ld) instead of EMPTY\n",
			    (int)(bp-buftable),
			    bp->status == FILLING? "FILLING" :
			    bp->status == FULL? "FULL" : "EMPTY!?!?",
			    (long)bp->status);
		}
		dumpbufs("taper");
		sleep(1);
		dumpbufs("taper: after 1 sec");
		if(bp->status == EMPTY)
		    fprintf(stderr, "taper: result now correct!\n");
		fflush(stderr);
		
		errstr = newstralloc(errstr,
				     "[fatal buffer mismanagement bug]");
		q = squote(errstr);
		putresult(TRYAGAIN, "%s %s\n", handle, q);
		cur_span_chunkstart = 0;
		amfree(q);
		log_add(L_INFO, "retrying %s:%s.%d on new tape due to: %s",
		        hostname, diskname, level, errstr);
		closing = 1;
		syncpipe_put('X', 0);	/* X == buffer snafu, bail */
		do {
		    tok = syncpipe_get(&bufnum);
		} while(tok != 'x');
		aclose(fd);
		return -1;
	    } /* end 'if (bf->status != EMPTY || bufnum != bp-buftable)' */

	    bp->status = FILLING;
	    buflen = header_read ? tt_blocksize : DISK_BLOCK_BYTES;
	    if(interactive || bufdebug) dumpstatus(bp);
 	    if(header_written == 0 && (header_read == 1 || cur_span_chunkstart > 0)){
 		/* for split dumpfiles, modify headers for the second - nth
 		   pieces that signify that they're continuations of the last
 		   normal one */
 		char *cont_filename;
 		file.type = F_SPLIT_DUMPFILE;
 		file.partnum = num_splits + 1;
 		file.totalparts = expected_splits;
                 cont_filename = stralloc(file.cont_filename);
 		file.cont_filename[0] = '\0';
 		build_header(bp->buffer, &file, tt_blocksize);
  
 		if(cont_filename[0] != '\0') {
 		  file.type = F_CONT_DUMPFILE;
                   strncpy(file.cont_filename, cont_filename,
                           sizeof(file.cont_filename));
  			}
 		memcpy(&cur_holdfile, &file, sizeof(dumpfile_t));
  
 		if(interactive || bufdebug) dumpstatus(bp);
 		bp->size = tt_blocksize;
 		rc = tt_blocksize;
 		header_written = 1;
 		amfree(cont_filename);
 			}
 	    else if(retry_from_splitbuf){
 		/* quietly pull dump data from our in-memory cache, and the
 		   writer side need never know the wiser */
 		memcpy(bp->buffer, splitbuf_rd_ptr, tt_blocksize);
 		bp->size = tt_blocksize;
 		rc = tt_blocksize;
 
 		splitbuf_rd_ptr += tt_blocksize;
 		if(splitbuf_rd_ptr >= splitbuf_wr_ptr) retry_from_splitbuf = 0;
 	    }
 	    else if((rc = taper_fill_buffer(fd, bp, buflen)) < 0) {
 		err = errno;
 		closing = 1;
 		strclosing = newvstralloc(strclosing,"Can't read data: ",NULL);
 		syncpipe_put('C', 0);
 	    }
  
 	    if(!closing) {
 	        if(rc < buflen) { /* switch to next holding file */
 		    int ret;
 		    if(file.cont_filename[0] != '\0'){
 		       cur_filename = newvstralloc(cur_filename, file.cont_filename, NULL);
 				}
 		    ret = get_next_holding_file(fd, bp, strclosing, rc);
 		    if(ret <= 0){
 			need_closing = 1;
  			    }
  			    else {
 		        memcpy(&cur_holdfile, &file, sizeof(dumpfile_t));
 		        rc += ret;
  				bp->size = rc;
  			    }
  			}
  		if(rc > 0) {
  		    bp->status = FULL;
 		    /* rebuild the header block, which might have CONT junk */
  		    if(header_read == 0) {
  			char *cont_filename;
 			/* write the "real" filename if the holding-file
 			   is a partial one */
  			parse_file_header(bp->buffer, &file, rc);
  			parse_file_header(bp->buffer, &first_file, rc);
  			cont_filename = stralloc(file.cont_filename);
  			file.cont_filename[0] = '\0';
 			if(splitsize > 0){
 			    file.type = F_SPLIT_DUMPFILE;
 			    file.partnum = 1;
 			    file.totalparts = expected_splits;
 			}
  			file.blocksize = tt_blocksize;
  			build_header(bp->buffer, &file, tt_blocksize);
 			kbytesread += tt_blocksize/1024; /* XXX shady */
 
 			file.type = F_CONT_DUMPFILE;
 
  			/* add CONT_FILENAME back to in-memory header */
  			strncpy(file.cont_filename, cont_filename, 
  				sizeof(file.cont_filename));
  			if(interactive || bufdebug) dumpstatus(bp);
  			bp->size = tt_blocksize; /* output a full tape block */
 			/* save the header, we'll need it if we jump tapes */
 			memcpy(&cur_holdfile, &file, sizeof(dumpfile_t));
  			header_read = 1;
 			header_written = 1;
  			amfree(cont_filename);
  		    }
  		    else {
 			filesize = kbytesread;
  		    }

		    if(bufdebug) {
			fprintf(stderr,"taper: r: put W%d\n",(int)(bp-buftable));
			fflush(stderr);
		    }
		    syncpipe_put('W', bp-buftable);
		    bp = nextbuf(bp);
		}

		if(kbytesread + DISK_BLOCK_BYTES/1024 >= splitsize && splitsize > 0 && !need_closing){

		    if(mode == MODE_PORT_WRITE){
			splitbuf_wr_ptr = splitbuf;
			splitbuf_rd_ptr = splitbuf;
			memset(splitbuf, 0, sizeof(splitbuf));
			retry_from_splitbuf = 0;
		    }

		    fprintf(stderr,"taper: r: end %s.%s.%s.%d part %d, splitting chunk that started at %ldkb after %ldkb (next chunk will start at %ldkb)\n", hostname, diskname, datestamp, level, num_splits+1, cur_span_chunkstart, kbytesread, cur_span_chunkstart+kbytesread);
		    fflush(stderr);

		    nexting = 1;
		    need_closing = 1;
		} /* end '(kbytesread >= splitsize && splitsize > 0)' */
		if(need_closing && rc <= 0) {
		    syncpipe_put('C', 0);
		    need_closing = 0;
		    closing = 1;
		}
                kbytesread += rc/1024;
	    } /* end the 'if(!closing)' (successful buffer fill) */
	    break;

	case 'T':
	case 'E':
	    syncpipe_put('e', 0);	/* ACK error */

	    str = syncpipe_getstr();
	    errstr = newvstralloc(errstr, "[", str ? str : "(null)", "]", NULL);
	    amfree(str);

	    q = squote(errstr);
	    if(tok == 'T') {
		if(splitsize > 0){
		    /* we'll be restarting this chunk on the next tape */
		    if(mode == MODE_FILE_WRITE){
		      aclose(fd);
		    }

		    putresult(SPLIT_NEEDNEXT, "%s %ld\n", handle, cur_span_chunkstart);
		    log_add(L_INFO, "continuing %s:%s.%d on new tape from %ldkb mark: %s",
			    hostname, diskname, level, cur_span_chunkstart, errstr);
		    return 1;
		}
		else{
		    /* restart the entire dump (failure propagates to driver) */
		    aclose(fd);
		    putresult(TRYAGAIN, "%s %s\n", handle, q);
		    cur_span_chunkstart = 0;
		    log_add(L_INFO, "retrying %s:%s.%d on new tape due to: %s",
			    hostname, diskname, level, errstr);
		}
	    } else {
		aclose(fd);
		putresult(TAPE_ERROR, "%s %s\n", handle, q);
		log_add(L_FAIL, "%s %s %s %d [out of tape]",
			hostname, diskname, datestamp, level);
		log_add(L_ERROR,"no-tape [%s]", "No more writable valid tape found");
	    }
	    amfree(q);

	    return 0;

	case 'C':
	    assert(!opening);
	    assert(closing);

	    if(nexting){
	      cur_span_chunkstart += kbytesread; /* XXX possibly wrong */
	      holdfile_name = newvstralloc(holdfile_name, cur_filename, NULL);

	      kbytesread = 0;
	      if(cur_filename != NULL) amfree(cur_filename);
	    }


	    str = syncpipe_getstr();
	    label = newstralloc(label, str ? str : "(null)");
	    amfree(str);
	    str = syncpipe_getstr();
	    filenum = atoi(str ? str : "-9876");	/* ??? */
	    amfree(str);
	    fprintf(stderr, "taper: reader-side: got label %s filenum %d\n",
		    label, filenum);
	    fflush(stderr);

	    /* we'll need that file descriptor if we're gonna write more */
	    if(!nexting){
	    aclose(fd);
	    }

	    runtime = stopclock();
	    if(nexting) startclock();
	    if(err) {
		if(strclosing) {
		    errstr = newvstralloc(errstr,
				          "[input: ", strclosing, ": ",
					  strerror(err), "]", NULL);
		    amfree(strclosing);
		}
		else
		    errstr = newvstralloc(errstr,
				          "[input: ", strerror(err), "]",
				          NULL);
		q = squote(errstr);
		putresult(TAPE_ERROR, "%s %s\n", handle, q);

		amfree(q);
		if(splitsize){
		  log_add(L_FAIL, "%s %s %s.%d %d %s", hostname, diskname,
			  datestamp, num_splits, level, errstr);
		}
		else{
		log_add(L_FAIL, "%s %s %s %d %s",
			hostname, diskname, datestamp, level, errstr);
		}
		str = syncpipe_getstr();	/* reap stats */
		amfree(str);
	    } else {
		char kb_str[NUM_STR_SIZE];
		char kps_str[NUM_STR_SIZE];
		double rt;

		rt = runtime.r.tv_sec+runtime.r.tv_usec/1000000.0;
		curdump_rt = timesadd(runtime, curdump_rt);
		snprintf(kb_str, sizeof(kb_str), "%ld", filesize);
		snprintf(kps_str, sizeof(kps_str), "%3.1f",
				     rt ? filesize / rt : 0.0);
		str = syncpipe_getstr();
		errstr = newvstralloc(errstr,
				      "[sec ", walltime_str(runtime),
				      " kb ", kb_str,
				      " kps ", kps_str,
				      " ", str,
				      "]",
				      NULL);
		q = squote(errstr);
		if (splitsize == 0) { /* Ordinary dump */
		    if(first_file.is_partial) {
			putresult(PARTIAL, "%s %s %d %s\n",
				  handle, label, filenum, q);
			log_add(L_PARTIAL, "%s %s %s %d %s",
				hostname, diskname, datestamp, level, errstr);
		    }
		    else {
			putresult(DONE, "%s %s %d %s\n",
				  handle, label, filenum, q);
			log_add(L_SUCCESS, "%s %s %s %d %s",
				hostname, diskname, datestamp, level, errstr);
		    }
		} else { /* Chunked dump */
		    num_splits++;
		    if(mode == MODE_FILE_WRITE){
			holdfile_path_thischunk = stralloc(holdfile_path);
			holdfile_offset_thischunk = (lseek(fd, (off_t)0, SEEK_CUR))/1024;
			if(!save_holdfile){
			    save_holdfile = alloc(sizeof(dumpfile_t));
			}
			memcpy(save_holdfile, &cur_holdfile,sizeof(dumpfile_t));
		    }
		    log_add(L_CHUNK, "%s %s %s %d %d %s", hostname, diskname,
			    datestamp, num_splits, level, errstr);
		    if(!nexting){ /* split dump complete */
			rt =curdump_rt.r.tv_sec+curdump_rt.r.tv_usec/1000000.0;
			snprintf(kb_str, sizeof(kb_str), "%ld",
				    filesize+cur_span_chunkstart);
			snprintf(kps_str, sizeof(kps_str), "%3.1f",
				    rt ? (filesize+cur_span_chunkstart) / rt : 0.0);
			errstr = newvstralloc(errstr,
					      "[sec ", walltime_str(curdump_rt),
					      " kb ", kb_str,
					      " kps ", kps_str,
					      " ", str,
					      "]",
					      NULL);
			putresult(DONE, "%s %s %d %s\n", handle, label,
				  filenum, q);
			log_add(L_CHUNKSUCCESS, "%s %s %s %d %s",
				hostname, diskname, datestamp, level, errstr);
			amfree(save_holdfile);
			amfree(holdfile_path_thischunk);
		    }
 		}
		amfree(str);

 		if(!nexting){
 		    num_splits = 0;
 		    expected_splits = 0;
 		    amfree(holdfile_name);
 		    num_holdfiles = 0;
 		    cur_span_chunkstart = 0;
 		    curdump_rt = times_zero;
 		}
		
 		amfree(q);
		
#ifdef HAVE_LIBVTBLC
		/* 
		 *  We have 44 characters available for the label string:
		 *  use max 20 characters for hostname
		 *      max 20 characters for diskname 
		 *             (it could contain a samba share or dos path)
		 *           2 for level
		 */
		memset(desc, '\0', 45);

		strncpy(desc, hostname, 20);

		if ((len = strlen(hostname)) <= 20) {
		    memset(desc + len, ' ', 1);
		    offset = len + 1;
		}
		else{
		    memset(desc + 20, ' ', 1);
		    offset = 21;
		}

		strncpy(desc + offset, diskname, 20);

		if ((len = strlen(diskname)) <= 20) {
		    memset(desc + offset + len, ' ', 1);
		    offset = offset + len + 1;
		}
		else{
		    memset(desc + offset + 20, ' ', 1);
		    offset = offset + 21;
		}

		sprintf(desc + offset, "%i", level);

	        strncpy(vol_label, desc, 44);
		fprintf(stderr, "taper: added vtbl label string %i: \"%s\"\n",
			filenum, vol_label);
		fflush(stderr);

		/* pass label string on to tape writer */
		syncpipe_put('L', filenum);
		syncpipe_putstr(vol_label);		

		/* 
		 * reformat datestamp for later use with set_date from vtblc 
		 */
		strptime(datestamp, "%Y%m%d", &backup_time);
		strftime(vol_date, 20, "%T %D", &backup_time);
		fprintf(stderr, 
			"taper: reformatted vtbl date string: \"%s\"->\"%s\"\n",
			datestamp,
			vol_date);

		/* pass date string on to tape writer */		
		syncpipe_put('D', filenum);
		syncpipe_putstr(vol_date);

#endif /* HAVE_LIBVTBLC */
	    }
	    /* reset stuff that assumes we're on a new file */

	    if(nexting){
		opening = 1;
		nexting = 0;
		closing = 0;
		filesize = 0;
		syncpipe_put('O', 0);
		syncpipe_putstr(datestamp);
		syncpipe_putstr(hostname);
		syncpipe_putstr(diskname);
		syncpipe_putint(level);
		for(bp = buftable; bp < buftable + conf_tapebufs; bp++) {
		    bp->status = EMPTY;
		}
		bp = buftable;
		header_written = 0;
		break;
	    }
	    else return 0;

	default:
	    assert(0);
	}
    }

    return 0;
}

int taper_fill_buffer(fd, bp, buflen)
int fd;
buffer_t *bp;
int buflen;
{
    char *curptr;
    int spaceleft, cnt;

    curptr = bp->buffer;
    bp->size = 0;
    spaceleft = buflen;

    cnt = fullread(fd, curptr, spaceleft);
    switch(cnt) {
    case 0:	/* eof */
	if(interactive) fputs("r0", stderr);
	return bp->size;
    case -1:	/* error on read, punt */
	if(interactive) fputs("rE", stderr);
	return -1;
    default:
	if(mode == MODE_PORT_WRITE && splitsize > 0){
	    memcpy(splitbuf_wr_ptr, curptr, (size_t)cnt);
	    splitbuf_wr_ptr += cnt;
	}
	spaceleft -= cnt;
	curptr += cnt;
	bp->size += cnt;
    }

    if(interactive) fputs("R", stderr);
    return bp->size;
}

/* Given a dumpfile in holding, determine its size and figure out how many
 * times we'd have to split it.
 */
int predict_splits(filename)
char *filename;
{
    int splits = 0;
    long total_kb = 0;
    long adj_splitsize = splitsize - DISK_BLOCK_BYTES/1024;

    if(splitsize <= 0) return(0);

    if(adj_splitsize <= 0){
      error("Split size must be > %ldk", DISK_BLOCK_BYTES/1024);
    }

    /* should only calculuate this once, not on retries etc */
    if(expected_splits != 0) return(expected_splits);

    total_kb = size_holding_files(filename, 1);
    
    if(total_kb <= 0){
      fprintf(stderr, "taper: r: %ld kb holding file makes no sense, not precalculating splits\n", total_kb);
      fflush(stderr);
      return(0);
    }

    fprintf(stderr, "taper: r: Total dump size should be %ldkb, chunk size is %ldkb\n", total_kb, splitsize);
    fflush(stderr);

    splits = total_kb/adj_splitsize;
    if(total_kb % adj_splitsize) splits++;


    fprintf(stderr, "taper: r: Expecting to split into %d parts \n", splits);
    fflush(stderr);

    return(splits);
}

/*
 * ========================================================================
 * TAPE WRITER SIDE
 *
 */
times_t idlewait, rdwait, wrwait, fmwait;
long total_writes;
double total_tape_used;
int total_tape_fm;

void write_file P((void));
int write_buffer P((buffer_t *bp));

void tape_writer_side(getp, putp)
int getp, putp;
{
    char tok;
    int tape_started;
    char *str;
    char *hostname;
    char *diskname;
    char *datestamp;
    int level;
    int tmpint;

#ifdef HAVE_LIBVTBLC
    char *vol_label;
    char *vol_date;
#endif /* HAVE_LIBVTBLC */

    procname = "writer";
    syncpipe_init(getp, putp);

    tape_started = 0;
    idlewait = times_zero;

    while(1) {
	startclock();
	tok = syncpipe_get(&tmpint);
	idlewait = timesadd(idlewait, stopclock());
	if(tok != 'S' && tok != 'Q' && !tape_started) {
	    error("writer: token '%c' before start", tok);
	}

	switch(tok) {
	case 'S':		/* start-tape */
	    if(tape_started) {
		error("writer: multiple start requests");
	    }
	    str = syncpipe_getstr();
	    if(!first_tape(str ? str : "bad-datestamp")) {
		if(tape_fd >= 0) {
		    tapefd_close(tape_fd);
		    tape_fd = -1;
		}
		syncpipe_put('E', 0);
		syncpipe_putstr(errstr);
		/* wait for reader to acknowledge error */
		do {
		    tok = syncpipe_get(&tmpint);
		    if(tok != 'e') {
			error("writer: got '%c' unexpectedly after error", tok);
		    }
		} while(tok != 'e');
	    } else {
		syncpipe_put('S', 0);
		tape_started = 1;
	    }
	    amfree(str);

	    break;

	case 'O':		/* open-output */
	    datestamp = syncpipe_getstr();
	    tapefd_setinfo_datestamp(tape_fd, datestamp);
	    amfree(datestamp);
	    hostname = syncpipe_getstr();
	    tapefd_setinfo_host(tape_fd, hostname);
	    amfree(hostname);
	    diskname = syncpipe_getstr();
	    tapefd_setinfo_disk(tape_fd, diskname);
	    amfree(diskname);
	    level = syncpipe_getint();
	    tapefd_setinfo_level(tape_fd, level);
	    write_file();
	    break;

#ifdef HAVE_LIBVTBLC
	case 'L':		/* read vtbl label */
	    vtbl_no = tmpint;
	    vol_label = syncpipe_getstr();
	    fprintf(stderr, "taper: read label string \"%s\" from pipe\n", 
		    vol_label);
	    strncpy(vtbl_entry[vtbl_no].label, vol_label, 45);
	    break;

	case 'D':		/* read vtbl date */
	    vtbl_no = tmpint;
	    vol_date = syncpipe_getstr();
	    fprintf(stderr, "taper: read date string \"%s\" from pipe\n", 
		    vol_date);
	    strncpy(vtbl_entry[vtbl_no].date, vol_date, 20);
	    break;
#endif /* HAVE_LIBVTBLC */

	case 'Q':
	    end_tape(0);	/* XXX check results of end tape ?? */
	    clear_tapelist();
	    amfree(taper_datestamp);
	    amfree(label);
	    amfree(errstr);
	    amfree(changer_resultstr);
	    amfree(tapedev);
	    amfree(conf_tapelist);
	    amfree(config_dir);
	    amfree(config_name);

	    malloc_size_2 = malloc_inuse(&malloc_hist_2);

	    if(malloc_size_1 != malloc_size_2) {
		malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
	    }

	    exit(0);

	default:
	    assert(0);
	}
    }
}

void write_file()
{
    buffer_t *bp;
    int full_buffers, i, bufnum;
    char tok;
    char number[NUM_STR_SIZE];
    char *rdwait_str, *wrwait_str, *fmwait_str;
    int tmpint;

    rdwait = wrwait = times_zero;
    total_writes = 0;

    bp = buftable;
    full_buffers = 0;
    tok = '?';

    if(bufdebug) {
	fprintf(stderr, "taper: w: start file\n");
	fflush(stderr);
    }

    /*
     * Tell the reader that the tape is open, and give it all the buffers.
     */
    syncpipe_put('O', 0);
    for(i = 0; i < conf_tapebufs; i++) {
	if(bufdebug) {
	    fprintf(stderr, "taper: w: put R%d\n", i);
	    fflush(stderr);
	}
	syncpipe_put('R', i);
    }

    /*
     * We write the filemark at the start of the file rather than at the end,
     * so that it can proceed in parallel with the reader's initial filling
     * up of the buffers.
     */

    startclock();
    if(!write_filemark())
	goto tape_error;
    fmwait = stopclock();

    filenum += 1;

    do {

	/*
	 * STOPPED MODE
	 *
	 * At the start of the file, or if the input can't keep up with the
	 * tape, we enter STOPPED mode, which waits for most of the buffers
	 * to fill up before writing to tape.  This maximizes the amount of
	 * data written in chunks to the tape drive, minimizing the number
	 * of starts/stops, which in turn saves tape and time.
	 */

	if(interactive) fputs("[WS]", stderr);
	startclock();
	while(full_buffers < conf_tapebufs - THRESHOLD) {
	    tok = syncpipe_get(&bufnum);
	    if(tok != 'W') break;
	    if(bufdebug) {
		fprintf(stderr,"taper: w: got W%d\n",bufnum);
		fflush(stderr);
	    }
	    full_buffers++;
	}
	rdwait = timesadd(rdwait, stopclock());

	/*
	 * STARTING MODE
	 *
	 * We start output when sufficient buffers have filled up, or at
	 * end-of-file, whichever comes first.  Here we drain all the buffers
	 * that were waited on in STOPPED mode.  If more full buffers come
	 * in, then we will be STREAMING.
	 */

	while(full_buffers) {
	    if(tt_file_pad && bp->size < tt_blocksize) {
		memset(bp->buffer+bp->size, 0, tt_blocksize - bp->size);
		bp->size = tt_blocksize;
	    }
	    if(!write_buffer(bp)) goto tape_error;
	    full_buffers--;
	    bp = nextbuf(bp);
	}

	/*
	 * STREAMING MODE
	 *
	 * With any luck, the input source is faster than the tape drive.  In
	 * this case, full buffers will appear in the circular queue faster
	 * than we can write them, so the next buffer in the queue will always
	 * be marked FULL by the time we get to it.  If so, we'll stay in
	 * STREAMING mode.
	 *
	 * On the other hand, if we catch up to the input and thus would have
	 * to wait for buffers to fill, we are then STOPPED again.
	 */

	while(tok == 'W' && bp->status == FULL) {
	    tok = syncpipe_get(&bufnum);
	    if(tok == 'W') {
		if(bufdebug) {
		    fprintf(stderr,"taper: w: got W%d\n",bufnum);
		    fflush(stderr);
		}
		if(bufnum != bp-buftable) {
		    fprintf(stderr,
			    "taper: tape-writer: my buf %d reader buf %d\n",
			    (int)(bp-buftable), bufnum);
		    fflush(stderr);
		    syncpipe_put('E', 0);
		    syncpipe_putstr("writer-side buffer mismatch");
		    goto error_ack;
		}
		if(tt_file_pad && bp->size < tt_blocksize) {
		    memset(bp->buffer+bp->size, 0, tt_blocksize - bp->size);
		    bp->size = tt_blocksize;
		}
		if(!write_buffer(bp)) goto tape_error;
		bp = nextbuf(bp);
	    }
	    else if(tok == 'Q')
		return;
	    else if(tok == 'X')
		goto reader_buffer_snafu;
	    else
		error("writer-side not expecting token: %c", tok);
	}
    } while(tok == 'W');

    /* got close signal from reader, acknowledge it */

    if(tok == 'X')
	goto reader_buffer_snafu;

    assert(tok == 'C');
    syncpipe_put('C', 0);

    /* tell reader the tape and file number */

    syncpipe_putstr(label);
    snprintf(number, sizeof(number), "%d", filenum);
    syncpipe_putstr(number);

    snprintf(number, sizeof(number), "%ld", total_writes);
    rdwait_str = stralloc(walltime_str(rdwait));
    wrwait_str = stralloc(walltime_str(wrwait));
    fmwait_str = stralloc(walltime_str(fmwait));
    errstr = newvstralloc(errstr,
			  "{wr:",
			  " writers ", number,
			  " rdwait ", rdwait_str,
			  " wrwait ", wrwait_str,
			  " filemark ", fmwait_str,
			  "}",
			  NULL);
    amfree(rdwait_str);
    amfree(wrwait_str);
    amfree(fmwait_str);
    syncpipe_putstr(errstr);

    /* XXX go to next tape if past tape size? */

    return;

 tape_error:
    /* got tape error */
    if(next_tape(1)) syncpipe_put('T', 0);	/* next tape in place, try again */
    else syncpipe_put('E', 0);		/* no more tapes, fail */
    syncpipe_putstr(errstr);

 error_ack:
    /* wait for reader to acknowledge error */
    do {
	tok = syncpipe_get(&tmpint);
	if(tok != 'W' && tok != 'C' && tok != 'e')
	    error("writer: got '%c' unexpectedly after error", tok);
    } while(tok != 'e');
    return;

 reader_buffer_snafu:
    syncpipe_put('x', 0);
    return;
}

int write_buffer(bp)
buffer_t *bp;
{
    int rc;

    assert(bp->status == FULL);

    startclock();
    rc = tapefd_write(tape_fd, bp->buffer, bp->size);
    if(rc == bp->size) {
#if defined(NEED_RESETOFS)
	static double tape_used_modulus_2gb = 0;

	/*
	 * If the next write will go over the 2 GByte boundary, reset
	 * the kernel concept of where we are to make sure it does not
	 * go silly on us.
	 */
	tape_used_modulus_2gb += (double)rc;
	if(tape_used_modulus_2gb + (double)rc > (double)0x7fffffff) {
	    tape_used_modulus_2gb = 0;
	    tapefd_resetofs(tape_fd);
	}
#endif
	wrwait = timesadd(wrwait, stopclock());
	total_writes += 1;
	total_tape_used += (double)rc;
	bp->status = EMPTY;
	if(interactive || bufdebug) dumpstatus(bp);
	if(interactive) fputs("W", stderr);

	if(bufdebug) {
	    fprintf(stderr, "taper: w: put R%d\n", (int)(bp-buftable));
	    fflush(stderr);
	}
	syncpipe_put('R', bp-buftable);
	return 1;
    } else {
	errstr = newvstralloc(errstr,
			      "writing file: ",
			      (rc != -1) ? "short write" : strerror(errno),
			      NULL);
	wrwait = timesadd(wrwait, stopclock());
	if(interactive) fputs("[WE]", stderr);
	return 0;
    }
}


static void 
cleanup(void)
{
    REMOVE_SHARED_MEMORY(); 
}


/*
 * Cleanup shared memory segments 
 */
static void 
signal_handler(int signum)
{
    log_add(L_INFO, "Received signal %d", signum);

    exit(1);
}


/*
 * Installing signal handlers for signal whose default action is 
 * process termination so that we can clean up shared memory
 * segments
 */
static void
install_signal_handlers(void) 
{
    struct sigaction act;

    act.sa_handler = signal_handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);

    signal(SIGPIPE, SIG_IGN);

    if (sigaction(SIGINT, &act, NULL) != 0) {
	error("taper: couldn't install SIGINT handler [%s]", strerror(errno));
    }

    if (sigaction(SIGHUP, &act, NULL) != 0) {
	error("taper: couldn't install SIGHUP handler [%s]", strerror(errno));
    }
   
    if (sigaction(SIGTERM, &act, NULL) != 0) {
	error("taper: couldn't install SIGTERM handler [%s]", strerror(errno));
    }

    if (sigaction(SIGUSR1, &act, NULL) != 0) {
	error("taper: couldn't install SIGUSR1 handler [%s]", strerror(errno));
    }

    if (sigaction(SIGUSR2, &act, NULL) != 0) {
	error("taper: couldn't install SIGUSR2 handler [%s]", strerror(errno));
    }

    if (sigaction(SIGALRM, &act, NULL) != 0) {
	error("taper: couldn't install SIGALRM handler [%s]", strerror(errno));
    }
}


/*
 * ========================================================================
 * SHARED-MEMORY BUFFER SUBSYSTEM
 *
 */

#ifdef HAVE_SYSVSHM

int shmid = -1;

char *attach_buffers(size)
    unsigned int size;
{
    char *result;

    shmid = shmget(IPC_PRIVATE, size, IPC_CREAT|0700);
    if(shmid == -1) {
	return NULL;
    }

    result = (char *)shmat(shmid, (SHM_ARG_TYPE *)NULL, 0);

    if(result == (char *)-1) {
	int save_errno = errno;

	destroy_buffers();
	errno = save_errno;
	error("shmat: %s", strerror(errno));
    }

    return result;
}


void detach_buffers(bufp)
    char *bufp;
{
    if ((bufp != NULL) &&
        (shmdt((SHM_ARG_TYPE *)bufp) == -1)) {
	error("shmdt: %s", strerror(errno));
    }
}

void destroy_buffers()
{
    if(shmid == -1) return;	/* nothing to destroy */
    if(shmctl(shmid, IPC_RMID, NULL) == -1) {
	error("shmctl: %s", strerror(errno));
    }
}

#else
#ifdef HAVE_MMAP

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#ifndef MAP_ANON
#  ifdef MAP_ANONYMOUS			/* OSF/1-style */
#    define MAP_ANON MAP_ANONYMOUS
#  else					/* SunOS4-style */
#    define MAP_ANON 0
#    define ZERO_FILE "/dev/zero"
#  endif
#endif

int shmfd = -1;
unsigned int saved_size;

char *attach_buffers(size)
    unsigned int size;
{
    char *shmbuf;

#ifdef ZERO_FILE
    shmfd = open(ZERO_FILE, O_RDWR);
    if(shmfd == -1) {
	error("attach_buffers: could not open %s: %s",
	      ZERO_FILE,
	      strerror(errno));
    }
#endif

    saved_size = size;
    shmbuf = (char *) mmap((void *) 0,
			   size,
			   PROT_READ|PROT_WRITE,
			   MAP_ANON|MAP_SHARED,
			   shmfd, 0);

    return shmbuf;
}

void detach_buffers(bufp)
char *bufp;
{
    if ((bufp != NULL) && 
	(munmap((void *)bufp, saved_size) == -1)) {
	error("detach_buffers: munmap: %s", strerror(errno));
    }

    if (shmfd != -1)
	aclose(shmfd);
}

void destroy_buffers()
{
}

#else
#error: must define either HAVE_SYSVSHM or HAVE_MMAP!
#endif
#endif



/*
 * ========================================================================
 * SYNC-PIPE SUBSYSTEM
 *
 */

int getpipe, putpipe;

void syncpipe_init(rd, wr)
int rd, wr;
{
    getpipe = rd;
    putpipe = wr;
}

char syncpipe_get(intp)
int *intp;
{
    int rc;
    char buf[sizeof(char) + sizeof(int)];

    rc = fullread(getpipe, buf, sizeof(buf));
    if(rc == 0)		/* EOF */
	error("syncpipe_get: %c: unexpected EOF", *procname);
    else if(rc < 0)
	error("syncpipe_get: %c: %s", *procname, strerror(errno));
    else if(rc != sizeof(buf))
	error("syncpipe_get: %s", "short read");

    if(bufdebug && *buf != 'R' && *buf != 'W') {
	fprintf(stderr,"taper: %c: getc %c\n",*procname,*buf);
	fflush(stderr);
    }

    memcpy(intp, &buf[1], sizeof(int));
    return buf[0];
}

int syncpipe_getint()
{
    int rc, i;

    if ((rc = fullread(getpipe, &i, sizeof(i))) != sizeof(i))
	error("syncpipe_getint: %s", rc < 0 ? strerror(errno) : "short read");

    return (i);
}


char *syncpipe_getstr()
{
    int rc, len;
    char *str;

    if((len = syncpipe_getint()) <= 0) {
	error("syncpipe_getstr: Protocol error - Invalid length (%d)", len);
	/* NOTREACHED */
    }

    str = alloc(len);

    if ((rc = fullread(getpipe, str, len)) != len) {
	error("syncpipe_getstr: %s", rc < 0 ? strerror(errno) : "short read");
	/* NOTREACHED */
    }

    return (str);
}


void syncpipe_put(chi, intval)
int chi;
int intval;
{
    char buf[sizeof(char) + sizeof(int)];

    buf[0] = (char)chi;
    memcpy(&buf[1], &intval, sizeof(int));
    if(bufdebug && buf[0] != 'R' && buf[0] != 'W') {
	fprintf(stderr,"taper: %c: putc %c\n",*procname,buf[0]);
	fflush(stderr);
    }

    if (fullwrite(putpipe, buf, sizeof(buf)) < 0)
	error("syncpipe_put: %s", strerror(errno));
}

void syncpipe_putint(i)
int i;
{

    if (fullwrite(putpipe, &i, sizeof(i)) < 0)
	error("syncpipe_putint: %s", strerror(errno));
}

void syncpipe_putstr(str)
const char *str;
{
    int n;

    n = strlen(str)+1;				/* send '\0' as well */
    syncpipe_putint(n);
    if (fullwrite(putpipe, str, n) < 0)
	error("syncpipe_putstr: %s", strerror(errno));
}


/*
 * ========================================================================
 * TAPE MANIPULATION SUBSYSTEM
 *
 */

/* local functions */
int label_tape P((void));

int label_tape()
{  
    char *conf_tapelist_old = NULL;
    char *result;
    static int first_call = 1;
    char *timestamp;
    char *error_msg;

    if (taper_scan(NULL, &label, &timestamp, &error_msg, &tapedev) < 0) {
        fprintf(stderr, "%s\n", error_msg);
	errstr = newstralloc(errstr, error_msg);
        amfree(error_msg);
        amfree(timestamp);
	return 0;
    }
    
    if((tape_fd = tape_open(tapedev, O_WRONLY)) == -1) {
	if(errno == EACCES) {
	    errstr = newstralloc(errstr,
				 "writing label: tape is write protected");
	} else {
	    errstr = newstralloc2(errstr,
				  "writing label: ", strerror(errno));
	}
	return 0;
    }

    tapefd_setinfo_length(tape_fd, tt->length);

    tapefd_setinfo_datestamp(tape_fd, taper_datestamp);
    tapefd_setinfo_disk(tape_fd, label);
    result = tapefd_wrlabel(tape_fd, taper_datestamp, label, tt_blocksize);
    if(result != NULL) {
	errstr = newstralloc(errstr, result);
	return 0;
    }

    fprintf(stderr, "taper: wrote label `%s' date `%s'\n", label, taper_datestamp);
    fflush(stderr);

#ifdef HAVE_LIBVTBLC
    /* store time for the first volume entry */
    time(&raw_time);
    tape_timep = localtime(&raw_time);
    strftime(start_datestr, 20, "%T %D", tape_timep);
    fprintf(stderr, "taper: got vtbl start time: %s\n", start_datestr);
    fflush(stderr);
#endif /* HAVE_LIBVTBLC */

    if (strcmp(label, FAKE_LABEL) != 0) {

	if(cur_tape == 0) {
	    conf_tapelist_old = stralloc2(conf_tapelist, ".yesterday");
	} else {
	    char cur_str[NUM_STR_SIZE];

	    snprintf(cur_str, sizeof(cur_str), "%d", cur_tape - 1);
	    conf_tapelist_old = vstralloc(conf_tapelist,
					  ".today.", cur_str, NULL);
	}
	if(write_tapelist(conf_tapelist_old)) {
	    error("could not write tapelist: %s", strerror(errno));
	}
	amfree(conf_tapelist_old);

	remove_tapelabel(label);
	add_tapelabel(atoi(taper_datestamp), label);
	if(write_tapelist(conf_tapelist)) {
	    error("could not write tapelist: %s", strerror(errno));
	}
    }

    log_add(L_START, "datestamp %s label %s tape %d",
	    taper_datestamp, label, cur_tape);
    if (first_call && strcmp(label, FAKE_LABEL) == 0) {
	first_call = 0;
	log_add(L_WARNING, "tapedev is %s, dumps will be thrown away", tapedev);
    }

    total_tape_used=0.0;
    total_tape_fm = 0;

    return 1;
}

int first_tape(new_datestamp)
char *new_datestamp;
{
    if((have_changer = changer_init()) < 0) {
	error("changer initialization failed: %s", strerror(errno));
    }
    changer_debug = 1;

    taper_datestamp = newstralloc(taper_datestamp, new_datestamp);

    if(!label_tape())
	return 0;

    filenum = 0;
    return 1;
}

int next_tape(writerror)
int writerror;
{
    end_tape(writerror);

    if(++cur_tape >= runtapes)
	return 0;

    if(!label_tape()) {
	return 0;
    }

    filenum = 0;
    return 1;
}


int end_tape(writerror)
int writerror;
{
    char *result;
    int rc = 0;

    if(tape_fd >= 0) {
	log_add(L_INFO, "tape %s kb %ld fm %d %s", 
		label,
		(long) ((total_tape_used+1023.0) / 1024.0),
		total_tape_fm,
		writerror? errstr : "[OK]");

	fprintf(stderr, "taper: writing end marker. [%s %s kb %ld fm %d]\n",
		label,
		writerror? "ERR" : "OK",
		(long) ((total_tape_used+1023.0) / 1024.0),
		total_tape_fm);
	fflush(stderr);
	if(! writerror) {
	    if(! write_filemark()) {
		rc = 1;
		goto common_exit;
	    }

	    result = tapefd_wrendmark(tape_fd, taper_datestamp, tt_blocksize);
	    if(result != NULL) {
		errstr = newstralloc(errstr, result);
		rc = 1;
		goto common_exit;
	    }
	}
    }

#ifdef HAVE_LINUX_ZFTAPE_H
    if (tape_fd >= 0 && is_zftape(tapedev) == 1) {
	/* rewind the tape */

	if(tapefd_rewind(tape_fd) == -1 ) {
	    errstr = newstralloc2(errstr, "rewinding tape: ", strerror(errno));
	    rc = 1;
	    goto common_exit;
	}
	/* close the tape */

	if(tapefd_close(tape_fd) == -1) {
	    errstr = newstralloc2(errstr, "closing tape: ", strerror(errno));
	    rc = 1;
	    goto common_exit;
	}
	tape_fd = -1;

#ifdef HAVE_LIBVTBLC
	/* update volume table */
	fprintf(stderr, "taper: updating volume table ...\n");
	fflush(stderr);
    
	if ((tape_fd = raw_tape_open(rawtapedev, O_RDWR)) == -1) {
	    if(errno == EACCES) {
		errstr = newstralloc(errstr,
				     "updating volume table: tape is write protected");
	    } else {
		errstr = newstralloc2(errstr,
				      "updating volume table: ", 
				      strerror(errno));
	    }
	    rc = 1;
	    goto common_exit;
	}
	/* read volume table */
	if ((num_volumes = read_vtbl(tape_fd, volumes, vtbl_buffer,
				     &first_seg, &last_seg)) == -1 ) {
	    errstr = newstralloc2(errstr,
				  "reading volume table: ", 
				  strerror(errno));
	    rc = 1;
	    goto common_exit;
	}
	/* set volume label and date for first entry */
	vtbl_no = 0;
	if(set_label(label, volumes, num_volumes, vtbl_no)){
	    errstr = newstralloc2(errstr,
				  "setting label for entry 1: ",
				  strerror(errno));
	    rc = 1;
	    goto common_exit;
	}
	/* date of start writing this tape */
	if (set_date(start_datestr, volumes, num_volumes, vtbl_no)){
	    errstr = newstralloc2(errstr,
				  "setting date for entry 1: ", 
				  strerror(errno));
	    rc = 1;
	    goto common_exit;
	}
	/* set volume labels and dates for backup files */
	for (vtbl_no = 1; vtbl_no <= num_volumes - 2; vtbl_no++){ 
	    fprintf(stderr,"taper: label %i: %s, date %s\n", 
		    vtbl_no,
		    vtbl_entry[vtbl_no].label,
		    vtbl_entry[vtbl_no].date);
	    fflush(stderr);
	    if(set_label(vtbl_entry[vtbl_no].label, 
			 volumes, num_volumes, vtbl_no)){
		errstr = newstralloc2(errstr,
				      "setting label for entry i: ", 
				      strerror(errno));
		rc = 1;
		goto common_exit;
	    }
	    if(set_date(vtbl_entry[vtbl_no].date, 
			volumes, num_volumes, vtbl_no)){
		errstr = newstralloc2(errstr,
				      "setting date for entry i: ",
				      strerror(errno));
		rc = 1;
		goto common_exit;
	    }
	}
	/* set volume label and date for last entry */
	vtbl_no = num_volumes - 1;
	if(set_label("AMANDA Tape End", volumes, num_volumes, vtbl_no)){
	    errstr = newstralloc2(errstr,
				  "setting label for last entry: ", 
				  strerror(errno));
	    rc = 1;
	    goto common_exit;
	}
	datestr = NULL; /* take current time */ 
	if (set_date(datestr, volumes, num_volumes, vtbl_no)){
	    errstr = newstralloc2(errstr,
				  "setting date for last entry 1: ", 
				  strerror(errno));
	    rc = 1;
	    goto common_exit;
	}
	/* write volume table back */
	if (write_vtbl(tape_fd, volumes, vtbl_buffer, num_volumes, first_seg,
		       op_mode == trunc)) {
	    errstr = newstralloc2(errstr,
				  "writing volume table: ", 
				  strerror(errno));
	    rc = 1;
	    goto common_exit;
	}  

	fprintf(stderr, "taper: updating volume table: done.\n");
	fflush(stderr);
#endif /* HAVE_LIBVTBLC */
    }
#endif /* !HAVE_LINUX_ZFTAPE_H */

    /* close the tape and let the OS write the final filemarks */

common_exit:

    if(tape_fd >= 0 && tapefd_close(tape_fd) == -1 && ! writerror) {
	errstr = newstralloc2(errstr, "closing tape: ", strerror(errno));
	rc = 1;
    }
    tape_fd = -1;
    amfree(label);

    return rc;
}


int write_filemark()
{
    if(tapefd_weof(tape_fd, 1) == -1) {
	errstr = newstralloc2(errstr, "writing filemark: ", strerror(errno));
	return 0;
    }
    total_tape_fm++;
    return 1;
}


