/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1997-1998 University of Maryland at College Park
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
 * $Id: file.c,v 1.15 1999/03/04 22:40:02 kashmir Exp $
 *
 * file and directory bashing routines
 */

#include "amanda.h"

/* Make a directory (internal function).
** If the directory already exists then we pretend we created it.
** XXX - I'm not sure about the use of the chown() stuff.  On most systems
**       it will do nothing - only root is permitted to change the owner
**       of a file.
*/
int mk1dir(dir, mode, uid, gid)
char *dir;	/* directory to create */
int mode;	/* mode for new directory */
uid_t uid;	/* uid for new directory */
gid_t gid;	/* gid for new directory */
{
    int rc;	/* return code */

    rc = 0;	/* assume the best */

    if(mkdir(dir, mode) == 0) {
	chmod(dir, mode);	/* mkdir() is affected by the umask */
	chown(dir, uid, gid);	/* XXX - no-op on most systems? */
    }
    else {	/* maybe someone beat us to it */
	int serrno;

	serrno = errno;
	if(access(dir, F_OK) != 0) rc = -1;
	errno = serrno;	/* pass back the real error */
    }

    return rc;
}


/* Make a directory hierarchy.
*/
int mkpdir(file, mode, uid, gid)
char *file;	/* file to create parent directories for */
int mode;	/* mode for new directories */
uid_t uid;	/* uid for new directories */
gid_t gid;	/* gid for new directories */
{
    char *dir = NULL, *p;
    int rc;	/* return code */

    rc = 0;

    dir = stralloc(file);	/* make a copy we can play with */

    p = strrchr(dir, '/');
    if(p != dir) {	/* got a '/' */
	*p = '\0';

	if(access(dir, F_OK) != 0) {	/* doesn't exist */
	    if(mkpdir(dir, mode, uid, gid) != 0 ||
	       mk1dir(dir, mode, uid, gid) != 0) rc = -1; /* create failed */
	}
    }

    amfree(dir);
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
    char *p, *dir = NULL;

    if(strcmp(file, topdir) == 0) return 0; /* all done */

    rc = rmdir(file);
    if (rc != 0) switch(errno) {
#ifdef ENOTEMPTY
#if ENOTEMPTY != EEXIST			/* AIX makes these the same */
	case ENOTEMPTY:
#endif
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

    amfree(dir);

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
    char *buf;
    int buf_size;
    char *s, *d;
    int ch;

    buf_size = 2 * strlen(inp) + 1;		/* worst case */
    buf = alloc(buf_size);
    d = buf;
    s = inp;
    while((ch = *s++) != '\0') {
	if(ch == '_') {
	    if(d >= buf + buf_size) {
		return NULL;			/* cannot happen */
	    }
	    *d++ = '_';				/* convert _ to __ to try */
						/* and ensure unique output */
	} else if(ch == '/' || isspace(ch)) {
	    ch = '_';	/* convert "bad" to "_" */
	}
	if(d >= buf + buf_size) {
	    return NULL;			/* cannot happen */
	}
	*d++ = ch;
    }
    if(d >= buf + buf_size) {
	return NULL;				/* cannot happen */
    }
    *d = '\0';

    return buf;
}

/*
 *=====================================================================
 * Get the next line of input from a stdio file.
 *
 * char *agets (FILE *f)
 *
 * entry:	f = stdio stream to read
 * exit:	returns a pointer to an alloc'd string or NULL at EOF
 *		or error (errno will be zero on EOF).
 *
 * Notes:	the newline, if read, is removed from the string
 *		the caller is responsible for free'ing the string
 *=====================================================================
 */

char *
#if defined(USE_DBMALLOC)
dbmalloc_agets(s, l, file)
    char *s;
    int l;
    FILE *file;
#else
agets(file)
    FILE *file;
#endif
{
    char *line = NULL, *line_ptr;
    int line_size, line_free, size_save, line_len;
    char *cp;
    char *f;

    malloc_enter(dbmalloc_caller_loc(s, l));

#define	AGETS_LINE_INCR	128

    line_size = AGETS_LINE_INCR;
    line = alloc (line_size);
    line_free = line_size;
    line_ptr = line;
    line_len = 0;

    while ((f = fgets(line_ptr, line_free, file)) != NULL) {
	/*
	 * Note that we only have to search what we just read, not
	 * the whole buffer.
	 */
	if ((cp = strchr (line_ptr, '\n')) != NULL) {
	    line_len += cp - line_ptr;
	    *cp = '\0';				/* zap the newline */
	    break;				/* got to end of line */
	}
	line_len += line_free - 1;		/* bytes read minus '\0' */
	size_save = line_size;
	line_size += AGETS_LINE_INCR;		/* get more space */
	cp = alloc (line_size);
	memcpy (cp, line, size_save);		/* copy old to new */
	free (line);				/* and release the old */
	line = cp;
	line_ptr = line + size_save - 1;	/* start at the null byte */
	line_free = AGETS_LINE_INCR + 1;	/* and we get to use it */
    }
    /*
     * Return what we got even if there was not a newline.  Only
     * report done (NULL) when no data was processed.
     */
    if (f == NULL && line_len == 0) {
	amfree (line);
	line = NULL;				/* redundant, but clear */
	if(!ferror(file)) {
	    errno = 0;				/* flag EOF vs error */
	}
    }
    malloc_leave(dbmalloc_caller_loc(s, l));
    return line;
}

/*
 *=====================================================================
 * Get the next line of input from a file descriptor.
 *
 * char *areads (int fd)
 *
 * entry:	fd = file descriptor to read
 * exit:	returns a pointer to an alloc'd string or NULL at EOF
 *		or error (errno will be zero on EOF).
 *
 * Notes:	the newline, if read, is removed from the string
 *		the caller is responsible for free'ing the string
 *=====================================================================
 */

char *
#if defined(USE_DBMALLOC)
dbmalloc_areads (s, l, fd)
    char *s;
    int l;
    int fd;
#else
areads (fd)
    int fd;
#endif
{
    char *nl;
    char *line;
    static char buffer[BUFSIZ+1];
    static char *line_buffer = NULL;
    char *t;
    ssize_t r;

    malloc_enter(dbmalloc_caller_loc(s, l));

    while(1) {
	/*
	 * First, see if we have a line in the buffer.
	 */
	if(line_buffer) {
	    if((nl = strchr(line_buffer, '\n')) != NULL) {
		*nl++ = '\0';
		line = stralloc(line_buffer);
		if(*nl) {
		    t = stralloc(nl);		/* save data still in buffer */
		} else {
		    t = NULL;
		}
		amfree(line_buffer);
		line_buffer = t;
		malloc_leave(dbmalloc_caller_loc(s, l));
		return line;
	    }
	}
	/*
	 * Now, get more data and loop back to check for a completed
	 * line again.
	 */
	if ((r = read(fd, buffer, sizeof(buffer)-1)) <= 0) {
	    if(r == 0) {
		errno = 0;			/* flag EOF instead of error */
	    }
	    amfree(line_buffer);
	    malloc_leave(dbmalloc_caller_loc(s, l));
	    return NULL;
	}
	buffer[r] = '\0';
	if(line_buffer) {
	    strappend(line_buffer, buffer);
	} else {
	    line_buffer = stralloc(buffer);
	}
    }
}

#ifdef TEST

int main(argc, argv)
	int argc;
	char **argv;
{
	int rc;
	int fd;
	char *name;
	char *top;

	for(fd = 3; fd < FD_SETSIZE; fd++) {
		/*
		 * Make sure nobody spoofs us with a lot of extra open files
		 * that would cause an open we do to get a very high file
		 * descriptor, which in turn might be used as an index into
		 * an array (e.g. an fd_set).
		 */
		close(fd);
	}

	set_pname("file test");

	if (argc > 2) {
		name = *++argv;
		top = *++argv;
	} else {
		name = "/tmp/a/b/c/d/e";
		top = "/tmp";
	}

	printf("Create %s ...", name);
	rc = mkpdir(name, 0777, (uid_t)-1, (gid_t)-1);
	if (rc == 0)
		printf("done\n");
	else {
		perror("failed");
		return rc;
	}

	printf("Delete %s back to %s ...", name, top);
	rc = rmpdir(name, top);
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
