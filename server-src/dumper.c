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
/* $Id: dumper.c,v 1.75.2.14.2.5 2001/09/17 22:18:08 jrjackson Exp $
 *
 * requests remote amandad processes to dump filesystems
 */
#include "amanda.h"
#include "amindex.h"
#include "arglist.h"
#include "clock.h"
#include "conffile.h"
#include "logfile.h"
#include "protocol.h"
#include "stream.h"
#include "token.h"
#include "version.h"
#include "fileheader.h"
#include "server_util.h"

#ifdef KRB4_SECURITY
#include "dumper-krb4.c"
#else
#define NAUGHTY_BITS_INITIALIZE		/* I'd tell you what these do */
#define NAUGHTY_BITS			/* but then I'd have to kill you */
#endif


#ifndef SEEK_SET
#define SEEK_SET 0
#endif

#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif

#define CONNECT_TIMEOUT	5*60
#define MAX_ARGS	11
#define MESGBUF_SIZE	4*1024

#define STARTUP_TIMEOUT 60

typedef enum { BOGUS, FILE_DUMP, PORT_DUMP, CONTINUE, ABORT, QUIT } cmd_t;

char *argv[MAX_ARGS+1];
int argc;
int interactive;
char *handle = NULL;

char databuf[DISK_BLOCK_BYTES];
char mesgbuf[MESGBUF_SIZE+1];
char *errstr = NULL;
char *datain;				/* where to read in data */
char *dataout;				/* where to write out data */
char *datalimit;			/* end of the data area */
int abort_pending;
long dumpsize;				/* total size of dump */
long dumpbytes;
long origsize;
long filesize;				/* size of current holding disk file */
int nb_header_block;
static enum { srvcomp_none, srvcomp_fast, srvcomp_best } srvcompress;

static FILE *errf = NULL;
char *filename = NULL;			/* holding disk base file name */
string_t cont_filename;
char *hostname = NULL;
char *diskname = NULL;
char *options = NULL;
char *progname = NULL;
int level;
char *dumpdate = NULL;
long chunksize;
long use;				/* space remaining in this hold disk */
char *datestamp;
char *backup_name = NULL;
char *recover_cmd = NULL;
char *compress_suffix = NULL;
char *config_name = NULL;
char *config_dir = NULL;
int conf_dtimeout;

dumpfile_t file;
int filename_seq;
long split_size;			/* next dumpsize we will split at */

int datafd = -1;
int mesgfd = -1;
int indexfd = -1;
int amanda_port;

/* local functions */
int main P((int main_argc, char **main_argv));
static cmd_t getcmd P((void));
static void putresult P((char *format, ...))
    __attribute__ ((format (printf, 1, 2)));
int do_dump P((int mesgfd, int datafd, int indexfd, int outfd));
void check_options P((char *options));
void service_ports_init P((void));
int write_tapeheader P((int outfd, dumpfile_t *type));
int write_dataptr P((int outf));
int update_dataptr P((int *outf, int size));
static void process_dumpeof P((void));
static void process_dumpline P((char *str));
static void add_msg_data P((char *str, int len));
static void log_msgout P((logtype_t typ));
void sendbackup_response P((proto_t *p, pkt_t *pkt));
int startup_dump P((char *hostname, char *disk, int level, char *dumpdate,
		    char *progname, char *options));


void check_options(options)
char *options;
{
#ifdef KRB4_SECURITY
    krb4_auth = strstr(options, "krb4-auth;") != NULL;
    kencrypt = strstr(options, "kencrypt;") != NULL;
#endif
    if (strstr(options, "srvcomp-best;") != NULL)
      srvcompress = srvcomp_best;
    else if (strstr(options, "srvcomp-fast;") != NULL)
      srvcompress = srvcomp_fast;
    else
      srvcompress = srvcomp_none;
}

void service_ports_init()
{
    struct servent *amandad;

    if((amandad = getservbyname(AMANDA_SERVICE_NAME, "udp")) == NULL) {
	amanda_port = AMANDA_SERVICE_DEFAULT;
	log_add(L_WARNING, "no %s/udp service, using default port %d",
	        AMANDA_SERVICE_NAME, AMANDA_SERVICE_DEFAULT);
    }
    else
	amanda_port = ntohs(amandad->s_port);

#ifdef KRB4_SECURITY
    if((amandad = getservbyname(KAMANDA_SERVICE_NAME, "udp")) == NULL) {
	kamanda_port = KAMANDA_SERVICE_DEFAULT;
	log_add(L_WARNING, "no %s/udp service, using default port %d",
	        KAMANDA_SERVICE_NAME, KAMANDA_SERVICE_DEFAULT);
    }
    else
	kamanda_port = ntohs(amandad->s_port);
#endif
}


int main(main_argc, main_argv)
int main_argc;
char **main_argv;
{
    cmd_t cmd;
    int outfd, protocol_port, taper_port, rc;
    dgram_t *msg;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    char *conffile;
    char *q = NULL;
    int fd;
    char *tmp_filename = NULL;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    set_pname("dumper");

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

    /* set up dgram port first thing */

    msg = dgram_alloc();
    if(dgram_bind(msg, &protocol_port) == -1)
	error("could not bind result datagram port: %s", strerror(errno));

    if(geteuid() == 0) {
	/* set both real and effective uid's to real uid, likewise for gid */
	setgid(getgid());
	setuid(getuid());
    }
#ifdef BSD_SECURITY
    else error("must be run setuid root to communicate correctly");
#endif

    fprintf(stderr,
	    "%s: pid %ld executable %s version %s, using port %d\n",
	    get_pname(), (long) getpid(),
	    main_argv[0], version(), protocol_port);
    fflush(stderr);

    /* now, make sure we are a valid user */

    if(getpwuid(getuid()) == NULL)
	error("can't get login name for my uid %ld", (long)getuid());

    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    interactive = isatty(0);

    amfree(datestamp);
    datestamp = construct_datestamp(NULL);
    conf_dtimeout = getconf_int(CNF_DTIMEOUT);

    service_ports_init();
    proto_init(msg->socket, time(0), 16);

    do {
	cmd = getcmd();

	switch(cmd) {
	case QUIT:
	    break;
	case FILE_DUMP:
	    /*
	     * FILE-DUMP handle filename host disk level dumpdate chunksize
	     *   progname use options
	     */
	    if(argc != 11) {
		error("error [dumper FILE-DUMP argc != 11: %d]", argc);
	    }
	    handle = newstralloc(handle, argv[2]);
	    filename = newstralloc(filename, argv[3]);
	    hostname = newstralloc(hostname, argv[4]);
	    diskname = newstralloc(diskname, argv[5]);
	    level = atoi(argv[6]);
	    dumpdate = newstralloc(dumpdate, argv[7]);
	    chunksize = am_floor(atoi(argv[8]), DISK_BLOCK_KB);
	    progname = newstralloc(progname, argv[9]);
	    use = am_floor(atoi(argv[10]), DISK_BLOCK_KB);
	    options = newstralloc(options, argv[11]);
	    cont_filename[0] = '\0';

	    tmp_filename = newvstralloc(tmp_filename, filename, ".tmp", NULL);
	    outfd = open(tmp_filename, O_RDWR|O_CREAT|O_TRUNC, 0600);
	    if(outfd == -1) {
		int save_errno = errno;
		q = squotef("[main holding file \"%s\": %s]",
			    tmp_filename, strerror(errno));
		if(save_errno == ENOSPC) {
		    putresult("NO-ROOM %s %lu\n", handle, use);
		    putresult("TRY-AGAIN %s %s\n", handle, q);
		}
		else {
		    putresult("FAILED %s %s\n", handle, q);
		}
		amfree(q);
		break;
	    }
	    filename_seq = 1;

	    check_options(options);

	    rc = startup_dump(hostname, diskname, level, dumpdate, progname, options);
	    if(rc) {
		q = squote(errstr);
		putresult("%s %s %s\n", rc == 2? "FAILED" : "TRY-AGAIN",
			  handle, q);
		if(rc == 2) {
		    log_add(L_FAIL, "%s %s %d [%s]", hostname, diskname, level, errstr);
		}
		amfree(q);
		/* do need to close if TRY-AGAIN, doesn't hurt otherwise */
		if (mesgfd != -1)
		    aclose(mesgfd);
		if (datafd != -1)
		    aclose(datafd);
		if (indexfd != -1)
		    aclose(indexfd);
		if (outfd != -1)
		    aclose(outfd);
		break;
	    }

	    abort_pending = 0;
	    split_size = (chunksize>use)?use:chunksize;
	    use -= split_size;
	    if(do_dump(mesgfd, datafd, indexfd, outfd)) {
	    }
	    aclose(mesgfd);
	    aclose(datafd);
	    if (indexfd != -1)
		aclose(indexfd);
	    aclose(outfd);
	    if(abort_pending) putresult("ABORT-FINISHED %s\n", handle);
	    break;

	case PORT_DUMP:
	    /*
	     * PORT-DUMP handle port host disk level dumpdate progname options
	     */
	    if(argc != 9) {
		error("error [dumper PORT-DUMP argc != 9: %d]", argc);
	    }
	    handle = newstralloc(handle, argv[2]);
	    taper_port = atoi(argv[3]);
	    filename = newstralloc(filename, "<taper program>");
	    hostname = newstralloc(hostname, argv[4]);
	    diskname = newstralloc(diskname, argv[5]);
	    level = atoi(argv[6]);
	    dumpdate = newstralloc(dumpdate, argv[7]);
	    progname = newstralloc(progname, argv[8]);
	    options = newstralloc(options, argv[9]);
	    cont_filename[0] = '\0';

	    /* connect outf to taper port */

	    outfd = stream_client("localhost", taper_port,
				  STREAM_BUFSIZE, -1, NULL);
	    if(outfd == -1) {
		q = squotef("[taper port open: %s]", strerror(errno));
		putresult("FAILED %s %s\n", handle, q);
		amfree(q);
		break;
	    }
	    filename_seq = 1;

	    check_options(options);

	    rc = startup_dump(hostname, diskname, level, dumpdate, progname, options);
	    if(rc) {
		q = squote(errstr);
		putresult("%s %s %s\n", rc == 2? "FAILED" : "TRY-AGAIN",
			  handle, q);
		if(rc == 2) {
		    log_add(L_FAIL, "%s %s %d [%s]", hostname, diskname, level, errstr);
		}
		amfree(q);
		/* do need to close if TRY-AGAIN, doesn't hurt otherwise */
		if (mesgfd != -1)
		    aclose(mesgfd);
		if (datafd != -1)
		    aclose(datafd);
		if (indexfd != -1)
		    aclose(indexfd);
		if (outfd != -1)
		    aclose(outfd);
		break;
	    }

	    abort_pending = 0;
	    split_size = -1;
	    if(do_dump(mesgfd, datafd, indexfd, outfd)) {
	    }
	    aclose(mesgfd);
	    aclose(datafd);
	    if (indexfd != -1)
		aclose(indexfd);
	    aclose(outfd);
	    if(abort_pending) putresult("ABORT-FINISHED %s\n", handle);
	    break;

	default:
	    q = squote(argv[1]);
	    putresult("BAD-COMMAND %s\n", q);
	    amfree(q);
	}
	while(wait(NULL) != -1);
    } while(cmd != QUIT);

    amfree(errstr);
    amfree(msg);
    amfree(datestamp);
    amfree(backup_name);
    amfree(recover_cmd);
    amfree(compress_suffix);
    amfree(handle);
    amfree(filename);
    amfree(hostname);
    amfree(diskname);
    amfree(dumpdate);
    amfree(progname);
    amfree(options);
    amfree(config_dir);
    amfree(config_name);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    return 0;
}

static cmd_t getcmd()
{
    char *line;

    if(interactive) {
	printf("%s> ", get_pname());
	fflush(stdout);
    }

    if((line = agets(stdin)) == NULL) {
	return QUIT;
    }

    argc = split(line, argv, sizeof(argv) / sizeof(argv[0]), " ");
    amfree(line);

#if DEBUG
    {
      int arg;
      printf("argc = %d\n", argc);
      for(arg = 0; arg < sizeof(argv) / sizeof(argv[0]); arg++)
	printf("argv[%d] = \"%s\"\n", arg, argv[arg]);
    }
#endif

    /* not enough commands for a table lookup */

    if(argc < 1) return BOGUS;
    if(strcmp(argv[1],"FILE-DUMP") == 0) return FILE_DUMP;
    if(strcmp(argv[1],"PORT-DUMP") == 0) return PORT_DUMP;
    if(strcmp(argv[1],"CONTINUE") == 0) return CONTINUE;
    if(strcmp(argv[1],"ABORT") == 0) return ABORT;
    if(strcmp(argv[1],"QUIT") == 0) return QUIT;
    return BOGUS;
}


arglist_function(static void putresult, char *, format)
{
    va_list argp;

    arglist_start(argp, format);
    vprintf(format, argp);
    fflush(stdout);
    arglist_end(argp);
}


int write_dataptr(outf)
int outf;
{
    int w;
    int written;

    written = w = 0;
    while(dataout < datain) {
	if((w = write(outf, dataout, datain - dataout)) < 0) {
	    break;
	}
	dataout += w;
	written += w;
    }
    dumpbytes += written;
    dumpsize += (dumpbytes / 1024);
    filesize += (dumpbytes / 1024);
    dumpbytes %= 1024;
    if(w < 0) {
	if(errno != ENOSPC) {
	    errstr = squotef("data write: %s", strerror(errno));
	    return 1;
	}
	/*
	 * NO-ROOM is informational only.  Later, RQ_MORE_DISK will be
	 * issued to use another holding disk.
	 */
	putresult("NO-ROOM %s %lu\n", handle, use+split_size-dumpsize);
	use = 0;				/* force RQ_MORE_DISK */
	split_size = dumpsize;
    }
    if(dataout == datain) {
	/*
	 * We flushed the whole buffer so reset to use it all.
	 */
	dataout = datain = databuf;
    }
    return 0;
}

int update_dataptr(p_outfd, size)
int *p_outfd, size;
/*
 * Updates the buffer pointer for the input data buffer.  The buffer is
 * written if it is full or we are at EOF.
 */
{
    int outfd = *p_outfd;
    int rc = 0;
    char *new_filename = NULL;
    char *tmp_filename = NULL;
    char sequence[NUM_STR_SIZE];
    int new_outfd = -1;
    cmd_t cmd;
    int save_type;
    long left_in_chunk;

    datain += size;

    while((size == 0 && dataout < datain) || datain >= datalimit) {

	NAUGHTY_BITS;

	while(size > 0 && split_size > 0 && dumpsize >= split_size) {
	    amfree(new_filename);
	    if(use == 0) {
		/*
		 * Probably no more space on this disk.  Request more.
		 */
                putresult("RQ-MORE-DISK %s\n", handle);
                cmd = getcmd();
                if(cmd == CONTINUE) {
		    /* CONTINUE filename chunksize use */
		    chunksize = am_floor(atoi(argv[3]), DISK_BLOCK_KB);
                    use = atoi(argv[4]);
		    if(strcmp(filename, argv[2]) == 0) {
			/*
			 * Same disk, so use what room is left up to the
			 * next chunk boundary or the amount we were given,
			 * whichever is less.
			 */
			left_in_chunk = chunksize - filesize;
			if(left_in_chunk > use) {
			    split_size += use;
			    use = 0;
			} else {
			    split_size += left_in_chunk;
			    use -= left_in_chunk;
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
			filename = newstralloc(filename, argv[2]);
		    }
                } else if(cmd == ABORT) {
                    abort_pending = 1;
                    errstr = newstralloc(errstr, "ERROR");
                    rc = 1;
		    goto common_exit;
		} else {
                    error("error [bad command after RQ-MORE-DISK: %d]", cmd);
                }
	    }

	    ap_snprintf(sequence, sizeof(sequence), "%d", filename_seq);
	    new_filename = newvstralloc(new_filename,
					filename,
					".",
					sequence,
					NULL);
	    tmp_filename = newvstralloc(tmp_filename,
					new_filename,
					".tmp",
					NULL);
	    new_outfd = open(tmp_filename, O_RDWR|O_CREAT|O_TRUNC, 0600);
	    if(new_outfd == -1) {
		int save_errno = errno;

		errstr = squotef("creating chunk holding file \"%s\": %s",
		 		 tmp_filename,
				 strerror(save_errno));
		if(save_errno == ENOSPC) {
		    putresult("NO-ROOM %s %lu\n",
			      handle, 
			      use + split_size - dumpsize);
		    use = 0;		/* force RQ_MORE_DISK */
		    split_size = dumpsize;
		    continue;
		}
		aclose(outfd);
		rc = 1;
		goto common_exit;
	    }
	    save_type = file.type;
	    file.type = F_CONT_DUMPFILE;
	    file.cont_filename[0] = '\0';
	    if(write_tapeheader(new_outfd, &file)) {
		int save_errno = errno;

		aclose(new_outfd);
		unlink(tmp_filename);
		if(save_errno == ENOSPC) {
		    putresult("NO-ROOM %s %lu\n",
			      handle, 
			      use + split_size - dumpsize);
		    use = 0;			/* force RQ_MORE_DISK */
		    split_size = dumpsize;
		    continue;
		}
		errstr = squotef("write_tapeheader file \"%s\": %s",
				 tmp_filename, strerror(errno));
		rc = 1;
		goto common_exit;
	    }
	    if(lseek(outfd, (off_t)0, SEEK_SET) != 0) {
		errstr = squotef("cannot lseek: %s", strerror(errno));
		aclose(new_outfd);
		unlink(tmp_filename);
		rc = 1;
		goto common_exit;
	    }
	    strncpy(file.cont_filename, new_filename, 
		    sizeof(file.cont_filename));
	    file.cont_filename[sizeof(file.cont_filename)-1] = '\0';
	    file.type = save_type;
	    if(write_tapeheader(outfd, &file)) {
		errstr = squotef("write_tapeheader file linked to \"%s\": %s",
				 tmp_filename, strerror(errno));
		aclose(new_outfd);
		unlink(tmp_filename);
		rc = 1;
		goto common_exit;
	    }
	    file.type = F_CONT_DUMPFILE;
	    strncpy(cont_filename, new_filename, sizeof(cont_filename));
	    cont_filename[sizeof(cont_filename)-1] = '\0';

	    aclose(outfd);
	    *p_outfd = outfd = new_outfd;
	    new_outfd = -1;

	    dumpsize += DISK_BLOCK_KB;
	    filesize = DISK_BLOCK_KB;
	    split_size += (chunksize>use)?use:chunksize;
	    use = (chunksize>use)?0:use-chunksize;
	    nb_header_block++;
	    filename_seq++;
	}
	rc = write_dataptr(outfd);
    }

common_exit:

    amfree(new_filename);
    amfree(tmp_filename);
    return rc;
}


static char *msgbuf = NULL;
int got_info_endline;
int got_sizeline;
int got_endline;
int dump_result;
#define max(a,b) ((a)>(b)?(a):(b))

static void process_dumpeof()
{
    /* process any partial line in msgbuf? !!! */
    if(msgbuf != NULL) {
	fprintf(errf,"? %s: error [partial line in msgbuf: %ld bytes]\n",
		get_pname(), (long) strlen(msgbuf));
	fprintf(errf,"? %s: error [partial line in msgbuf: \"%s\"]\n",
		get_pname(), msgbuf);
    }
    if(!got_sizeline && dump_result < 2) {
	/* make a note if there isn't already a failure */
	fprintf(errf,"? %s: strange [missing size line from sendbackup]\n",
		get_pname());
	dump_result = max(dump_result, 1);
    }

    if(!got_endline && dump_result < 2) {
	fprintf(errf,"? %s: strange [missing end line from sendbackup]\n",
		get_pname());
	dump_result = max(dump_result, 1);
    }
}

/* Parse an information line from the client.
** We ignore unknown parameters and only remember the last
** of any duplicates.
*/
static void parse_info_line(str)
char *str;
{
    if(strcmp(str, "end") == 0) {
	got_info_endline = 1;
	return;
    }

#define sc "BACKUP="
    if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	backup_name = newstralloc(backup_name, str + sizeof(sc)-1);
	return;
    }
#undef sc

#define sc "RECOVER_CMD="
    if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	recover_cmd = newstralloc(recover_cmd, str + sizeof(sc)-1);
	return;
    }
#undef sc

#define sc "COMPRESS_SUFFIX="
    if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	compress_suffix = newstralloc(compress_suffix, str + sizeof(sc)-1);
	return;
    }
#undef sc
}

static void process_dumpline(str)
char *str;
{
    char *s, *fp;
    int ch;

    s = str;
    ch = *s++;

    switch(ch) {
    case '|':
	/* normal backup output line */
	break;
    case '?':
	/* sendbackup detected something strange */
	dump_result = max(dump_result, 1);
	break;
    case 's':
	/* a sendbackup line, just check them all since there are only 5 */
#define sc "sendbackup: start"
	if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	    break;
	}
#undef sc
#define sc "sendbackup: size"
	if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	    s += sizeof(sc)-1;
	    ch = s[-1];
	    skip_whitespace(s, ch);
	    if(ch) {
		origsize = (long)atof(str + sizeof(sc)-1);
		got_sizeline = 1;
		break;
	    }
	}
#undef sc
#define sc "sendbackup: end"
	if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	    got_endline = 1;
	    break;
	}
#undef sc
#define sc "sendbackup: error"
	if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	    s += sizeof(sc)-1;
	    ch = s[-1];
#undef sc
	    got_endline = 1;
	    dump_result = max(dump_result, 2);
	    skip_whitespace(s, ch);
	    if(ch == '\0' || ch != '[') {
		errstr = newvstralloc(errstr,
				      "bad remote error: ", str,
				      NULL);
	    } else {
		ch = *s++;
		fp = s - 1;
		while(ch && ch != ']') ch = *s++;
		s[-1] = '\0';
		errstr = newstralloc(errstr, fp);
		s[-1] = ch;
	    }
	    break;
	}
#define sc "sendbackup: info"
	if(strncmp(str, sc, sizeof(sc)-1) == 0) {
	    s += sizeof(sc)-1;
	    ch = s[-1];
	    skip_whitespace(s, ch);
	    parse_info_line(s - 1);
	    break;
	}
#undef sc
	/* else we fall through to bad line */
    default:
	fprintf(errf, "??%s", str);
	dump_result = max(dump_result, 1);
	return;
    }
    fprintf(errf, "%s\n", str);
}

static void add_msg_data(str, len)
char *str;
int len;
{
    char *t;
    char *nl;

    while(len > 0) {
	if((nl = strchr(str, '\n')) != NULL) {
	    *nl = '\0';
	}
	if(msgbuf) {
	    t = stralloc2(msgbuf, str);
	    amfree(msgbuf);
	    msgbuf = t;
	} else if(nl == NULL) {
	    msgbuf = stralloc(str);
	} else {
	    msgbuf = str;
	}
	if(nl == NULL) break;
	process_dumpline(msgbuf);
	if(msgbuf != str) free(msgbuf);
	msgbuf = NULL;
	len -= nl + 1 - str;
	str = nl + 1;
    }
}


static void log_msgout(typ)
logtype_t typ;
{
    char *line = NULL;

    fflush(errf);
    (void) fseek(errf, 0L, SEEK_SET);
    for(; (line = agets(errf)) != NULL; free(line)) {
	log_add(typ, "%s", line);
    }
    afclose(errf);
}

/* ------------- */

void make_tapeheader(file, type)
dumpfile_t *file;
filetype_t type;
{
    fh_init(file);
    file->type = type;
    strncpy(file->datestamp  , datestamp  , sizeof(file->datestamp)-1);
    file->datestamp[sizeof(file->datestamp)-1] = '\0';
    strncpy(file->name       , hostname   , sizeof(file->name)-1);
    file->name[sizeof(file->name)-1] = '\0';
    strncpy(file->disk       , diskname   , sizeof(file->disk)-1);
    file->disk[sizeof(file->disk)-1] = '\0';
    file->dumplevel = level;
    strncpy(file->program    , backup_name, sizeof(file->program)-1);
    file->program[sizeof(file->program)-1] = '\0';
    strncpy(file->recover_cmd, recover_cmd, sizeof(file->recover_cmd)-1);
    file->recover_cmd[sizeof(file->recover_cmd)-1] = '\0';

    if (srvcompress) {
	file->compressed=1;
	ap_snprintf(file->uncompress_cmd, sizeof(file->uncompress_cmd),
		    " %s %s |", UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
		    UNCOMPRESS_OPT
#else
		    ""
#endif
		    );
	strncpy(file->comp_suffix, COMPRESS_SUFFIX,sizeof(file->comp_suffix)-1);
	file->comp_suffix[sizeof(file->comp_suffix)-1] = '\0';
    }
    else {
	file->uncompress_cmd[0] = '\0';
	file->compressed=compress_suffix!=NULL;
	if(compress_suffix) {
	    strncpy(file->comp_suffix, compress_suffix,
		    sizeof(file->comp_suffix)-1);
	    file->comp_suffix[sizeof(file->comp_suffix)-1] = '\0';
	} else {
	    strncpy(file->comp_suffix, "N", sizeof(file->comp_suffix)-1);
	    file->comp_suffix[sizeof(file->comp_suffix)-1] = '\0';
	}
    }
    strncpy(file->cont_filename, cont_filename, sizeof(file->cont_filename)-1);
    file->cont_filename[sizeof(file->cont_filename)-1] = '\0';
}

/* Send an Amanda dump header to the output file.
 * returns true if an error occured, false on success
 */

int write_tapeheader(outfd, file)
int outfd;
dumpfile_t *file;
{
    char buffer[DISK_BLOCK_BYTES];
    int	 written;

    build_header(buffer, file, sizeof(buffer), sizeof(buffer));

    written = fullwrite(outfd, buffer, sizeof(buffer));
    if(written == sizeof(buffer)) return 0;
    if(written < 0) return written;
    errno = ENOSPC;
    return -1;
}


int do_dump(mesgfd, datafd, indexfd, outfd)
int mesgfd, datafd, indexfd, outfd;
{
    int maxfd, nfound, size1, size2, eof1, eof2;
    int rc;
    fd_set readset, selectset;
    struct timeval timeout;
    int outpipe[2];
    int header_done;	/* flag - header has been written */
    char *indexfile_tmp = NULL;
    char *indexfile_real = NULL;
    char level_str[NUM_STR_SIZE];
    char kb_str[NUM_STR_SIZE];
    char kps_str[NUM_STR_SIZE];
    char orig_kb_str[NUM_STR_SIZE];
    char *fn;
    char *q;
    times_t runtime;
    double dumptime;	/* Time dump took in secs */
    int compresspid = -1, indexpid = -1, killerr;
    char *errfname = NULL;

#ifndef DUMPER_SOCKET_BUFFERING
#define DUMPER_SOCKET_BUFFERING 0
#endif

#if !defined(SO_RCVBUF) || !defined(SO_RCVLOWAT)
#undef  DUMPER_SOCKET_BUFFERING
#define DUMPER_SOCKET_BUFFERING 0
#endif

#if DUMPER_SOCKET_BUFFERING
    int lowat = NETWORK_BLOCK_BYTES;
    int recbuf = 0;
    int sizeof_recbuf = sizeof(recbuf);
    int lowwatset = 0;
    int lowwatset_count = 0;
#endif

    startclock();

    datain = dataout = databuf;
    datalimit = databuf + sizeof(databuf);
    dumpsize = dumpbytes = origsize = filesize = dump_result = 0;
    nb_header_block = 0;
    got_info_endline = got_sizeline = got_endline = 0;
    header_done = 0;
    amfree(backup_name);
    amfree(recover_cmd);
    amfree(compress_suffix);

    ap_snprintf(level_str, sizeof(level_str), "%d", level);
    fn = sanitise_filename(diskname);
    errfname = newvstralloc(errfname,
			    AMANDA_TMPDIR,
			    "/", hostname,
			    ".", fn,
			    ".", level_str,
			    ".errout",
			    NULL);
    amfree(fn);
    if((errf = fopen(errfname, "w+")) == NULL) {
	errstr = newvstralloc(errstr,
			      "errfile open \"", errfname, "\": ",
			      strerror(errno),
			      NULL);
	amfree(errfname);
	rc = 2;
	goto failed;
    }
    unlink(errfname);				/* so it goes away on close */
    amfree(errfname);

    /* insert pipe in the *READ* side, if server-side compression is desired */
    compresspid = -1;
    if (srvcompress) {
	int tmpfd;

	tmpfd = datafd;
	pipe(outpipe); /* outpipe[0] is pipe's stdin, outpipe[1] is stdout. */
	datafd = outpipe[0];
	if(datafd < 0 || datafd >= FD_SETSIZE) {
	    aclose(outpipe[0]);
	    aclose(outpipe[1]);
	    errstr = newstralloc(errstr, "descriptor out of range");
	    errno = EMFILE;
	    rc = 2;
	    goto failed;
	}
	switch(compresspid=fork()) {
	case -1:
	    errstr = newstralloc2(errstr, "couldn't fork: ", strerror(errno));
	    rc = 2;
	    goto failed;
	default:
	    aclose(outpipe[1]);
	    aclose(tmpfd);
	    break;
	case 0:
	    aclose(outpipe[0]);
	    /* child acts on stdin/stdout */
	    if (dup2(outpipe[1],1) == -1)
		fprintf(stderr, "err dup2 out: %s\n", strerror(errno));
	    if (dup2(tmpfd, 0) == -1)
		fprintf(stderr, "err dup2 in: %s\n", strerror(errno));
	    for(tmpfd = 3; tmpfd <= FD_SETSIZE; ++tmpfd) {
		close(tmpfd);
	    }
	    /* now spawn gzip -1 to take care of the rest */
	    execlp(COMPRESS_PATH, COMPRESS_PATH,
		   (srvcompress == srvcomp_best ? COMPRESS_BEST_OPT
						: COMPRESS_FAST_OPT),
		   (char *)0);
	    error("error: couldn't exec %s.\n", COMPRESS_PATH);
	}
	/* Now the pipe has been inserted. */
    }

    indexpid = -1;
    if (indexfd != -1) {
	int tmpfd;

	indexfile_real = getindexfname(hostname, diskname, datestamp, level),
	indexfile_tmp = stralloc2(indexfile_real, ".tmp");

	if (mkpdir(indexfile_tmp, 02755, (uid_t)-1, (gid_t)-1) == -1) {
	   errstr = newvstralloc(errstr,
				 "err create ",
				 indexfile_tmp,
				 ": ",
				 strerror(errno),
				 NULL);
	   amfree(indexfile_real);
	   amfree(indexfile_tmp);
	   rc = 2;
	   goto failed;
	}

	switch(indexpid=fork()) {
	case -1:
	    errstr = newstralloc2(errstr, "couldn't fork: ", strerror(errno));
	    rc = 2;
	    goto failed;
	default:
	    aclose(indexfd);
	    indexfd = -1;			/* redundant */
	    break;
	case 0:
	    if (dup2(indexfd, 0) == -1) {
		error("err dup2 in: %s", strerror(errno));
	    }
	    indexfd = open(indexfile_tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	    if (indexfd == -1)
		error("err open %s: %s", indexfile_tmp, strerror(errno));
	    if (dup2(indexfd,1) == -1)
		error("err dup2 out: %s", strerror(errno));
	    for(tmpfd = 3; tmpfd <= FD_SETSIZE; ++tmpfd) {
		close(tmpfd);
	    }
	    execlp(COMPRESS_PATH, COMPRESS_PATH, COMPRESS_BEST_OPT, (char *)0);
	    error("error: couldn't exec %s.", COMPRESS_PATH);
	}
    }

    NAUGHTY_BITS_INITIALIZE;

    maxfd = max(mesgfd, datafd) + 1;
    eof1 = eof2 = 0;

    FD_ZERO(&readset);

    /* Just process messages for now.  Once we have done the header
    ** we will start processing data too.
    */
    FD_SET(mesgfd, &readset);

    if(datafd == -1) eof1 = 1;	/* fake eof on data */

#if DUMPER_SOCKET_BUFFERING

#ifndef EST_PACKET_SIZE
#define EST_PACKET_SIZE	512
#endif
#ifndef	EST_MIN_WINDOW
#define	EST_MIN_WINDOW	EST_PACKET_SIZE*4 /* leave room for 2k in transit */
#endif

    else {
	recbuf = STREAM_BUFSIZE;
	if (setsockopt(datafd, SOL_SOCKET, SO_RCVBUF,
		       (void *) &recbuf, sizeof_recbuf)) {
	    const int errornumber = errno;
	    fprintf(stderr, "%s: pid %ld setsockopt(SO_RCVBUF): %s\n",
		    get_pname(), (long) getpid(), strerror(errornumber));
	}
	if (getsockopt(datafd, SOL_SOCKET, SO_RCVBUF,
		       (void *) &recbuf, (void *)&sizeof_recbuf)) {
	    const int errornumber = errno;
	    fprintf(stderr, "%s: pid %ld getsockopt(SO_RCVBUF): %s\n",
		    get_pname(), (long) getpid(), strerror(errornumber));
	    recbuf = 0;
	}

	/* leave at least EST_MIN_WINDOW between lowwat and recbuf */
	if (recbuf-lowat < EST_MIN_WINDOW)
	    lowat = recbuf-EST_MIN_WINDOW;

	/* if lowwat < ~512, don't bother */
	if (lowat < EST_PACKET_SIZE)
	    recbuf = 0;
	fprintf(stderr, "%s: pid %ld receive size is %d, low water is %d\n",
		get_pname(), (long) getpid(), recbuf, lowat);
    }
#endif

    while(!(eof1 && eof2)) {

#if DUMPER_SOCKET_BUFFERING
	/* Set socket buffering */
	if (recbuf>0 && !lowwatset) {
	    if (setsockopt(datafd, SOL_SOCKET, SO_RCVLOWAT,
			   (void *) &lowat, sizeof(lowat))) {
		const int errornumber = errno;
		fprintf(stderr,
			"%s: pid %ld setsockopt(SO_RCVLOWAT): %s\n",
			get_pname(), (long) getpid(), strerror(errornumber));
	    }
	    lowwatset = 1;
	    lowwatset_count++;
	}
#endif

	timeout.tv_sec = conf_dtimeout;
	timeout.tv_usec = 0;
	memcpy(&selectset, &readset, sizeof(fd_set));

	nfound = select(maxfd, (SELECT_ARG_TYPE *)(&selectset), NULL, NULL, &timeout);

	/* check for errors or timeout */

#if DUMPER_SOCKET_BUFFERING
	if (nfound==0 && lowwatset) {
	    const int zero = 0;
	    /* Disable socket buffering and ... */
	    if (setsockopt(datafd, SOL_SOCKET, SO_RCVLOWAT,
			   (void *) &zero, sizeof(zero))) {
		const int errornumber = errno;
		fprintf(stderr,
			"%s: pid %ld setsockopt(SO_RCVLOWAT): %s\n",
			get_pname(), (long) getpid(), strerror(errornumber));
	    }
	    lowwatset = 0;

	    /* ... try once more */
	    timeout.tv_sec = conf_dtimeout;
	    timeout.tv_usec = 0;
	    memcpy(&selectset, &readset, sizeof(fd_set));
	    nfound = select(maxfd, (SELECT_ARG_TYPE *)(&selectset), NULL, NULL, &timeout);
	}
#endif

	if(nfound == 0)  {
	    errstr = newstralloc(errstr, "data timeout");
	    rc = 2;
	    goto failed;
	}
	if(nfound == -1) {
	    errstr = newstralloc2(errstr, "select: ", strerror(errno));
	    rc = 2;
	    goto failed;
	}

	/* read/write any data */

	if(datafd >= 0 && FD_ISSET(datafd, &selectset)) {
	    size1 = read(datafd, datain, datalimit - datain);
	    if(size1 < 0) {
		errstr = newstralloc2(errstr, "data read: ", strerror(errno));
		rc = 2;
		goto failed;
	    }
	    if(update_dataptr(&outfd, size1)) {
		rc = 2;
		goto failed;
	    }
	    if(size1 == 0) {
		eof1 = 1;
		FD_CLR(datafd, &readset);
		aclose(datafd);
	    }
	}

	if(mesgfd >= 0 && FD_ISSET(mesgfd, &selectset)) {
	    size2 = read(mesgfd, mesgbuf, sizeof(mesgbuf)-1);
	    switch(size2) {
	    case -1:
		errstr = newstralloc2(errstr, "mesg read: ", strerror(errno));
		rc = 2;
		goto failed;
	    case 0:
		eof2 = 1;
		process_dumpeof();
		FD_CLR(mesgfd, &readset);
		aclose(mesgfd);
		break;
	    default:
		mesgbuf[size2] = '\0';
		add_msg_data(mesgbuf, size2);
	    }

	    if (got_info_endline && !header_done) { /* time to do the header */
		make_tapeheader(&file, F_DUMPFILE);
		if (write_tapeheader(outfd, &file)) {
		    int save_errno = errno;
		    errstr = newstralloc2(errstr, "write_tapeheader: ", 
					  strerror(errno));
		    if(save_errno == ENOSPC) {
			putresult("NO-ROOM %s %lu\n", handle, 
				  use+split_size-dumpsize);
			use = 0; /* force RQ_MORE_DISK */
			split_size = dumpsize;
			rc = 1;
		    }
		    else {
			rc = 2;
		    }
		    goto failed;
		}
		dumpsize += DISK_BLOCK_KB;
		filesize += DISK_BLOCK_KB;
		nb_header_block++;
		header_done = 1;
		strncat(cont_filename,filename,sizeof(cont_filename));
		cont_filename[sizeof(cont_filename)-1] = '\0';

		if (datafd != -1)
		    FD_SET(datafd, &readset);	/* now we can read the data */
	    }
	}
    } /* end while */

#if DUMPER_SOCKET_BUFFERING
    if(lowwatset_count > 1) {
	fprintf(stderr, "%s: pid %ld low water set %d times\n",
	        get_pname(), (long) getpid(), lowwatset_count);
    }
#endif

    if(dump_result > 1) {
	rc = 2;
	goto failed;
    }

    runtime = stopclock();
    dumptime = runtime.r.tv_sec + runtime.r.tv_usec/1000000.0;

    dumpsize -= (nb_header_block * DISK_BLOCK_KB);/* don't count the header */
    if (dumpsize < 0) dumpsize = 0;	/* XXX - maybe this should be fatal? */

    ap_snprintf(kb_str, sizeof(kb_str), "%ld", dumpsize);
    ap_snprintf(kps_str, sizeof(kps_str),
		"%3.1f",
		dumptime ? dumpsize / dumptime : 0.0);
    ap_snprintf(orig_kb_str, sizeof(orig_kb_str), "%ld", origsize);
    errstr = newvstralloc(errstr,
			  "sec ", walltime_str(runtime),
			  " ", "kb ", kb_str,
			  " ", "kps ", kps_str,
			  " ", "orig-kb ", orig_kb_str,
			  NULL);
    q = squotef("[%s]", errstr);
    putresult("DONE %s %ld %ld %ld %s\n", handle, origsize, dumpsize,
	      (long)(dumptime+0.5), q);
    amfree(q);

    switch(dump_result) {
    case 0:
	log_add(L_SUCCESS, "%s %s %s %d [%s]", hostname, diskname, datestamp, level, errstr);

	break;

    case 1:
	log_start_multiline();
	log_add(L_STRANGE, "%s %s %d [%s]", hostname, diskname, level, errstr);
	log_msgout(L_STRANGE);
	log_end_multiline();

	break;
    }

    if(errf) afclose(errf);

    if (indexfile_tmp) {
	waitpid(indexpid,NULL,0);
	if(rename(indexfile_tmp, indexfile_real) != 0) {
	    log_add(L_WARNING, "could not rename \"%s\" to \"%s\": %s",
		    indexfile_tmp, indexfile_real, strerror(errno));
	}
	amfree(indexfile_tmp);
	amfree(indexfile_real);
    }

    return 0;

 failed:

#if DUMPER_SOCKET_BUFFERING
    if(lowwatset_count > 1) {
	fprintf(stderr, "%s: pid %ld low water set %d times\n",
	        get_pname(), (long) getpid(), lowwatset_count);
    }
#endif

    if(!abort_pending) {
	q = squotef("[%s]", errstr);
	if(rc==2)
	    putresult("FAILED %s %s\n", handle, q);
	else
	    putresult("TRY-AGAIN %s %s\n", handle, q);
	amfree(q);
    }

    /* kill all child process */
    if(compresspid != -1) {
	killerr = kill(compresspid,SIGTERM);
	if(killerr == 0) {
	    fprintf(stderr,"%s: kill compress command\n",get_pname());
	}
	else if ( killerr == -1 ) {
	    if(errno != ESRCH)
		fprintf(stderr,"%s: can't kill compress command: %s\n", 
			       get_pname(), strerror(errno));
	}
    }

    if(indexpid != -1) {
	killerr = kill(indexpid,SIGTERM);
	if(killerr == 0) {
	    fprintf(stderr,"%s: kill index command\n",get_pname());
	}
	else if ( killerr == -1 ) {
	    if(errno != ESRCH)
		fprintf(stderr,"%s: can't kill index command: %s\n", 
			       get_pname(),strerror(errno));
	}
    }

    if(!abort_pending) {
	log_start_multiline();
	log_add(L_FAIL, "%s %s %d [%s]", hostname, diskname, level, errstr);
	if (errf) {
	    log_msgout(L_FAIL);
	}
	log_end_multiline();
    }

    if(errf) afclose(errf);

    if (indexfile_tmp) {
	unlink(indexfile_tmp);
	amfree(indexfile_tmp);
	amfree(indexfile_real);
    }

    return rc;
}

/* -------------------- */

char *hostname, *disk;
int response_error;

void sendbackup_response(p, pkt)
proto_t *p;
pkt_t *pkt;
{
    char *optionstr = NULL;
    int data_port, mesg_port, index_port;
    char *s;
    int ch;
    char *nl;

    if(p->state == S_FAILED) {
	if(pkt == NULL) {
	    errstr = newstralloc(errstr, "[request timeout]");
	    response_error = 1;
	    return;
	}
	else {
/*	    fprintf(stderr, "got nak response:\n----\n%s----\n\n", pkt->body);*/
#define sc "ERROR"
	    if(strncmp(pkt->body, sc, sizeof(sc)-1) != 0) {
		goto request_NAK;
	    }
	    s = pkt->body+sizeof(sc)-1;
	    ch = *s++;
#undef sc
	    skip_whitespace(s, ch);
	    if(ch == '\0') {
		goto request_NAK;
	    }
	    errstr = newvstralloc(errstr, "nak error:", s - 1, NULL);
	    if(errstr[strlen(errstr)-1] == '\n' )
		errstr[strlen(errstr)-1] = '\0';
	    response_error = 1;
	    return;
	}
    }

/*     fprintf(stderr, "got response:\n----\n%s----\n\n", pkt->body); */

#ifdef KRB4_SECURITY
    if(krb4_auth && !check_mutual_authenticator(&cred.session, pkt, p)) {
	errstr = newstralloc(errstr, "[mutual-authentication failed]");
	response_error = 2;
	return;
    }
#endif

#define sc "ERROR"
    if(strncmp(pkt->body, sc, sizeof(sc)-1) == 0) {
	/* this is an error response packet */
	s = pkt->body+sizeof(sc)-1;
	ch = *s++;
#undef sc
	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto bogus_error_packet;
	}
	errstr = newstralloc(errstr, s - 1);
	response_error = 2;
	return;
    }

    if((nl = strchr(pkt->body, '\n')) == NULL) {
	goto parse_of_reply_message_failed;
    }
    *nl = '\0';

    s = pkt->body;
    ch = *s++;

    skip_whitespace(s, ch);
#define sc "CONNECT"
    if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	goto parse_of_reply_message_failed;
    }
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc

    skip_whitespace(s, ch);
#define sc "DATA"
    if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	goto parse_of_reply_message_failed;
    }
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc

    skip_whitespace(s, ch);
    if(ch == '\0' || sscanf(s - 1, "%d", &data_port) != 1) {
	goto parse_of_reply_message_failed;
    }
    skip_integer(s, ch);

    skip_whitespace(s, ch);
#define sc "MESG"
    if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	goto parse_of_reply_message_failed;
    }
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc

    skip_whitespace(s, ch);
    if(ch == '\0' || sscanf(s - 1, "%d", &mesg_port) != 1) {
	goto parse_of_reply_message_failed;
    }
    skip_integer(s, ch);

    skip_whitespace(s, ch);
#define sc "INDEX"
    if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	goto parse_of_reply_message_failed;
    }
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc

    skip_whitespace(s, ch);
    if(ch == '\0' || sscanf(s - 1, "%d", &index_port) != 1) {
	goto parse_of_reply_message_failed;
    }
    skip_integer(s, ch);

    skip_whitespace(s, ch);
    if(ch != '\0') {
	goto parse_of_reply_message_failed;
    }

    s = nl + 1;
    ch = *s++;
    *nl = '\n';
    nl = NULL;

    skip_whitespace(s, ch);
#define sc "OPTIONS"
    if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	goto parse_of_reply_message_failed;
    }
    s += sizeof(sc)-1;
    ch = s[-1];

    skip_whitespace(s, ch);
    if(ch == '\0') {
	goto parse_of_reply_message_failed;
    }
    if ((nl = strchr(s - 1, '\n')) != NULL) {
	*nl = '\0';
	if(nl == s - 1) {
	    goto parse_of_reply_message_failed;
	}
    }
    optionstr = stralloc(s - 1);
    if(nl) {
	*nl = '\n';
	nl = NULL;
    }

    datafd = stream_client(hostname, data_port, -1, -1, NULL);
    if(datafd == -1) {
	errstr = newvstralloc(errstr,
			      "[could not connect to data port: ",
			      strerror(errno),
			      "]",
			      NULL);
	response_error = 1;
	amfree(optionstr);
	return;
    }
    mesgfd = stream_client(hostname, mesg_port, -1, -1, NULL);
    if(mesgfd == -1) {
	errstr = newvstralloc(errstr,
			      "[could not connect to mesg port: ",
			      strerror(errno),
			      "]",
			      NULL);
	aclose(datafd);
	datafd = -1;				/* redundant */
	response_error = 1;
	amfree(optionstr);
	return;
    }

    if (index_port != -1) {
	indexfd = stream_client(hostname, index_port, -1, -1, NULL);
	if (indexfd == -1) {
	    errstr = newvstralloc(errstr,
				  "[could not connect to index port: ",
				  strerror(errno),
				  "]",
				  NULL);
	    aclose(datafd);
	    aclose(mesgfd);
	    datafd = mesgfd = -1;		/* redundant */
	    response_error = 1;
	    amfree(optionstr);
	    return;
	}
    }

    /* everything worked */

#ifdef KRB4_SECURITY
    if(krb4_auth && kerberos_handshake(datafd, cred.session) == 0) {
	errstr = newstralloc(errstr,
			     "[mutual authentication in data stream failed]");
	aclose(datafd);
	aclose(mesgfd);
	if (indexfd != -1)
	    aclose(indexfd);
	response_error = 1;
	amfree(optionstr);
	return;
    }
    if(krb4_auth && kerberos_handshake(mesgfd, cred.session) == 0) {
	errstr = newstralloc(errstr,
			     "[mutual authentication in mesg stream failed]");
	aclose(datafd);
	if (indexfd != -1)
	    aclose(indexfd);
	aclose(mesgfd);
	response_error = 1;
	amfree(optionstr);
	return;
    }
#endif
    response_error = 0;
    amfree(optionstr);
    return;

 request_NAK:

#if 0
    fprintf(stderr, "%s: got strange NAK: %s\n", get_pname(), pkt->body);
#endif
    errstr = newstralloc(errstr, "[request NAK]");
    response_error = 2;
    amfree(optionstr);
    return;

 bogus_error_packet:

    errstr = newstralloc(errstr, "[bogus error packet]");
    response_error = 2;
    amfree(optionstr);
    return;

 parse_of_reply_message_failed:

    if(nl) *nl = '\n';
    errstr = newstralloc(errstr, "[parse of reply message failed]");
    response_error = 2;
    amfree(optionstr);
    return;
}

int startup_dump(hostname, disk, level, dumpdate, progname, options)
char *hostname, *disk, *dumpdate, *progname, *options;
int level;
{
    char level_string[NUM_STR_SIZE];
    char rc_str[NUM_STR_SIZE];
    char *req = NULL;
    int rc;

    ap_snprintf(level_string, sizeof(level_string), "%d", level);
    req = vstralloc("SERVICE sendbackup\n",
		    "OPTIONS ",
		    "hostname=", hostname, ";",
		    "\n",
		    progname, " ", disk, " ", level_string, " ", dumpdate, " ",
		    "OPTIONS ", options,
		    "\n",
		    NULL);

    datafd = mesgfd = indexfd = -1;

#ifdef KRB4_SECURITY
    if(krb4_auth) {
	rc = make_krb_request(hostname, kamanda_port, req, NULL,
			      STARTUP_TIMEOUT, sendbackup_response);
	if(!rc) {
	    char inst[256], realm[256];
#define HOSTNAME_INSTANCE inst
	    /*
	     * This repeats a lot of work with make_krb_request, but it's
	     * ultimately the kerberos library's fault: krb_mk_req calls
	     * krb_get_cred, but doesn't make the session key available!
	     * XXX: But admittedly, we could restructure a bit here and
	     * at least eliminate the duplicate gethostbyname().
	     */
	    if(host2krbname(hostname, inst, realm) == 0)
		rc = -1;
	    else
		rc = krb_get_cred(CLIENT_HOST_PRINCIPLE, CLIENT_HOST_INSTANCE,
				  realm, &cred);
	    if(rc > 0 ) {
		ap_snprintf(rc_str, sizeof(rc_str), "%d", rc);
		errstr = newvstralloc(errstr,
				      "[host ", hostname,
				      ": krb4 error (krb_get_cred) ",
				      rc_str,
				      ": ", krb_err_txt[rc],
				      NULL);
		amfree(req);
		return 2;
	    }
	}
	if(rc > 0) {
	    ap_snprintf(rc_str, sizeof(rc_str), "%d", rc);
	    errstr = newvstralloc(errstr,
				  "[host ", hostname,
				  ": krb4 error (make_krb_req) ",
				  rc_str,
				  ": ", krb_err_txt[rc],
				  NULL);
	    amfree(req);
	    return 2;
	}
    } else
#endif
	rc = make_request(hostname, amanda_port, req, NULL,
			  STARTUP_TIMEOUT, sendbackup_response);

    req = NULL;					/* do not own this any more */

    if(rc) {
	ap_snprintf(rc_str, sizeof(rc_str), "%d", rc);
	errstr = newvstralloc(errstr,
			      "[could not resolve name \"", hostname, "\"",
			      ": error ", rc_str, "]",
			      NULL);
	return 2;
    }
    run_protocol();
    return response_error;
}
