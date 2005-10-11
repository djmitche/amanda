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
 * $Id: amindexd.c,v 1.78 2005/10/11 01:17:00 vectro Exp $
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
#include "clock.h"
#include "version.h"
#include "amindex.h"
#include "disk_history.h"
#include "list_dir.h"
#include "logfile.h"
#include "token.h"
#include "find.h"
#include "tapefile.h"

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

/* state */
char local_hostname[MAX_HOSTNAME_LENGTH+1];	/* me! */
char *remote_hostname = NULL;			/* the client */
char *dump_hostname = NULL;			/* machine we are restoring */
char *disk_name;				/* disk we are restoring */
char *target_date = NULL;
disklist_t disk_list;				/* all disks in cur config */
find_result_t *output_find = NULL;
int split_support_warn = 0;

static int amindexd_debug = 0;

static REMOVE_ITEM *uncompress_remove = NULL;
					/* uncompressed files to remove */

static am_feature_t *our_features = NULL;
static am_feature_t *their_features = NULL;

static int check_security P((struct sockaddr_in *addr, char *str,
    unsigned long cksum, char **errstr));
static REMOVE_ITEM *remove_files P((REMOVE_ITEM *));
static char *uncompress_file P((char *, char **));
static int process_ls_dump P((char *, DUMP_ITEM *, int, char **));

 /* XXX this is a hack to make sure the printf-ish output buffer
    for lreply and friends is big enough for long label strings.
    Should go away if someone institutes a more fundamental fix 
    for that problem. */
 static int str_buffer_size = STR_SIZE;

static void reply P((int, char *, ...))
    __attribute__ ((format (printf, 2, 3)));
static void lreply P((int, char *, ...))
    __attribute__ ((format (printf, 2, 3)));
static void fast_lreply P((int, char *, ...))
    __attribute__ ((format (printf, 2, 3)));
static int is_dump_host_valid P((char *));
static int is_disk_valid P((char *));
static int is_config_valid P((char *));
static int build_disk_table P((void));
static int disk_history_list P((void));
static int is_dir_valid_opaque P((char *));
static int opaque_ls P((char *, int));
static int tapedev_is P((void));
static int are_dumps_compressed P((void));
int main P((int, char **));

static REMOVE_ITEM *remove_files(remove)
REMOVE_ITEM *remove;
{
    REMOVE_ITEM *prev;

    while(remove) {
	dbprintf(("%s: removing index file: %s\n",
		  debug_prefix_time(NULL), remove->filename));
	unlink(remove->filename);
	amfree(remove->filename);
	prev = remove;
	remove = remove->next;
	amfree(prev);
    }
    return remove;
}

static char *uncompress_file(filename_gz, emsg)
char *filename_gz;
char **emsg;
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
	dbprintf(("%s: uncompress command: %s\n",
		  debug_prefix_time(NULL), cmd));
	if (system(cmd)!=0) {
	    amfree(*emsg);
	    *emsg = vstralloc("\"", cmd, "\" failed", NULL);
	    unlink(filename);
	    errno = -1;
	    amfree(filename);
	    amfree(cmd);
	    return NULL;
	}

	/* add at beginning */
	remove_file = (REMOVE_ITEM *)alloc(sizeof(REMOVE_ITEM));
	remove_file->filename = stralloc(filename);
	remove_file->next = uncompress_remove;
	uncompress_remove = remove_file;
    } else if(!S_ISREG((stat_filename.st_mode))) {
	    amfree(*emsg);
	    *emsg = vstralloc("\"", filename, "\" is not a regular file", NULL);
	    errno = -1;
	    amfree(filename);
	    amfree(cmd);
	    return NULL;
    } else {
	/* already uncompressed */
    }
    amfree(cmd);
    return filename;
}

/* find all matching entries in a dump listing */
/* return -1 if error */
static int process_ls_dump(dir, dump_item, recursive, emsg)
char *dir;
DUMP_ITEM *dump_item;
int  recursive;
char **emsg;
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

    filename_gz = getindexfname(dump_hostname, disk_name, dump_item->date,
			        dump_item->level);
    if((filename = uncompress_file(filename_gz, emsg)) == NULL) {
	amfree(filename_gz);
	amfree(dir_slash);
	return -1;
    }
    amfree(filename_gz);

    if((fp = fopen(filename,"r"))==0) {
	amfree(*emsg);
	*emsg = stralloc(strerror(errno));
	amfree(dir_slash);
	return -1;
    }

    len_dir_slash=strlen(dir_slash);

    for(; (line = agets(fp)) != NULL; free(line)) {
	if(strncmp(dir_slash, line, len_dir_slash) == 0) {
	    if(!recursive) {
		s = line + len_dir_slash;
		ch = *s++;
		while(ch && ch != '/') ch = *s++;/* find end of the file name */
		if(ch == '/') {
		    ch = *s++;
		}
		s[-1] = '\0';
	    }
	    if(old_line == NULL || strcmp(line, old_line) != 0) {
		add_dir_list_item(dump_item, line);
		amfree(old_line);
		old_line = line;
		line = NULL;
	    }
	}
    }
    afclose(fp);
    amfree(old_line);
    amfree(line);
    amfree(filename);
    amfree(dir_slash);
    return 0;
}

/* send a 1 line reply to the client */
printf_arglist_function1(static void reply, int, n, char *, fmt)
{
    va_list args;
    char buf[str_buffer_size];

    arglist_start(args, fmt);
    snprintf(buf, sizeof(buf), "%03d ", n);
    vsnprintf(buf+4, sizeof(buf)-4, fmt, args);
    arglist_end(args);

    if (printf("%s\r\n", buf) < 0)
    {
	dbprintf(("%s: ! error %d (%s) in printf\n",
		  debug_prefix_time(NULL), errno, strerror(errno)));
	uncompress_remove = remove_files(uncompress_remove);
	exit(1);
    }
    if (fflush(stdout) != 0)
    {
	dbprintf(("%s: ! error %d (%s) in fflush\n",
		  debug_prefix_time(NULL), errno, strerror(errno)));
	uncompress_remove = remove_files(uncompress_remove);
	exit(1);
    }
    dbprintf(("%s: < %s\n", debug_prefix_time(NULL), buf));
}

/* send one line of a multi-line response */
printf_arglist_function1(static void lreply, int, n, char *, fmt)
{
    va_list args;
    char buf[str_buffer_size];

    arglist_start(args, fmt);
    snprintf(buf, sizeof(buf), "%03d-", n);
    vsnprintf(buf+4, sizeof(buf)-4, fmt, args);
    arglist_end(args);

    if (printf("%s\r\n", buf) < 0)
    {
	dbprintf(("%s: ! error %d (%s) in printf\n",
		  debug_prefix_time(NULL), errno, strerror(errno)));
	uncompress_remove = remove_files(uncompress_remove);
	exit(1);
    }
    if (fflush(stdout) != 0)
    {
	dbprintf(("%s: ! error %d (%s) in fflush\n",
		  debug_prefix_time(NULL), errno, strerror(errno)));
	uncompress_remove = remove_files(uncompress_remove);
	exit(1);
    }

    dbprintf(("%s: < %s\n", debug_prefix_time(NULL), buf));
}

/* send one line of a multi-line response */
printf_arglist_function1(static void fast_lreply, int, n, char *, fmt)
{
    va_list args;
    char buf[str_buffer_size];

    arglist_start(args, fmt);
    snprintf(buf, sizeof(buf), "%03d-", n);
    vsnprintf(buf+4, sizeof(buf)-4, fmt, args);
    arglist_end(args);

    if (printf("%s\r\n", buf) < 0)
    {
	dbprintf(("%s: ! error %d (%s) in printf\n",
		  debug_prefix_time(NULL), errno, strerror(errno)));
	uncompress_remove = remove_files(uncompress_remove);
	exit(1);
    }
}

/* see if hostname is valid */
/* valid is defined to be that there is an index directory for it */
/* also do a security check on the requested dump hostname */
/* to restrict access to index records if required */
/* return -1 if not okay */
static int is_dump_host_valid(host)
char *host;
{
    struct stat dir_stat;
    char *fn;
    am_host_t *ihost;

    if (config_name == NULL) {
	reply(501, "Must set config before setting host.");
	return -1;
    }

#if 0
    /* only let a client restore itself for now unless it is the server */
    if (strcasecmp(remote_hostname, local_hostname) == 0)
	return 0;
    if (strcasecmp(remote_hostname, host) != 0)
    {
	reply(501,
	      "You don't have the necessary permissions to set dump host to %s.",
	      buf1);
	return -1;
    }
#endif

    /* check that the config actually handles that host */
    ihost = lookup_host(host);
    if(ihost == NULL) {
	reply(501, "Host %s is not in your disklist.", host);
	return -1;
    }

    /* assume an index dir already */
    fn = getindexfname(host, NULL, NULL, 0);
    if (stat (fn, &dir_stat) != 0 || !S_ISDIR(dir_stat.st_mode)) {
	reply(501, "No index records for host: %s. Have you enabled indexing?", host);
	amfree(fn);
	return -1;
    }

    amfree(fn);
    return 0;
}


static int is_disk_valid(disk)
char *disk;
{
    char *fn;
    struct stat dir_stat;
    disk_t *idisk;

    if (config_name == NULL || dump_hostname == NULL) {
	reply(501, "Must set config,host before setting disk.");
	return -1;
    }

    /* check that the config actually handles that disk */
    idisk = lookup_disk(dump_hostname, disk);
    if(idisk == NULL) {
	reply(501, "Disk %s:%s is not in your disklist.", dump_hostname, disk);
	return -1;
    }

    /* assume an index dir already */
    fn = getindexfname(dump_hostname, disk, NULL, 0);
    if (stat (fn, &dir_stat) != 0 || !S_ISDIR(dir_stat.st_mode)) {
	reply(501, "No index records for disk: %s. Invalid?", disk);
	amfree(fn);
	return -1;
    }

    amfree(fn);
    return 0;
}


static int is_config_valid(config)
char *config;
{
    char *conffile;
    char *conf_diskfile;
    char *conf_tapelist;
    char *conf_indexdir;
    struct stat dir_stat;

    /* check that the config actually exists */
    if (config == NULL) {
	reply(501, "Must set config first.");
	return -1;
    }

    /* read conffile */
    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if (read_conffile(conffile)) {
	reply(501, "Could not read config file %s!", conffile);
	amfree(conffile);
	return -1;
    }
    amfree(conffile);

    conf_diskfile = getconf_str(CNF_DISKFILE);
    if (*conf_diskfile == '/') {
	conf_diskfile = stralloc(conf_diskfile);
    } else {
	conf_diskfile = stralloc2(config_dir, conf_diskfile);
    }
    if (read_diskfile(conf_diskfile, &disk_list) < 0) {
	reply(501, "Could not read disk file %s!", conf_diskfile);
	amfree(conf_diskfile);
	return -1;
    }
    amfree(conf_diskfile);

    conf_tapelist = getconf_str(CNF_TAPELIST);
    if (*conf_tapelist == '/') {
	conf_tapelist = stralloc(conf_tapelist);
    } else {
	conf_tapelist = stralloc2(config_dir, conf_tapelist);
    }
    if(read_tapelist(conf_tapelist)) {
	reply(501, "Could not read tapelist file %s!", conf_tapelist);
	amfree(conf_tapelist);
	return -1;
    }
    amfree(conf_tapelist);

    output_find = find_dump(1, &disk_list);
    sort_find_result("DLKHpB", &output_find);

    conf_indexdir = getconf_str(CNF_INDEXDIR);
    if(*conf_indexdir == '/') {
	conf_indexdir = stralloc(conf_indexdir);
    } else {
	conf_indexdir = stralloc2(config_dir, conf_indexdir);
    }
    if (stat (conf_indexdir, &dir_stat) != 0 || !S_ISDIR(dir_stat.st_mode)) {
	reply(501, "Index directory %s does not exist", conf_indexdir);
	amfree(conf_indexdir);
	return -1;
    }
    amfree(conf_indexdir);

    return 0;
}


static int build_disk_table()
{
    char date[3 * NUM_STR_SIZE + 2 + 1];
    long last_datestamp;
    int last_filenum;
    int last_level;
    int last_partnum;
    find_result_t *find_output;

    if (config_name == NULL || dump_hostname == NULL || disk_name == NULL) {
	reply(590, "Must set config,host,disk before building disk table");
	return -1;
    }

    clear_list();
    last_datestamp = -1;
    last_filenum = -1;
    last_level = -1;
    last_partnum = -1;
    split_support_warn = 0;
    for(find_output = output_find;
	find_output != NULL; 
	find_output = find_output->next) {
	if(strcasecmp(dump_hostname, find_output->hostname) == 0 &&
	   strcmp(disk_name    , find_output->diskname) == 0 &&
	   strcmp("OK"         , find_output->status)   == 0) {
	    int partnum = -1;
	    if(strcmp("--", find_output->partnum)){
		partnum = atoi(find_output->partnum);
		if(!am_has_feature(their_features, fe_recover_splits)){
		    split_support_warn = 1;
		}
	    }
	    /*
	     * The sort order puts holding disk entries first.  We want to
	     * use them if at all possible, so ignore any other entries
	     * for the same datestamp after we see a holding disk entry
	     * (as indicated by a filenum of zero).
	     */
	    if(find_output->datestamp == last_datestamp &&
	       find_output->level == last_level && 
	       partnum == last_partnum && last_filenum == 0) {
		continue;
	    }
	    last_datestamp = find_output->datestamp;
	    last_filenum = find_output->filenum;
	    last_level = find_output->level;
	    last_partnum = partnum;
	    snprintf(date, sizeof(date), "%04d-%02d-%02d",
			find_output->datestamp/10000,
			(find_output->datestamp/100) %100,
			find_output->datestamp %100);
	    add_dump(date, find_output->level, find_output->label, 
		     find_output->filenum, partnum);
	    dbprintf(("%s: - %s %d %s %d %d\n",
		     debug_prefix_time(NULL), date, find_output->level, 
		     find_output->label, find_output->filenum, partnum));
	}
    }
    return 0;
}


static int disk_history_list()
{
    DUMP_ITEM *item;

    if (config_name == NULL || dump_hostname == NULL || disk_name == NULL) {
	reply(502, "Must set config,host,disk before listing history");
	return -1;
    }

    lreply(200, " Dump history for config \"%s\" host \"%s\" disk \"%s\"",
	  config_name, dump_hostname, disk_name);

    for (item=first_dump(); item!=NULL; item=next_dump(item)){
        char *tapelist_str = marshal_tapelist(item->tapes, 1);

	if(am_has_feature(their_features, fe_recover_splits)){
	    str_buffer_size = strlen(item->date) + NUM_STR_SIZE +
	                      strlen(tapelist_str) + 9;
	    lreply(201, " %s %d %s", item->date, item->level, tapelist_str);
	}
	else{
	    str_buffer_size = strlen(item->date) + NUM_STR_SIZE +
	                      strlen(tapelist_str) + NUM_STR_SIZE + 9;
	    lreply(201, " %s %d %s %d", item->date, item->level, tapelist_str,
	       item->file);
	}
	str_buffer_size = STR_SIZE;
    }

    reply(200, "Dump history for config \"%s\" host \"%s\" disk \"%s\"",
	  config_name, dump_hostname, disk_name);

    return 0;
}


/* is the directory dir backed up - dir assumed complete relative to
   disk mount point */
/* opaque version of command */
static int is_dir_valid_opaque(dir)
char *dir;
{
    DUMP_ITEM *item;
    char *line = NULL;
    FILE *fp;
    int last_level;
    char *ldir = NULL;
    char *filename_gz = NULL;
    char *filename = NULL;
    int ldir_len;
    static char *emsg = NULL;

    if (config_name == NULL || dump_hostname == NULL || disk_name == NULL) {
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
	amfree(filename);
	filename_gz = getindexfname(dump_hostname, disk_name,
				    item->date, item->level);
	if((filename = uncompress_file(filename_gz, &emsg)) == NULL) {
	    reply(599, "System error %s", emsg);
	    amfree(filename_gz);
	    amfree(emsg);
	    amfree(ldir);
	    return -1;
	}
	amfree(filename_gz);
	dbprintf(("%s: f %s\n", debug_prefix_time(NULL), filename));
	if ((fp = fopen(filename, "r")) == NULL) {
	    reply(599, "System error %s", strerror(errno));
	    amfree(filename);
	    amfree(ldir);
	    return -1;
	}
	for(; (line = agets(fp)) != NULL; free(line)) {
	    if (strncmp(line, ldir, ldir_len) != 0) {
		continue;			/* not found yet */
	    }
	    amfree(filename);
	    amfree(ldir);
	    amfree(line);
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

    amfree(filename);
    amfree(ldir);
    reply(500, "\"%s\" is an invalid directory", dir);
    return -1;
}

static int opaque_ls(dir,recursive)
char *dir;
int  recursive;
{
    DUMP_ITEM *dump_item;
    DIR_ITEM *dir_item;
    int last_level;
    static char *emsg = NULL;

    clear_dir_list();

    if (config_name == NULL || dump_hostname == NULL || disk_name == NULL) {
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
    if (process_ls_dump(dir, dump_item, recursive, &emsg) == -1) {
	reply(599, "System error %s", emsg);
	amfree(emsg);
	return -1;
    }

    /* go back processing higher level dumps till we hit a level 0 dump */
    last_level = dump_item->level;
    while ((last_level != 0) && ((dump_item=next_dump(dump_item)) != NULL))
    {
	if (dump_item->level < last_level)
	{
	    last_level = dump_item->level;
	    if (process_ls_dump(dir, dump_item, recursive, &emsg) == -1) {
		reply(599, "System error %s", emsg);
		amfree(emsg);
		return -1;
	    }
	}
    }

    /* return the information to the caller */
    if(recursive)
    {
	lreply(200, " Opaque recursive list of %s", dir);
	for (dir_item = get_dir_list(); dir_item != NULL; 
	     dir_item = dir_item->next) {
	    char *tapelist_str = marshal_tapelist(dir_item->dump->tapes, 1);

	    if(am_has_feature(their_features, fe_amindexd_fileno_in_ORLD)){
		str_buffer_size = strlen(dir_item->dump->date) + NUM_STR_SIZE +
		              strlen(tapelist_str) + strlen(dir_item->path) +
			      NUM_STR_SIZE + 9;
		fast_lreply(201, " %s %d %s %d %s",
			    dir_item->dump->date, dir_item->dump->level,
			    tapelist_str, dir_item->dump->file,
			    dir_item->path);
	    }
	    else {
		str_buffer_size = strlen(dir_item->dump->date) + NUM_STR_SIZE +
		              strlen(tapelist_str) + strlen(dir_item->path) + 9;
		fast_lreply(201, " %s %d %s %s",
			    dir_item->dump->date, dir_item->dump->level,
			    tapelist_str, dir_item->path);
	    }
	    str_buffer_size = STR_SIZE;
	}
	reply(200, " Opaque recursive list of %s", dir);
    }
    else
    {
	lreply(200, " Opaque list of %s", dir);
	for (dir_item = get_dir_list(); dir_item != NULL; 
	     dir_item = dir_item->next) {
	    char *tapelist_str = marshal_tapelist(dir_item->dump->tapes, 1);

	    if(am_has_feature(their_features, fe_amindexd_fileno_in_OLSD)){
		str_buffer_size = strlen(dir_item->dump->date) + NUM_STR_SIZE +
		              strlen(tapelist_str) + strlen(dir_item->path) +
			      NUM_STR_SIZE + 9;
		lreply(201, " %s %d %s %d %s",
			    dir_item->dump->date, dir_item->dump->level,
			    tapelist_str, dir_item->dump->file,
			    dir_item->path);
	    }
	    else {
		str_buffer_size = strlen(dir_item->dump->date) + NUM_STR_SIZE +
		              strlen(tapelist_str) + strlen(dir_item->path) + 9;
		lreply(201, " %s %d %s %s",
			    dir_item->dump->date, dir_item->dump->level,
			    tapelist_str, dir_item->path);
	    }
	    str_buffer_size = STR_SIZE;
	}
	reply(200, " Opaque list of %s", dir);
    }
    clear_dir_list();
    return 0;
}


/* returns the value of tapedev from the amanda.conf file if set,
   otherwise reports an error */
static int tapedev_is()
{
    char *result;

    /* check state okay to do this */
    if (config_name == NULL) {
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
static int are_dumps_compressed()
{
    disk_t *diskp;

    /* check state okay to do this */
    if (config_name == NULL || dump_hostname == NULL || disk_name == NULL) {
	reply(501, "Must set config,host,disk name before asking about dumps.");
	return -1;
    }

    /* now go through the list of disks and find which have indexes */
    for (diskp = disk_list.head; diskp != NULL; diskp = diskp->next)
	if ((strcasecmp(diskp->host->hostname, dump_hostname) == 0)
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
    char *line = NULL, *part = NULL;
    char *s, *fp;
    int ch;
    char *cmd_undo, cmd_undo_ch;
    socklen_t socklen;
    struct sockaddr_in his_addr;
    struct hostent *his_name;
    char *arg;
    char *cmd;
    int len;
    int user_validated = 0;
    char *errstr = NULL;
    char *pgm = "amindexd";			/* in case argv[0] is not set */

    safe_fd(-1, 0);
    safe_cd();

    /*
     * When called via inetd, it is not uncommon to forget to put the
     * argv[0] value on the config line.  On some systems (e.g. Solaris)
     * this causes argv and/or argv[0] to be NULL, so we have to be
     * careful getting our name.
     */
    if (argc >= 1 && argv != NULL && argv[0] != NULL) {
	if((pgm = strrchr(argv[0], '/')) != NULL) {
	    pgm++;
	} else {
	    pgm = argv[0];
	}
    }

    set_pname(pgm);

#ifdef FORCE_USERID

    /* we'd rather not run as root */

    if(geteuid() == 0) {
	if(client_uid == (uid_t) -1) {
	    error("error [cannot find user %s in passwd file]\n", CLIENT_LOGIN);
	}

	initgroups(CLIENT_LOGIN, client_gid);
	setgid(client_gid);
	setuid(client_uid);
    }

#endif	/* FORCE_USERID */

    dbopen();
    dbprintf(("%s: version %s\n", get_pname(), version()));

    if (! (argc >= 1 && argv != NULL && argv[0] != NULL)) {
	dbprintf(("%s: WARNING: argv[0] not defined: check inetd.conf\n",
		  debug_prefix_time(NULL)));
    }

    {
	int db_fd = dbfd();
	if(db_fd != -1) {
	    dup2(db_fd, 2);
	}
    }

    /* initialize */

    argc--;
    argv++;

    if(argc > 0 && strcmp(*argv, "-t") == 0) {
	amindexd_debug = 1;
	argc--;
	argv++;
    }

    if (argc > 0) {
	config_name = stralloc(*argv);
	config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
	argc--;
	argv++;
    }

    if(gethostname(local_hostname, sizeof(local_hostname)-1) == -1)
	error("gethostname: %s", strerror(errno));
    local_hostname[sizeof(local_hostname)-1] = '\0';

    /* now trim domain off name */
    s = local_hostname;
    ch = *s++;
    while(ch && ch != '.') ch = *s++;
    s[-1] = '\0';

    if(amindexd_debug) {
	/*
	 * Fake the remote address as the local address enough to get
	 * through the security check.
	 */
	his_name = gethostbyname(local_hostname);
	if(his_name == NULL) {
	    error("gethostbyname(%s) failed\n", local_hostname);
	}
	assert(his_name->h_addrtype == AF_INET);
	his_addr.sin_family = his_name->h_addrtype;
	his_addr.sin_port = htons(0);
	memcpy((char *)&his_addr.sin_addr.s_addr,
	       (char *)his_name->h_addr_list[0], his_name->h_length);
    } else {
	/* who are we talking to? */
	socklen = sizeof (his_addr);
	if (getpeername(0, (struct sockaddr *)&his_addr, &socklen) == -1)
	    error("getpeername: %s", strerror(errno));
    }
    if (his_addr.sin_family != AF_INET || ntohs(his_addr.sin_port) == 20)
    {
	error("connection rejected from %s family %d port %d",
	      inet_ntoa(his_addr.sin_addr), his_addr.sin_family,
	      htons(his_addr.sin_port));
    }
    if ((his_name = gethostbyaddr((char *)&(his_addr.sin_addr),
				  sizeof(his_addr.sin_addr),
				  AF_INET)) == NULL) {
	error("gethostbyaddr(%s): hostname lookup failed",
	      inet_ntoa(his_addr.sin_addr));
    }
    fp = s = his_name->h_name;
    ch = *s++;
    while(ch && ch != '.') ch = *s++;
    s[-1] = '\0';
    remote_hostname = newstralloc(remote_hostname, fp);
    s[-1] = ch;

    /* clear these so we can detect when the have not been set by the client */
    amfree(dump_hostname);
    amfree(disk_name);
    amfree(target_date);

    our_features = am_init_feature_set();
    their_features = am_set_default_feature_set();

    if (config_name != NULL && is_config_valid(config_name) != -1) {
	return 1;
    }

    reply(220, "%s AMANDA index server (%s) ready.", local_hostname,
	  version());

    /* a real simple parser since there are only a few commands */
    while (1)
    {
	/* get a line from the client */
	amfree(line);
	while(1) {
	    if((part = agets(stdin)) == NULL) {
		if(errno != 0) {
		    dbprintf(("%s: ? read error: %s\n",
			      debug_prefix_time(NULL), strerror(errno)));
		} else {
		    dbprintf(("%s: ? unexpected EOF\n",
			      debug_prefix_time(NULL)));
		}
		if(line) {
		    dbprintf(("%s: ? unprocessed input:\n",
			      debug_prefix_time(NULL)));
		    dbprintf(("-----\n"));
		    dbprintf(("? %s\n", line));
		    dbprintf(("-----\n"));
		}
		amfree(line);
		amfree(part);
		uncompress_remove = remove_files(uncompress_remove);
		dbclose();
		return 1;		/* they hung up? */
	    }
	    if(line) {
		strappend(line, part);
		amfree(part);
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
	     * Hmmm.  We got a "line" from agets(), which means it saw
	     * a '\n' (or EOF, etc), but there was not a '\r' before it.
	     * Put a '\n' back in the buffer and loop for more.
	     */
	    strappend(line, "\n");
	}

	dbprintf(("%s: > %s\n", debug_prefix_time(NULL), line));

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
	    }
	}

	amfree(errstr);
	if (!user_validated && strcmp(cmd, "SECURITY") == 0 && arg) {
	    user_validated = check_security(&his_addr, arg, 0, &errstr);
	    if(user_validated) {
		reply(200, "Access OK");
		continue;
	    }
	}
	if (!user_validated) {
	    if (errstr) {
		reply(500, "Access not allowed: %s", errstr);
	    } else {
		reply(500, "Access not allowed");
	    }
	    break;
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
		amfree(disk_name);		/* invalidate any value */
	    }
	    s[-1] = ch;
	} else if (strcmp(cmd, "DISK") == 0 && arg) {
	    s[-1] = '\0';
	    if (is_disk_valid(arg) != -1) {
		disk_name = newstralloc(disk_name, arg);
		if (build_disk_table() != -1) {
		    if(split_support_warn)
			/* XXX is there a non-asinine way to do this? */
			reply(200, "Disk set to %s.\nWARNING- your client does not support split dumps, this may or may not work.\nYou should upgrade, if possible.", disk_name);
		    else
		    reply(200, "Disk set to %s.", disk_name);
		}
	    }
	    s[-1] = ch;
	} else if (strcmp(cmd, "LISTDISK") == 0) {
	    disk_t *disk;
	    int nbdisk = 0;
	    s[-1] = '\0';
	    if (config_name == NULL || dump_hostname == NULL) {
		reply(501, "Must set config, host before listdisk");
	    }
	    else if(arg) {
		lreply(200, " List of disk for device %s on host %s", arg,
		       dump_hostname);
		for (disk = disk_list.head; disk!=NULL; disk = disk->next) {
		    if(strcmp(disk->host->hostname, dump_hostname) == 0 &&
		       ((disk->device && strcmp(disk->device, arg) == 0) ||
			(!disk->device && strcmp(disk->name, arg) == 0))) {
			fast_lreply(201, " %s", disk->name);
			nbdisk++;
		    }
		}
		if(nbdisk > 0) {
		    reply(200, "List of disk for device %s on host %s", arg,
			  dump_hostname);
		}
		else {
		    reply(200, "No disk for device %s on host %s", arg,
			  dump_hostname);
		}
	    }
	    else {
		lreply(200, " List of disk for host %s", dump_hostname);
		for (disk = disk_list.head; disk!=NULL; disk = disk->next) {
		    if(strcmp(disk->host->hostname, dump_hostname) == 0) {
			fast_lreply(201, " %s", disk->name);
			nbdisk++;
		    }
		}
		if(nbdisk > 0) {
		    reply(200, "List of disk for host %s", dump_hostname);
		}
		else {
		    reply(200, "No disk for host %s", dump_hostname);
		}
	    }
	    s[-1] = ch;
	} else if (strcmp(cmd, "SCNF") == 0 && arg) {
	    s[-1] = '\0';
	    amfree(config_name);
	    amfree(config_dir);
	    config_name = newstralloc(config_name, arg);
	    config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
	    if (is_config_valid(arg) != -1) {
		amfree(dump_hostname);		/* invalidate any value */
		amfree(disk_name);		/* invalidate any value */
		reply(200, "Config set to %s.", config_name);
	    } else {
		amfree(config_name);
		amfree(config_dir);
	    }
	    s[-1] = ch;
	} else if (strcmp(cmd, "FEATURES") == 0 && arg) {
	    char *our_feature_string = NULL;
	    char *their_feature_string = NULL;
	    s[-1] = '\0';
	    am_release_feature_set(our_features);
	    am_release_feature_set(their_features);
	    our_features = am_init_feature_set();
	    our_feature_string = am_feature_to_string(our_features);
	    their_feature_string = newstralloc(target_date, arg);
	    their_features = am_string_to_feature(their_feature_string);
	    reply(200, "FEATURES %s", our_feature_string);
	    amfree(our_feature_string);
	    amfree(their_feature_string);
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
    amfree(line);

    uncompress_remove = remove_files(uncompress_remove);
    free_find_result(&output_find);
    reply(200, "Good bye.");
    dbclose();
    return 0;
}

static int
check_security(addr, str, cksum, errstr)
struct sockaddr_in *addr;
char *str;
unsigned long cksum;
char **errstr;
{
    char *remotehost = NULL, *remoteuser = NULL;
    char *bad_bsd = NULL;
    struct hostent *hp;
    struct passwd *pwptr;
    int myuid, i, j;
    char *s, *fp;
    int ch;

    *errstr = NULL;

    /* what host is making the request? */

    hp = gethostbyaddr((char *)&addr->sin_addr, sizeof(addr->sin_addr),
		       AF_INET);
    if(hp == NULL) {
	/* XXX include remote address in message */
	*errstr = vstralloc("[",
			    "addr ", inet_ntoa(addr->sin_addr), ": ",
			    "hostname lookup failed",
			    "]", NULL);
	return 0;
    }
    remotehost = stralloc(hp->h_name);

    /* Now let's get the hostent for that hostname */
    hp = gethostbyname( remotehost );
    if(hp == NULL) {
	/* XXX include remote hostname in message */
	*errstr = vstralloc("[",
			    "host ", remotehost, ": ",
			    "hostname lookup failed",
			    "]", NULL);
	amfree(remotehost);
	return 0;
    }

    /* Verify that the hostnames match -- they should theoretically */
    if( strncasecmp( remotehost, hp->h_name, strlen(remotehost)+1 ) != 0 ) {
	*errstr = vstralloc("[",
			    "hostnames do not match: ",
			    remotehost, " ", hp->h_name,
			    "]", NULL);
	amfree(remotehost);
	return 0;
    }

    /* Now let's verify that the ip which gave us this hostname
     * is really an ip for this hostname; or is someone trying to
     * break in? (THIS IS THE CRUCIAL STEP)
     */
    for (i = 0; hp->h_addr_list[i]; i++) {
	if (memcmp(hp->h_addr_list[i],
		   (char *) &addr->sin_addr, sizeof(addr->sin_addr)) == 0)
	    break;                     /* name is good, keep it */
    }

    /* If we did not find it, your DNS is messed up or someone is trying
     * to pull a fast one on you. :(
     */

   /*   Check even the aliases list. Work around for Solaris if dns goes over NIS */

    if( !hp->h_addr_list[i] ) {
        for (j = 0; hp->h_aliases[j] !=0 ; j++) {
	     if ( strcmp(hp->h_aliases[j],inet_ntoa(addr->sin_addr)) == 0)
	         break;                          /* name is good, keep it */
        }
	if( !hp->h_aliases[j] ) {
	    *errstr = vstralloc("[",
			        "ip address ", inet_ntoa(addr->sin_addr),
			        " is not in the ip list for ", remotehost,
			        "]",
			        NULL);
	    amfree(remotehost);
	    return 0;
	}
    }

    /* next, make sure the remote port is a "reserved" one */

    if(ntohs(addr->sin_port) >= IPPORT_RESERVED) {
	char number[NUM_STR_SIZE];

	snprintf(number, sizeof(number), "%d", ntohs(addr->sin_port));
	*errstr = vstralloc("[",
			    "host ", remotehost, ": ",
			    "port ", number, " not secure",
			    "]", NULL);
	amfree(remotehost);
	return 0;
    }

    /* extract the remote user name from the message */

    s = str;
    ch = *s++;

    bad_bsd = vstralloc("[",
			"host ", remotehost, ": ",
			"bad bsd security line",
			"]", NULL);

#define sc "USER "
    if(strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	*errstr = bad_bsd;
	bad_bsd = NULL;
	amfree(remotehost);
	return 0;
    }
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc

    skip_whitespace(s, ch);
    if(ch == '\0') {
	*errstr = bad_bsd;
	bad_bsd = NULL;
	amfree(remotehost);
	return 0;
    }
    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    remoteuser = stralloc(fp);
    s[-1] = ch;
    amfree(bad_bsd);

    /* lookup our local user name */

    myuid = getuid();
    if((pwptr = getpwuid(myuid)) == NULL)
        error("error [getpwuid(%d) fails]", myuid);

    dbprintf(("bsd security: remote host %s user %s local user %s\n",
	      remotehost, remoteuser, pwptr->pw_name));

#ifndef USE_AMANDAHOSTS
    s = check_user_ruserok(remotehost, pwptr, remoteuser);
#else
    s = check_user_amandahosts(remotehost, pwptr, remoteuser);
#endif
    if (s != NULL) {
	*errstr = vstralloc("[",
			    "access as ", pwptr->pw_name, " not allowed",
			    " from ", remoteuser, "@", remotehost,
			    ": ", s, "]", NULL);
    }
    return s == NULL;
}
