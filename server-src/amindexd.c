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
 * $Id: amindexd.c,v 1.19 1998/01/02 18:48:18 jrj Exp $
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
#include "logfile.h"

#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif

#ifdef HAVE_NETINET_IP_H
#include <netinet/ip.h>
#endif

#include <grp.h>

typedef struct REMOVE_ITEM
{
    char *filename;
    struct REMOVE_ITEM *next;
} REMOVE_ITEM;

char *pname = "amindexd";
char *server_version = "1.1";

/* state */
char local_hostname[MAX_HOSTNAME_LENGTH+1];	/* me! */
char *remote_hostname = NULL;			/* the client */
char *dump_hostname = NULL;			/* machine we are restoring */
char *disk_name;				/* disk we are restoring */
char *config = NULL;				/* config we are restoring */
char *target_date = NULL;
disklist_t *disk_list;				/* all disks in cur config */

static int amindexd_debug = 0;

static REMOVE_ITEM *uncompress_remove = NULL;
					/* uncompressed files to remove */

REMOVE_ITEM *remove_files(REMOVE_ITEM *remove)
{
    REMOVE_ITEM *prev;

    while(remove) {
	dbprintf(("Removing index file: %s\n", remove->filename));
	unlink(remove->filename);
	afree(remove->filename);
	prev = remove;
	remove = remove->next;
	afree(prev);
    }
    return remove;
}

char *uncompress_file(filename_gz)
char *filename_gz;
{
    char *cmd = NULL;
    char *filename = NULL;
    struct stat stat_filename;
    int result;
    int len;

    filename = stralloc(filename_gz);
    len = strlen(filename);
    if(len > 3 && strcmp(&(filename[len-3]),".gz")==0) {
	filename[len-3]='\0';
    } else if(len > 2 && strcmp(&(filename[len-2]),".Z")==0) {
	filename[len-2]='\0';
    }

    /* uncompress the file */
    result=stat(filename,&stat_filename);
    if(result==-1 && errno==ENOENT) {		/* file does not exist */
	REMOVE_ITEM *remove_file;
	cmd = vstralloc(UNCOMPRESS_PATH,
#ifdef UNCOMPRESS_OPT
			" ", UNCOMPRESS_OPT,
#endif
			" \'", filename_gz, "\'",
			" 2>/dev/null",
			" | sort",
			" > ", "\'", filename, "\'",
			NULL);
	dbprintf(("Uncompress command: %s\n",cmd));
	if (system(cmd)!=0) {
	    afree(filename);
	    afree(cmd);
	    return NULL;
	}

	/* add at beginning */
	remove_file = (REMOVE_ITEM *)alloc(sizeof(REMOVE_ITEM));
	remove_file->filename = stralloc(filename);
	remove_file->next = uncompress_remove;
	uncompress_remove = remove_file;
    } else if(!S_ISREG((stat_filename.st_mode))) {
	    afree(filename);
	    afree(cmd);
	    return NULL;
    } else {
	/* already uncompressed */
    }
    afree(cmd);
    return filename;
}

/* find all matching entries in a dump listing */
/* return -1 if error */
static int process_ls_dump(dir, dump_item, recursive)
char *dir;
DUMP_ITEM *dump_item;
int  recursive;
{
    char *line = NULL;
    char *old_line = NULL;
    char *filename = NULL;
    char *filename_gz;
    char *dir_slash = NULL;
    FILE *fp;
    char *s;
    int ch;
    int len_dir_slash;

    if (strcmp(dir, "/") == 0) {
	dir_slash = stralloc(dir);
    } else {
	dir_slash = stralloc2(dir, "/");
    }

    filename_gz=getindexfname(dump_hostname, disk_name, dump_item->date,
			      dump_item->level);
    if((filename = uncompress_file(filename_gz)) == NULL) {
	afree(dir_slash);
	return -1;
    }

    if((fp = fopen(filename,"r"))==0) {
	afree(dir_slash);
	return -1;
    }

    len_dir_slash=strlen(dir_slash);

    for(; (line = agets(fp)) != NULL; free(line)) {
	if(strncmp(dir_slash, line, len_dir_slash) == 0) {
	    s = line + len_dir_slash;
	    ch = *s++;
	    while(ch && ch != '/') ch = *s++;	/* find end of the file name */
	    if(ch == '/') {
		ch = *s++;
	    }
	    s[-1] = '\0';
	    if(old_line == NULL || strcmp(line, old_line) != 0) {
		add_dir_list_item(dump_item, line);
		old_line = line;
		line = NULL;
	    }
	}
    }
    afree(old_line);
    afree(line);
    afree(filename);
    afree(dir_slash);
    return 0;
}

/* send a 1 line reply to the client */
arglist_function1(void reply, int, n, char *, fmt)
{
    va_list args;
    char buf[STR_SIZE];

    arglist_start(args, fmt);
    ap_snprintf(buf, sizeof(buf), "%03d ", n);
    ap_vsnprintf(buf+4, sizeof(buf)-4, fmt, args);
    arglist_end(args);

    if (printf("%s\r\n", buf) <= 0)
    {
	dbprintf(("! error %d in printf\n", errno));
	uncompress_remove = remove_files(uncompress_remove);
	exit(1);
    }
    if (fflush(stdout) != 0)
    {
	dbprintf(("! error in fflush %d\n", errno));
	uncompress_remove = remove_files(uncompress_remove);
	exit(1);
    }
    dbprintf(("< %s\n", buf));
}

/* send one line of a multi-line response */
arglist_function1(void lreply, int, n, char *, fmt)
{
    va_list args;
    char buf[STR_SIZE];

    arglist_start(args, fmt);
    ap_snprintf(buf, sizeof(buf), "%03d-", n);
    ap_vsnprintf(buf+4, sizeof(buf)-4, fmt, args);
    arglist_end(args);

    if (printf("%s\r\n", buf) <= 0)
    {
	dbprintf(("! error %d in printf\n", errno));
	uncompress_remove = remove_files(uncompress_remove);
	exit(1);
    }
    if (fflush(stdout) != 0)
    {
	dbprintf(("! error in fflush %d\n", errno));
	uncompress_remove = remove_files(uncompress_remove);
	exit(1);
    }

    dbprintf(("< %s\n", buf));
}

/* send one line of a multi-line response */
arglist_function1(void fast_lreply, int, n, char *, fmt)
{
    va_list args;
    char buf[STR_SIZE];

    arglist_start(args, fmt);
    ap_snprintf(buf, sizeof(buf), "%03d-", n);
    ap_vsnprintf(buf+4, sizeof(buf)-4, fmt, args);
    arglist_end(args);

    if (printf("%s\r\n", buf) <= 0)
    {
	dbprintf(("! error %d in printf\n", errno));
	uncompress_remove = remove_files(uncompress_remove);
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

    if (config == NULL) {
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

    if (config == NULL || dump_hostname == NULL) {
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
    char *conf_dir = NULL;

    /* check that the config actually exists */
    if (config == NULL) {
	reply(501, "Must set config first.");
	return -1;
    }

    /* cd to confdir */
    conf_dir = vstralloc(CONFIG_DIR, "/", config, NULL);
    if (chdir(conf_dir) == -1)
    {
	reply(501, "Couldn't cd into config dir.  Misconfiguration?");
	afree(conf_dir);
	return -1;
    }
    afree(conf_dir);

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
    char *date;
    char *host;
    char *disk;
    int level;
    char *tape;
    int file;
    char *status;
    char *cmd = NULL;
    FILE *fp;
    int first_line = 0;
    char *s;
    int ch;
    char *line;

    if (config == NULL || dump_hostname == NULL || disk_name == NULL) {
	reply(590, "Must set config,host,disk before building disk table");
	return -1;
    }

    cmd = vstralloc(sbindir, "/", "amadmin", versionsuffix(),
		    " ", config,
		    " ", "find",
		    " ", dump_hostname,
		    " ", disk_name,
		    NULL);
    if ((fp = popen(cmd, "r")) == NULL)
    {
	reply(599, "System error %d", errno);
	afree(cmd);
	return -1;
    }
    afree(cmd);
    clear_list();
    for(; (line = agets(fp)) != NULL; free(line)) {
	if (first_line++ == 0) {
	    continue;
	}

	s = line;
	ch = *s++;

	skip_whitespace(s, ch);			/* find the date */
	if (ch == '\0') {
	    continue;				/* no date field */
	}
	date = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the host name */
	if (ch == '\0') {
	    continue;				/* no host name */
	}
	host = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';
	if (strcmp(dump_hostname, host) != 0) {
	    continue;				/* wrong host */
	}

	skip_whitespace(s, ch);			/* find the disk name */
	if (ch == '\0') {
	    continue;				/* no disk name */
	}
	host = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';
	if (strcmp(disk_name, host) != 0) {
	    continue;				/* wrong disk */
	}

	skip_whitespace(s, ch);			/* find the level number */
	if (ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	    continue;				/* bad level */
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);			/* find the tape label */
	if (ch == '\0') {
	    continue;				/* no tape field */
	}
	tape = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the file number */
	if (ch == '\0' || sscanf(s - 1, "%d", &file) != 1) {
	    continue;				/* bad file number */
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);			/* find the status */
	if (ch == '\0') {
	    continue;				/* no status field */
	}
	status = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';
	if (strcmp("OK", status) != 0) {
	    continue;			/* wrong status */
	}

	add_dump(date, level, tape, file);
	dbprintf(("- %s %d %s %d\n", date, level, tape, file));
    }
    apclose(fp);
    return 0;
}


int disk_history_list P((void))
{
    DUMP_ITEM *item;

    if (config == NULL || dump_hostname == NULL || disk_name == NULL) {
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
    char *line = NULL;
    FILE *fp;
    int last_level;
    char *ldir = NULL;
    char *filename_gz;
    char *filename = NULL;
    int len;
    int ldir_len;

    if (config == NULL || dump_hostname == NULL || disk_name == NULL) {
	reply(502, "Must set config,host,disk before asking about directories");
	return -1;
    }
    if (target_date == NULL) {
	reply(502, "Must set date before asking about directories");
	return -1;
    }

    /* scan through till we find first dump on or before date */
    for (item=first_dump(); item!=NULL; item=next_dump(item))
	if (strcmp(item->date, target_date) <= 0)
	    break;

    if (item == NULL)
    {
	/* no dump for given date */
	reply(500, "No dumps available on or before date \"%s\"", target_date);
	return -1;
    }

    if(strcmp(dir, "/") == 0) {
	ldir = stralloc(dir);
    } else {
	ldir = stralloc2(dir, "/");
    }
    ldir_len = strlen(ldir);

    /* go back till we hit a level 0 dump */
    do
    {
	filename_gz=getindexfname(dump_hostname, disk_name,
				  item->date, item->level);
	afree(filename);
	if((filename = uncompress_file(filename_gz)) == NULL) {
	    reply(599, "System error %d", errno);
	    afree(ldir);
	    return -1;
	}
	dbprintf(("f %s\n", filename));
	if ((fp = fopen(filename, "r")) == NULL) {
	    reply(599, "System error %d", errno);
	    afree(filename);
	    afree(ldir);
	    return -1;
	}
	for(; (line = agets(fp)) != NULL; free(line)) {
	    if (strncmp(line, ldir, ldir_len) != 0) {
		continue;			/* not found yet */
	    }
	    afree(filename);
	    afree(ldir);
	    afclose(fp);
	    return 0;
	}
	afclose(fp);

	last_level = item->level;
	do
	{
	    item=next_dump(item);
	} while ((item != NULL) && (item->level >= last_level));
    } while (item != NULL);

    afree(filename);
    afree(ldir);
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

    if (config == NULL || dump_hostname == NULL || disk_name == NULL) {
	reply(502, "Must set config,host,disk before listing a directory");
	return -1;
    }
    if (target_date == NULL) {
	reply(502, "Must set date before listing a directory");
	return -1;
    }

    /* scan through till we find first dump on or before date */
    for (dump_item=first_dump(); dump_item!=NULL; dump_item=next_dump(dump_item))
	if (strcmp(dump_item->date, target_date) <= 0)
	    break;

    if (dump_item == NULL)
    {
	/* no dump for given date */
	reply(500, "No dumps available on or before date \"%s\"", target_date);
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
    if (config == NULL) {
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
    if (config == NULL || dump_hostname == NULL || disk_name == NULL) {
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
    char *line = NULL, *part = NULL;
    char *s, *fp;
    int ch;
    char *cmd_undo, cmd_undo_ch;
    int i;
    struct sockaddr_in his_addr;
    struct hostent *his_name;
    char *arg;
    int arg_len;
    char *cmd;
    int len;
    int fd;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

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

    argc--;
    argv++;

    if(argc > 0 && strcmp(*argv, "-t") == 0) {
	amindexd_debug = 1;
	argc--;
	argv++;
    }

    afree(config);
    if (argc > 0) {
	config = newstralloc(config, *argv);
	argc--;
	argv++;
    }

    umask(0007);
    dbopen();
    dbprintf(("%s: version %s\n", pname, version()));

    if(gethostname(local_hostname, sizeof(local_hostname)-1) == -1)
	error("gethostname: %s", strerror(errno));
    local_hostname[sizeof(local_hostname)-1] = '\0';

    /* now trim domain off name */
    s = local_hostname;
    ch = *s++;
    while(ch && ch != '.') ch = *s++;
    s[-1] = '\0';

    if(amindexd_debug) {
	remote_hostname = newstralloc(remote_hostname, local_hostname);
    } else {
	/* who are we talking to? */
	i = sizeof (his_addr);
	if (getpeername(0, (struct sockaddr *)&his_addr, &i) == -1)
	error("getpeername: %s", strerror(errno));
	if ((his_name = gethostbyaddr((char *)&(his_addr.sin_addr),
				      sizeof(struct in_addr),
				      AF_INET)) == NULL) {
	    error("gethostbyaddr: %s", strerror(errno));
	}
	s = his_name->h_name;
	ch = *s++;
	while(ch && ch != '.') ch = *s++;
	s[-1] = '\0';
	remote_hostname = newstralloc(remote_hostname, fp);
	s[-1] = ch;
    }

    /* clear these so we can detect when the have not been set by the client */
    afree(dump_hostname);
    afree(disk_name);
    afree(target_date);

    if (config != NULL && is_config_valid(config) != -1) return 1;

    reply(220, "%s AMANDA index server (%s) ready.", local_hostname,
	  server_version);

    /* a real simple parser since there are only a few commands */
    while (1)
    {
	/* get a line from the client */
	afree(line);
	while(1) {
	    if((part = agets(stdin)) == NULL) {
		if(errno != 0) {
		    dbprintf(("? read error: %s\n", strerror(errno)));
		} else {
		    dbprintf(("? unexpected EOF\n"));
		}
		if(line) {
		    dbprintf(("? unprocessed input:\n"));
		    dbprintf(("-----\n"));
		    dbprintf(("? %s\n", line));
		    dbprintf(("-----\n"));
		}
		afree(line);
		afree(part);
		uncompress_remove = remove_files(uncompress_remove);
		dbclose();
		return 1;		/* they hung up? */
	    }
	    if(line) {
		strappend(line, part);
		afree(part);
	    } else {
		line = part;
		part = NULL;
	    }
	    if(amindexd_debug) {
		break;			/* we have a whole line */
	    }
	    if((len = strlen(line)) > 0 && line[len-1] == '\r') {
		line[len-1] = '\0';	/* zap the '\r' */
		break;
	    }
	    /*
	     * Hmmm.  We got a "line" from areads(), which means it saw
	     * a '\n' (or EOF, etc), but there was not a '\r' before it.
	     * Put a '\n' back in the buffer and loop for more.
	     */
	    strappend(line, "\n");
	}

	dbprintf(("> %s\n", line));

	arg = NULL;
	s = line;
	ch = *s++;

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    reply(500, "Command not recognised/incorrect: %s", line);
	    continue;
	}
	cmd = s - 1;

	skip_non_whitespace(s, ch);
	cmd_undo = s-1;				/* for error message */
	cmd_undo_ch = *cmd_undo;
	*cmd_undo = '\0';
	if (ch) {
	    skip_whitespace(s, ch);		/* find the argument */
	    if (ch) {
		arg = s-1;
		skip_non_whitespace(s, ch);
		/*
		 * Save the length of the next non-whitespace string
		 * (e.g. a host name), but do not terminate it.  Some
		 * commands want the rest of the line, whitespace or
		 * not.
		 */
		arg_len = s-arg;
	    }
	}

	if (strcmp(cmd, "QUIT") == 0) {
	    break;
	} else if (strcmp(cmd, "HOST") == 0 && arg) {
	    /* set host we are restoring */
	    s[-1] = '\0';
	    if (is_dump_host_valid(arg) != -1)
	    {
		dump_hostname = newstralloc(dump_hostname, arg);
		reply(200, "Dump host set to %s.", dump_hostname);
		afree(disk_name);		/* invalidate any value */
	    }
	    s[-1] = ch;
	} else if (strcmp(cmd, "DISK") == 0 && arg) {
	    s[-1] = '\0';
	    if (is_disk_valid(arg) != -1) {
		disk_name = newstralloc(disk_name, arg);
		if (build_disk_table() != -1) {
		    reply(200, "Disk set to %s.", disk_name);
		}
	    }
	    s[-1] = ch;
	} else if (strcmp(cmd, "SCNF") == 0 && arg) {
	    s[-1] = '\0';
	    if (is_config_valid(arg) != -1) {
		config = newstralloc(config, arg);
		afree(dump_hostname);		/* invalidate any value */
		afree(disk_name);		/* invalidate any value */
		reply(200, "Config set to %s.", config);
	    }
	    s[-1] = ch;
	} else if (strcmp(cmd, "DATE") == 0 && arg) {
	    s[-1] = '\0';
	    target_date = newstralloc(target_date, arg);
	    reply(200, "Working date set to %s.", target_date);
	    s[-1] = ch;
	} else if (strcmp(cmd, "DHST") == 0) {
	    (void)disk_history_list();
	} else if (strcmp(cmd, "OISD") == 0 && arg) {
	    if (is_dir_valid_opaque(arg) != -1) {
		reply(200, "\"%s\" is a valid directory", arg);
	    }
	} else if (strcmp(cmd, "OLSD") == 0 && arg) {
	    (void)opaque_ls(arg,0);
	} else if (strcmp(cmd, "ORLD") == 0 && arg) {
	    (void)opaque_ls(arg,1);
	} else if (strcmp(cmd, "TAPE") == 0) {
	    (void)tapedev_is();
	} else if (strcmp(cmd, "DCMP") == 0) {
	    (void)are_dumps_compressed();
	} else {
	    *cmd_undo = cmd_undo_ch;	/* restore the command line */
	    reply(500, "Command not recognised/incorrect: %s", cmd);
	}
    }

    uncompress_remove = remove_files(uncompress_remove);
    reply(200, "Good bye.");
    dbclose();
    return 0;
}
