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
 * getfsent - generic version of code to read fstab
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

    if(!sys_fsent)
	return 0;
    fsent->fsname  = sys_fsent->fs_spec;
    fsent->mntdir  = sys_fsent->fs_file;
    fsent->freq    = sys_fsent->fs_freq;
    fsent->passno  = sys_fsent->fs_passno;
#ifdef STATFS_ULTRIX
    fsent->fstype  = sys_fsent->fs_name;
    fsent->mntopts = sys_fsent->fs_opts;
#elif defined(_AIX)
    fsent->fstype  = "unknown";
    fsent->mntopts = sys_fsent->fs_type;
#else
    fsent->fstype  = sys_fsent->fs_vfstype;
    fsent->mntopts = sys_fsent->fs_mntops;
#endif
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
	fclose(fstabf);
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
	fclose(fstabf);
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
#else /* } { */

#define GETFSENT_TYPE "undefined"

#endif /* } */

int search_fstab(fsname, mntdir, fsent)
char *fsname, *mntdir;
generic_fsent_t *fsent;
{
    int fsname_ok, mntdir_ok;

    while(get_fstab_nextentry(fsent)) {
	fsname_ok = fsname == NULL || fsent->fsname == NULL ||
	            !strcmp(fsname, fsent->fsname);
	mntdir_ok = mntdir == NULL || fsent->mntdir == NULL ||
	            !strcmp(mntdir, fsent->mntdir);
	if(mntdir_ok && fsname_ok)
	    return 1;
    }
    return 0;
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
	   strcmp(fsent->fstype, "piofs") != 0;	/* an AIX printer thing? */
}


char *amname_to_devname(str)
char *str;
{
    generic_fsent_t fsent;
    static char devname[1024];

    if(str[0] != '/') {
	sprintf(devname, "%s%s", RDEV_PREFIX, str);
    }
    else {
	if(!open_fstab())
	    return str;
	if(!search_fstab(NULL, str, &fsent) || fsent.fsname == NULL) {
	    close_fstab();
	    return str;
	}

	/* convert block to raw */
	if(!strncmp(fsent.fsname, DEV_PREFIX, strlen(DEV_PREFIX)))
	    sprintf(devname, "%s%s", RDEV_PREFIX, 
		    fsent.fsname + strlen(DEV_PREFIX));
	else
	    strcpy(devname, fsent.fsname);
	close_fstab();

    }

    return devname;
}

char *amname_to_dirname(str)
char *str;
{
    generic_fsent_t fsent;
    char devname[1024];
    static char dirname[1024];

    if(str[0] == '/') {
	strcpy(dirname, str);
    }
    else {
	sprintf(devname, "%s%s", DEV_PREFIX, str);

	if(!open_fstab())
	    return str;
	if(!search_fstab(devname, NULL, &fsent) || fsent.mntdir == NULL) {
	    close_fstab();  
	    return str;
	}

	strcpy(dirname, fsent.mntdir);
	close_fstab();

    }
    return dirname;
}

char *amname_to_fstype(str)
char *str;
{
    generic_fsent_t fsent;
    static char fstype[1024];

    if(!open_fstab())
       return "";
    if(!search_fstab(str, NULL, &fsent) || fsent.mntdir == NULL) {
       close_fstab();  
       if(!open_fstab())
          return "";
       if(!search_fstab(NULL, str, &fsent) || fsent.mntdir == NULL) {
          close_fstab();  
          return "";
       }
    }

    strcpy(fstype, fsent.fstype);
    close_fstab();

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

int main()
{
    generic_fsent_t fsent;

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

    if(!open_fstab()) {
	fprintf(stderr, "getfsent_test: could not open fstab\n");
	return 1;
    }

    if(search_fstab(NULL, "/usr", &fsent)) {
	printf("Found %s mount for /usr:\n",
	       is_local_fstype(&fsent)? "local" : "remote");
	print_entry(&fsent);
    }
    else 
	printf("Mount for /usr not found\n");

    close_fstab();

    printf("fstype of `/': %s\n", amname_to_fstype("/"));
    printf("fstype of `/dev/root': %s\n", amname_to_fstype("/dev/root"));
    printf("fstype of `/usr': %s\n", amname_to_fstype("/usr"));

    return 0;
}

#endif
