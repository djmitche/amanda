/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1996 University of Maryland at College Park
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
 * $Id: extract_list.c,v 1.33 1998/04/14 17:11:26 jrj Exp $
 *
 * implements the "extract" command in amrecover
 */

#include "amanda.h"
#include "version.h"
#include "amrecover.h"
#include "fileheader.h"
#include "dgram.h"
#ifdef SAMBA_CLIENT
#include "findpass.h"
#endif

#if defined(KRB4_SECURITY)
#include "krb4-security.h"
#endif

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


#define READ_TIMEOUT	30*60


int read_buffer(datafd, buffer, buflen)
int datafd;
char *buffer;
int buflen;
{
    int maxfd, nfound, size;
    fd_set readset, selectset;
    struct timeval timeout;
    char *dataptr;
    int spaceleft;
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
	    fprintf(stderr,"nfound == 0\n");
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
		fprintf(stderr,"EOF, check amidxtaped.debug file.\n");
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
    return(buflen-spaceleft);
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


void add_file(path)
char *path;
{
    DIR_ITEM *ditem, lditem;
    char *path_on_disk = NULL;
    char *path_on_disk_slash = NULL;
    char *cmd = NULL;
    char *err = NULL;
    int  i;
    char *dir, *dir_undo, dir_undo_ch;
    char *ditem_path = NULL;
    char *l;
    int  added;
    char *s, *fp;
    int ch;

    if (disk_path == NULL) {
	printf("Must select directory before adding files\n");
	return;
    }

    dbprintf(("add_file: Looking for \"%s\"\n", path));

    /* remove "/" at end of path */
    i = strlen(path)-1;
    if(path[i] == '/') path[i] = '\0';

    /* convert path (assumed in cwd) to one on disk */
    if (strcmp(disk_path, "/") == 0)
	path_on_disk = stralloc2("/", path);
    else
	path_on_disk = vstralloc(disk_path, "/", path, NULL);

    path_on_disk_slash = stralloc2(path_on_disk, "/");

    dbprintf(("add_file: Converted path=\"%s\" to path_on_disk=\"%s\"\n",
	      path, path_on_disk));

    for (ditem=get_dir_list(); ditem!=NULL; ditem=get_next_dir_item(ditem))
    {
	dbprintf(("add_file: Pondering ditem->path=\"%s\"\n", ditem->path));
	if (strcmp(ditem->path, path_on_disk) == 0 ||
	    strcmp(ditem->path, path_on_disk_slash) == 0)
	{
	    i = strlen(ditem->path);
	    if((i > 0 && ditem->path[i-1] == '/')
	       || (i > 1 && ditem->path[i-2] == '/' && ditem->path[i-1] == '.'))
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
		    printf("Added %s\n", path_on_disk);
		    dbprintf(("add_file: (Successful) Added %s\n",
			      path_on_disk));
		    break;
		case  1:
		    printf("File %s already added\n", path_on_disk);
		    dbprintf(("add_file: file %s already added\n",
			      path_on_disk));
		    break;
		}
	    }
	    amfree(cmd);
	    amfree(ditem_path);
	    amfree(path_on_disk);
	    amfree(path_on_disk_slash);
	    return;
	}
    }
    amfree(cmd);
    amfree(ditem_path);
    amfree(path_on_disk);
    amfree(path_on_disk_slash);

    printf("File %s doesn't exist in directory\n", path);
    dbprintf(("add_file: (Failed) File %s doesn't exist in directory\n",
	      path));
}



void delete_file(path)
char *path;
{
    DIR_ITEM *ditem, lditem;
    char *path_on_disk = NULL;
    char *path_on_disk_slash = NULL;
    char *cmd = NULL;
    char *err = NULL;
    int  i;
    char *date, *date_undo, date_undo_ch;
    char *tape, *tape_undo, tape_undo_ch;
    char *dir, *dir_undo, dir_undo_ch;
    int  level;
    char *ditem_path = NULL;
    char *l;
    int  deleted;
    char *s;
    int ch;

    if (disk_path == NULL) {
	printf("Must select directory before deleting files\n");
	return;
    }

    /* remove "/" at the end of the path */
    i = strlen(path)-1;
    if(path[i] == '/') path[i] = '\0';

    /* convert path (assumed in cwd) to one on disk */
    if (strcmp(disk_path, "/") == 0)
	path_on_disk = stralloc2("/", path);
    else
	path_on_disk = vstralloc(disk_path, "/", path, NULL);

    path_on_disk_slash = stralloc2(path_on_disk, "/");

    for (ditem=get_dir_list(); ditem!=NULL; ditem=get_next_dir_item(ditem))
    {
	if (strcmp(ditem->path, path_on_disk) == 0 ||
	    strcmp(ditem->path, path_on_disk_slash) == 0)
	{
	    if(ditem->path[strlen(ditem->path)-1]=='/' ||
	       strcmp(&(ditem->path[strlen(ditem->path)-2]),"/.")==0)
	    {	/* It is a directory */
		ditem_path = newstralloc(ditem_path, ditem->path);
		clean_pathname(ditem->path);

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
			   path_on_disk);
		}
	    }
	    else
	    {
		switch(delete_extract_item(ditem)) {
		case -1:
		    printf("System error\n");
		    break;
		case  0:
		    printf("Deleted %s\n", path_on_disk);    
		    break;
		case  1:
		    printf("Warning - file '%s' not on tape list\n",
			   path_on_disk);
		    break;
		}
	    }
	    amfree(cmd);
	    amfree(ditem_path);
	    amfree(path_on_disk);
	    amfree(path_on_disk_slash);
	    return;
	}
    }
    amfree(cmd);
    amfree(ditem_path);
    amfree(path_on_disk);
    amfree(path_on_disk_slash);
    printf("File %s doesn't exist in directory\n", path);
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
static int okay_to_continue P((void))
{
    int ch;
    int ret;

    printf("Continue? [Y/n]: ");
    fflush(stdout); fflush(stderr);
    while((ch = getchar()) != EOF && ch != '\n' && isspace(ch)) {}
    if (ch == '\n' || ch == 'Y' || ch == 'y') {
	ret = 1;
    } else {
	ret = 0;
    }
    while(ch != EOF && ch != '\n') ch = getchar();
    return ret;
}

static void send_to_tape_server(tss, cmd)
int tss;
char *cmd;
{
    int l, n, s;
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
    struct sockaddr_in tape_server;
    struct sockaddr_in myname;
    struct servent *sp;
    struct hostent *hp;
    int tape_server_socket;
    char *disk_regex = NULL;
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
    if ((hp = gethostbyname(tape_server_name)) == NULL)
    {
	printf("%s is an unknown host\n", tape_server_name);
	return -1;
    }
    memset((char *)&tape_server, 0, sizeof(tape_server));
    memcpy((char *)&tape_server.sin_addr, hp->h_addr, hp->h_length);
    tape_server.sin_family = hp->h_addrtype;
    tape_server.sin_port = sp->s_port;

    /* contact the tape server */
    if ((tape_server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
	perror("Error creating socket");
	exit(1);
    }
    if ((hp = gethostbyname(localhost)) == NULL)
    {
	(void)fprintf(stderr, "%s: %s is an unknown host\n",
		      get_pname(), localhost);
	dbprintf(("%s is an unknown host\n", localhost));
	dbclose();
	exit(1);
    }
    memset((char *)&myname, 0, sizeof(myname));
    memcpy((char *)&myname.sin_addr, hp->h_addr, hp->h_length);
    myname.sin_family = hp->h_addrtype;
    seteuid(0);					/* it either works ... */
    setegid(0);
    if (bind_reserved(tape_server_socket, &myname) != 0)
    {
	perror("amrecover: Error binding socket");
	exit(2);
    }
    setegid(getgid());
    seteuid(getuid());				/* put it back */
    if (connect(tape_server_socket, (struct sockaddr *)&tape_server,
		sizeof(struct sockaddr_in)) == -1)
    {
	perror("Error connecting to tape server");
	exit(2);
    }

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

    clean_datestamp = stralloc(dump_datestamp);
    for(ch=ch1=clean_datestamp;*ch1 != '\0';ch1++) {
	if(*ch1 != '-') {
	    *ch = *ch1;
	    ch++;
	}
    }
    *ch = '\0';
    /* send to the tape server what tape file we want */
    /* 7 args: "-h ", "-p", "-d", "datestamp", "tape device", "hostname", "diskname" */
    send_to_tape_server(tape_server_socket, "7");
    send_to_tape_server(tape_server_socket, "-h");
    send_to_tape_server(tape_server_socket, "-p");
    send_to_tape_server(tape_server_socket, "-d");
    send_to_tape_server(tape_server_socket, clean_datestamp);
    send_to_tape_server(tape_server_socket, dump_device_name);
    send_to_tape_server(tape_server_socket, dump_hostname);
    send_to_tape_server(tape_server_socket, disk_regex);

    dbprintf(("Started amidxtaped with arguments \"7 -h -p -d %s %s %s %s\"\n",
	      clean_datestamp, dump_device_name, dump_hostname, disk_regex));

    amfree(disk_regex);

    return tape_server_socket;
}


int read_file_header(buffer, file, buflen, tapedev)
char *buffer;
dumpfile_t *file;
int buflen;
int tapedev;
/*
 * Reads the first block of a tape file.
 */
{
    int bytes_read;
    bytes_read=read_buffer(tapedev,buffer,buflen);
    if(bytes_read < 0) {
	error("error reading tape: %s", strerror(errno));
    }
    else if(bytes_read < buflen) {
	fprintf(stderr, "%s: short block %d byte%s\n",
		get_pname(), bytes_read, (bytes_read == 1) ? "" : "s");
	print_header(stdout, file);
	error("Can't read file header");
    }
    else { /* bytes_read == buflen */
	parse_file_header(buffer, file, bytes_read);
    }
    return(bytes_read);
}

enum dumptypes {IS_UNKNOWN, IS_DUMP, IS_GNUTAR, IS_TAR, IS_SAMBA};

/* exec restore to do the actual restoration */
static void extract_files_child(in_fd, elist)
int in_fd;
EXTRACT_LIST *elist;
{
    int no_initial_params;
    int i,j=0;
    char **restore_args = NULL;
    int files_off_tape;
    EXTRACT_LIST_ITEM *fn;
    enum dumptypes dumptype = IS_UNKNOWN;
    char buffer[TAPE_BLOCK_BYTES];
    dumpfile_t file;
    int buflen;
    int len_program;
    char *cmd = NULL;
    char *domain = NULL, *smbpass = NULL;

    /* code executed by child to do extraction */
    /* never returns */

    /* make in_fd be our stdin */
    if (dup2(in_fd, STDIN_FILENO) == -1)
    {
	perror("extract_list - extract files client");
	exit(1);
    }

    /* read the file header */
    buflen=read_file_header(buffer, &file, sizeof(buffer), STDIN_FILENO);

    if(file.type != F_DUMPFILE) {
	print_header(stdout, &file);
	error("bad header");
    }

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
    if ((dumptype == IS_UNKNOWN) && (strcmp(file.program, SAMBA_CLIENT) == 0))
    	dumptype = IS_SAMBA;
#endif
    if (dumptype == IS_UNKNOWN)
    	dumptype = IS_DUMP;

    /* form the arguments to restore */
    switch (dumptype) {
    case IS_SAMBA:
#ifdef SAMBA_CLIENT
    	no_initial_params = 10;
    	break;
#endif
    case IS_TAR:
    case IS_GNUTAR:
        no_initial_params = 3;
        break;
    case IS_DUMP:
#ifdef AIX_BACKUP
        no_initial_params = 2;
#else
        no_initial_params = 4;
#endif
    	break;
    }

    files_off_tape = length_of_tape_list(elist);
    if ((restore_args
	 = (char **)malloc((no_initial_params + files_off_tape + 1)
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
    	    restore_args[j++] = smbpass;
    	    restore_args[j++] = stralloc("-U");
    	    restore_args[j++] = stralloc(SAMBA_USER);
    	    if (domain) {
            	restore_args[j++] = stralloc("-W");
    	    	restore_args[j++] = stralloc(domain);
   	    }	
    	}
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
    case IS_DUMP:
        restore_args[j++] = stralloc("restore");
#ifdef AIX_BACKUP
        restore_args[j++] = stralloc("-xB");
#else
        restore_args[j++] = stralloc("xbf");
        restore_args[j++] = stralloc("2");	/* read in units of 1K */
        restore_args[j++] = stralloc("-");	/* data on stdin */
#endif
    }
  
    for (i = 0, fn = elist->files; i < files_off_tape; i++, fn = fn->next)
    {
	switch (dumptype) {
    	case IS_TAR:
    	case IS_GNUTAR:
    	case IS_SAMBA:
	    restore_args[j+i] = stralloc2(".", fn->path);
	    break;
	case IS_DUMP:
	    restore_args[j+i] = stralloc(fn->path);
  	}
    }
    j+=i;
    restore_args[j] = NULL;
  
    switch (dumptype) {
    case IS_SAMBA:
#ifdef SAMBA_CLIENT
    	cmd = stralloc(SAMBA_CLIENT);
    	break;
#endif
    case IS_TAR:
    case IS_GNUTAR:
#ifndef GNUTAR
	fprintf(stderr, "warning: GNUTAR program not available.\n");
	cmd = "tar";
#else
  	cmd = stralloc(GNUTAR);
#endif
    	break;
    case IS_DUMP:
#ifndef RESTORE
	fprintf(stderr, "RESTORE program not available.\n");
	cmd = "restore";
#else
    	cmd = stralloc(RESTORE);
#endif
    }
    if (cmd) {
        dbprintf(("Exec'ing %s with arguments:\n", cmd));
	for (i = 0; i < no_initial_params + files_off_tape; i++)
  	    dbprintf(("\t%s\n", restore_args[i]));
        (void)execv(cmd, restore_args);
	/* only get here if exec failed */
	for (i = 0; i < no_initial_params + files_off_tape; i++) {
  	    amfree(restore_args[i]);
  	}
  	amfree(restore_args);
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
    if (!okay_to_continue())
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
	    if (!okay_to_continue())
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
	    exit(1);
	}

	/* finish up */
	delete_tape_list(elist);	/* tape done so delete from list */
    }
}
