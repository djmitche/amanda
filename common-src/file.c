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
 * $Id: file.c,v 1.14.4.8 2001/07/11 20:54:34 jrjackson Exp $
 *
 * file and directory bashing routines
 */

#include "amanda.h"
#include "util.h"

uid_t client_uid = (uid_t) -1;
gid_t client_gid = (gid_t) -1;

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
    } else {			/* maybe someone beat us to it */
	int serrno;

	serrno = errno;
	if(access(dir, F_OK) != 0) rc = -1;
	errno = serrno;	/* pass back the real error */
    }

    return rc;
}


/*
 * Make a directory hierarchy given an entry to be created (by the caller)
 * in the new target.  In other words, create all the directories down to
 * the last element, but not the last element.  So a (potential) file name
 * may be passed to mkpdir and all the parents of that file will be created.
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
    if(p != dir && p != NULL) {	/* got a '/' or a simple name */
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
 *=====================================================================
 * Do Amanda setup for all programs.
 *
 * void amanda_setup (int argc, char **argv, int setup_flags)
 *
 * entry:	setup_flags (see AMANDA_SETUP_FLAG_xxx)
 * exit:	none
 *=====================================================================
 */

void
amanda_setup (argc, argv, setup_flags)
    int			argc;
    char		**argv;
    int			setup_flags;
{
}

/*
 *=====================================================================
 * Change directory to a "safe" location and set some base environment.
 *
 * void safe_cd (void)
 *
 * entry:	client_uid and client_gid set to CLIENT_LOGIN information
 * exit:	none
 *
 * Set a default umask of 0077.
 *
 * Create the Amada debug directory (if defined) and the Amanda temp
 * directory.
 *
 * Try to chdir to the Amanda debug directory first, but it must be owned
 * by the Amanda user and not allow rwx to group or other.  Otherwise,
 * try the same thing to the Amanda temp directory.
 *
 * If that is all OK, call save_core().
 *
 * Otherwise, cd to "/" so if we take a signal we cannot drop core
 * unless the system administrator has made special arrangements (e.g.
 * pre-created a core file with the right ownership and permissions).
 *=====================================================================
 */

void
safe_cd()
{
    int			cd_ok = 0;
    struct stat		sbuf;
    struct passwd	*pwent;
    char		*d;

    if(client_uid == (uid_t) -1 && (pwent = getpwnam(CLIENT_LOGIN)) != NULL) {
	client_uid = pwent->pw_uid;
	client_gid = pwent->pw_gid;
    }

    (void) umask(0077);

    if (client_uid != (uid_t) -1) {
#if defined(AMANDA_DBGDIR)
	d = stralloc2(AMANDA_DBGDIR, "/.");
	(void) mkpdir(AMANDA_DBGDIR, 02700, client_uid, client_gid);
	amfree(d);
#endif
	d = stralloc2(AMANDA_TMPDIR, "/.");
	(void) mkpdir(AMANDA_TMPDIR, 02700, client_uid, client_gid);
	amfree(d);
    }

#if defined(AMANDA_DBGDIR)
    if (chdir(AMANDA_DBGDIR) != -1
	&& stat(".", &sbuf) != -1
	&& (sbuf.st_mode & 0777) == 0700	/* drwx------ */
	&& sbuf.st_uid == client_uid) {		/* owned by Amanda user */
	cd_ok = 1;				/* this is a good place to be */
    }
#endif
    if (! cd_ok
	&& chdir(AMANDA_TMPDIR) != -1
	&& stat(".", &sbuf) != -1
	&& (sbuf.st_mode & 0777) == 0700	/* drwx------ */
	&& sbuf.st_uid == client_uid) {		/* owned by Amanda user */
	cd_ok = 1;				/* this is a good place to be */
    }
    if(cd_ok) {
	save_core();				/* save any old core file */
    } else {
	(void) chdir("/");			/* assume this works */
    }
}

/*
 *=====================================================================
 * Save an existing core file.
 *
 * void save_core (void)
 *
 * entry:	none
 * exit:	none
 *
 * Renames:
 *
 *	"core"          to "coreYYYYMMDD",
 *	"coreYYYYMMDD"  to "coreYYYYMMDDa",
 *	"coreYYYYMMDDa" to "coreYYYYMMDDb",
 *	...
 *
 * ... where YYYYMMDD is the modification time of the original file.
 * If it gets that far, an old "coreYYYYMMDDz" is thrown away.
 *=====================================================================
 */

void
save_core()
{
    struct stat sbuf;

    if(stat("core", &sbuf) != -1) {
        char *ts;
        char suffix[2];
        char *old, *new;

	ts = construct_datestamp((time_t *)&sbuf.st_mtime);
        suffix[0] = 'z';
        suffix[1] = '\0';
        old = vstralloc("core", ts, suffix, NULL);
        new = NULL;
        while(ts[0] != '\0') {
            amfree(new);
            new = old;
            if(suffix[0] == 'a') {
                suffix[0] = '\0';
            } else if(suffix[0] == '\0') {
                ts[0] = '\0';
            } else {
                suffix[0]--;
            }
            old = vstralloc("core", ts, suffix, NULL);
            (void)rename(old, new);         /* it either works ... */
        }
	amfree(ts);
        amfree(old);
        amfree(new);
    }
}

/*
** Sanitise a file name.
** 
** Convert all funny characters to '_' so that we can use,
** for example, disk names as part of file names.
** Notes: 
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
	if (line_size < 256 * AGETS_LINE_INCR) {
	    line_size *= 2;
	} else {
	    line_size += 256 * AGETS_LINE_INCR;
	}
	cp = alloc (line_size);			/* get more space */
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
 * Find/create a buffer for a particular file descriptor for use with
 * areads().
 *
 * void areads_getbuf (int fd)
 *
 * entry:	fd = file descriptor to look up
 * exit:	returns a pointer to the buffer, possibly new
 *=====================================================================
 */

static struct areads_buffer {
    char *buffer;
    char *endptr;
    ssize_t bufsize;
} *areads_buffer = NULL;
static int areads_bufcount = 0;
static ssize_t areads_bufsize = BUFSIZ;		/* for the test program */

static void
areads_getbuf(fd)
    int fd;
{
    struct areads_buffer *new;
    ssize_t size;

    assert(fd >= 0);
    if(fd >= areads_bufcount) {
	size = (fd + 1) * sizeof(*areads_buffer);
	new = (struct areads_buffer *) alloc(size);
	memset((char *)new, 0, size);
	if(areads_buffer) {
	    size = areads_bufcount * sizeof(*areads_buffer);
	    memcpy(new, areads_buffer, size);
	}
	amfree(areads_buffer);
	areads_buffer = new;
	areads_bufcount = fd + 1;
    }
    if(areads_buffer[fd].buffer == NULL) {
	areads_buffer[fd].bufsize = areads_bufsize;
	areads_buffer[fd].buffer = alloc(areads_buffer[fd].bufsize + 1);
	areads_buffer[fd].buffer[0] = '\0';
	areads_buffer[fd].endptr = areads_buffer[fd].buffer;
    }
}

/*
 *=====================================================================
 * Return the amount of data still in an areads buffer.
 *
 * ssize_t areads_dataready (int fd)
 *
 * entry:	fd = file descriptor to release buffer for
 * exit:	returns number of bytes of data ready to process
 *=====================================================================
 */

ssize_t
areads_dataready(fd)
    int fd;
{
    ssize_t r = 0;

    if(fd >= 0 && fd < areads_bufcount && areads_buffer[fd].buffer != NULL) {
	r = areads_buffer[fd].endptr - areads_buffer[fd].buffer;
    }
    return r;
}

/*
 *=====================================================================
 * Release a buffer for a particular file descriptor used by areads().
 *
 * void areads_relbuf (int fd)
 *
 * entry:	fd = file descriptor to release buffer for
 * exit:	none
 *=====================================================================
 */

void
areads_relbuf(fd)
    int fd;
{
    if(fd >= 0 && fd < areads_bufcount) {
	amfree(areads_buffer[fd].buffer);
	areads_buffer[fd].endptr = NULL;
	areads_buffer[fd].bufsize = 0;
    }
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
    char *buffer;
    char *endptr;
    char *newbuf;
    ssize_t buflen;
    ssize_t size;
    ssize_t r;

    malloc_enter(dbmalloc_caller_loc(s, l));

    if(fd < 0) {
	errno = EBADF;
	return NULL;
    }
    areads_getbuf(fd);
    buffer = areads_buffer[fd].buffer;
    endptr = areads_buffer[fd].endptr;
    buflen = areads_buffer[fd].bufsize - (endptr - buffer);
    while((nl = strchr(buffer, '\n')) == NULL) {
	/*
	 * No newline yet, so get more data.
	 */
	if (buflen == 0) {
	    if ((size = areads_buffer[fd].bufsize) < 256 * areads_bufsize) {
		size *= 2;
	    } else {
		size += 256 * areads_bufsize;
	    }
	    newbuf = alloc(size + 1);
	    memcpy (newbuf, buffer, areads_buffer[fd].bufsize + 1);
	    amfree(areads_buffer[fd].buffer);
	    buffer = NULL;
	    areads_buffer[fd].buffer = newbuf;
	    areads_buffer[fd].endptr = newbuf + areads_buffer[fd].bufsize;
	    areads_buffer[fd].bufsize = size;
	    buffer = areads_buffer[fd].buffer;
	    endptr = areads_buffer[fd].endptr;
	    buflen = areads_buffer[fd].bufsize - (endptr - buffer);
	}
	if ((r = read(fd, endptr, buflen)) <= 0) {
	    if(r == 0) {
		errno = 0;		/* flag EOF instead of error */
	    }
	    malloc_leave(dbmalloc_caller_loc(s, l));
	    return NULL;
	}
	endptr[r] = '\0';		/* we always leave room for this */
	endptr += r;
	buflen -= r;
    }
    *nl++ = '\0';
    line = stralloc(buffer);
    size = endptr - nl;			/* data still left in buffer */
    memmove(buffer, nl, size);
    areads_buffer[fd].endptr = buffer + size;
    areads_buffer[fd].endptr[0] = '\0';
    malloc_leave(dbmalloc_caller_loc(s, l));
    return line;
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
	char *file;
	char *line;

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

	name = "/tmp/a/b/c/d/e";
	if (argc > 2 && argv[1][0] != '\0') {
		name = argv[1];
	}
	top = "/tmp";
	if (argc > 3 && argv[2][0] != '\0') {
		name = argv[2];
	}
	file = "/etc/hosts";
	if (argc > 4 && argv[3][0] != '\0') {
		name = argv[3];
	}

	fprintf(stderr, "Create parent directories of %s ...", name);
	rc = mkpdir(name, 02777, (uid_t)-1, (gid_t)-1);
	if (rc == 0)
		fprintf(stderr, " done\n");
	else {
		perror("failed");
		return rc;
	}

	fprintf(stderr, "Delete %s back to %s ...", name, top);
	rc = rmpdir(name, top);
	if (rc == 0)
		fprintf(stderr, " done\n");
	else {
		perror("failed");
		return rc;
	}

	fprintf(stderr, "areads dump of %s ...", file);
	if ((fd = open (file, 0)) < 0) {
		perror(file);
		return 1;
	}
	areads_bufsize = 1;			/* force buffer overflow */
	while ((line = areads(fd)) != NULL) {
		puts(line);
		amfree(line);
	}
	aclose(fd);
	fprintf(stderr, " done.\n");

	fprintf(stderr, "Finished.\n");
	return 0;
}

#endif
