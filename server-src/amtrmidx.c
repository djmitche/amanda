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
 * $Id: amtrmidx.c,v 1.34 2006/01/14 04:37:19 paddy_s Exp $
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

static int sort_by_name_reversed(a, b)
    const void *a;
    const void *b;
{
    char **ap = (char **) a;
    char **bp = (char **) b;

    return -1 * strcmp(*ap, *bp);
}

int main P((int, char **));

int main(argc, argv)
int argc;
char **argv;
{
    disk_t *diskp;
    disklist_t diskl;
    int i;
    char *conffile;
    char *conf_diskfile;
    char *conf_tapelist;
    char *conf_indexdir;
    find_result_t *output_find;
    time_t tmp_time;
    int amtrmidx_debug = 0;

    safe_fd(-1, 0);
    safe_cd();

    set_pname("amtrmidx");

    /* Don't die when child closes pipe */
    signal(SIGPIPE, SIG_IGN);

    dbopen();
    dbprintf(("%s: version %s\n", argv[0], version()));

    if (argc > 1 && strcmp(argv[1], "-t") == 0) {
	amtrmidx_debug = 1;
	argc--;
	argv++;
    }

    if (argc < 2) {
	fprintf(stderr, "Usage: %s [-t] <config>\n", argv[0]);
	return 1;
    }

    config_name = argv[1];

    config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if (read_conffile(conffile))
	error("errors processing config file \"%s\"", conffile);
    amfree(conffile);

    conf_diskfile = getconf_str(CNF_DISKFILE);
    if(*conf_diskfile == '/') {
	conf_diskfile = stralloc(conf_diskfile);
    } else {
	conf_diskfile = stralloc2(config_dir, conf_diskfile);
    }
    if (read_diskfile(conf_diskfile, &diskl) < 0)
	error("could not load disklist \"%s\"", conf_diskfile);
    amfree(conf_diskfile);

    conf_tapelist = getconf_str(CNF_TAPELIST);
    if(*conf_tapelist == '/') {
	conf_tapelist = stralloc(conf_tapelist);
    } else {
	conf_tapelist = stralloc2(config_dir, conf_tapelist);
    }
    if(read_tapelist(conf_tapelist))
	error("could not load tapelist \"%s\"", conf_tapelist);
    amfree(conf_tapelist);

    output_find = find_dump(1, &diskl);

    conf_indexdir = getconf_str(CNF_INDEXDIR);
    if(*conf_indexdir == '/') {
	conf_indexdir = stralloc(conf_indexdir);
    } else {
	conf_indexdir = stralloc2(config_dir, conf_indexdir);
    }

    /* now go through the list of disks and find which have indexes */
    time(&tmp_time);
    tmp_time -= 7*24*60*60;			/* back one week */
    for (diskp = diskl.head; diskp != NULL; diskp = diskp->next)
    {
	if (diskp->index)
	{
	    char *indexdir;
	    DIR *d;
	    struct dirent *f;
	    char **names;
	    int name_length;
	    int name_count;
	    char *host;
	    char *disk;

	    dbprintf(("%s %s\n", diskp->host->hostname, diskp->name));

	    /* get listing of indices, newest first */
	    host = sanitise_filename(diskp->host->hostname);
	    disk = sanitise_filename(diskp->name);
	    indexdir = vstralloc(conf_indexdir, "/",
				 host, "/",
				 disk, "/",
				 NULL);
	    amfree(host);
	    amfree(disk);
	    if ((d = opendir(indexdir)) == NULL) {
		dbprintf(("could not open index directory \"%s\"\n", indexdir));
		amfree(indexdir);
		continue;
	    }
	    name_length = 100;
	    names = (char **)alloc(name_length * sizeof(char *));
	    name_count = 0;
	    while ((f = readdir(d)) != NULL) {
		int l;

		if(is_dot_or_dotdot(f->d_name)) {
		    continue;
		}
		for(i = 0; i < sizeof("YYYYMMDD")-1; i++) {
		    if(! isdigit((int)(f->d_name[i]))) {
			break;
		    }
		}
		if(i < sizeof("YYYYMMDD")-1
		    || f->d_name[i] != '_'
		    || ! isdigit((int)(f->d_name[i+1]))) {
		    continue;			/* not an index file */
		}
		/*
		 * Clear out old index temp files.
		 */
		l = strlen(f->d_name) - (sizeof(".tmp")-1);
		if(l > sizeof("YYYYMMDD_L")-1
		    && strcmp (f->d_name + l, ".tmp") == 0) {
		    struct stat sbuf;
		    char *path;

		    path = stralloc2(indexdir, f->d_name);
		    if(lstat(path, &sbuf) != -1
			&& (sbuf.st_mode & S_IFMT) == S_IFREG
			&& sbuf.st_mtime < tmp_time) {
			dbprintf(("rm %s\n", path));
		        if(amtrmidx_debug == 0 && unlink(path) == -1) {
			    dbprintf(("Error removing \"%s\": %s\n",
				      path, strerror(errno)));
		        }
		    }
		    amfree(path);
		    continue;
		}
		if(name_count >= name_length) {
		    char **new_names;

		    new_names = alloc((name_length + 100) * sizeof(char *));
		    memcpy(new_names, names, name_length * sizeof(char *));
		    name_length += 100;
		    amfree(names);
		    names = new_names;
		}
		names[name_count++] = stralloc(f->d_name);
	    }
	    closedir(d);
	    qsort(names, name_count, sizeof(char *), sort_by_name_reversed);

	    /*
	     * Search for the first full dump past the minimum number
	     * of index files to keep.
	     */
	    for(i = 0; i < name_count; i++) {
		if(!dump_exist(output_find,
					 diskp->host->hostname,diskp->name,
					 atoi(names[i]),
					 names[i][sizeof("YYYYMMDD_L")-1-1] - '0')) {
		    char *path;
		    path = stralloc2(indexdir, names[i]);
		    dbprintf(("rm %s\n", path));
		    if(amtrmidx_debug == 0 && unlink(path) == -1) {
			dbprintf(("Error removing \"%s\": %s\n",
				  path, strerror(errno)));
		    }
		    amfree(path);
		}
		amfree(names[i]);
	    }
	    amfree(names);
	    amfree(indexdir);
	}
    }

    amfree(conf_indexdir);
    amfree(config_dir);
    free_find_result(&output_find);

    dbclose();

    return 0;
}
