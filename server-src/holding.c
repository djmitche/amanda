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
 * $Id: holding.c,v 1.6 1997/12/16 18:02:34 jrj Exp $
 *
 * Functions to access holding disk
 */

#include "amanda.h"
#include "holding.h"

#define MAX_ARGS 10
#define MAX_LINE 1024

/* define schedule structure */

typedef struct sched_s {
    int level;			/* dump level */
    char destname[128];		/* file name */
} sched_t;

#define sched(dp)	((sched_t *) (dp)->up)

disklist_t *diskqp;

int result_argc;
char *result_argv[MAX_ARGS];
char datestamp[80];
char taper_program[80], reporter_program[80];
char host[MAX_HOSTNAME_LENGTH], *domain;

/* local functions */
int main P((int argc, char **argv));
char get_letter_from_user P((void));
int select_dir P((void));
void scan_holdingdisk P((char *diskdir,int verbose));
void pick_datestamp P((void));
int get_amanda_names P((char *fname, char *hostname, char *diskname,
			int *level));
void get_host_and_domain();


int is_dir(fname)
char *fname;
{
    struct stat statbuf;

    if(stat(fname, &statbuf) == -1) return 0;

    return (statbuf.st_mode & S_IFDIR) == S_IFDIR;
}

int is_emptyfile(fname)
char *fname;
{
    struct stat statbuf;

    if(stat(fname, &statbuf) == -1) return 0;

    return (statbuf.st_mode & S_IFDIR) != S_IFDIR && statbuf.st_size == 0;
}

int is_datestr(fname)
char *fname;
/* sanity check on datestamp of the form YYYYMMDD */
{
    char *cp;
    int num, date, year, month;

    /* must be 8 digits */
    for(cp = fname; *cp; cp++)
	if(!isdigit(*cp)) break;
    if(*cp != '\0' || cp-fname != 8) return 0;

    /* sanity check year, month, and day */

    num = atoi(fname);
    year = num / 10000;
    month = (num / 100) % 100;
    date = num % 100;
    if(year<1990 || year>2100 || month<1 || month>12 || date<1 || date>31)
	return 0;

    /* yes, we passed all the checks */

    return 1;
}


int non_empty(fname)
char *fname;
{
    DIR *dir;
    struct dirent *entry;
    int gotentry;

    if((dir = opendir(fname)) == NULL)
	return 0;

    gotentry = 0;
    while(!gotentry && (entry = readdir(dir)) != NULL)
	gotentry = strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..");

    closedir(dir);
    return gotentry;
}

#define MAX_DIRS 26	/* so we can select them A .. Z */

struct dirname *dir_list = NULL;
int ndirs = 0;

struct dirname *insert_dirname(name)
char *name;
{
    struct dirname *d, *p, *n;
    int cmp;

    for(p = NULL, d = dir_list; d != NULL; p = d, d = d->next)
	if((cmp = strcmp(name, d->name)) > 0) continue;
	else if(cmp == 0) return d;
	else break;

    if(ndirs == MAX_DIRS)
	return NULL;

    ndirs++;
    n = (struct dirname *)alloc(sizeof(struct dirname));
    n->name = stralloc(name);
    n->next = d;
    if(p) p->next = n;
    else dir_list = n;
    return n;
}

char get_letter_from_user()
{
    char ch;
    char line[MAX_LINE];

    fflush(stdout); fflush(stderr);
    if(fgets(line, MAX_LINE, stdin) == NULL) {
	printf("\nGot EOF.  Goodbye.\n");
	exit(1);
    }
    /* zap leading whitespace */
    if(sscanf(line, " %c", &ch) < 1) ch = '\0';	/* no char on line */

    if(islower(ch)) ch = toupper(ch);
    return ch;
}

int select_dir()
{
    int i;
    char ch;
    struct dirname *dir;

    while(1) {
	puts("\nMultiple Amanda directories, please pick one by letter:");
	for(dir = dir_list, i = 0; dir != NULL; dir = dir->next, i++)
	    printf("  %c. %s\n", 'A'+i, dir->name);
	printf("Select a directory to flush [A..%c]: ", 'A' + ndirs - 1);
	ch = get_letter_from_user();
	if(ch < 'A' || ch > 'A' + ndirs - 1)
	    printf("That is not a valid answer.  Try again, or ^C to quit.\n");
	else
	    return ch - 'A';
    }
}

void scan_holdingdisk(diskdir,verbose)
char *diskdir;
int verbose;
{
    DIR *topdir;
    struct dirent *workdir;

    if((topdir = opendir(diskdir)) == NULL) {
	printf("Warning: could not open holding dir %s: %s\n", diskdir, strerror(errno));
	return;
    }

    /* find all directories of the right format  */

    printf("Scanning %s...\n", diskdir);
    chdir(diskdir);
    while((workdir = readdir(topdir)) != NULL) {
	if(!strcmp(workdir->d_name, ".")
	   || !strcmp(workdir->d_name, "..")
	   || !strcmp(workdir->d_name, "lost+found"))
	    continue;

	if(verbose)
	    printf("  %s: ", workdir->d_name);
	if(!is_dir(workdir->d_name)) {
	    if(verbose)
	        puts("skipping cruft file, perhaps you should delete it.");
	}
	else if(!is_datestr(workdir->d_name)) {
	    if(verbose)
	        puts("skipping cruft directory, perhaps you should delete it.");
	}
	else if(rmdir(workdir->d_name) == 0) {
	    if(verbose)
	        puts("deleted empty Amanda directory.");
	}
	else {
	    if(insert_dirname(workdir->d_name) == NULL) {
	        if(verbose)
		    puts("too many non-empty Amanda dirs, can't handle this one.");
	    }
	    else {
	        if(verbose)
		    puts("found non-empty Amanda directory.");
	    }
	}
    }
    closedir(topdir);
}

void pick_datestamp()
{
    holdingdisk_t *hdisk;
    struct dirname *dir;
    int picked;

    for(hdisk = holdingdisks; hdisk != NULL; hdisk = hdisk->next)
	scan_holdingdisk(hdisk->diskdir,1);

    if(ndirs == 0) {
	puts("Could not find any Amanda directories to flush.");
	exit(1);
    }
    else if(ndirs > 1) picked = select_dir();
    else picked = 0;

    for(dir = dir_list; dir != NULL; dir = dir->next)
	if(picked-- == 0) break;

    strncpy(datestamp, dir->name, sizeof(datestamp)-1);
    datestamp[sizeof(datestamp)-1] = '\0';
}

int get_amanda_names(fname, hostname, diskname, level)
char *fname, *hostname, *diskname;
int *level;
{
    char buffer[TAPE_BLOCK_BYTES], datestamp[80];
    int fd;

    if((fd = open(fname, O_RDONLY)) == -1)
	return 1;

    if(read(fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
	close(fd);
	return 1;
    }

    if(sscanf(buffer, "AMANDA: FILE %s %s %s lev %d",
	    datestamp, hostname, diskname, level) != 4) {
	close(fd);
	return 1;
    }

    close(fd);
    return 0;
}
