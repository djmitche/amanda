/***************************************************************************
*
* File:          $RCSfile: amrecover.h,v $
* Module:        
* Part of:       
*
* Revision:      $Revision: 1.2 $
* Last Edited:   $Date: 1997/04/17 09:17:00 $
* Author:        $Author: amcore $
*
* Description:   
* Public Func:   
* History:       $Log: amrecover.h,v $
* History:       Revision 1.2  1997/04/17 09:17:00  amcore
* History:       amrecover failed to restore from an uncompressed dump image
* History:       because I read the amrestore man page incorrectly. It now
* History:       handles uncompressed as well as compressed dump images.
* History:
* History:       Revision 1.1.1.1  1997/03/15 21:29:58  amcore
* History:       Mass import of 2.3.0.4 as-is.  We can remove generated files later.
* History:
* History:       Revision 1.14  1996/12/19 08:53:40  alan
* History:       first go at file extraction
* History:
* History:       Revision 1.13  1996/11/11 08:22:02  alan
* History:       added basic help command
* History:
* History:       Revision 1.12  1996/11/08 09:57:28  alan
* History:       added clear command
* History:
* History:       Revision 1.11  1996/10/02 18:36:33  alan
* History:       synchronization with Blair's changes
* History:
* History:       Revision 1.10  1996/09/09 10:27:51  alan
* History:       added pwd command
* History:
* History:       Revision 1.9  1996/06/06 10:01:06  alan
* History:       changed default server to kowhai
* History:
* History:       Revision 1.8  1996/05/23 10:09:50  alan
* History:       changed display_extract_list to take filename argument
* History:
* History:       Revision 1.7  1996/05/22 11:03:09  alan
* History:       added delete and show
* History:
* History:       Revision 1.6  1996/05/22 09:29:53  alan
* History:       added defaults for config and host
* History:
* History:       Revision 1.5  1996/05/17 10:51:06  alan
* History:       added protos
* History:
* History:       Revision 1.4  1996/05/16 11:00:21  alan
* History:       *** empty log message ***
* History:
* History:       Revision 1.3  1996/05/14 10:52:09  alan
* History:       ?
* History:
* History:       Revision 1.2  1996/05/13 09:21:13  alan
* History:       changes
* History:
* History:       Revision 1.1  1996/05/12 10:06:35  alan
* History:       Initial revision
* History:
*
***************************************************************************/

#include "amanda.h"

#define LINE_LENGTH 1024

typedef struct DIR_ITEM
{
    char date[11];
    int  level;
    char tape[256];
    char path[1024];

    struct DIR_ITEM *next;
}
DIR_ITEM;

extern char server_name[LINE_LENGTH];
extern char config[LINE_LENGTH];
extern char dump_hostname[MAX_HOSTNAME_LENGTH];	/* which machine we are restoring */
extern char disk_name[LINE_LENGTH];	/* disk we are restoring */
extern char mount_point[LINE_LENGTH];	/* where disk was mounted */
extern char disk_path[LINE_LENGTH];	/* path relative to mount point */
extern char dump_date[LINE_LENGTH];	/* date on which we are restoring */
extern int quit_prog;			/* set when time to exit parser */
extern char tape_server_name[LINE_LENGTH];
extern char tape_device_name[LINE_LENGTH];
extern pid_t extract_restore_child_pid;

extern int converse P((char *cmd));
extern int exchange P((char *cmd));
extern int server_happy P((void));
extern int send_command P((char *cmd));
extern int get_reply_line P((void));
extern char *reply_line P((void));

extern void quit P((void));

extern void help_list P((void));		/* list commands */

extern void set_disk P((char *dsk, char *mtpt));
extern void set_host P((char *host));
extern int set_date P((char *date));
extern void set_directory P((char *dir));
extern void show_directory P((void));

extern void list_disk_history P((void));
extern void list_directory P((void));
extern DIR_ITEM *get_dir_list P((void));
extern DIR_ITEM *get_next_dir_item P((DIR_ITEM *this));

extern void display_extract_list P((char *file));
extern void clear_extract_list P((void));
extern int is_extract_list_nonempty P((void));
extern void add_file P((char *path));
extern void delete_file P((char *path));

extern void extract_files P((void));
