/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1995 University of Maryland at College Park
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
#ifndef GETFSENT_H
#define GETFSENT_H

#ifndef STANDALONE_TEST
#include "amanda.h"
#endif

#define FSTAB_RW	"rw"	/* writeable filesystem */
#define FSTAB_RQ	"rq"	/* writeable, with quotas */
#define FSTAB_RO	"ro"	/* read-only filesystem */
#define FSTAB_SW	"sw"	/* swap device */
#define FSTAB_XX	"xx"	/* ignore this entry */

typedef struct generic_fsent_s {
    char *fsname;
    char *fstype;
    char *mntdir;
    char *mntopts;
    int freq;
    int passno;
} generic_fsent_t;

int open_fstab P((void));
void close_fstab P((void));

int get_fstab_nextentry P((generic_fsent_t *fsent));
int search_fstab P((char *fsname, char *mntdir, generic_fsent_t *fsent));
int is_local_fstype P((generic_fsent_t *fsent));

char *amname_to_devname P((char *str));
char *amname_to_dirname P((char *str));

char *amname_to_fstype P((char *str));

#endif /* ! GETFSENT_H */
