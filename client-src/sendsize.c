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
 * $Id: sendsize.c,v 1.30 1997/09/18 23:47:59 george Exp $
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
#  define SETPGRP_FAILED() \
	    dbprintf(("setpgrp() failed: %s\n", strerror(errno)));

#else
#  define SETPGRP	setpgrp(0, getpid())
#  define SETPGRP_FAILED() \
	    dbprintf(("setpgrp(0,%ld) failed: %s\n", (long) getpid(), \
		      strerror(errno)));

#endif

#define MAXLINE 4096
char line[MAXLINE];
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
    char exclude[256], disk[256], prog[256], opt[256], *str;
    char dumpdate[256];
    disk_estimates_t *est;
    int scanres;

    /* initialize */

    chdir("/tmp");
    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);
    umask(0);
    dbopen("/tmp/sendsize.debug");
    dbprintf(("%s: version %s\n", argv[0], version()));

    /* handle all service requests */

    start_amandates(0);

    while(fgets(line, MAXLINE, stdin)) {
	if(!strncmp(line, "OPTIONS", 7)) {
	    if((str = strstr(line, "MAXDUMPS=")) != NULL &&
	       sscanf(str, "MAXDUMPS=%d", &new_maxdumps) == 1) {
	      if (new_maxdumps > MAXMAXDUMPS)
		maxdumps = MAXMAXDUMPS;
	      else if (new_maxdumps > 0)
		maxdumps = new_maxdumps;
	    }
	    sprintf(opt, "OPTIONS MAXDUMPS=%d;\n", maxdumps);
	    write(1, opt, strlen(opt));
	    continue;
	}

	scanres = sscanf(line, "%s %s %d %s %d %s\n",
			 prog, disk, &level, dumpdate, &platter, exclude+2);
	switch(scanres) {
	case 4:
	  platter = 0;
	  /* do not break; */
	case 5:
	  *exclude = 0;
	  break;
	case 6:
	  exclude[0] = exclude[1] = '-';
	  break;
	default:
	  goto err;
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
    dbprintf(("REQ packet is bogus\n"));
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
	if(!strcmp(curp->amname, disk)) {
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
      default:
	return;
      }
    }
    
    /* Now in the child process */
#ifndef USE_GENERIC_CALCSIZE
    if(!strcmp(est->program, "DUMP"))
	dump_calc_estimates(est);
    else
#endif
#ifdef SAMBA_CLIENT
      if (!strcmp(est->program, "GNUTAR") &&
	  est->amname[0] == '/' && est->amname[1] == '/')
	smbtar_calc_estimates(est);
      else
#endif
#ifdef GNUTAR
	if (!strcmp(est->program, "GNUTAR"))
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
    char cmd[256], line[2048], *str;
    char *argv[DUMP_LEVELS*2+10];
    int i=0, level, argc, calcpid;

    sprintf(cmd, "%s/calcsize%s", libexecdir,versionsuffix());

    argv[i++] = "calcsize";
    argv[i++] = est->program;
#ifdef BUILTIN_EXCLUDE_SUPPORT
    if(est->exclude && *est->exclude) {
	argv[i++] = "-X";
	argv[i++] = est->exclude;
    }
#endif
    argv[i++] = est->amname;
    argv[i++] = est->dirname;
    
    argc = i;
    str = line;

    dbprintf(("%s: running cmd:", argv[0]));
    for(i=0; i<argc; ++i)
	dbprintf((" %s", argv[i]));

    for(level = 0; level < DUMP_LEVELS; level++) {
	if(est->est[level].needestimate) {
	    sprintf(str, "%d", level);
	    argv[argc++] = str; 
	    dbprintf((" %s", str));
	    str += strlen(str) + 1;
	    sprintf(str, "%ld", (long)est->est[level].dumpsince);
	    dbprintf((" %s", str));
	    argv[argc++] = str;
	    str += strlen(str) + 1;
	}
    }
    argv[argc] = 0;
    dbprintf(("\n"));

    fflush(stderr); fflush(stdout);

    switch(calcpid = fork()) {
    case -1:
        error("%s: fork returned: %s", cmd, strerror(errno));
    default:
        break;
    case 0:
	execve(cmd, argv, NULL);
	dbprintf(("%s: execve %s returned: %s", pname, cmd, strerror(errno)));
	exit(1);
    }

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
    char result[1024];

    for(level = 0; level < DUMP_LEVELS; level++) {
	if(est->est[level].needestimate) {
	    dbprintf(("%s: getting size via dump for %s level %d\n",
		      pname, est->amname, level));
	    size = getsize_dump(est->amname, level);
	    sprintf(result, "%s %d SIZE %ld\n", est->amname, level, size);

	    amflock(1, "size");

	    lseek(1, (off_t)0, SEEK_END);
	    write(1, result, strlen(result));

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
    char result[1024];

    for(level = 0; level < DUMP_LEVELS; level++) {
	if(est->est[level].needestimate) {
	    dbprintf(("%s: getting size via smbclient for %s level %d\n",
		      pname, est->amname, level));
	    size = getsize_smbtar(est->amname, level);
	    sprintf(result, "%s %d SIZE %ld\n", est->amname, level, size);

	    amflock(1, "size");

	    lseek(1, (off_t)0, SEEK_END);
	    write(1, result, strlen(result));

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
  char result[1024];

  for(level = 0; level < DUMP_LEVELS; level++) {
      if (est->est[level].needestimate) {
	  dbprintf(("%s: getting size via gnutar for %s level %d\n",
		    pname, est->amname, level));
	  size = getsize_gnutar(est->amname, level,
				est->exclude, est->est[level].dumpsince);
	  sprintf(result, "%s %d SIZE %ld\n", est->amname, level, size);

	  amflock(1, "size");

	  lseek(1, (off_t)0, SEEK_END);
	  write(1, result, strlen(result));

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
    {"Total bytes listed: [0-9][0-9]*", 1},		     /* Samba client */
    {"Total bytes written: [0-9][0-9]*", 1},		    /* Gnutar client */

    { NULL, 0 }
};


char line[MAXLINE];


long getsize_dump(disk, level)
char *disk;
int level;
{
    int pipefd[2], nullfd, dumppid;
    long size;
    FILE *dumpout;
    char dumpkeys[10], device[1024];
    int status;
    char cmd[4096];

#ifdef OSF1_VDUMP
    strcpy(device, amname_to_dirname(disk));
#else
    strcpy(device, amname_to_devname(disk));
#endif

    sprintf(cmd, "%s/rundump%s", libexecdir, versionsuffix());

    nullfd = open("/dev/null", O_RDWR);
    pipe(pipefd);
#ifdef XFSDUMP
#ifdef DUMP
    if (!strcmp(amname_to_fstype(device), "xfs"))
#else
    if (1)
#endif
    {
#ifdef USE_RUNDUMP
        char *name = " (xfsdump)";
#else
        char *name = "";
	sprintf(cmd, "%s", XFSDUMP);
#endif
        sprintf(dumpkeys, "%d", level);
	dbprintf(("%s: running \"%s%s -F -J -l %s - %s\"\n",
		  pname, cmd, name, dumpkeys, device));
    }
    else
#endif
#ifdef VXDUMP
#ifdef DUMP
    if (!strcmp(amname_to_fstype(device), "vxfs"))
#else
    if (1)
#endif
    {
#ifdef USE_RUNDUMP
        char *name = " (vxdump)";
#else
	char *name = "";
	sprintf(cmd, "%s", VXDUMP);
#endif
	sprintf(dumpkeys, "%dsf", level);
        dbprintf(("%s: running \"%s%s %s 100000 - %s\"\n",
		  pname, cmd, name, dumpkeys, device));
    }
    else
#endif      
#ifdef DUMP
    if (1) {
#ifdef USE_RUNDUMP
        char *name = " ("
#ifdef AIX_BACKUP
	  "backup"
#else
	  DUMP
#endif
	  ")";
#else
	char *name = "";
        sprintf(cmd, "%s", DUMP);
#endif
#ifndef AIX_BACKUP
	sprintf(dumpkeys, "%d"
#ifdef HAVE_DUMP_ESTIMATE
		"E"
#endif
#ifndef OSF1_VDUMP
		"s"
#endif
		"f", level);
	dbprintf(("%s: running \"%s%s %s 100000 - %s\"\n",
		  pname, cmd, name, dumpkeys, device));
#else /* AIX_BACKUP */
	sprintf(dumpkeys, "-%df", level);
	dbprintf(("%s: running \"%s%s %s - %s\"\n",
		  pname, cmd, name, dumpkeys, device));
#endif
    }
    else
#endif
    {
        dbprintf(("%s: no dump program available", pname));
	error("%s: no dump program available", pname);
    }

    switch(dumppid = fork()) {
    case -1: return -1;
    default: break; 
    case 0:	/* child process */
#ifndef HAVE_DUMP_ESTIMATE
	if(SETPGRP == -1)
	    SETPGRP_FAILED();
#endif

	dup2(nullfd, 0);
	dup2(nullfd, 1);
	dup2(pipefd[1], 2);
	close(pipefd[0]);

#ifdef XFSDUMP
	if (!strcmp(amname_to_fstype(device), "xfs"))
	    execl(cmd, "xfsdump", "-F", "-J", "-l", dumpkeys, "-", device,
		  (char *)0);
	else
#endif
#ifdef VXDUMP
	if (!strcmp(amname_to_fstype(device), "vxfs"))
	    execl(cmd, "vxdump", dumpkeys, "100000", "-", device, (char *)0);
	else
#endif
#ifdef DUMP
#ifndef AIX_BACKUP
	    execl(cmd, "dump", dumpkeys, "100000", "-", device, (char *)0);
#else
	    execl(cmd, "backup", dumpkeys, "-", device, (char *)0);
#endif
#endif
	{
	  dbprintf(("%s: exec %s failed or no dump program available",
		    pname, cmd));
	  error("%s: exec %s failed or no dump program available",
		pname, cmd);
	  exit(1);
	}
    }
    close(pipefd[1]);
    dumpout = fdopen(pipefd[0],"r");

    size = -1;
    while(fgets(line,MAXLINE,dumpout) != NULL) {
	dbprintf(("%s",line));
	size = handle_dumpline(line);
	if(size > -1) {
	    if(fgets(line, MAXLINE, dumpout) != NULL)
		dbprintf(("%s",line));
	    break;
	}
    }

    dbprintf((".....\n"));
    if(size == -1)
	dbprintf(("(no size line match in above dump output)\n.....\n"));
    if(size == 0 && level == 0)
	dbprintf(("(PC SHARE connection problem, is this disk really empty?)\n.....\n"));

#ifndef HAVE_DUMP_ESTIMATE
#ifdef OSF1_VDUMP
    sleep(5);
#endif

    kill(-dumppid,
#ifdef XFSDUMP
    /* `xfsdump' catches and ignores `SIGTERM', so make sure it dies. */
	 strcmp(amname_to_fstype(device), "xfs")!=0 ? SIGTERM : SIGKILL
#else
	 SIGTERM
#endif
	 );
#endif /* HAVE_DUMP_ESTIMATE */
    wait(&status);

    close(nullfd);
    fclose(dumpout);

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
    char *tarkeys, sharename[256], pass[256], domain[256];

    if (findpass(disk, pass, domain) == 0)
	error("[sendsize : error in smbtar diskline, unable to find password]");
    makesharename(disk, sharename, 0);
    nullfd = open("/dev/null", O_RDWR);
    pipe(pipefd);
    if (level == 0)
	tarkeys = "archive 0;recurse;dir";
    else
	tarkeys = "archive 1;recurse;dir";

    dbprintf(("%s: running \"%s %s %s -U backup -E%s%s -c %s\"\n",
	      pname, SAMBA_CLIENT, sharename, "XXXXX",
	      domain[0] ? " -W " : "", domain,
	      tarkeys));

    switch(dumppid = fork()) {
	case -1:
	    return -1;
	    break;
	default:
	    break; 
	case 0:   /* child process */
	    dup2(nullfd, 0);
	    dup2(nullfd, 1);
	    dup2(pipefd[1], 2);
	    close(pipefd[0]);

	    execl(SAMBA_CLIENT, "smbclient", sharename, pass,
		  "-U", "backup", "-E",
		  domain[0] ? "-W" : "-c",
		  domain[0] ? domain : tarkeys,
		  domain[0] ? "-c" : (char *)0,
		  domain[0] ? tarkeys : (char *)0,
		  (char *)0);
	    exit(1);
	    break;
    }
    close(pipefd[1]);
    dumpout = fdopen(pipefd[0],"r");

    size = -1;
    while(fgets(line,MAXLINE,dumpout) != NULL) {
	dbprintf(("%s",line));
	size = handle_dumpline(line);
	if(size > -1) {
	    if(fgets(line, MAXLINE, dumpout) != NULL)
		dbprintf(("%s",line));
	    break;
	}
    }

    dbprintf((".....\n"));
    if(size == -1)
	dbprintf(("(no size line match in above smbclient output)\n.....\n"));
    if(size==0 && level ==0)
	size=-1;

    kill(-dumppid, SIGTERM);

    close(nullfd);
    fclose(dumpout);

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
    char *host;
    char *dirname;
    char cmd[256], dumptimestr[80];
    struct tm *gmtm;
    time_t prev_dumptime;
    int l;

    host = getenv("HOSTNAME");
    if (host == NULL) {
      dbprintf(("environment variable HOSTNAME must be set\n"));
      return -1;
    }

#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
    {
      int i;
      int len = sizeof(GNUTAR_LISTED_INCREMENTAL_DIR) +
	strlen(host) + strlen(disk);

      incrname = alloc(len+11);
      sprintf(incrname, "%s/%s", GNUTAR_LISTED_INCREMENTAL_DIR, host);
      i = strlen(incrname);
      strcat(incrname, disk);
      for (i = sizeof(GNUTAR_LISTED_INCREMENTAL_DIR); i<len; ++i)
	if (incrname[i] == '/' || incrname[i] == ' ')
	  incrname[i] = '_';

      sprintf(incrname + len, "_%d.new", level);
      unlink(incrname);
      umask(0007);

      if (level == 0) {
      notincremental:
	;
      } else {
	FILE *in = NULL, *out;
	char *inputname = stralloc(incrname);
	char buf[512];
	int baselevel = level;

	while (in == NULL && --baselevel >= 0) {
	  sprintf(inputname+len, "_%d", baselevel);
	  in = fopen(inputname, "r");
	}

	if (in == NULL) {
	  free(inputname);
	  inputname = 0;
	  goto notincremental;
	}
	    
	out = fopen(incrname, "w");
	if (out == NULL) {
	  dbprintf(("error [opening %s: %s]\n", incrname, strerror(errno)));
	  return -1;
	}

	while(fgets(buf, sizeof(buf), in) != NULL)
	  if (fputs(buf, out) == EOF) {
	    dbprintf(("error [writing to %s: %s]\n", incrname,
		      strerror(errno)));
	    return -1;
	  }

	if (ferror(in)) {
	  incrname[len] = '\0';
	  dbprintf(("error [reading from %s: %s]\n", inputname,
		    strerror(errno)));
	  return -1;
	}

	if (fclose(in) == EOF) {
	  dbprintf(("error [closing %s: %s]\n", inputname, strerror(errno)));
	  return -1;
	}

	if (fclose(out) == EOF) {
	  dbprintf(("error [closing %s: %s]\n", incrname, strerror(errno)));
	  return -1;
	}

	free(inputname);
      }
    }
#endif

    gmtm = gmtime(&dumpsince);
    sprintf(dumptimestr, "%04d-%02d-%02d %2d:%02d:%02d GMT",
	    gmtm->tm_year + 1900, gmtm->tm_mon+1, gmtm->tm_mday,
	    gmtm->tm_hour, gmtm->tm_min, gmtm->tm_sec);

    dirname = amname_to_dirname(disk);

    sprintf(cmd, "%s/runtar%s", libexecdir, versionsuffix());

    if (*efile == 0) /* do nothing */;
    else if (strncmp(efile, "--exclude-list", strlen("--exclude-list"))==0)
      strncpy(&efile[strlen("--exclude-")], "from", strlen("from"));
    else if (strncmp(efile, "--exclude-file", strlen("--exclude-file"))==0)
      memmove(&efile[strlen("--exclude")],
	      &efile[strlen("--exclude-file")],
	      strlen(&efile[strlen("--exclude-file")])+1);
    else assert(0); /* should never happen */

    {
      char *spec =
	"%s: running \"%s --create --directory %s "
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
	"--listed-incremental %s "
#else
	"--incremental --newer %s "
#endif
	"--sparse --one-file-system "
#ifdef ENABLE_GNUTAR_ATIME_PRESERVE
	"--atime-preserve "
#endif
	"--ignore-failed-read --totals --file /dev/null %s%s.\"\n";

      char *name_or_time =
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
	incrname
#else
	dumptimestr
#endif
	;
      
      dbprintf((spec, pname, cmd, dirname, name_or_time,
		efile[0] ? efile : "", efile[0] ? " " : ""));
    }

    nullfd = open("/dev/null", O_RDWR);
    pipe(pipefd);

    switch(dumppid = fork()) {
    case -1:
      return -1;
      break;

    default:
      break;
    case 0:
      dup2(nullfd, 0);
      dup2(nullfd, 1);
      dup2(pipefd[1], 2);
      close(pipefd[0]);

      execl(cmd,
#ifdef GNUTAR
	    GNUTAR,
#else
	    "tar",
#endif
	    "--create", "--directory", dirname,
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
	    "--listed-incremental", incrname,
#else
	    "--incremental", "--newer", dumptimestr,
#endif
	    "--sparse", "--one-file-system",
#ifdef ENABLE_GNUTAR_ATIME_PRESERVE
	    "--atime-preserve",
#endif
	    "--ignore-failed-read", "--totals", "--file", "/dev/null",
	    efile[0] ? efile : ".",
	    efile[0] ? "." : (char *)0,
	    (char *)0);

      exit(1);
      break;
    }
    close(pipefd[1]);
    dumpout = fdopen(pipefd[0],"r");

    size = -1;
    while(fgets(line,MAXLINE,dumpout) != NULL) {
	dbprintf(("%s",line));
	size = handle_dumpline(line);
	if(size > -1) {
	    if(fgets(line, MAXLINE, dumpout) != NULL)
		dbprintf(("%s",line));
	    break;
	}
    }

    dbprintf((".....\n"));
    if(size == -1)
	dbprintf(("(no size line match in above gnutar output)\n.....\n"));
    if(size==0 && level ==0)
	size=-1;

    kill(-dumppid, SIGTERM);

    unlink(incrname);
    free(incrname);

    close(nullfd);
    fclose(dumpout);

    return size;
}
#endif

double first_num(str)
char *str;
/*
 * Returns the value of the first integer in a string.
 */
{
    char tmp[16], *tp;

    tp = tmp;
    while(*str && !isdigit(*str)) str++;
    while(*str && (isdigit(*str) || (*str == '.'))) *tp++ = *str++;
    *tp = '\0';

    return atof(tmp);
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
