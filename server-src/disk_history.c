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
/* $Id: disk_history.c,v 1.5.2.2 1998/04/08 16:26:47 amcore Exp $
 *
 * functions for obtaining backup history
 */

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
	amfree(this);
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

    strncpy(new->date, date, sizeof(new->date)-1);
    new->date[sizeof(new->date)-1] = '\0';
    new->level = level;
    strncpy(new->tape, tape, sizeof(new->tape)-1);
    new->tape[sizeof(new->tape)-1] = '\0';
    new->file = file;

    if (disk_hist == NULL)
    {
	disk_hist = new;
	new->next = NULL;
	return;
    }

    if (strcmp(disk_hist->date, new->date) <= 0)
    {
	new->next = disk_hist;
	disk_hist = new;
	return;
    }

    before = disk_hist;
    item = disk_hist->next;
    while ((item != NULL) && (strcmp(item->date, new->date) > 0))
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
