/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998, 2000 University of Maryland at College Park
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
 * $Id: extract_list.c,v 1.43.2.13.4.6.2.5 2002/03/24 04:12:20 jrjackson Exp $
 *
 * implements the "extract" command in amrecover
 */

#include "amanda.h"
#include "version.h"
#include "amrecover.h"
#include "fileheader.h"
#include "dgram.h"
#include "stream.h"
#ifdef SAMBA_CLIENT
#include "findpass.h"
#endif

#if defined(KRB4_SECURITY)
#include "krb4-security.h"
#endif
#include "util.h"

typedef struct EXTRACT_LIST_ITEM
{
    char path[1024];

    struct EXTRACT_LIST_ITEM *next;
}
EXTRACT_LIST_ITEM;

typedef struct EXTRACT_LIST
{
    char date[11];			/* date tape created */
    int  level;				/* level of dump */
    char tape[256];			/* tape label */
    EXTRACT_LIST_ITEM *files;		/* files to get off tape */

    struct EXTRACT_LIST *next;
}
EXTRACT_LIST;

char *dump_device_name = NULL;

extern char *localhost;

/* global pid storage for interrupt handler */
pid_t extract_restore_child_pid = -1;


static EXTRACT_LIST *extract_list = NULL;

#ifdef SAMBA_CLIENT
unsigned short samba_extract_method = SAMBA_TAR;
#endif /* SAMBA_CLIENT */

#define READ_TIMEOUT	30*60

static int okay_to_continue P((int));

ssize_t read_buffer(datafd, buffer, buflen)
int datafd;
char *buffer;
size_t buflen;
{
    int maxfd, nfound = 0;
    ssize_t size = 0;
    fd_set readset, selectset;
    struct timeval timeout;
    char *dataptr;
    size_t spaceleft;
    int eof;

    if(datafd < 0 || datafd >= FD_SETSIZE) {
	errno = EMFILE;					/* out of range */
	return -1;
    }

    dataptr = buffer;
    spaceleft = buflen;

    maxfd = datafd + 1;
    eof = 0;

    FD_ZERO(&readset);
    FD_SET(datafd, &readset);

    do {

	timeout.tv_sec = READ_TIMEOUT;
	timeout.tv_usec = 0;
	memcpy(&selectset, &readset, sizeof(fd_set));

	nfound = select(maxfd, (SELECT_ARG_TYPE *)(&selectset), NULL, NULL, &timeout);

	/* check for errors or timeout */

	if(nfound == 0)  {
	    size=-2;
	    fprintf(stderr,"timeout waiting for amrestore\n");
	    fprintf(stderr,"increase READ_TIMEOUT in recover-src/extract_list.c if your tape is slow\n");
	}
	if(nfound == -1) {
	    size=-3;
	    fprintf(stderr,"nfound == -1\n");
	}

	/* read any data */

	if(FD_ISSET(datafd, &selectset)) {
	    size = read(datafd, dataptr, spaceleft);
	    switch(size) {
	    case -1:
		break;
	    case 0:
		spaceleft -= size;
		dataptr += size;
		fprintf(stderr,"EOF, check amidxtaped.debug file on %s.\n",
			       tape_server_name);
		break;
	    default:
		spaceleft -= size;
		dataptr += size;
		break;
	    }
	}
    } while (spaceleft>0 && size>0);

    if(size<0) {
	return -1;
    }
    return (ssize_t)(buflen-spaceleft);
}

EXTRACT_LIST *first_tape_list P((void))
{
    return extract_list;
}

EXTRACT_LIST *next_tape_list(list)
EXTRACT_LIST *list;
{
    if (list == NULL)
	return NULL;
    return list->next;
}

static void clear_tape_list(tape_list)
EXTRACT_LIST *tape_list;
{
    EXTRACT_LIST_ITEM *this, *next;

    this = tape_list->files;
    while (this != NULL)
    {
	next = this->next;
	free(this);
	this = next;
    }
    tape_list->files = NULL;
}


/* remove a tape list from the extract list, clearing the tape list
   beforehand if necessary */
void delete_tape_list(tape_list)
EXTRACT_LIST *tape_list;
{
    EXTRACT_LIST *this, *prev;

    /* is it first on the list? */
    if (tape_list == extract_list)
    {
	clear_tape_list(tape_list);
	extract_list = tape_list->next;
	amfree(tape_list);
	return;
    }

    /* so not first on list - find it and delete */
    prev = extract_list;
    this = extract_list->next;
    while (this != NULL)
    {
	if (this == tape_list)
	{
	    clear_tape_list(tape_list);
	    prev->next = tape_list->next;
	    amfree(tape_list);
	    return;
	}
	prev = this;
	this = this->next;
    }
    /*NOTREACHED*/
}


/* return the number of files on a tape's list */
int length_of_tape_list(tape_list)
EXTRACT_LIST *tape_list;
{
    EXTRACT_LIST_ITEM *fn;
    int n;

    n = 0;
    for (fn = tape_list->files; fn != NULL; fn = fn->next)
	n++;

    return n;
}


void clear_extract_list P((void))
{
    while (extract_list != NULL)
	delete_tape_list(extract_list);
}


/* returns -1 if error */
/* returns  0 on succes */
/* returns  1 if already added */
static int add_extract_item(ditem)
DIR_ITEM *ditem;
{
    EXTRACT_LIST *this, *this1;
    EXTRACT_LIST_ITEM *that, *curr;
    char *ditem_path = NULL;

    ditem_path = stralloc(ditem->path);
    clean_pathname(ditem_path);

    for (this = extract_list; this != NULL; this = this->next)
    {
	/* see if this is the list for the tape */	
	if (strcmp(this->tape, ditem->tape) == 0)
	{
	    /* yes, so add to list */
	    curr=this->files;
	    while(curr!=NULL)
	    {
		if (strcmp(curr->path,ditem_path) == 0) {
		    amfree(ditem_path);
		    return 1;
		}
		curr=curr->next;
	    }
	    if ((that = (EXTRACT_LIST_ITEM *)malloc(sizeof(EXTRACT_LIST_ITEM)))
		== NULL) {
		amfree(ditem_path);
		return -1;
	    }
	    strncpy(that->path, ditem_path, sizeof(that->path)-1);
	    that->path[sizeof(that->path)-1] = '\0';
	    that->next = this->files;
	    this->files = that;		/* add at front since easiest */
	    amfree(ditem_path);
	    return 0;
	}
    }

    /* so this is the first time we have seen this tape */
    if ((this = (EXTRACT_LIST *)malloc(sizeof(EXTRACT_LIST))) == NULL) {
	amfree(ditem_path);
	return -1;
    }
    strncpy(this->tape, ditem->tape, sizeof(this->tape)-1);
    this->tape[sizeof(this->tape)-1] ='\0';
    this->level = ditem->level;
    strncpy(this->date, ditem->date, sizeof(this->date)-1);
    this->date[sizeof(this->date)-1] = '\0';
    if ((that = (EXTRACT_LIST_ITEM *)malloc(sizeof(EXTRACT_LIST_ITEM)))
	== NULL) {
	amfree(ditem_path);
	return -1;
    }
    strncpy(that->path, ditem_path, sizeof(that->path)-1);
    that->path[sizeof(that->path)-1] = '\0';
    that->next = NULL;
    this->files = that;

    /* add this in date increasing order          */
    /* because restore must be done in this order */
    /* add at begining */
    if(extract_list==NULL || strcmp(this->date,extract_list->date) < 0) 
    {
	this->next = extract_list;
	extract_list = this;
	amfree(ditem_path);
	return 0;
    }
    for (this1 = extract_list; this1->next != NULL; this1 = this1->next)
    {
	/* add in the middle */
	if(strcmp(this->date,this1->next->date) < 0)
	{
	    this->next = this1->next;
	    this1->next = this;
	    amfree(ditem_path);
	    return 0;
	}
    }
    /* add at end */
    this->next = NULL;
    this1->next = this;
    amfree(ditem_path);
    return 0;
}


/* returns -1 if error */
/* returns  0 on deletion */
/* returns  1 if not there */
static int delete_extract_item(ditem)
DIR_ITEM *ditem;
{
    EXTRACT_LIST *this;
    EXTRACT_LIST_ITEM *that, *prev;
    char *ditem_path = NULL;

    ditem_path = stralloc(ditem->path);
    clean_pathname(ditem_path);

    for (this = extract_list; this != NULL; this = this->next)
    {
	/* see if this is the list for the tape */	
	if (strcmp(this->tape, ditem->tape) == 0)
	{
	    /* yes, so find file on list */
	    that = this->files;
	    if (strcmp(that->path, ditem_path) == 0)
	    {
		/* first on list */
		this->files = that->next;
		amfree(that);
		/* if list empty delete it */
		if (this->files == NULL)
		    delete_tape_list(this);
		amfree(ditem_path);
		return 0;
	    }
	    prev = that;
	    that = that->next;
	    while (that != NULL)
	    {
		if (strcmp(that->path, ditem_path) == 0)
		{
		    prev->next = that->next;
		    amfree(that);
		    amfree(ditem_path);
		    return 0;
		}
		prev = that;
		that = that->next;
	    }
	    amfree(ditem_path);
	    return 1;
	}
    }

    amfree(ditem_path);
    return 1;
}


void add_glob(glob)
char *glob;
{
    char *regex;
    char *regex_path;
    char *s;

    regex = glob_to_regex(glob);
    dbprintf(("add_glob (%s) -> %s\n", glob, regex));
    if ((s = validate_regexp(regex)) != NULL) {
	printf("\"%s\" is not a valid shell wildcard pattern: ", glob);
	puts(s);
	return;
    }
    /*
     * glob_to_regex() anchors the beginning of the pattern with ^,
     * but we will be tacking it onto the end of the current directory
     * in add_file, so strip that off.  Also, it anchors the end with
     * $, but we need to match an optional trailing /, so tack that on
     * the end.
     */
    regex_path = stralloc(regex + 1);
    amfree(regex);
    regex_path[strlen(regex_path) - 1] = '\0';
    strappend(regex_path, "[/]*$");
    add_file(glob, regex_path);
    amfree(regex_path);
}

void add_regex(regex)
char *regex;
{
    char *s;

    if ((s = validate_regexp(regex)) != NULL) {
	printf("\"%s\" is not a valid regular expression: ", regex);
	puts(s);
	return;
    }
    add_file(regex, regex);
}

void add_file(path, regex)
char *path;
char *regex;
{
    DIR_ITEM *ditem, lditem;
    char *path_on_disk = NULL;
    char *path_on_disk_slash = NULL;
    char *cmd = NULL;
    char *err = NULL;
    int i;
    size_t j;
    char *dir, *dir_undo, dir_undo_ch = '\0';
    char *ditem_path = NULL;
    char *l = NULL;
    int  added;
    char *s, *fp;
    int ch;
    int found_one;

    if (disk_path == NULL) {
	printf("Must select directory before adding files\n");
	return;
    }

    dbprintf(("add_file: Looking for \"%s\"\n", regex));

    /* remove "/" at end of path */
    j = strlen(regex)-1;
    while(j >= 0 && regex[j] == '/') regex[j--] = '\0';

    /* convert path (assumed in cwd) to one on disk */
    if (strcmp(disk_path, "/") == 0)
	path_on_disk = stralloc2("/", regex);
    else {
	char *clean_disk_path = clean_regex(disk_path);
	path_on_disk = vstralloc(clean_disk_path, "/", regex, NULL);
	amfree(clean_disk_path);
    }

    path_on_disk_slash = stralloc2(path_on_disk, "/");

    dbprintf(("add_file: Converted path=\"%s\" to path_on_disk=\"%s\"\n",
	      regex, path_on_disk));

    found_one = 0;
    for (ditem=get_dir_list(); ditem!=NULL; ditem=get_next_dir_item(ditem))
    {
	dbprintf(("add_file: Pondering ditem->path=\"%s\"\n", ditem->path));
	if (match(path_on_disk, ditem->path)
	    || match(path_on_disk_slash, ditem->path))
	{
	    found_one = 1;
	    j = strlen(ditem->path);
	    if((j > 0 && ditem->path[j-1] == '/')
	       || (j > 1 && ditem->path[j-2] == '/' && ditem->path[j-1] == '.'))
	    {	/* It is a directory */

		ditem_path = newstralloc(ditem_path, ditem->path);
		clean_pathname(ditem_path);

		cmd = stralloc2("ORLD ", ditem_path);
		if(send_command(cmd) == -1) {
		    amfree(cmd);
		    amfree(ditem_path);
		    amfree(path_on_disk);
		    amfree(path_on_disk_slash);
		    exit(1);
		}
		amfree(cmd);
		/* skip preamble */
		if ((i = get_reply_line()) == -1) {
		    amfree(ditem_path);
		    amfree(path_on_disk);
		    amfree(path_on_disk_slash);
		    exit(1);
		}
		if(i==0)		/* assume something wrong */
		{
		    amfree(ditem_path);
		    amfree(path_on_disk);
		    amfree(path_on_disk_slash);
		    l = reply_line();
		    printf("%s\n", l);
		    return;
		}
		amfree(err);
		dir_undo = NULL;
		added=0;
		strncpy(lditem.path, ditem_path, sizeof(lditem.path)-1);
		lditem.path[sizeof(lditem.path)-1] = '\0';
		/* skip the last line -- duplicate of the preamble */
		while ((i = get_reply_line()) != 0)
		{
		    if (i == -1) {
			amfree(ditem_path);
		        amfree(path_on_disk);
		        amfree(path_on_disk_slash);
			exit(1);
		    }
		    if(err) {
			if(cmd == NULL) {
			    if(dir_undo) *dir_undo = dir_undo_ch;
			    dir_undo = NULL;
			    cmd = stralloc(l);	/* save for error report */
			}
			continue;	/* throw the rest of the lines away */
		    }
		    l=reply_line();
		    if (!server_happy()) {
			puts(l);
			continue;
		    }
#define sc "201-"
		    if(strncmp(l, sc, sizeof(sc)-1) != 0) {
			err = "bad reply: not 201-";
			continue;
		    }

		    s = l + sizeof(sc)-1;
		    ch = *s++;
#undef sc
		    skip_whitespace(s, ch);
		    if(ch == '\0') {
			err = "bad reply: missing date field";
			continue;
		    }
		    copy_string(s, ch, lditem.date, sizeof(lditem.date), fp);
		    if(fp == NULL) {
			err = "bad reply: date field too large";
			continue;
		    }

		    skip_whitespace(s, ch);
		    if(ch == '\0' || sscanf(s - 1, "%d", &lditem.level) != 1) {
			err = "bad reply: cannot parse level field";
			continue;
		    }
		    skip_integer(s, ch);

		    skip_whitespace(s, ch);
		    if(ch == '\0') {
			err = "bad reply: missing tape field";
			continue;
		    }
		    copy_string(s, ch, lditem.tape, sizeof(lditem.tape), fp);
		    if(fp == NULL) {
			err = "bad reply: tape field too large";
			continue;
		    }

		    skip_whitespace(s, ch);
		    if(ch == '\0') {
			err = "bad reply: missing directory field";
			continue;
		    }
		    dir = s - 1;
		    skip_non_whitespace(s, ch);
		    dir_undo = s - 1;
		    dir_undo_ch = *dir_undo;
		    *dir_undo = '\0';

		    switch(add_extract_item(&lditem)) {
		    case -1:
			printf("System error\n");
			dbprintf(("add_file: (Failed) System error\n"));
			break;
		    case  0:
			printf("Added dir %s at date %s\n",
			       ditem_path, lditem.date);
			dbprintf(("add_file: (Successful) Added dir %s at date %s\n",
				  ditem_path,lditem.date));
			added=1;
			break;
		    case  1:
			break;
		    }
		}
		if(!server_happy()) {
		    puts(reply_line());
		} else if(err) {
		    if(*err) {
			puts(err);
		    }
		    puts(cmd);
		} else if(added == 0) {
		    printf("dir %s already added\n", ditem_path);
		    dbprintf(("add_file: dir %s already added\n", ditem_path));
		}
	    }
	    else /* It is a file */
	    {
		switch(add_extract_item(ditem)) {
		case -1:
		    printf("System error\n");
		    dbprintf(("add_file: (Failed) System error\n"));
		    break;
		case  0:
		    printf("Added %s\n", ditem->path);
		    dbprintf(("add_file: (Successful) Added %s\n",
			      ditem->path));
		    break;
		case  1:
		    printf("File %s already added\n", ditem->path);
		    dbprintf(("add_file: file %s already added\n",
			      ditem->path));
		    break;
		}
	    }
	}
    }
    amfree(cmd);
    amfree(ditem_path);
    amfree(path_on_disk);
    amfree(path_on_disk_slash);

    if(! found_one) {
	printf("File %s doesn't exist in directory\n", path);
	dbprintf(("add_file: (Failed) File %s doesn't exist in directory\n",
	          path));
    }
}


void delete_glob(glob)
char *glob;
{
    char *regex;
    char *regex_path;
    char *s;

    regex = glob_to_regex(glob);
    dbprintf(("delete_glob (%s) -> %s\n", glob, regex));
    if ((s = validate_regexp(regex)) != NULL) {
	printf("\"%s\" is not a valid shell wildcard pattern: ", glob);
	puts(s);
	return;
    }
    /*
     * glob_to_regex() anchors the beginning of the pattern with ^,
     * but we will be tacking it onto the end of the current directory
     * in add_file, so strip that off.  Also, it anchors the end with
     * $, but we need to match an optional trailing /, so tack that on
     * the end.
     */
    regex_path = stralloc(regex + 1);
    amfree(regex);
    regex_path[strlen(regex_path) - 1] = '\0';
    strappend(regex_path, "[/]*$");
    delete_file(glob, regex_path);
    amfree(regex_path);
}

void delete_regex(regex)
char *regex;
{
    char *s;

    if ((s = validate_regexp(regex)) != NULL) {
	printf("\"%s\" is not a valid regular expression: ", regex);
	puts(s);
	return;
    }
    delete_file(regex, regex);
}

void delete_file(path, regex)
char *path;
char *regex;
{
    DIR_ITEM *ditem, lditem;
    char *path_on_disk = NULL;
    char *path_on_disk_slash = NULL;
    char *cmd = NULL;
    char *err = NULL;
    int i;
    size_t j;
    char *date, *date_undo, date_undo_ch = '\0';
    char *tape, *tape_undo, tape_undo_ch = '\0';
    char *dir, *dir_undo, dir_undo_ch = '\0';
    int  level;
    char *ditem_path = NULL;
    char *l = NULL;
    int  deleted;
    char *s;
    int ch;
    int found_one;

    if (disk_path == NULL) {
	printf("Must select directory before deleting files\n");
	return;
    }

    dbprintf(("delete_file: Looking for \"%s\"\n", path));
    /* remove "/" at the end of the path */
    j = strlen(regex)-1;
    while(j >= 0 && regex[j] == '/') regex[j--] = '\0';

    /* convert path (assumed in cwd) to one on disk */
    if (strcmp(disk_path, "/") == 0)
	path_on_disk = stralloc2("/", regex);
    else {
	char *clean_disk_path = clean_regex(disk_path);
	path_on_disk = vstralloc(clean_disk_path, "/", regex, NULL);
	amfree(clean_disk_path);
    }

    path_on_disk_slash = stralloc2(path_on_disk, "/");

    dbprintf(("delete_file: Converted path=\"%s\" to path_on_disk=\"%s\"\n",
	      regex, path_on_disk));
    found_one = 0;
    for (ditem=get_dir_list(); ditem!=NULL; ditem=get_next_dir_item(ditem))
    {
	dbprintf(("delete_file: Pondering ditem->path=\"%s\"\n", ditem->path));
	if (match(path_on_disk, ditem->path)
	    || match(path_on_disk_slash, ditem->path))
	{
	    found_one = 1;
	    j = strlen(ditem->path);
	    if((j > 0 && ditem->path[j-1] == '/')
	       || (j > 1 && ditem->path[j-2] == '/' && ditem->path[j-1] == '.'))
	    {	/* It is a directory */
		ditem_path = newstralloc(ditem_path, ditem->path);
		clean_pathname(ditem_path);

		cmd = stralloc2("ORLD ", ditem_path);
		if(send_command(cmd) == -1) {
		    amfree(cmd);
		    amfree(ditem_path);
		    amfree(path_on_disk);
		    amfree(path_on_disk_slash);
		    exit(1);
		}
		amfree(cmd);
		/* skip preamble */
		if ((i = get_reply_line()) == -1) {
		    amfree(ditem_path);
		    amfree(path_on_disk);
		    amfree(path_on_disk_slash);
		    exit(1);
		}
		if(i==0)		/* assume something wrong */
		{
		    amfree(ditem_path);
		    amfree(path_on_disk);
		    amfree(path_on_disk_slash);
		    l = reply_line();
		    printf("%s\n", l);
		    return;
		}
		deleted=0;
		strncpy(lditem.path, ditem->path, sizeof(lditem.path)-1);
		lditem.path[sizeof(lditem.path)-1] = '\0';
		amfree(cmd);
		amfree(err);
		date_undo = tape_undo = dir_undo = NULL;
		/* skip the last line -- duplicate of the preamble */
		while ((i = get_reply_line()) != 0)
		{
		    if (i == -1) {
			amfree(ditem_path);
			amfree(path_on_disk);
			amfree(path_on_disk_slash);
			exit(1);
		    }
		    if(err) {
			if(cmd == NULL) {
			    if(tape_undo) *tape_undo = tape_undo_ch;
			    if(dir_undo) *dir_undo = dir_undo_ch;
			    date_undo = tape_undo = dir_undo = NULL;
			    cmd = stralloc(l);	/* save for the error report */
			}
			continue;	/* throw the rest of the lines away */
		    }
		    l=reply_line();
		    if (!server_happy()) {
			puts(l);
			continue;
		    }
#define sc "201-"
		    if(strncmp(l, sc, sizeof(sc)-1) != 0) {
			err = "bad reply: not 201-";
			continue;
		    }
		    s = l + sizeof(sc)-1;
		    ch = *s++;
#undef sc
		    skip_whitespace(s, ch);
		    if(ch == '\0') {
			err = "bad reply: missing date field";
			continue;
		    }
		    date = s - 1;
		    skip_non_whitespace(s, ch);
		    date_undo = s - 1;
		    date_undo_ch = *date_undo;
		    *date_undo = '\0';

		    skip_whitespace(s, ch);
		    if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
			err = "bad reply: cannot parse level field";
			continue;
		    }
		    skip_integer(s, ch);

		    skip_whitespace(s, ch);
		    if(ch == '\0') {
			err = "bad reply: missing tape field";
			continue;
		    }
		    tape = s - 1;
		    skip_non_whitespace(s, ch);
		    tape_undo = s - 1;
		    tape_undo_ch = *tape_undo;
		    *tape_undo = '\0';

		    skip_whitespace(s, ch);
		    if(ch == '\0') {
			err = "bad reply: missing directory field";
			continue;
		    }
		    dir = s - 1;
		    skip_non_whitespace(s, ch);
		    dir_undo = s - 1;
		    dir_undo_ch = *dir_undo;
		    *dir_undo = '\0';

		    strncpy(lditem.date, date, sizeof(lditem.date)-1);
		    lditem.date[sizeof(lditem.date)-1] = '\0';
		    lditem.level=level;
		    strncpy(lditem.tape, tape, sizeof(lditem.tape)-1);
		    lditem.tape[sizeof(lditem.tape)-1] = '\0';
		    switch(delete_extract_item(&lditem)) {
		    case -1:
			printf("System error\n");
			dbprintf(("delete_file: (Failed) System error\n"));
			break;
		    case  0:
			printf("Deleted dir %s at date %s\n", ditem_path, date);
			dbprintf(("delete_file: (Successful) Deleted dir %s at date %s\n",
				  ditem_path, date));
			deleted=1;
			break;
		    case  1:
			break;
		    }
		}
		if(!server_happy()) {
		    puts(reply_line());
		} else if(err) {
		    if(*err) {
			puts(err);
		    }
		    puts(cmd);
		} else if(deleted == 0) {
		    printf("Warning - dir '%s' not on tape list\n",
			   ditem_path);
		    dbprintf(("delete_file: dir '%s' not on tape list\n",
			      ditem_path));
		}
	    }
	    else
	    {
		switch(delete_extract_item(ditem)) {
		case -1:
		    printf("System error\n");
		    dbprintf(("delete_file: (Failed) System error\n"));
		    break;
		case  0:
		    printf("Deleted %s\n", ditem->path);
		    dbprintf(("delete_file: (Successful) Deleted %s\n",
			      ditem->path));
		    break;
		case  1:
		    printf("Warning - file '%s' not on tape list\n",
			   ditem->path);
		    dbprintf(("delete_file: file '%s' not on tape list\n",
			      ditem->path));
		    break;
		}
	    }
	}
    }
    amfree(cmd);
    amfree(ditem_path);
    amfree(path_on_disk);
    amfree(path_on_disk_slash);

    if(! found_one) {
	printf("File %s doesn't exist in directory\n", path);
	dbprintf(("delete_file: (Failed) File %s doesn't exist in directory\n",
	          path));
    }
}


/* print extract list into file. If NULL ptr passed print to screen */
void display_extract_list(file)
char *file;
{
    EXTRACT_LIST *this;
    EXTRACT_LIST_ITEM *that;
    FILE *fp;
    char *pager;
    char *pager_command;

    if (file == NULL)
    {
	if ((pager = getenv("PAGER")) == NULL)
	{
	    pager = "more";
	}
	/*
	 * Set up the pager command so if the pager is terminated, we do
	 * not get a SIGPIPE back.
	 */
	pager_command = stralloc2(pager, " ; /bin/cat > /dev/null");
	if ((fp = popen(pager_command, "w")) == NULL)
	{
	    printf("Warning - can't pipe through %s\n", pager);
	    fp = stdout;
	}
	amfree(pager_command);
    }
    else
    {
	if ((fp = fopen(file, "w")) == NULL)
	{
	    printf("Can't open file '%s' to print extract list into\n", file);
	    return;
	}
    }

    for (this = extract_list; this != NULL; this = this->next)
    {
	fprintf(fp, "TAPE %s LEVEL %d DATE %s\n",
		this->tape, this->level, this->date);
	for (that = this->files; that != NULL; that = that->next)
	    fprintf(fp, "\t%s\n", that->path);
    }

    if (file == NULL) {
	apclose(fp);
    } else {
	printf("Extract list written to file %s\n", file);
	afclose(fp);
    }
}


/* returns 0 if extract list empty and 1 if it isn't */
int is_extract_list_nonempty P((void))
{
    return (extract_list != NULL);
}


/* prints continue prompt and waits for response,
   returns 0 if don't, non-0 if do */
static int okay_to_continue(allow_tape)
    int allow_tape;
{
    int ch;
    int ret = -1;
    char *line = NULL;
    char *s;
    char *prompt;
    int get_tape;

    get_tape = 0;
    while (ret < 0) {
	if (get_tape) {
	    prompt = "New tape device [?]: ";
	} else if (allow_tape) {
	    prompt = "Continue [?/Y/n/t]? ";
	} else {
	    prompt = "Continue [?/Y/n]? ";
	}
	fputs(prompt, stdout);
	fflush(stdout); fflush(stderr);
	amfree(line);
	if ((line = agets(stdin)) == NULL) {
	    putchar('\n');
	    clearerr(stdin);
	    if (get_tape) {
		get_tape = 0;
		continue;
	    }
	    ret = 0;
	    break;
	}
	s = line;
	while ((ch = *s++) != '\0' && isspace(ch)) {}
	if (ch == '?') {
	    if (get_tape) {
		printf("Enter a new device ([host:]device) or \"default\"\n");
	    } else {
		printf("Enter \"y\"es to continue, \"n\"o to stop");
		if (allow_tape) {
		    printf(" or \"t\"ape to change tape drives");
		}
		putchar('\n');
	    }
	} else if (get_tape) {
	    set_tape(s - 1);
	    get_tape = 0;
	} else if (ch == '\0' || ch == 'Y' || ch == 'y') {
	    ret = 1;
	} else if (allow_tape && (ch == 'T' || ch == 't')) {
	    get_tape = 1;
	} else if (ch == 'N' || ch == 'n') {
	    ret = 0;
	}
    }
    amfree(line);
    return ret;
}

static void send_to_tape_server(tss, cmd)
int tss;
char *cmd;
{
    size_t l, n;
    ssize_t s;
    char *end;

    for (l = 0, n = strlen(cmd); l < n; l += s)
	if ((s = write(tss, cmd + l, n - l)) < 0)
	{
	    perror("Error writing to tape server");
	    exit(101);
	}
    end = "\r\n";
    for (l = 0, n = strlen(end); l < n; l += s)
	if ((s = write(tss, end + l, n - l)) < 0)
	{
	    perror("Error writing to tape server");
	    exit(101);
	}
}


/* start up connection to tape server and set commands to initiate
   transfer of dump image.
   Return tape server socket on success, -1 on error. */
static int extract_files_setup P((void))
{
    struct servent *sp;
    int my_port;
    int tape_server_socket;
    char *disk_regex = NULL;
    char *host_regex = NULL;
    char *service_name = NULL;
    char *line = NULL;
    char *clean_datestamp, *ch, *ch1;

    service_name = stralloc2("amidxtape", SERVICE_SUFFIX);

    /* get tape server details */
    if ((sp = getservbyname(service_name, "tcp")) == NULL)
    {
	printf("%s/tcp unknown protocol - config error?\n", service_name);
	amfree(service_name);
	return -1;
    }
    amfree(service_name);
    seteuid(0);					/* it either works ... */
    setegid(0);
    tape_server_socket = stream_client_privileged(tape_server_name,
						  ntohs(sp->s_port),
						  -1,
						  STREAM_BUFSIZE,
						  &my_port);
    if (tape_server_socket < 0)
    {
	printf("cannot connect to %s: %s\n", tape_server_name, strerror(errno));
	return -1;
    }
    if (my_port >= IPPORT_RESERVED) {
	aclose(tape_server_socket);
	printf("did not get a reserved port: %d\n", my_port);
	return -1;
    }
    setegid(getgid());
    seteuid(getuid());				/* put it back */

    /* do the security thing */
#if defined(KRB4_SECURITY)
#if 0 /* not yet implemented */
    if(krb4_auth)
    {
	line = get_krb_security();
    }
#endif /* 0 */
#endif
    {
	line = get_bsd_security();
    }
    send_to_tape_server(tape_server_socket, line);
    memset(line, '\0', strlen(line));
    amfree(line);

    disk_regex = alloc(strlen(disk_name) * 2 + 3);

    ch = disk_name;
    ch1 = disk_regex;

    /* we want to force amrestore to only match disk_name exactly */
    *(ch1++) = '^';

    /* We need to escape some characters first... NT compatibilty crap */
    for (; *ch != 0; ch++, ch1++) {
	switch (*ch) {     /* done this way in case there are more */
	case '$':
	    *(ch1++) = '\\';
	    /* no break; we do want to fall through... */
	default:
	    *ch1 = *ch;
	}
    }

    /* we want to force amrestore to only match disk_name exactly */
    *(ch1++) = '$';

    *ch1 = '\0';

    host_regex = alloc(strlen(dump_hostname) * 2 + 3);

    ch = dump_hostname;
    ch1 = host_regex;

    /* we want to force amrestore to only match disk_name exactly */
    *(ch1++) = '^';

    /* We need to escape some characters first... NT compatibilty crap */
    for (; *ch != 0; ch++, ch1++) {
	switch (*ch) {     /* done this way in case there are more */
	case '$':
	    *(ch1++) = '\\';
	    /* no break; we do want to fall through... */
	default:
	    *ch1 = *ch;
	}
    }

    /* we want to force amrestore to only match disk_name exactly */
    *(ch1++) = '$';

    *ch1 = '\0';

    clean_datestamp = stralloc(dump_datestamp);
    for(ch=ch1=clean_datestamp;*ch1 != '\0';ch1++) {
	if(*ch1 != '-') {
	    *ch = *ch1;
	    ch++;
	}
    }
    *ch = '\0';
    /* send to the tape server what tape file we want */
    /* 6 args:
     *   "-h"
     *   "-p"
     *   "tape device"
     *   "hostname"
     *   "diskname"
     *   "datestamp"
     */
    send_to_tape_server(tape_server_socket, "6");
    send_to_tape_server(tape_server_socket, "-h");
    send_to_tape_server(tape_server_socket, "-p");
    send_to_tape_server(tape_server_socket, dump_device_name);
    send_to_tape_server(tape_server_socket, host_regex);
    send_to_tape_server(tape_server_socket, disk_regex);
    send_to_tape_server(tape_server_socket, clean_datestamp);

    dbprintf(("Started amidxtaped with arguments \"6 -h -p %s %s %s %s\"\n",
	      dump_device_name, host_regex, disk_regex, clean_datestamp));

    amfree(disk_regex);
    amfree(host_regex);

    return tape_server_socket;
}


size_t read_file_header(buffer, file, buflen, tapedev)
char *buffer;
dumpfile_t *file;
size_t buflen;
int tapedev;
/*
 * Reads the first block of a tape file.
 */
{
    ssize_t bytes_read;
    bytes_read=read_buffer(tapedev,buffer,buflen);
    if(bytes_read < 0) {
	error("error reading tape: %s", strerror(errno));
    }
    else if((size_t)bytes_read < buflen) {
	fprintf(stderr, "%s: short block %d byte%s\n",
		get_pname(), (int)bytes_read, (bytes_read == 1) ? "" : "s");
	print_header(stdout, file);
	error("Can't read file header");
    }
    else { /* bytes_read == buflen */
	parse_file_header(buffer, file, bytes_read);
    }
    return((size_t)bytes_read);
}

enum dumptypes {IS_UNKNOWN, IS_DUMP, IS_GNUTAR, IS_TAR, IS_SAMBA};

/* exec restore to do the actual restoration */
static void extract_files_child(in_fd, elist)
    int in_fd;
    EXTRACT_LIST *elist;
{
    int save_errno;
    int extra_params = 0;
    int i,j=0;
    char **restore_args = NULL;
    int files_off_tape;
    EXTRACT_LIST_ITEM *fn;
    enum dumptypes dumptype = IS_UNKNOWN;
    char buffer[DISK_BLOCK_BYTES];
    dumpfile_t file;
    size_t buflen;
    size_t len_program;
    char *cmd = NULL;
    int passwd_field = -1;
#ifdef SAMBA_CLIENT
    char *domain = NULL, *smbpass = NULL;
#endif

    /* code executed by child to do extraction */
    /* never returns */

    /* make in_fd be our stdin */
    if (dup2(in_fd, STDIN_FILENO) == -1)
    {
	perror("extract_list - extract files client");
	exit(1);
    }

    /* read the file header */
    fh_init(&file);
    buflen=read_file_header(buffer, &file, sizeof(buffer), STDIN_FILENO);

    if(buflen == 0 || file.type != F_DUMPFILE) {
	print_header(stdout, &file);
	error("bad header");
    }

    if (file.program != NULL) {
#ifdef GNUTAR
	if (strcmp(file.program, GNUTAR) == 0)
	    dumptype = IS_GNUTAR;
#endif

	if (dumptype == IS_UNKNOWN) {
	    len_program = strlen(file.program);
	    if(len_program >= 3 &&
	       strcmp(&file.program[len_program-3],"tar") == 0)
		dumptype = IS_TAR;
	}

#ifdef SAMBA_CLIENT
	if (dumptype == IS_UNKNOWN && strcmp(file.program, SAMBA_CLIENT) ==0) {
	    if (samba_extract_method == SAMBA_TAR)
	      dumptype = IS_TAR;
	    else
	      dumptype = IS_SAMBA;
	}
#endif
    }

    /* form the arguments to restore */
    files_off_tape = length_of_tape_list(elist);
    switch (dumptype) {
    case IS_SAMBA:
#ifdef SAMBA_CLIENT
    	extra_params = 10;
    	break;
#endif
    case IS_TAR:
    case IS_GNUTAR:
        extra_params = 3;
        break;
    case IS_UNKNOWN:
    case IS_DUMP:
#ifdef AIX_BACKUP
        extra_params = 2;
#else
#if defined(XFSDUMP)
	if (strcmp(file.program, XFSDUMP) == 0) {
            extra_params = 4 + files_off_tape;
	} else
#endif
	{
        extra_params = 4;
	}
#endif
    	break;
    }

    if ((restore_args
	 = (char **)malloc((extra_params + files_off_tape + 1)
			   * sizeof(char *)))
	== NULL) 
    {
  	perror("Couldn't malloc restore_args");
  	exit(3);
    }
    switch(dumptype) {
    case IS_SAMBA:
#ifdef SAMBA_CLIENT
    	restore_args[j++] = stralloc("smbclient");
    	smbpass = findpass(file.disk, &domain);
    	if (smbpass) {
            restore_args[j++] = stralloc(file.disk);
	    passwd_field=j;
    	    restore_args[j++] = stralloc("-U");
    	    restore_args[j++] = smbpass;
    	    if (domain) {
            	restore_args[j++] = stralloc("-W");
    	    	restore_args[j++] = stralloc(domain);
   	    } else
		extra_params -= 2;
    	} else
	    extra_params -= 6;
    	restore_args[j++] = stralloc("-d0");
    	restore_args[j++] = stralloc("-Tx");
	restore_args[j++] = stralloc("-");	/* data on stdin */
    	break;
#endif
    case IS_TAR:
    case IS_GNUTAR:
    	restore_args[j++] = stralloc("tar");
	restore_args[j++] = stralloc("-xpGvf");
	restore_args[j++] = stralloc("-");	/* data on stdin */
	break;
    case IS_UNKNOWN:
    case IS_DUMP:
        restore_args[j++] = stralloc("restore");
#ifdef AIX_BACKUP
        restore_args[j++] = stralloc("-xB");
#else
#if defined(XFSDUMP)
	if (strcmp(file.program, XFSDUMP) == 0) {
            restore_args[j++] = stralloc("-v");
            restore_args[j++] = stralloc("silent");
	} else
#endif
#if defined(VDUMP)
	if (strcmp(file.program, VDUMP) == 0) {
            restore_args[j++] = stralloc("xf");
            restore_args[j++] = stralloc("-");	/* data on stdin */
	} else
#endif
	{
        restore_args[j++] = stralloc("xbf");
        restore_args[j++] = stralloc("2");	/* read in units of 1K */
        restore_args[j++] = stralloc("-");	/* data on stdin */
	}
#endif
    }
  
    for (i = 0, fn = elist->files; i < files_off_tape; i++, fn = fn->next)
    {
	switch (dumptype) {
    	case IS_TAR:
    	case IS_GNUTAR:
    	case IS_SAMBA:
	    restore_args[j++] = stralloc2(".", fn->path);
	    break;
	case IS_UNKNOWN:
	case IS_DUMP:
#if defined(XFSDUMP)
	    if (strcmp(file.program, XFSDUMP) == 0) {
		/*
		 * xfsrestore needs a -s option before each file to be
		 * restored, and also wants them to be relative paths.
		 */
		restore_args[j++] = stralloc("-s");
		restore_args[j++] = stralloc(fn->path + 1);
	    } else
#endif
	    {
	    restore_args[j++] = stralloc(fn->path);
	    }
  	}
    }
#if defined(XFSDUMP)
    if (strcmp(file.program, XFSDUMP) == 0) {
	restore_args[j++] = stralloc("-");
	restore_args[j++] = stralloc(".");
    }
#endif
    restore_args[j] = NULL;

    switch (dumptype) {
    case IS_SAMBA:
#ifdef SAMBA_CLIENT
    	cmd = stralloc(SAMBA_CLIENT);
    	break;
#else
	/* fall through to ... */
#endif
    case IS_TAR:
    case IS_GNUTAR:
#ifndef GNUTAR
	fprintf(stderr, "warning: GNUTAR program not available.\n");
	cmd = stralloc("tar");
#else
  	cmd = stralloc(GNUTAR);
#endif
    	break;
    case IS_UNKNOWN:
    case IS_DUMP:
	cmd = NULL;
#if defined(DUMP)
	if (strcmp(file.program, DUMP) == 0) {
    	    cmd = stralloc(RESTORE);
	}
#endif
#if defined(VDUMP)
	if (strcmp(file.program, VDUMP) == 0) {
    	    cmd = stralloc(VRESTORE);
	}
#endif
#if defined(VXDUMP)
	if (strcmp(file.program, VXDUMP) == 0) {
    	    cmd = stralloc(VXRESTORE);
	}
#endif
#if defined(XFSDUMP)
	if (strcmp(file.program, XFSDUMP) == 0) {
    	    cmd = stralloc(XFSRESTORE);
	}
#endif
	if (cmd == NULL) {
	    fprintf(stderr, "warning: restore program for %s not available.\n",
		    file.program);
	    cmd = stralloc("restore");
	}
    }
    if (cmd) {
        dbprintf(("Exec'ing %s with arguments:\n", cmd));
	for (i = 0; i < j; i++) {
	    if( i == passwd_field)
		dbprintf(("\tXXXXX\n"));
	    else
  	        dbprintf(("\t%s\n", restore_args[i]));
	}
        (void)execv(cmd, restore_args);
	/* only get here if exec failed */
	save_errno = errno;
	for (i = 0; i < j; i++) {
  	    amfree(restore_args[i]);
  	}
  	amfree(restore_args);
	errno = save_errno;
        perror("amrecover couldn't exec");
        fprintf(stderr, " problem executing %s\n", cmd);
	amfree(cmd);
    }
    exit(1);
    /*NOT REACHED */
}


/* does the actual extraction of files */
/* The original design had the dump image being returned exactly as it
   appears on the tape, and this routine getting from the index server
   whether or not it is compressed, on the assumption that the tape
   server may not know how to uncompress it. But
   - Amrestore can't do that. It returns either compressed or uncompressed
   (always). Amrestore assumes it can uncompress files. It is thus a good
   idea to run the tape server on a machine with gzip.
   - The information about compression in the disklist is really only
   for future dumps. It is possible to change compression on a drive
   so the information in the disklist may not necessarily relate to
   the dump image on the tape.
     Consequently the design was changed to assuming that amrestore can
   uncompress any dump image and have it return an uncompressed file
   always. */
void extract_files P((void))
{
    EXTRACT_LIST *elist;
    pid_t pid;
    amwait_t child_stat;
    char buf[STR_SIZE];
    char *l;
    int tape_server_socket;
    int first;

    if (!is_extract_list_nonempty())
    {
	printf("Extract list empty - No files to extract!\n");
	return;
    }

    /* get tape device name from index server if none specified */
    if (tape_server_name == NULL) {
	tape_server_name = newstralloc(tape_server_name, server_name);
    }
    if (tape_device_name == NULL) {
	if (send_command("TAPE") == -1)
	    exit(1);
	if (get_reply_line() == -1)
	    exit(1);
	l = reply_line();
	if (!server_happy())
	{
	    printf("%s\n", l);
	    exit(1);
	}
	/* skip reply number */
	tape_device_name = newstralloc(tape_device_name, l+4);
    }

    if (strcmp(tape_device_name, "/dev/null") == 0)
    {
	printf("amrecover: warning: using %s as the tape device will not work\n",
	       tape_device_name);
    }

    first=1;
    for (elist = first_tape_list(); elist != NULL; elist = next_tape_list(elist))
	if(elist->tape[0]!='/') {
	    if(first) {
		printf("\nExtracting files using tape drive %s on host %s.\n",
			tape_device_name, tape_server_name);
		printf("The following tapes are needed:");
		first=0;
	    }
	    else
		printf("                               ");
	    printf(" %s\n", elist->tape);
	}
    first=1;
    for (elist = first_tape_list(); elist != NULL; elist = next_tape_list(elist))
	if(elist->tape[0]=='/') {
	    if(first) {
		printf("\nExtracting files from holding disk on host %s.\n",
			tape_server_name);
		printf("The following files are needed:");
		first=0;
	    }
	    else
		printf("                               ");
	    printf(" %s\n", elist->tape);
	}
    printf("\n");
    getcwd(buf, sizeof(buf));
    printf("Restoring files into directory %s\n", buf);
#ifdef SAMBA_CLIENT
    if (samba_extract_method == SAMBA_SMBCLIENT)
      printf("(unless it is a Samba backup, that will go through to the SMB server)\n");
#endif
    if (!okay_to_continue(0))
	return;
    printf("\n");

    while ((elist = first_tape_list()) != NULL)
    {
	if(elist->tape[0]=='/') {
	    dump_device_name = newstralloc(dump_device_name, elist->tape);
	    printf("Extracting from file %s\n",dump_device_name);
	}
	else {
	    dump_device_name = newstralloc(dump_device_name, tape_device_name);
	    printf("Load tape %s now\n", elist->tape);
	    if (!okay_to_continue(1))
	        return;
	}
	dump_datestamp = newstralloc(dump_datestamp, elist->date);

	/* connect to the tape handler daemon on the tape drive server */
	if ((tape_server_socket = extract_files_setup()) == -1)
	{
	    fprintf(stderr, "amrecover - can't talk to tape server\n");
	    return;
	}

	/* okay, ready to extract. fork a child to do the actual work */
	if ((pid = fork()) == 0)
	{
	    /* this is the child process */
	    /* never gets out of this clause */
	    extract_files_child(tape_server_socket, elist);
	    /*NOT REACHED*/
	}
	/* this is the parent */
	if (pid == -1)
	{
	    perror("extract_list - error forking child");
	    exit(1);
	}

	/* store the child pid globally so that it can be killed on intr */
	extract_restore_child_pid = pid;

	aclose(tape_server_socket);

	/* wait for the child process to finish */
	if ((pid = waitpid(-1, &child_stat, 0)) == (pid_t)-1)
	{
	    perror("extract_list - error waiting for child");
	    exit(1);
	}
	if (pid == extract_restore_child_pid)
	{
	    extract_restore_child_pid = -1;
	}
	else
	{
	    fprintf(stderr, "extract list - unknown child terminated?\n");
	    exit(1);
	}
	if ((WIFEXITED(child_stat) != 0) && (WEXITSTATUS(child_stat) != 0))
	{
	    fprintf(stderr,
		    "extract_list - child returned non-zero status: %d\n",
		    WEXITSTATUS(child_stat));
	    if (!okay_to_continue(0))
		return;
	}

	/* finish up */
	delete_tape_list(elist);	/* tape done so delete from list */
    }
}
