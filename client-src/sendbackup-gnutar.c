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
 * sendbackup-gnutar.c - send backup data using GNU tar
 */

#include "amanda.h"
#include "sendbackup-common.h"
#include "amandates.h"
#include "getfsent.h"			/* for amname_to_dirname lookup */
#include "version.h"

#ifdef SAMBA_CLIENT
#include "findpass.h"
#endif

#ifdef KRB4_SECURITY
#include "sendbackup-krb4.c"
#else					/* I'd tell you what this does */
#define NAUGHTY_BITS			/* but then I'd have to kill you */
#endif


regex_t re_table[] = {
  /* tar prints the size in bytes */
  { DMP_SIZE, 
	"^Total bytes written: [0-9][0-9]*",				1},

  /* catch-all: DMP_STRANGE is returned for all other lines */
  { DMP_STRANGE, NULL, 0}
};

extern char efile[256];

char *backup_program_name = "gtar";	/* for printing purposes */
char *restore_program_name = "gtar";
char *amanda_backup_program = "GNUTAR";	/* for the header */

int cur_level;
char *cur_disk;
time_t cur_dumptime;

#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
char *incrname;
#endif

void start_backup(disk, level, datestamp, dataf, mesgf)
char *disk, *datestamp;
int level, dataf, mesgf;
{
    int dumpin, dumpout;
    char host[MAX_HOSTNAME_LENGTH], cmd[256];
    char *dirname;
    char dbprintf_buf[1024];
#ifndef GNUTAR_LISTED_INCREMENTAL_DIR
    int l;
    char dumptimestr[80];
    struct tm *gmtm;
    amandates_t *amdates;
    time_t prev_dumptime;
#endif
    int indexout;
#ifndef USE_FQDN
    char *domain;
#endif

    host[sizeof(host)-1] = '\0';
    if(gethostname(host, sizeof(host)-1) == -1)
	error("error [gethostname: %s]", strerror(errno));
#ifndef USE_FQDN
    if((domain = strchr(host, '.'))) *domain++ = '\0';
#endif

    fprintf(stderr, "%s: start [%s:%s level %d datestamp %s]\n",
	    pname, host, disk, level, datestamp);

    NAUGHTY_BITS;

    write_tapeheader(host, disk, level, compress, datestamp, dataf);

    if(compress) {
#if defined(COMPRESS_BEST_OPT) && defined(COMPRESS_FAST_OPT)
	char *compopt;
	compopt = (compress == COMPR_BEST?
		   COMPRESS_BEST_OPT : COMPRESS_FAST_OPT);
#else
	const char compopt[] = "";
#endif
	comppid = pipespawn(COMPRESS_PATH, &indexout, dataf, mesgf,
			    COMPRESS_PATH, compopt, (char *)0);
	dbprintf(("sendbackup-gnutar: pid %d: %s %s\n",
		comppid, COMPRESS_PATH, compopt));
    } else {
	indexout = dataf;
	comppid = -1;
    }

    if(createindex) {
	char cmd2[256];
	char level_str[12];

	sprintf(cmd2, "createindex-gnutar%s", versionsuffix());
	sprintf(cmd, "%s/%s", libexecdir, cmd2);
	sprintf(level_str, "%d", level);

	indexpid = pipespawn(cmd, &dumpout, indexout, mesgf,
			     cmd2,
			     host, disk, level_str, datestamp,
			     (char *)0);
	dbprintf(("sendbackup-gnutar: pid %d: %s %s %s %s %s\n",
		indexpid, cmd, host, disk, level_str, datestamp));
    } else {
	dumpout = indexout;
	indexpid = -1;
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

	incrname = malloc(len+11);
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
		dbprintf(("%s: doing level %d dump as listed-incremental: %s\n",
			  pname, level, incrname));
	} else {
	    FILE *in, *out;
	    char *inputname = strdup(incrname);
	    char buf[512];
	    out = fopen(incrname, "w");
	    if (out == NULL)
		error("error [opening %s: %s]", incrname, strerror(errno));

	    sprintf(inputname+len, "_%d", level-1);

	    in = fopen(inputname, "r");
	    if (in == NULL)
		error("error [opening %s: %s]", inputname, strerror(errno));
	
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
#else

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
#endif

    dirname = amname_to_dirname(disk);

    cur_dumptime = time(0);
    cur_level = level;
    cur_disk = stralloc(disk);

#ifdef SAMBA_CLIENT
    /* Use sambatar if the disk to back up is a PC disk */
   if (disk[0] == '/' && disk[1]=='/') {
	char sharename[256], pass[256], *taropt;

	if (findpass(disk, pass) == 0)
	    error("[invalid samba host or password not found?]");
	makesharename(disk, sharename, 0);
	if (level==0)
	    taropt = "-Tca";
	else
	    taropt = "-Tcga";
	dbprintf(("backup from %s, pass %s\n", sharename, "XXXXX"));
	dumppid = pipespawn(SAMBA_CLIENT, &dumpin, dumpout, mesgf,
			    "gtar",
			    sharename, pass, "-U", "backup",
			    "-d0",
			    taropt, "-",
			    (char *) 0);
    } else {
#endif
    sprintf(cmd, "%s/runtar%s", libexecdir, versionsuffix());

    if (*efile) {
	char sprintf_buf[512];

	dumppid = pipespawn(cmd, &dumpin, dumpout, mesgf,
			"gtar", "--create", efile,
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
			"--file", "-", ".",
			(char *) 0);

	strcpy(sprintf_buf,
	       "sendbackup-gnutar: pid %d: %s --create %s --directory %s ");
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
	strcat(sprintf_buf, "--listed-incremental %s ");
#else
	strcat(sprintf_buf, "--incremental --newer %s ");
#endif
	strcat(sprintf_buf, "--sparse --one-file-system ");
#ifdef ENABLE_GNUTAR_ATIME_PRESERVE
	strcat(sprintf_buf, "--atime-preserve ");
#endif
	strcat(sprintf_buf, "--ignore-failed-read --totals --file - .\n");

	sprintf(dbprintf_buf, sprintf_buf, 
			dumppid, cmd, efile, dirname,
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
			incrname
#else
			dumptimestr
#endif
		);
	dbprintf((dbprintf_buf));
    } else {
	char sprintf_buf[512];

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
			"--file", "-", ".",
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
	strcat(sprintf_buf, "--ignore-failed-read --totals --file - .\n");

	sprintf(dbprintf_buf, sprintf_buf,
			dumppid, cmd, dirname,
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
			incrname
#else
			dumptimestr
#endif
		);
	dbprintf((dbprintf_buf));
    }

#ifdef SAMBA_CLIENT
    }
#endif

    /* close the write ends of the pipes */

    close(dumpin);
    close(dumpout);
    close(indexout);
    close(dataf);
    close(mesgf);
}

void end_backup(goterror)
int goterror;
{
    if(!no_record && !goterror) {
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
#ifdef SAMBA_CLIENT
      if (incrname != NULL) {
#endif
        char *nodotnew = strdup(incrname);
        nodotnew[strlen(nodotnew)-4] = '\0';
	unlink(nodotnew);
        if (rename(incrname, nodotnew))
            error("error [renaming %s to %s: %s]", 
		  incrname, nodotnew, strerror(errno));
	free(nodotnew); 
#ifdef SAMBA_CLIENT
      }
#endif
#endif

	amandates_updateone(cur_disk, cur_level, cur_dumptime);
    }

    finish_amandates();
    free_amandates();
}
