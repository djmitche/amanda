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
 * diskfile.c - read disklist file
 */
#include "amanda.h"
#include "arglist.h"
#include "conffile.h"
#include "diskfile.h"


static disklist_t lst;
static FILE *diskf;
static char *diskfname = NULL;
static host_t *hostlist;
static int line_num, got_parserror;
static char str[1024];

/* local functions */
static void init_string P((char **ptrp, char *st));
static char *upcase P((char *st));
static int read_diskline P((void));
static void get_string P((void));
static void parserror P((char *format, ...));
static void eat_line P((void));


disklist_t *read_diskfile(filename)
char *filename;
{
    extern int errno;

    /* initialize */
    
    hostlist = NULL;
    lst.head = lst.tail = NULL;
    init_string(&diskfname, filename);
    line_num = got_parserror = 0;

    if((diskf = fopen(filename, "r")) == NULL)
       error("could not open disklist file \"%s\": %s", 
	     filename, strerror(errno));

    while(read_diskline());
    fclose(diskf);

    if(got_parserror) return NULL;
    else return &lst;
}

host_t *lookup_host(hostname)
char *hostname;
{
    host_t *p;
    int nameLen = strlen(hostname);

    for(p = hostlist; p != NULL; p = p->next) {
	if(!strncasecmp(p->hostname, hostname, nameLen)) {
	    if (p->hostname[nameLen] == '\0' || p->hostname[nameLen] == '.')
		return p;
	}
    }
    return NULL;
}

disk_t *lookup_disk(hostname, diskname)
char *hostname, *diskname;
{
    host_t *host;
    disk_t *disk;

    host = lookup_host(hostname);
    if(host == NULL) return NULL;

    for(disk = host->disks; disk != NULL; disk = disk->hostnext) {
	if(!strcmp(disk->name, diskname)) return disk;
    }
    return NULL;
}

void enqueue_disk(list, disk)	/* put disk on end of queue */
disklist_t *list;
disk_t *disk;
{
    if(list->tail == NULL) list->head = disk;
    else list->tail->next = disk;
    disk->prev = list->tail;

    list->tail = disk;
    disk->next = NULL;
}

void insert_disk(list, disk, cmp)	/* insert in sorted order */
disklist_t *list;
disk_t *disk;
int (*cmp) P((disk_t *a, disk_t *b));
{
    disk_t *prev, *ptr;

    prev = NULL;
    ptr = list->head;

    while(ptr != NULL) {
	if(cmp(disk, ptr) < 0) break;
	prev = ptr;
	ptr = ptr->next;
    }
    disk->next = ptr;
    disk->prev = prev;

    if(prev == NULL) list->head = disk;
    else prev->next = disk;
    if(ptr == NULL) list->tail = disk;
    else ptr->prev = disk;
}

void sort_disk(in, out, cmp)	/* sort a whole queue */
disklist_t *in;
disklist_t *out;
int (*cmp) P((disk_t *a, disk_t *b));
{
    disklist_t *tmp;
    disk_t *disk;

    tmp = in;		/* just in case in == out */

    out->head = (disk_t *)0;
    out->tail = (disk_t *)0;

    while(disk = dequeue_disk(tmp))
	insert_disk(out, disk, cmp);
}

disk_t *dequeue_disk(list)	/* remove disk from front of queue */
disklist_t *list;
{
    disk_t *disk;

    if(list->head == NULL) return NULL;

    disk = list->head;
    list->head = disk->next;

    if(list->head == NULL) list->tail = NULL;
    else list->head->prev = NULL;

    disk->prev = disk->next = NULL;	/* for debugging */
    return disk;
}

void remove_disk(list, disk)
disklist_t *list;
disk_t *disk;
{
    if(disk->prev == NULL) list->head = disk->next;
    else disk->prev->next = disk->next;

    if(disk->next == NULL) list->tail = disk->prev;
    else disk->next->prev = disk->prev;

    disk->prev = disk->next = NULL;
}

static void init_string(ptrp, s)
char *s, **ptrp;
{
    if(*ptrp) free(*ptrp);
    *ptrp = stralloc(s);
}

static char *upcase(st)
char *st;
{
    char *s = st;

    while(*s) {
	if(islower(*s)) *s = toupper(*s);
	s++;
    }
    return st;
}


static int read_diskline()
{
    host_t *host;
    disk_t *disk;
    dumptype_t *dtype;

    line_num += 1;

    get_string();
    if(*str == '\0') return 0;
    if(*str == '\n') return 1;

    host = lookup_host(str);
    if(host == NULL) {			/* new host */
	host = alloc(sizeof(host_t));
	host->next = hostlist;
	hostlist = host;

	host->hostname = stralloc(str);
	host->disks = NULL;
	host->up = NULL;
	host->inprogress = 0;
	host->maxdumps = 1;	/* will be overwritten */
	host->netif = lookup_interface("");
    }

    get_string();
    if(*str == '\0' || *str == '\n') {
	parserror("disk device name expected");
	return 1;
    }

    /* check for duplicate disk */

    if((disk = lookup_disk(host->hostname, str)) != NULL) {
	parserror("duplicate disk record, previous on line %d", disk->line);
	eat_line();
	return 1;
    }

    disk = alloc(sizeof(disk_t));
    disk->line = line_num;
    disk->name = stralloc(str);
    disk->platter = -1;
    disk->up = NULL;
    disk->inprogress = 0;
    
    get_string();
    if(*str == '\0' || *str == '\n') {
	parserror("disk dumptype expected");
	free(disk->name);
	free(disk);
	return 1;
    }

    if((dtype = lookup_dumptype(upcase(str))) == NULL) {
	parserror("undefined dumptype `%s'", str);
	free(disk->name);
	free(disk);
	eat_line();
	return 1;
    }
    disk->dtype_name = dtype->name;
    disk->program   = dtype->program;
    disk->exclude   = dtype->exclude;
    disk->priority  = dtype->priority;
    disk->dumpcycle = dtype->dumpcycle;
    disk->frequency = dtype->frequency;
    disk->auth      = dtype->auth;
    disk->maxdumps  = dtype->maxdumps;
    disk->start_t   = dtype->start_t;
    disk->strategy  = dtype->strategy;
    disk->compress  = dtype->compress;
    disk->record    = dtype->record;
    disk->skip_incr = dtype->skip_incr;
    disk->skip_full = dtype->skip_full;
    disk->no_hold   = dtype->no_hold;
    disk->kencrypt  = dtype->kencrypt;
    disk->index     = dtype->index;

    get_string();
    if(*str != '\n' && *str != '\0') {	/* got optional platter number */
	disk->platter = atoi(str);
	get_string();
    }

    if(*str != '\n' && *str != '\0') {	/* got optional network interface */
	if((host->netif = lookup_interface(upcase(str))) == NULL) {
	    parserror("undefined network interface `%s'", str);
	    free(disk->name);
	    free(disk);
	    eat_line();
	    return 1;
	    }
	get_string();
    }

    if(*str != '\n' && *str != '\0')	/* now we have garbage, ignore it */
	parserror("end of line expected");

    /* success, add disk to lists */

    enqueue_disk(&lst, disk);

    disk->host = host;
    disk->hostnext = host->disks;
    host->disks = disk;
    host->maxdumps = disk->maxdumps;

    return 1;
}


arglist_function(static void parserror, char *, format)
{
    va_list argp;

    /* print error message */

    fprintf(stderr, "\"%s\", line %d: ", diskfname, line_num);
    arglist_start(argp, format);
    vfprintf(stderr, format, argp);
    arglist_end(argp);
    fputc('\n', stderr);

    got_parserror = 1;
}

static void eat_line()
{
    int ch;

    do {
	ch = getc(diskf); 
    } while(ch != '\n' && ch != EOF);
}

static void get_string()
{
    int ch;
    char *p;

    ch = getc(diskf);

    /* eat whitespace */
    while(ch == ' ' || ch == '\t') ch = getc(diskf);

    /* eat comment - everything but eol/eof */
    if(ch == '#') do {
	ch = getc(diskf);
    } while(ch != '\n' && ch != EOF);

    p = str;
    if(ch == '\n') *p++ = ch;
    else if(ch != EOF) {
	while(ch!=' ' && ch!='\t' && ch!='#' && ch!='\n' && ch!=EOF) {
	    *p++ = ch;
	    ch = getc(diskf);
	}
	ungetc(ch, diskf);
    }

    *p = '\0';
}


void dump_queue(st, q, npr, f)
char *st;
disklist_t q;
int npr;	/* we print first npr disks on queue, plus last two */
FILE *f;
{
    disk_t *d,*p;
    int pos;

    if(empty(q)) {
	fprintf(f, "%s QUEUE: empty\n", st);
	return;
    }
    fprintf(f, "%s QUEUE:\n", st);
    for(pos = 0, d = q.head, p = NULL; d != NULL; p = d, d = d->next, pos++) {
	if(pos < npr) fprintf(f, "%3d: %-10s %-4s\n",
			     pos, d->host->hostname, d->name);
    }
    if(pos > npr) {
	if(pos > npr+2) fprintf(f, "  ...\n");
	if(pos > npr+1) {
	    d = p->prev;
	    fprintf(f, "%3d: %-10s %-4s\n", pos-2, d->host->hostname, d->name);
	}
	d = p;
	fprintf(f, "%3d: %-10s %-4s\n", pos-1, d->host->hostname, d->name);
    }
}

#ifdef TEST

dump_disklist()
{
    disk_t *dp, *prev;
    host_t *hp;

    if(hostlist == NULL) {
	printf("DISKLIST not read in\n");
	return;
    }

    printf("DISKLIST BY HOSTNAME:\n");

    for(hp = hostlist; hp != NULL; hp = hp->next) {
	printf("HOST %s, busy = %d\n", hp->hostname, hp->busy);
	for(dp = hp->disks; dp != NULL; dp = dp->hostnext)
	    dump_disk(dp);
	putchar('\n');
    }
    
    
    printf("DISKLIST IN FILE ORDER:\n");

    prev = NULL;
    for(dp = lst.head; dp != NULL; prev = dp, dp = dp->next) {
	dump_disk(dp);
	/* check pointers */
	if(dp->prev != prev) printf("*** prev pointer mismatch!\n");
	if(dp->next == NULL && lst.tail != dp) printf("tail mismatch!\n");
    }
}

dump_disk(dp)
disk_t *dp;
{
    printf("  DISK %s (HOST %s, LINE %d) TYPE %s FILESYS %s\n",
	   dp->name, dp->host->hostname, dp->line, dp->dtype_name,
	   dp->filesys == NULL? "(null)": dp->filesys);
}
#endif /* TEST */
