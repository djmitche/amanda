/***************************************************************************
*
* File:          $RCSfile: set_commands.c,v $
* Module:        
* Part of:       
*
* Revision:      $Revision: 1.1 $
* Last Edited:   $Date: 1997/03/15 21:29:58 $
* Author:        $Author: amcore $
*
* Notes:         
* Private Func:  
* History:       $Log: set_commands.c,v $
* History:       Revision 1.1  1997/03/15 21:29:58  amcore
* History:       Initial revision
* History:
* History:       Revision 1.10  1996/11/08 09:57:03  alan
* History:       set_disk and set_host now check for non-zero extract list
* History:
* History:       Revision 1.9  1996/11/08 08:57:05  alan
* History:       added code to set_disk() so disk_path set to mount point
* History:
* History:       Revision 1.8  1996/10/29 08:30:57  alan
* History:       Pete Geenhuizen inspired changes to support logical disk names etc
* History:
* History:       Revision 1.7  1996/10/24 08:17:41  alan
* History:       If user entered "cd /mount/point" it didn't work - does now
* History:
* History:       Revision 1.6  1996/10/23 09:13:20  alan
* History:       removed Blair's mapping of disk names to server
* History:
* History:       Revision 1.5  1996/10/02 18:48:53  alan
* History:       synchronization with Blair's changes
* History:
* History:       Revision 1.4  1996/09/09 10:27:32  alan
* History:       added pwd command
* History:
* History:       Revision 1.3  1996/05/20 10:23:01  alan
* History:       *** empty log message ***
* History:
* History:       Revision 1.2  1996/05/13 09:21:40  alan
* History:       changes
* History:
* History:       Revision 1.1  1996/05/12 10:06:23  alan
* History:       Initial revision
* History:
*
***************************************************************************/

#include "amanda.h"
#include "amrecover.h"


/* sets a date, mapping given date into standard form if needed */
int set_date(date)
char *date;
{
    char cmd[LINE_LENGTH];

    sprintf(cmd, "DATE %s", date);
    if (converse(cmd) == -1)
	exit(1);
    return 0;
}


void set_host(host)
char *host;
{
    char cmd[LINE_LENGTH];

    if (is_extract_list_nonempty())
    {
	printf("Must clear extract list before changing host\n");
	return;
    }

    sprintf(cmd, "HOST %s", host);
    if (converse(cmd) == -1)
	exit(1);
    if (server_happy())
    {
	strcpy(dump_hostname, host);
	strcpy(disk_name, "");
	strcpy(mount_point, "");
	strcpy(disk_path, "");
    }
}
    

void set_disk(dsk, mtpt)
char *dsk;
char *mtpt;
{
    char cmd[LINE_LENGTH];

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
    
    sprintf(cmd, "DISK %s", dsk);
    if (converse(cmd) == -1)
	exit(1);

    if (!server_happy())
	return;

    strcpy(disk_name, dsk);
    if (mtpt == NULL)
    {
	/* mount point not specified */
	if (*dsk == '/')
	{
	    /* disk specified by mount point, hence use it */
	    strcpy(mount_point, dsk);
	}
	else
	{
	    /* device name given, use '/' because nothing better */
	    strcpy(mount_point, "/");
	}
    }
    else
    {
	/* mount point specified */
	strcpy(mount_point, mtpt);
    }

    /* set the working directory to the mount point */
    strcpy(disk_path, "/");
}


void set_directory(dir)
char *dir;
{
    char cmd[LINE_LENGTH];
    char new_dir[LINE_LENGTH];
    char *dp, *de;

    /* convert directory into absolute path relative to disk mount point */
    if (dir[0] == '/')
    {
	/* absolute path specified, must start with mount point */
	if (strcmp(mount_point, "/") == 0)
	{
	    strcpy(new_dir, dir);
	}
	else
	{
	    if (strncmp(mount_point, dir, strlen(mount_point)) != 0)
	    {
		printf("Invalid directory - Can't cd outside mount point \"%s\"\n",
		       mount_point);
		return;
	    }
	    strcpy(new_dir, dir+strlen(mount_point));
	    if (strlen(new_dir) == 0)
		strcpy(new_dir, "/");	/* ie dir == mount_point */
	}
    }
    else
    {
	strcpy(new_dir, disk_path);
	dp = dir;
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
	if (strcmp(dp, "..") == 0)
	{
	    if (strcmp(new_dir, "/") == 0)
	    {
		/* at top of disk */
		printf("Invalid directory - Can't cd outside mount point \"%s\"\n",
		       mount_point);
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
	}
	else
	{
	    if (strcmp(new_dir, "/") != 0)
		strcat(new_dir, "/");
	    strcat(new_dir, dir);
	}
    }

    sprintf(cmd, "OISD %s", new_dir);
    if (exchange(cmd) == -1)
	exit(1);
    if (server_happy())
    {
	strcpy(disk_path, new_dir);
	show_directory();
    }
    else
    {
	printf("Invalid directory - %s\n", dir);
    }
}
    

/* prints the current working directory */
void show_directory P((void))
{
    if (strcmp(mount_point, "/") == 0)
	printf("%s\n", disk_path);
    else if (strcmp(disk_path, "/") == 0)
	printf("%s\n", mount_point);
    else
	printf("%s%s\n", mount_point, disk_path);
}
