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
 * $Id: sendbackup-dump.c,v 1.63.2.1 1998/08/27 16:26:55 blair Exp $
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

#define LEAF_AND_DIRS "sed -e \'\ns/^leaf[ \t]*[0-9]*[ \t]*\\.//\nt\n/^dir[ \t]/ {\ns/^dir[ \t]*[0-9]*[ \t]*\\.//\ns%$%/%\nt\n}\nd\n\'"

static regex_t re_table[] = {
  /* the various encodings of dump size */
  /* this should also match BSDI pre-3.0's buggy dump program, that
     produced doubled DUMP: DUMP: messages */
  { DMP_SIZE, 
	"DUMP: [0-9][0-9]* tape blocks",				1024},

  { DMP_SIZE,
	"dump: Actual: [0-9][0-9]* tape blocks",			1024},

  { DMP_SIZE,
        "backup: There are [0-9][0-9]* tape blocks on [0-9][0-9]* tapes",    1024},

  { DMP_SIZE,
        "backup: [0-9][0-9]* tape blocks on [0-9][0-9]* tape\\(s\\)",       1024},

  { DMP_SIZE,
	"backup: [0-9][0-9]* 1k blocks on [0-9][0-9]* volume\\(s\\)",	1024},

  { DMP_SIZE,
	"DUMP: [0-9][0-9]* blocks \\([0-9][0-9]*KB\\) on [0-9][0-9]* volume", 512},

  { DMP_SIZE,
"DUMP: [0-9][0-9]* blocks \\([0-9][0-9]*\\.[0-9][0-9]*MB\\) on [0-9][0-9]* volume",
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

  { DMP_SIZE, "vxdump: vxdump: [0-9][0-9]* tape blocks", 1024},
		/* HPUX 10.20 and above vxdump */

  { DMP_SIZE, "vxdump: vxdump: [0-9][0-9]* blocks", 1024},
		/* UnixWare vxdump */

  { DMP_SIZE, "   VXDUMP: [0-9][0-9]* blocks",                            512},
		/* SINIX vxdump */

  { DMP_SIZE, "   UFSDUMP: [0-9][0-9]* blocks",                           512},
		/* SINIX ufsdump */

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
  { DMP_NORMAL, "^  VXDUMP:" },                                 /* Sinix */
  { DMP_NORMAL, "^  UFSDUMP:" },                                /* Sinix */

#ifdef VDUMP	/* this is for OSF/1 3.2's vdump for advfs */
  { DMP_NORMAL, "^The -s option is ignored"},			/* OSF/1 */
  { DMP_NORMAL, "^path"},					/* OSF/1 */
  { DMP_NORMAL, "^dev/fset"},					/* OSF/1 */
  { DMP_NORMAL, "^type"},					/* OSF/1 */
  { DMP_NORMAL, "^advfs id"},					/* OSF/1 */
  { DMP_NORMAL, "^[A-Z][a-z][a-z] [A-Z][a-z][a-z] .[0-9] [0-9]"}, /* OSF/1 */
#endif

  { DMP_NORMAL, "^backup:" },					/* AIX */
  { DMP_NORMAL, "^        Use the umount command to unmount the filesystem" },

  { DMP_NORMAL, "^[ \t]*$" },

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
    char *dumpkeys = NULL;
    char *device = NULL;
    char *cmd = NULL;
    char *indexcmd = NULL;
    char level_str[NUM_STR_SIZE];
    char *fstype = NULL;

    ap_snprintf(level_str, sizeof(level_str), "%d", level);

    fprintf(stderr, "%s: start [%s:%s level %d]\n",
	    get_pname(), host, disk, level);

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
    device = amname_to_devname(disk);

#if defined(USE_RUNDUMP) || !defined(DUMP)
    cmd = vstralloc(libexecdir, "/", "rundump", versionsuffix(), NULL);
#else
    cmd = stralloc(DUMP);
#endif

#ifndef AIX_BACKUP					/* { */
    /* normal dump */
#ifdef XFSDUMP						/* { */
#ifdef DUMP						/* { */
    fstype = amname_to_fstype(device);
    if (strcmp(fstype, "xfs") == 0)
#else							/* } { */
    if (1)
#endif							/* } */
    {
        char *progname = cmd = newvstralloc(cmd, libexecdir, "/", "rundump",
					    versionsuffix(), NULL);
	program->backup_name  = XFSDUMP;
#ifndef XFSRESTORE
#define XFSRESTORE "xfsrestore"
#endif
	program->restore_name = XFSRESTORE;

	indexcmd = vstralloc(XFSRESTORE,
			     " -t",
			     " -v", " silent",
			     " -",
			     " 2>/dev/null",
			     " | /sbin/sed",
			     " -e", " \'s/^/\\//\'",
			     NULL);
	write_tapeheader();

	start_index(createindex, dumpout, mesgf, indexf, indexcmd);

	dumpkeys = stralloc(level_str);
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
#endif							/* } */
#ifdef VXDUMP						/* { */
#ifdef DUMP
    fstype = amname_to_fstype(device);
    if (strcmp(fstype, "vxfs") == 0)
#else
    if (1)
#endif
    {
#ifdef USE_RUNDUMP
        char *progname = cmd = newvstralloc(cmd, libexecdir, "/", "rundump",
					    versionsuffix(), NULL);
#else
	char *progname = cmd = newvstralloc(cmd, VXDUMP, NULL);
#endif
	program->backup_name  = VXDUMP;
#ifndef VXRESTORE
#define VXRESTORE "vxrestore"
#endif
	program->restore_name = VXRESTORE;

	dumpkeys = vstralloc(level_str, no_record ? "" : "u", "s", "f", NULL);

	indexcmd = vstralloc(VXRESTORE,
			     " -tvf", " -",
			     " 2>/dev/null",
			     " | ",
			     LEAF_AND_DIRS,
			     NULL);
	write_tapeheader();

	start_index(createindex, dumpout, mesgf, indexf, indexcmd);

	dumppid = pipespawn(progname, &dumpin, dumpout, mesgf, 
			    "vxdump", dumpkeys, "1048576", "-", device,
			    (char *)0);
    }
    else
#endif							/* } */

#ifdef VDUMP						/* { */
#ifdef DUMP
    if (strcmp(amname_to_fstype(device), "advfs") == 0)
#else
    if (1)
#endif
    {
        char *progname = cmd = newvstralloc(cmd, libexecdir, "/", "rundump",
					    versionsuffix(), NULL);
	amfree(device);
	device = amname_to_dirname(disk);
	program->backup_name  = VDUMP;
#ifndef VRESTORE
#define VRESTORE "vrestore"
#endif
	program->restore_name = VRESTORE;

	dumpkeys = vstralloc(level_str, no_record ? "" : "u", "b", "f", NULL);

	indexcmd = vstralloc(VRESTORE,
			     " -tvf", " -",
			     " 2>/dev/null",
			     " | ",
			     "sed -e \'\n/^\\./ {\ns/^\\.//\ns/, [0-9]*$//\ns/^\\.//\ns/ @-> .*$//\nt\n}\nd\n\'",
			     NULL);
	write_tapeheader();

	start_index(createindex, dumpout, mesgf, indexf, indexcmd);

	dumppid = pipespawn(cmd, &dumpin, dumpout, mesgf, 
			    "vdump", dumpkeys,
			    "60",
			    "-", device,
			    (char *)0);
    }
    else
#endif							/* } */

    {
#ifndef RESTORE
#define RESTORE "restore"
#endif

#ifdef HAVE_HONOR_NODUMP
	dumpkeys = vstralloc(level_str, no_record ? "" : "u", "s", "h", "f", NULL);
#else
	dumpkeys = vstralloc(level_str, no_record ? "" : "u", "s", "f", NULL);
#endif

	indexcmd = vstralloc(RESTORE,
			     " -tvf", " -",
			     " 2>&1",
			     /* not to /dev/null because of DU's dump */
			     " | ",
			     LEAF_AND_DIRS,
			     NULL);
	write_tapeheader();

	start_index(createindex, dumpout, mesgf, indexf, indexcmd);

	dumppid = pipespawn(cmd, &dumpin, dumpout, mesgf, 
			    "dump", dumpkeys,
			    "1048576",
#ifdef HAVE_HONOR_NODUMP
			    "0",
#endif
			    "-", device,
			    (char *)0);
    }
#else							/* } { */
    /* AIX backup program */
    dumpkeys = vstralloc("-", level_str, no_record ? "" : "u", "f", NULL);

    indexcmd = vstralloc(RESTORE,
			 " -B",
			 " -tvf", " -",
			 " 2>/dev/null",
			 " | ",
			 LEAF_AND_DIRS,
			 NULL);
    write_tapeheader();

    start_index(createindex, dumpout, mesgf, indexf, indexcmd);

    dumppid = pipespawn(cmd, &dumpin, dumpout, mesgf, 
			"backup", dumpkeys, "-", device, (char *)0);
#endif							/* } */

    amfree(dumpkeys);
    amfree(device);
    amfree(fstype);
    amfree(cmd);
    amfree(indexcmd);

    /* close the write ends of the pipes */

    aclose(dumpin);
    aclose(dumpout);
    aclose(dataf);
    aclose(mesgf);
    aclose(indexf);
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
  RESTORE
  ,
  re_table, start_backup, end_backup
};
