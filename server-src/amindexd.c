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
 * $Id: amindexd.c,v 1.10 1997/12/16 01:27:00 amcore Exp $
 *
 * This is the server daemon part of the index client/server system.
 * It is assummed that this is launched from inetd instead of being
 * started as a daemon because it is not often used
 */

#include "amanda.h"
#include "conffile.h"
#include "diskfile.h"
#include "arglist.h"
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#ifdef HAVE_NETINET_IP_H
#include <netinet/ip.h>
#endif
#include <grp.h>
#include "dgram.h"
#include "version.h"
#include "protocol.h"
#include "amindexd.h"
#include "version.h"
#include "amindex.h"

typedef struct REMOVE_ITEM
{
    char filename[1024];
    struct REMOVE_ITEM *next;
}
REMOVE_ITEM;

static REMOVE_ITEM *to_remove = NULL; /* file to remove at end */

char *pname = "amindexd";
char *server_version = "1.1";

/* state */
char local_hostname[MAX_HOSTNAME_LENGTH];/* me! */
char remote_hostname[LONG_LINE];	/* the client */
char dump_hostname[LONG_LINE];		/* the machine we are restoring */
char disk_name[LONG_LINE];		/* the disk we are restoring */
char config[LONG_LINE];			/* the config we are restoring */
char date[LONG_LINE];

void remove_uncompressed_file()
{
    REMOVE_ITEM *remove_file;

    for(remove_file=to_remove;remove_file!=NULL;remove_file=remove_file->next)
    {
	unlink(remove_file->filename);
	dbprintf(("Removing index file: %s\n",remove_file->filename));
    }
}

/* send a 1 line reply to the client */
arglist_function1(void reply, int, n, char *, fmt)
{
    va_list args;
    char buf[LONG_LINE];

    arglist_start(args, fmt);
    (void)sprintf(buf, "%03d ", n);
    (void)vsprintf(buf+4, fmt, args);
    arglist_end(args);

    if (printf("%s\r\n", buf) <= 0)
    {
	dbprintf(("! error %d in printf\n", errno));
	remove_uncompressed_file();
	exit(1);
    }
    if (fflush(stdout) != 0)
    {
	dbprintf(("! error in fflush %d\n", errno));
	remove_uncompressed_file();
	exit(1);
    }
    dbprintf(("< %s\n", buf));
}


/* send one line of a multi-line response */
arglist_function1(void lreply, int, n, char *, fmt)
{
    va_list args;
    char buf[LONG_LINE];

    arglist_start(args, fmt);
    (void)sprintf(buf, "%03d-", n);
    (void)vsprintf(buf+4, fmt, args);
    arglist_end(args);

    if (printf("%s\r\n", buf) <= 0)
    {
	dbprintf(("! error %d in printf\n", errno));
	remove_uncompressed_file();
	exit(1);
    }
    if (fflush(stdout) != 0)
    {
	dbprintf(("! error in fflush %d\n", errno));
	remove_uncompressed_file();
	exit(1);
    }

    dbprintf(("< %s\n", buf));
}


/* send one line of a multi-line response */
arglist_function1(void fast_lreply, int, n, char *, fmt)
{
    va_list args;
    char buf[LONG_LINE];

    arglist_start(args, fmt);
    (void)sprintf(buf, "%03d-", n);
    (void)vsprintf(buf+4, fmt, args);
    arglist_end(args);

    if (printf("%s\r\n", buf) <= 0)
    {
	dbprintf(("! error %d in printf\n", errno));
	remove_uncompressed_file();
	exit(1);
    }
}


int uncompress_file(filename_gz, filename)
char *filename_gz;
char *filename;
{
    char cmd[2048];
    struct stat stat_filename;
    int result;

    strcpy(filename,filename_gz);
    if(strcmp(&(filename[strlen(filename)-3]),".gz")==0)
	filename[strlen(filename)-3]='\0';
    if(strcmp(&(filename[strlen(filename)-2]),".Z")==0)
	filename[strlen(filename)-2]='\0';

    /* uncompress the file */
    result=stat(filename,&stat_filename);
    if(result==-1 && errno==ENOENT) /* file does not exist */
    {
	REMOVE_ITEM *remove_file;
	sprintf(cmd, "%s %s '%s' 2>/dev/null | sort > '%s'",
		UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
		UNCOMPRESS_OPT,
#else
		"",
#endif
		filename_gz, filename);
	dbprintf(("Uncompress command: %s\n",cmd));
	if (system(cmd)!=0)
	    return -1;

	/* add at beginning */
	remove_file = (REMOVE_ITEM *)alloc(sizeof(REMOVE_ITEM));
	strcpy(remove_file->filename,filename);
	remove_file->next = to_remove;
	to_remove = remove_file;
    }
    else if(!S_ISREG((stat_filename.st_mode))) {
	    return -1;
    }
    else {} /* already uncompressed */
    return 0;
}

/* see if hostname is valid */
/* valid is defined to be that there are index records for it */
/* also do a security check on the requested dump hostname */
/* to restrict access to index records if required */
/* return -1 if not okay */
int is_dump_host_valid(host)
char *host;
{
    DIR *dirp;
    struct dirent *direntp;
    char cwd[LONG_LINE];

    if (strlen(config) == 0)
    {
	reply(501, "Must set config before setting host.");
	return -1;
    }

#if 0
    /* only let a client restore itself for now unless it is the server */
    if (strcmp(remote_hostname, local_hostname) == 0)
	return 0;
    if (strcmp(remote_hostname, host) != 0)
    {
	reply(501,
	      "You don't have the necessary permissions to set dump host to %s.",
	      buf1);
	return -1;
    }
#endif

    /* check that the config actually handles that host */
    /* assume in config dir already */
    (void)getcwd(cwd, LONG_LINE);
    if ((dirp = opendir(cwd)) == NULL)
    {
	reply(599, "System error: %d.", errno);
	return -1;
    }

    while ((direntp = readdir(dirp)) != NULL)
    {
	if (strncmp(direntp->d_name, host, strlen(host)) == 0)
	{
	    (void)closedir(dirp);
	    return 0;
	}
    }

    reply(501, "No index records for host: %s. Invalid?", host);
    return -1;
}


int is_disk_valid(disk)
char *disk;
{
    DIR *dirp;
    struct dirent *direntp;
    char cwd[LONG_LINE];
    char *search_str;
    int i;

    if (strlen(dump_hostname) == 0)
    {
	reply(501, "Must set host before setting disk.");
	return -1;
    }

    /* check that given disk is from given host and handled by given config */
    (void)getcwd(cwd, LONG_LINE);
    if ((dirp = opendir(cwd)) == NULL)
    {
	reply(599, "System error: %d.", errno);
	return -1;
    }
    search_str = getindexfname(dump_hostname, disk, "00000000", 0);
    /* NOTE!!!!!! the following assumes knowledge of the
       host,disk,date,level to file name mapping and may need changing if
       that is changed */
    i = strlen(dump_hostname) + strlen(disk) + 1;

    while ((direntp = readdir(dirp)) != NULL)
    {
	if (strncmp(direntp->d_name, search_str, i) == 0)
	{
	    (void)closedir(dirp);
	    return 0;
	}
    }

    reply(501, "No index records for disk: %s. Invalid?", disk);
    return -1;
}


int is_config_valid(config)
char *config;
{
    char conf_dir[1024];

    /* check that the config actually exists */
    if (strlen(config) == 0)
    {
	reply(501, "Must set config first.");
	return -1;
    }

    /* cd to confdir */
    sprintf(conf_dir, "%s/%s", CONFIG_DIR, config);
    if (chdir(conf_dir) == -1)
    {
	reply(501, "Couldn't cd into config dir. Misconfiguration?");
	return -1;
    }

    /* read conffile */
    if (read_conffile(CONFFILE_NAME))
    {
	reply(501, "Couldn't read config file!");
	return -1;
    }

    /* okay, now look for the index directory */
    if (chdir(INDEX_DIR) == -1)
    {
	reply(501, "Index directory %s does not exist", INDEX_DIR);
	return -1;
    }
    if (chdir(getconf_byname("indexdir")) == -1)
    {
	(void)chdir(INDEX_DIR);
	reply(501, "There is no index directory for config %s.", config);
	return -1;
    }
    return 0;
}


int build_disk_table P((void))
{
    char date[LONG_LINE];
    int level;
    char tape[LONG_LINE];
    int file;
    char cmd[LONG_LINE];
    char status[LONG_LINE];
    FILE *fp;
    int first_line = 0;
    char format[LONG_LINE];

    if (strlen(disk_name) == 0)
    {
	reply(590, "Must set disk before building disk table");
	return -1;
    }

    sprintf(cmd, "%s/amadmin%s %s find %s %s", sbindir, versionsuffix(),
	    config, dump_hostname, disk_name);
    if ((fp = popen(cmd, "r")) == NULL)
    {
	reply(599, "System error %d", errno);
	return -1;
    }
    sprintf(format, "%%s %s %s %%d %%s %%d %%s", dump_hostname, disk_name);
    clear_list();
    while (fgets(cmd, LONG_LINE, fp) != NULL)
    {
	if (first_line++ == 0)
	    continue;
	if (sscanf(cmd, format, date, &level, tape, &file, status) != 5)
	    continue;			/* assume failed dump */
	if (strcmp(status,"OK")!=0)
	    continue;			/* dump failed */
	add_dump(date, level, tape, file);
	dbprintf(("- %s %d %s %d\n", date, level, tape, file));
    }
    pclose(fp);
    return 0;
}


int disk_history_list P((void))
{
    DUMP_ITEM *item;

    if (strlen(disk_name) == 0)
    {
	reply(502, "Must set config,host,disk before listing history");
	return -1;
    }

    lreply(200, " Dump history for config \"%s\" host \"%s\" disk \"%s\"",
	  config, dump_hostname, disk_name);
    for (item=first_dump(); item!=NULL; item=next_dump(item))
	lreply(201, " %s %d %s %d", item->date, item->level, item->tape,
	       item->file);
    reply(200, "Dump history for config \"%s\" host \"%s\" disk \"%s\"",
	  config, dump_hostname, disk_name);
    return 0;
}


/* is the directory dir backed up - dir assumed complete relative to
   disk mount point */
/* opaque version of command */
int is_dir_valid_opaque(dir)
char *dir;
{
    DUMP_ITEM *item;
    char cmd[LONG_LINE];
    FILE *fp;
    int last_level;
    char ldir[LONG_LINE];
    char *filename_gz;
    char filename[LONG_LINE];

    strcpy(ldir,dir);
    /* add a "/" at the end of the path */
    if(strcmp(ldir,"/"))
	strcat(ldir,"/");

    if (strlen(disk_name) == 0)
    {
	reply(502, "Must set config,host,disk before asking about directories");
	return -1;
    }
    if (strlen(date) == 0)
    {
	reply(502, "Must set date before asking about directories");
	return -1;
    }

    /* scan through till we find first dump on or before date */
    for (item=first_dump(); item!=NULL; item=next_dump(item))
	if (strcmp(item->date, date) <= 0)
	    break;
    if (item == NULL)
    {
	/* no dump for given date */
	reply(500, "No dumps available on or before date \"%s\"", date);
	return -1;
    }

    /* go back till we hit a level 0 dump */
    do
    {
	filename_gz=getindexfname(dump_hostname, disk_name,
				  item->date, item->level);
	if(uncompress_file(filename_gz,filename)!=0)
	{
	    reply(599, "System error %d", errno);
	    return -1;
	}
	sprintf(cmd, "grep \"^%s\" %s 2>/dev/null",
		ldir, filename);
	dbprintf(("c %s\n", cmd));
	if ((fp = popen(cmd, "r")) == NULL)
	{
	    reply(599, "System error %d", errno);
	    return -1;
	}
	if (fgets(cmd, LONG_LINE, fp) != NULL)
	{
	    /* found it */
	    pclose(fp);
	    return 0;
	}
	pclose(fp);

	last_level = item->level;
	do
	{
	    item=next_dump(item);
	}
	while ((item != NULL) && (item->level >= last_level));
    }
    while (item != NULL);

    reply(500, "\"%s\" is an invalid directory", dir);
    return -1;
}



/* returns the value of tapedev from the amanda.conf file if set,
   otherwise reports an error */
int tapedev_is P((void))
{
    char orig_dir[1024];
    char conf_dir[1024];
    char *result;

    /* check state okay to do this */
    if (strlen(config) == 0)
    {
	reply(501, "Must set config before asking about tapedev.");
	return -1;
    }

    /* record cwd */
    (void)getcwd(orig_dir, 1024);

    /* cd to confdir */
    sprintf(conf_dir, "%s/%s", CONFIG_DIR, config);
    if (chdir(conf_dir) == -1)
    {
	reply(501, "Couldn't cd into config dir. Misconfiguration?");
	return -1;
    }

    /* read conffile */
    if (read_conffile(CONFFILE_NAME))
    {
	reply(501, "Couldn't read config file!");
	return -1;
    }

    /* get tapedev value */
    if ((result = getconf_byname("tapedev")) == NULL)
    {
	reply(501, "Tapedev not set inside config file.");
	return -1;
    }

    /* cd back to orig cwd */
    if (chdir(orig_dir) == -1)
    {
	reply(501, "Couldn't cd back to orig dir.");
	return -1;
    }

    reply(200, result);
    return 0;
}


/* returns YES if dumps for disk are compressed, NO if not */
int are_dumps_compressed P((void))
{
    char orig_dir[1024];
    char conf_dir[1024];
    disk_t *diskp;
    disklist_t *diskl;

    /* check state okay to do this */
    if (strlen(config) == 0)
    {
	reply(501, "Must set config before asking about tapedev.");
	return -1;
    }
    if (strlen(dump_hostname) == 0)
    {
	reply(501, "Must set host name before asking about tapedev.");
	return -1;
    }
    if (strlen(disk_name) == 0)
    {
	reply(501, "Must set disk name before asking about tapedev.");
	return -1;
    }

    /* record cwd */
    (void)getcwd(orig_dir, 1024);

    /* cd to confdir */
    sprintf(conf_dir, "%s/%s", CONFIG_DIR, config);
    if (chdir(conf_dir) == -1)
    {
	reply(501, "Couldn't cd into config dir. Misconfiguration?");
	return -1;
    }

    /* read conffile */
    if (read_conffile(CONFFILE_NAME))
    {
	reply(501, "Couldn't read config file!");
	return -1;
    }

    /* read the disk file */
    if ((diskl = read_diskfile(getconf_str(CNF_DISKFILE))) == NULL)
    {
	reply(501, "Couldn't read disk file");
	return -1;
    }

    /* now go through the list of disks and find which have indexes */
    for (diskp = diskl->head; diskp != NULL; diskp = diskp->next)
	if ((strcmp(diskp->host->hostname, dump_hostname) == 0)
	    && (strcmp(diskp->name, disk_name) == 0))
	    break;
    if (diskp == NULL)
    {
	reply(501, "Couldn't find host/disk in disk file.");
	return -1;
    }

    /* cd back to orig cwd */
    if (chdir(orig_dir) == -1)
    {
	reply(501, "Couldn't cd back to orig dir.");
	return -1;
    }

    /* send data to caller */
    if (diskp->compress == COMP_NONE)
	reply(200, "NO");
    else
	reply(200, "YES");

    return 0;
}

int main(argc, argv)
int argc;
char **argv;
{
#ifdef FORCE_USERID
    char *pwname;
    struct passwd *pwptr;
#endif	/* FORCE_USERID */
    char buffer[LONG_LINE];
    char *bptr;
    int i;
    struct sockaddr_in his_addr;
    struct hostent *his_name;
    char buf1[LONG_LINE];

#ifdef FORCE_USERID

    /* we'd rather not run as root */

    if(geteuid() == 0) {
	pwname = CLIENT_LOGIN;
	if((pwptr = getpwnam(pwname)) == NULL)
	    error("error [cannot find user %s in passwd file]\n", pwname);

	initgroups(pwname, pwptr->pw_gid);
	setgid(pwptr->pw_gid);
	setuid(pwptr->pw_uid);
    }

#endif	/* FORCE_USERID */

    /* initialize */
    umask(0);
    dbopen();
    dbprintf(("%s: version %s\n", argv[0], version()));

    /* localhost[sizeof(local_hostname)-1] = '\0'; */ /* local_hostname is static */
    if(gethostname(local_hostname, sizeof(local_hostname)-1) == -1)
	error("gethostname: %s", strerror(errno));

    if (chdir(INDEX_DIR) == -1)
    {
	lreply(520, "%s AMANDA index server (%s) not ready.",
	       local_hostname, server_version);
	lreply(520, "Configuration error: cannot cd to INDEX_DIR \"%s\"",
	       INDEX_DIR);
	reply(520, "Server exiting!");
	return 1;
    }

    reply(220, "%s AMANDA index server (%s) ready.", local_hostname,
	  server_version);

    /* now trim domain off name */
    for (bptr = local_hostname; *bptr != '\0'; bptr++)
	if (*bptr == '.')
	{
	    *bptr = '\0';
	    break;
	}

    /* who are we talking to? */
    i = sizeof (his_addr);
    if (getpeername(0, (struct sockaddr *)&his_addr, &i) == -1)
	error("getpeername: %s", strerror(errno));
    if ((his_name = gethostbyaddr((char *)&(his_addr.sin_addr),
				  sizeof(struct in_addr), AF_INET)) == NULL)
	error("gethostbyaddr: %s", strerror(errno));
    for (i = 0; (his_name->h_name[i] != '\0') && (his_name->h_name[i] != '.'); i++)
	remote_hostname[i] = his_name->h_name[i];
    remote_hostname[i] = '\0';

    /* set this to an empty string so we can detect when it hasn't been set
       by the client */
    dump_hostname[0] = '\0';
    disk_name[0] = '\0';
    date[0] = '\0';

    if (argc == 2)
	strcpy(config, argv[1]);
    else
	config[0] = '\0';

    /* a real simple parser since there are only a few commands */
    while (1)
    {
	/* get a line from client */
	bptr = buffer;
	while (1)
	{
	    if ((i = getchar()) == EOF) {
		remove_uncompressed_file();
		return 1;		/* they hung up? */
	    }
	    if ((char)i == '\r')
	    {
		if ((i = getchar()) == EOF) {
		    remove_uncompressed_file();
		    return 1;		/* they hung up? */
		}
		if ((char)i == '\n')
		    break;
	    }
	    *bptr++ = (char)i;
	}
	*bptr = '\0';

	dbprintf(("> %s\n", buffer));


	if (strncmp(buffer, "QUIT", 4) == 0)
	{
	    remove_uncompressed_file();
	    reply(200, "Good bye.");
	    dbclose();
	    return 0;
	}
	else if (sscanf(buffer, "HOST %s", buf1) == 1)
	{
	    /* set host we are restoring */
	    if (is_dump_host_valid(buf1) != -1)
	    {
		strcpy(dump_hostname, buf1);
		reply(200, "Dump host set to %s.", dump_hostname);
		disk_name[0] = '\0';	/* invalidate any value */
	    }
	}
	else if (sscanf(buffer, "DISK %s", buf1) == 1)
	{
	    if (is_disk_valid(buf1) != -1)
	    {
		strcpy(disk_name, buf1);
		if (build_disk_table() != -1)
		    reply(200, "Disk set to %s.", disk_name);
	    }
	}
	else if (sscanf(buffer, "SCNF %s", buf1) == 1)
	{
	    if (is_config_valid(buf1) != -1)
	    {
		strcpy(config, buf1);
		dump_hostname[0] = '\0';
		disk_name[0] = '\0';	/* invalidate any value */
		reply(200, "Config set to %s.", config);
	    }
	}
	else if (sscanf(buffer, "DATE %s", buf1) == 1)
	{
	    strcpy(date, buf1);
	    reply(200, "Working date set to %s.", date);
	}
	else if (strcmp(buffer, "DHST") == 0)
	{
	    (void)disk_history_list();
	}
	else if (strncmp(buffer, "OISD ", 5) == 0)
	{
	    strcpy(buf1, buffer+5);
	    if (is_dir_valid_opaque(buf1) != -1)
	    {
		reply(200, "\"%s\" is a valid directory", buf1);
	    }
	}
	else if (strncmp(buffer, "OLSD ", 5) == 0)
	{
	    strcpy(buf1, buffer+5);
	    (void)opaque_ls(buf1,0);
	}
	else if (strncmp(buffer, "ORLD ", 5) == 0)
	{
	    strcpy(buf1, buffer+5);
	    (void)opaque_ls(buf1,1);
	}
	else if (strcmp(buffer, "TAPE") == 0)
	{
	    (void)tapedev_is();
	}
	else if (strcmp(buffer, "DCMP") == 0)
	{
	    (void)are_dumps_compressed();
	}
	else
	{
	    reply(500, "Command not recognised/incorrect: %s", buffer);
	}
    }

    /*NOTREACHED*/
    return 0;
}
