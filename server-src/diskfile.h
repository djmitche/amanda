/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991 University of Maryland
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
 * diskfile.h -  interface for disklist file reading code
 */
#ifndef DISKFILE_H
#define DISKFILE_H

#include "amanda.h"
#include "conffile.h"

typedef struct host_s {
    struct host_s *next;		/* next host */
    char *hostname;			/* name of host */
    struct disk_s *disks;		/* linked list of disk records */
    int inprogress;			/* # dumps in progress */
    int maxdumps;			/* maximum dumps in parallel */
    interface_t *netif;			/* network interface this host is on */
    char *up;				/* generic user pointer */
} host_t;

typedef struct disk_s {
    int line;				/* line number of last definition */
    struct disk_s *prev, *next;		/* doubly linked disk list */

    host_t *host;			/* host list */
    struct disk_s *hostnext;

    char *name;				/* device name for disk, eg "sd0g" */
    char *dtype_name;			/* name of dump type   XXX shouldn't need this */
    char *program;			/* dump program, eg DUMP, GNUTAR */
    char *exclude;			/* file exclude list */
    long priority;			/* priority of disk */
    long dumpcycle;			/* days between fulls */
    long frequency;			/* XXX - not used */
    auth_t auth;			/* type of authentication (per system?) */
    int maxdumps;			/* max number of parallel dumps (per system) */
    time_t start_t;			/* start this dump after this time */
    int strategy;			/* what dump strategy to use */
    int compress;			/* type of compression to use */
    /* flag options */
    int record:1;			/* record dump in /etc/dumpdates ? */
    int skip_incr:1;			/* incs done externally ? */
    int skip_full:1;			/* fulls done externally ? */
    int no_hold:1;			/* don't use holding disk ? */
    int kencrypt:1;
    int index:1;			/* produce an index ? */
    int platter;			/* platter # - for parallel dumps */
    int inprogress;			/* being dumped now? */
    void *up;				/* generic user pointer */
} disk_t;

typedef struct disklist_s {
    disk_t *head, *tail;
} disklist_t;

#define empty(dlist)	((dlist).head == NULL)


disklist_t *read_diskfile P((char *filename));

host_t *lookup_host P((char *hostname));
disk_t *lookup_disk P((char *hostname, char *diskname));

void enqueue_disk P((disklist_t *list, disk_t *disk));
void insert_disk P((disklist_t *list, disk_t *disk, int (*f)(disk_t *a, disk_t *b)));
void sort_disk P((disklist_t *in, disklist_t *out, int (*f)(disk_t *a, disk_t *b)));
disk_t *dequeue_disk P((disklist_t *list));
void remove_disk P((disklist_t *list, disk_t *disk));

void dump_queue P((char *str, disklist_t q, int npr, FILE *f));

#endif /* ! DISKFILE_H */
