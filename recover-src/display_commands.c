/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998 University of Maryland at College Park
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
 * $Id: display_commands.c,v 1.12.4.1.6.2 2002/03/24 19:23:23 jrjackson Exp $
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
	amfree(this);
    } while (dir_list != NULL);
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
	dir_list = (DIR_ITEM *)alloc(sizeof(DIR_ITEM));
	dir_list->next = NULL;
	strncpy(dir_list->date, date, sizeof(dir_list->date)-1);
	dir_list->date[sizeof(dir_list->date)-1] = '\0';
	dir_list->level = level;
	strncpy(dir_list->tape, tape, sizeof(dir_list->tape)-1);
	dir_list->tape[sizeof(dir_list->tape)-1] = '\0';
	strncpy(dir_list->path, path, sizeof(dir_list->path)-1);
	dir_list->path[sizeof(dir_list->path)-1] = '\0';

	return 0;
    }

    last = dir_list;
    while (last->next != NULL)
    {
	last = last->next;
    }

    last->next = (DIR_ITEM *)alloc(sizeof(DIR_ITEM));
    last->next->next = NULL;
    strncpy(last->next->date, date, sizeof(last->next->date)-1);
    last->next->date[sizeof(last->next->date)-1] = '\0';
    last->next->level = level;
    strncpy(last->next->tape, tape, sizeof(last->next->tape)-1);
    last->next->tape[sizeof(last->next->tape)-1] = '\0';
    strncpy(last->next->path, path, sizeof(last->next->path)-1);
    last->next->path[sizeof(last->next->path)-1] = '\0';

    return 0;
}


void list_disk_history P((void))
{
    if (converse("DHST") == -1)
	exit(1);
}


void suck_dir_list_from_server P((void))
{
    char *cmd = NULL;
    char *err = NULL;
    int i;
    char *l = NULL;
    char *date, *date_undo, date_undo_ch = '\0';
    int level;
    char *tape, *tape_undo, tape_undo_ch = '\0';
    char *dir, *dir_undo, dir_undo_ch = '\0';
    char *disk_path_slash = NULL;
    char *disk_path_slash_dot = NULL;
    char *s;
    int ch;

    if (disk_path == NULL) {
	printf("Directory must be set before getting listing\n");
	return;
    } else if(strcmp(disk_path, "/") == 0) {
	disk_path_slash = stralloc(disk_path);
    } else {
	disk_path_slash = stralloc2(disk_path, "/");
    }

    clear_dir_list();

    cmd = stralloc2("OLSD ", disk_path);
    if (send_command(cmd) == -1) {
	amfree(cmd);
	amfree(disk_path_slash);
	exit(1);
    }
    amfree(cmd);
    /* skip preamble */
    if ((i = get_reply_line()) == -1) {
	amfree(disk_path_slash);
	exit(1);
    }
    if (i == 0)				/* assume something wrong! */
    {
	amfree(disk_path_slash);
	l = reply_line();
	printf("%s\n", l);
	return;
    }
    disk_path_slash_dot = stralloc2(disk_path_slash, ".");
    amfree(cmd);
    amfree(err);
    date_undo = tape_undo = dir_undo = NULL;
    /* skip the last line -- duplicate of the preamble */
    while ((i = get_reply_line()) != 0)
    {
	if (i == -1) {
	    amfree(disk_path_slash_dot);
	    amfree(disk_path_slash);
	    exit(1);
	}
	if(err) {
	    if(cmd == NULL) {
		if(tape_undo) *tape_undo = tape_undo_ch;
		if(dir_undo) *dir_undo = dir_undo_ch;
		date_undo = tape_undo = dir_undo = NULL;
		cmd = stralloc(l);	/* save for the error report */
	    }
	    continue;			/* throw the rest of the lines away */
	}
	l = reply_line();
	if (!server_happy())
	{
	    printf("%s\n", l);
	    continue;
	}
#define sc "201-"
	if (strncmp(l, sc, sizeof(sc)-1) != 0) {
	    err = "bad reply: not 201-";
	    continue;
	}
	s = l + sizeof(sc)-1;
	ch = *s++;
#undef sc
	skip_whitespace(s, ch);
	if(ch == '\0') {
	    err = "bad reply: missing date field";
	    continue;
	}
	date = s - 1;
	skip_non_whitespace(s, ch);
	date_undo = s - 1;
	date_undo_ch = *date_undo;
	*date_undo = '\0';

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	    err = "bad reply: cannot parse level field";
	    continue;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    err = "bad reply: missing tape field";
	    continue;
	}
	tape = s - 1;
	skip_non_whitespace(s, ch);
	tape_undo = s - 1;
	tape_undo_ch = *tape_undo;
	*tape_undo = '\0';

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    err = "bad reply: missing directory field";
	    continue;
	}
	dir = s - 1;

	/* add a '.' if it a the entry for the current directory */
	if(strcmp(disk_path,dir)==0 || strcmp(disk_path_slash,dir)==0) {
	    dir = disk_path_slash_dot;
	}
	add_dir_list_item(date, level, tape, dir);
    }
    amfree(disk_path_slash_dot);
    amfree(disk_path_slash);
    if(!server_happy()) {
	puts(reply_line());
    } else if(err) {
	if(*err) {
	    puts(err);
	}
	puts(cmd);
	clear_dir_list();
    }
    amfree(cmd);
}


void list_directory P((void))
{
    size_t i;
    DIR_ITEM *item;
    FILE *fp;
    char *pager;
    char *pager_command;

    if (disk_path == NULL) {
	printf("Must select a disk before listing files\n");
	return;
    }

    if ((pager = getenv("PAGER")) == NULL)
    {
	pager = "more";
    }
    /*
     * Set up the pager command so if the pager is terminated, we do
     * not get a SIGPIPE back.
     */
    pager_command = stralloc2(pager, " ; /bin/cat > /dev/null");
    if ((fp = popen(pager_command, "w")) == NULL)
    {
	printf("Warning - can't pipe through %s\n", pager);
	fp = stdout;
    }
    amfree(pager_command);
    i = strlen(disk_path);
    if (i != 1)
	i++;				/* so disk_path != "/" */
    for (item = get_dir_list(); item != NULL; item=get_next_dir_item(item))
	fprintf(fp, "%s %s\n", item->date, item->path+i);
    apclose(fp);
}
