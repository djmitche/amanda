/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1994 University of Maryland
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
 * amflush.c - write files from work directory onto tape
 */
#include "amanda.h"

#include "conffile.h"
#include "diskfile.h"
#include "tapefile.h"
#include "logfile.h"
#include "clock.h"
#include "version.h"

#define MAX_ARGS 10
#define MAX_LINE 1024

/* define schedule structure */

typedef struct sched_s {
    int level;			/* dump level */
    char destname[128];		/* file name */
} sched_t;

#define sched(dp)	((sched_t *) (dp)->up)

/* command/result tokens */

typedef enum {
    BOGUS, QUIT, DONE,
    FILE_DUMP, PORT_DUMP, CONTINUE, ABORT,		/* dumper cmds */
    FAILED, TRYAGAIN, NO_ROOM, ABORT_FINISHED,	 	/* dumper results */
    START_TAPER, FILE_WRITE, PORT_WRITE, 		/* taper cmds */
    PORT, TAPE_ERROR, TAPER_OK,				/* taper results */
    LAST_TOK
} tok_t;

char *cmdstr[] = {
    "BOGUS", "QUIT", "DONE",
    "FILE-DUMP", "PORT-DUMP", "CONTINUE", "ABORT",	/* dumper cmds */
    "FAILED", "TRY-AGAIN", "NO-ROOM", "ABORT-FINISHED",	/* dumper results */
    "START-TAPER", "FILE-WRITE", "PORT-WRITE", 		/* taper cmds */
    "PORT", "TAPE-ERROR", "TAPER-OK",			/* taper results */
    NULL
};

tok_t tok;
    
char *pname = "amflush";

int taper, taper_pid;

disklist_t *diskqp;

int result_argc;
char *result_argv[MAX_ARGS];
static char *config;
char datestamp[80];
char confdir[256];
char taper_program[80], reporter_program[80];
char host[MAX_HOSTNAME_LENGTH], *domain;

/* local functions */
int main P((int argc, char **argv));
int is_dir P((char *fname));
int is_emptyfile P((char *fname));
int is_datestr P((char *fname));
int non_empty P((char *fname));
struct dirname *insert_dirname P((char *name));
char get_letter_from_user P((void));
int select_dir P((void));
void scan_holdingdisk P((char *diskdir));
void flush_holdingdisk P((char *diskdir));
static void startup_tape_process P((void));
tok_t getresult P((int fd));
void taper_cmd P((tok_t cmd, void *ptr, char *destname, int level));
void pick_datestamp P((void));
void confirm P((void));
void detach P((void));
void run_dumps P((void));
int get_amanda_names P((char *fname, char *hostname, char *diskname,
			int *level));


int main(argc, argv)
int argc;
char **argv;
{
    int foreground;
    struct passwd *pw;
    char *dumpuser;

    erroutput_type = ERR_INTERACTIVE;
    foreground = 0;

    if(argc > 1 && !strcmp(argv[1], "-f")) {
	foreground = 1;
	argc--,argv++;
    }

    if(argc != 2) 
	error("Usage: amflush%s [-f] <confdir>", versionsuffix());

    config = argv[1];
    sprintf(confdir, "%s/%s", CONFIG_DIR, argv[1]);
    if(chdir(confdir) != 0)
	error("could not cd to confdir %s: %s",	confdir, strerror(errno));

    if(read_conffile(CONFFILE_NAME))
	error("could not read amanda config file\n");

    if((diskqp = read_diskfile(getconf_str(CNF_DISKFILE))) == NULL)
        error("could not read disklist file\n");

    if(read_tapelist(getconf_str(CNF_TAPELIST)))
        error("parse error in %s", getconf_str(CNF_TAPELIST));

    dumpuser = getconf_str(CNF_DUMPUSER);
    if((pw = getpwnam(dumpuser)) == NULL)
	error("dumpuser %s not found in password file", dumpuser);
    if(pw->pw_uid != getuid())
	error("must run amflush as user %s", dumpuser);

    /* host[sizeof(host)-1] = '\0';*/ /* not necessary when host[] is static */
    if(gethostname(host, sizeof(host)-1) == -1)
	error("gethostname failed: %s", strerror(errno));
    if((domain = strchr(host, '.'))) domain++;

    sprintf(taper_program, "%s/taper%s", libexecdir, versionsuffix());
    sprintf(reporter_program, "%s/reporter%s", libexecdir, versionsuffix());

    pick_datestamp();
    confirm();
    if(!foreground) detach();
    erroutput_type = (ERR_AMANDALOG|ERR_INTERACTIVE);
    run_dumps();
    return 0;
}


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
    int num, date, year, month;

    /* must be 8 digits */
    for(cp = fname; *cp; cp++)
	if(!isdigit(*cp)) break;
    if(*cp != '\0' || cp-fname != 8) return 0;

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
    while(!gotentry && (entry = readdir(dir)) != NULL)
	gotentry = strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..");

    closedir(dir);
    return gotentry;
}

#define MAX_DIRS 26	/* so we can select them A .. Z */

struct dirname {
    struct dirname *next;
    char *name;
} *dir_list = NULL;
int ndirs = 0;

struct dirname *insert_dirname(name)
char *name;
{
    struct dirname *d, *p, *n;
    int cmp;

    for(p = NULL, d = dir_list; d != NULL; p = d, d = d->next)
	if((cmp = strcmp(name, d->name)) > 0) continue;
        else if(cmp == 0) return d;
	else break;

    if(ndirs == MAX_DIRS)
	return NULL;

    ndirs++;
    n = (struct dirname *)alloc(sizeof(struct dirname));
    n->name = stralloc(name);
    n->next = d;
    if(p) p->next = n;
    else dir_list = n;
    return n;
}

char get_letter_from_user()
{
    char ch;
    char line[MAX_LINE];

    fflush(stdout); fflush(stderr);
    if(fgets(line, MAX_LINE, stdin) == NULL) {
	printf("\nGot EOF.  Goodbye.\n");
	exit(1);
    }
    /* zap leading whitespace */
    if(sscanf(line, " %c", &ch) < 1) ch = '\0';	/* no char on line */

    if(islower(ch)) ch = toupper(ch);
    return ch;
}

int select_dir()
{
    int i;
    char ch;
    struct dirname *dir;

    while(1) {
	puts("\nMultiple Amanda directories, please pick one by letter:");
	for(dir = dir_list, i = 0; dir != NULL; dir = dir->next, i++)
	    printf("  %c. %s\n", 'A'+i, dir->name);
	printf("Select a directory to flush [A..%c]: ", 'A' + ndirs - 1);
	ch = get_letter_from_user();
	if(ch < 'A' || ch > 'A' + ndirs - 1)
	    printf("That is not a valid answer.  Try again, or ^C to quit.\n");
	else
	    return ch - 'A';
    }
}

void scan_holdingdisk(diskdir)
char *diskdir;
{
    DIR *topdir;
    struct dirent *workdir;

    if((topdir = opendir(diskdir)) == NULL)
	error("could not open holding dir %s: %s", diskdir, strerror(errno));

    /* find all directories of the right format  */

    printf("Scanning %s...\n", diskdir);
    chdir(diskdir);
    while((workdir = readdir(topdir)) != NULL) {
	if(!strcmp(workdir->d_name, ".") || !strcmp(workdir->d_name, ".."))
	    continue;

	printf("  %s: ", workdir->d_name);
	if(!is_dir(workdir->d_name))
	    puts("skipping cruft file, perhaps you should delete it.");
	else if(!is_datestr(workdir->d_name))
	    puts("skipping cruft directory, perhaps you should delete it.");
	else if(rmdir(workdir->d_name) == 0)
	    puts("deleted empty Amanda directory.");
	else {
	    if(insert_dirname(workdir->d_name) == NULL)
		puts("too many non-empty Amanda dirs, can't handle this one.");
	    else
		puts("found non-empty Amanda directory.");
	}
    }
    closedir(topdir);
}

void pick_datestamp()
{
    holdingdisk_t *hdisk;
    struct dirname *dir;
    int picked;

    for(hdisk = holdingdisks; hdisk != NULL; hdisk = hdisk->next)
	scan_holdingdisk(hdisk->diskdir);

    if(ndirs == 0) {
	puts("Could not find any Amanda directories to flush.");
	exit(1);
    }
    else if(ndirs > 1) picked = select_dir();
    else picked = 0;

    for(dir = dir_list; dir != NULL; dir = dir->next)
	if(picked-- == 0) break;

    strcpy(datestamp, dir->name);
}

void confirm()
/* confirm before detaching and running */
{
    tape_t *tp;
    char *tpchanger;

    printf("\nFlushing dumps in %s ", datestamp);
    tpchanger = getconf_str(CNF_TPCHANGER);
    if(*tpchanger != '\0') printf("using tape changer \"%s\".\n", tpchanger);
    else printf("to tape drive %s.\n", getconf_str(CNF_TAPEDEV));

    printf("Expecting ");
    tp = lookup_tapepos(getconf_int(CNF_TAPECYCLE));
    if(tp != NULL) printf("tape %s or ", tp->label);
    printf("a new tape.");
    tp = lookup_tapepos(1);
    if(tp != NULL) printf("  (The last dumps were to tape %s)", tp->label);

    printf("\nAre you sure you want to do this? ");
    if(get_letter_from_user() == 'Y') return;

    printf("Ok, quitting.  Run amflush again when you are ready.\n");
    exit(1);
}

void detach()
{
    int fd;
    
    fflush(stdout); fflush(stderr);
    if((fd = open("/dev/null", O_RDWR, 0666)) == -1)
	error("could not open /dev/null: %s", strerror(errno));
	
    switch(fork()) {
    case -1: error("could not fork: %s", strerror(errno));
    case 0:
	dup2(fd,0);
	dup2(fd,1);
	dup2(fd,2);
	close(fd);
	setsid();
	return;
    }

    puts("Running in background, you can log off now.");
    puts("You'll get mail when amflush is finished.");
    exit(0);
}


void flush_holdingdisk(diskdir)
char *diskdir;
{
    DIR *workdir;
    struct dirent *entry;
    char dirname[80], destname[128], hostname[256], diskname[80];
    int level;
    disk_t *dp;

    sprintf(dirname, "%s/%s", diskdir, datestamp);

    if((workdir = opendir(dirname)) == NULL) {
	log(L_INFO, "%s: could not open working dir: %s",
	    dirname, strerror(errno));
	return;
    }
    chdir(dirname);

    while((entry = readdir(workdir)) != NULL) {
	if(!strcmp(entry->d_name, ".") ||  !strcmp(entry->d_name, ".."))
	    continue;

	if(is_emptyfile(entry->d_name)) {
	    if(unlink(entry->d_name) == -1)
		log(L_INFO,"%s: ignoring zero length file.", entry->d_name);
	    else 
		log(L_INFO,"%s: removed zero length file.", entry->d_name);
	    continue;
	}

	sprintf(destname, "%s/%s", dirname, entry->d_name);

	if(get_amanda_names(destname, hostname, diskname, &level)) {
	    log(L_INFO, "%s: ignoring cruft file.", entry->d_name);
	    continue;
	}

	/*
	** Old Behavior: send-backup would put the first token of the
	** hostname in the dump header.  amflush would look for an exact
	** match of this string in the disklist.
	**
	** Problems:  The hostname in the dump header (which is created
	** in send-backup and parsed by amdump and amrestore) does not
	** necessarily match the hostname in the disklist file.  If your
	** disklist contains anything other than the first token of the
	** client hostname then amflush will not work for those disks.
	**
	** New Behavior: send-backup puts the entire gethostname()
	** result in the dump header.  amflush looks for an exact match,
	** in the database.  If it can't find one it strips the last token
	** off the dump header hostname and keeps trying until it finds
	** a match in the disklist or runs out of tokens.  If the dump
	** header does not contain a domain, we append one internally.
	** This allows us to match FQDNs in the disklist against unqualified
	** hostnames from the clients.
	**
	** Advantages:  If gethostname() returns FQDNs and you specify
	** the same FQDNs in the disklist, then amflush will always work.
	** It will also work as long as the disklist and gethostname()
	** on the clients return enough information to be unambiguous.
	**
	** Ideal Solution:  Have the amanda server pass the hostname
	** for the dumpheader to the client.  Make amflush always look
	** for an exact match.  This guarantees that amflush always
	** puts every dump in the right place -- even if you use a CNAME
	** or some other oddity in the disklist file.  Unfortunately this
	** would require non-backwards compatible protocol extensions.
	*/
	dp = NULL;
	if(strchr(hostname,'.') == NULL && domain && *domain)
	    strcat(strcat(hostname,"."),domain);
	for(;;) {
	    char *s;
	    if((dp = lookup_disk(hostname, diskname)))
		break;
	    if((s = strrchr(hostname,'.')) == NULL)
		break;
	    *s = '\0';
	}
	if ( dp == NULL ) {
	    log(L_INFO, "%s: disk %s:%s not in database, skipping it.",
		entry->d_name, hostname, diskname);
	    continue;
	}

	if(level < 0 || level > 9) {
	    log(L_INFO, "%s: ignoring file with bogus dump level %d.",
		entry->d_name, level);
	    continue;
	}

	taper_cmd(FILE_WRITE, dp, destname, level);
	tok = getresult(taper);
	if(tok == TRYAGAIN) {
	    /* we'll retry one time */
	    taper_cmd(FILE_WRITE, dp, destname, level);
	    tok = getresult(taper);
	}

	switch(tok) {
	case DONE:
	    unlink(destname);
	    break;
	case TRYAGAIN:
	    log(L_WARNING, "%s: too many taper retries, leaving file on disk", 
		destname);
	    break;
	default:
	    log(L_WARNING, "%s: taper error, leaving file on disk",
		destname);
	    break;
	}
    }

    closedir(workdir);

    /* try to zap the now (hopefully) empty working dir */
    chdir(confdir);
    if(rmdir(dirname))
        log(L_WARNING, "Could not rmdir %s.  Check for cruft.",
	    dirname);
}

void run_dumps()
{
    holdingdisk_t *hdisk;

    startclock();
    log(L_START, "date %s", datestamp);

    chdir(confdir);
    startup_tape_process();
    taper_cmd(START_TAPER, datestamp, 	NULL, 0);
    tok = getresult(taper);

    if(tok != TAPER_OK) {
	/* forget it */
	sleep(5);	/* let taper log first, but not really necessary */
	log(L_ERROR, "Cannot flush without tape.  Try again.");
	log(L_FINISH, "date %s time %s", datestamp, walltime_str(curclock()));
    }
    else {

	for(hdisk = holdingdisks; hdisk != NULL; hdisk = hdisk->next)
	    flush_holdingdisk(hdisk->diskdir);

	/* tell taper to quit, then wait for it */
	taper_cmd(QUIT, NULL, 	NULL, 0);
	while(wait(NULL) != -1);

    }

    log(L_FINISH, "date %s time %s", datestamp, walltime_str(curclock()));

    /* now, have reporter generate report and send mail */

    execl(reporter_program, "reporter", (char *)0);
}

int get_amanda_names(fname, hostname, diskname, level)
char *fname, *hostname, *diskname;
int *level;
{
    char buffer[TAPE_BLOCK_BYTES], datestamp[80];
    int fd;

    if((fd = open(fname, O_RDONLY)) == -1)
	return 1;

    if(read(fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
	close(fd);
	return 1;
    }

    if(sscanf(buffer, "AMANDA: FILE %s %s %s lev %d",
	    datestamp, hostname, diskname, level) != 4) {
	close(fd);
	return 1;
    }

    close(fd);
    return 0;
}


static void startup_tape_process()
{
    int fd[2];

    if(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == -1)
	error("taper pipe: %s", strerror(errno));

    switch(taper_pid = fork()) {
    case -1:
	error("fork taper: %s", strerror(errno));
    case 0:	/* child process */
	close(fd[0]);
	if(dup2(fd[1], 0) == -1 || dup2(fd[1], 1) == -1)
	    error("taper dup2: %s", strerror(errno));
	execl(taper_program, "taper", (char *)0);
	error("exec %s: %s", taper_program, strerror(errno));
    default:	/* parent process */
	close(fd[1]);
	taper = fd[0];
    }
}

char line[MAX_LINE];

tok_t getresult(fd)
int fd;
{
    char *p; 
    int arg, len;
    tok_t t;

    if((len = read(fd, line, MAX_LINE)) == -1)
	error("reading result from taper: %s", strerror(errno));

    line[len] = '\0';

    p = line;
    result_argc = 0;
    while(*p) {
        while(isspace(*p)) p++;
        if(result_argc < MAX_ARGS) result_argv[result_argc++] = p;
        while(*p && !isspace(*p)) p++;
        if(*p) *p++ = '\0';
    }
    for(arg = result_argc; arg < MAX_ARGS; arg++) result_argv[arg] = "";

#ifdef DEBUG
    printf("argc = %d\n", result_argc);
    for(arg = 0; arg < MAX_ARGS; arg++)
        printf("argv[%d] = \"%s\"\n", arg, result_argv[arg]);
#endif

    for(t = BOGUS+1; t < LAST_TOK; t++)
	if(!strcmp(result_argv[0], cmdstr[t])) return t;
    
    return BOGUS;
}


void taper_cmd(cmd, /* optional */ ptr, destname, level)
tok_t cmd;
void *ptr;
char *destname;
int level;
{
    char cmdline[MAX_LINE];
    disk_t *dp;
    int len;

    switch(cmd) {
    case START_TAPER:
	sprintf(cmdline, "START-TAPER %s\n", (char *) ptr);
	break;
    case FILE_WRITE:
	dp = (disk_t *) ptr;
	sprintf(cmdline, "FILE-WRITE handle %s %s %s %d\n", 
		destname, dp->host->hostname, dp->name,
		level);
	break;
    case PORT_WRITE:
	dp = (disk_t *) ptr;
	sprintf(cmdline, "PORT-WRITE handle %s %s %d\n", 
		dp->host->hostname, dp->name, level);
	break;
    case QUIT:
	sprintf(cmdline, "QUIT\n");
	break;
    default:
	assert(0);
    }
    len = strlen(cmdline);
    if(write(taper, cmdline, len) < len)
	error("writing taper command");
}

