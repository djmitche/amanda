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
 * $Id: sendsize.c,v 1.126 2002/03/31 19:14:22 jrjackson Exp $
 *
 * send estimated backup sizes using dump
 */

#include "amanda.h"
#include "pipespawn.h"
#include "amandates.h"
#include "util.h"
#include "getfsent.h"
#include "version.h"
#include "client_util.h"

#ifdef SAMBA_CLIENT
#include "findpass.h"
#endif

#ifdef HAVE_SETPGID
#  define SETPGRP	setpgid(getpid(), getpid())
#  define SETPGRP_FAILED() do {						\
    dbprintf(("setpgid(%ld,%ld) failed: %s\n",				\
	      (long)getpid(), (long)getpid(), strerror(errno)));	\
} while(0)

#else /* () line 0 */
#if defined(SETPGRP_VOID)
#  define SETPGRP	setpgrp()
#  define SETPGRP_FAILED() do {						\
    dbprintf(("setpgrp() failed: %s\n", strerror(errno)));		\
} while(0)

#else
#  define SETPGRP	setpgrp(0, getpid())
#  define SETPGRP_FAILED() do {						\
    dbprintf(("setpgrp(0,%ld) failed: %s\n",				\
	      (long)getpid(), strerror(errno)));			\
} while(0)

#endif
#endif

typedef struct level_estimates_s {
    time_t dumpsince;
    int estsize;
    int needestimate;
} level_estimate_t;

typedef struct disk_estimates_s {
    struct disk_estimates_s *next;
    char *amname;
    char *amdevice;
    char *dirname;
    char *program;
    int program_is_wrapper;
    int spindle;
    pid_t child;
    int done;
    option_t *options;
    level_estimate_t est[DUMP_LEVELS];
} disk_estimates_t;

disk_estimates_t *est_list;

#define MAXMAXDUMPS 16

char *host;				/* my hostname from the server */
char *prefix;				/* debug line prefix if maxdumps */
char *prefix_line;			/* debug line prefix if maxdumps */

/* local functions */
int main P((int argc, char **argv));
void add_diskest P((char *disk, char *amdevice, int level, int spindle, 
		    int program_is_wrapper, char *prog, option_t *options));
void calc_estimates P((disk_estimates_t *est));
void free_estimates P((disk_estimates_t *est));
void dump_calc_estimates P((disk_estimates_t *));
void smbtar_calc_estimates P((disk_estimates_t *));
void gnutar_calc_estimates P((disk_estimates_t *));
void wrapper_calc_estimates P((disk_estimates_t *));
void generic_calc_estimates P((disk_estimates_t *));



int main(argc, argv)
int argc;
char **argv;
{
    int level, new_maxdumps, spindle;
    char *prog, *disk, *amdevice, *dumpdate;
    option_t *options = NULL;
    int program_is_wrapper;
    disk_estimates_t *est;
    disk_estimates_t *est1;
    disk_estimates_t *est_prev;
    char *line = NULL;
    char *s, *fp;
    int ch;
    char *err_extra = NULL;
    int fd;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    int done;
    int need_wait;
    int maxdumps = 1;
    int dumpsrunning;
    char my_pid[NUM_STR_SIZE];


    /* initialize */

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    safe_cd();

    set_pname("sendsize");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);
    dbopen();
    dbprintf(("%s: version %s\n", argv[0], version()));

    snprintf(my_pid, sizeof(my_pid), "%d", getpid());
    prefix = vstralloc(get_pname(), ": ", my_pid, NULL);
    prefix_line = stralloc2(prefix, ": ");

    host = alloc(MAX_HOSTNAME_LENGTH+1);
    gethostname(host, MAX_HOSTNAME_LENGTH);
    host[MAX_HOSTNAME_LENGTH] = '\0';

    /* handle all service requests */

    start_amandates(0);

    for(; (line = agets(stdin)) != NULL; free(line)) {
#define sc "OPTIONS"
	if(strncmp(line, sc, sizeof(sc)-1) == 0) {
#undef sc
#define sc "maxdumps="
	    s = strstr(line, sc);
	    if(s != NULL) {
		s += sizeof(sc)-1;
#undef sc
		if(sscanf(s, "%d;", &new_maxdumps) != 1) {
		    err_extra = "bad maxdumps value";
		    goto err;
		}
		if (new_maxdumps > MAXMAXDUMPS) {
		    maxdumps = MAXMAXDUMPS;
		} else if (new_maxdumps > 0) {
		    maxdumps = new_maxdumps;
		}
	    }

#define sc "hostname="
	    s = strstr(line, sc);
	    if(s != NULL) {
		s += sizeof(sc)-1;
		ch = *s++;
#undef sc
		fp = s-1;
		while(ch != '\0' && ch != ';') ch = *s++;
		s[-1] = '\0';
		host = newstralloc(host, fp);
	    }

	    printf("OPTIONS maxdumps=%d;\n", maxdumps);
	    fflush(stdout);
	    continue;
	}

	s = line;
	ch = *s++;

	skip_whitespace(s, ch);			/* find the program name */
	if(ch == '\0') {
	    err_extra = "no program name";
	    goto err;				/* no program name */
	}
	prog = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	program_is_wrapper=0;
	if(strcmp(prog,"DUMPER")==0) {
	    program_is_wrapper=1;
	    skip_whitespace(s, ch);		/* find dumper name */
	    if (ch == '\0') {
		goto err;			/* no program */
	    }
	    prog = s - 1;
	    skip_non_whitespace(s, ch);
	    s[-1] = '\0';
	}

	skip_whitespace(s, ch);			/* find the disk name */
	if(ch == '\0') {
	    err_extra = "no disk name";
	    goto err;				/* no disk name */
	}
	disk = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the device or level */
	if (ch == '\0') {
	    err_extra = "bad level";
	    goto err;
	}
	if(!isdigit((int)s[-1])) {
	    amdevice = s - 1;
	    skip_non_whitespace(s, ch);
	    s[-1] = '\0';
	    skip_whitespace(s, ch);		/* find level number */
	}
	else {
	    amdevice = stralloc(disk);
	}

						/* find the level number */
	if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	    err_extra = "bad level";
	    goto err;				/* bad level */
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);			/* find the dump date */
	if(ch == '\0') {
	    err_extra = "no dumpdate";
	    goto err;				/* no dumpdate */
	}
	dumpdate = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	spindle = 0;				/* default spindle */

	skip_whitespace(s, ch);			/* find the spindle */
	if(ch != '\0') {
	    if(sscanf(s - 1, "%d", &spindle) != 1) {
		err_extra = "bad spindle";
		goto err;			/* bad spindle */
	    }
	    skip_integer(s, ch);

	    skip_whitespace(s, ch);		/* find the exclusion list */
	    if(ch != '\0') {
		if(strncmp(s-1, "OPTIONS |;",10) == 0) {
		    options = parse_options(s+8, disk, amdevice, 0);
		}
		else {
		    options = alloc(sizeof(option_t));
		    init_options(options);
		    if(strncmp(s-1, "exclude-file=", 13) == 0) {
			options->exclude_file =
				append_sl(options->exclude_file, s+12);
		    }
		    if(strncmp(s-1, "exclude-list=", 13) == 0) {
			options->exclude_list =
				append_sl(options->exclude_list, s+12);
		    }
		
		    skip_non_whitespace(s, ch);
		    if(ch) {
			err_extra = "extra text at end";
			goto err;		/* should have gotten to end */
		    }
		}
	    }
	    else {
		options = alloc(sizeof(option_t));
		init_options(options);
	    }
	}

	add_diskest(disk, amdevice, level, spindle, program_is_wrapper, prog, options);
    }
    amfree(line);

    finish_amandates();
    free_amandates();

    dumpsrunning = 0;
    need_wait = 0;
    done = 0;
    while(! done) {
	done = 1;

	/*
	 * See if we need to wait for a child before we can do anything
	 * else in this pass.
	 */
	if(need_wait) {
	    pid_t child_pid;
	    amwait_t child_status;
	    int exit_code;

	    dbprintf(("%s: waiting for child to complete\n", prefix));
	    child_pid = wait(&child_status);
	    if(child_pid == -1) {
		error("wait failed: %s", strerror(errno));
	    }
	    if(WIFSIGNALED(child_status)) {
		dbprintf(("%s: child %ld terminated with signal %d\n",
			  prefix, (long) child_pid, WTERMSIG(child_status)));
	    } else {
		exit_code = WEXITSTATUS(child_status);
		if(exit_code == 0) {
		    dbprintf(("%s: child %ld terminated normally\n",
			      prefix, (long) child_pid));
		} else {
		    dbprintf(("%s: child %ld terminated with code %d\n",
			      prefix, (long) child_pid, exit_code));
		}
	    }
	    /*
	     * Find the child and mark it done.
	     */
	    for(est = est_list; est != NULL; est = est->next) {
		if(est->child == child_pid) {
		    break;
		}
	    }
	    if(est == NULL) {
		dbprintf(("%s: unexpected child %ld\n", prefix, child_pid));
	    } else {
		est->done = 1;
		dumpsrunning--;
	    }
	}
	/*
	 * If we are already running the maximum number of children
	 * go back and wait until one of them finishes.
	 */
	if(dumpsrunning >= maxdumps) {
	    done = 0;
	    need_wait = 1;
	    continue;				/* have to wait first */
	}
	/*
	 * Find a new child to start.
	 */
	for(est = est_list; est != NULL; est = est->next) {
	    if(est->done == 0) {
		done = 0;			/* more to do */
	    }
	    if(est->child != 0) {
		continue;			/* child is running or done */
	    }
	    /*
	     * Make sure there is no spindle conflict.
	     */
	    if(est->spindle != -1) {
		for(est1 = est_list; est1 != NULL; est1 = est1->next) {
		    if(est1->child == 0) {
			/*
			 * Ignore anything not yet started, including us.
			 */
			continue;
		    }
		    if(est1->done) {
			continue;		/* ignore completed disks */
		    }
		    if(est1->spindle == est->spindle) {
			break;			/* oops -- they match */
		    }
		}
		if(est1 != NULL) {
		    continue;			/* spindle conflict */
		}
	    }
	    break;				/* start this estimate */
	}
	if(est == NULL) {
	    if(dumpsrunning > 0) {
		need_wait = 1;			/* nothing to do but wait */
	    }
	} else {
	    done = 0;
	    if((est->child = fork()) == 0) {
		snprintf(my_pid, sizeof(my_pid), "%d", getpid());
		prefix = newvstralloc(prefix, get_pname(), ": ", my_pid, NULL);
		prefix_line = newstralloc2(prefix_line, prefix, ": ");
		calc_estimates(est);		/* child does the estimate */
		exit(0);
	    } else if(est->child == -1) {
		error("calc_estimates fork failed: %s", strerror(errno));
	    }
	    dumpsrunning++;			/* parent */
	}
    }

    est_prev = NULL;
    for(est = est_list; est != NULL; est = est->next) {
	free_estimates(est);
	amfree(est_prev);
	est_prev = est;
    }
    amfree(est_prev);
    amfree(host);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
#if defined(USE_DBMALLOC)
	extern int db_fd;

	malloc_list(db_fd, malloc_hist_1, malloc_hist_2);
#endif
    }

    dbclose();
    return 0;
 err:
    printf("FORMAT ERROR IN REQUEST PACKET\n");
    if(err_extra) {
	dbprintf(("REQ packet is bogus: %s\n", err_extra));
    } else {
	dbprintf(("REQ packet is bogus\n"));
    }
    dbclose();
    return 1;
}


void add_diskest(disk, amdevice, level, spindle, program_is_wrapper, prog, options)
char *disk, *amdevice, *prog;
int level, spindle, program_is_wrapper;
option_t *options;
{
    disk_estimates_t *newp, *curp;
    amandates_t *amdp;
    int dumplev, estlev;
    time_t dumpdate;

    for(curp = est_list; curp != NULL; curp = curp->next) {
	if(strcmp(curp->amname, disk) == 0) {
	    /* already have disk info, just note the level request */
	    curp->est[level].needestimate = 1;
	    return;
	}
    }

    newp = (disk_estimates_t *) alloc(sizeof(disk_estimates_t));
    memset(newp, 0, sizeof(*newp));
    newp->next = est_list;
    est_list = newp;
    newp->amname = stralloc(disk);
    newp->amdevice = stralloc(amdevice);
    newp->dirname = amname_to_dirname(newp->amdevice);
    newp->program = stralloc(prog);
    newp->program_is_wrapper = program_is_wrapper;
    newp->spindle = spindle;
    newp->est[level].needestimate = 1;
    newp->options = options;

    /* fill in dump-since dates */

    amdp = amandates_lookup(newp->amname);

    newp->est[0].dumpsince = EPOCH;
    for(dumplev = 0; dumplev < DUMP_LEVELS; dumplev++) {
	dumpdate = amdp->dates[dumplev];
	for(estlev = dumplev+1; estlev < DUMP_LEVELS; estlev++) {
	    if(dumpdate > newp->est[estlev].dumpsince)
		newp->est[estlev].dumpsince = dumpdate;
	}
    }
}


void free_estimates(est)
disk_estimates_t *est;
{
    amfree(est->amname);
    amfree(est->dirname);
    amfree(est->program);
    if(est->options) {
	free_sl(est->options->exclude_file);
	free_sl(est->options->exclude_list);
    }
    amfree(est->options);
}

/*
 * ------------------------------------------------------------------------
 *
 */

void calc_estimates(est)
disk_estimates_t *est;
{
    dbprintf(("%s: calculating for amname '%s', dirname '%s', spindle %d\n",
	      prefix, est->amname, est->dirname, est->spindle));

    if(est->program_is_wrapper ==  1)
	wrapper_calc_estimates(est);
    else
#ifndef USE_GENERIC_CALCSIZE
    if(strcmp(est->program, "DUMP") == 0)
	dump_calc_estimates(est);
    else
#endif
#ifdef SAMBA_CLIENT
      if (strcmp(est->program, "GNUTAR") == 0 &&
	  est->amdevice[0] == '/' && est->amdevice[1] == '/')
	smbtar_calc_estimates(est);
      else
#endif
#ifdef GNUTAR
	if (strcmp(est->program, "GNUTAR") == 0)
	  gnutar_calc_estimates(est);
	else
#endif
	  generic_calc_estimates(est);

    dbprintf(("%s: done with amname '%s', dirname '%s', spindle %d\n",
	      prefix, est->amname, est->dirname, est->spindle));
}

/*
 * ------------------------------------------------------------------------
 *
 */

/* local functions */
long getsize_dump P((char *disk, char *amdevice, int level, option_t *options));
long getsize_smbtar P((char *disk, char *amdevice, int level, option_t *options));
long getsize_gnutar P((char *disk, char *amdevice, int level,
		       option_t *options, time_t dumpsince));
long getsize_wrapper P((char *program, char *disk, char *amdevice, int level,
			option_t *options, time_t dumpsince));
long handle_dumpline P((char *str));
double first_num P((char *str));

void wrapper_calc_estimates(est)
disk_estimates_t *est;
{
  int level;
  long size;

  for(level = 0; level < DUMP_LEVELS; level++) {
      if (est->est[level].needestimate) {
	  dbprintf(("%s: getting size via wrapper for %s level %d\n",
		    get_pname(), est->amname, level));
	  size = getsize_wrapper(est->program, est->amname, est->amdevice, level, est->options,
				 est->est[level].dumpsince);

	  amflock(1, "size");

	  fseek(stdout, (off_t)0, SEEK_SET);

	  printf("%s %d SIZE %ld\n", est->amname, level, size);
	  fflush(stdout);

	  amfunlock(1, "size");
      }
  }
}


void generic_calc_estimates(est)
disk_estimates_t *est;
{
    char *cmd;
    char *argv[DUMP_LEVELS*2+10];
    char number[NUM_STR_SIZE];
    int i, level, argc, calcpid;

    cmd = vstralloc(libexecdir, "/", "calcsize", versionsuffix(), NULL);

    argc = 0;
    argv[argc++] = stralloc("calcsize");
    argv[argc++] = stralloc(est->program);
#ifdef BUILTIN_EXCLUDE_SUPPORT
    if(est->exclude && *est->exclude) {
	argv[argc++] = stralloc("-X");
	argv[argc++] = stralloc(est->exclude);
    }
#endif
    argv[argc++] = stralloc(est->amdevice);
    argv[argc++] = stralloc(est->dirname);

    dbprintf(("%s: running cmd: %s", prefix, argv[0]));
    for(i=0; i<argc; ++i)
	dbprintf((" %s", argv[i]));

    for(level = 0; level < DUMP_LEVELS; level++) {
	if(est->est[level].needestimate) {
	    snprintf(number, sizeof(number), "%d", level);
	    argv[argc++] = stralloc(number); 
	    dbprintf((" %s", number));
	    snprintf(number, sizeof(number),
			"%ld", (long)est->est[level].dumpsince);
	    argv[argc++] = stralloc(number); 
	    dbprintf((" %s", number));
	}
    }
    argv[argc] = NULL;
    dbprintf(("\n"));

    fflush(stderr); fflush(stdout);

    switch(calcpid = fork()) {
    case -1:
        error("%s: %s: fork returned: %s", get_pname(), cmd, strerror(errno));
    default:
        break;
    case 0:
	execve(cmd, argv, safe_env());
	dbprintf(("%s: execve %s returned: %s", prefix, cmd, strerror(errno)));
	exit(1);
    }
    for(i = 0; i < argc; i++) {
	amfree(argv[i]);
    }
    amfree(cmd);

    wait(NULL);
}


void dump_calc_estimates(est)
disk_estimates_t *est;
{
    int level;
    long size;

    for(level = 0; level < DUMP_LEVELS; level++) {
	if(est->est[level].needestimate) {
	    dbprintf(("%s: getting size via dump for %s level %d\n",
		      prefix, est->amname, level));
	    size = getsize_dump(est->amname, est->amdevice,level, est->options);

	    amflock(1, "size");

	    fseek(stdout, (off_t)0, SEEK_SET);

	    printf("%s %d SIZE %ld\n", est->amname, level, size);
	    fflush(stdout);

	    amfunlock(1, "size");
	}
    }
}

#ifdef SAMBA_CLIENT
void smbtar_calc_estimates(est)
disk_estimates_t *est;
{
    int level;
    long size;

    for(level = 0; level < DUMP_LEVELS; level++) {
	if(est->est[level].needestimate) {
	    dbprintf(("%s: getting size via smbclient for %s level %d\n",
		      prefix, est->amname, level));
	    size = getsize_smbtar(est->amname, est->amdevice, level, est->options);

	    amflock(1, "size");

	    fseek(stdout, (off_t)0, SEEK_SET);

	    printf("%s %d SIZE %ld\n", est->amname, level, size);
	    fflush(stdout);

	    amfunlock(1, "size");
	}
    }
}
#endif

#ifdef GNUTAR
void gnutar_calc_estimates(est)
disk_estimates_t *est;
{
  int level;
  long size;

  for(level = 0; level < DUMP_LEVELS; level++) {
      if (est->est[level].needestimate) {
	  dbprintf(("%s: getting size via gnutar for %s level %d\n",
		    prefix, est->amname, level));
	  size = getsize_gnutar(est->amname, est->amdevice, level,
				est->options, est->est[level].dumpsince);

	  amflock(1, "size");

	  fseek(stdout, (off_t)0, SEEK_SET);

	  printf("%s %d SIZE %ld\n", est->amname, level, size);
	  fflush(stdout);

	  amfunlock(1, "size");
      }
  }
}
#endif

typedef struct regex_s {
    char *regex;
    int scale;
} regex_t;

regex_t re_size[] = {
#ifdef DUMP
    {"  DUMP: estimated -*[0-9][0-9]* tape blocks", 1024},
    {"  DUMP: [Ee]stimated [0-9][0-9]* blocks", 512},
    {"  DUMP: [Ee]stimated [0-9][0-9]* bytes", 1},	       /* Ultrix 4.4 */
    {" UFSDUMP: estimated [0-9][0-9]* blocks", 512},           /* NEC EWS-UX */
    {"dump: Estimate: [0-9][0-9]* tape blocks", 1024},		    /* OSF/1 */
    {"backup: There are an estimated [0-9][0-9]* tape blocks.",1024}, /* AIX */
    {"backup: estimated [0-9][0-9]* 1k blocks", 1024},		      /* AIX */
    {"backup: estimated [0-9][0-9]* tape blocks", 1024},	      /* AIX */
    {"backup: [0-9][0-9]* tape blocks on [0-9][0-9]* tape(s)",1024},  /* AIX */
    {"backup: [0-9][0-9]* 1k blocks on [0-9][0-9]* volume(s)",1024},  /* AIX */
    {"dump: Estimate: [0-9][0-9]* blocks being output to pipe",1024},
                                                              /* DU 4.0 dump */
    {"dump: Dumping [0-9][0-9]* bytes, ", 1},                /* DU 4.0 vdump */
    {"DUMP: estimated [0-9][0-9]* KB output", 1024},                 /* HPUX */
    {"  UFSDUMP: estimated [0-9][0-9]* blocks", 512},               /* Sinix */

#ifdef HAVE_DUMP_ESTIMATE
    {"[0-9][0-9]* blocks, [0-9][0-9]*.[0-9][0-9]* volumes", 1024},
                                                          /* DU 3.2g dump -E */
    {"^[0-9][0-9]* blocks$", 1024},			  /* DU 4.0 dump  -E */
    {"^[0-9][0-9]*$", 1},			       /* Solaris ufsdump -S */
#endif
#endif

#ifdef VDUMP
    {"vdump: Dumping [0-9][0-9]* bytes, ", 1},		      /* OSF/1 vdump */
#endif
    
#ifdef VXDUMP
    {"vxdump: estimated [0-9][0-9]* blocks", 512},          /* HPUX's vxdump */
    {"  VXDUMP: estimated [0-9][0-9]* blocks", 512},                /* Sinix */
#endif

#ifdef XFSDUMP
    {"xfsdump: estimated dump size: [0-9][0-9]* bytes", 1},  /* Irix 6.2 xfs */
#endif
    
#ifdef GNUTAR
    {"Total bytes written: [0-9][0-9]*", 1},		    /* Gnutar client */
#endif

#ifdef SAMBA_CLIENT
#if SAMBA_VERSION >= 2
#define SAMBA_DEBUG_LEVEL "0"
    {"Total number of bytes: [0-9][0-9]*", 1},			 /* Samba du */
#else
#define SAMBA_DEBUG_LEVEL "3"
    {"Total bytes listed: [0-9][0-9]*", 1},			/* Samba dir */
#endif
#endif

    { NULL, 0 }
};


long getsize_dump(disk, amdevice, level, options)
    char *disk, *amdevice;
    int level;
    option_t *options;
{
    int pipefd[2], nullfd, stdoutfd, killctl[2];
    pid_t dumppid;
    long size;
    FILE *dumpout;
    char *dumpkeys = NULL;
    char *device = NULL;
    char *fstype = NULL;
    char *cmd = NULL;
    char *line = NULL;
    char *rundump_cmd = NULL;
    char level_str[NUM_STR_SIZE];
    int s;

    snprintf(level_str, sizeof(level_str), "%d", level);

    device = amname_to_devname(amdevice);
    fstype = amname_to_fstype(amdevice);

    cmd = vstralloc(libexecdir, "/rundump", versionsuffix(), NULL);
    rundump_cmd = stralloc(cmd);

    stdoutfd = nullfd = open("/dev/null", O_RDWR);
    pipefd[0] = pipefd[1] = killctl[0] = killctl[1] = -1;
    pipe(pipefd);

#ifdef XFSDUMP						/* { */
#ifdef DUMP						/* { */
    if (strcmp(fstype, "xfs") == 0)
#else							/* } { */
    if (1)
#endif							/* } */
    {
        char *name = " (xfsdump)";
	dbprintf(("%s: running \"%s%s -F -J -l %s - %s\"\n",
		  prefix, cmd, name, level_str, device));
    }
    else
#endif							/* } */
#ifdef VXDUMP						/* { */
#ifdef DUMP						/* { */
    if (strcmp(fstype, "vxfs") == 0)
#else							/* } { */
    if (1)
#endif							/* } */
    {
#ifdef USE_RUNDUMP
        char *name = " (vxdump)";
#else
	char *name = "";

	cmd = newstralloc(cmd, VXDUMP);
#endif
	dumpkeys = vstralloc(level_str, "s", "f", NULL);
        dbprintf(("%s: running \"%s%s %s 1048576 - %s\"\n",
		  prefix, cmd, name, dumpkeys, device));
    }
    else
#endif							/* } */
#ifdef VDUMP						/* { */
#ifdef DUMP						/* { */
    if (strcmp(fstype, "advfs") == 0)
#else							/* } { */
    if (1)
#endif							/* } */
    {
	char *name = " (vdump)";
	amfree(device);
	device = amname_to_dirname(amdevice);
	dumpkeys = vstralloc(level_str, "b", "f", NULL);
	dbprintf(("%s: running \"%s%s %s 60 - %s\"\n",
		  prefix, cmd, name, dumpkeys, device));
    }
    else
#endif							/* } */
#ifdef DUMP						/* { */
    if (1) {
	char *name = NULL;

# ifdef USE_RUNDUMP					/* { */
#  ifdef AIX_BACKUP					/* { */
	name = stralloc(" (backup)");
#  else							/* } { */
	name = vstralloc(" (", DUMP, ")", NULL);
#  endif						/* } */
# else							/* } { */
	name = stralloc("");
	cmd = newstralloc(cmd, DUMP);
# endif							/* } */

# ifdef AIX_BACKUP					/* { */
	dumpkeys = vstralloc("-", level_str, "f", NULL);
	dbprintf(("%s: running \"%s%s %s - %s\"\n",
		  prefix, cmd, name, dumpkeys, device));
# else							/* } { */
	dumpkeys = vstralloc(level_str,
#  ifdef HAVE_DUMP_ESTIMATE				/* { */
			     HAVE_DUMP_ESTIMATE,
#  endif						/* } */
#  ifdef HAVE_HONOR_NODUMP				/* { */
			     "h",
#  endif						/* } */
			     "s", "f", NULL);

#  ifdef HAVE_DUMP_ESTIMATE
	stdoutfd = pipefd[1];
#  endif

#  ifdef HAVE_HONOR_NODUMP				/* { */
	dbprintf(("%s: running \"%s%s %s 0 1048576 - %s\"\n",
		  prefix, cmd, name, dumpkeys, device));
#  else							/* } { */
	dbprintf(("%s: running \"%s%s %s 1048576 - %s\"\n",
		  prefix, cmd, name, dumpkeys, device));
#  endif						/* } */
# endif							/* } */
	amfree(name);
    }
    else
#endif							/* } */
    {
        dbprintf(("%s: no dump program available", prefix));
	error("%s: no dump program available", get_pname());
    }

    pipe(killctl);

    switch(dumppid = fork()) {
    case -1:
	dbprintf(("%s: cannot fork for killpgrp: %s\n",
		  prefix, strerror(errno)));
	amfree(dumpkeys);
	amfree(cmd);
	amfree(rundump_cmd);
	amfree(device);
	return -1;
    default:
	break; 
    case 0:	/* child process */
	if(SETPGRP == -1)
	    SETPGRP_FAILED();
	else if (killctl[0] == -1 || killctl[1] == -1)
	    dbprintf(("%s: pipe for killpgrp failed, trying without killpgrp\n",
		      prefix));
	else {
	    switch(fork()) {
	    case -1:
		dbprintf(("%s: fork failed, trying without killpgrp\n",
			  prefix));
		break;

	    default:
	    {
		char *killpgrp_cmd = vstralloc(libexecdir, "/killpgrp",
					       versionsuffix(), NULL);
		dbprintf(("%s: running %s\n", prefix, killpgrp_cmd));
		dup2(killctl[0], 0);
		dup2(nullfd, 1);
		dup2(nullfd, 2);
		close(pipefd[0]);
		close(pipefd[1]);
		close(killctl[1]);
		close(nullfd);
		execle(killpgrp_cmd, killpgrp_cmd, (char *)0, safe_env());
		dbprintf(("%s: cannot execute %s: %s\n", prefix, killpgrp_cmd,
		    strerror(errno)));
		exit(-1);
	    }

	    case 0:  /* child process */
		break;
	    }
	}

	dup2(nullfd, 0);
	dup2(stdoutfd, 1);
	dup2(pipefd[1], 2);
	aclose(pipefd[0]);
	if (killctl[0] != -1)
	    aclose(killctl[0]);
	if (killctl[1] != -1)
	    aclose(killctl[1]);

#ifdef XFSDUMP
#ifdef DUMP
	if (strcmp(fstype, "xfs") == 0)
#else
	if (1)
#endif
	    execle(cmd, "xfsdump", "-F", "-J", "-l", level_str, "-", device,
		   (char *)0, safe_env());
	else
#endif
#ifdef VXDUMP
#ifdef DUMP
	if (strcmp(fstype, "vxfs") == 0)
#else
	if (1)
#endif
	    execle(cmd, "vxdump", dumpkeys, "1048576", "-", device, (char *)0,
		   safe_env());
	else
#endif
#ifdef VDUMP
#ifdef DUMP
	if (strcmp(fstype, "advfs") == 0)
#else
	if (1)
#endif
	    execle(cmd, "vdump", dumpkeys, "60", "-", device, (char *)0,
		   safe_env());
	else
#endif
#ifdef DUMP
# ifdef AIX_BACKUP
	    execle(cmd, "backup", dumpkeys, "-", device, (char *)0, safe_env());
# else
	    execle(cmd, "dump", dumpkeys, 
#ifdef HAVE_HONOR_NODUMP
		   "0",
#endif
		   "1048576", "-", device, (char *)0, safe_env());
# endif
#endif
	{
	    error("%s: exec %s failed or no dump program available: %s",
		  get_pname(), cmd, strerror(errno));
	}
    }

    amfree(dumpkeys);
    amfree(cmd);
    amfree(rundump_cmd);

    aclose(pipefd[1]);
    if (killctl[0] != -1)
	aclose(killctl[0]);
    dumpout = fdopen(pipefd[0],"r");

    for(size = -1; (line = agets(dumpout)) != NULL; free(line)) {
	dbprintf(("%s%s\n", prefix_line, line));
	size = handle_dumpline(line);
	if(size > -1) {
	    amfree(line);
	    if((line = agets(dumpout)) != NULL) {
		dbprintf(("%s%s\n", prefix_line, line));
	    }
	    break;
	}
    }
    amfree(line);

    dbprintf(("%s.....\n", prefix_line));
    if(size == -1)
	dbprintf(("%s: (no size line match in above dump output)\n%s.....\n",
		  prefix, prefix_line));
    if(size == 0 && level == 0)
	dbprintf(("%s: (PC SHARE connection problem, is this disk really empty?)\n%s.....\n",
		  prefix, prefix_line));

    if (killctl[1] != -1) {
	dbprintf(("%s: asking killpgrp to terminate\n", prefix));
	aclose(killctl[1]);
	for(s = 5; s > 0; --s) {
	    sleep(1);
	    if (waitpid(dumppid, NULL, WNOHANG) != -1)
		goto terminated;
	}
    }
    
    /*
     * First, try to kill the dump process nicely.  If it ignores us
     * for several seconds, hit it harder.
     */
    dbprintf(("%s: sending SIGTERM to process group %ld\n",
	      prefix, (long) dumppid));
    if (kill(-dumppid, SIGTERM) == -1) {
	dbprintf(("%s: kill failed: %s\n", prefix, strerror(errno)));
    }
    /* Now check whether it dies */
    for(s = 5; s > 0; --s) {
	sleep(1);
	if (waitpid(dumppid, NULL, WNOHANG) != -1)
	    goto terminated;
    }

    dbprintf(("%s: sending SIGKILL to process group %ld\n",
	      prefix, (long) dumppid));
    if (kill(-dumppid, SIGKILL) == -1) {
	dbprintf(("%s: kill failed: %s\n", prefix, strerror(errno)));
    }
    for(s = 5; s > 0; --s) {
	sleep(1);
	if (waitpid(dumppid, NULL, WNOHANG) != -1)
	    goto terminated;
    }

    dbprintf(("%s: cannot kill it, waiting for normal termination\n", prefix));
    wait(NULL);

 terminated:

    aclose(nullfd);
    afclose(dumpout);

    amfree(device);
    amfree(fstype);

    return size;
}

#ifdef SAMBA_CLIENT
long getsize_smbtar(disk, amdevice, level, optionns)
    char *disk, *amdevice;
    int level;
    option_t *optionns;
{
    int pipefd = -1, nullfd = -1, passwdfd = -1;
    int dumppid;
    long size;
    FILE *dumpout;
    char *tarkeys, *sharename, *user_and_password = NULL, *domain = NULL;
    char *share = NULL, *subdir = NULL;
    int lpass;
    char *pwtext;
    int pwtext_len;
    char *line;
    char *pw_fd_env;

    parsesharename(amdevice, &share, &subdir);
    if (!share) {
	amfree(share);
	amfree(subdir);
	error("%s-smbtar: cannot parse disk entry '%s' for share/subdir",
	      get_pname(), disk);
    }
    if ((subdir) && (SAMBA_VERSION < 2)) {
	amfree(share);
	amfree(subdir);
	error("%s-smbtar: subdirectory specified for share '%s' but samba not v2 or better", get_pname(), disk);
    }
    if ((user_and_password = findpass(share, &domain)) == NULL) {

	if(domain) {
	    memset(domain, '\0', strlen(domain));
	    amfree(domain);
	}
	error("%s-smbtar: cannot find password for %s", get_pname(), disk);
    }
    lpass = strlen(user_and_password);
    if ((pwtext = strchr(user_and_password, '%')) == NULL) {
	memset(user_and_password, '\0', lpass);
	amfree(user_and_password);
	if(domain) {
	    memset(domain, '\0', strlen(domain));
	    amfree(domain);
	}
	error("%s-smbtar: password field not \'user%%pass\' for %s",
	      get_pname(), disk);
    }
    *pwtext++ = '\0';
    pwtext_len = strlen(pwtext);
    if ((sharename = makesharename(share, 0)) == NULL) {
	memset(user_and_password, '\0', lpass);
	amfree(user_and_password);
	if(domain) {
	    memset(domain, '\0', strlen(domain));
	    amfree(domain);
	}
	error("%s-smbtar: cannot make share name of %s", get_pname(), share);
    }
    nullfd = open("/dev/null", O_RDWR);

#if SAMBA_VERSION >= 2
    if (level == 0)
	tarkeys = "archive 0;recurse;du";
    else
	tarkeys = "archive 1;recurse;du";
#else
    if (level == 0)
	tarkeys = "archive 0;recurse;dir";
    else
	tarkeys = "archive 1;recurse;dir";
#endif

    if (pwtext_len > 0) {
	pw_fd_env = "PASSWD_FD";
    } else {
	pw_fd_env = "dummy_PASSWD_FD";
    }
    dumppid = pipespawn(SAMBA_CLIENT, STDERR_PIPE|PASSWD_PIPE,
	      &nullfd, &nullfd, &pipefd, 
	      pw_fd_env, &passwdfd,
	      "smbclient",
	      sharename,
	      "-d", SAMBA_DEBUG_LEVEL,
	      *user_and_password ? "-U" : skip_argument,
	      *user_and_password ? user_and_password : skip_argument,
	      "-E",
	      domain ? "-W" : skip_argument,
	      domain ? domain : skip_argument,
#if SAMBA_VERSION >= 2
	      subdir ? "-D" : skip_argument,
	      subdir ? subdir : skip_argument,
#endif
	      "-c", tarkeys,
	      NULL);
    if(domain) {
	memset(domain, '\0', strlen(domain));
	amfree(domain);
    }
    aclose(nullfd);
    if(pwtext_len > 0 && fullwrite(passwdfd, pwtext, pwtext_len) < 0) {
	int save_errno = errno;

	memset(user_and_password, '\0', lpass);
	amfree(user_and_password);
	aclose(passwdfd);
	error("%s-smbtar: password write failed: %s",
	      get_pname(), strerror(save_errno));
    }
    memset(user_and_password, '\0', lpass);
    amfree(user_and_password);
    aclose(passwdfd);
    amfree(sharename);
    amfree(share);
    amfree(subdir);
    dumpout = fdopen(pipefd,"r");

    for(size = -1; (line = agets(dumpout)) != NULL; free(line)) {
	dbprintf(("%s%s\n", prefix_line, line));
	size = handle_dumpline(line);
	if(size > -1) {
	    amfree(line);
	    if((line = agets(dumpout)) != NULL) {
		dbprintf(("%s%s\n", prefix_line, line));
	    }
	    break;
	}
    }
    amfree(line);

    dbprintf(("%s.....\n", prefix_line));
    if(size == -1)
	dbprintf(("%s: (no size line match in above smbclient output)\n%s.....\n",
		  prefix, prefix_line));
    if(size==0 && level ==0)
	size=-1;

    kill(-dumppid, SIGTERM);

    wait(NULL);

    afclose(dumpout);
    pipefd = -1;

    return size;
}
#endif

#ifdef GNUTAR
long getsize_gnutar(disk, amdevice, level, options, dumpsince)
char *disk, *amdevice;
int level;
option_t *options;
time_t dumpsince;
{
    int pipefd = -1, nullfd = -1, dumppid;
    long size = -1;
    FILE *dumpout = NULL;
    char *incrname = NULL;
    char *basename = NULL;
    char *dirname = NULL;
    char *inputname = NULL;
    FILE *in = NULL;
    FILE *out = NULL;
    char *line = NULL;
    char *cmd = NULL;
    char dumptimestr[80];
    struct tm *gmtm;
    int nb_exclude = 0;
    int nb_include = 0;
    char **my_argv;
    int i;
    char *file_exclude = NULL;
    char *file_include = NULL;

    if(options->exclude_file) nb_exclude += options->exclude_file->nb_element;
    if(options->exclude_list) nb_exclude += options->exclude_list->nb_element;
    if(options->include_file) nb_include += options->include_file->nb_element;
    if(options->include_list) nb_include += options->include_list->nb_element;

    if(nb_exclude > 0) file_exclude = build_exclude(disk, amdevice, options, 0);
    if(nb_include > 0) file_include = build_include(disk, amdevice, options, 0);

    my_argv = alloc(sizeof(char *) * 21);
    i = 0;

#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
    {
	char number[NUM_STR_SIZE];
	char *s;
	int ch;
	int baselevel;

	basename = vstralloc(GNUTAR_LISTED_INCREMENTAL_DIR,
			     "/",
			     host,
			     disk,
			     NULL);
	/*
	 * The loop starts at the first character of the host name,
	 * not the '/'.
	 */
	s = basename + sizeof(GNUTAR_LISTED_INCREMENTAL_DIR);
	while((ch = *s++) != '\0') {
	    if(ch == '/' || isspace(ch)) s[-1] = '_';
	}

	snprintf(number, sizeof(number), "%d", level);
	incrname = vstralloc(basename, "_", number, ".new", NULL);
	unlink(incrname);

	/*
	 * Open the listed incremental file from the previous level.  Search
	 * backward until one is found.  If none are found (which will also
	 * be true for a level 0), arrange to read from /dev/null.
	 */
	baselevel = level;
	while (in == NULL) {
	    if (--baselevel >= 0) {
		snprintf(number, sizeof(number), "%d", baselevel);
		inputname = newvstralloc(inputname,
					 basename, "_", number, NULL);
	    } else {
		inputname = newstralloc(inputname, "/dev/null");
	    }
	    if ((in = fopen(inputname, "r")) == NULL) {
		int save_errno = errno;

		dbprintf(("%s: gnutar: error opening %s: %s\n",
			  prefix,
			  inputname,
			  strerror(save_errno)));
		if (baselevel < 0) {
		    goto common_exit;
		}
	    }
	}

	/*
	 * Copy the previous listed incremental file to the new one.
	 */
	if ((out = fopen(incrname, "w")) == NULL) {
	    dbprintf(("%s: opening %s: %s\n",
		      prefix, incrname, strerror(errno)));
	    goto common_exit;
	}

	for (; (line = agets(in)) != NULL; free(line)) {
	    if (fputs(line, out) == EOF || putc('\n', out) == EOF) {
		dbprintf(("%s: writing to %s: %s\n",
			   prefix, incrname, strerror(errno)));
		goto common_exit;
	    }
	}
	amfree(line);

	if (ferror(in)) {
	    dbprintf(("%s: reading from %s: %s\n",
		      prefix, inputname, strerror(errno)));
	    goto common_exit;
	}
	if (fclose(in) == EOF) {
	    dbprintf(("%s: closing %s: %s\n",
		      prefix, inputname, strerror(errno)));
	    in = NULL;
	    goto common_exit;
	}
	in = NULL;
	if (fclose(out) == EOF) {
	    dbprintf(("%s: closing %s: %s\n",
		      prefix, incrname, strerror(errno)));
	    out = NULL;
	    goto common_exit;
	}
	out = NULL;

	amfree(inputname);
	amfree(basename);
    }
#endif

    gmtm = gmtime(&dumpsince);
    snprintf(dumptimestr, sizeof(dumptimestr),
		"%04d-%02d-%02d %2d:%02d:%02d GMT",
		gmtm->tm_year + 1900, gmtm->tm_mon+1, gmtm->tm_mday,
		gmtm->tm_hour, gmtm->tm_min, gmtm->tm_sec);

    dirname = amname_to_dirname(amdevice);

    cmd = vstralloc(libexecdir, "/", "runtar", versionsuffix(), NULL);

#ifdef GNUTAR
    my_argv[i++] = GNUTAR;
#else
    my_argv[i++] = "tar";
#endif
    my_argv[i++] = "--create";
    my_argv[i++] = "--file";
    my_argv[i++] = "/dev/null";
    my_argv[i++] = "--directory";
    my_argv[i++] = dirname;
    my_argv[i++] = "--one-file-system";
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
    my_argv[i++] = "--listed-incremental";
    my_argv[i++] = incrname;
#else
    my_argv[i++] = "--incremental";
    my_argv[i++] = "--newer";
    my_argv[i++] = dumptimestr;
#endif
#ifdef ENABLE_GNUTAR_ATIME_PRESERVE
    /* --atime-preserve causes gnutar to call
     * utime() after reading files in order to
     * adjust their atime.  However, utime()
     * updates the file's ctime, so incremental
     * dumps will think the file has changed. */
    my_argv[i++] = "--atime-preserve";
#endif
    my_argv[i++] = "--sparse";
    my_argv[i++] = "--ignore-failed-read";
    my_argv[i++] = "--totals";

    if(file_exclude) {
	my_argv[i++] = "--exclude-from";
	my_argv[i++] = file_exclude;
    }

    if(file_include) {
	my_argv[i++] = "--files-from";
	my_argv[i++] = file_include;
    }
    else {
	my_argv[i++] = ".";
    }
    my_argv[i++] = NULL;

    nullfd = open("/dev/null", O_RDWR);
    dumppid = pipespawnv(cmd, STDERR_PIPE, &nullfd, &nullfd, &pipefd, my_argv);
    amfree(cmd);
    amfree(file_exclude);
    amfree(file_include);
    amfree(my_argv);

    dumpout = fdopen(pipefd,"r");

    for(size = -1; (line = agets(dumpout)) != NULL; free(line)) {
	dbprintf(("%s%s\n", prefix_line, line));
	size = handle_dumpline(line);
	if(size > -1) {
	    amfree(line);
	    if((line = agets(dumpout)) != NULL) {
		dbprintf(("%s%s\n", prefix_line, line));
	    }
	    break;
	}
    }
    amfree(line);

    dbprintf(("%s.....\n", prefix_line));
    if(size == -1)
	dbprintf(("%s: (no size line match in above gnutar output)\n%s.....\n",
		  prefix, prefix_line));
    if(size==0 && level ==0)
	size=-1;

    kill(-dumppid, SIGTERM);

    wait(NULL);

common_exit:

    if (incrname) {
	unlink(incrname);
    }
    amfree(incrname);
    amfree(basename);
    amfree(dirname);
    amfree(inputname);

    aclose(nullfd);
    afclose(dumpout);
    afclose(in);
    afclose(out);

    return size;
}
#endif

long getsize_wrapper(program, disk, amdevice, level, options, dumpsince)
char *program, *disk, *amdevice;
int level;
option_t *options;
time_t dumpsince;
{
    int pipefd[2], nullfd, dumppid;
    long size;
    FILE *dumpout;
    char *line = NULL;
    char *cmd = NULL;
    char dumptimestr[80];
    struct tm *gmtm;
    int  i, j;
    char *argvchild[10];
    char *newoptstr;
    long size1, size2;

    gmtm = gmtime(&dumpsince);
    snprintf(dumptimestr, sizeof(dumptimestr),
		"%04d-%02d-%02d %2d:%02d:%02d GMT",
		gmtm->tm_year + 1900, gmtm->tm_mon+1, gmtm->tm_mday,
		gmtm->tm_hour, gmtm->tm_min, gmtm->tm_sec);

    cmd = vstralloc(DUMPER_DIR, "/", program, NULL);

    i=0;
    argvchild[i++] = program;
    argvchild[i++] = "estimate";
    if(level == 0)
	argvchild[i++] = "full";
    else {
	char levelstr[20];
	snprintf(levelstr,19,"%d",level);
	argvchild[i++] = "level";
	argvchild[i++] = levelstr;
    }
    argvchild[i++] = amdevice;
    newoptstr = vstralloc(options->str,"estimate-direct;", NULL);
    argvchild[i++] = newoptstr;

    dbprintf(("%s: running \"%s", get_pname(), cmd));
    for(j=1;j<i;j++) dbprintf((" %s",argvchild[j]));
    dbprintf(("\"\n"));
    nullfd = open("/dev/null", O_RDWR);
    pipe(pipefd);

    switch(dumppid = fork()) {
    case -1:
      amfree(cmd);
      return -1;
    default:
      break;
    case 0:
      dup2(nullfd, 0);
      dup2(nullfd, 2);
      dup2(pipefd[1], 1);
      aclose(pipefd[0]);

      execve(cmd, argvchild, safe_env());
      exit(1);
      break;
    }
    amfree(cmd);

    aclose(pipefd[1]);
    dumpout = fdopen(pipefd[0],"r");

    line = agets(dumpout);
    dbprintf(("DUMP:%s",line));
    i = sscanf(line,"%ld %ld",&size1, &size2);
    if(i == 2) size = size1 * size2;
    else size = -1;
    amfree(line);

    dbprintf((".....\n"));
    if(size == -1)
	dbprintf(("(no size line match in above gnutar output)\n.....\n"));
    if(size==0 && level ==0)
	size=-1;

    kill(-dumppid, SIGTERM);

    wait(NULL);

    aclose(nullfd);
    afclose(dumpout);

    return size;
}


double first_num(str)
char *str;
/*
 * Returns the value of the first integer in a string.
 */
{
    char *start;
    int ch;
    double d;

    ch = *str++;
    while(ch && !isdigit(ch)) ch = *str++;
    start = str-1;
    while(isdigit(ch) || (ch == '.')) ch = *str++;
    str[-1] = '\0';
    d = atof(start);
    str[-1] = ch;
    return d;
}


long handle_dumpline(str)
char *str;
/*
 * Checks the dump output line against the error and size regex tables.
 */
{
    regex_t *rp;
    double size;

    /* check for size match */
    for(rp = re_size; rp->regex != NULL; rp++) {
	if(match(rp->regex, str)) {
	    size = ((first_num(str)*rp->scale+1023.0)/1024.0);
	    if(size < 0) size = 1;		/* found on NeXT -- sigh */
	    return (long) size;
	}
    }
    return -1;
}
