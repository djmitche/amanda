/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998, 2000 University of Maryland at College Park
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
 * $Id: set_commands.c,v 1.11.2.3.2.1 2001/06/19 19:35:26 jrjackson Exp $
 *
 * implements the "set" commands in amrecover
 */

#include "amanda.h"
#include "amrecover.h"

#ifdef SAMBA_CLIENT
extern unsigned short samba_extract_method;
#endif /* SAMBA_CLIENT */

/* sets a date, mapping given date into standard form if needed */
int set_date(date)
char *date;
{
    char *cmd = NULL;

    clear_dir_list();

    cmd = stralloc2("DATE ", date);
    if (converse(cmd) == -1)
	exit(1);

    /* if a host/disk/directory is set, then check if that directory
       is still valid at the new date, and if not set directory to
       mount_point */
    if (disk_path != NULL) {
	cmd = newstralloc2(cmd, "OISD ", disk_path);
	if (exchange(cmd) == -1)
	    exit(1);
	if (server_happy())
	{
	    suck_dir_list_from_server();
	}
	else
	{
	    printf("No index records for cwd on new date\n");
	    printf("Setting cwd to mount point\n");
	    disk_path = newstralloc(disk_path, "/");	/* fake it */
	    clear_dir_list();
	}
    }
    amfree(cmd);

    return 0;
}


void set_host(host)
char *host;
{
    char *cmd = NULL;
    struct hostent *hp;
    char **hostp;
    int found_host = 0;

    if (is_extract_list_nonempty())
    {
	printf("Must clear extract list before changing host\n");
	return;
    }

    cmd = stralloc2("HOST ", host);
    if (converse(cmd) == -1)
	exit(1);
    if (server_happy())
    {
	found_host = 1;
    }
    else
    {
	/*
	 * Try converting the given host to a fully qualified name
	 * and then try each of the aliases.
	 */
	if ((hp = gethostbyname(host)) != NULL) {
	    host = hp->h_name;
	    printf("Trying host %s ...\n", host);
	    cmd = newstralloc2(cmd, "HOST ", host);
	    if (converse(cmd) == -1)
		exit(1);
	    if(server_happy())
	    {
		found_host = 1;
	    }
	    else
	    {
	        for (hostp = hp->h_aliases; (host = *hostp) != NULL; hostp++)
	        {
		    printf("Trying host %s ...\n", host);
		    cmd = newstralloc2(cmd, "HOST ", host);
		    if (converse(cmd) == -1)
		        exit(1);
		    if(server_happy())
		    {
		        found_host = 1;
		        break;
		    }
		}
	    }
	}
    }
    if(found_host)
    {
	dump_hostname = newstralloc(dump_hostname, host);
	amfree(disk_name);
	amfree(mount_point);
	amfree(disk_path);
	clear_dir_list();
    }
    amfree(cmd);
}


void set_disk(dsk, mtpt)
char *dsk;
char *mtpt;
{
    char *cmd = NULL;

    if (is_extract_list_nonempty())
    {
	printf("Must clear extract list before changing disk\n");
	return;
    }

    /* if mount point specified, check it is valid */
    if ((mtpt != NULL) && (*mtpt != '/'))
    {
	printf("Mount point \"%s\" invalid - must start with /\n", mtpt);
	return;
    }

    clear_dir_list();
    cmd = stralloc2("DISK ", dsk);
    if (converse(cmd) == -1)
	exit(1);
    amfree(cmd);

    if (!server_happy())
	return;

    disk_name = newstralloc(disk_name, dsk);
    if (mtpt == NULL)
    {
	/* mount point not specified */
	if (*dsk == '/')
	{
	    /* disk specified by mount point, hence use it */
	    mount_point = newstralloc(mount_point, dsk);
	}
	else
	{
	    /* device name given, use '/' because nothing better */
	    mount_point = newstralloc(mount_point, "/");
	}
    }
    else
    {
	/* mount point specified */
	mount_point = newstralloc(mount_point, mtpt);
    }

    /* set the working directory to the mount point */
    /* there is the possibility that there are no index records for the
       disk for the given date, hence setting the directory to the
       mount point will fail. Preempt this by checking first so we can write
       a more informative message. */
    if (exchange("OISD /") == -1)
	exit(1);
    if (server_happy())
    {
	disk_path = newstralloc(disk_path, "/");
	suck_dir_list_from_server();	/* get list of directory contents */
    }
    else
    {
	printf("No index records for disk for specified date\n");
	printf("If date correct, notify system administrator\n");
	disk_path = newstralloc(disk_path, "/");	/* fake it */
	clear_dir_list();
    }
}


void set_directory(dir)
char *dir;
{
    char *cmd = NULL;
    char *new_dir = NULL;
    char *dp, *de;
    char *ldir = NULL;

    /* do nothing if "." */
    if(strcmp(dir,".")==0) {
	show_directory();		/* say where we are */
	return;
    }

    if (disk_name == NULL) {
	printf("Must select disk before setting directory\n");
	return;
    }

    ldir = stralloc(dir);
    clean_pathname(ldir);

    /* convert directory into absolute path relative to disk mount point */
    if (ldir[0] == '/')
    {
	/* absolute path specified, must start with mount point */
	if (strcmp(mount_point, "/") == 0)
	{
	    new_dir = stralloc(ldir);
	}
	else
	{
	    if (strncmp(mount_point, ldir, strlen(mount_point)) != 0)
	    {
		printf("Invalid directory - Can't cd outside mount point \"%s\"\n",
		       mount_point);
		amfree(ldir);
		return;
	    }
	    new_dir = stralloc(ldir+strlen(mount_point));
	    if (strlen(new_dir) == 0) {
		new_dir = newstralloc(new_dir, "/");
					/* i.e. ldir == mount_point */
	    }
	}
    }
    else
    {
	new_dir = stralloc(disk_path);
	dp = ldir;
	/* strip any leading ..s */
	while (strncmp(dp, "../", 3) == 0)
	{
	    de = strrchr(new_dir, '/');	/* always at least 1 */
	    if (de == new_dir)
	    {
		/* at top of disk */
		*(de + 1) = '\0';
		dp = dp + 3;
	    }
	    else
	    {
		*de = '\0';
		dp = dp + 3;
	    }
	}
	if (strcmp(dp, "..") == 0) {
	    if (strcmp(new_dir, "/") == 0) {
		/* at top of disk */
		printf("Invalid directory - Can't cd outside mount point \"%s\"\n",
		       mount_point);
		amfree(ldir);
		return;
	    }
	    de = strrchr(new_dir, '/');	/* always at least 1 */
	    if (de == new_dir)
	    {
		/* at top of disk */
		*(de+1) = '\0';
	    }
	    else
	    {
		*de = '\0';
 	    }
	} else {
	    if (strcmp(new_dir, "/") != 0) {
		strappend(new_dir, "/");
	    }
	    strappend(new_dir, ldir);
	}
    }

    cmd = stralloc2("OISD ", new_dir);
    if (exchange(cmd) == -1)
	exit(1);
    amfree(cmd);
    if (server_happy())
    {
	disk_path = newstralloc(disk_path, new_dir);
	suck_dir_list_from_server();	/* get list of directory contents */
	show_directory();		/* say where we moved to */
    }
    else
    {
	printf("Invalid directory - %s\n", dir);
    }

    amfree(new_dir);
    amfree(ldir);
}


/* prints the current working directory */
void show_directory P((void))
{
    if (mount_point == NULL || disk_path == NULL)
        printf("Must select disk first\n");
    else if (strcmp(mount_point, "/") == 0)
	printf("%s\n", disk_path);
    else if (strcmp(disk_path, "/") == 0)
	printf("%s\n", mount_point);
    else
	printf("%s%s\n", mount_point, disk_path);
}


/* set the tape server and device */
void set_tape (tape)
    char *tape;
{
    char *tapedev = strchr(tape, ':');

    if (tapedev)
    {
	if (tapedev != tape) {
	    *tapedev = '\0';
	    tape_server_name = newstralloc(tape_server_name, tape);
	} else
	    amfree(tape_server_name);
	++tapedev;
    } else
	tapedev = tape;
    
    if (tapedev[0])
    {
	if (strcmp(tapedev, "default") == 0)
	    amfree(tape_device_name);
	else
	    tape_device_name = newstralloc(tape_device_name, tapedev);
    }

    if (tape_device_name)
	printf ("Using tape %s", tape_device_name);
    else
	printf ("Using default tape");

    if (tape_server_name)
	printf (" from server %s.\n", tape_server_name);
    else
	printf (".\nTape server unspecified, assumed to be %s.\n",
		server_name);
}

void set_mode (mode)
int mode;
{
#ifdef SAMBA_CLIENT
  if (mode == SAMBA_SMBCLIENT) {
    printf ("SAMBA dumps will be extracted using smbclient\n");
    samba_extract_method = SAMBA_SMBCLIENT;
  } else {
    if (mode == SAMBA_TAR) {
      printf ("SAMBA dumps will be extracted as TAR dumps\n");
      samba_extract_method = SAMBA_TAR;
    }
  }
#endif /* SAMBA_CLIENT */
}

void show_mode (void) 
{
#ifdef SAMBA_CLIENT
  printf ("SAMBA dumps are extracted ");

  if (samba_extract_method == SAMBA_TAR) {
    printf (" as TAR dumps\n");
  } else {
    printf ("using smbclient\n");
  }
#endif /* SAMBA_CLIENT */
}
