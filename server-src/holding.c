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
 * $Id: holding.c,v 1.38 2002/02/03 15:51:40 martinea Exp $
 *
 * Functions to access holding disk
 */

#include "amanda.h"
#include "util.h"
#include "holding.h"
#include "fileheader.h"
#include "util.h"
#include "logfile.h"

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
	if(verbose && errno != ENOENT)
	   printf("Warning: could not open holding dir %s: %s\n",
		  diskdir, strerror(errno));
	return;
    }

    /* find all directories of the right format  */

    if(verbose)
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


void scan_holdingdir(holding_list, holdp, datestamp)
holding_t **holding_list;
holdingdisk_t *holdp;
char *datestamp;
{
    DIR *workdir;
    struct dirent *entry;
    char *dirname = NULL;
    char *destname = NULL;
    disk_t *dp;
    dumpfile_t file;
    holding_t *n, *last_holding_list;

    last_holding_list = *holding_list;
    if(last_holding_list) {
	while(last_holding_list->next !=NULL)
	    last_holding_list = last_holding_list->next;
    }

    dirname = vstralloc(holdp->diskdir, "/", datestamp, NULL);
    if((workdir = opendir(dirname)) == NULL) {
	if(errno != ENOENT)
	    log_add(L_INFO, "%s: could not open dir: %s",
		    dirname, strerror(errno));
	amfree(dirname);
	return;
    }
    chdir(dirname);
    while((entry = readdir(workdir)) != NULL) {
	if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
	    continue;

	if(is_emptyfile(entry->d_name))
	    continue;

	destname = newvstralloc(destname,
				dirname, "/", entry->d_name,
				NULL);
	get_dumpfile(destname, &file);
	if( file.type != F_DUMPFILE) {
	    if( file.type != F_CONT_DUMPFILE )
		log_add(L_INFO, "%s: ignoring cruft file.", entry->d_name);
	    continue;
	}

	dp = lookup_disk(file.name, file.disk);

	if (dp == NULL) {
	    log_add(L_INFO, "%s: disk %s:%s not in database, skipping it.",
		    entry->d_name, file.name, file.disk);
	    continue;
	}

	if(file.dumplevel < 0 || file.dumplevel > 9) {
	    log_add(L_INFO, "%s: ignoring file with bogus dump level %d.",
		    entry->d_name, file.dumplevel);
	    continue;
	}

	n = (holding_t *)alloc(sizeof(holding_t));
	n->name = stralloc(destname);
	n->next = NULL;
	if(last_holding_list) /* add at end */
	    last_holding_list->next = n;
	else /* first */
	    *holding_list = n;
	last_holding_list = n;
    }
}


void get_flush(dateargs, holding_list, datestamp, amflush, verbose)
char **dateargs;  /* NULL terminated array */
holding_t **holding_list;
char *datestamp;  /* don't do this date */
int amflush, verbose;
{
    holding_t *date_list;
    char **datearg;
    holding_t *date;
    holdingdisk_t *hdisk;
    char current_dir[1000];

    getcwd(current_dir, 999);

    if(dateargs) {
	holding_t *prev_date = NULL, *next_date;
	int ok;

	date_list = pick_all_datestamp(verbose);
	for(date = date_list; date != NULL;) {
	    next_date = date->next;
	    ok = 0;
	    for(datearg=dateargs; datearg != NULL && ok==0; datearg++) {
		ok = match_datestamp(*datearg, date->name);
	    }
	    if(ok == 0) { /* remove dir */
		amfree(date);
		if(prev_date)
		    prev_date->next = next_date;
		else
		    date_list = next_date;
	    }
	    else {
		prev_date = date;
	    }
	    date=next_date;
	}
    }
    else if (amflush) {
	date_list = pick_datestamp(verbose);
    }
    else {
	date_list = pick_all_datestamp(verbose);
    }

    for(date = date_list; date !=NULL; date = date->next) {
	if(!datestamp || strcmp(datestamp,date->name) != 0) {
	    for(hdisk = getconf_holdingdisks(); hdisk != NULL;
						hdisk = hdisk->next) {
		scan_holdingdir(holding_list, hdisk, date->name);
	    }
	}
    }

    chdir(current_dir);
}


holding_t *pick_all_datestamp(verbose)
int verbose;
{
    holdingdisk_t *hdisk;
    holding_t *holding_list = NULL;

    for(hdisk = getconf_holdingdisks(); hdisk != NULL; hdisk = hdisk->next)
	scan_holdingdisk(&holding_list, hdisk->diskdir, verbose);

    return holding_list;
}


holding_t *pick_datestamp(verbose)
int verbose;
{
    holding_t *holding_list;
    holding_t *dir, **directories;
    int i;
    int ndirs;
    char answer[1024];
    char max_char = '\0', *ch, chupper = '\0';

    holding_list = pick_all_datestamp(verbose);

    ndirs=0;
    for(dir = holding_list; dir != NULL;
	dir = dir->next) {
	ndirs++;
    }

    if(ndirs == 0) {
	return holding_list;
    }
    else if(ndirs == 1 || !verbose) {
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
	    fgets(answer, sizeof(answer), stdin);
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
    char buffer[DISK_BLOCK_BYTES];
    int fd;
    *hostname = *diskname = NULL;

    if((fd = open(fname, O_RDONLY)) == -1)
	return F_UNKNOWN;

    if(fullread(fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
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
    char buffer[DISK_BLOCK_BYTES];
    int fd;

    fh_init(file);
    file->type = F_UNKNOWN;
    if((fd = open(fname, O_RDONLY)) == -1)
	return;

    if(fullread(fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
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
    char buffer[DISK_BLOCK_BYTES];
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
	buflen = fullread(fd, buffer, sizeof(buffer));
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
    char buffer[DISK_BLOCK_BYTES];
    dumpfile_t file;
    char *filename;

    filename = stralloc(holding_file);
    while(filename != NULL && filename[0] != '\0') {
	if((fd = open(filename,O_RDONLY)) == -1) {
	    fprintf(stderr,"unlink_holding_files: open of %s failed: %s\n",filename,strerror(errno));
	    amfree(filename);
	    return 0;
	}
	buflen = fullread(fd, buffer, sizeof(buffer));
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
    char buffer[DISK_BLOCK_BYTES];
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
	buflen = fullread(fd, buffer, sizeof(buffer));
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
	    build_header(buffer, &file, sizeof(buffer));
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


void cleanup_holdingdisk(diskdir, verbose)
char *diskdir;
int verbose;
{
    DIR *topdir;
    struct dirent *workdir;

    if((topdir = opendir(diskdir)) == NULL) {
	if(verbose && errno != ENOENT)
	    printf("Warning: could not open holding dir %s: %s\n",
		   diskdir, strerror(errno));
	return;
   }

    /* find all directories of the right format  */

    if(verbose)
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
     }
     closedir(topdir);
}

