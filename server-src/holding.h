/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1994 University of Maryland
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
 * $Id: holding.h,v 1.2 1997/11/17 13:06:01 amcore Exp $
 *
 */

#ifndef HOLDING_H
#define HOLDING_H

#include "amanda.h"
#include "diskfile.h"

extern disklist_t *diskqp;
extern struct dirname {
    struct dirname *next;
    char *name;
} *dir_list;
extern int ndirs;
char host[MAX_HOSTNAME_LENGTH], *domain;

/* local functions */
int is_dir P((char *fname));
int is_emptyfile P((char *fname));
int is_datestr P((char *fname));
int non_empty P((char *fname));
struct dirname *insert_dirname P((char *name));
char get_letter_from_user P((void));
int select_dir P((void));
void scan_holdingdisk P((char *diskdir,int verbose));
void pick_datestamp P((void));
int get_amanda_names P((char *fname, char *hostname, char *diskname,
			int *level));

#endif /* HOLDING_H */
