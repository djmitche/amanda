/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1995 University of Maryland at College Park
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
 * $Id: getfsent.c,v 1.11.2.3 1998/02/26 11:24:47 amcore Exp $
 *
 * generic version of code to read fstab
 */

#include "amanda.h"

#ifdef TEST
#  include <stdio.h>
#  include <sys/types.h>
#  undef P
#  define P(x)	x
#endif

#include "getfsent.h"

/*
 * You are in a twisty maze of passages, all alike.
 * Geesh.
 */

#if defined(HAVE_FSTAB_H) && !defined(HAVE_MNTENT_H) /* { */
/*
** BSD (GETFSENT_BSD)
*/
#define GETFSENT_TYPE "BSD (Ultrix, AIX)"

#include <fstab.h>

int open_fstab()
{
    return setfsent();
}

void close_fstab()
{
    endfsent();
}


int get_fstab_nextentry(fsent)
generic_fsent_t *fsent;
{
    struct fstab *sys_fsent = getfsent();
    static char *xfsname = NULL, *xmntdir = NULL;
    static char *xfstype = NULL, *xmntopts = NULL;

    if(!sys_fsent)
	return 0;
    fsent->fsname  = xfsname  = newstralloc(xfsname,  sys_fsent->fs_spec);
    fsent->mntdir  = xmntdir  = newstralloc(xmntdir,  sys_fsent->fs_file);
    fsent->freq    = sys_fsent->fs_freq;
    fsent->passno  = sys_fsent->fs_passno;
#ifdef STATFS_ULTRIX
    fsent->fstype  = xfstype  = newstralloc(xfstype,  sys_fsent->fs_name);
    fsent->mntopts = xmntopts = newstralloc(xmntopts, sys_fsent->fs_opts);
#elif defined(_AIX)
    fsent->fstype  = xfstype  = newstralloc(xfstype,  "unknown");
    fsent->mntopts = xmntopts = newstralloc(xmntopts, sys_fsent->fs_type);
#else
    fsent->fstype  = xfstype  = newstralloc(xfstype,  sys_fsent->fs_vfstype);
    fsent->mntopts = xmntopts = newstralloc(xmntopts, sys_fsent->fs_mntops);
#endif
    return 1;
}

#elif defined(HAVE_SYS_VFSTAB_H) /* } { */
/*
** SVR4 (GETFSENT_SOLARIS)
*/
#define GETFSENT_TYPE "SVR4 (Solaris)"

#include <sys/vfstab.h>

static FILE *fstabf = NULL;

int open_fstab()
{
    close_fstab();
    return (fstabf = fopen(VFSTAB, "r")) != NULL;
}

void close_fstab()
{
    if(fstabf)
	afclose(fstabf);
    fstabf = NULL;
}

int get_fstab_nextentry(fsent)
generic_fsent_t *fsent;
{
    struct vfstab sys_fsent;

    if(getvfsent(fstabf, &sys_fsent) != 0)
	return 0;

    fsent->fsname  = sys_fsent.vfs_special;
    fsent->fstype  = sys_fsent.vfs_fstype;
    fsent->mntdir  = sys_fsent.vfs_mountp;
    fsent->mntopts = sys_fsent.vfs_mntopts;
    fsent->freq    = 1;	/* N/A */
    fsent->passno  = sys_fsent.vfs_fsckpass? atoi(sys_fsent.vfs_fsckpass) : 0;
    return 1;
}

#elif defined(HAVE_MNTENT_H) /* } { */

/*
** System V.3 (GETFSENT_SVR3, GETFSENT_LINUX)
*/
#define GETFSENT_TYPE "SVR3 (NeXTstep, Irix, Linux, HP-UX)"

#include <mntent.h>

static FILE *fstabf = NULL;

int open_fstab()
{
    close_fstab();
    return (fstabf = fopen(MNTTAB, "r")) != NULL;
}

void close_fstab()
{
    if(fstabf)
	afclose(fstabf);
    fstabf = NULL;
}

int get_fstab_nextentry(fsent)
generic_fsent_t *fsent;
{
    struct mntent *sys_fsent = getmntent(fstabf);

    if(!sys_fsent)
	return 0;
    fsent->fsname  = sys_fsent->mnt_fsname;
    fsent->fstype  = sys_fsent->mnt_type;
    fsent->mntdir  = sys_fsent->mnt_dir;
    fsent->mntopts = sys_fsent->mnt_opts;
    fsent->freq    = sys_fsent->mnt_freq;
    fsent->passno  = sys_fsent->mnt_passno;
    return 1;
}

#elif defined(HAVE_SYS_MNTTAB_H) /* } { */

/* we won't actually include mnttab.h, since it contains nothing useful.. */

#define GETFSENT_TYPE "SVR3 (Interactive UNIX)"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define FSTAB "/etc/fstab"

static FILE *fstabf = NULL;

int open_fstab()
{
    close_fstab();
    return (fstabf = fopen(FSTAB, "r")) != NULL;
}

void close_fstab()
{
    if(fstabf)
	afclose(fstabf);
    fstabf = NULL;
}

static generic_fsent_t _fsent;

int get_fstab_nextentry(fsent)
generic_fsent_t *fsent;
{
    static char *lfsnam = NULL;
    static char *opts = NULL;
    static char *cp = NULL;
    char *s;
    int ch;

    afree(cp);
    for (; (cp = agets(fstabf) != NULL; free(cp)) {
	fsent->fsname = strtok(cp, " \t");
	if ( fsent->fsname && *fsent->fsname != '#' )
	    break;
    }
    if (cp == NULL) return 0;

    fsent->mntdir = strtok((char *)NULL, " \t");
    fsent->mntopts = strtok((char *)NULL, " \t");
    if ( *fsent->mntopts != '-' )  {
	fsent->fstype = fsent->mntopts;
	fsent->mntopts = "rw";
    } else {
	fsent->fstype = "";
	if (strcmp(fsent->mntopts, "-r") == 0) {
	    fsent->mntopts = "ro";
	}
    }
    if ((s = strchr(fsent->fstype, ',')) != NULL) {
	*s++ = '\0';
	strappend(fsent->mntopts, ",");
	strappend(fsent->mntopts, s);
    }

    lfsnam = newstralloc(lfsnam, fsent->fstype);
    s = lfsnam;
    while((ch = *s++) != '\0') {
	if(isupper(ch)) ch = tolower(ch);
	s[-1] = ch;
    }
    fsent->fstype = lfsnam;

#define sc "hs"
    if (strncmp(fsent->fstype, sc, sizeof(sc)-1) == 0)
	fsent->fstype = "iso9660";
#undef sc

    fsent->freq = 0;
    fsent->passno = 0;

    return 1;
}

/* PAG97 - begin */

#elif defined(HAVE_MNTTAB_H) /* } { */

#define GETFSENT_TYPE "SVR3 (SCO UNIX)"

#include <mnttab.h>
#include <sys/fstyp.h>
#include <sys/statfs.h>

#define MNTTAB "/etc/mnttab"

/*
 * If these are defined somewhere please let me know.
 */

#define MNT_READONLY 0101
#define MNT_READWRITE 0100

static FILE *fstabf = NULL;

int open_fstab()
{
    close_fstab();
    return (fstabf = fopen(MNTTAB, "r")) != NULL;
}

void close_fstab()
{
    if(fstabf)
	afclose(fstabf);
    fstabf = NULL;
}

static generic_fsent_t _fsent;

int get_fstab_nextentry(fsent)
generic_fsent_t *fsent;
{
    struct statfs fsd;
    char typebuf[FSTYPSZ];
    struct mnttab mnt;
    char *dp, *ep;

    if(!fread (&mnt, sizeof mnt, 1, fstabf))
      return 0;

    fsent->fsname  = mnt.mt_dev;
    fsent->mntdir  = mnt.mt_filsys;
    fsent->fstype = "";

    if (statfs (fsent->mntdir, &fsd, sizeof fsd, 0) != -1
        && sysfs (GETFSTYP, fsd.f_fstyp, typebuf) != -1) {
       dp = typebuf;
       ep = fsent->fstype;
       while (*dp)
            *ep++ = tolower(*dp++);
    }

    if ( mnt.mt_ro_flg == MNT_READONLY ) {
	fsent->mntopts = "ro";
    } else {
	fsent->mntopts = "rw";
    }

    fsent->freq = 0;
    fsent->passno = 0;
    return 1;
}

/* PAG97 - end */
#else /* } { */

#define GETFSENT_TYPE "undefined"

#endif /* } */

/*
 *=====================================================================
 * Convert either a block or character device name to a character (raw)
 * device name.
 *
 * char *dev2rdev(char *name);
 *
 * entry:	name - device name to convert
 * exit:	matching character device name if found,
 *		otherwise returns the input
 *
 * The input must be an absolute path.
 *
 * The exit string area is always an alloc-d area that the caller is
 * responsible for releasing.
 *=====================================================================
 */

static char *dev2rdev(name)
char *name;
{
  char *fname = NULL;
  struct stat st;
  char *s;
  int ch;

  if(stat(name, &st) == 0 && S_ISCHR(st.st_mode)) {
    /*
     * If the input is already a character device, just return it.
     */
    return stralloc(name);
  }

  s = name;
  ch = *s++;

  if(ch == '\0' || ch != '/') return stralloc(name);

  ch = *s++;					/* start after first '/' */
  /*
   * Break the input path at each '/' and create a new name with an
   * 'r' before the right part.  For instance:
   *
   *   /dev/sd0a -> /dev/rsd0a
   *   /dev/dsk/c0t0d0s0 -> /dev/rdsk/c0t0d0s0 -> /dev/dsk/rc0t0d0s0
   */
  while(ch) {
    if (ch == '/') {
      s[-1] = '\0';
      fname = newvstralloc(fname, name, "/r", s, NULL);
      s[-1] = ch;
      if(stat(fname, &st) == 0 && S_ISCHR(st.st_mode)) return fname;
    }
    ch = *s++;
  }
  return stralloc(name);			/* no match */
}

static int samefile(stats, estat)
struct stat stats[3], *estat;
{
  int i;
  for(i = 0; i < 3; ++i) {
    if (stats[i].st_dev == estat->st_dev &&
	stats[i].st_ino == estat->st_ino)
      return 1;
  }
  return 0;
}

int search_fstab(name, fsent)
char *name;
generic_fsent_t *fsent;
{
  struct stat stats[3];
  char *fullname = NULL;
  char *rdev = NULL;
  int rc;

  if (!name)
    return 0;

  stats[0].st_dev = stats[1].st_dev = stats[2].st_dev = -1;

  if (stat(name, &stats[0]) == -1)
    stats[0].st_dev = -1;
  if (name[0] != '/') {
    fullname = stralloc2(DEV_PREFIX, name);
    if (stat(fullname, &stats[1]) == -1)
      stats[1].st_dev = -1;
    fullname = newstralloc2(fullname, RDEV_PREFIX, name);
    if (stat(fullname, &stats[2]) == -1)
      stats[2].st_dev = -1;
    afree(fullname);
  }
  else if (stat((rdev = dev2rdev(name)), &stats[1]) == -1)
    stats[1].st_dev = -1;

  afree(rdev);

  if (!open_fstab())
    return 0;

  rc = 0;
  while(get_fstab_nextentry(fsent)) {
    struct stat estat;
    if ((fsent->mntdir != NULL
	 && strcmp(fsent->mntdir, name) == 0) ||
	(fsent->fsname != NULL
	 && strcmp(fsent->fsname, name) == 0) ||
	(fsent->mntdir != NULL
	 && stat(fsent->mntdir, &estat) != -1
	 && samefile(stats, &estat)) ||
	(fsent->fsname != NULL
	 && stat(fsent->fsname, &estat) != -1
	 && samefile(stats, &estat)) ||
	(fsent->fsname != NULL
	 && stat((rdev = dev2rdev(fsent->fsname)), &estat) != -1
	 && samefile(stats, &estat))) {
      rc = 1;
      break;
    }
  }
  afree(rdev);
  close_fstab();
  return rc;
}

int is_local_fstype(fsent)
generic_fsent_t *fsent;
{
    if(fsent->fstype == NULL)	/* unknown, assume local */
	return 1;

    /* just eliminate fstypes known to be remote or unsavable */

    return strcmp(fsent->fstype, "nfs") != 0 && /* NFS */
	   strcmp(fsent->fstype, "afs") != 0 &&	/* Andrew Filesystem */
	   strcmp(fsent->fstype, "swap") != 0 && /* Swap */
	   strcmp(fsent->fstype, "iso9660") != 0 && /* CDROM */
	   strcmp(fsent->fstype, "hs") != 0 && /* CDROM */
	   strcmp(fsent->fstype, "piofs") != 0;	/* an AIX printer thing? */
}


char *amname_to_devname(str)
char *str;
{
    generic_fsent_t fsent;

    if(search_fstab(str, &fsent))
      if (fsent.fsname != NULL)
	str = fsent.fsname;

    return dev2rdev(str);
}

char *amname_to_dirname(str)
char *str;
{
    generic_fsent_t fsent;
    static char *dirname = NULL;

    if(search_fstab(str, &fsent))
      if (fsent.mntdir != NULL)
	str = fsent.mntdir;

    dirname = newstralloc(dirname, str);
    return dirname;
}

char *amname_to_fstype(str)
char *str;
{
    generic_fsent_t fsent;
    static char *fstype = NULL;

    if (!search_fstab(str, &fsent))
      return "";

    fstype = newstralloc(fstype, fsent.fstype);
    return fstype;
}

#ifdef TEST

print_entry(fsent)
generic_fsent_t *fsent;
{
#define nchk(s)	((s)? (s) : "<NULL>")
    printf("%-20.20s %-14.14s %-7.7s %4d %5d %s\n",
	   nchk(fsent->fsname), nchk(fsent->mntdir), nchk(fsent->fstype),
	   fsent->freq, fsent->passno, nchk(fsent->mntopts));
}

char *pname = "getfsent";

int main()
{
    generic_fsent_t fsent;
    int fd;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    if(!open_fstab()) {
	fprintf(stderr, "getfsent_test: could not open fstab\n");
	return 1;
    }

    printf("getfsent (%s)\n",GETFSENT_TYPE);
    printf("l/r fsname               mntdir         fstype  freq pass# mntopts\n");
    while(get_fstab_nextentry(&fsent)) {
	printf("%c  ",is_local_fstype(&fsent)? 'l' : 'r');
	print_entry(&fsent);
    }
    printf("--------\n");

    close_fstab();

    if(search_fstab("/usr", &fsent)) {
	printf("Found %s mount for /usr:\n",
	       is_local_fstype(&fsent)? "local" : "remote");
	print_entry(&fsent);
    }
    else 
	printf("Mount for /usr not found\n");

    printf("fstype of `/': %s\n", amname_to_fstype("/"));
    printf("fstype of `/dev/root': %s\n", amname_to_fstype("/dev/root"));
    printf("fstype of `/usr': %s\n", amname_to_fstype("/usr"));
    printf("fstype of `c0t3d0s0': %s\n", amname_to_fstype("c0t3d0s0"));

    printf("device of `/tmp/foo': %s\n", amname_to_devname("/tmp/foo"));
    printf("dirname of `/tmp/foo': %s\n", amname_to_dirname("/tmp/foo"));
    printf("fstype of `/tmp/foo': %s\n", amname_to_fstype("/tmp/foo"));
    printf("device of `./foo': %s\n", amname_to_devname("./foo"));
    printf("dirname of `./foo': %s\n", amname_to_dirname("./foo"));
    printf("fstype of `./foo': %s\n", amname_to_fstype("./foo"));

    return 0;
}

#endif
