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
/* $Id: list_dir.c,v 1.5 1997/08/27 08:13:22 amcore Exp $
 *
 * obtains directory listings from index files
 */

#include "amanda.h"
#include "amindexd.h"
#include "amindex.h"

typedef struct DIR_ITEM
{
    DUMP_ITEM *dump;
    char path[1024];

    struct DIR_ITEM *next;
}
DIR_ITEM;


static DIR_ITEM *dir_list = NULL;

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
static int add_dir_list_item(dump, path)
DUMP_ITEM *dump;
char *path;
{
    DIR_ITEM *last;

    if (dir_list == NULL)
    {
	if ((dir_list = (DIR_ITEM *)malloc(sizeof(DIR_ITEM))) == NULL)
	    return -1;
	dir_list->next = NULL;
	dir_list->dump = dump;
	strcpy(dir_list->path, path);
	return 0;
    }

    if (strcmp(path, dir_list->path) == 0)
	return 0;

    last = dir_list;
    while (last->next != NULL)
    {
	last = last->next;
	if (strcmp(path, last->path) == 0)
	    return 0;
    }

    if ((last->next = (DIR_ITEM *)malloc(sizeof(DIR_ITEM))) == NULL)
	return -1;
    last->next->next = NULL;
    last->next->dump = dump;
    strcpy(last->next->path, path);
    return 0;
}


/* find all matching entries in a dump listing */
/* return -1 if error - and writes error message */
static int process_ls_dump(dir, dump_item)
char *dir;
DUMP_ITEM *dump_item;
{
    char cmd[1024];
    char dir_slash[1024];
    int no_fields;
    FILE *fp;
    char *c;

    /* count 2 plus the number of /'s is path to get no fields */
    if (strcmp(dir, "/") == 0)
    {
	strcpy(dir_slash, dir);
	no_fields = 2;
    }
    else
    {
	sprintf(dir_slash, "%s/", dir);
	for (no_fields = 2, c = dir; *c != '\0'; c++)
	    if (*c == '/')
		no_fields++;
    }

    sprintf(cmd, "%s %s '%s' 2>/dev/null | grep \"^%s\" | cut -d/ -f1-%d | sort | uniq",
	    UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
	    UNCOMPRESS_OPT,
#else
	    "",
#endif
	    getindexfname(dump_hostname, disk_name, dump_item->date,
			  dump_item->level),
	    dir_slash, no_fields);
    dbprintf(("c %s\n", cmd));
    if ((fp = popen(cmd, "r")) == NULL)
    {
	reply(599, "System error %d", errno);
	return -1;
    }
    while (fgets(cmd, LONG_LINE, fp) != NULL)
    {
	/* sometimes cut returns a blank line if no input! */
	if (strlen(cmd) > 1)
	{
	    cmd[strlen(cmd)-1] = '\0';	/* overwrite '\n' */
	    add_dir_list_item(dump_item, cmd);
	}
    }
    pclose(fp);

    return 0;
}


int opaque_ls(dir)
char *dir;
{
    DUMP_ITEM *dump_item;
    DIR_ITEM *dir_item;
    int last_level;

    clear_dir_list();

    if (strlen(disk_name) == 0)
    {
	reply(502, "Must set config,host,disk before listing a directory");
	return -1;
    }
    if (strlen(date) == 0)
    {
	reply(502, "Must set date before listing a directory");
	return -1;
    }

    /* scan through till we find first dump on or before date */
    for (dump_item=first_dump(); dump_item!=NULL; dump_item=next_dump(dump_item))
	if (strcmp(dump_item->date, date) <= 0)
	    break;
    if (dump_item == NULL)
    {
	/* no dump for given date */
	reply(500, "No dumps available on or before date \"%s\"", date);
	return -1;
    }

    /* get data from that dump */
    if (process_ls_dump(dir, dump_item) == -1)
	return -1;

    /* go back processing higher level dumps till we hit a level 0 dump */
    last_level = dump_item->level;
    while ((last_level != 0) && ((dump_item=next_dump(dump_item)) != NULL))
    {
	if (dump_item->level < last_level)
	{
	    last_level = dump_item->level;
	    if (process_ls_dump(dir, dump_item) == -1)
		return -1;
	}
    }

    /* return the information to the caller */
    lreply(200, " Opaque list of %s", dir);
    for (dir_item = dir_list; dir_item != NULL; dir_item = dir_item->next)
	lreply(201, " %s %d %-16s %s",
	       dir_item->dump->date, dir_item->dump->level,
	       dir_item->dump->tape, dir_item->path);
    reply(200, " Opaque list of %s", dir);
    clear_dir_list();
    return 0;
}
