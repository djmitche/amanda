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
/* $Id: list_dir.c,v 1.6 1997/12/09 06:59:47 amcore Exp $
 *
 * obtains directory listings from index files
 */

#include "amanda.h"
#include "amindexd.h"
#include "amindex.h"

typedef struct DIR_ITEM
{
    DUMP_ITEM *dump;
    char *path;

    struct DIR_ITEM *next;
}
DIR_ITEM;

static DIR_ITEM *dir_list = NULL; /* first dir entry */
static DIR_ITEM *dir_last = NULL; /* last dir entry  */
static DIR_ITEM *cur_list = NULL; /* current dir entry,for speeding up search */

static void clear_dir_list P((void))
{
    DIR_ITEM *this;

    if (dir_list == NULL)
	return;
    do
    {
	this = dir_list;
	dir_list = dir_list->next;
	free(this->path);
	free(this);
    }
    while (dir_list != NULL);
    dir_last = NULL;
    cur_list = NULL;
}

/* add item to list if path not already on list                     */
/* Since this function is almost called with increasing path order, */
/* we keep a pointer on the last element added (curr_list), this    */
/* reduce the time for the search of a path.                        */
/* It's true because the output of the index file is sorted         */
/* Maybe it could be more efficient if the index was sorted when    */
/* it is generated                                                  */
static int add_dir_list_item(dump, path)
DUMP_ITEM *dump;
char *path;
{
    DIR_ITEM *cur;

    if (dir_list == NULL)
    {
	if ((dir_list = (DIR_ITEM *)malloc(sizeof(DIR_ITEM))) == NULL)
	    return -1;
	dir_list->next = NULL;
	dir_list->dump = dump;
	dir_list->path = stralloc(path);
	dir_last=dir_list;
	cur_list=dir_list;
	return 0; /* added */
    }

    if(strcmp(path,dir_last->path) == 0)
	return 0; /* found */
    /* if smaller than last path */
    if(strcmp(path,dir_last->path) == -1)
    {
	if(cur_list==NULL)
	    cur_list=dir_list;
	/* reset cur_list if path is smaller than cur_list->path */
	if(strcmp(path,cur_list->path) == -1)
	    cur_list=dir_list;
	if(strcmp(path,cur_list->path) == 0)
	    return 0; /* found */
	while (cur_list->next!=NULL && (strcmp(path,cur_list->next->path) == 1))
	{
	    cur_list=cur_list->next;
	}
	if(strcmp(path,cur_list->next->path) == 0)
	{
	    cur_list=cur_list->next;
	    return 0; /* found */
	}
	/* add at cur_list */
	if((cur = (DIR_ITEM *)malloc(sizeof(DIR_ITEM))) == NULL)
	    return -1;
	cur->next = cur_list->next;
	cur->dump = dump;
	cur->path = stralloc(path);
	cur_list->next=cur;
	cur_list=cur;
	return 0; /* added */
    }
    else /* add at end of list */
    {
	if ((dir_last->next = (DIR_ITEM *)malloc(sizeof(DIR_ITEM))) == NULL)
	    return -1;
	dir_last=dir_last->next;
	dir_last->next = NULL;
	dir_last->dump = dump;
	dir_last->path = stralloc(path);
	return 0; /* added */
    }
}


/* find all matching entries in a dump listing */
/* return -1 if error - and writes error message */
static int process_ls_dump(dir, dump_item, recursive)
char *dir;
DUMP_ITEM *dump_item;
int  recursive;
{
    char cmd[2048];
    char line[2048];
    char filename[1024];
    char *filename_gz;
    struct stat stat_filename;
    char dir_slash[1024];
    char awk_print[1024];
    char awk_print_field[1024];
    int  no_fields;
    FILE *fp;
    char *c;

    /* count 2 plus the number of /'s is path to get no fields */
    strcpy(awk_print,"\"/\" $2");
    no_fields=2;
    if (strcmp(dir, "/") == 0)
    {
	strcpy(dir_slash, dir);
    }
    else
    {
	sprintf(dir_slash, "%s/", dir);
	for (c = dir; *c != '\0'; c++)
	    if (*c == '/') {
		no_fields++;
		sprintf(awk_print_field," \"/\" $%d",no_fields);
		strcat(awk_print,awk_print_field);
	    }
    }
    /* awk_print is set to '"/" $2 "/" $3 ...' according to no_fields */

    filename_gz=getindexfname(dump_hostname, disk_name, dump_item->date,
			      dump_item->level);
    if(uncompress_file(filename_gz,filename)!=0)
    {
	reply(599, "System error %d", errno);
	return -1;
    }

    if(recursive) {
        sprintf(cmd, "grep \"^%s\" %s 2>/dev/null | uniq",
	        dir_slash, filename);
    }
    else { /* not recursive */
        sprintf(cmd, "grep \"^%s\" %s 2>/dev/null | awk -F \\/ '{if(NF<%d) print %s; else print %s \"/\"}' | uniq",
	        dir_slash, filename, no_fields+1, awk_print, awk_print);
    }
    dbprintf(("c %s\n", cmd));
    if ((fp = popen(cmd, "r")) == NULL)
    {
	reply(599, "System error %d", errno);
	return -1;
    }
    while (fgets(line, LONG_LINE, fp) != NULL)
    {
	/* sometimes cut returns a blank line if no input! */
	if (strlen(line) > 1)
	{
	    line[strlen(line)-1] = '\0';	/* overwrite '\n' */
	    add_dir_list_item(dump_item, line);
	}
    }
    pclose(fp);

    return 0;
}


int opaque_ls(dir,recursive)
char *dir;
int  recursive;
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
    if (process_ls_dump(dir, dump_item,recursive) == -1)
	return -1;

    /* go back processing higher level dumps till we hit a level 0 dump */
    last_level = dump_item->level;
    while ((last_level != 0) && ((dump_item=next_dump(dump_item)) != NULL))
    {
	if (dump_item->level < last_level)
	{
	    last_level = dump_item->level;
	    if (process_ls_dump(dir, dump_item,recursive) == -1)
		return -1;
	}
    }

    /* return the information to the caller */
    if(recursive)
    {
	lreply(200, " Opaque recursive list of %s", dir);
	for (dir_item = dir_list; dir_item != NULL; dir_item = dir_item->next)
	    fast_lreply(201, " %s %d %-16s %s",
			dir_item->dump->date, dir_item->dump->level,
			dir_item->dump->tape, dir_item->path);
	reply(200, " Opaque recursive list of %s", dir);
    }
    else
    {
	lreply(200, " Opaque list of %s", dir);
	for (dir_item = dir_list; dir_item != NULL; dir_item = dir_item->next)
	    lreply(201, " %s %d %-16s %s",
		   dir_item->dump->date, dir_item->dump->level,
		   dir_item->dump->tape, dir_item->path);
	reply(200, " Opaque list of %s", dir);
    }
    clear_dir_list();
    return 0;
}
