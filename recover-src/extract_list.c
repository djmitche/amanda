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
 * $Id: extract_list.c,v 1.10 1997/12/09 07:11:28 amcore Exp $
 *
 * implements the "extract" command in amrecover
 */

#include "amanda.h"
#include "version.h"
#include "amrecover.h"
#include "fileheader.h"
#include "tapeio.h"

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

char dump_device_name[LINE_LENGTH];

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
	    /*strcpy(errstr,"data timeout");*/
		fprintf(stderr,"nfound == 0\n");
	}
	if(nfound == -1) {
	    size=-3;
	    /*sprintf(errstr,  "select: %s", strerror(errno));*/
		fprintf(stderr,"nfound == -1\n");
	}

	/* read any data */

	if(FD_ISSET(datafd, &selectset)) {
	    size = read(datafd, dataptr, spaceleft);
	    switch(size) {
	    case -1:
		/*sprintf(errstr, "data read: %s", strerror(errno));*/
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
	free(tape_list);
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
	    free(tape_list);
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
    char ditem_path[1024];

    strcpy(ditem_path,ditem->path);
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
		if (strcmp(curr->path,ditem_path) == 0)
		    return 1;
		curr=curr->next;
	    }
	    if ((that = (EXTRACT_LIST_ITEM *)malloc(sizeof(EXTRACT_LIST_ITEM)))
		== NULL)
		return -1;
	    strcpy(that->path, ditem_path);
	    that->next = this->files;
	    this->files = that;		/* add at front since easiest */
	    return 0;
	}
    }

    /* so this is the first time we have seen this tape */
    if ((this = (EXTRACT_LIST *)malloc(sizeof(EXTRACT_LIST))) == NULL)
	return -1;
    strcpy(this->tape, ditem->tape);
    this->level = ditem->level;
    strcpy(this->date, ditem->date);
    if ((that = (EXTRACT_LIST_ITEM *)malloc(sizeof(EXTRACT_LIST_ITEM)))
	== NULL)
	return -1;
    strcpy(that->path, ditem_path);
    that->next = NULL;
    this->files = that;

    /* add this in date increasing order          */
    /* because restore must be done in this order */
    /* add at begining */
    if(extract_list==NULL || strcmp(this->date,extract_list->date) < 0) 
    {
	this->next = extract_list;
	extract_list = this;
	return 0;
    }
    for (this1 = extract_list; this1->next != NULL; this1 = this1->next)
    {
	/* add in the middle */
	if(strcmp(this->date,this1->next->date) < 0)
	{
	    this->next = this1->next;
	    this1->next = this;
	    return 0;
	}
    }
    /* add at end */
    this->next = NULL;
    this1->next = this;
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
    char ditem_path[1024];

    strcpy(ditem_path,ditem->path);
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
		free(that);
		/* if list empty delete it */
		if (this->files == NULL)
		    delete_tape_list(this);
		return 0;
	    }
	    prev = that;
	    that = that->next;
	    while (that != NULL)
	    {
		if (strcmp(that->path, ditem_path) == 0)
		{
		    prev->next = that->next;
		    free(that);
		    return 0;
		}
		prev = that;
		that = that->next;
	    }
	    return 1;
	}
    }

    return 1;
}


void add_file(path)
char *path;
{
    DIR_ITEM *ditem, lditem;
    char path_on_disk[1024];
    char path_on_disk_slash[1024];
    char cmd[1024];
    int  i;
    /*char date[1024],tape[1024];*/
    char dir[1024];
    /*int  level;*/
    char *l;
    char ditem_path[1024];
    int  added;

    if (strlen(disk_path) == 0) 
    {
	printf("Must select directory before adding files\n");
	return;
    }
    
    dbprintf(("add_file: Looking for \"%s\"\n", path));

    /* remove "/" at end of path */
    if(path[strlen(path)-1]=='/')
	path[strlen(path)-1]='\0';

    /* convert path (assumed in cwd) to one on disk */
    if (strcmp(disk_path, "/") == 0)
	sprintf(path_on_disk, "/%s", path);
    else
	sprintf(path_on_disk, "%s/%s", disk_path, path);

    strcpy(path_on_disk_slash,path_on_disk);
    strcat(path_on_disk_slash,"/");

    dbprintf(("add_file: Converted path=\"%s\" to path_on_disk=\"%s\"\n",
	      path, path_on_disk));

    for (ditem=get_dir_list(); ditem!=NULL; ditem=get_next_dir_item(ditem))
    {
	dbprintf(("add_file: Pondering ditem->path=\"%s\"\n", ditem->path));
	if (strcmp(ditem->path, path_on_disk) == 0 ||
	    strcmp(ditem->path, path_on_disk_slash) == 0)
	{
	    if(ditem->path[strlen(ditem->path)-1]=='/' ||
	       strcmp(&(ditem->path[strlen(ditem->path)-2]),"/.")==0)
	    {	/* It is a directory */
		
		strcpy(ditem_path,ditem->path);
		clean_pathname(ditem_path);

		sprintf(cmd, "ORLD %s",ditem_path);
		if(send_command(cmd) == -1)
		    exit(1);
		/* skip preamble */
		if ((i = get_reply_line()) == -1)
		    exit(1);
		if(i==0)		/* assume something wrong */
		{
		    l = reply_line();
		    printf("%s\n", l);
		    return;
		}
		added=0;
		strcpy(lditem.path, ditem->path);
		/* miss last line too */
		while ((i = get_reply_line()) != 0)
		{
		    if (i == -1)
			exit(1);
		    l=reply_line();
		    if (!server_happy())
			return;
		    if(strncmp(l,"201- ",5)==0) {
			sscanf(l+5,"%s %d %s %s",lditem.date,&lditem.level,
				lditem.tape,dir);
			switch(add_extract_item(&lditem)) {
			case -1:
			    printf("System error\n");
			    dbprintf(("add_file: (Failed) System error\n"));
			    break;
			case  0:
			    printf("Added dir %s at date %s\n", ditem_path, lditem.date);
			    dbprintf(("add_file: (Successful) Added dir %s at date %s\n",ditem_path,lditem.date));
			    added=1;
			    break;
			case  1:
			    break;
			}
		    }
		    else
			printf("ERROR:not '201- ': %s\n",l);
		}
		if(added==0) {
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
		        dbprintf(("add_file: (Successful) Added %s\n",path_on_disk));
			break;
		    case  1:
		        printf("File %s already added\n", path_on_disk);
		        dbprintf(("add_file: file %s already added\n",path_on_disk));
			break;
		}
	    }
	    return;
	}
    }

    printf("File %s doesn't exist in directory\n", path);
    dbprintf(("add_file: (Failed) File %s doesn't exist in directory\n",
	      path));
}



void delete_file(path)
char *path;
{
    DIR_ITEM *ditem, lditem;
    char path_on_disk[1024];
    char path_on_disk_slash[1024];
    char cmd[1024];
    int  i;
    char date[1024],tape[1024],dir[1024];
    int  level;
    char *l;
    char ditem_path[1024];
    int  deleted;

    if (strlen(disk_path) == 0) 
    {
	printf("Must select directory before deleting files\n");
	return;
    }

    /* remove "/" at the end of the path */
    if(path[strlen(path)-1]=='/')
        path[strlen(path)-1]='\0';
    
    /* convert path (assumed in cwd) to one on disk */
    if (strcmp(disk_path, "/") == 0)
	sprintf(path_on_disk, "/%s", path);
    else
	sprintf(path_on_disk, "%s/%s", disk_path, path);

    strcpy(path_on_disk_slash,path_on_disk);
    strcat(path_on_disk_slash,"/");

    for (ditem=get_dir_list(); ditem!=NULL; ditem=get_next_dir_item(ditem))
    {
	if (strcmp(ditem->path, path_on_disk) == 0 ||
	    strcmp(ditem->path, path_on_disk_slash) == 0)
	{
	    if(ditem->path[strlen(ditem->path)-1]=='/' ||
	       strcmp(&(ditem->path[strlen(ditem->path)-2]),"/.")==0)
	    {	/* It is a directory */
		strcpy(ditem_path,ditem->path);
		clean_pathname(ditem_path);

		sprintf(cmd, "ORLD %s",ditem_path);
		if(send_command(cmd) == -1)
		    exit(1);
		/* skip preamble */
		if ((i = get_reply_line()) == -1)
		    exit(1);
		if(i==0)		/* assume something wrong */
		{
		    l = reply_line();
		    printf("%s\n", l);
		    return;
		}
		deleted=0;
		strcpy(lditem.path, ditem->path);
		/* miss last line too */
		while ((i = get_reply_line()) != 0)
		{
		    if (i == -1)
			exit(1);
		    l=reply_line();
		    if (!server_happy())
			return;
		    if(strncmp(l,"201- ",5)==0) {
			sscanf(l+5,"%s %d %s %s",date,&level,tape,dir);
			strcpy(lditem.date,date);
			lditem.level=level;
			strcpy(lditem.tape, tape);
			switch(delete_extract_item(&lditem)) {
			case -1:
			    printf("System error\n");
			    dbprintf(("delete_file: (Failed) System error\n"));
			    break;
			case  0:
			    printf("Deleted dir %s at date %s\n", ditem_path, date);
			    dbprintf(("delete_file: (Successful) Deleted dir %s at date %s\n", ditem_path, date));
			    deleted=1;
			    break;
			case  1:
			    break;
			}
		    }
		    else
			printf("ERROR:not '201- ': %s\n",l);
		}
		if(deleted==0) {
		    printf("Warning - dir '%s' not on tape list\n",path_on_disk);
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
		    printf("Warning - file '%s' not on tape list\n",path_on_disk);
		    break;
		}
	    }
	    return;
	}
    }
    printf("File %s doesn't exist in directory\n", path);
}


/* print extract list into file. If NULL ptr passed print to screen */
void display_extract_list(file)
char *file;
{
    EXTRACT_LIST *this;
    EXTRACT_LIST_ITEM *that;
    FILE *fp;

    if (file == NULL)
    {
	if ((fp = popen("more", "w")) == NULL)
	{
	    printf("Warning - can't pipe through more\n");
	    fp = stdout;
	}
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

    if (file == NULL)
	pclose(fp);
    else
    {
	printf("Extract list written to file %s\n", file);
	fclose(fp);
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
    char ans[32];

    printf("Continue? [Y/n]: ");
    fflush(stdout);
    (void)gets(ans);
    if ((strlen(ans) == 0) || (ans[0] == 'Y') || (ans[0] == 'y'))
	return 1;
    return 0;
}

static void send_to_tape_server(tss, cmd)
int tss;
char *cmd;
{
    int l, n, s;
    
    for (l = 0, n = strlen(cmd); l < n; l += s)
	if ((s = write(tss, cmd + l, n - l)) == -1)
	{
	    perror("Error writing to tape server");
	    exit(101);
	}
    if (write(tss, "\r", 1) != 1)
    {
	perror("Error writing to tape server");
	exit(102);
    }
    if (write(tss, "\n", 1) != 1)
    {
	perror("Error writing to tape server");
	exit(103);
    }
}


/* start up connection to tape server and set commands to initiate
   transfer of dump image.
   Return tape server socket on success, -1 on error. */
static int extract_files_setup P((void))
{
    struct sockaddr_in tape_server;
    struct servent *sp;
    struct hostent *hp;
    int tape_server_socket;
    char disk_regex[256];
    char service_name[1024];

    sprintf(service_name, "amidxtape%s", SERVICE_SUFFIX);

    /* get tape server details */
    if ((sp = getservbyname(service_name, "tcp")) == NULL)
    {
	printf("%s/tcp unknown protocol - config error?\n", service_name);
	return -1;
    }
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
    if (connect(tape_server_socket, (struct sockaddr *)&tape_server,
		sizeof(struct sockaddr_in)) == -1)
    {
	perror("Error connecting to tape server");
	exit(2);
    }


    /* we want to force amrestore to only match disk_name exactly */
    sprintf(disk_regex, "^%s$", disk_name);

    /* send to the tape server what tape file we want */
    /* 5 args: "-h ", "-p", "tape device", "hostname", "diskname" */
    send_to_tape_server(tape_server_socket, "5");
    send_to_tape_server(tape_server_socket, "-h");
    send_to_tape_server(tape_server_socket, "-p");
    send_to_tape_server(tape_server_socket, dump_device_name);
    send_to_tape_server(tape_server_socket, dump_hostname);
    send_to_tape_server(tape_server_socket, disk_regex);
		
    dbprintf(("Started amidxtaped with arguments \"5 -h -p %s %s %s\"\n",
	      dump_device_name, dump_hostname, disk_regex));

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
	fprintf(stderr, "%s: short block %d bytes\n", pname, bytes_read);
	print_header(stdout, file);
	error("Can't read file header");
    }
    else { /* bytes_read == buflen */
	parse_file_header(buffer, file, bytes_read);
    }
    return(bytes_read);
}


/* exec restore to do the actual restoration */
static int extract_files_child(in_fd, elist)
int in_fd;
EXTRACT_LIST *elist;
{
    int no_initial_params;
    int i;
    char **restore_args;
    int files_off_tape;
    EXTRACT_LIST_ITEM *fn;
    int istar;
    char buffer[TAPE_BLOCK_BYTES];
    dumpfile_t file;
    int buflen;
    int len_program;

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

    istar = !strcmp(file.program, GNUTAR);
    if (!istar) {
	len_program=strlen(file.program);
	if(len_program<3)
	    istar=0;
	else
	    istar=!strcmp(&file.program[len_program-3],"tar");
    }
    
    /* form the arguments to restore */
    if(istar)
        no_initial_params = 3;
    else
#ifdef AIX_BACKUP
        no_initial_params = 2;
#else
        no_initial_params = 4;
#endif
    files_off_tape = length_of_tape_list(elist);
    if ((restore_args
	 = (char **)malloc((no_initial_params + files_off_tape + 1)
			   * sizeof(char *)))
	== NULL) 
    {
	perror("Couldn't malloc restore_args");
	exit(3);
    }
    if ((restore_args[0] = (char *)malloc(1024)) == NULL)
    {
	perror("Couldn't malloc restore_args[0]");
	exit(4);
    }
    if(istar)
	strcpy(restore_args[0], "tar");
    else
        strcpy(restore_args[0], "restore");
    if ((restore_args[1] = (char *)malloc(1024)) == NULL)
    {
	perror("Couldn't malloc restore_args[1]");
	exit(5);
    }
    if(istar) {
	strcpy(restore_args[1], "-xpGvf");
        if ((restore_args[2] = (char *)malloc(1024)) == NULL)
        {
	    perror("Couldn't malloc restore_args[2]");
	    exit(6);
        }
        strcpy(restore_args[2], "-");	/* data on stdin */
    }
    else {
#ifdef AIX_BACKUP
        strcpy(restore_args[1], "-xB");
#else
        strcpy(restore_args[1], "xbf");
        if ((restore_args[2] = (char *)malloc(1024)) == NULL)
        {
	    perror("Couldn't malloc restore_args[2]");
	    exit(6);
        }
        strcpy(restore_args[2], "2");	/* read in units of 1K */
        if ((restore_args[3] = (char *)malloc(1024)) == NULL)
        {
	    perror("Couldn't malloc restore_args[3]");
	    exit(61);
        }
        strcpy(restore_args[3], "-");	/* data on stdin */
#endif
    }

    for (i = 0, fn = elist->files; i < files_off_tape; i++, fn = fn->next)
    {
	if ((restore_args[no_initial_params+i]
	     = (char *)malloc(1024)) == NULL)
	{
	    perror("Couldn't malloc restore_args[no_initial_params+i]");
	    exit(7);
	}
	if(istar) {
	    restore_args[no_initial_params+i][0]='.';
	    strcpy(&(restore_args[no_initial_params+i][1]), fn->path);
	}
	else
	    strcpy(restore_args[no_initial_params+i], fn->path);
    }

    restore_args[no_initial_params + files_off_tape] = NULL;

    if(istar) {
	char cmd[1024];
#ifndef GNUTAR
        fprintf(stderr, "GNUTAR program not available.\n");
#else
	sprintf(cmd, "%s/runtar%s", libexecdir, versionsuffix());
	/* strcpy(cmd,GNUTAR); */
        dbprintf(("Exec'ing %s with arguments:\n", cmd));
        for (i = 0; i < no_initial_params + files_off_tape; i++)
	    dbprintf(("\t%s\n", restore_args[i]));
        (void)execv(cmd, restore_args);
        /* only get here if exec failed */
        perror("amrecover couldn't exec tar");
#endif
    }
    else {
#ifndef RESTORE
        fprintf(stderr, "RESTORE program not available.\n");
#else
        dbprintf(("Exec'ing %s with arguments:\n", RESTORE));
        for (i = 0; i < no_initial_params + files_off_tape; i++)
	    dbprintf(("\t%s\n", restore_args[i]));
        (void)execv(RESTORE, restore_args);
        /* only get here if exec failed */
        perror("amrecover couldn't exec restore");
#endif
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
    int child_stat;
    char buf[1024];
    char *l;
    int tape_server_socket;
    int first;

    if (!is_extract_list_nonempty())
    {
	printf("Extract list empty - No files to extract!\n");
	return;
    }

    /* get tape device name from index server if none specified */
    if (strlen(tape_server_name) == 0)
    {
	strcpy(tape_server_name, server_name);
	sprintf(buf, "TAPE");
	if (send_command(buf) == -1)
	    exit(1);
	if (get_reply_line() == -1)
	    exit(1);
	l = reply_line();
	if (!server_happy())
	{
	    printf("%s\n", l);
	    exit(1);
	}
	strcpy(tape_device_name, l+4);	/* skip reply number */
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
    getcwd(buf, 1024);
    printf("Restoring files into directory %s\n", buf);
    if (!okay_to_continue())
	return;
    printf("\n");

    while ((elist = first_tape_list()) != NULL)
    {
	if(elist->tape[0]=='/') {
	    strcpy(dump_device_name,elist->tape);
	    printf("Extracting from file %s\n",dump_device_name);
	}
	else {
	    strcpy(dump_device_name,tape_device_name);
	    printf("Load tape %s now\n", elist->tape);
	    if (!okay_to_continue())
	        return;
	}

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

	(void)close(tape_server_socket);
	
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
