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
 * $Id: diskfile.c,v 1.44 2002/02/10 03:34:04 jrjackson Exp $
 *
 * read disklist file
 */
#include "amanda.h"
#include "arglist.h"
#include "conffile.h"
#include "diskfile.h"

static host_t *hostlist;

/* local functions */
static char *upcase P((char *st));
static int parse_diskline P((disklist_t *, const char *, FILE *, int *, char **));
static void parserror P((const char *, int, const char *, ...))
    __attribute__ ((format (printf, 3, 4)));


int
read_diskfile(filename, lst)
    const char *filename;
    disklist_t *lst;
{
    FILE *diskf;
    int line_num;
    char *line;

    /* initialize */

    hostlist = NULL;
    lst->head = lst->tail = NULL;
    line_num = 0;

    if ((diskf = fopen(filename, "r")) == NULL) {
	error("could not open disklist file \"%s\": %s",
	      filename, strerror(errno));
    }

    while ((line = agets(diskf)) != NULL) {
	line_num++;
	if (parse_diskline(lst, filename, diskf, &line_num, &line) < 0) {
	    amfree(line);
	    afclose(diskf);
	    return (-1);
	}
	amfree(line);
    }

    afclose(diskf);
    return (0);
}

host_t *
lookup_host(hostname)
    const char *hostname;
{
    host_t *p;

    for (p = hostlist; p != NULL; p = p->next) {
	if(strcasecmp(p->hostname, hostname) == 0) return p;
    }
    return (NULL);
}

disk_t *
lookup_disk(hostname, diskname)
    const char *hostname, *diskname;
{
    host_t *host;
    disk_t *disk;

    host = lookup_host(hostname);
    if (host == NULL)
	return (NULL);

    for (disk = host->disks; disk != NULL; disk = disk->hostnext) {
	if (strcmp(disk->name, diskname) == 0)
	    return (disk);
    }
    return (NULL);
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

void headqueue_disk(list, disk)	/* put disk on head of queue */
disklist_t *list;
disk_t *disk;
{
    if(list->head == NULL) list->tail = disk;
    else list->head->prev = disk;
    disk->next = list->head;

    list->head = disk;
    disk->prev = NULL;
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

disk_t *add_disk(list, hostname, diskname)
disklist_t *list;
char *hostname;
char *diskname;
{
    disk_t *disk;
    host_t *host;

    disk = alloc(sizeof(disk_t));
    disk->line = 0;
    disk->name = stralloc(diskname);
    disk->spindle = -1;
    disk->up = NULL;
    disk->compress = COMP_NONE;
    disk->start_t = 0;

    host = lookup_host(hostname);
    if(host == NULL) {
	host = alloc(sizeof(host_t));
	host->next = hostlist;
	hostlist = host;

	host->hostname = stralloc(hostname);
	host->disks = NULL;
	host->up = NULL;
	host->inprogress = 0;
	host->maxdumps = 1;
	host->start_t = 0;
    }
    enqueue_disk(list, disk);

    disk->host = host;
    disk->hostnext = host->disks;
    host->disks = disk;

    return disk;
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
	if(islower((int)*s)) *s = toupper((int)*s);
	s++;
    }
    return st;
}


static int
parse_diskline(lst, filename, diskf, line_num_p, line_p)
    disklist_t *lst;
    const char *filename;
    FILE *diskf;
    int *line_num_p;
    char **line_p;
{
    host_t *host;
    disk_t *disk;
    dumptype_t *dtype;
    interface_t *netif = 0;
    char *hostname = NULL;
    char *s, *fp;
    int ch, dup = 0;
    char *line = *line_p;
    int line_num = *line_num_p;

    assert(filename != NULL);
    assert(line_num > 0);
    assert(line != NULL);

    s = line;
    ch = *s++;
    skip_whitespace(s, ch);
    if(ch == '\0' || ch == '#')
	return (0);

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
	parserror(filename, line_num, "disk device name expected");
	return (-1);
    }
    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    /* check for duplicate disk */

    if(host && (disk = lookup_disk(hostname, fp)) != NULL) {
	parserror(filename, line_num,
	    "duplicate disk record, previous on line %d", disk->line);
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
	parserror(filename, line_num, "disk dumptype expected");
	if(host == NULL) amfree(hostname);
	if(!dup) {
	    amfree(disk->name);
	    amfree(disk);
	}
	return (-1);
    }
    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    if (fp[0] == '{') {
	s[-1] = ch;
	s = fp+2;
	skip_whitespace(s, ch);
	if (ch != '\0' && ch != '#') {
	    parserror(filename, line_num,
		      "expected line break after `{\', ignoring rest of line");
	}

	if (strchr(s-1, '}') &&
	    (strchr(s-1, '#') == NULL ||
	     strchr(s-1, '}') < strchr(s-1, '#'))) {
	    if(host == NULL) amfree(hostname);
	    if(!dup) {
		amfree(disk->name);
		amfree(disk);
	    }
	    return (-1);
	}
	amfree(line);

	dtype = read_dumptype(vstralloc("custom(", hostname,
					":", disk->name, ")", 0),
			      diskf, (char*)filename, line_num_p);

	*line_p = line = agets(diskf);
	line_num = *line_num_p; /* no incr, read_dumptype did it already */

	if (dtype == NULL || dup) {
	    if(host == NULL) amfree(hostname);
	    if(!dup) {
	      amfree(disk->name);
	      amfree(disk);
	    }
	    return (-1);
	}

	if (line == NULL)
	    *line_p = line = stralloc("");
	s = line;
	ch = *s++;
    } else if((dtype = lookup_dumptype(upcase(fp))) == NULL) {
	parserror(filename, line_num, "undefined dumptype `%s'", fp);
	if(host == NULL) amfree(hostname);
	if (!dup) {
	    amfree(disk->name);
	    amfree(disk);
	}
	return (-1);
    }

    if (dup) {
	if (host == NULL) amfree(hostname);
	return (-1);
    }

    disk->dtype_name	= dtype->name;
    disk->program	= dtype->program;
    disk->exclude	= dtype->exclude;
    disk->exclude_list	= dtype->exclude_list;
    disk->priority	= dtype->priority;
    disk->dumpcycle	= dtype->dumpcycle;
    disk->frequency	= dtype->frequency;
    disk->security_driver = dtype->security_driver;
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
    disk->todo		= 1;

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
	    parserror(filename, line_num,
		"undefined network interface `%s'", fp);
	    if(host == NULL) amfree(hostname);
	    amfree(disk->name);
	    amfree(disk);
	    return (-1);
	}
    } else {
	netif = lookup_interface("");
    }

    skip_whitespace(s, ch);
    if(ch && ch != '#') {		/* now we have garbage, ignore it */
	parserror(filename, line_num, "end of line expected");
    }

    if(dtype->ignore || dtype->strategy == DS_SKIP) {
	return (-1);
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

    enqueue_disk(lst, disk);

    disk->host = host;
    disk->hostnext = host->disks;
    host->disks = disk;
    host->maxdumps = disk->maxdumps;

    return (0);
}


printf_arglist_function2(static void parserror, const char *, filename,
    int, line_num, const char *, format)
{
    va_list argp;

    /* print error message */

    fprintf(stderr, "\"%s\", line %d: ", filename, line_num);
    arglist_start(argp, format);
    vfprintf(stderr, format, argp);
    arglist_end(argp);
    fputc('\n', stderr);
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
    char *kencrypt_opt = "";
    char *compress_opt = "";
    char *record_opt = "";
    char *index_opt = "";
    char *exclude_opt1 = "";
    char *exclude_opt2 = "";
    char *exclude_opt3 = "";

    amfree(str);

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

    if(dp->kencrypt) kencrypt_opt = "kencrypt;";

    return vstralloc(";",
		     "auth=",
		     dp->security_driver,
		     ";",
		     kencrypt_opt,
		     compress_opt,
		     record_opt,
		     index_opt,
		     exclude_opt1,
		     exclude_opt2,
		     exclude_opt3,
		     NULL);
}

 
void match_disklist(disklist_t *origqp, int sargc, char **sargv)
{
    char *prevhost = NULL;
    int i;
    int match_a_host;
    int match_a_disk;
    int prev_match;
    disk_t *dp;

    if(sargc <= 0)
	return;

    for(dp = origqp->head; dp != NULL; dp = dp->next) {
	if(dp->todo == 1)
	    dp->todo = -1;
    }

    prev_match = 0;
    for(i=0;i<sargc;i++) {
	match_a_host = 0;
	for(dp = origqp->head; dp != NULL; dp = dp->next) {
	    if(match_host(sargv[i], dp->host->hostname))
		match_a_host = 1;
	}
	match_a_disk = 0;
	for(dp = origqp->head; dp != NULL; dp = dp->next) {
	    if(prevhost != NULL &&
	       match_host(prevhost, dp->host->hostname) &&
	       match_disk(sargv[i], dp->name)) {
		if(match_a_host) {
		    error("Argument %s match a host and a disk",sargv[i]);
		}
		else {
		    if(dp->todo == -1) {
			dp->todo = 1;
			match_a_disk = 1;
			prev_match = 0;
		    }
		}
	    }
	}
	if(!match_a_disk) {
	    if(match_a_host == 1) {
		if(prev_match == 1) { /* all disk of the previous host */
		    for(dp = origqp->head; dp != NULL; dp = dp->next) {
			if(match_host(prevhost,dp->host->hostname))
			    if(dp->todo == -1)
				dp->todo = 1;
		    }
		}
		prevhost = sargv[i];
		prev_match = 1;
	    }
	    else {
		prev_match = 0;
		/*error("%s match nothing",sargv[i]);*/
	    }
	}
    }

    if(prev_match == 1) { /* all disk of the previous host */
	for(dp = origqp->head; dp != NULL; dp = dp->next) {
	    if(match_host(prevhost,dp->host->hostname))
		if(dp->todo == -1)
		    dp->todo = 1;
	}
    }

    for(dp = origqp->head; dp != NULL; dp = dp->next) {
	if(dp->todo == -1)
	    dp->todo = 0;
    }
}

 
#ifdef TEST

static void dump_disk P((const disk_t *));
static void dump_disklist P((const disklist_t *));
int main P((int, char *[]));

char *config_name = NULL;
char *config_dir = NULL;

static void
dump_disk(dp)
    const disk_t *dp;
{
    printf("  DISK %s (HOST %s, LINE %d) TYPE %s NAME %s SPINDLE %d\n",
	   dp->name, dp->host->hostname, dp->line, dp->dtype_name,
	   dp->name == NULL? "(null)": dp->name,
	   dp->spindle);
}

static void
dump_disklist(lst)
    const disklist_t *lst;
{
    const disk_t *dp, *prev;
    const host_t *hp;

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
    for(dp = lst->head; dp != NULL; prev = dp, dp = dp->next) {
	dump_disk(dp);
	/* check pointers */
	if(dp->prev != prev) printf("*** prev pointer mismatch!\n");
	if(dp->next == NULL && lst->tail != dp) printf("tail mismatch!\n");
    }
}

int
main(argc, argv)
     int argc;
     char *argv[];
{
  char *conffile;
  char *conf_diskfile;
  disklist_t lst;
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

  if (argc>1) {
    config_name = argv[1];
    if (strchr(config_name, '/') != NULL) {
      config_dir = stralloc2(argv[1], "/");
      config_name = strrchr(config_name, '/') + 1;
    } else {
      config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
    }
  } else {
    config_dir = stralloc("");
  }
  conffile = stralloc2(config_dir, CONFFILE_NAME);
  if((result = read_conffile(conffile)) == 0) {
    conf_diskfile = getconf_str(CNF_DISKFILE);
    if (*conf_diskfile == '/') {
      conf_diskfile = stralloc(conf_diskfile);
    } else {
      conf_diskfile = stralloc2(config_dir, conf_diskfile);
    }
    result = read_diskfile(conf_diskfile, &lst);
    if(result == 0) {
      dump_disklist(&lst);
    }
    amfree(conf_diskfile);
  }
  amfree(conffile);
  amfree(config_dir);

  malloc_size_2 = malloc_inuse(&malloc_hist_2);

  if(malloc_size_1 != malloc_size_2) {
    malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
  }

  return result;
}
#endif /* TEST */
