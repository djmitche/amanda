/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1996 University of Maryland at College Park
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
 * $Id: amtrmidx.c,v 1.20 1998/04/22 18:30:20 jrj Exp $
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

int main(argc, argv)
int argc;
char **argv;
{
    char *line;
    char *cmd = NULL;
    disk_t *diskp;
    disklist_t *diskl;
    int no_keep;			/* files per system to keep */
    int i;
    int level_position;			/* where (from end) is level in name */
    int datestamp_position;
    FILE *fp;
    char *ptr;
    int fd;
    find_result_t *output_find;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    set_pname("amtrmidx");

    if (argc != 2)
    {
	fprintf(stderr, "Usage: %s <config>\n", argv[0]);
	return 1;
    }

    dbopen();
    dbprintf(("%s: version %s\n", argv[0], version()));

    /* read the config file */
    ptr = vstralloc(CONFIG_DIR, "/", argv[1], NULL);
    if (chdir(ptr) != 0)
	error("could not cd to confdir \"%s\": %s", ptr, strerror(errno));

    if (read_conffile(CONFFILE_NAME))
	error("could not read amanda config file");

    /* get the list of disks being dumped and their types */
    if ((diskl = read_diskfile(getconf_str(CNF_DISKFILE))) == NULL)
	error("could not load \"%s\".", getconf_str(CNF_DISKFILE));

    if(read_tapelist(getconf_str(CNF_TAPELIST)))
	error("could not load \"%s\"\n", getconf_str(CNF_TAPELIST));

    output_find = find_dump(NULL,0,NULL);

    /* change into the index directory */
    if (chdir(getconf_str(CNF_INDEXDIR)) == -1)
	error("could not cd to index directory \"%s\": %s",
	      getconf_str(CNF_INDEXDIR), strerror(errno));

    /* determine how many indices to keep */
    no_keep = getconf_int(CNF_TAPECYCLE) + 1;
    dbprintf(("Keeping %d index file%s\n", no_keep, (no_keep == 1) ? "" : "s"));

    level_position = strlen(COMPRESS_SUFFIX);
    datestamp_position = level_position + 9;

    /* now go through the list of disks and find which have indexes */
    cmd = NULL;
    for (diskp = diskl->head; diskp != NULL; diskp = diskp->next)
    {
	if (diskp->index)
	{
	    char *host;
	    char *disk;

	    dbprintf(("%s %s\n", diskp->host->hostname, diskp->name));

	    /* get listing of indices, newest first */
	    host = stralloc(sanitise_filename(diskp->host->hostname));
	    disk = stralloc(sanitise_filename(diskp->name));
	    cmd = newvstralloc(cmd,
			       "ls", " -r",
			       " ", "\'", host, "\'",
			       "/", "\'", disk, "\'",
			       "/", "????????",
			       "_", "?",
			       COMPRESS_SUFFIX,
			       NULL);
	    amfree(host);
	    amfree(disk);
	    if ((fp = popen(cmd, "r")) == NULL) {
		error("couldn't open cmd \"%s\".", cmd);
	    }

	    /* skip over the first no_keep indices */
	    for (i = 0; i < no_keep && (line = agets(fp)) != NULL; i++) {}

	    /* skip indices until find a level 0 */
	    while ((line = agets(fp)) != NULL) {
		int len;

		if ((len = strlen(line)) < level_position + 1) {
		    error("file name \"%s\" too short.", line);
		}
		if (line[len - level_position - 1] == '0') {
		    break;
		}
	    }

	    /* okay, delete the rest */
	    while ((line = agets(fp)) != NULL) {
		int len;
		char datestamp[20];
		int level;

		len = strlen(line);
		strncpy(datestamp, &line[len - datestamp_position - 1], 
			sizeof(datestamp) );
		datestamp[8] = '\0';
		level = line[len - level_position - 1] - '0';
		if(!dump_exist(output_find, diskp->host->hostname,diskp->name, 
			       atoi(datestamp), level)) {
		    dbprintf(("rm %s\n", line));
		    if (remove(line) == -1) {
			dbprintf(("Error removing \"%s\": %s\n",
				  line, strerror(errno)));
		    }
		}
	    }
	    apclose(fp);
	}
    }

    amfree(cmd);
    dbclose();

    return 0;
}
