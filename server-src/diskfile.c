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
 * $Id: diskfile.c,v 1.27.4.4 1999/09/05 23:18:07 jrj Exp $
 *
 * read disklist file
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

/* local functions */
static char *upcase P((char *st));
static int read_diskline P((void));
static void parserror P((char *format, ...))
    __attribute__ ((format (printf, 1, 2)));


disklist_t *read_diskfile(filename)
char *filename;
{
    extern int errno;

    /* initialize */

    hostlist = NULL;
    lst.head = lst.tail = NULL;
    diskfname = newstralloc(diskfname, filename);
    malloc_mark(diskfname);
    line_num = got_parserror = 0;

    if((diskf = fopen(filename, "r")) == NULL)
	error("could not open disklist file \"%s\": %s",
	      filename, strerror(errno));

    while(read_diskline());
    afclose(diskf);

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
	if(strcmp(disk->name, diskname) == 0) return disk;
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

int find_disk(list, disk)
disklist_t *list;
disk_t *disk;
/* check if disk is present in list. Return true if so, false otherwise. */
{
disk_t *t;

    for( t = list->head; t && t != disk; t = t->next );

    return t == disk;
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

    while((disk = dequeue_disk(tmp)))
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

static char *upcase(st)
char *st;
{
    char *s = st;

    while(*s) {
	if(islower((int)*s)) *s = toupper(*s);
	s++;
    }
    return st;
}


static int read_diskline()
{
    host_t *host;
    disk_t *disk;
    dumptype_t *dtype;
    interface_t *netif = 0;
    char *hostname = NULL;
    static char *line = NULL;
    char *s = NULL, *fp;
    int ch = '\0', dup = 0;

    amfree(line);
    for(; (line = agets(diskf)) != NULL; free(line)) {
	line_num += 1;
	s = line;
	ch = *s++;

	skip_whitespace(s, ch);
	if(ch != '\0' && ch != '#') break;
    }
    if(line == NULL) return 0;

    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    host = lookup_host(fp);
    if (host == NULL) {
      hostname = stralloc(fp);
      malloc_mark(hostname);
    } else {
      hostname = host->hostname;
    }

    skip_whitespace(s, ch);
    if(ch == '\0' || ch == '#') {
	parserror("disk device name expected");
	return 1;
    }
    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    /* check for duplicate disk */

    if(host && (disk = lookup_disk(hostname, fp)) != NULL) {
	parserror("duplicate disk record, previous on line %d", disk->line);
	dup = 1;
    } else {
	disk = alloc(sizeof(disk_t));
	malloc_mark(disk);
	disk->line = line_num;
	disk->name = stralloc(fp);
	malloc_mark(disk->name);
	disk->spindle = -1;
	disk->up = NULL;
	disk->inprogress = 0;
    }

    skip_whitespace(s, ch);
    if(ch == '\0' || ch == '#') {
	parserror("disk dumptype expected");
	if(host == NULL) amfree(hostname);
	if(!dup) {
	    amfree(disk->name);
	    amfree(disk);
	}
	return 1;
    }
    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    if (fp[0] == '{') {
	s[-1] = ch;
	s = fp+2;
	skip_whitespace(s, ch);
	if (ch != '\0' && ch != '#') {
	    parserror("expected line break after `{\', ignoring rest of line");
	}

	if (strchr(s-1, '}') &&
	    (strchr(s-1, '#') == NULL ||
	     strchr(s-1, '}') < strchr(s-1, '#'))) {
	    if(host == NULL) amfree(hostname);
	    if(!dup) {
		amfree(disk->name);
		amfree(disk);
	    }
	    return 1;
	}
	amfree(line);

	dtype = read_dumptype(vstralloc("custom(", hostname,
					":", disk->name, ")", 0),
			      diskf, diskfname, &line_num);

	line = agets(diskf);
	/* line_num += 1; */ /* read_dumptype did it already */

	if (dtype == NULL || dup) {
	    if(host == NULL) amfree(hostname);
	    if(!dup) {
	      amfree(disk->name);
	      amfree(disk);
	    }
	    return line != NULL;
	}

	if (line == NULL)
	    line = stralloc("");
	s = line;
	ch = *s++;
    } else if((dtype = lookup_dumptype(upcase(fp))) == NULL) {
	parserror("undefined dumptype `%s'", fp);
	if(host == NULL) amfree(hostname);
	if(!dup) {
	  amfree(disk->name);
	  amfree(disk);
	}
	return 1;
    }

    if (dup) {
	if(host == NULL) amfree(hostname);
	return 1;
    }

    disk->dtype_name	= dtype->name;
    disk->program	= dtype->program;
    disk->exclude	= dtype->exclude;
    disk->exclude_list	= dtype->exclude_list;
    disk->priority	= dtype->priority;
    disk->dumpcycle	= dtype->dumpcycle;
    disk->frequency	= dtype->frequency;
    disk->auth		= dtype->auth;
    disk->maxdumps	= dtype->maxdumps;
    disk->start_t	= dtype->start_t;
    disk->strategy	= dtype->strategy;
    disk->compress	= dtype->compress;
    disk->comprate[0]	= dtype->comprate[0];
    disk->comprate[1]	= dtype->comprate[1];
    disk->record	= dtype->record;
    disk->skip_incr	= dtype->skip_incr;
    disk->skip_full	= dtype->skip_full;
    disk->no_hold	= dtype->no_hold;
    disk->kencrypt	= dtype->kencrypt;
    disk->index		= dtype->index;

    skip_whitespace(s, ch);
    fp = s - 1;
    if(ch && ch != '#') {		/* get optional spindle number */
	disk->spindle = atoi(fp);
	skip_integer(s, ch);
    }

    skip_whitespace(s, ch);
    fp = s - 1;
    if(ch && ch != '#') {		/* get optional network interface */
	skip_non_whitespace(s, ch);
	s[-1] = '\0';
	if((netif = lookup_interface(upcase(fp))) == NULL) {
	    parserror("undefined network interface `%s'", fp);
	    if(host == NULL) amfree(hostname);
	    amfree(disk->name);
	    amfree(disk);
	    return 1;
	}
    } else {
	netif = lookup_interface("");
    }

    skip_whitespace(s, ch);
    if(ch && ch != '#') {		/* now we have garbage, ignore it */
	parserror("end of line expected");
    }

    if(dtype->ignore || dtype->strategy == DS_SKIP) {
	return 1;
    }

    /* success, add disk to lists */

    if(host == NULL) {			/* new host */
	host = alloc(sizeof(host_t));
	malloc_mark(host);
	host->next = hostlist;
	hostlist = host;

	host->hostname = hostname;
	hostname = NULL;
	host->disks = NULL;
	host->up = NULL;
	host->inprogress = 0;
	host->maxdumps = 1;		/* will be overwritten */
	host->start_t = 0;
    }

    host->netif = netif;

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

char *optionstr(dp)
disk_t *dp;
{
    static char *str = NULL;
    char *auth_opt = "";
    char *kencrypt_opt = "";
    char *compress_opt = "";
    char *record_opt = "";
    char *index_opt = "";
    char *exclude_opt1 = "";
    char *exclude_opt2 = "";
    char *exclude_opt3 = "";

    amfree(str);

    if(dp->auth == AUTH_BSD) {
	auth_opt = "bsd-auth;";
    } else if(dp->auth == AUTH_KRB4) {
	auth_opt = "krb4-auth;";
	if(dp->kencrypt) kencrypt_opt = "kencrypt;";
    }

    switch(dp->compress) {
    case COMP_FAST:
	compress_opt = "compress-fast;";
	break;
    case COMP_BEST:
	compress_opt = "compress-best;";
	break;
    case COMP_SERV_FAST:
	compress_opt = "srvcomp-fast;";
	break;
    case COMP_SERV_BEST:
        compress_opt = "srvcomp-best;";
	break;
    }

    if(!dp->record) record_opt = "no-record;";
    if(dp->index) index_opt = "index;";

    if(dp->exclude) {
	exclude_opt1 = dp->exclude_list ? "exclude-list=" : "exclude-file=";
	exclude_opt2 = dp->exclude;
	exclude_opt3 = ";";
    }

    return vstralloc(";",
		     auth_opt,
		     kencrypt_opt,
		     compress_opt,
		     record_opt,
		     index_opt,
		     exclude_opt1,
		     exclude_opt2,
		     exclude_opt3,
		     NULL);
}

#ifdef TEST

void
dump_disk(dp)
disk_t *dp;
{
    printf("  DISK %s (HOST %s, LINE %d) TYPE %s NAME %s SPINDLE %d\n",
	   dp->name, dp->host->hostname, dp->line, dp->dtype_name,
	   dp->name == NULL? "(null)": dp->name,
	   dp->spindle);
}

void
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
	printf("HOST %s INTERFACE %s\n",
	       hp->hostname,
	       (hp->netif == NULL||hp->netif->name == NULL) ? "(null)"
							    : hp->netif->name);
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

int
main(argc, argv)
int argc;
char *argv[];
{
  int result;
  int fd;
  unsigned long malloc_hist_1, malloc_size_1;
  unsigned long malloc_hist_2, malloc_size_2;

  for(fd = 3; fd < FD_SETSIZE; fd++) {
    /*
     * Make sure nobody spoofs us with a lot of extra open files
     * that would cause an open we do to get a very high file
     * descriptor, which in turn might be used as an index into
     * an array (e.g. an fd_set).
     */
    close(fd);
  }

  set_pname("diskfile");

  malloc_size_1 = malloc_inuse(&malloc_hist_1);

  if (argc>1)
    if (chdir(argv[1])) {
       perror(argv[1]);
       return 1;
    }
  if((result = read_conffile(CONFFILE_NAME)) == 0) {
    result = (read_diskfile(getconf_str(CNF_DISKFILE)) == NULL);
  }
  dump_disklist();

  malloc_size_2 = malloc_inuse(&malloc_hist_2);

  if(malloc_size_1 != malloc_size_2) {
    malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
  }

  return result;
}

#endif /* TEST */
