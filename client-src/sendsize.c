/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1996 University of Maryland
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
 * $Id: sendsize.c,v 1.50 1998/01/02 03:29:39 jrj Exp $
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

#ifdef SETPGRP_VOID
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

char *pname = "sendsize";

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
    int platter;
    level_estimate_t est[DUMP_LEVELS];
} disk_estimates_t;

disk_estimates_t *est_list;

#define MAXMAXDUMPS 16

int maxdumps = 1, dumpsrunning = 0;
char *host;				/* my hostname from the server */

/* local functions */
int main P((int argc, char **argv));
void add_diskest P((char *disk, int level, char *exclude, int platter, char *prog));
void calc_estimates P((disk_estimates_t *est));
void dump_calc_estimates P((disk_estimates_t *));
void smbtar_calc_estimates P((disk_estimates_t *));
void gnutar_calc_estimates P((disk_estimates_t *));
void generic_calc_estimates P((disk_estimates_t *));



int main(argc, argv)
int argc;
char **argv;
{
    int level, new_maxdumps, platter;
    char *prog, *disk, *dumpdate, *exclude;
    disk_estimates_t *est;
    int scanres;
    char *line;
    char *s, *fp;
    int ch;
    char *err_extra = NULL;

    /* initialize */

    chdir("/tmp");
    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);
    umask(0007);
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

	platter = 0;				/* default platter */
	exclude = "";				/* default is no exclude list */

	skip_whitespace(s, ch);			/* find the platter */
	if(ch != '\0') {
	    if(sscanf(s - 1, "%d", &platter) != 1) {
		err_extra = "bad platter";
		goto err;			/* bad platter */
	    }
	    skip_integer(s, ch);

	    skip_whitespace(s, ch);		/* find the exclusion list */
	    if(ch != '\0') {
		exclude = stralloc2("--", s - 1);
		skip_non_whitespace(s, ch);
		if(ch) {
		    err_extra = "extra text at end";
		    goto err;			/* should have gotten to end */
		}
	    }
	}

	add_diskest(disk, level, exclude, platter, prog);
    }

    finish_amandates();
    free_amandates();

    for(est = est_list; est != NULL; est = est->next)
	calc_estimates(est);

    while(dumpsrunning > 0) {
      wait(NULL);
      --dumpsrunning;
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


void add_diskest(disk, level, exclude, platter, prog)
char *disk, *prog;
char *exclude;
int level, platter;
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
    newp->dirname = stralloc(amname_to_dirname(newp->amname));
    newp->exclude = stralloc(exclude);
    newp->program = stralloc(prog);
    newp->platter = platter;
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
	dbprintf(("%s: execve %s returned: %s", pname, cmd, strerror(errno)));
	exit(1);
    }
    for(i = 0; i < argc; i++) {
	afree(argv[i]);
    }
    afree(cmd);

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
		      pname, est->amname, level));
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
		      pname, est->amname, level));
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
		    pname, est->amname, level));
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
    {"  DUMP: estimated [0-9][0-9]* tape blocks", 1024},
    {"  DUMP: [Ee]stimated [0-9][0-9]* blocks", 512},
    {"  DUMP: [Ee]stimated [0-9][0-9]* bytes", 1},	       /* Ultrix 4.4 */
    {" UFSDUMP: estimated [0-9][0-9]* blocks", 512},           /* NEC EWS-UX */
    {"vdump: Dumping [0-9][0-9]* bytes, ", 1},		      /* OSF/1 vdump */
    {"dump: Estimate: [0-9][0-9]* tape blocks", 1024},		    /* OSF/1 */
    {"vxdump: estimated [0-9][0-9]* blocks", 512},          /* HPUX's vxdump */
    {"backup: There are an estimated [0-9][0-9]* tape blocks.",1024}, /* AIX */
    {"backup: estimated [0-9][0-9]* 1k blocks", 1024},		      /* AIX */
    {"backup: estimated [0-9][0-9]* tape blocks", 1024},	      /* AIX */
    {"backup: [0-9][0-9]* tape blocks on [0-9][0-9]* tape(s)",1024},  /* AIX */
    {"backup: [0-9][0-9]* 1k blocks on [0-9][0-9]* volume(s)",1024},  /* AIX */
    {"[0-9][0-9]* blocks, [0-9][0-9]*.[0-9][0-9]* volumes", 1024},
                                                          /* DU 3.2g dump -E */
    {"dump: Estimate: [0-9][0-9]* blocks being output to pipe",1024},
                                                              /* DU 4.0 dump */
    {"dump: Dumping [0-9][0-9]* bytes, ", 1},                /* DU 4.0 vdump */
    {"DUMP: estimated [0-9][0-9]* KB output", 1024},                 /* HPUX */
    {"xfsdump: estimated dump size: [0-9][0-9]* bytes", 1},  /* Irix 6.2 xfs */
    {"  UFSDUMP: estimated [0-9][0-9]* blocks", 512},               /* Sinix */
    {"  VXDUMP: estimated [0-9][0-9]* blocks", 512},                /* Sinix */
    {"Total bytes listed: [0-9][0-9]*", 1},		     /* Samba client */
    {"Total bytes written: [0-9][0-9]*", 1},		    /* Gnutar client */

    { NULL, 0 }
};


long getsize_dump(disk, level)
char *disk;
int level;
{
    int pipefd[2], nullfd, dumppid;
    long size;
    FILE *dumpout;
    char *dumpkeys = NULL;
    char *device = NULL;
    int status;
    char *cmd = NULL;
    char *line = NULL;
    char level_str[NUM_STR_SIZE];
    pid_t p;
    int sig;
    int s;

    ap_snprintf(level_str, sizeof(level_str), "%d", level);

#ifdef OSF1_VDUMP
    device = stralloc(amname_to_dirname(disk));
#else
    device = stralloc(amname_to_devname(disk));
#endif

    cmd = vstralloc(libexecdir, "/", "rundump", versionsuffix(), NULL);

    nullfd = open("/dev/null", O_RDWR);
    pipe(pipefd);
#ifdef XFSDUMP						/* { */
#ifdef DUMP						/* { */
    if (strcmp(amname_to_fstype(device), "xfs") == 0)
#else							/* } { */
    if (1)
#endif							/* } */
    {
#ifdef USE_RUNDUMP					/* { */
        char *name = " (xfsdump)";
#else							/* } { */
        char *name = "";
	cmd = newstralloc(cmd, XFSDUMP);
#endif							/* } */
	dbprintf(("%s: running \"%s%s -F -J -l %s - %s\"\n",
		  pname, cmd, name, level_str, device));
    }
    else
#endif							/* } */
#ifdef VXDUMP						/* { */
#ifdef DUMP						/* { */
    if (strcmp(amname_to_fstype(device), "vxfs") == 0)
#else							/* } { */
    if (1)
#endif							/* } */
    {
#ifdef USE_RUNDUMP					/* { */
        char *name = " (vxdump)";
#else							/* } { */
	char *name = "";

	cmd = newstralloc(cmd, VXDUMP);
#endif							/* } */
	dumpkeys = vstralloc(level_str, "s", "f", NULL);
        dbprintf(("%s: running \"%s%s %s 100000 - %s\"\n",
		  pname, cmd, name, dumpkeys, device));
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
		  pname, cmd, name, dumpkeys, device));
# else							/* } { */
	dumpkeys = vstralloc(level_str,
#  ifdef HAVE_DUMP_ESTIMATE				/* { */
			     "E",
#  else							/* } { */
			     "",
#  endif						/* } */
#  ifdef OSF1_VDUMP					/* { */
			     "b"
#  else							/* } { */
			     "s"
#  endif						/* } */
			     "f",
			     NULL);

#  ifdef OSF1_VDUMP					/* { */
	dbprintf(("%s: running \"%s%s %s 60 - %s\"\n",
		  pname, cmd, name, dumpkeys, device));
#  else							/* } { */
	dbprintf(("%s: running \"%s%s %s 100000 - %s\"\n",
		  pname, cmd, name, dumpkeys, device));
#  endif						/* } */
# endif							/* } */
	afree(name);
    }
    else
#endif							/* } */
    {
        dbprintf(("%s: no dump program available", pname));
	error("%s: no dump program available", pname);
    }

    switch(dumppid = fork()) {
    case -1:
	afree(dumpkeys);
	afree(cmd);
	afree(device);
	return -1;
    default:
	break; 
    case 0:	/* child process */
#ifndef HAVE_DUMP_ESTIMATE
	if(SETPGRP == -1)
	    SETPGRP_FAILED();
#endif

	dup2(nullfd, 0);
	dup2(nullfd, 1);
	dup2(pipefd[1], 2);
	aclose(pipefd[0]);

#ifdef XFSDUMP
	if (strcmp(amname_to_fstype(device), "xfs") == 0)
	    execle(cmd, "xfsdump", "-F", "-J", "-l", dumpkeys, "-", device,
		   (char *)0, safe_env());
	else
#endif
#ifdef VXDUMP
	if (strcmp(amname_to_fstype(device), "vxfs") == 0)
	    execle(cmd, "vxdump", dumpkeys, "100000", "-", device, (char *)0,
		   safe_env());
	else
#endif
#ifdef DUMP
# ifdef AIX_BACKUP
	    execle(cmd, "backup", dumpkeys, "-", device, (char *)0, safe_env());
# else
#  ifdef OSF1_VDUMP
	    execle(cmd, "dump", dumpkeys, "60", "-", device, (char *)0,
		   safe_env());
#  else
	    execle(cmd, "dump", dumpkeys, "100000", "-", device, (char *)0,
		   safe_env());
#  endif
# endif
#endif
	{
	  dbprintf(("%s: exec %s failed or no dump program available",
		    pname, cmd));
	  error("%s: exec %s failed or no dump program available",
		pname, cmd);
	  exit(1);
	}
    }

    afree(dumpkeys);
    afree(cmd);

    aclose(pipefd[1]);
    dumpout = fdopen(pipefd[0],"r");

    for(size = -1; (line = agets(dumpout)) != NULL; free(line)) {
	dbprintf(("%s\n",line));
	size = handle_dumpline(line);
	if(size > -1) {
	    afree(line);
	    if((line = agets(dumpout)) != NULL) {
		dbprintf(("%s\n",line));
	    }
	    break;
	}
    }
    afree(line);

    dbprintf((".....\n"));
    if(size == -1)
	dbprintf(("(no size line match in above dump output)\n.....\n"));
    if(size == 0 && level == 0)
	dbprintf(("(PC SHARE connection problem, is this disk really empty?)\n.....\n"));

#ifndef HAVE_DUMP_ESTIMATE
#ifdef OSF1_VDUMP
    sleep(5);
#endif
    /*
     * First, try to kill the dump process nicely.  If it ignores us
     * for several seconds, hit it harder.
     */
#ifdef XFSDUMP
    /*
     * We know xfsdump ignores SIGTERM, so arrange to hit it hard.
     */
    sig = (strcmp(amname_to_fstype(device), "xfs") == 0 ? SIGKILL : SIGTERM;
#else
    sig = SIGTERM;
#endif
    dbprintf(("sending signal %d to process group %ld\n", sig, (long)dumppid));
    kill(-dumppid, sig);
    for(s = 5; s > 0 && (p = waitpid(dumppid, &status, WNOHANG)) == 0; s--) {
	sleep(1);
    }
    if(p == 0) {
        dbprintf(("sending KILL to process group %ld\n", (long)dumppid));
	kill(-dumppid, SIGKILL);
	wait(&status);
    }
#else /* HAVE_DUMP_ESTIMATE */
    wait(&status);
#endif /* HAVE_DUMP_ESTIMATE */

    aclose(nullfd);
    afclose(dumpout);

    afree(device);

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

    if ((pass = findpass(disk, &domain) == NULL) {
	error("[sendsize : error in smbtar diskline, unable to find password]");
    }
    if ((sharename = makesharename(disk, 0)) == NULL) {
	memset(pass, '\0', strlen(pass));
	afree(pass);
	if(domain) {
	    memset(domain, '\0', strlen(domain));
	    afree(domain);
	}
	error("[sendsize : can't make share name of %s]", disk);
    }
    nullfd = open("/dev/null", O_RDWR);
    pipe(pipefd);
    if (level == 0)
	tarkeys = "archive 0;recurse;dir";
    else
	tarkeys = "archive 1;recurse;dir";

    dbprintf(("%s: running \"%s \'%s\' %s -d 3 -U %s -E%s%s -c \'%s\'\"\n",
	      pname, SAMBA_CLIENT, sharename, "XXXXX", SAMBA_USER,
	      domain ? " -W " : "", domain,
	      tarkeys));

    switch(dumppid = fork()) {
    case -1:
	memset(pass, '\0', strlen(pass));
	afree(pass);
	if(domain) {
	    memset(domain, '\0', strlen(domain));
	    afree(domain);
	}
	afree(sharename);
	return -1;
    default:
	afree(sharename);
	break; 
    case 0:   /* child process */
	dup2(nullfd, 0);
	dup2(nullfd, 1);
	dup2(pipefd[1], 2);
	aclose(pipefd[0]);

	execle(SAMBA_CLIENT, "smbclient", sharename, pass,
	       "-d", "3",
	       "-U", SAMBA_USER,
	       "-E",
	       domain ? "-W" : "-c",
	       domain ? domain : tarkeys,
	       domain ? "-c" : (char *)0,
	       domain ? tarkeys : (char *)0,
	       (char *)0,
	       safe_env());
	/* should not get here */
	memset(pass, '\0', strlen(pass));
	afree(pass);
	if(domain) {
	    memset(domain, '\0', strlen(domain));
	    afree(domain);
	}
	afree(sharename);
	exit(1);
	/* NOTREACHED */
    }
    memset(pass, '\0', strlen(pass));
    afree(pass);
    if(domain) {
	memset(domain, '\0', strlen(domain));
	afree(domain);
    }
    afree(sharename);
    aclose(pipefd[1]);
    dumpout = fdopen(pipefd[0],"r");

    for(size = -1; (line = agets(dumpout)) != NULL; free(line)) {
	dbprintf(("%s\n",line));
	size = handle_dumpline(line);
	if(size > -1) {
	    afree(line);
	    if((line = agets(dumpout)) != NULL) {
		dbprintf(("%s",line));
	    }
	    break;
	}
    }
    afree(line);

    dbprintf((".....\n"));
    if(size == -1)
	dbprintf(("(no size line match in above smbclient output)\n.....\n"));
    if(size==0 && level ==0)
	size=-1;

    kill(-dumppid, SIGTERM);

    aclose(nullfd);
    afclose(dumpout);

    return size;
}
#endif

#ifdef GNUTAR
long getsize_gnutar(disk, level, efile, dumpsince)
char *disk;
int level;
char *efile;
time_t dumpsince;
{
    int pipefd[2], nullfd, dumppid;
    long size;
    FILE *dumpout;
    char *incrname;
    char *dirname;
    char *line = NULL;
    char *cmd = NULL;
    char *cmd_line;
    char dumptimestr[80];
    struct tm *gmtm;

#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
    {
      int i;
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
      umask(0007);

      if (level == 0) {
	FILE *out;

notincremental:

	out = fopen(incrname, "w");
	if (out == NULL) {
	  dbprintf(("error [opening %s: %s]\n", incrname, strerror(errno)));
	  afree(incrname);
	  afree(basename);
	  return -1;
	}

	if (fclose(out) == EOF) {
	  dbprintf(("error [closing %s: %s]\n", incrname, strerror(errno)));
	  out = NULL;
	  afree(incrname);
	  afree(basename);
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
	  afree(inputname);
	  goto notincremental;
	}

	out = fopen(incrname, "w");
	if (out == NULL) {
	  dbprintf(("error [opening %s: %s]\n", incrname, strerror(errno)));
	  afree(incrname);
	  afree(basename);
	  afree(inputname);
	  return -1;
	}

	while(fgets(buf, sizeof(buf), in) != NULL)
	  if (fputs(buf, out) == EOF) {
	    dbprintf(("error [writing to %s: %s]\n", incrname,
		      strerror(errno)));
	    afree(incrname);
	    afree(basename);
	    afree(inputname);
	    return -1;
	  }

	if (ferror(in)) {
	  dbprintf(("error [reading from %s: %s]\n", inputname,
		    strerror(errno)));
	  afree(incrname);
	  afree(basename);
	  afree(inputname);
	  return -1;
	}

	if (fclose(in) == EOF) {
	  dbprintf(("error [closing %s: %s]\n", inputname, strerror(errno)));
	  in = NULL;
	  afree(incrname);
	  afree(basename);
	  afree(inputname);
	  return -1;
	}
	in = NULL;

	if (fclose(out) == EOF) {
	  dbprintf(("error [closing %s: %s]\n", incrname, strerror(errno)));
	  out = NULL;
	  afree(incrname);
	  afree(basename);
	  afree(inputname);
	  return -1;
	}
	out = NULL;

	afree(inputname);
      }
      afree(basename);
    }
#endif

    gmtm = gmtime(&dumpsince);
    ap_snprintf(dumptimestr, sizeof(dumptimestr),
		"%04d-%02d-%02d %2d:%02d:%02d GMT",
		gmtm->tm_year + 1900, gmtm->tm_mon+1, gmtm->tm_mday,
		gmtm->tm_hour, gmtm->tm_min, gmtm->tm_sec);

    dirname = amname_to_dirname(disk);

    cmd = vstralloc(libexecdir, "/", "runtar", versionsuffix(), NULL);

    if (efile == NULL) {
	/* do nothing */
#define sc "--exclude-list"
    } else if (strncmp(efile, sc, sizeof(sc)-1)==0) {
	efile = newstralloc(efile, "--exclude-from");
#undef sc
#define sc "--exclude-file"
    } else if (strncmp(efile, "--exclude-file", strlen("--exclude-file"))==0) {
	char *t;

	t = stralloc2("--exclude", efile+sizeof(sc)-1);
	afree(efile);
	efile = t;
	t = NULL;
#undef sc
    } else {
	dbprintf(("error [efile not --exclude-list or -file: %s]\n", efile));
	error("error: efile not --exclude-list or -file: %s]", efile);
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
			 " --file", "/dev/null",
			 " ", efile ? efile : ".",
			 efile ? " ." : "",
			 NULL);

    dbprintf(("%s: running \"%s\"\n", pname, cmd_line));

    nullfd = open("/dev/null", O_RDWR);
    pipe(pipefd);

    switch(dumppid = fork()) {
    case -1:
      afree(cmd);
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
	     efile[0] ? efile : ".",
	     efile[0] ? "." : (char *)0,
	     (char *)0,
	     safe_env());

      exit(1);
      break;
    }
    afree(cmd);

    aclose(pipefd[1]);
    dumpout = fdopen(pipefd[0],"r");

    for(size = -1; (line = agets(dumpout)) != NULL; free(line)) {
	dbprintf(("%s\n",line));
	size = handle_dumpline(line);
	if(size > -1) {
	    afree(line);
	    if((line = agets(dumpout)) != NULL) {
		dbprintf(("%s\n",line));
	    }
	    break;
	}
    }
    afree(line);

    dbprintf((".....\n"));
    if(size == -1)
	dbprintf(("(no size line match in above gnutar output)\n.....\n"));
    if(size==0 && level ==0)
	size=-1;

    kill(-dumppid, SIGTERM);

    unlink(incrname);
    afree(incrname);

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
    char *start, *tp;
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

    /* check for size match */
    for(rp = re_size; rp->regex != NULL; rp++) {
	if(match(rp->regex, str))
	    return (long) ((first_num(str)*rp->scale+1023.0)/1024.0);
    }
    return -1;
}
