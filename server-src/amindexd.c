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
 * $Id: amindexd.c,v 1.13 1997/12/19 16:04:30 george Exp $
 *
 * This is the server daemon part of the index client/server system.
 * It is assumed that this is launched from inetd instead of being
 * started as a daemon because it is not often used
 */

/*
** Notes:
** - this server will do very little until it knows what Amanda config it
**   is to use.  Setting the config has the side effect of changing to the
**   index directory.
** - XXX - I'm pretty sure the config directory name should have '/'s stripped
**   from it.  It is given to us by an unknown person via the network.
*/

#include "amanda.h"
#include "conffile.h"
#include "diskfile.h"
#include "arglist.h"
#include "dgram.h"
#include "version.h"
#include "protocol.h"
#include "version.h"
#include "amindex.h"
#include "disk_history.h"
#include "list_dir.h"

#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif

#ifdef HAVE_NETINET_IP_H
#include <netinet/ip.h>
#endif

#include <grp.h>

#define LONG_LINE 256

typedef struct REMOVE_ITEM
{
    char filename[1024];
    struct REMOVE_ITEM *next;
} REMOVE_ITEM;

char *pname = "amindexd";
char *server_version = "1.1";

/* state */
char local_hostname[MAX_HOSTNAME_LENGTH];/* me! */
char remote_hostname[LONG_LINE];	/* the client */
char dump_hostname[LONG_LINE];		/* the machine we are restoring */
char disk_name[LONG_LINE];		/* the disk we are restoring */
char config[LONG_LINE];			/* the config we are restoring */
char date[LONG_LINE];
disklist_t *disk_list;			/* all the disks in the current config */

static REMOVE_ITEM *to_remove = NULL; /* file to remove at end */


void remove_uncompressed_file()
{
    REMOVE_ITEM *remove_file;

    for(remove_file=to_remove;remove_file!=NULL;remove_file=remove_file->next)
    {
	unlink(remove_file->filename);
	dbprintf(("Removing index file: %s\n",remove_file->filename));
    }
}

int uncompress_file(filename_gz, filename, filename_len)
char *filename_gz;
char *filename;
int filename_len;
{
    char cmd[2048];
    struct stat stat_filename;
    int result;

    strncpy(filename, filename_gz, filename_len-1);
    filename[filename_len-1] = '\0';
    if(strcmp(&(filename[strlen(filename)-3]),".gz")==0)
	filename[strlen(filename)-3]='\0';
    if(strcmp(&(filename[strlen(filename)-2]),".Z")==0)
	filename[strlen(filename)-2]='\0';

    /* uncompress the file */
    result=stat(filename,&stat_filename);
    if(result==-1 && errno==ENOENT) /* file does not exist */
    {
	REMOVE_ITEM *remove_file;
	ap_snprintf(cmd, sizeof(cmd), "%s %s '%s' 2>/dev/null | sort > '%s'",
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
	strncpy(remove_file->filename,
		filename, sizeof(remove_file->filename)-1);
	remove_file->filename[sizeof(remove_file->filename)-1] = '\0';
	remove_file->next = to_remove;
	to_remove = remove_file;
    }
    else if(!S_ISREG((stat_filename.st_mode))) {
	    return -1;
    }
    else {} /* already uncompressed */
    return 0;
}

/* find all matching entries in a dump listing */
/* return -1 if error */
static int process_ls_dump(dir, dump_item, recursive)
char *dir;
DUMP_ITEM *dump_item;
int  recursive;
{
    char line[2048];
    char old_line[2048];
    char filename[1024];
    char *filename_gz;
    char dir_slash[1024];
    FILE *fp;
    char *p;
    int len_dir_slash;

    if (strcmp(dir, "/") == 0) {
	strncpy(dir_slash, dir, sizeof(dir_slash)-1);
	dir_slash[sizeof(dir_slash)-1] = '\0';
    } else {
	ap_snprintf(dir_slash, sizeof(dir_slash), "%s/", dir);
    }

    filename_gz=getindexfname(dump_hostname, disk_name, dump_item->date,
			      dump_item->level);
    if(uncompress_file(filename_gz, filename, sizeof(filename))!=0)
	return -1;

    if((fp = fopen(filename,"r"))==0)
	return -1;

    len_dir_slash= strlen(dir_slash);
    old_line[0]='\0';
    while (fgets(line, LONG_LINE, fp) != NULL)
    {
	if(strncmp(dir_slash,line,len_dir_slash)==0)
	{
	    p=&line[len_dir_slash];
	    while((*p != '\n') && (*p != '/')) /* read the file name */
		p++;
	    if(*p == '/')
		p++;
	    *p = '\0'; /* overwire '\n' or cut the line */
	    if(strcmp(line,old_line)!=0)
	    {
		strncpy(old_line, line, sizeof(old_line)-1);
		old_line[sizeof(old_line)-1] = '\0';
		add_dir_list_item(dump_item, line);
	    }
	}
    }
    return 0;
}

/* send a 1 line reply to the client */
arglist_function1(void reply, int, n, char *, fmt)
{
    va_list args;
    char buf[LONG_LINE];

    arglist_start(args, fmt);
    ap_snprintf(buf, sizeof(buf), "%03d ", n);
    ap_vsnprintf(buf+4, sizeof(buf)-4, fmt, args);
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
    ap_snprintf(buf, sizeof(buf), "%03d-", n);
    ap_vsnprintf(buf+4, sizeof(buf)-4, fmt, args);
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
    ap_snprintf(buf, sizeof(buf), "%03d-", n);
    ap_vsnprintf(buf+4, sizeof(buf)-4, fmt, args);
    arglist_end(args);

    if (printf("%s\r\n", buf) <= 0)
    {
	dbprintf(("! error %d in printf\n", errno));
	remove_uncompressed_file();
	exit(1);
    }
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
    /* assume in index dir already */
    if ((dirp = opendir(".")) == NULL)
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
    char *search_str;
    int i;

    if (strlen(dump_hostname) == 0)
    {
	reply(501, "Must set config,host before setting disk.");
	return -1;
    }

    /* check that given disk is from given host and handled by given config */
    if ((dirp = opendir(".")) == NULL)
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
    ap_snprintf(conf_dir, sizeof(conf_dir), "%s/%s", CONFIG_DIR, config);
    if (chdir(conf_dir) == -1)
    {
	reply(501, "Couldn't cd into config dir.  Misconfiguration?");
	return -1;
    }

    /* read conffile */
    if (read_conffile(CONFFILE_NAME))
    {
	reply(501, "Couldn't read config file!");
	return -1;
    }

    /* read the disk file while we are here - just in case we need it */
    if ((disk_list = read_diskfile(getconf_str(CNF_DISKFILE))) == NULL)
    {
	reply(501, "Couldn't read disk file");
	return -1;
    }

    /* okay, now look for the index directory */
    if (chdir(getconf_str(CNF_INDEXDIR)) == -1)
    {
	reply(501, "Index directory %s does not exist", getconf_str(CNF_INDEXDIR));
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
	reply(590, "Must set config,host,disk before building disk table");
	return -1;
    }

    ap_snprintf(cmd, sizeof(cmd),
		"%s/amadmin%s %s find %s %s", sbindir, versionsuffix(),
		config, dump_hostname, disk_name);
    if ((fp = popen(cmd, "r")) == NULL)
    {
	reply(599, "System error %d", errno);
	return -1;
    }
    ap_snprintf(format, sizeof(format),
		"%%s %s %s %%d %%s %%d %%s", dump_hostname, disk_name);
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

    strncpy(ldir, dir, sizeof(ldir)-1);
    ldir[sizeof(ldir)-1] = '\0';
    /* add a "/" at the end of the path */
    if(strcmp(ldir,"/"))
	strncat(ldir, "/", sizeof(ldir)-strlen(ldir));

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
	if(uncompress_file(filename_gz, filename, sizeof(filename))!=0)
	{
	    reply(599, "System error %d", errno);
	    return -1;
	}
	ap_snprintf(cmd, sizeof(cmd), "grep \"^%s\" %s 2>/dev/null",
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

int opaque_ls(dir,recursive)
char *dir;
int  recursive;
{
    DUMP_ITEM *dump_item;
    DIR_ITEM *dir_item;
    int last_level;

    clear_dir_list();

    if (strlen(disk_name) == 0)
    {
	reply(502, "Must set config,host,disk before listing a directory");
	return -1;
    }
    if (strlen(date) == 0)
    {
	reply(502, "Must set date before listing a directory");
	return -1;
    }

    /* scan through till we find first dump on or before date */
    for (dump_item=first_dump(); dump_item!=NULL; dump_item=next_dump(dump_item))
	if (strcmp(dump_item->date, date) <= 0)
	    break;

    if (dump_item == NULL)
    {
	/* no dump for given date */
	reply(500, "No dumps available on or before date \"%s\"", date);
	return -1;
    }

    /* get data from that dump */
    if (process_ls_dump(dir, dump_item,recursive) == -1) {
	reply(599, "System error %d", errno);
	return -1;
    }

    /* go back processing higher level dumps till we hit a level 0 dump */
    last_level = dump_item->level;
    while ((last_level != 0) && ((dump_item=next_dump(dump_item)) != NULL))
    {
	if (dump_item->level < last_level)
	{
	    last_level = dump_item->level;
	    if (process_ls_dump(dir, dump_item,recursive) == -1) {
		reply(599, "System error %d", errno);
		return -1;
	    }
	}
    }

    /* return the information to the caller */
    if(recursive)
    {
	lreply(200, " Opaque recursive list of %s", dir);
	for (dir_item = dir_list; dir_item != NULL; dir_item = dir_item->next)
	    fast_lreply(201, " %s %d %-16s %s",
			dir_item->dump->date, dir_item->dump->level,
			dir_item->dump->tape, dir_item->path);
	reply(200, " Opaque recursive list of %s", dir);
    }
    else
    {
	lreply(200, " Opaque list of %s", dir);
	for (dir_item = dir_list; dir_item != NULL; dir_item = dir_item->next)
	    lreply(201, " %s %d %-16s %s",
		   dir_item->dump->date, dir_item->dump->level,
		   dir_item->dump->tape, dir_item->path);
	reply(200, " Opaque list of %s", dir);
    }
    clear_dir_list();
    return 0;
}


/* returns the value of tapedev from the amanda.conf file if set,
   otherwise reports an error */
int tapedev_is P((void))
{
    char *result;

    /* check state okay to do this */
    if (strlen(config) == 0)
    {
	reply(501, "Must set config before asking about tapedev.");
	return -1;
    }

    /* get tapedev value */
    if ((result = getconf_str(CNF_TAPEDEV)) == NULL)
    {
	reply(501, "Tapedev not set in config file.");
	return -1;
    }

    reply(200, result);
    return 0;
}


/* returns YES if dumps for disk are compressed, NO if not */
int are_dumps_compressed P((void))
{
    disk_t *diskp;

    /* check state okay to do this */
    if (strlen(disk_name) == 0)
    {
	reply(501, "Must set config,host,disk name before asking about dumps.");
	return -1;
    }

    /* now go through the list of disks and find which have indexes */
    for (diskp = disk_list->head; diskp != NULL; diskp = diskp->next)
	if ((strcmp(diskp->host->hostname, dump_hostname) == 0)
	    && (strcmp(diskp->name, disk_name) == 0))
	    break;

    if (diskp == NULL)
    {
	reply(501, "Couldn't find host/disk in disk file.");
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

    if (argc == 2) {
	strncpy(config, argv[1], sizeof(config)-1);
	config[sizeof(config)-1] = '\0';
	if (is_config_valid(config) != -1) return 1;
    } else {
	config[0] = '\0';
    }

    reply(220, "%s AMANDA index server (%s) ready.", local_hostname,
	  server_version);

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
		strncpy(dump_hostname, buf1, sizeof(dump_hostname)-1);
		dump_hostname[sizeof(dump_hostname)-1] = '\0';
		reply(200, "Dump host set to %s.", dump_hostname);
		disk_name[0] = '\0';	/* invalidate any value */
	    }
	}
	else if (sscanf(buffer, "DISK %s", buf1) == 1)
	{
	    if (is_disk_valid(buf1) != -1)
	    {
		strncpy(disk_name, buf1, sizeof(disk_name)-1);
		disk_name[sizeof(disk_name)-1] = '\0';
		if (build_disk_table() != -1)
		    reply(200, "Disk set to %s.", disk_name);
	    }
	}
	else if (sscanf(buffer, "SCNF %s", buf1) == 1)
	{
	    if (is_config_valid(buf1) != -1)
	    {
		strncpy(config, buf1, sizeof(config)-1);
		config[sizeof(config)-1] = '\0';
		dump_hostname[0] = '\0';
		disk_name[0] = '\0';	/* invalidate any value */
		reply(200, "Config set to %s.", config);
	    }
	}
	else if (sscanf(buffer, "DATE %s", buf1) == 1)
	{
	    strncpy(date, buf1, sizeof(date)-1);
	    date[sizeof(date)-1] = '\0';
	    reply(200, "Working date set to %s.", date);
	}
	else if (strcmp(buffer, "DHST") == 0)
	{
	    (void)disk_history_list();
	}
	else if (strncmp(buffer, "OISD ", 5) == 0)
	{
	    strncpy(buf1, buffer+5, sizeof(buf1)-1);
	    buf1[sizeof(buf1)-1] = '\0';
	    if (is_dir_valid_opaque(buf1) != -1)
	    {
		reply(200, "\"%s\" is a valid directory", buf1);
	    }
	}
	else if (strncmp(buffer, "OLSD ", 5) == 0)
	{
	    strncpy(buf1, buffer+5, sizeof(buf1)-1);
	    buf1[sizeof(buf1)-1] = '\0';
	    (void)opaque_ls(buf1,0);
	}
	else if (strncmp(buffer, "ORLD ", 5) == 0)
	{
	    strncpy(buf1, buffer+5, sizeof(buf1)-1);
	    buf1[sizeof(buf1)-1] = '\0';
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
