/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1996 University of Maryland at College Park
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
 * $Id: display_commands.c,v 1.2 1997/08/27 08:12:31 amcore Exp $
 *
 * implements the directory-display related commands in amrecover
 */

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


void clear_dir_list P((void))
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

    if (dir_list == NULL)
    {
	if ((dir_list = (DIR_ITEM *)malloc(sizeof(DIR_ITEM))) == NULL)
	    return -1;
	dir_list->next = NULL;
	strcpy(dir_list->date, date);
	dir_list->level = level;
	strcpy(dir_list->tape, tape);
	strcpy(dir_list->path, path);

	return 0;
    }
    
    last = dir_list;
    while (last->next != NULL)
    {
	last = last->next;
    }
    
    if ((last->next = (DIR_ITEM *)malloc(sizeof(DIR_ITEM))) == NULL)
	return -1;
    last->next->next = NULL;
    strcpy(last->next->date, date);
    last->next->level = level;
    strcpy(last->next->tape, tape);
    strcpy(last->next->path, path);

    return 0;
}


void list_disk_history P((void))
{
    char cmd[LINE_LENGTH];

    sprintf(cmd, "DHST");
    if (converse(cmd) == -1)
	exit(1);
}


void suck_dir_list_from_server P((void))
{
    char cmd[LINE_LENGTH];
    int i;
    char *l;
    char date[11];
    int level;
    char tape[256];
    char dir[1024];

    if (strlen(disk_path) == 0) 
    {
	printf("Directory must be set before getting listing\n");
	printf("This is a coding error. Please report\n");
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
}



void list_directory P((void))
{
    int i;
    DIR_ITEM *item;
    FILE *fp;

    if (strlen(disk_path) == 0) 
    {
	printf("Directory should have been set already but wasn't\n");
	printf("This is a coding error. Please report\n");
	return;
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
