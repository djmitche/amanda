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
 * $Id: sendbackup-gnutar.c,v 1.79 2002/02/13 15:21:17 martinea Exp $
 *
 * send backup data using GNU tar
 */

#include "amanda.h"
#include "sendbackup.h"
#include "amandates.h"
#include "util.h"
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
  { DMP_NORMAL, "^[ \t]*[0-9][0-9]* \\([ \t]*[0-9][0-9]*\\.[0-9][0-9]* kb/s\\)", 1},
  { DMP_NORMAL, "^[ \t]*directory ", 1},
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

#if SAMBA_VERSION >= 2
  /* Backup attempt of nonexisting directory */
  { DMP_ERROR, "ERRDOS - ERRbadpath (Directory invalid.)", 1},
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

    if(options->compress) {
	char *compopt = skip_argument;

#if defined(COMPRESS_BEST_OPT) && defined(COMPRESS_FAST_OPT)
	if(options->compress == COMPR_BEST) {
	    compopt = COMPRESS_BEST_OPT;
	} else {
	    compopt = COMPRESS_FAST_OPT;
	}
#endif
	comppid = pipespawn(COMPRESS_PATH, STDIN_PIPE,
			    &dumpout, &dataf, &mesgf,
			    COMPRESS_PATH, compopt, NULL);
	dbprintf(("%s-gnutar: pid %ld: %s",
		  get_pname(), (long)comppid, COMPRESS_PATH));
	if(compopt != skip_argument) {
	    dbprintf((" %s", compopt));
	}
	dbprintf(("\n"));
    } else {
	dumpout = dataf;
	comppid = -1;
    }

#ifdef GNUTAR_LISTED_INCREMENTAL_DIR					/* { */
#ifdef SAMBA_CLIENT							/* { */
    if (disk[0] == '/' && disk[1]=='/')
	amfree(incrname);
    else
#endif									/* } */
    {
	char *basename = NULL;
	char number[NUM_STR_SIZE];
	char *s;
	int ch;
	char *inputname = NULL;
	FILE *in = NULL;
	FILE *out;
	int baselevel;
	char *line = NULL;

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
	 * Open the listed incremental file for the previous level.  Search
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

		dbprintf(("%s-gnutar: error opening %s: %s\n",
			  get_pname(),
			  inputname,
			  strerror(save_errno)));
		if (baselevel < 0) {
		    error("error [opening %s: %s]", inputname, strerror(errno));
		}
	    }
	}

	/*
	 * Copy the previous listed incremental file to the new one.
	 */
	if ((out = fopen(incrname, "w")) == NULL) {
	    error("error [opening %s: %s]", incrname, strerror(errno));
	}

	for (; (line = agets(in)) != NULL; free(line)) {
	    if(fputs(line, out) == EOF || putc('\n', out) == EOF) {
		error("error [writing to %s: %s]", incrname, strerror(errno));
	    }
	}
	amfree(line);

	if (ferror(in)) {
	    error("error [reading from %s: %s]", inputname, strerror(errno));
	}
	if (fclose(in) == EOF) {
	    error("error [closing %s: %s]", inputname, strerror(errno));
	}
	in = NULL;
	if (fclose(out) == EOF) {
	    error("error [closing %s: %s]", incrname, strerror(errno));
	}
	out = NULL;

	dbprintf(("%s-gnutar: doing level %d dump as listed-incremental",
		  get_pname(), level));
	if(baselevel >= 0) {
	    dbprintf((" from %s", inputname));
	}
	dbprintf((" to %s\n", incrname));
	amfree(inputname);
	amfree(basename);
    }
#endif									/* } */

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

    dbprintf(("%s-gnutar: doing level %d dump from date: %s\n",
	      get_pname(), level, dumptimestr));

    dirname = amname_to_dirname(disk);

    cur_dumptime = time(0);
    cur_level = level;
    cur_disk = stralloc(disk);
    indexcmd = vstralloc(
#ifdef GNUTAR
			 GNUTAR,
#else
			 "tar",
#endif
			 " -tf", " -",
			 " 2>/dev/null",
			 " | sed", " -e",
			 " \'s/^\\.//\'",
			 NULL);

#ifdef SAMBA_CLIENT							/* { */
    /* Use sambatar if the disk to back up is a PC disk */
    if (disk[0] == '/' && disk[1]=='/') {
	char *sharename = NULL, *user_and_password = NULL, *domain = NULL;
	char *share = NULL, *subdir = NULL;
	char *pwtext;
	char *taropt;
	int passwdf;
	int lpass;
	int pwtext_len;
	char *pw_fd_env;

	parsesharename(disk, &share, &subdir);
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
	    error("error [invalid samba host or password not found?]");
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
	if ((sharename = makesharename(share, 0)) == 0) {
	    memset(user_and_password, '\0', lpass);
	    amfree(user_and_password);
	    if(domain) {
		memset(domain, '\0', strlen(domain));
		amfree(domain);
	    }
	    error("error [can't make share name of %s]", share);
	}

	taropt = stralloc("-T");
	if(options->exclude_file && options->exclude_file->nb_element == 1) {
	    strappend(taropt, "X");
	}
#if SAMBA_VERSION >= 2
	strappend(taropt, "q");
#endif
	strappend(taropt, "c");
	if (level != 0) {
	    strappend(taropt, "g");
	} else if (!options->no_record) {
	    strappend(taropt, "a");
	}

	dbprintf(("%s-gnutar: backup of %s/%s\n", get_pname(), sharename, subdir));

	program->backup_name = program->restore_name = SAMBA_CLIENT;
	cmd = stralloc(program->backup_name);
	info_tapeheader();

	start_index(options->createindex, dumpout, mesgf, indexf, indexcmd);

	if (pwtext_len > 0) {
	    pw_fd_env = "PASSWD_FD";
	} else {
	    pw_fd_env = "dummy_PASSWD_FD";
	}
	dumppid = pipespawn(cmd, STDIN_PIPE|PASSWD_PIPE,
			    &dumpin, &dumpout, &mesgf,
			    pw_fd_env, &passwdf,
			    "smbclient",
			    sharename,
			    *user_and_password ? "-U" : skip_argument,
			    *user_and_password ? user_and_password : skip_argument,
			    "-E",
			    domain ? "-W" : skip_argument,
			    domain ? domain : skip_argument,
#if SAMBA_VERSION >= 2
			    subdir ? "-D" : skip_argument,
			    subdir ? subdir : skip_argument,
#endif
			    "-d0",
			    taropt,
			    "-",
			    options->exclude_file && options->exclude_file->nb_element == 1 ? options->exclude_file->first->name : skip_argument,
			    NULL);
	if(domain) {
	    memset(domain, '\0', strlen(domain));
	    amfree(domain);
	}
	if(pwtext_len > 0 && fullwrite(passwdf, pwtext, pwtext_len) < 0) {
	    int save_errno = errno;

	    aclose(passwdf);
	    memset(user_and_password, '\0', lpass);
	    amfree(user_and_password);
	    error("error [password write failed: %s]", strerror(save_errno));
	}
	memset(user_and_password, '\0', lpass);
	amfree(user_and_password);
	aclose(passwdf);
	amfree(sharename);
	amfree(share);
	amfree(subdir);
	amfree(taropt);
	tarpid = dumppid;
    } else
#endif									/* } */
    {

	int nb_exclude = 0;
	char **my_argv;
	int i = 0;
	sle_t *excl;

	if(options->exclude_file) nb_exclude+=options->exclude_file->nb_element;
	if(options->exclude_list) nb_exclude+=options->exclude_list->nb_element;

	my_argv = malloc(sizeof(char *) * (17 + (nb_exclude *2)));

	cmd = vstralloc(libexecdir, "/", "runtar", versionsuffix(), NULL);
	info_tapeheader();

	start_index(options->createindex, dumpout, mesgf, indexf, indexcmd);

	my_argv[i++] = "gtar";
	my_argv[i++] = "--create";
	my_argv[i++] = "--file";
	my_argv[i++] = "-";
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
	if(options->exclude_file) {
	    for(excl = options->exclude_file->first; excl != NULL;
		excl = excl->next) {
		my_argv[i++] = "--exclude";
		my_argv[i++] = excl->name;
	    }
	}
	if(options->exclude_list) {
	    for(excl = options->exclude_list->first; excl != NULL;
		excl = excl->next) {
		my_argv[i++] = "--exclude";
		my_argv[i++] = excl->name;
	    }
	}
	my_argv[i++] = ".";
	my_argv[i++] = NULL;
	if(i >= 17 + 2*nb_exclude) {
	    error("i = %d",i);
	}
	dumppid = pipespawnv(cmd, STDIN_PIPE,
			     &dumpin, &dumpout, &mesgf, my_argv);
	tarpid = dumppid;
	amfree(my_argv);
    }
    dbprintf(("%s-gnutar: %s: pid %ld\n", get_pname(), cmd, (long)dumppid));

    amfree(dirname);
    amfree(cmd);
    amfree(indexcmd);

    /* close the write ends of the pipes */

    aclose(dumpin);
    aclose(dumpout);
    aclose(dataf);
    aclose(mesgf);
    if (options->createindex)
	aclose(indexf);
}

static void end_backup(goterror)
int goterror;
{
    if(!options->no_record && !goterror) {
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
