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
 * $Id: sendsize.c,v 1.97.2.9 1999/09/11 00:38:25 jrj Exp $
 *
 * send estimated backup sizes using dump
 */

#include "amanda.h"
#include "amandates.h"
#include "getfsent.h"
#include "version.h"

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
    char *dirname;
    char *exclude;
    char *program;
    int spindle;
    level_estimate_t est[DUMP_LEVELS];
} disk_estimates_t;

disk_estimates_t *est_list;

#define MAXMAXDUMPS 16

int maxdumps = 1, dumpsrunning = 0;
char *host;				/* my hostname from the server */

/* local functions */
int main P((int argc, char **argv));
void add_diskest P((char *disk, int level, char *exclude, int spindle, char *prog));
void calc_estimates P((disk_estimates_t *est));
void free_estimates P((disk_estimates_t *est));
void dump_calc_estimates P((disk_estimates_t *));
void smbtar_calc_estimates P((disk_estimates_t *));
void gnutar_calc_estimates P((disk_estimates_t *));
void generic_calc_estimates P((disk_estimates_t *));



int main(argc, argv)
int argc;
char **argv;
{
    int level, new_maxdumps, spindle;
    char *prog, *disk, *dumpdate, *exclude = NULL;
    disk_estimates_t *est;
    disk_estimates_t *est_prev;
    char *line = NULL;
    char *s, *fp;
    int ch;
    char *err_extra = NULL;
    int fd;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;

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

	skip_whitespace(s, ch);			/* find the disk name */
	if(ch == '\0') {
	    err_extra = "no disk name";
	    goto err;				/* no disk name */
	}
	disk = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';

	skip_whitespace(s, ch);			/* find the level number */
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
	amfree(exclude);				/* default is no exclude */

	skip_whitespace(s, ch);			/* find the spindle */
	if(ch != '\0') {
	    if(sscanf(s - 1, "%d", &spindle) != 1) {
		err_extra = "bad spindle";
		goto err;			/* bad spindle */
	    }
	    skip_integer(s, ch);

	    skip_whitespace(s, ch);		/* find the exclusion list */
	    if(ch != '\0') {
		exclude = newstralloc2(exclude, "--", s - 1);
		skip_non_whitespace(s, ch);
		if(ch) {
		    err_extra = "extra text at end";
		    goto err;			/* should have gotten to end */
		}
	    }
	}

	add_diskest(disk, level, exclude, spindle, prog);
    }
    amfree(line);

    finish_amandates();
    free_amandates();

    for(est = est_list; est != NULL; est = est->next)
	calc_estimates(est);

    while(dumpsrunning > 0) {
      wait(NULL);
      --dumpsrunning;
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


void add_diskest(disk, level, exclude, spindle, prog)
char *disk, *prog;
char *exclude;
int level, spindle;
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
    newp->dirname = amname_to_dirname(newp->amname);
    newp->exclude = exclude ? stralloc(exclude) : NULL;
    newp->program = stralloc(prog);
    newp->spindle = spindle;
    newp->est[level].needestimate = 1;

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
    amfree(est->exclude);
    amfree(est->program);
}

/*
 * ------------------------------------------------------------------------
 *
 */

void calc_estimates(est)
disk_estimates_t *est;
{
    dbprintf(("calculating for amname '%s', dirname '%s'\n", est->amname,
	      est->dirname));
    if (maxdumps > 1) {
      while(dumpsrunning >= maxdumps) {
	wait(NULL);
	--dumpsrunning;
      }
      ++dumpsrunning;
      switch(fork()) {
      case 0:
	break;
      case -1:
        error("calc_estimates: fork returned: %s", strerror(errno));
      default:
	return;
      }
    }

    /* Now in the child process */
#ifndef USE_GENERIC_CALCSIZE
    if(strcmp(est->program, "DUMP") == 0)
	dump_calc_estimates(est);
    else
#endif
#ifdef SAMBA_CLIENT
      if (strcmp(est->program, "GNUTAR") == 0 &&
	  est->amname[0] == '/' && est->amname[1] == '/')
	smbtar_calc_estimates(est);
      else
#endif
#ifdef GNUTAR
	if (strcmp(est->program, "GNUTAR") == 0)
	  gnutar_calc_estimates(est);
	else
#endif
	  generic_calc_estimates(est);
    if (maxdumps > 1)
      exit(0);
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
    argv[argc++] = stralloc(est->amname);
    argv[argc++] = stralloc(est->dirname);

    dbprintf(("%s: running cmd:", argv[0]));
    for(i=0; i<argc; ++i)
	dbprintf((" %s", argv[i]));

    for(level = 0; level < DUMP_LEVELS; level++) {
	if(est->est[level].needestimate) {
	    ap_snprintf(number, sizeof(number), "%d", level);
	    argv[argc++] = stralloc(number); 
	    dbprintf((" %s", number));
	    ap_snprintf(number, sizeof(number),
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
        error("%s: fork returned: %s", cmd, strerror(errno));
    default:
        break;
    case 0:
	execve(cmd, argv, safe_env());
	dbprintf(("%s: execve %s returned: %s",
		  get_pname(), cmd, strerror(errno)));
	exit(1);
    }
    for(i = 0; i < argc; i++) {
	amfree(argv[i]);
    }
    amfree(cmd);

    wait(NULL);
}


/*
 * ------------------------------------------------------------------------
 *
 */

/* local functions */
void dump_calc_estimates P((disk_estimates_t *est));
long getsize_dump P((char *disk, int level));
long getsize_smbtar P((char *disk, int level));
long getsize_gnutar P((char *disk, int level,
		       char *exclude, time_t dumpsince));
long handle_dumpline P((char *str));
double first_num P((char *str));

void dump_calc_estimates(est)
disk_estimates_t *est;
{
    int level;
    long size;

    for(level = 0; level < DUMP_LEVELS; level++) {
	if(est->est[level].needestimate) {
	    dbprintf(("%s: getting size via dump for %s level %d\n",
		      get_pname(), est->amname, level));
	    size = getsize_dump(est->amname, level);

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
		      get_pname(), est->amname, level));
	    size = getsize_smbtar(est->amname, level);

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
		    get_pname(), est->amname, level));
	  size = getsize_gnutar(est->amname, level,
				est->exclude, est->est[level].dumpsince);

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


long getsize_dump(disk, level)
    char *disk;
    int level;
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

    ap_snprintf(level_str, sizeof(level_str), "%d", level);

    device = amname_to_devname(disk);
    fstype = amname_to_fstype(disk);

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
		  get_pname(), cmd, name, level_str, device));
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
		  get_pname(), cmd, name, dumpkeys, device));
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
	device = amname_to_dirname(disk);
	dumpkeys = vstralloc(level_str, "b", "f", NULL);
	dbprintf(("%s: running \"%s%s %s 60 - %s\"\n",
		  get_pname(), cmd, name, dumpkeys, device));
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
		  get_pname(), cmd, name, dumpkeys, device));
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
		  get_pname(), cmd, name, dumpkeys, device));
#  else							/* } { */
	dbprintf(("%s: running \"%s%s %s 1048576 - %s\"\n",
		  get_pname(), cmd, name, dumpkeys, device));
#  endif						/* } */
# endif							/* } */
	amfree(name);
    }
    else
#endif							/* } */
    {
        dbprintf(("%s: no dump program available", get_pname()));
	error("%s: no dump program available", get_pname());
    }

    pipe(killctl);

    switch(dumppid = fork()) {
    case -1:
	dbprintf(("cannot fork for killpgrp: %s\n", strerror(errno)));
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
	    dbprintf(("pipe for killpgrp failed, trying without killpgrp\n"));
	else {
	    switch(fork()) {
	    case -1:
		dbprintf(("fork failed, trying without killpgrp\n"));
		break;

	    default:
	    {
		char *killpgrp_cmd = vstralloc(libexecdir, "/killpgrp",
					       versionsuffix(), NULL);
		dbprintf(("running %s\n",killpgrp_cmd));
		dup2(killctl[0], 0);
		dup2(nullfd, 1);
		dup2(nullfd, 2);
		close(pipefd[0]);
		close(pipefd[1]);
		close(killctl[1]);
		close(nullfd);
		execle(killpgrp_cmd, killpgrp_cmd, (char *)0, safe_env());
		dbprintf(("cannot execute %s: %s\n", killpgrp_cmd,
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
	  char *e;

	  e = strerror(errno);
	  dbprintf(("%s: exec %s failed or no dump program available: %s\n",
		    get_pname(), cmd, e));
	  error("%s: exec %s failed or no dump program available: %s",
		get_pname(), cmd, e);
	  exit(1);
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
	dbprintf(("%s\n",line));
	size = handle_dumpline(line);
	if(size > -1) {
	    amfree(line);
	    if((line = agets(dumpout)) != NULL) {
		dbprintf(("%s\n",line));
	    }
	    break;
	}
    }
    amfree(line);

    dbprintf((".....\n"));
    if(size == -1)
	dbprintf(("(no size line match in above dump output)\n.....\n"));
    if(size == 0 && level == 0)
	dbprintf(("(PC SHARE connection problem, is this disk really empty?)\n.....\n"));

    if (killctl[1] != -1) {
	dbprintf(("asking killpgrp to terminate\n"));
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
    dbprintf(("sending SIGTERM to process group %ld\n", (long) dumppid));
    if (kill(-dumppid, SIGTERM) == -1) {
	dbprintf(("kill failed: %s\n", strerror(errno)));
    }
    /* Now check whether it dies */
    for(s = 5; s > 0; --s) {
	sleep(1);
	if (waitpid(dumppid, NULL, WNOHANG) != -1)
	    goto terminated;
    }

    dbprintf(("sending SIGKILL to process group %ld\n", (long) dumppid));
    if (kill(-dumppid, SIGKILL) == -1) {
	dbprintf(("kill failed: %s\n", strerror(errno)));
    }
    for(s = 5; s > 0; --s) {
	sleep(1);
	if (waitpid(dumppid, NULL, WNOHANG) != -1)
	    goto terminated;
    }

    dbprintf(("cannot kill it, waiting for normal termination\n"));
    wait(NULL);

 terminated:

    aclose(nullfd);
    afclose(dumpout);

    amfree(device);
    amfree(fstype);

    return size;
}

#ifdef SAMBA_CLIENT
long getsize_smbtar(disk, level)
     char *disk;
     int level;
{
    int pipefd[2], nullfd, dumppid;
    long size;
    FILE *dumpout;
    char *tarkeys, *sharename, *pass, *domain = NULL;
    char *line;
    int debug_level;

    if ((pass = findpass(disk, &domain)) == NULL) {
	error("[sendsize : error in smbtar diskline, unable to find password]");
    }
    if ((sharename = makesharename(disk, 0)) == NULL) {
	memset(pass, '\0', strlen(pass));
	amfree(pass);
	if(domain) {
	    memset(domain, '\0', strlen(domain));
	    amfree(domain);
	}
	error("[sendsize : can't make share name of %s]", disk);
    }
    nullfd = open("/dev/null", O_RDWR);
    pipe(pipefd);
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

    dbprintf(("%s: running \"%s \'%s\' %s -d %s -U %s -E%s%s -c \'%s\'\"\n",
	      get_pname(), SAMBA_CLIENT, sharename, "XXXXX",
	      SAMBA_DEBUG_LEVEL, SAMBA_USER, domain ? " -W " : "",
	      domain ? domain : "", tarkeys));

    switch(dumppid = fork()) {
    case -1:
	dbprintf(("fork for %s failed: %s\n", SAMBA_CLIENT, strerror(errno)));
	memset(pass, '\0', strlen(pass));
	amfree(pass);
	if(domain) {
	    memset(domain, '\0', strlen(domain));
	    amfree(domain);
	}
	amfree(sharename);
	return -1;
    default:
	amfree(sharename);
	break; 
    case 0:   /* child process */
	dup2(nullfd, 0);
	dup2(nullfd, 1);
	dup2(pipefd[1], 2);
	aclose(pipefd[0]);

	execle(SAMBA_CLIENT, "smbclient", sharename, pass,
	       "-d", SAMBA_DEBUG_LEVEL,
	       "-U", SAMBA_USER,
	       "-E",
	       domain ? "-W" : "-c",
	       domain ? domain : tarkeys,
	       domain ? "-c" : (char *)0,
	       domain ? tarkeys : (char *)0,
	       (char *)0,
	       safe_env());
	/* should not get here */
	dbprintf(("execle of %s failed: %s\n", SAMBA_CLIENT, strerror(errno)));
	memset(pass, '\0', strlen(pass));
	amfree(pass);
	if(domain) {
	    memset(domain, '\0', strlen(domain));
	    amfree(domain);
	}
	amfree(sharename);
	exit(1);
	/* NOTREACHED */
    }
    memset(pass, '\0', strlen(pass));
    amfree(pass);
    if(domain) {
	memset(domain, '\0', strlen(domain));
	amfree(domain);
    }
    amfree(sharename);
    aclose(pipefd[1]);
    dumpout = fdopen(pipefd[0],"r");

    for(size = -1; (line = agets(dumpout)) != NULL; free(line)) {
	dbprintf(("%s\n",line));
	size = handle_dumpline(line);
	if(size > -1) {
	    amfree(line);
	    if((line = agets(dumpout)) != NULL) {
		dbprintf(("%s",line));
	    }
	    break;
	}
    }
    amfree(line);

    dbprintf((".....\n"));
    if(size == -1)
	dbprintf(("(no size line match in above smbclient output)\n.....\n"));
    if(size==0 && level ==0)
	size=-1;

    kill(-dumppid, SIGTERM);

    wait(NULL);

    aclose(nullfd);
    afclose(dumpout);

    return size;
}
#endif

#ifdef GNUTAR
long getsize_gnutar(disk, level, exclude_spec, dumpsince)
char *disk;
int level;
char *exclude_spec;
time_t dumpsince;
{
    int pipefd[2], nullfd, dumppid;
    long size;
    FILE *dumpout;
    char *incrname = NULL;
    char *dirname = NULL;
    char *exclude_arg = NULL;
    char *line = NULL;
    char *cmd = NULL;
    char *cmd_line;
    char dumptimestr[80];
    struct tm *gmtm;

#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
    {
      char *basename = NULL;
      char number[NUM_STR_SIZE];
      char *s;
      int ch;

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

      ap_snprintf(number, sizeof(number), "%d", level);
      incrname = vstralloc(basename, "_", number, ".new", NULL);
      unlink(incrname);

      if (level == 0) {
	FILE *out;

notincremental:

	out = fopen(incrname, "w");
	if (out == NULL) {
	  dbprintf(("error [opening %s: %s]\n", incrname, strerror(errno)));
	  amfree(incrname);
	  amfree(basename);
	  amfree(dirname);
	  return -1;
	}

	if (fclose(out) == EOF) {
	  dbprintf(("error [closing %s: %s]\n", incrname, strerror(errno)));
	  out = NULL;
	  amfree(incrname);
	  amfree(basename);
	  amfree(dirname);
	  return -1;
	}
	out = NULL;

      } else {
	FILE *in = NULL, *out;
	char *inputname = NULL;
	char buf[BUFSIZ];
	int baselevel = level;

	while (in == NULL && --baselevel >= 0) {
	  ap_snprintf(number, sizeof(number), "%d", baselevel);
	  inputname = newvstralloc(inputname, basename, "_", number, NULL);
	  in = fopen(inputname, "r");
	}

	if (in == NULL) {
	  amfree(inputname);
	  goto notincremental;
	}

	out = fopen(incrname, "w");
	if (out == NULL) {
	  dbprintf(("error [opening %s: %s]\n", incrname, strerror(errno)));
	  amfree(incrname);
	  amfree(basename);
	  amfree(inputname);
	  amfree(dirname);
	  return -1;
	}

	while(fgets(buf, sizeof(buf), in) != NULL)
	  if (fputs(buf, out) == EOF) {
	    dbprintf(("error [writing to %s: %s]\n", incrname,
		      strerror(errno)));
	    amfree(incrname);
	    amfree(basename);
	    amfree(inputname);
	    amfree(dirname);
	    return -1;
	  }

	if (ferror(in)) {
	  dbprintf(("error [reading from %s: %s]\n", inputname,
		    strerror(errno)));
	  amfree(incrname);
	  amfree(basename);
	  amfree(inputname);
	  amfree(dirname);
	  return -1;
	}

	if (fclose(in) == EOF) {
	  dbprintf(("error [closing %s: %s]\n", inputname, strerror(errno)));
	  in = NULL;
	  amfree(incrname);
	  amfree(basename);
	  amfree(inputname);
	  amfree(dirname);
	  return -1;
	}
	in = NULL;

	if (fclose(out) == EOF) {
	  dbprintf(("error [closing %s: %s]\n", incrname, strerror(errno)));
	  out = NULL;
	  amfree(incrname);
	  amfree(basename);
	  amfree(inputname);
	  amfree(dirname);
	  return -1;
	}
	out = NULL;

	amfree(inputname);
      }
      amfree(basename);
    }
#endif

    gmtm = gmtime(&dumpsince);
    ap_snprintf(dumptimestr, sizeof(dumptimestr),
		"%04d-%02d-%02d %2d:%02d:%02d GMT",
		gmtm->tm_year + 1900, gmtm->tm_mon+1, gmtm->tm_mday,
		gmtm->tm_hour, gmtm->tm_min, gmtm->tm_sec);

    dirname = amname_to_dirname(disk);

    cmd = vstralloc(libexecdir, "/", "runtar", versionsuffix(), NULL);

    if (exclude_spec == NULL) {
	amfree(exclude_arg);
	/* do nothing */
#define sc "--exclude-list="
    } else if (strncmp(exclude_spec, sc, sizeof(sc)-1)==0) {
	char *file = exclude_spec + sizeof(sc)-1;
/* BEGIN HPS */
	if(*file != '/')
	  file = vstralloc(dirname,"/",file, NULL);
/* END HPS */		
	if (access(file, F_OK) == 0)
	    exclude_arg = newstralloc2(exclude_arg, "--exclude-from=", file);
	else {
	    dbprintf(("%s: missing exclude list file \"%s\" discarded\n",
		      get_pname(), file));
	    amfree(exclude_arg);
	}
#undef sc
#define sc "--exclude-file="
    } else if (strncmp(exclude_spec, sc, sizeof(sc)-1)==0) {
	exclude_arg = newstralloc2(exclude_arg, "--exclude=",
				   exclude_spec+sizeof(sc)-1);
#undef sc
    } else {
	dbprintf(("error [exclude_spec is neither --exclude-list nor --exclude-file: %s]\n", exclude_spec));
	error("error: exclude_spec is neither --exclude-list nor --exclude-file: %s]", exclude_spec);
    }

    cmd_line = vstralloc(cmd,
			 " --create",
			 " --directory ", dirname,
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
			 " --listed-incremental ", incrname,
#else
			 " --incremental",
			 " --newer ", dumptimestr,
#endif
			 " --sparse",
			 " --one-file-system",
#ifdef ENABLE_GNUTAR_ATIME_PRESERVE
			 " --atime-preserve",
#endif
			 " --ignore-failed-read",
			 " --totals",
			 " --file", " /dev/null",
			 " ", exclude_arg ? exclude_arg : ".",
			 exclude_arg ? " ." : "",
			 NULL);

    dbprintf(("%s: running \"%s\"\n", get_pname(), cmd_line));

    nullfd = open("/dev/null", O_RDWR);
    pipe(pipefd);

    switch(dumppid = fork()) {
    case -1:
      dbprintf(("fork for %s failed: %s\n", GNUTAR, strerror(errno)));
      amfree(cmd);
      amfree(dirname);
      return -1;
    default:
      break;
    case 0:
      dup2(nullfd, 0);
      dup2(nullfd, 1);
      dup2(pipefd[1], 2);
      aclose(pipefd[0]);

      execle(cmd,
#ifdef GNUTAR
	     GNUTAR,
#else
	     "tar",
#endif
	     "--create",
	     "--directory", dirname,
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
	     "--listed-incremental", incrname,
#else
	     "--incremental", "--newer", dumptimestr,
#endif
	     "--sparse",
	     "--one-file-system",
#ifdef ENABLE_GNUTAR_ATIME_PRESERVE
	     "--atime-preserve",
#endif
	     "--ignore-failed-read",
	     "--totals",
	     "--file", "/dev/null",
	     exclude_arg ? exclude_arg : ".",
	     exclude_arg ? "." : (char *)0,
	     (char *)0,
	     safe_env());
      dbprintf(("execle of %s failed: %s\n", GNUTAR, strerror(errno)));

      exit(1);
      break;
    }
    amfree(exclude_arg);
    amfree(cmd);

    aclose(pipefd[1]);
    dumpout = fdopen(pipefd[0],"r");

    for(size = -1; (line = agets(dumpout)) != NULL; free(line)) {
	dbprintf(("%s\n",line));
	size = handle_dumpline(line);
	if(size > -1) {
	    amfree(line);
	    if((line = agets(dumpout)) != NULL) {
		dbprintf(("%s\n",line));
	    }
	    break;
	}
    }
    amfree(line);

    dbprintf((".....\n"));
    if(size == -1)
	dbprintf(("(no size line match in above gnutar output)\n.....\n"));
    if(size==0 && level ==0)
	size=-1;

    kill(-dumppid, SIGTERM);

    unlink(incrname);
    amfree(incrname);
    amfree(dirname);

    wait(NULL);

    aclose(nullfd);
    afclose(dumpout);

    return size;
}
#endif


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
