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
 * $Id: amcleanupdisk.c,v 1.5 1999/04/17 22:20:03 martinea Exp $
 */
#include "amanda.h"

#include "conffile.h"
#include "diskfile.h"
#include "clock.h"
#include "version.h"
#include "holding.h"
#include "infofile.h"
#include "server_util.h"

static char *config;
char *confdir;
holding_t *holding_list;
char *datestamp;

/* local functions */
int main P((int argc, char **argv));
void check_holdingdisk P((char *diskdir, char *datestamp));
void check_disks P((void));

int main(main_argc, main_argv)
int main_argc;
char **main_argv;
{
    struct passwd *pw;
    char *dumpuser;
    int fd;
    disklist_t *diskqp;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    set_pname("amcleanupdisk");

    if(main_argc != 2)
	error("Usage: amcleanupdisk%s <confdir>", versionsuffix());

    config = main_argv[1];
    confdir = vstralloc(CONFIG_DIR, "/", main_argv[1], NULL);
    if(chdir(confdir) != 0)
	error("could not cd to confdir %s: %s",	confdir, strerror(errno));

    if(read_conffile(CONFFILE_NAME))
	error("could not read amanda config file\n");

    datestamp = construct_datestamp();

    if((diskqp = read_diskfile(getconf_str(CNF_DISKFILE))) == NULL)
	error("could not read disklist file\n");

    dumpuser = getconf_str(CNF_DUMPUSER);
    if((pw = getpwnam(dumpuser)) == NULL)
	error("dumpuser %s not found in password file", dumpuser);
    if(pw->pw_uid != getuid())
	error("must run amcleanupdisk as user %s", dumpuser);

    open_infofile(getconf_str(CNF_INFOFILE));

    holding_list = pick_all_datestamp();

    check_disks();

    close_infofile();

    return 0;
}


void check_holdingdisk(diskdir, datestamp)
char *diskdir, *datestamp;
{
    DIR *workdir;
    struct dirent *entry;
    char *dirname = NULL;
    char *tmpname = NULL;
    char *destname = NULL;
    char *hostname = NULL;
    char *diskname = NULL;
    disk_t *dp;
    filetype_t filetype;
    info_t info;
    int level;

    dirname = vstralloc(diskdir, "/", datestamp, NULL);

    if((workdir = opendir(dirname)) == NULL) {
	amfree(dirname);
	return;
    }
    chdir(dirname);

    while((entry = readdir(workdir)) != NULL) {
	if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
	    continue;

	if(strlen(entry->d_name) < 7 ) continue;

	if(strncmp(&entry->d_name[strlen(entry->d_name)-4],".tmp",4) != 0) 
	    continue;

	tmpname = newvstralloc( destname,
				dirname, "/", entry->d_name,
				NULL);

	destname = newvstralloc(destname, tmpname, NULL);
	destname[strlen(destname)-4] = '\0';

	amfree(hostname);
	amfree(diskname);
	filetype = get_amanda_names(tmpname, &hostname, &diskname, &level);
	if( filetype != F_DUMPFILE) continue;

	dp = lookup_disk(hostname, diskname);

	if (dp == NULL) {
	    continue;
	}

	if(level < 0 || level > 9) {
	    continue;
	}

	if(rename_tmp_holding(destname, 0)) {
	    get_info(dp->host->hostname, dp->name, &info);
	    if( info.command & FORCE_BUMP)
		info.command ^= FORCE_BUMP;
	    info.command |= FORCE_NO_BUMP;
	    if(put_info(dp->host->hostname, dp->name, &info))
		error("could not put info record for %s:%s: %s",
		      dp->host->hostname, dp->name, strerror(errno));
	}
	else {
	    fprintf(stderr,"rename_tmp_holding failed\n");
	}
    }
    closedir(workdir);

    /* try to zap the now (hopefully) empty working dir */
    amfree(diskname);
    amfree(hostname);
    amfree(destname);
    amfree(dirname);
}


void check_disks()
{
    holdingdisk_t *hdisk;
    holding_t *dir;

    chdir(confdir);

    for(dir = holding_list; dir !=NULL; dir = dir->next) {
	for(hdisk = getconf_holdingdisks(); hdisk != NULL; hdisk = hdisk->next)
	    check_holdingdisk(hdisk->diskdir, dir->name);
    }

}

