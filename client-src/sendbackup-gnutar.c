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
 * $Id: sendbackup-gnutar.c,v 1.30 1997/12/13 04:49:48 amcore Exp $
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

#ifdef KRB4_SECURITY
#include "sendbackup-krb4.h"
#else					/* I'd tell you what this does */
#define NAUGHTY_BITS			/* but then I'd have to kill you */
#endif


static regex_t re_table[] = {
  /* tar prints the size in bytes */
  { DMP_SIZE, 
	"^Total bytes written: [0-9][0-9]*",				1},

 /* samba 1.9.17 has introduced these output messages */
  { DMP_NORMAL, "^doing parameter", 1},
  { DMP_NORMAL, "^pm_process\\(\\)", 1},
  { DMP_NORMAL, "^adding IPC", 1},
  { DMP_NORMAL, "^Added interface", 1},
  { DMP_NORMAL, "^Opening", 1},
  { DMP_NORMAL, "^Connect", 1},
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
  { DMP_NORMAL, "^dos_clean_name", 1},
  { DMP_NORMAL, "^file", 1},
  { DMP_NORMAL, "^getting file", 1},
  { DMP_NORMAL, "^Rejected chained", 1},
  { DMP_NORMAL, "^nread=", 1},
  { DMP_NORMAL, "^\\([0-9][0-9]* kb/s\\)", 1},
  { DMP_NORMAL, "^\\([0-9][0-9]*\\.[0-9][0-9]* kb/s\\)", 1},
  { DMP_NORMAL, "^tar: dumped [0-9][0-9]* tar files", 1},

  /* catch-all: DMP_STRANGE is returned for all other lines */
  { DMP_STRANGE, NULL, 0}
};

extern char efile[256];

int cur_level;
char *cur_disk;
time_t cur_dumptime;

#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
char *incrname;
#endif

static void start_backup(host, disk, level, dumpdate, dataf, mesgf, indexf)
char *host;
char *disk;
int level, dataf, mesgf, indexf;
char *dumpdate;
{
    int dumpin, dumpout;
    char cmd[256];
    char *dirname;
    char dbprintf_buf[1024];
    int l;
    char dumptimestr[80];
    struct tm *gmtm;
    amandates_t *amdates;
    time_t prev_dumptime;
    char indexcmd[4096];

    fprintf(stderr, "%s: start [%s:%s level %d]\n",
	    pname, host, disk, level);

    NAUGHTY_BITS;

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
      incrname = NULL;
    else
#endif
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
	  FILE *out;
	notincremental:
	  out = fopen(incrname, "w");
	  if (out == NULL)
	    error("error [opening %s: %s]", incrname, strerror(errno));

	  if (fclose(out) == EOF)
	    error("error [closing %s: %s]", incrname, strerror(errno));

	  dbprintf(("%s: doing level %d dump as listed-incremental: %s\n",
		    pname, level, incrname));
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
	    if (out == NULL)
		error("error [opening %s: %s]", incrname, strerror(errno));

	    while(fgets(buf, sizeof(buf), in) != NULL)
		if (fputs(buf, out) == EOF)
		    error("error [writing to %s: %s]", incrname,
			  strerror(errno));

	    if (ferror(in)) {
		incrname[len] = '\0';
		error("error [reading from %s: %s]", inputname,
		      strerror(errno));
	    }

	    if (fclose(in) == EOF)
		error("error [closing %s: %s]", inputname, strerror(errno));

	    if (fclose(out) == EOF)
		error("error [closing %s: %s]", incrname, strerror(errno));

	    dbprintf(("%s: doing level %d dump as listed-incremental from %s, updating to %s\n",
		      pname, level, inputname, incrname));

	    free(inputname);
	}
    }
#endif

    /* find previous dump time */

    if(!start_amandates(1))
	error("error [opening %s: %s]", AMANDATES_FILE, strerror(errno));

    amdates = amandates_lookup(disk);

    prev_dumptime = EPOCH;
    for(l = 0; l < level; l++) {
	if(amdates->dates[l] > prev_dumptime)
	    prev_dumptime = amdates->dates[l];
    }

    gmtm = gmtime(&prev_dumptime);
    sprintf(dumptimestr, "%04d-%02d-%02d %2d:%02d:%02d GMT",
	    gmtm->tm_year + 1900, gmtm->tm_mon+1, gmtm->tm_mday,
	    gmtm->tm_hour, gmtm->tm_min, gmtm->tm_sec);

    dbprintf(("%s: doing level %d dump from date: %s\n",
	      pname, level, dumptimestr));

    dirname = amname_to_dirname(disk);

    cur_dumptime = time(0);
    cur_level = level;
    cur_disk = stralloc(disk);

#ifdef SAMBA_CLIENT
    /* Use sambatar if the disk to back up is a PC disk */
   if (disk[0] == '/' && disk[1]=='/') {
	char sharename[256], pass[256], domain[256], *taropt;

	if (findpass(disk, pass, domain) == 0)
	    error("[invalid samba host or password not found?]");
	makesharename(disk, sharename, 0);
	if (level==0)
	    taropt = "-Tca";
	else
	    taropt = "-Tcga";
	dbprintf(("backup from %s, pass %s\n", sharename, "XXXXX"));

	program->backup_name = program->restore_name = SAMBA_CLIENT;
	
        sprintf(indexcmd,
                "%s -tf - 2>/dev/null | cut -c2-",
#ifdef GNUTAR
		GNUTAR
#else
		"tar"
#endif
                );

	write_tapeheader();

	start_index(createindex, dumpout, mesgf, indexf, indexcmd);

	dumppid = pipespawn(program->backup_name, &dumpin, dumpout, mesgf,
			    "smbclient",
			    sharename, pass, "-U", "backup", "-E",
			    domain[0] ? "-W" : "-d0",
			    domain[0] ? domain : taropt,
			    domain[0] ? "-d0" : "-",
			    domain[0] ? taropt : (char *)0,
			    domain[0] ? "-" : (char *)0,
			    (char *) 0);
    } else {
#endif
      sprintf(cmd, "%s/runtar%s", libexecdir, versionsuffix());

      {
	char sprintf_buf[512];

        sprintf(indexcmd,
                "%s -tf - 2>/dev/null | cut -c2-",
#ifdef GNUTAR
		GNUTAR
#else
		"tar"
#endif
                );

	write_tapeheader();

	start_index(createindex, dumpout, mesgf, indexf, indexcmd);

	dumppid = pipespawn(cmd, &dumpin, dumpout, mesgf,
			"gtar", "--create",
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
			*efile ? efile : ".",
			*efile ? "." : (char *)0,
			(char *) 0);

	strcpy(sprintf_buf,
	       "sendbackup-gnutar: pid %d: %s --create --directory %s ");
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
	strcat(sprintf_buf, "--listed-incremental %s ");
#else
	strcat(sprintf_buf, "--incremental --newer %s ");
#endif
	strcat(sprintf_buf, "--sparse --one-file-system ");
#ifdef ENABLE_GNUTAR_ATIME_PRESERVE
	strcat(sprintf_buf, "--atime-preserve ");
#endif
	strcat(sprintf_buf, "--ignore-failed-read --totals --file - %s.\n");

	sprintf(dbprintf_buf, sprintf_buf, 
			dumppid, cmd, efile, dirname,
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
			incrname,
#else
			dumptimestr,
#endif
		        efile
		);
	dbprintf((dbprintf_buf));
      }
#ifdef SAMBA_CLIENT
    }
#endif

    /* close the write ends of the pipes */

    close(dumpin);
    close(dumpout);
    close(dataf);
    close(mesgf);
    close(indexf);
}

static void end_backup(goterror)
int goterror;
{
    if(!no_record && !goterror) {
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
#ifdef SAMBA_CLIENT
      if (incrname != NULL) {
#endif
        char *nodotnew = stralloc(incrname);
        nodotnew[strlen(nodotnew)-4] = '\0';
	unlink(nodotnew);
        if (rename(incrname, nodotnew))
            error("error [renaming %s to %s: %s]", 
		  incrname, nodotnew, strerror(errno));
	free(nodotnew);
	free(incrname);
#ifdef SAMBA_CLIENT
      }
#endif
#endif

	amandates_updateone(cur_disk, cur_level, cur_dumptime);
    }

    finish_amandates();
    free_amandates();
}

backup_program_t backup_program = {
  "GNUTAR",
#ifdef GNUTAR
  GNUTAR, GNUTAR,
#else
  "gtar", "gtar",
#endif
  re_table, start_backup, end_backup
};
