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
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/*
 * $Id: amlogroll.c,v 1.5 2002/11/05 01:58:52 martinea Exp $
 *
 * rename a live log file to the datestamped name.
 */

#include "amanda.h"
#include "conffile.h"
#include "logfile.h"
#include "version.h"

char *datestamp;

void handle_start P((void));

int main(argc, argv)
int argc;
char **argv;
{
    char *conffile;
    char *logfname;
    char *conf_logdir;
    FILE *logfile;
    int fd;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    char my_cwd[STR_SIZE];

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    set_pname("amreport");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    /* Process options */
    
    erroutput_type = ERR_INTERACTIVE;

    if (getcwd(my_cwd, sizeof(my_cwd)) == NULL) {
	error("cannot determine current working directory");
    }

    if (argc < 2) {
	config_dir = stralloc2(my_cwd, "/");
	if ((config_name = strrchr(my_cwd, '/')) != NULL) {
	    config_name = stralloc(config_name + 1);
	}
    } else {
	config_name = stralloc(argv[1]);
	config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
    }

    safe_cd();

    /* read configuration files */

    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if(read_conffile(conffile)) {
        error("errors processing config file \"%s\"", conffile);
    }
    amfree(conffile);

    conf_logdir = getconf_str(CNF_LOGDIR);
    if (*conf_logdir == '/') {
        conf_logdir = stralloc(conf_logdir);
    } else {
        conf_logdir = stralloc2(config_dir, conf_logdir);
    }
    logfname = vstralloc(conf_logdir, "/", "log", NULL);
    amfree(conf_logdir);

    if((logfile = fopen(logfname, "r")) == NULL) {
	error("could not open log %s: %s", logfname, strerror(errno));
    }
    amfree(logfname);

    erroutput_type |= ERR_AMANDALOG;
    set_logerror(logerror);

    while(get_logline(logfile)) {
	if(curlog == L_START) {
	    handle_start();
	    if(datestamp != NULL) {
		break;
	    }
	}
    }
    afclose(logfile);
 
    log_rename(datestamp);

    amfree(datestamp);
    amfree(config_dir);
    amfree(config_name);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    return 0;
}

void handle_start()
{
    static int started = 0;
    char *s, *fp;
    int ch;

    if(!started) {
	s = curstr;
	ch = *s++;

	skip_whitespace(s, ch);
#define sc "date"
	if(ch == '\0' || strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	    return;				/* ignore bogus line */
	}
	s += sizeof(sc)-1;
	ch = s[-1];
#undef sc
	skip_whitespace(s, ch);
	if(ch == '\0') {
	    return;
	}
	fp = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';
	datestamp = newstralloc(datestamp, fp);
	s[-1] = ch;

	started = 1;
    }
}
