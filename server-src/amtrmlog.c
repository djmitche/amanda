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
 * $Id: amtrmlog.c,v 1.7 2002/11/05 01:58:52 martinea Exp $
 *
 * trims number of index files to only those still in system.  Well
 * actually, it keeps a few extra, plus goes back to the last level 0
 * dump.
 */

#include "amanda.h"
#include "arglist.h"
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#include "conffile.h"
#include "diskfile.h"
#include "tapefile.h"
#include "find.h"
#include "version.h"

int main P((int, char **));

int main(argc, argv)
int argc;
char **argv;
{
    disklist_t diskl;
    int no_keep;			/* files per system to keep */
    int fd;
    char **output_find_log;
    DIR *dir;
    struct dirent *adir;
    char **name;
    int useful;
    char *olddir;
    char *oldfile = NULL, *newfile = NULL;
    time_t today, date_keep;
    char *logname = NULL;
    struct stat stat_log;
    struct stat stat_old;
    char *conffile;
    char *conf_diskfile;
    char *conf_tapelist;
    char *conf_logdir;
    int amtrmidx_debug = 0;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    safe_cd();

    set_pname("amtrmlog");

    if (argc > 1 && strcmp(argv[1], "-t") == 0) {
	amtrmidx_debug = 1;
	argc--;
	argv++;
    }

    if (argc < 2) {
	fprintf(stderr, "Usage: %s [-t] <config>\n", argv[0]);
	return 1;
    }

    dbopen();
    dbprintf(("%s: version %s\n", argv[0], version()));

    config_name = argv[1];

    config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if (read_conffile(conffile))
	error("errors processing amanda config file \"%s\"", conffile);
    amfree(conffile);

    conf_diskfile = getconf_str(CNF_DISKFILE);
    if (*conf_diskfile == '/') {
	conf_diskfile = stralloc(conf_diskfile);
    } else {
	conf_diskfile = stralloc2(config_dir, conf_diskfile);
    }
    if (read_diskfile(conf_diskfile, &diskl) < 0)
	error("could not load disklist \"%s\"", conf_diskfile);
    amfree(conf_diskfile);

    conf_tapelist = getconf_str(CNF_TAPELIST);
    if (*conf_tapelist == '/') {
	conf_tapelist = stralloc(conf_tapelist);
    } else {
	conf_tapelist = stralloc2(config_dir, conf_tapelist);
    }
    if (read_tapelist(conf_tapelist))
	error("could not load tapelist \"%s\"", conf_tapelist);
    amfree(conf_tapelist);

    today = time((time_t *)NULL);
    date_keep = today - (getconf_int(CNF_DUMPCYCLE)*86400);

    output_find_log = find_log();

    /* determine how many log to keep */
    no_keep = getconf_int(CNF_TAPECYCLE) * 2;
    dbprintf(("Keeping %d log file%s\n", no_keep, (no_keep == 1) ? "" : "s"));

    conf_logdir = getconf_str(CNF_LOGDIR);
    if (*conf_logdir == '/') {
	conf_logdir = stralloc(conf_logdir);
    } else {
	conf_logdir = stralloc2(config_dir, conf_logdir);
    }
    olddir = vstralloc(conf_logdir, "/oldlog", NULL);
    if (mkpdir(olddir, 02700, (uid_t)-1, (gid_t)-1) != 0)
	error("could not create parents of %s: %s", olddir, strerror(errno));
    if (mkdir(olddir, 02700) != 0 && errno != EEXIST)
	error("could not create %s: %s", olddir, strerror(errno));

    if (stat(olddir,&stat_old) == -1) {
	error("can't stat oldlog directory \"%s\": %s", olddir, strerror(errno));
    }

    if (!S_ISDIR(stat_old.st_mode)) {
	error("Oldlog directory \"%s\" is not a directory", olddir);
    }

    if ((dir = opendir(conf_logdir)) == NULL)
	error("could not open log directory \"%s\": %s", conf_logdir,strerror(errno));
    while ((adir=readdir(dir)) != NULL) {
	if(strncmp(adir->d_name,"log.",4)==0) {
	    useful=0;
	    for (name=output_find_log;*name !=NULL; name++) {
		if(strncmp(adir->d_name,*name,12)==0) {
		    useful=1;
		}
	    }
	    logname=newvstralloc(logname,
				 conf_logdir, "/" ,adir->d_name, NULL);
	    if(stat(logname,&stat_log)==0) {
		if(stat_log.st_mtime > date_keep) {
		    useful = 1;
		}
	    }
	    if(useful == 0) {
		oldfile = newvstralloc(oldfile,
				       conf_logdir, "/", adir->d_name, NULL);
		newfile = newvstralloc(newfile,
				       olddir, "/", adir->d_name, NULL);
		if (rename(oldfile,newfile) != 0)
		    error("could not rename \"%s\" to \"%s\": %s",
			  oldfile, newfile, strerror(errno));
	    }
	}
    }
    closedir(dir);
    amfree(logname);
    amfree(oldfile);
    amfree(newfile);
    amfree(olddir);
    amfree(config_dir);

    dbclose();

    return 0;
}
