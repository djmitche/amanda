/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1999 University of Maryland at College Park
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
 * $Id: sendbackup-gnutar.c,v 1.72 2000/10/11 02:23:33 martinea Exp $
 *
 * send backup data using GNU tar
 */

#include "amanda.h"
#include "sendbackup.h"
#include "amandates.h"
#include "getfsent.h"			/* for amname_to_dirname lookup */
#include "version.h"

#ifdef SAMBA_CLIENT
#include "findpass.h"
#endif

static regex_t re_table[] = {
  /* tar prints the size in bytes */
  { DMP_SIZE, 
	"^Total bytes written: [0-9][0-9]*",				1},

  { DMP_NORMAL, "^Elapsed time:", 1 },
  { DMP_NORMAL, "^Throughput", 1 },

  /* GNU tar 1.13.17 will print this warning when (not) backing up a
     Unix named socket.  */
  { DMP_NORMAL, ": socket ignored$", 1 },

  /* GNUTAR produces a few error messages when files are modified or
     removed while it is running.  They may cause data to be lost, but
     then they may not.  We shouldn't consider them NORMAL until
     further investigation.  */
#ifdef IGNORE_TAR_ERRORS
  { DMP_NORMAL, ": File .* shrunk by [0-9][0-9]* bytes, padding with zeros", 1 },
  { DMP_NORMAL, ": Cannot add file .*: No such file or directory$", 1},
  { DMP_NORMAL, ": Error exit delayed from previous errors", 1},
#endif
  
  /* samba may produce these output messages */
  { DMP_NORMAL, "^[Aa]dded interface", 1},
  { DMP_NORMAL, "^session request to ", 1},
  { DMP_NORMAL, "^tar: dumped [0-9][0-9]* (tar )?files", 1},

#if SAMBA_VERSION < 2
  { DMP_NORMAL, "^doing parameter", 1},
  { DMP_NORMAL, "^pm_process\\(\\)", 1},
  { DMP_NORMAL, "^adding IPC", 1},
  { DMP_NORMAL, "^Opening", 1},
  { DMP_NORMAL, "^Connect", 1},
  { DMP_NORMAL, "^Domain=", 1},
  { DMP_NORMAL, "^max", 1},
  { DMP_NORMAL, "^security=", 1},
  { DMP_NORMAL, "^capabilities", 1},
  { DMP_NORMAL, "^Sec mode ", 1},
  { DMP_NORMAL, "^Got ", 1},
  { DMP_NORMAL, "^Chose protocol ", 1},
  { DMP_NORMAL, "^Server ", 1},
  { DMP_NORMAL, "^Timezone ", 1},
  { DMP_NORMAL, "^received", 1},
  { DMP_NORMAL, "^FINDFIRST", 1},
  { DMP_NORMAL, "^FINDNEXT", 1},
  { DMP_NORMAL, "^dos_clean_name", 1},
  { DMP_NORMAL, "^file", 1},
  { DMP_NORMAL, "^getting file", 1},
  { DMP_NORMAL, "^Rejected chained", 1},
  { DMP_NORMAL, "^nread=", 1},
  { DMP_NORMAL, "^\\([0-9][0-9]* kb/s\\)", 1},
  { DMP_NORMAL, "^\\([0-9][0-9]*\\.[0-9][0-9]* kb/s\\)", 1},
  { DMP_NORMAL, "^[ ]*[0-9][0-9]* \\([ ]*[0-9][0-9]*\\.[0-9][0-9]* kb/s\\)", 1},
  { DMP_NORMAL, "^[ ]*directory ", 1},
  { DMP_NORMAL, "^load_client_codepage", 1},
#endif

#ifdef IGNORE_SMBCLIENT_ERRORS
  /* This will cause amanda to ignore real errors, but that may be
   * unavoidable when you're backing up system disks.  It seems to be
   * a safe thing to do if you know what you're doing.  */
  { DMP_NORMAL, "^ERRDOS - ERRbadshare opening remote file", 1},
  { DMP_NORMAL, "^ERRDOS - ERRbadfile opening remote file", 1},
  { DMP_NORMAL, "^ERRDOS - ERRnoaccess opening remote file", 1},
  { DMP_NORMAL, "^ERRSRV - ERRaccess setting attributes on file", 1},
  { DMP_NORMAL, "^ERRDOS - ERRnoaccess setting attributes on file", 1},
#endif

  /* catch-all: DMP_STRANGE is returned for all other lines */
  { DMP_STRANGE, NULL, 0}
};

extern char *efile;

int cur_level;
char *cur_disk;
time_t cur_dumptime;

#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
static char *incrname = NULL;
#endif

static void start_backup P((char *, char *, int, char *, int, int, int));
static void end_backup P((int));

static void start_backup(host, disk, level, dumpdate, dataf, mesgf, indexf)
char *host;
char *disk;
int level, dataf, mesgf, indexf;
char *dumpdate;
{
    int dumpin, dumpout;
    char *cmd = NULL;
    char *indexcmd = NULL;
    char *dirname = NULL;
    int l;
    char dumptimestr[80];
    struct tm *gmtm;
    amandates_t *amdates;
    time_t prev_dumptime;

    fprintf(stderr, "%s: start [%s:%s level %d]\n",
	    get_pname(), host, disk, level);

    if(compress) {
#if defined(COMPRESS_BEST_OPT) && defined(COMPRESS_FAST_OPT)
	char *compopt;
	compopt = (compress == COMPR_BEST?
		   COMPRESS_BEST_OPT : COMPRESS_FAST_OPT);
#else
	const char compopt[] = "";
#endif
	comppid = pipespawn(COMPRESS_PATH, &dumpout, dataf, mesgf,
			    COMPRESS_PATH, compopt, (char *)0);
	dbprintf(("sendbackup-gnutar: pid %d: %s %s\n",
		comppid, COMPRESS_PATH, compopt));
    } else {
	dumpout = dataf;
	comppid = -1;
    }

#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
#ifdef SAMBA_CLIENT
    if (disk[0] == '/' && disk[1]=='/')
      amfree(incrname);
    else
#endif
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

	snprintf(number, sizeof(number), "%d", level);
	incrname = vstralloc(basename, "_", number, ".new", NULL);
	unlink(incrname);

	if (level == 0) {
	  FILE *out;

notincremental:

	  out = fopen(incrname, "w");
	  if (out == NULL)
	    error("error [opening %s: %s]", incrname, strerror(errno));

	  if (fclose(out) == EOF)
	    error("error [closing %s: %s]", incrname, strerror(errno));
	  out = NULL;

	  dbprintf(("%s: doing level %d dump as listed-incremental: %s\n",
		    get_pname(), level, incrname));
	} else {
	    FILE *in = NULL, *out;
	    char *inputname = NULL;
	    char buf[BUFSIZ];
	    int baselevel = level;

	    while (in == NULL && --baselevel >= 0) {
		snprintf(number, sizeof(number), "%d", baselevel);
		inputname = newvstralloc(inputname,
					 basename, "_", number,
					 NULL);
		in = fopen(inputname, "r");
	    }

	    if (in == NULL) {
	      amfree(inputname);
	      goto notincremental;
	    }

	    out = fopen(incrname, "w");
	    if (out == NULL)
		error("error [opening %s: %s]", incrname, strerror(errno));

	    while(fgets(buf, sizeof(buf), in) != NULL)
		if (fputs(buf, out) == EOF)
		    error("error [writing to %s: %s]", incrname,
			  strerror(errno));

	    if (ferror(in)) {
		error("error [reading from %s: %s]", inputname,
		      strerror(errno));
	    }

	    if (fclose(in) == EOF)
		error("error [closing %s: %s]", inputname, strerror(errno));
	    in = NULL;

	    if (fclose(out) == EOF)
		error("error [closing %s: %s]", incrname, strerror(errno));
	    out = NULL;

	    dbprintf(("%s: doing level %d dump as listed-incremental from %s, updating to %s\n",
		      get_pname(), level, inputname, incrname));

	    amfree(inputname);
	}
	amfree(basename);
    }
#endif

    /* find previous dump time */

    if(!start_amandates(0))
	error("error [opening %s: %s]", AMANDATES_FILE, strerror(errno));

    amdates = amandates_lookup(disk);

    prev_dumptime = EPOCH;
    for(l = 0; l < level; l++) {
	if(amdates->dates[l] > prev_dumptime)
	    prev_dumptime = amdates->dates[l];
    }

    finish_amandates();
    free_amandates();

    gmtm = gmtime(&prev_dumptime);
    snprintf(dumptimestr, sizeof(dumptimestr),
		"%04d-%02d-%02d %2d:%02d:%02d GMT",
		gmtm->tm_year + 1900, gmtm->tm_mon+1, gmtm->tm_mday,
		gmtm->tm_hour, gmtm->tm_min, gmtm->tm_sec);

    dbprintf(("%s: doing level %d dump from date: %s\n",
	      get_pname(), level, dumptimestr));

    dirname = amname_to_dirname(disk);

    cur_dumptime = time(0);
    cur_level = level;
    cur_disk = stralloc(disk);

#ifdef SAMBA_CLIENT
    /* Use sambatar if the disk to back up is a PC disk */
   if (disk[0] == '/' && disk[1]=='/') {
	char *sharename = NULL, *pass, *domain = NULL;
	char *tarcmd, *taropt;

	if ((pass = findpass(disk, &domain)) == NULL) {
	    error("[invalid samba host or password not found?]");
	}
	if ((sharename = makesharename(disk, 0)) == 0) {
	    memset(pass, '\0', strlen(pass));
	    amfree(pass);
	    if(domain) {
		memset(domain, '\0', strlen(domain));
		amfree(domain);
	    }
	    error("[can't make share name of %s]", disk);
	}
#if SAMBA_VERSION >= 2
	if (level==0) {
	    if (no_record)
		taropt = "-Tqc";
	    else
		taropt = "-Tqca";
	} else
	    taropt = "-Tqcg";
#else
	if (level==0) {
	    if (no_record)
		taropt = "-Tc";
	    else
		taropt = "-Tca";
	} else
	    taropt = "-Tcg";
#endif
	dbprintf(("backup from %s, pass %s\n", 
		  sharename, "(see amandapass)"));

	program->backup_name = program->restore_name = SAMBA_CLIENT;

#ifdef GNUTAR
	tarcmd = GNUTAR;
#else
	tarcmd = "tar";
#endif
	indexcmd = vstralloc(tarcmd,
			     " -tf", " -",
			     " 2>/dev/null",
			     " | sed", " -e", 
			     " \'s/^\\.//\'",
			     NULL);
	info_tapeheader();

	start_index(createindex, dumpout, mesgf, indexf, indexcmd);

	dumppid = pipespawn(program->backup_name, &dumpin, dumpout, mesgf,
			    "smbclient",
			    sharename, "-U", pass, "-E",
			    domain ? "-W" : "-d0",
			    domain ? domain : taropt,
			    domain ? "-d0" : "-",
			    domain ? taropt : (char *)0,
			    domain ? "-" : (char *)0,
			    (char *) 0);
	tarpid = dumppid;
	memset(pass, '\0', strlen(pass));
	amfree(pass);
	if(domain) {
	    memset(domain, '\0', strlen(domain));
	    amfree(domain);
	}
	amfree(sharename);
    } else {
#endif
      cmd = vstralloc(libexecdir, "/", "runtar", versionsuffix(), NULL);

      {
	char *format_buf;
	char *a00, *a01, *a02, *a03, *a04, *a05;
	char *a06, *a07, *a08, *a09, *a10, *a11;
	char *incr;
	char *tarcmd;

#ifdef GNUTAR
	tarcmd = GNUTAR;
#else
	tarcmd = "tar";
#endif
	indexcmd = vstralloc(tarcmd,
			     " -tf", " -",
			     " 2>/dev/null",
			     " | sed", " -e",
			     " \'s/^\\.//\'",
			     NULL);
	info_tapeheader();

	start_index(createindex, dumpout, mesgf, indexf, indexcmd);

	dumppid = pipespawn(cmd, &dumpin, dumpout, mesgf,
			"gtar",
			"--create",
			"--directory", dirname,
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
			"--listed-incremental", incrname,
#else
			"--incremental", "--newer", dumptimestr,
#endif
			"--sparse","--one-file-system",
#ifdef ENABLE_GNUTAR_ATIME_PRESERVE
			/* --atime-preserve causes gnutar to call
			 * utime() after reading files in order to
			 * adjust their atime.  However, utime()
			 * updates the file's ctime, so incremental
			 * dumps will think the file has changed. */
			"--atime-preserve",
#endif
			"--ignore-failed-read", "--totals",
			"--file", "-",
			efile ? efile : ".",
			efile ? "." : (char *)0,
			(char *) 0);
	tarpid = dumppid;
	
	a00 = "sendbackup-gnutar: pid %d: %s";
	a01 = " --create";
	a02 = " --directory %s";
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
	a03 = " --listed-incremental %s";
#else
	a03 = " --incremental --newer %s";
#endif
	a04 = " --sparse";
	a05 = " --one-file-system";
#ifdef ENABLE_GNUTAR_ATIME_PRESERVE
	a06 = " --atime-preserve";
#else
	a06 = "";
#endif
	a07 = " --ignore-failed-read";
	a08 = " --totals";
	a09 = " --file -";
	a10 = " %s";
	a11 = efile ? " ." : "";

	format_buf = vstralloc(a00, a01, a02, a03,
			       a04, a05, a06, a07,
			       a08, a09, a10, a11,
			       "\n", NULL);

#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
	incr = incrname;
#else
	incr = dumptimestr;
#endif
	dbprintf((format_buf, dumppid, cmd, dirname, incr,
		  efile ? efile : "."));
	amfree(format_buf);
      }
#ifdef SAMBA_CLIENT
    }
#endif

    amfree(dirname);
    amfree(cmd);
    amfree(indexcmd);

    /* close the write ends of the pipes */

    aclose(dumpin);
    aclose(dumpout);
    aclose(dataf);
    aclose(mesgf);
    if (createindex)
	aclose(indexf);
}

static void end_backup(goterror)
int goterror;
{
    if(!no_record && !goterror) {
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
      if (incrname != NULL && strlen(incrname) > 4) {
        char *nodotnew;
	
	nodotnew = stralloc(incrname);
        nodotnew[strlen(nodotnew)-4] = '\0';
        if (rename(incrname, nodotnew))
            error("error [renaming %s to %s: %s]", 
		  incrname, nodotnew, strerror(errno));
	amfree(nodotnew);
	amfree(incrname);
      }
#endif

        if(!start_amandates(1))
	    error("error [opening %s: %s]", AMANDATES_FILE, strerror(errno));
	amandates_updateone(cur_disk, cur_level, cur_dumptime);
	finish_amandates();
	free_amandates();
    }
}

backup_program_t gnutar_program = {
  "GNUTAR",
#ifdef GNUTAR
  GNUTAR, GNUTAR,
#else
  "gtar", "gtar",
#endif
  re_table, start_backup, end_backup
};
