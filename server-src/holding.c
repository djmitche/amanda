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
 * $Id: holding.c,v 1.31 2000/06/02 14:31:35 martinea Exp $
 *
 * Functions to access holding disk
 */

#include "amanda.h"
#include "util.h"
#include "holding.h"
#include "fileheader.h"

static holding_t *insert_dirname P((holding_t **, char *));
static void scan_holdingdisk P((holding_t **, char *, int));

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
    int ch, num, date, year, month;

    /* must be 8 digits */
    for(cp = fname; (ch = *cp) != '\0'; cp++) {
	if(!isdigit(ch)) {
	    break;
	}
    }
    if(ch != '\0' || cp-fname != 8) {
	return 0;
    }

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
    while(!gotentry && (entry = readdir(dir)) != NULL) {
	gotentry = !is_dot_or_dotdot(entry->d_name);
    }

    closedir(dir);
    return gotentry;
}

#define MAX_DIRS 26	/* so we can select them A .. Z */

static holding_t *insert_dirname(holding_list, name)
holding_t **holding_list;
char *name;
{
    holding_t *d, *p, *n;
    int cmp;

    for(p = NULL, d = *holding_list; d != NULL; p = d, d = d->next)
	if((cmp = strcmp(name, d->name)) > 0) continue;
	else if(cmp == 0) return d;
	else break;
    n = (holding_t *)alloc(sizeof(holding_t));
    n->name = stralloc(name);
    n->next = d;
    if(p) p->next = n;
    else *holding_list = n;
    return n;
}


void free_holding_list(holding_list)
holding_t *holding_list;
{
    holding_t *p, *n;
    p = holding_list;
    while(p != NULL) {
	n = p;
	p = p->next;
	amfree(n->name);
	amfree(n);
    }
}

static void scan_holdingdisk(holding_list, diskdir, verbose)
holding_t **holding_list;
char *diskdir;
int verbose;
{
    DIR *topdir;
    struct dirent *workdir;
    char *entryname = NULL;

    if((topdir = opendir(diskdir)) == NULL) {
	printf("Warning: could not open holding dir %s: %s\n",
	       diskdir, strerror(errno));
	return;
    }

    /* find all directories of the right format  */

    printf("Scanning %s...\n", diskdir);
    while((workdir = readdir(topdir)) != NULL) {
	if(is_dot_or_dotdot(workdir->d_name)) {
	    continue;
	}
	entryname = newvstralloc(entryname,
				 diskdir, "/", workdir->d_name, NULL);
	if(verbose) {
	    printf("  %s: ", workdir->d_name);
	}
	if(!is_dir(entryname)) {
	    if(verbose) {
	        puts("skipping cruft file, perhaps you should delete it.");
	    }
	} else if(!is_datestr(workdir->d_name)) {
	    if(verbose) {
	        puts("skipping cruft directory, perhaps you should delete it.");
	    }
	} else {
	    if(insert_dirname(holding_list, workdir->d_name) == NULL) {
	        if(verbose) {
		    puts("too many non-empty Amanda dirs, can't handle this one.");
		}
	    } else {
	        if(verbose) {
		    puts("found Amanda directory.");
		}
	    }
	}
    }
    closedir(topdir);
    amfree(entryname);
}

holding_t *pick_all_datestamp()
{
    holdingdisk_t *hdisk;
    holding_t *holding_list = NULL;

    for(hdisk = getconf_holdingdisks(); hdisk != NULL; hdisk = hdisk->next)
	scan_holdingdisk(&holding_list, hdisk->diskdir, 1);

    return holding_list;
}


holding_t *pick_datestamp()
{
    holding_t *holding_list;
    holding_t *dir, **directories;
    int i;
    int ndirs;
    char answer[1024], *result;
    char max_char = '\0', *ch, chupper = '\0';

    holding_list = pick_all_datestamp();

    ndirs=0;
    for(dir = holding_list; dir != NULL;
	dir = dir->next) {
	ndirs++;
    }

    if(ndirs == 0) {
	return holding_list;
    }
    else if(ndirs == 1) {
	return holding_list;
    }
    else {
	directories = alloc((ndirs) * sizeof(holding_t *));
	for(dir = holding_list, i=0; dir != NULL; dir = dir->next,i++) {
	    directories[i] = dir;
	}

	while(1) {
	    puts("\nMultiple Amanda directories, please pick one by letter:");
	    for(dir = holding_list, i = 0; dir != NULL && i < 26; dir = dir->next, i++) {
		printf("  %c. %s\n", 'A'+i, dir->name);
		max_char = 'A'+i;
	    }
	    printf("Select directories to flush [A..%c]: [ALL] ", 'A' + i - 1);
	    result = fgets(answer, sizeof(answer), stdin);
	    if(strlen(answer) == 1 || !strncasecmp(answer,"ALL",3)) {
		amfree(directories);
		return(holding_list);
	    }
	    else {
		i=1;
		for(ch = answer; *ch != '\0'; ch++) {
		    chupper = toupper(*ch);
		    if(!((chupper >= 'A' && chupper <= max_char) ||
			 chupper == ' ' || chupper != ',' || chupper != '\n'))
			i=0;
		}
		if(i==1) {
		    holding_t *r_holding_list, *hlist, *p;
		    r_holding_list = p = NULL;
		    for(ch = answer; *ch != '\0'; ch++) {
			chupper = toupper(*ch);
			if(chupper >= 'A' && chupper <= max_char) {
			    hlist = malloc(sizeof(holding_t));
			    hlist->next = NULL;
			    hlist->name = stralloc(directories[chupper-'A']->name);
			    if(p == NULL)
				r_holding_list = p = hlist;
			    else {
				p->next = hlist;
				p = hlist;
			    }
			}
		    }
		    amfree(directories);
		    free_holding_list(holding_list);
		    return(r_holding_list);
		}
	    }

	}
    }

    return NULL;
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
    aclose(fd);

    parse_file_header(buffer,&file,sizeof(buffer));
    if(file.type != F_DUMPFILE && file.type != F_CONT_DUMPFILE) {
	return file.type;
    }
    *hostname = stralloc(file.name);
    *diskname = stralloc(file.disk);
    *level = file.dumplevel;

    return file.type;
}


void get_dumpfile(fname, file)
char *fname;
dumpfile_t *file;
{
    char buffer[TAPE_BLOCK_BYTES];
    int fd;

    fh_init(file);
    file->type = F_UNKNOWN;
    if((fd = open(fname, O_RDONLY)) == -1)
	return;

    if(read(fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
	aclose(fd);
	return;
    }
    aclose(fd);

    parse_file_header(buffer,file,sizeof(buffer));
    return;
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
	    fprintf(stderr,"size_holding_files: open of %s failed: %s\n",filename,strerror(errno));
	    amfree(filename);
	    return -1;
	}
	buflen=fullread(fd, buffer, sizeof(buffer));
	parse_file_header(buffer, &file, buflen);
	close(fd);
	if(stat(filename, &finfo) == -1) {
	    printf("stat %s: %s\n", filename, strerror(errno));
	    finfo.st_size = 0;
	}
	size += (finfo.st_size+1023)/1024;
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
	    fprintf(stderr,"unlink_holding_files: open of %s failed: %s\n",filename,strerror(errno));
	    amfree(filename);
	    return 0;
	}
	buflen=fullread(fd, buffer, sizeof(buffer));
	parse_file_header(buffer, &file, buflen);
	close(fd);
	unlink(filename);
	filename = newstralloc(filename,file.cont_filename);
    }
    amfree(filename);
    return 1;
}


int rename_tmp_holding( holding_file, complete )
char *holding_file;
int complete;
{
    int fd;
    int buflen;
    char buffer[TAPE_BLOCK_BYTES];
    dumpfile_t file;
    char *filename;
    char *filename_tmp = NULL;

    filename = stralloc(holding_file);
    while(filename != NULL && filename[0] != '\0') {
	filename_tmp = newvstralloc(filename_tmp, filename, ".tmp", NULL);
	if((fd = open(filename_tmp,O_RDONLY)) == -1) {
	    fprintf(stderr,"rename_tmp_holding: open of %s failed: %s\n",filename_tmp,strerror(errno));
	    amfree(filename);
	    amfree(filename_tmp);
	    return 0;
	}
	buflen=fullread(fd, buffer, sizeof(buffer));
	if (buflen == 0) {
	    fprintf(stderr,"rename_tmp_holding: %s: empty file?\n", filename_tmp);
	    amfree(filename);
	    amfree(filename_tmp);
	    close(fd);
	    return 0;
	}
	parse_file_header(buffer, &file, buflen);
	close(fd);
	if(complete == 0 ) {
	    if((fd = open(filename_tmp,O_RDWR)) == -1) {
		fprintf(stderr, "rename_tmp_holdingX: open of %s failed: %s\n",
			filename_tmp, strerror(errno));
		amfree(filename);
		amfree(filename_tmp);
		return 0;

	    }
	    file.is_partial = 1;
	    write_header(buffer, &file, sizeof(buffer));
	    fullwrite(fd, buffer, sizeof(buffer));
	    close(fd);
	}
	if(rename(filename_tmp, filename) != 0) {
	    fprintf(stderr,
		    "rename_tmp_holding(): could not rename \"%s\" to \"%s\": %s",
		    filename_tmp, filename, strerror(errno));
	}
	filename = newstralloc(filename, file.cont_filename);
    }
    amfree(filename);
    amfree(filename_tmp);
    return 1;
}
