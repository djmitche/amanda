/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1997 University of Maryland
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
 * Author: AMANDA core development group.
 */
/*
 * $Id: file.c,v 1.2 1997/11/02 23:13:45 george Exp $
 *
 * file and directory bashing routines
 */

#include "amanda.h"

/* Make a directory hierarchy.
** XXX - I'm not sure about the use of the chown() stuff.  On most systems
**       it will do nothing - only root is permitted to change the owner
**       of a file.
*/
int mkpdir(file, mode, uid, gid)
char *file;	/* file to create parent directories for */
int mode;	/* mode for new directories */
uid_t uid;	/* uid for new directories */
gid_t gid;	/* gid for new directories */
{
    char *dir, *p;
    int rc;	/* return code */

    dir = stralloc(file);	/* make a copy we can play with */

    p = strrchr(dir, '/');
    if(p == dir)
	rc = 0; /* no /'s */
    else {
	*p = '\0';

	if(access(dir, F_OK) == 0)
	    rc = 0; /* already exists */
	else {
	    if(mkpdir(dir, mode, uid, gid) == 0 &&
	       mkdir(dir, mode) == 0 &&
	       chmod(dir, mode) == 0) {	/* mkdir() is affected by the umask */
		chown(dir, uid, gid);
		rc = 0; /* all done */
	    }
	    else
		rc = -1; /* create failed */
	}
    }

    free(dir);
    return rc;
}


/* Remove as much of a directory hierarchy as possible.
** Notes:
**  - assumes that rmdir() on a non-empty directory will fail!
**  - stops deleting before topdir, ie: topdir will not be removed
**  - if file is not under topdir this routine will not notice
*/
int rmpdir(file, topdir)
char *file;	/* directory hierarchy to remove */
char *topdir;	/* where to stop removing */
{
    int rc;
    char *p, *dir;

    if(strcmp(file, topdir) == 0) return 0; /* all done */

    rc = rmdir(file);
    if (rc != 0) switch(errno) {
#ifdef ENOTEMPTY
	case ENOTEMPTY:
#endif
	case EEXIST:	/* directory not empty */
	    return 0; /* cant do much more */
	case ENOENT:	/* it has already gone */
	    rc = 0; /* ignore */
	    break;
	case ENOTDIR:	/* it was a file */
	    rc = unlink(file);
	    break;
	}

    if(rc != 0) return -1; /* unexpected error */

    dir = stralloc(file);

    p = strrchr(dir, '/');
    if(p == dir) rc = 0; /* no /'s */
    else {
	*p = '\0';

	rc = rmpdir(dir, topdir);
    }

    free(dir);

    return rc;
}


/*
** Sanitise a file name.
** 
** Convert all funny characters to '_' so that we can use,
** for example, disk names as part of file names.
** Notes: 
**  - the internal buffer is static.
**  - there is a many-to-one mapping between input and output
** XXX - We only look for '/' and ' ' at the moment.  May
** XXX - be we should also do all unprintables.
*/
char *sanitise_filename(inp)
char *inp;
{
    static char buf[512]; /* XXX string overflow */
    char *s, *d;

    d = buf;
    for(s = inp; *s != '\0'; s++) {
	switch(*s) {
	case '_':	/* convert _ to __ to try and ensure unique output */
	    *d++ = '_';
	    /* fall through */
	case '/':
	case ' ':	/* convert ' ' for convenience */
	    *d++ = '_';
	    break;
        default:
	    *d++ = *s;
	}
    }
    *d = '\0';

    return buf;
}

#ifdef TEST

char *pname = "file test";

int main() {

	int rc;

	printf("Create...");
	rc = mkpdir("/tmp/a/b/c/d/e", 0777, (uid_t)-1, (gid_t)-1);
	if (rc == 0)
		printf("done\n");
	else {
		perror("failed");
		return rc;
	}

	printf("Delete...");
	rc = rmpdir("/tmp/a/b/c/d/e", "/tmp");
	if (rc == 0)
		printf("done\n");
	else {
		perror("failed");
		return rc;
	}

	printf("Finished.\n");
	return 0;
}

#endif
