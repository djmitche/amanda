/***************************************************************************
*
* File:          $RCSfile: display_commands.c,v $
* Module:        
* Part of:       
*
* Revision:      $Revision: 1.1 $
* Last Edited:   $Date: 1997/03/15 21:29:58 $
* Author:        $Author: amcore $
*
* Notes:         
* Private Func:  
* History:       $Log: display_commands.c,v $
* History:       Revision 1.1  1997/03/15 21:29:58  amcore
* History:       Initial revision
* History:
* History:       Revision 1.7  1996/12/14 09:21:04  alan
* History:       removed point where list_directory() could sleep for ever waiting for
* History:       input that wasn't going to come.
* History:
* History:       Revision 1.6  1996/10/02 18:38:27  alan
* History:       synchronization with Blair's changes
* History:
* History:       Revision 1.5  1996/07/29 10:23:38  alan
* History:       due to problems on SunOS changed get_line() to strip off \r\n
* History:
* History:       Revision 1.4  1996/05/17 10:36:27  alan
* History:       made access to dir_list public
* History:
* History:       Revision 1.3  1996/05/16 10:59:46  alan
* History:       made display go through more
* History:
* History:       Revision 1.2  1996/05/13 09:23:29  alan
* History:       changes
* History:
* History:       Revision 1.1  1996/05/12 10:52:28  alan
* History:       Initial revision
* History:
*
***************************************************************************/

#include "amanda.h"
#include "amrecover.h"

static DIR_ITEM *dir_list = NULL;

DIR_ITEM *get_dir_list P((void))
{
    return dir_list;
}

DIR_ITEM *get_next_dir_item(this)
DIR_ITEM *this;
{
    return this->next;
}


static void clear_dir_list P((void))
{
    DIR_ITEM *this;

    if (dir_list == NULL)
	return;
    do
    {
	this = dir_list;
	dir_list = dir_list->next;
	free(this);
    }
    while (dir_list != NULL);
}

/* add item to list if path not already on list */
static int add_dir_list_item(date, level, tape, path)
char *date;
int level;
char *tape;
char *path;
{
    DIR_ITEM *last;

    dbprintf(("add_dir_list_item: Adding \"%s\" \"%d\" \"%s\" \"%s\"\n",
	      date, level, tape, path));

    dbprintf(("add_dir_list_item: dir_list=0x%08x\n", (unsigned int)dir_list));

    if (dir_list == NULL)
    {
	dbprintf(("add_dir_list_item: dir_list==NULL\n"));

	if ((dir_list = (DIR_ITEM *)malloc(sizeof(DIR_ITEM))) == NULL)
	    return -1;
	dir_list->next = NULL;
	strcpy(dir_list->date, date);
	dir_list->level = level;
	strcpy(dir_list->tape, tape);
	strcpy(dir_list->path, path);

	dbprintf(("add_dir_list_item: dir_list=0x%08x\n",
		  (unsigned int)dir_list));

	return 0;
    }
    
    dbprintf(("add_dir_list_item: dir_list!=NULL\n"));

    last = dir_list;
    while (last->next != NULL)
    {
	last = last->next;
    }
    
    dbprintf(("add_dir_list_item: last=0x%08x\n", (unsigned int)last));

    if ((last->next = (DIR_ITEM *)malloc(sizeof(DIR_ITEM))) == NULL)
	return -1;
    last->next->next = NULL;
    strcpy(last->next->date, date);
    last->next->level = level;
    strcpy(last->next->tape, tape);
    strcpy(last->next->path, path);

    dbprintf(("add_dir_list_item: last->next=0x%08x\n",
	      (unsigned int)(last->next)));

    return 0;
}


void list_disk_history P((void))
{
    char cmd[LINE_LENGTH];

    sprintf(cmd, "DHST");
    if (converse(cmd) == -1)
	exit(1);
}


void list_directory P((void))
{
    char cmd[LINE_LENGTH];
    int i;
    char *l;
    char date[11];
    int level;
    char tape[256];
    char dir[1024];
    DIR_ITEM *item;
    FILE *fp;

    if (strlen(disk_path) == 0) 
    {
	printf("Must select directory before listing\n");
	return;
    }
    
    clear_dir_list();

    sprintf(cmd, "OLSD %s", disk_path);
    if (send_command(cmd) == -1)
	exit(1);
    /* skip preamble */
    if ((i = get_reply_line()) == -1)
	exit(1);
    if (i == 0)				/* assume something wrong! */
    {
	l = reply_line();
	printf("%s\n", l);
	return;
    }
    /* miss last line too */
    while ((i = get_reply_line()) != 0)
    {
	if (i == -1)
	    exit(1);
	l = reply_line();
	if (!server_happy())
	{
	    printf("%s\n", l);
	    clear_dir_list();
	    return;
	}
	sscanf(l+5, "%s %d %s %s", date, &level, tape, dir);
	add_dir_list_item(date, level, tape, dir);
    }
    
    if ((fp = popen("more", "w")) == NULL)
    {
	printf("Warning - can't pipe through more\n");
	fp = stdout;
    }
    i = strlen(disk_path);
    if (i != 1)
	i++;				/* so disk_path != "/" */
    for (item = get_dir_list(); item != NULL; item=get_next_dir_item(item))
	fprintf(fp, "%s %s\n", item->date, item->path+i);
    pclose(fp);
}
