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
 * $Id: output-file.c,v 1.1.4.3 2001/06/29 23:39:55 jrjackson Exp $
 *
 * tapeio.c virtual tape interface for a file device.
 *
 * The following was based on testing with real tapes on Solaris 2.6.
 * It is possible other OS drivers behave somewhat different in end
 * cases, usually involving errors.
 */

#include "amanda.h"

#include "token.h"
#include "tapeio.h"
#include "output-file.h"
#include "fileheader.h"

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

#define	MAX_TOKENS	10

struct file_info {
    int flags;				/* open flags */
    int mask;				/* open mask */
    int file_current;			/* current file position */
    int file_count;			/* number of files */
    char *basename;			/* filename from open */
    char **filename;			/* list of file names */
    int file_limit;			/* length of filename array */
    int fd;				/* data file descriptor */
    int is_online;			/* true if "tape" is "online" */
    int at_bof;				/* true if at begining of file */
    int at_eof;				/* true if at end of file */
    int at_eom;				/* true if at end of medium */
    int last_operation_write;		/* true if last op was a write */
    long amount_written;		/* KBytes written since open/rewind */
} *file_info = NULL;

static int open_count = 0;

/*
 * "Open" the tape by scanning the "data" directory.  "Tape files"
 * have five leading digits indicating the position (counting from zero)
 * followed by a '.' and optional other information (e.g. host/disk/level
 * image name).
 *
 * We allow for the following situations:
 *
 *   + If we see the same "file" (position number) more than once, the
 *     last one seen wins.  This should not normally happen.
 *
 *   + We allow gaps in the positions.  This should not normally happen.
 *
 * Anything in the directory that does not match a "tape file" name
 * pattern is ignored.
 *
 * If the data directory does not exist, the "tape" is considered offline.
 * It is allowed to "appear" later.
 */

static int
check_online(fd)
    int fd;
{
    char *token[MAX_TOKENS];
    DIR *tapedir;
    struct dirent *entry;
    char *line;
    int f;
    int fn;
    int rc = 0;

    /*
     * If we are already online, there is nothing else to do.
     */
    if (file_info[fd].is_online) {
	goto common_exit;
    }

    if ((tapedir = opendir(file_info[fd].basename)) == NULL) {
	/*
	 * We have already opened the info file which is in the same
	 * directory as the data directory, so ENOENT has to mean the data
	 * directory is not there, which we treat as being "offline".
	 * We're already offline at this point (see the above test)
	 * and this is not an error, so just return success (no error).
	 */
	rc = (errno != ENOENT);
	goto common_exit;
    }
    while ((entry = readdir(tapedir)) != NULL) {
	if (is_dot_or_dotdot(entry->d_name)) {
	    continue;
	}
	if (isdigit((int)entry->d_name[0])
	    && isdigit((int)entry->d_name[1])
	    && isdigit((int)entry->d_name[2])
	    && isdigit((int)entry->d_name[3])
	    && isdigit((int)entry->d_name[4])
	    && entry->d_name[5] == '.') {

	    /*
	     * This is a "tape file".
	     */
	    fn = atoi(entry->d_name);
	    amtable_alloc((void **)&file_info[fd].filename,
			  sizeof(*file_info[fd].filename),
			  fn,
			  &file_info[fd].file_limit,
			  10,
			  NULL);
	    if (file_info[fd].filename[fn] != NULL) {
		/*
		 * Two files with the same position???
		 */
		amfree(file_info[fd].filename[fn]);
	    }
	    file_info[fd].filename[fn] = stralloc(entry->d_name);
	    if (fn + 1 > file_info[fd].file_count) {
		file_info[fd].file_count = fn + 1;
	    }
	}
    }
    closedir(tapedir);

    /*
     * Parse the info file.  We know we are at beginning of file because
     * the only thing that can happen to it prior to here is it being
     * opened.
     */
    for (; (line = areads(fd)) != NULL; free(line)) {
	f = split(line, token, sizeof(token) / sizeof(token[0]), " ");
	if (f == 2 && strcmp(token[1], "position") == 0) {
	    file_info[fd].file_current = atoi(token[2]);
	}
    }

    /*
     * Set EOM and make sure we are not pre-BOI.
     */
    if (file_info[fd].file_current >= file_info[fd].file_count) {
	file_info[fd].at_eom = 1;
    }
    if (file_info[fd].file_current < 0) {
	file_info[fd].file_current = 0;
    }

    file_info[fd].is_online = 1;

common_exit:

    return rc;
}

/*
 * Open the tape file if not already.  If we are beyond the file count
 * (end of tape) or the file is missing and we are only reading, set
 * up to read /dev/null which will look like EOF.  If we are writing,
 * create the file.
 */

static int
file_open(fd)
    int fd;
{
    char *filename = NULL;
    char *f = NULL;
    int pos;
    char *host;
    char *disk;
    int level;
    char number[NUM_STR_SIZE];
    int flags;

    if (file_info[fd].fd < 0) {
	flags = file_info[fd].flags;
	pos = file_info[fd].file_current;
	amtable_alloc((void **)&file_info[fd].filename,
		      sizeof(*file_info[fd].filename),
		      pos,
		      &file_info[fd].file_limit,
		      10,
		      NULL);

	/*
	 * See if we are creating a new file.
	 */
	if (pos >= file_info[fd].file_count) {
	    file_info[fd].file_count = pos + 1;
	}

	/*
	 * Generate the file name to open.
	 */
	if (file_info[fd].filename[pos] == NULL) {
	    if ((file_info[fd].flags & 3) != O_RDONLY) {

		/*
		 * This is a new file, so make sure we create/truncate
		 * it.	Generate the name based on the host/disk/level
		 * information from the caller, if available, else
		 * a constant.
		 */
		flags |= (O_CREAT | O_TRUNC);
		host = tapefd_getinfo_host(fd);
		disk = tapefd_getinfo_disk(fd);
		level = tapefd_getinfo_level(fd);
		ap_snprintf(number, sizeof(number), "%d", level);
		if (host != NULL) {
		    f = stralloc(host);
		}
		if (disk != NULL) {
		    disk = sanitise_filename(disk);
		    if (f == NULL) {
			f = stralloc(disk);
		    } else {
			f = newvstralloc(f, f, ".", disk, NULL);
		    }
		    amfree(disk);
		}
		if (level >= 0) {
		    if (f == NULL) {
			f = stralloc(number);
		    } else {
			f = newvstralloc(f, f, ".", number, NULL);
		    }
		}
		if (f == NULL) {
		    f = stralloc("unknown");
		}
		ap_snprintf(number, sizeof(number), "%05d", pos);
		amfree(file_info[fd].filename[pos]);
		file_info[fd].filename[pos] = vstralloc(number,
							".",
							f,
							NULL);
		amfree(f);
	    } else {

		/*
		 * This is a missing file, so set up to read nothing.
		 */
		filename = stralloc("/dev/null");
	    }
	}
	if (filename == NULL) {
	    filename = stralloc2(file_info[fd].basename,
				 file_info[fd].filename[pos]);
	}

	/*
	 * Do the data file open.
	 */
	file_info[fd].fd = open(filename, flags, file_info[fd].mask);
	amfree(filename);
    }
    return file_info[fd].fd;
}

/*
 * Release any files beyond a given position current position and reset
 * file_count to file_current to indicate EOM.
 */

static void
file_release(fd)
    int fd;
{
    int position;
    char *filename;
    int pos;

    /*
     * If the current file is open, release everything beyond it.
     * If it is not open, release everything from current.
     */
    if (file_info[fd].fd >= 0) {
	position = file_info[fd].file_current + 1;
    } else {
	position = file_info[fd].file_current;
    }
    for (pos = position; pos < file_info[fd].file_count; pos++) {
	amtable_alloc((void **)&file_info[fd].filename,
		      sizeof(*file_info[fd].filename),
		      pos,
		      &file_info[fd].file_limit,
		      10,
		      NULL);
	if (file_info[fd].filename[pos] != NULL) {
	    filename = stralloc2(file_info[fd].basename,
				 file_info[fd].filename[pos]);
	    unlink(filename);
	    amfree(filename);
	    amfree(file_info[fd].filename[pos]);
	}
    }
    file_info[fd].file_count = position;
}

/*
 * The normal interface routines ...
 */

int
file_tape_open(filename, flags, mask)
    char *filename;
    int flags;
    int mask;
{
    int fd = -1;
    int save_errno;
    char *info_file = NULL;

    /*
     * Use only O_RDONLY and O_RDWR.
     */
    if ((flags & 3) != O_RDONLY) {
	flags &= ~3;
	flags |= O_RDWR;
    }

    /*
     * Open/create the info file for this "tape".
     */
    info_file = stralloc2(filename, "/info");
    if ((fd = open(info_file, O_RDWR|O_CREAT, 0600)) < 0) {
	goto common_exit;
    }

    /*
     * Create the internal info structure for this "tape".
     */
    amtable_alloc((void **)&file_info,
		  sizeof(*file_info),
		  fd,
		  &open_count,
		  10,
		  NULL);
    file_info[fd].flags = flags;
    file_info[fd].mask = mask;
    file_info[fd].file_current = 0;
    file_info[fd].file_count = 0;
    file_info[fd].fd = -1;
    file_info[fd].is_online = 0;		/* true when .../data found */
    file_info[fd].at_bof = 1;			/* by definition */
    file_info[fd].at_eof = 0;			/* do not know yet */
    file_info[fd].at_eom = 0;			/* may get reset below */
    file_info[fd].last_operation_write = 0;
    file_info[fd].amount_written = 0;

    /*
     * Save the base directory name and see if we are "online".
     */
    file_info[fd].basename = stralloc2(filename, "/data/");
    if (check_online(fd)) {
	save_errno = errno;
	aclose(fd);
	fd = -1;
	amfree(file_info[fd].basename);
	errno = save_errno;
	goto common_exit;
    }

common_exit:

    amfree(info_file);

    /*
     * Return the info file descriptor as the unique descriptor for
     * this open.
     */
    return fd;
}

int
file_tapefd_read(fd, buffer, count)
    int fd, count;
    void *buffer;
{
    int result;
    int file_fd;

    /*
     * Make sure we are online.
     */
    if ((result = check_online(fd)) != 0) {
	return result;
    }
    if (! file_info[fd].is_online) {
	errno = EIO;
	return -1;
    }

    /*
     * Do not allow any more reads after we find EOF.
     */
    if (file_info[fd].at_eof) {
	errno = EIO;
	return -1;
    }

    /*
     * If we are at EOM, set EOF and return a zero length result.
     */
    if (file_info[fd].at_eom) {
	file_info[fd].at_eof = 1;
	return 0;
    }

    /*
     * Open the file, if needed.
     */
    if ((file_fd = file_open(fd)) < 0) {
	return file_fd;
    }

    /*
     * Do a simple read.  Note that this does not emulate tape record
     * boundaries.
     */
    result = read(file_fd, buffer, count);
    if (result > 0) {
	file_info[fd].at_bof = 0;
    } else if (result == 0) {
	file_info[fd].at_eof = 1;
    }
    return result;
}

int
file_tapefd_write(fd, buffer, count)
    int fd, count;
    const void *buffer;
{
    int file_fd;
    int write_count = count;
    long length;
    long kbytes_left;
    int result;

    /*
     * Make sure we are online.
     */
    if ((result = check_online(fd)) != 0) {
	return result;
    }
    if (! file_info[fd].is_online) {
	errno = EIO;
	return -1;
    }

    /*
     * Check for write access first.
     */
    if ((file_info[fd].flags & 3) == O_RDONLY) {
	errno = EBADF;
	return -1;
    }

    /*
     * Special case: allow negative buffer size.
     */
    if (write_count <= 0) {
	return 0;				/* special case */
    }

    /*
     * If we are at EOM, it takes precedence over EOF.
     */
    if (file_info[fd].at_eom) {
	file_info[fd].at_eof = 0;
    }

#if 0 /*JJ*/
    /*
     * Writes are only allowed at BOF and EOM.
     */
    if (! (file_info[fd].at_bof || file_info[fd].at_eom)) {
	errno = EIO;
	return -1;
    }
#endif /*JJ*/

    /*
     * Writes are only allowed if we are not at EOF.
     */
    if (file_info[fd].at_eof) {
	errno = EIO;
	return -1;
    }

    /*
     * Open the file, if needed.
     */
    if ((file_fd = file_open(fd)) < 0) {
	return file_fd;
    }

    /*
     * Truncate the write if requested and return a simulated ENOSPC.
     */
    if ((length = tapefd_getinfo_length(fd)) > 0) {
	kbytes_left = length - file_info[fd].amount_written;
	if (write_count / 1024 > kbytes_left) {
	    write_count = kbytes_left * 1024;
	}
    }
    file_info[fd].amount_written += (write_count + 1023) / 1024;
    if (write_count <= 0) {
	file_release(fd);
	file_info[fd].at_bof = 0;
	file_info[fd].at_eom = 1;
	errno = ENOSPC;
	return -1;
    }

    /*
     * Do the write and truncate the file, if needed.  Checking for
     * last_operation_write is an optimization so we only truncate
     * once.
     */
    if (! file_info[fd].last_operation_write) {
	(void)ftruncate(file_fd, lseek(file_fd, 0, SEEK_CUR));
	file_release(fd);
	file_info[fd].at_bof = 0;
	file_info[fd].at_eom = 1;
    }
    result = write(file_fd, buffer, write_count);
    if (result >= 0) {
	file_info[fd].last_operation_write = 1;
    }

    return result;
}

int
file_tapefd_close(fd)
    int fd;
{
    int pos;
    int save_errno;
    char *line;
    int len;
    char number[NUM_STR_SIZE];
    int result;

    /*
     * If our last operation was a write, write a tapemark.
     */
    if (file_info[fd].last_operation_write) {
	if ((result = file_tapefd_weof(fd, 1)) != 0) {
	    return result;
	}
    }

    /*
     * If we are not at BOF, fsf to the next file unless we
     * are already at end of tape.
     */
    if (! file_info[fd].at_bof && ! file_info[fd].at_eom) {
	if ((result = file_tapefd_fsf(fd, 1)) != 0) {
	    return result;
	}
    }

    /*
     * Close the file if it is still open.
     */
    if (file_info[fd].fd >= 0) {
	close(file_info[fd].fd);
	file_info[fd].fd = -1;
    }

    /*
     * Release the info structure areas.
     */
    for (pos = 0; pos < file_info[fd].file_limit; pos++) {
	amfree(file_info[fd].filename[pos]);
    }
    amfree(file_info[fd].basename);

    /*
     * Update the status file if we were online.
     */
    if (file_info[fd].is_online) {
	if (lseek(fd, 0, SEEK_SET) != 0) {
	    save_errno = errno;
	    aclose(fd);
	    errno = save_errno;
	    return -1;
	}
	if (ftruncate(fd, 0) != 0) {
	    save_errno = errno;
	    aclose(fd);
	    errno = save_errno;
	    return -1;
	}
	ap_snprintf(number, sizeof(number),
		    "%d", file_info[fd].file_current);
	line = vstralloc("position ", number, "\n", NULL);
	len = strlen(line);
	result = write(fd, line, len);
	amfree(line);
	if (result != len) {
	    if (result >= 0) {
		errno = ENOSPC;
	    }
	    save_errno = errno;
	    aclose(fd);
	    errno = save_errno;
	    return -1;
	}
    }

    return close(fd);
}

void
file_tapefd_resetofs(fd)
    int fd;
{
}

int
file_tapefd_status(fd, stat)
    int fd;
    struct am_mt_status *stat;
{
    int result;

    /*
     * See if we are online.
     */
    if ((result = check_online(fd)) != 0) {
	return result;
    }
    memset((void *)stat, 0, sizeof(*stat));
    stat->online_valid = 1;
    stat->online = file_info[fd].is_online;
    return 0;
}

int
file_tape_stat(filename, buf)
     char *filename;
     struct stat *buf;
{
     return stat(filename, buf);
}

int
file_tape_access(filename, mode)
     char *filename;
     int mode;
{
     return access(filename, mode);
}

int
file_tapefd_rewind(fd)
    int fd;
{
    int result = 0;

    /*
     * Make sure we are online.
     */
    if ((result = check_online(fd)) != 0) {
	return result;
    }
    if (! file_info[fd].is_online) {
	errno = EIO;
	return -1;
    }

    /*
     * If our last operation was a write, write a tapemark.
     */
    if (file_info[fd].last_operation_write) {
	if ((result = file_tapefd_weof(fd, 1)) != 0) {
	    return result;
	}
    }

    /*
     * Close the file if it is still open.
     */
    if (file_info[fd].fd >= 0) {
	close(file_info[fd].fd);
	file_info[fd].fd = -1;
    }

    /*
     * Adjust the position and reset the flags.
     */
    file_info[fd].file_current = 0;

    file_info[fd].at_bof = 1;
    file_info[fd].at_eof = 0;
    file_info[fd].at_eom
      = (file_info[fd].file_current >= file_info[fd].file_count);
    file_info[fd].last_operation_write = 0;

    return result;
}

int
file_tapefd_unload(fd)
    int fd;
{
    int result;

    /*
     * Make sure we are online.
     */
    if ((result = check_online(fd)) != 0) {
	return result;
    }
    if (! file_info[fd].is_online) {
	errno = EIO;
	return -1;
    }

    file_tapefd_rewind(fd);
    return 0;
}

int
file_tapefd_fsf(fd, count)
    int fd, count;
{
    int result = 0;

    /*
     * Make sure we are online.
     */
    if ((result = check_online(fd)) != 0) {
	return result;
    }
    if (! file_info[fd].is_online) {
	errno = EIO;
	return -1;
    }

    /*
     * If our last operation was a write and we are going to move
     * backward, write a tapemark.
     */
    if (file_info[fd].last_operation_write && count < 0) {
	if ((result = file_tapefd_weof(fd, 1)) != 0) {
	    errno = EIO;
	    return -1;
	}
    }

    /*
     * Close the file if it is still open.
     */
    if (file_info[fd].fd >= 0) {
	close(file_info[fd].fd);
	file_info[fd].fd = -1;
    }

    /*
     * If we are at EOM and moving backward, adjust the count to go
     * one more file.
     */
    if (file_info[fd].at_eom && count < 0) {
	count--;
    }

    /*
     * Adjust the position and return an error if we go beyond either
     * end of the tape.
     */
    file_info[fd].file_current += count;

    if (file_info[fd].file_current > file_info[fd].file_count) {
        file_info[fd].file_current = file_info[fd].file_count;
	errno = EIO;
	result = -1;
    } else if (file_info[fd].file_current < 0) {
        file_info[fd].file_current = 0;
	errno = EIO;
	result = -1;
    }

    /*
     * Set BOF to true so we can write.  Set to EOF to false if the
     * fsf succeeded or if it failed but we were moving backward (and
     * thus we are at beginning of tape), otherwise set it to true so
     * a subsequent read will fail.  Set EOM to whatever is right.
     */
    file_info[fd].at_bof = 1;
    if (result == 0 || count < 0) {
	file_info[fd].at_eof = 0;
    } else {
	file_info[fd].at_eof = 1;
    }
    file_info[fd].at_eom
      = (file_info[fd].file_current >= file_info[fd].file_count);
    file_info[fd].last_operation_write = 0;

    return result;
}

int
file_tapefd_weof(fd, count)
    int fd, count;
{
    int file_fd;
    int result = 0;
    char *save_host;
    char *save_disk;
    int save_level;
    int save_errno;

    /*
     * Make sure we are online.
     */
    if ((result = check_online(fd)) != 0) {
	return result;
    }
    if (! file_info[fd].is_online) {
	errno = EIO;
	return -1;
    }

    /*
     * Check for write access first.
     */
    if ((file_info[fd].flags & 3) == O_RDONLY) {
	errno = EACCES;
	return -1;
    }

    /*
     * Special case: allow a zero count.
     */
    if (count == 0) {
	return 0;				/* special case */
    }

    /*
     * Disallow negative count.
     */
    if (count < 0) {
	errno = EINVAL;
	return -1;
    }

    /*
     * Close out the current file if open.
     */
    if ((file_fd = file_info[fd].fd) >= 0) {
	(void)ftruncate(file_fd, lseek(file_fd, 0, SEEK_CUR));
	close(file_info[fd].fd);
	file_info[fd].fd = -1;
	file_info[fd].file_current++;
	file_info[fd].at_bof = 1;
	file_info[fd].at_eof = 0;
	file_info[fd].at_eom = 1;
	file_info[fd].last_operation_write = 0;
	count--;
    }

    /*
     * Release any data files from current through the end.
     */
    file_release(fd);

    /*
     * Save any labelling information in case we clobber it.
     */
    if ((save_host = tapefd_getinfo_host(fd)) != NULL) {
	save_host = stralloc(save_host);
    }
    if ((save_disk = tapefd_getinfo_disk(fd)) != NULL) {
	save_disk = stralloc(save_disk);
    }
    save_level = tapefd_getinfo_level(fd);

    /*
     * Add more tapemarks.
     */
    while (--count >= 0) {
	if (file_open(fd) < 0) {
	    break;
	}
	close(file_info[fd].fd);
	file_info[fd].fd = -1;
	file_info[fd].file_current++;
	file_info[fd].file_count = file_info[fd].file_current;
	file_info[fd].at_bof = 1;
	file_info[fd].at_eof = 0;
	file_info[fd].at_eom = 1;
	file_info[fd].last_operation_write = 0;

	/*
	 * Only the first "file" terminated by an EOF gets the naming
	 * information from the caller.
	 */
	tapefd_setinfo_host(fd, NULL);
	tapefd_setinfo_disk(fd, NULL);
	tapefd_setinfo_level(fd, -1);
    }

    /*
     * Restore the labelling information.
     */
    save_errno = errno;
    tapefd_setinfo_host(fd, save_host);
    amfree(save_host);
    tapefd_setinfo_disk(fd, save_disk);
    amfree(save_disk);
    tapefd_setinfo_level(fd, save_level);
    errno = save_errno;

    return result;
}
