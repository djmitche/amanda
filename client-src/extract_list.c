/***************************************************************************
*
* File:          $RCSfile: extract_list.c,v $
*
* Revision:      $Revision: 1.2 $
* Last Edited:   $Date: 1997/03/19 11:41:08 $
* Author:        $Author: oliva $
*
* History:       $Log: extract_list.c,v $
* History:       Revision 1.2  1997/03/19 11:41:08  oliva
* History:       If no DUMP program is found, amcheck no longer produces error
* History:       messages.  If any filesystem is to be backed up with DUMP but
* History:       configure did not find a DUMP program, rundump will be invoked and it
* History:       will say that no DUMP program is available.
* History:
* History:       If no RESTORE program is found, amrecover will not invoke it.  An
* History:       error message will be printed instead.
* History:
* History:       Revision 1.1.1.1  1997/03/15 21:29:58  amcore
* History:       Mass import of 2.3.0.4 as-is.  We can remove generated files later.
* History:
* History:       Revision 1.15  1997/01/28 07:28:33  alan
* History:       forgot to malloc space for restore_args[3]
* History:
* History:       Revision 1.14  1997/01/24 08:40:53  alan
* History:       wrapped disk_name up into a regular expression that only matches it
* History:       exactly
* History:
* History:       Revision 1.13  1997/01/22 08:56:17  alan
* History:       added flags to restore so only reads blocks of size 1k
* History:
* History:       Revision 1.12  1996/12/31 08:56:32  alan
* History:       tidied up logging to debug file
* History:
* History:       Revision 1.11  1996/12/30 10:04:36  alan
* History:       Was getting dumps compressed/uncompressed wrong
* History:       Wan't piping data through uncompress as needed anyway, had pipe/socket
* History:       in wrong order.
* History:
* History:       Revision 1.10  1996/12/19 08:53:34  alan
* History:       first go at file extraction
* History:
* History:       Revision 1.9  1996/12/16 08:27:27  alan
* History:       first attempt at actual extraction
* History:
* History:       Revision 1.8  1996/11/08 09:57:36  alan
* History:       added clear command
* History:
* History:       Revision 1.7  1996/11/06 09:27:15  alan
* History:       a tape list is now deleted if the last item on it is deleted
* History:
* History:       Revision 1.6  1996/10/02 18:43:43  alan
* History:       synchronization with Blair's changes
* History:
* History:       Revision 1.5  1996/07/29 10:24:21  alan
* History:       due to problems on SunOS changed get_line() to strip off \r\n
* History:
* History:       Revision 1.4  1996/05/23 10:08:51  alan
* History:       made display_extract_list take a filename argument
* History:
* History:       Revision 1.3  1996/05/22 11:03:20  alan
* History:       added delete and show
* History:
* History:       Revision 1.2  1996/05/20 11:07:38  alan
* History:       added code to add file to extract list
* History:
* History:       Revision 1.1  1996/05/17 10:50:51  alan
* History:       Initial revision
* History:
*
***************************************************************************/

#include "amanda.h"
#include "amrecover.h"

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

/* global pid storage for interrupt handler */
pid_t extract_restore_child_pid = -1;
pid_t extract_compress_child_pid = -1;


static EXTRACT_LIST *extract_list = NULL;


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
static int add_extract_item(ditem)
DIR_ITEM *ditem;
{
    EXTRACT_LIST *this;
    EXTRACT_LIST_ITEM *that;

    for (this = extract_list; this != NULL; this = this->next)
    {
	/* see if this is the list for the tape */	
	if (strcmp(this->tape, ditem->tape) == 0)
	{
	    /* yes, so add to list */
	    if ((that = (EXTRACT_LIST_ITEM *)malloc(sizeof(EXTRACT_LIST_ITEM)))
		== NULL)
		return -1;
	    strcpy(that->path, ditem->path);
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
    strcpy(that->path, ditem->path);
    that->next = NULL;
    this->files = that;
    this->next = extract_list;
    extract_list = this;
    
    return 0;
}


/* returns -1 if error */
static int delete_extract_item(ditem)
DIR_ITEM *ditem;
{
    EXTRACT_LIST *this;
    EXTRACT_LIST_ITEM *that, *prev;

    for (this = extract_list; this != NULL; this = this->next)
    {
	/* see if this is the list for the tape */	
	if (strcmp(this->tape, ditem->tape) == 0)
	{
	    /* yes, so find file on list */
	    that = this->files;
	    if (strcmp(that->path, ditem->path) == 0)
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
		if (strcmp(that->path, ditem->path) == 0)
		{
		    prev->next = that->next;
		    free(that);
		    return 0;
		}
		prev = that;
		that = that->next;
	    }
	    printf("Warning - file '%s' not on tape list\n", ditem->path);
	    return 0;
	}
    }

    printf("Warning - no list for tape\n");
    return 0;
}


void add_file(path)
char *path;
{
    DIR_ITEM *ditem;
    char path_on_disk[1024];

    if (strlen(disk_path) == 0) 
    {
	printf("Must select directory before adding files\n");
	return;
    }
    
    dbprintf(("add_file: Looking for \"%s\"\n", path));

    /* convert path (assumed in cwd) to one on disk */
    if (strcmp(disk_path, "/") == 0)
	sprintf(path_on_disk, "/%s", path);
    else
	sprintf(path_on_disk, "%s/%s", disk_path, path);

    dbprintf(("add_file: Converted path=\"%s\" to path_on_disk=\"%s\"\n",
	      path, path_on_disk));

    for (ditem=get_dir_list(); ditem!=NULL; ditem=get_next_dir_item(ditem))
    {
	dbprintf(("add_file: Pondering ditem->path=\"%s\"\n", ditem->path));
	if (strcmp(ditem->path, path_on_disk) == 0)
	{
	    if (add_extract_item(ditem) == -1)
	    {
		printf("System error\n");
		dbprintf(("add_file: (Failed) System error\n"));
	    }
	    else
	    {
		printf("Added %s\n", path_on_disk);
		dbprintf(("add_file: (Successful) Added %s\n", path_on_disk));
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
    DIR_ITEM *ditem;
    char path_on_disk[1024];

    if (strlen(disk_path) == 0) 
    {
	printf("Must select directory before deleting files\n");
	return;
    }
    
    /* convert path (assumed in cwd) to one on disk */
    if (strcmp(disk_path, "/") == 0)
	sprintf(path_on_disk, "/%s", path);
    else
	sprintf(path_on_disk, "%s/%s", disk_path, path);

    for (ditem=get_dir_list(); ditem!=NULL; ditem=get_next_dir_item(ditem))
	if (strcmp(ditem->path, path_on_disk) == 0)
	{
	    if (delete_extract_item(ditem) == -1)
		printf("System error\n");
	    else
		printf("Deleted %s\n", path_on_disk);    
	    return;
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

    /* get tape server details */
    if ((sp = getservbyname("amidxtape", "tcp")) == NULL)
    {
	printf("amidxtape/tcp unknown protocol - config error?\n");
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
    /* 5 args: "-p", "-c", "tape device", "hostname", "diskname" */
    send_to_tape_server(tape_server_socket, "5");
    send_to_tape_server(tape_server_socket, "-p");
    send_to_tape_server(tape_server_socket, "-c");
    send_to_tape_server(tape_server_socket, tape_device_name);
    send_to_tape_server(tape_server_socket, dump_hostname);
    send_to_tape_server(tape_server_socket, disk_regex);
		
    dbprintf(("Started amidxtaped with arguments \"5 -p -c %s %s %s\"\n",
	      tape_device_name, dump_hostname, disk_regex));

    return tape_server_socket;
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

    /* code executed by child to do extraction */
    /* never returns */

    /* make in_fd be our stdin */
    if (dup2(in_fd, STDIN_FILENO) == -1)
    {
	perror("extract_list - extract files client");
	exit(1);
    }

    /* form the arguments to restore */
    no_initial_params = 4;
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
    strcpy(restore_args[0], "restore");
    if ((restore_args[1] = (char *)malloc(1024)) == NULL)
    {
	perror("Couldn't malloc restore_args[1]");
	exit(5);
    }
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
    for (i = 0, fn = elist->files; i < files_off_tape; i++, fn = fn->next)
    {
	if ((restore_args[no_initial_params+i]
	     = (char *)malloc(1024)) == NULL)
	{
	    perror("Couldn't malloc restore_args[no_initial_params+i]");
	    exit(7);
	}
	strcpy(restore_args[no_initial_params+i], fn->path);
    }

    restore_args[no_initial_params + files_off_tape] = NULL;
	    
#ifndef RESTORE
    fprintf(stderr, "RESTORE program not available.\n");
#else
    dbprintf(("Exec'ing %s with arguments:\n", RESTORE));
    for (i = 0; i < no_initial_params + files_off_tape; i++)
	dbprintf(("\t%s\n", restore_args[i]));

    /* then exec to it */
    (void)execv(RESTORE, restore_args);

    /* only get here if exec failed */
    perror("amrecover couldn't exec restore");
#endif
    exit(1);
    /*NOT REACHED */
}




/* does the actual extraction of files */
void extract_files P((void))
{
    EXTRACT_LIST *elist;
    pid_t pid;
    int child_stat;
    char buf[1024];
    char *l;
    int dumps_compressed;
    int pipe_fd[2];
    int tape_server_socket;

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

    printf("Extracting files using tape drive %s on host %s.\n",
	   tape_device_name, tape_server_name);
    printf("The following tapes are needed:");
    for (elist = first_tape_list(); elist != NULL; elist = next_tape_list(elist))
	printf(" %s", elist->tape);
    printf("\n");
    getcwd(buf, 1024);
    printf("Restoring files into directory %s\n", buf);
    if (!okay_to_continue())
	return;
    printf("\n");

    /* determine if dumps are compressed */
    sprintf(buf, "DCMP");
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
    dumps_compressed = (l[4] == 'Y')? 1: 0;

    while ((elist = first_tape_list()) != NULL)
    {
	printf("Load tape %s now\n", elist->tape);
	if (!okay_to_continue())
	    return;

	/* connect to the tape handler daemon on the tape drive server */
	if ((tape_server_socket = extract_files_setup()) == -1)
	{
	    fprintf(stderr, "amrecover - can't talk to tape server\n");
	    return;
	}
	
	/* okay, ready to extract. fork a child or 2 to do the actual work */
	if (dumps_compressed)
	{

	    dbprintf(("Dumps are compressed\n"));

	    /* need to pipe data through uncompress routine before
	       sending it to restore */
	    /* get a pipe for between processes */
	    if (pipe(pipe_fd) == -1)
	    {
		perror("extract_list - error getting pipe");
		exit(1);
	    }

	    /* start up uncompress command, as filter */
	    if ((pid = fork()) == 0)
	    {
		/* this is the child process */
		/* never gets out of this clause */
		(void)close(pipe_fd[0]); /* close read end of pipe */

		/* turn stdin into socket from tape server */
		if (dup2(tape_server_socket, STDIN_FILENO) == -1)
		{
		    perror("extract_list - uncompress client");
		    exit(1);
		}

		/* turn stdout into pipe write fd */
		if (dup2(pipe_fd[1], STDOUT_FILENO) == -1)
		{
		    perror("extract_list - uncompress client");
		    exit(1);
		}
		(void)execlp(UNCOMPRESS_PATH, UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
			     UNCOMPRESS_OPT,
#endif
			     (char *)0);
		/* only get here if exec failed */
		error("could not exec %s: %s", UNCOMPRESS_PATH,
		      strerror(errno));
		exit(1);
		/*NOT REACHED*/
	    }
	    /* this is the parent */
	    if (pid == -1)
	    {
		perror("extract_list - error forking child");
		exit(1);
	    }
	    /* store the child pid globally so that it can be killed on intr */
	    extract_compress_child_pid = pid;

	    /* now start up restore */
	    if ((pid = fork()) == 0)
	    {
		/* this is the child process */
		/* never gets out of this clause */
		(void)close(pipe_fd[1]);	/* close write end of pipe */
		extract_files_child(pipe_fd[0], elist);
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

	    (void)close(pipe_fd[0]);
	    (void)close(pipe_fd[1]);
	    (void)close(tape_server_socket);
	}
	else
	{
	    dbprintf(("Dumps are not compressed\n"));

	    /* only need 1 child, for restore */
	    extract_compress_child_pid = -1;

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
	}
	
	/* wait for the child processes to finish */
	while ((extract_restore_child_pid != -1)
	       && (extract_compress_child_pid != -1))
	{
	    if ((pid = waitpid(-1, &child_stat, 0)) == (pid_t)-1)
	    {
		perror("extract_list - error waiting for child");
		exit(1);
	    }
	    if (pid == extract_restore_child_pid)
		extract_restore_child_pid = -1;
	    else if (pid == extract_compress_child_pid)
		extract_compress_child_pid = -1;
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
	}

	/* finish up */
	delete_tape_list(elist);	/* tape done so delete from list */
    }
}
