/***************************************************************************
*
* File:          $RCSfile: amindexd.c,v $
* Part of:       
*
* Revision:      $Revision: 1.4 $
* Last Edited:   $Date: 1997/07/03 14:32:21 $
* Author:        $Author: george $
*
* Notes:         
* Private Func:  
* History:       $Log: amindexd.c,v $
* History:       Revision 1.4  1997/07/03 14:32:21  george
* History:       Put dumptype info directly into the disk structure.
* History:
* History:       This is in preparation for turning dumptype's into macros.
* History:
* History:       Revision 1.3  1997/07/03 11:29:11  george
* History:       Convert the config file compression variables from 3 separate mutually
* History:       exclusive flags into 1 int with different values.
* History:
* History:       Revision 1.2  1997/05/01 19:03:39  oliva
* History:       Integrated amgetidx into sendbackup&dumper.
* History:
* History:       New command in configuration file: indexdir; it indicates the
* History:       subdirectory of amanda-index where index files should be stored.
* History:
* History:       Index files are now compressed in the server (since the server will
* History:       have to decompress them)
* History:
* History:       Removed RSH configuration from configure
* History:
* History:       Added check for index directory to self check
* History:
* History:       Changed amindexd and amtrmidx to use indexdir option.
* History:
* History:       Revision 1.1.1.1  1997/03/15 21:30:10  amcore
* History:       Mass import of 2.3.0.4 as-is.  We can remove generated files later.
* History:
* History:       Revision 1.16  1996/12/19 08:56:29  alan
* History:       first go at file extraction
* History:
* History:       Revision 1.15  1996/12/14 08:56:32  alan
* History:       had compare value for strncmp wrong!
* History:
* History:       Revision 1.14  1996/12/09 08:08:42  alan
* History:       changes from Les Gonder <les@trigraph.on.ca> to support files with
* History:       spaces in their names
* History:
* History:       Revision 1.13  1996/11/06 08:24:45  alan
* History:       changed initial ordering so client sees something if INDEX_DIR doesn't
* History:       exist.
* History:
* History:       Revision 1.12  1996/11/03 10:03:20  alan
* History:       uncommented out code to set userid to dump user
* History:
* History:       Revision 1.11  1996/10/30 09:49:38  alan
* History:       removed #define of GREP since not needed
* History:
* History:       Revision 1.10  1996/10/29 08:32:14  alan
* History:       Pete Geenhuizen inspired changes to support logical disk names etc
* History:
* History:       Revision 1.9  1996/10/01 18:19:37  alan
* History:       synchronization with Blair's changes
* History:
* History:       Revision 1.8  1996/09/24 11:14:40  alan
* History:       updated version number
* History:
* History:       Revision 1.7  1996/07/18 10:31:25  alan
* History:       got it wrong
* History:
* History:       Revision 1.6  1996/07/18 10:24:18  alan
* History:       get gzip path from config now
* History:
* History:       Revision 1.5  1996/06/17 10:41:39  alan
* History:       made is_dir_valid opaque
* History:
* History:       Revision 1.4  1996/06/13 11:10:04  alan
* History:       made disk check only use file names in index directory
* History:       so question answered is really: Does amindexd know about disk?
* History:
* History:       Revision 1.3  1996/06/13 10:05:14  alan
* History:       added checking of return codes when sending to client
* History:
* History:       Revision 1.2  1996/06/09 10:01:44  alan
* History:       made command paths specifiable
* History:
* History:       Revision 1.1  1996/05/13 09:13:39  alan
* History:       Initial revision
* History:
*
***************************************************************************/

/* This is the server daemon part of the index client/server system. It is
   assummed that this is launched from inetd instead of being started as
   a daemon because it is not often used
 */

#include "amanda.h"
#include "conffile.h"
#include "diskfile.h"
#include "arglist.h"
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <grp.h>
#include "dgram.h"
#include "version.h"
#include "protocol.h"
#include "amindexd.h"
#include "version.h"
#include "amindex.h"

char *pname = "amindexd";
char *server_version = "1.0";

/* state */
char local_hostname[MAX_HOSTNAME_LENGTH];/* me! */
char remote_hostname[LONG_LINE];	/* the client */
char dump_hostname[LONG_LINE];		/* the machine we are restoring */
char disk_name[LONG_LINE];		/* the disk we are restoring */
char config[LONG_LINE];			/* the config we are restoring */
char date[LONG_LINE];

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
	exit(1);
    }
    if (fflush(stdout) != 0)
    {
	dbprintf(("! error in fflush %d\n", errno));
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
	exit(1);
    }
    if (fflush(stdout) != 0)
    {
	dbprintf(("! error in fflush %d\n", errno));
	exit(1);
    }

    dbprintf(("< %s\n", buf));
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
    char *result;
    
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
    FILE *fp;
    int first_line = 0;
    char format[LONG_LINE];
    
    if (strlen(disk_name) == 0)
    {
	reply(590, "Must set disk before building disk table");
	return -1;
    }
    
    sprintf(cmd, "%s/amadmin%s %s find %s %s", bindir, versionsuffix(),
	    config, dump_hostname, disk_name);
    if ((fp = popen(cmd, "r")) == NULL)
    {
	reply(599, "System error %d", errno);
	return -1;
    }
    sprintf(format, "%%s %s %s %%d %%s %%d", dump_hostname, disk_name);
    clear_list();
    while (fgets(cmd, LONG_LINE, fp) != NULL)
    {
	if (first_line++ == 0)
	    continue;
	if (sscanf(cmd, format, date, &level, tape, &file) != 4)
	    continue;			/* assume failed dump */
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
	sprintf(cmd, "%s %s %s 2>/dev/null | grep \"^%s\"",
		UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
		UNCOMPRESS_OPT,
#else
		"",
#endif
		getindexfname(dump_hostname, disk_name,
			      item->date, item->level),
		dir);
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
    dbopen("/tmp/amindexd.debug");
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
	    if ((i = getchar()) == EOF)
		return 1;		/* they hung up? */
	    if ((char)i == '\r') 
	    {
		if ((i = getchar()) == EOF)
		    return 1;		/* they hung up? */
		if ((char)i == '\n')
		    break;
	    }
	    *bptr++ = (char)i;
	}
	*bptr = '\0';

	dbprintf(("> %s\n", buffer));

		    
	if (strncmp(buffer, "QUIT", 4) == 0) 
	{
	    reply(200, "Good bye.");
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
	    (void)opaque_ls(buf1);
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
