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
 * $Id: amtrmidx.c,v 1.11 1997/12/16 20:44:55 jrj Exp $
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
#include "version.h"

char *pname = "amtrmidx";

int main(argc, argv)
int argc;
char **argv;
{
    char buf[1024];
    char cmd[1024];
    disk_t *diskp;
    disklist_t *diskl;
    int no_keep;			/* files per system to keep */
    int i;
    int level_position;			/* where (from end) is level in name */
    FILE *fp;
    char *ptr;

    if (argc != 2)
    {
	fprintf(stderr, "Usage: %s <config>\n", argv[0]);
	return 1;
    }

    dbopen();
    dbprintf(("%s: version %s\n", argv[0], version()));

    /* get the list of disks being dumped and their types */
    ap_snprintf(buf, sizeof(buf), "%s/%s", CONFIG_DIR, argv[1]);
    if (chdir(buf) != 0)
	error("could not cd to confdir %s: %s", buf, strerror(errno));
    if (read_conffile(CONFFILE_NAME))
	error("could not read amanda config file");
    if ((diskl = read_diskfile(getconf_str(CNF_DISKFILE))) == NULL)
	error("could not load \"%s\".", getconf_str(CNF_DISKFILE));

    /* determine how many indices to keep */
    no_keep = getconf_int(CNF_TAPECYCLE) + 1;
    dbprintf(("Keeping %d index files\n", no_keep));

    /* change into the index directory */
    if (chdir(INDEX_DIR) == -1)
	error("couldn't change into index root directory \"%s\".", INDEX_DIR);
    if (chdir(getconf_str(CNF_INDEXDIR)) == -1)
	error("couldn't change into index config directory \"%s\".",
	      getconf_str(CNF_INDEXDIR));

    level_position = strlen(COMPRESS_SUFFIX)+1;

    /* now go through the list of disks and find which have indexes */
    for (diskp = diskl->head; diskp != NULL; diskp = diskp->next)
    {
	if (diskp->index)
	{
	    dbprintf(("%s %s\n", diskp->host->hostname, diskp->name));

	    /* map '/' chars to '_' */
	    for (ptr = diskp->name; *ptr != '\0'; ptr++)
		if ( *ptr == '/' )
		    *ptr = '_';

	    /* get listing of indices, newest first */
	    /* We have to be careful here of
	     * trigraph replacement by some compilers.  */
	    ap_snprintf(cmd, sizeof(buf), "ls -r '%s_%s_'????%s-??_?%s",
			diskp->host->hostname, diskp->name,
			"-??", COMPRESS_SUFFIX);
	    if ((fp = popen(cmd, "r")) == NULL)
		error("couldn't open cmd \"%s\".", cmd);

	    /* skip over the first no_keep indices */
	    for (i = 0; i < no_keep; i++)
		if (fgets(buf, 1024, fp) == NULL)
		    break;
	    if (i < no_keep)
	    {
		(void)pclose(fp);
		continue;		/* not enough to consider */
	    }

	    /* skip indices until find a level 0 */
	    while (fgets(buf, 1024, fp) != NULL)
	    {
		if (strlen(buf) < 11+level_position)
		    error("file name \"%s\" too short.", buf);
		buf[strlen(buf)-1] = '\0'; /* get rid of \n */
		if (buf[strlen(buf)-level_position] == '0')
		    break;
	    }

	    /* okay, delete the rest */
	    while (fgets(buf, 1024, fp) != NULL)
	    {
		buf[strlen(buf)-1] = '\0'; /* get rid of \n */
		dbprintf(("rm %s\n", buf));
		if (remove(buf) == -1)
		    dbprintf(("Error %d removing \"%s\"\n", errno, buf));
	    }

	    (void)pclose(fp);
	}
    }

    dbclose();

    return 0;
}
