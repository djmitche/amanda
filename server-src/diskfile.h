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
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/*
 * $Id: diskfile.h,v 1.25 2002/04/17 20:06:10 martinea Exp $
 *
 * interface for disklist file reading code
 */
#ifndef DISKFILE_H
#define DISKFILE_H

#include "amanda.h"
#include "conffile.h"
#include "amfeatures.h"

typedef struct host_s {
    struct host_s *next;		/* next host */
    char *hostname;			/* name of host */
    struct disk_s *disks;		/* linked list of disk records */
    int inprogress;			/* # dumps in progress */
    int maxdumps;			/* maximum dumps in parallel */
    interface_t *netif;			/* network interface this host is on */
    time_t start_t;			/* start dump after this time */
    char *up;				/* generic user pointer */
    am_feature_t *features;		/* feature set */
} host_t;

typedef struct disk_s {
    int line;				/* line number of last definition */
    struct disk_s *prev, *next;		/* doubly linked disk list */

    host_t *host;			/* host list */
    struct disk_s *hostnext;

    char *name;				/* label name for disk */
    char *device;			/* device name for disk, eg "sd0g" */
    char *dtype_name;			/* name of dump type   XXX shouldn't need this */
    char *program;			/* dump program, eg DUMP, GNUTAR */
    sl_t *exclude_file;			/* file exclude spec */
    sl_t *exclude_list;			/* exclude list */
    sl_t *include_file;			/* file include spec */
    sl_t *include_list;			/* include list */
    int exclude_optional;		/* exclude list are optional */
    int include_optional;		/* include list are optional */
    long priority;			/* priority of disk */
    long dumpcycle;			/* days between fulls */
    long frequency;			/* XXX - not used */
    char *security_driver;		/* type of authentication (per disk) */
    int maxdumps;			/* max number of parallel dumps (per system) */
    time_t start_t;			/* start this dump after this time */
    int strategy;			/* what dump strategy to use */
    int compress;			/* type of compression to use */
    float comprate[2];			/* default compression rates */
    /* flag options */
    unsigned int record:1;			/* record dump in /etc/dumpdates ? */
    unsigned int skip_incr:1;			/* incs done externally ? */
    unsigned int skip_full:1;			/* fulls done externally ? */
    unsigned int no_hold:1;			/* don't use holding disk ? */
    unsigned int kencrypt:1;
    unsigned int index:1;			/* produce an index ? */
    int spindle;			/* spindle # - for parallel dumps */
    int inprogress;			/* being dumped now? */
    int todo;
    void *up;				/* generic user pointer */
} disk_t;

typedef struct disklist_s {
    disk_t *head, *tail;
} disklist_t;

#define empty(dlist)	((dlist).head == NULL)


int read_diskfile P((const char *, disklist_t *));

host_t *lookup_host P((const char *hostname));
disk_t *lookup_disk P((const char *hostname, const char *diskname));

disk_t *add_disk P((disklist_t *list, char *hostname, char *diskname));

void enqueue_disk P((disklist_t *list, disk_t *disk));
void headqueue_disk P((disklist_t *list, disk_t *disk));
void insert_disk P((disklist_t *list, disk_t *disk, int (*f)(disk_t *a, disk_t *b)));
int  find_disk P((disklist_t *list, disk_t *disk));
void sort_disk P((disklist_t *in, disklist_t *out, int (*f)(disk_t *a, disk_t *b)));
disk_t *dequeue_disk P((disklist_t *list));
void remove_disk P((disklist_t *list, disk_t *disk));

void dump_queue P((char *str, disklist_t q, int npr, FILE *f));

char *optionstr P((disk_t *dp));

void match_disklist P((disklist_t *origqp, int sargc, char **sargv));

#endif /* ! DISKFILE_H */
