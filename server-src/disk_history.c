/***************************************************************************
*
* File:          $RCSfile: disk_history.c,v $
* Part of:       
*
* Revision:      $Revision: 1.1 $
* Last Edited:   $Date: 1997/03/15 21:30:10 $
* Author:        $Author: amcore $
*
* Notes:         
* Private Func:  
* History:       $Log: disk_history.c,v $
* History:       Revision 1.1  1997/03/15 21:30:10  amcore
* History:       Initial revision
* History:
* History:       Revision 1.1  1996/12/04 13:17:57  th
* History:       Add amindex
* History:
* History:       Revision 1.3  1996/10/01 18:24:19  alan
* History:       synchronization with Blair's changes
* History:
* History:       Revision 1.2  1996/06/17 10:10:21  alan
* History:       disk_hist wasn't being initialised
* History:       disk_hist wasn't being set to NULL after list cleared
* History:
* History:       Revision 1.1  1996/05/13 09:14:53  alan
* History:       Initial revision
* History:
*
***************************************************************************/

#include "amanda.h"
#include "disk_history.h"

static DUMP_ITEM *disk_hist = NULL;

void clear_list P((void))
{
    DUMP_ITEM *item, *this;

    item = disk_hist;
    while (item != NULL)
    {
	this = item;
	item = item->next;
	free(this);
    }
    disk_hist = NULL;
}

/* add item, maintain list ordered by oldest date last */
void add_dump(date, level, tape, file)
char *date;
int level;
char *tape;
int file;
{
    DUMP_ITEM *new, *item, *before;

    if ((new = (DUMP_ITEM *)malloc(sizeof(DUMP_ITEM))) == NULL)
	return;				/* naughty naughty */

    strcpy(new->date, date);
    new->level = level;
    strcpy(new->tape, tape);
    new->file = file;

    if (disk_hist == NULL)
    {
	disk_hist = new;
	new->next = NULL;
	return;
    }

    if (strcmp(disk_hist->date, new->date) < 0)
    {
	new->next = disk_hist;
	disk_hist = new;
	return;
    }
    
    before = disk_hist;
    item = disk_hist->next;
    while ((item != NULL) && (strcmp(item->date, new->date) >= 0))
    {
	before = item;
	item = item->next;
    }
    new->next = item;
    before->next = new;
}


DUMP_ITEM *first_dump P((void))
{
    return disk_hist;
}

DUMP_ITEM *next_dump(item)
DUMP_ITEM *item;
{
    return item->next;
}
