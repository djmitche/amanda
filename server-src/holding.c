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
 * $Id: holding.c,v 1.19 1998/12/07 01:34:03 martinea Exp $
 *
 * Functions to access holding disk
 */

#include "amanda.h"
#include "holding.h"
#include "fileheader.h"

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

struct dirname *holding_list = NULL;
int ndirs = 0;

struct dirname *insert_dirname(name)
char *name;
{
    struct dirname *d, *p, *n;
    int cmp;

    for(p = NULL, d = holding_list; d != NULL; p = d, d = d->next)
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
    else holding_list = n;
    return n;
}

char get_letter_from_user()
{
    char r;
    int ch;

    fflush(stdout); fflush(stderr);
    while((ch = getchar()) != EOF && ch != '\n' && isspace(ch)) {}
    if(ch == '\n') {
	ch = '\0';
    } else if (ch != EOF) {
	r = ch;
	if(islower(r)) r = toupper(r);
	while((ch = getchar()) != EOF && ch != '\n') {}
    } else {
	printf("\nGot EOF.  Goodbye.\n");
	exit(1);
    }
    return r;
}

int select_dir()
{
    int i;
    char ch;
    struct dirname *dir;

    while(1) {
	puts("\nMultiple Amanda directories, please pick multiple by letter:");
	for(dir = holding_list, i = 0; dir != NULL && i < 26; dir = dir->next, i++)
	    printf("  %c. %s\n", 'A'+i, dir->name);
	printf("Select a directory to flush [A..%c]: [ALL] ", 'A' + i - 1);
	ch = get_letter_from_user();
	if(ch < 'A' || ch > 'A' + i - 1)
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
	printf("Warning: could not open holding dir %s: %s\n",
	       diskdir, strerror(errno));
	return;
    }

    /* find all directories of the right format  */

    printf("Scanning %s...\n", diskdir);
    chdir(diskdir);
    while((workdir = readdir(topdir)) != NULL) {
	if(strcmp(workdir->d_name, ".") == 0
	   || strcmp(workdir->d_name, "..") == 0
	   || strcmp(workdir->d_name, "lost+found") == 0)
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

char **pick_datestamp()
{
    holdingdisk_t *hdisk;
    struct dirname *dir;
    int i;
    char ** directories_names;
    struct dirname **directories;
    char answer[1024], *result;
    char max_char, *ch, chupper;

    for(hdisk = getconf_holdingdisks(); hdisk != NULL; hdisk = hdisk->next)
	scan_holdingdisk(hdisk->diskdir,1);

    directories_names = alloc((ndirs+1) * sizeof(char *));
    directories = alloc((ndirs) * sizeof(struct dirname *));
    for(dir = holding_list, i=0; dir != NULL; dir = dir->next,i++) {
	directories[i] = dir;
    }

    if(ndirs == 0) {
	directories_names[0] = NULL;
	puts("Could not find any Amanda directories to flush.");
	exit(1);
    }
    else if(ndirs == 1) {
	directories_names[0] = stralloc(holding_list->name);
	directories_names[1] = NULL;
    }
    else {
	while(1) {
	    puts("\nMultiple Amanda directories, please pick one by letter:");
	    for(dir = holding_list, i = 0; dir != NULL && i < 26; dir = dir->next, i++) {
		printf("  %c. %s\n", 'A'+i, dir->name);
		max_char = 'A'+i;
	    }
	    printf("Select directories to flush [A..%c]: [ALL] ", 'A' + i - 1);
	    result = fgets(answer, 1000, stdin);
	    if(strlen(answer) == 1 || !strncasecmp(answer,"ALL",3)) {
		for(dir = holding_list, i = 0; dir != NULL; dir = dir->next)
		    directories_names[i++] = stralloc(dir->name);
		directories_names[i] = NULL;
		break;
	    }
	    else {
		directories_names[0]   = NULL;
		i=0;
		for(ch = answer; *ch != '\0'; ch++) {
		    chupper = toupper(*ch);
		    if(chupper >= 'A' && chupper <= max_char) {
			directories_names[i++] = stralloc(directories[chupper-'A']->name);
			directories_names[i]   = NULL;
		    }
		    else if(chupper != ' ' && chupper != ',' && chupper != '\n') {
			i=0;
			printf("Invalid caracter: %c\n",*ch);
			break;
		    }
		}
		if(i>0)
		    break;
	    }

	}
    }

    return directories_names;
}

filetype_t get_amanda_names(fname, hostname, diskname, level)
char *fname, **hostname, **diskname;
int *level;
{
    dumpfile_t file;
    char buffer[TAPE_BLOCK_BYTES];
    int fd;
    *hostname = *diskname = NULL;

    if((fd = open(fname, O_RDONLY)) == -1)
	return F_UNKNOWN;

    if(read(fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
	aclose(fd);
	return F_UNKNOWN;
    }

    parse_file_header(buffer,&file,sizeof(buffer));
    if(file.type != F_DUMPFILE && file.type != F_CONT_DUMPFILE) {
	aclose(fd);
	return file.type;
    }
    *hostname = stralloc(file.name);
    *diskname = stralloc(file.disk);
    *level = file.dumplevel;

    aclose(fd);
    return file.type;
}


long size_holding_files(holding_file)
char *holding_file;
{
    int fd;
    int buflen;
    char buffer[TAPE_BLOCK_BYTES];
    dumpfile_t file;
    char *filename;
    long size=0;
    struct stat finfo;

    filename = stralloc(holding_file);
    while(filename != NULL && filename[0] != '\0') {
	if((fd = open(filename,O_RDONLY)) == -1) {
	    fprintf(stderr,"open of %s failed: %s\n",filename,strerror(errno));
	    amfree(filename);
	    return -1;
	}
	buflen=fill_buffer(fd, buffer, sizeof(buffer));
	parse_file_header(buffer, &file, buflen);
	close(fd);
	if(stat(filename, &finfo) == -1) {
	    printf("stat %s: %s\n", filename, strerror(errno));
	    finfo.st_size = 0;
	}
	size += finfo.st_size;
	filename = newstralloc(filename, file.cont_filename);
    }
    amfree(filename);
    return size;
}


int unlink_holding_files( holding_file )
char *holding_file;
{
    int fd;
    int buflen;
    char buffer[TAPE_BLOCK_BYTES];
    dumpfile_t file;
    char *filename;

    filename = stralloc(holding_file);
    while(filename != NULL && filename[0] != '\0') {
	if((fd = open(filename,O_RDONLY)) == -1) {
	    fprintf(stderr,"open of %s failed: %s\n",filename,strerror(errno));
	    amfree(filename);
	    return 0;
	}
	buflen=fill_buffer(fd, buffer, sizeof(buffer));
	parse_file_header(buffer, &file, buflen);
	close(fd);
	unlink(filename);
	filename = newstralloc(filename,file.cont_filename);
    }
    amfree(filename);
    return 1;
}
