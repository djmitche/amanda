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
 * $Id: sendbackup-dump.c,v 1.37 1997/10/01 12:57:49 amcore Exp $
 *
 * send backup data using BSD dump
 */

#include "sendbackup.h"
#include "getfsent.h"
#include "version.h"

#ifdef KRB4_SECURITY
#include "sendbackup-krb4.h"
#else					/* I'd tell you what this does */
#define NAUGHTY_BITS			/* but then I'd have to kill you */
#endif

static regex_t re_table[] = {
  /* the various encodings of dump size */
  /* this should also match BSDI pre-3.0's buggy dump program, that
     produced doubled DUMP: DUMP: messages */
  { DMP_SIZE, 
	"DUMP: [0-9][0-9]* tape blocks",				1024},

  { DMP_SIZE,
	"dump: Actual: [0-9][0-9]* tape blocks",			1024},

  { DMP_SIZE,
        "backup: There are [0-9][0-9]* tape blocks on [0-9]* tapes",    1024},

  { DMP_SIZE,
        "backup: [0-9][0-9]* tape blocks on [0-9][0-9]* tape(s)",       1024},

  { DMP_SIZE,
	"backup: [0-9][0-9]* 1k blocks on [0-9][0-9]* volume(s)",	1024},

  { DMP_SIZE,
	"DUMP: [0-9][0-9]* blocks ([0-9][0-9]*KB) on [0-9][0-9]* volume", 512},

  { DMP_SIZE,
"DUMP: [0-9][0-9]* blocks ([0-9][0-9]*\\.[0-9][0-9]*MB) on [0-9][0-9]* volume",
                                                                          512},
  { DMP_SIZE, "DUMP: [0-9][0-9]* blocks",                                 512},

  { DMP_SIZE, "DUMP: [0-9][0-9]* bytes were dumped",		            1},

  { DMP_SIZE, "vdump: Dumped  [0-9][0-9]* of [0-9][0-9]* bytes",	    1},
		/* OSF's vdump */

  { DMP_SIZE, "dump: Actual: [0-9][0-9]* blocks output to pipe",         1024},
                /* DU 4.0a dump */

  { DMP_SIZE, "dump: Dumped  [0-9][0-9]* of [0-9][0-9]* bytes",		    1},
		/* DU 4.0 vdump */

  { DMP_SIZE, "DUMP: [0-9][0-9]* KB actual output", 1024},
		/* HPUX dump */

  { DMP_SIZE, "vxdump: [0-9][0-9]* tape blocks", 512},
		/* HPUX vxdump */

  { DMP_SIZE, "xfsdump: media file size [0-9][0-9]* bytes",                 1},
		/* Irix 6.2 xfs dump */

  /* strange dump lines */
  { DMP_STRANGE, "should not happen" },
  { DMP_STRANGE, "Cannot open" },
  { DMP_STRANGE, "[Ee]rror" },
  { DMP_STRANGE, "[Ff]ail" },
  /* XXX add more ERROR entries here by scanning dump sources? */

  /* any blank or non-strange DUMP: lines are marked as normal */
  { DMP_NORMAL, "^  DUMP:" },
  { DMP_NORMAL, "^dump:" },					/* OSF/1 */
  { DMP_NORMAL, "^vdump:" },					/* OSF/1 */
  { DMP_NORMAL, "^  vxdump:" },					/* HPUX10 */
  { DMP_NORMAL, "^xfsdump:" },					/* IRIX xfs */
  { DMP_NORMAL, "^  UFSDUMP:" },                                /* Sinix */

#ifdef OSF1_VDUMP	/* this is for OSF/1 3.2's vdump for advfs */
  { DMP_NORMAL, "^The -s option is ignored"},			/* OSF/1 */
  { DMP_NORMAL, "^path"},					/* OSF/1 */
  { DMP_NORMAL, "^dev/fset"},					/* OSF/1 */
  { DMP_NORMAL, "^type"},					/* OSF/1 */
  { DMP_NORMAL, "^advfs id"},					/* OSF/1 */
  { DMP_NORMAL, "^[A-Z][a-z][a-z] [A-Z][a-z][a-z] .[0-9] [0-9]"}, /* OSF/1 */
#endif

  { DMP_NORMAL, "^backup:" },					/* AIX */
  { DMP_NORMAL, "^        Use the umount command to unmount the filesystem" },

  { DMP_NORMAL, "^[ \t]*\\\n" },

  /* catch-all; DMP_STRANGE is returned for all other lines */
  { DMP_STRANGE, NULL, 0}
};

static void start_backup(host, disk, level, dumpdate, dataf, mesgf, indexf)
char *host;
char *disk;
int level, dataf, mesgf, indexf;
char *dumpdate;
{
    int dumpin, dumpout;
    char dumpkeys[80];
    char device[1024];
    char cmd[4096];
    char indexcmd[4096];
    
    fprintf(stderr, "%s: start [%s:%s level %d]\n",
	    pname, host, disk, level);

    NAUGHTY_BITS;

    if(compress)
	comppid = pipespawn(COMPRESS_PATH, &dumpout, dataf, mesgf,
			    COMPRESS_PATH,
#if defined(COMPRESS_BEST_OPT) && defined(COMPRESS_FAST_OPT)
			    compress == COMPR_BEST? 
			        COMPRESS_BEST_OPT : COMPRESS_FAST_OPT,
#endif
			    (char *)0);
    else {
	dumpout = dataf;
	comppid = -1;
    }

    /* invoke dump */
#ifdef OSF1_VDUMP
    strcpy(device, amname_to_dirname(disk));
#else
    strcpy(device, amname_to_devname(disk));
#endif

#if defined(USE_RUNDUMP) || !defined(DUMP)
    sprintf(cmd, "%s/rundump%s", libexecdir, versionsuffix());
#else
    sprintf(cmd, "%s", DUMP);
#endif

#ifndef AIX_BACKUP
    /* normal dump */
#ifdef XFSDUMP
#ifdef DUMP
    if (!strcmp(amname_to_fstype(device), "xfs"))
#else
    if (1)
#endif
    {
#ifdef USE_RUNDUMP
        char *progname = cmd;
#else
	char *progname = XFSDUMP;
#endif
	program->backup_name  = XFSDUMP;
#ifndef XFSRESTORE
#define XFSRESTORE "xfsrestore"
#endif
	program->restore_name = XFSRESTORE;

	sprintf(indexcmd,
		"%s -t -v silent - 2>/dev/null | /sbin/sed -e 's/^/\\//'",
		XFSRESTORE);

	write_tapeheader();

	start_index(createindex, dumpout, mesgf, indexf, indexcmd);

	sprintf(dumpkeys, "%d", level);
	if (no_record)
	{
	    dumppid = pipespawn(progname, &dumpin, dumpout, mesgf,
				"xfsdump", "-J", "-F", "-l", dumpkeys, "-",
				device, (char *)0);
	}
	else
	{
	    dumppid = pipespawn(progname, &dumpin, dumpout, mesgf,
				"xfsdump", "-F", "-l", dumpkeys, "-",
				device, (char *)0);
	}
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
        char *progname = cmd;
#else
	char *progname = VXDUMP;
#endif
	program->backup_name  = VXDUMP;
#ifndef VXRESTORE
#define VXRESTORE "vxrestore"
#endif
	program->restore_name = VXRESTORE;

	sprintf(dumpkeys, "%d%ssf", level, no_record ? "" : "u");

	sprintf(indexcmd,
		"%s -tvf - 2>/dev/null |\
		    awk '/^leaf/ {$1=\"\"; $2=\"\"; print}' |\
		    cut -c4-",
		VRESTORE);

	write_tapeheader();

	start_index(createindex, dumpout, mesgf, indexf, indexcmd);

	dumppid = pipespawn(progname, &dumpin, dumpout, mesgf, 
			    "vxdump", dumpkeys, "100000", "-", device,
			    (char *)0);
    }
    else
#endif

    {
	sprintf(dumpkeys, "%d%s%sf", level, no_record ? "" : "u",
#ifdef OSF1_VDUMP
		"b"
#else
		"s"
#endif
		);

	sprintf(indexcmd,
		"%s -tvf - 2>/dev/null |\
		    awk '/^leaf/ {$1=\"\"; $2=\"\"; print}' |\
		    cut -c4-",
#ifdef RESTORE
		    RESTORE
#else
		    "restore"
#endif
		);

	write_tapeheader();

	start_index(createindex, dumpout, mesgf, indexf, indexcmd);

	dumppid = pipespawn(cmd, &dumpin, dumpout, mesgf, 
			    "dump", dumpkeys,
#ifdef OSF1_VDUMP
			    "60",
#else
			    "1048576",
#endif
			    "-", device,
			    (char *)0);
    }
#else
    /* AIX backup program */
    sprintf(dumpkeys, "-%d%sf", level, no_record ? "" : "u");

	sprintf(indexcmd,
		"%s -B -tvf - 2>/dev/null |\
		    awk '/^leaf/ {$1=\"\"; $2=\"\"; print}' |\
		    cut -c4-",
#ifdef RESTORE
		    RESTORE
#else
		    "restore"
#endif
		);

    write_tapeheader();

    start_index(createindex, dumpout, mesgf, indexf, indexcmd);

    dumppid = pipespawn(cmd, &dumpin, dumpout, mesgf, 
			"backup", dumpkeys, "-", device, (char *)0);
#endif

    /* close the write ends of the pipes */

    close(dumpin);
    close(dumpout);
    close(dataf);
    close(mesgf);
    close(indexf);
}

static void end_backup(status)
int status;
{
    /* don't need to do anything for dump */
}

backup_program_t dump_program = {
  "DUMP",
#ifdef DUMP
  DUMP
#else
  "dump"
#endif
  ,
#ifdef RESTORE
  RESTORE
#else
  "restore"
#endif
  ,
  re_table, start_backup, end_backup
};
