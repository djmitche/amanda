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
 * sendbackup-dump.c - send backup data using BSD dump
 */

#include "sendbackup-common.h"
#include "getfsent.h"
#include "version.h"

#ifdef KRB4_SECURITY
#include "sendbackup-krb4.c"
#else					/* I'd tell you what this does */
#define NAUGHTY_BITS			/* but then I'd have to kill you */
#endif


regex_t re_table[] = {
  /* the various encodings of dump size */
  { DMP_SIZE, 
	"DUMP: DUMP: [0-9][0-9]* tape blocks",				1024},

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

  { DMP_SIZE, "dump: Dumped  [0-9][0-9]* of [0-9][0-9]* bytes",             1},
		/* DU 4.0 vdump */
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
  { DMP_NORMAL, "^xfsdump:" },					/* IRIX xfs */

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

char *backup_program_name = "dump";	/* for printing */
char *restore_program_name = "restore";
char *amanda_backup_program = "DUMP";	/* for the header */

void start_backup(disk, level, datestamp, dataf, mesgf)
char *disk, *datestamp;
int level, dataf, mesgf;
{
    int dumpin, dumpout;
    int indexout;
    char host[MAX_HOSTNAME_LENGTH], dumpkeys[80];
    char device[80];
    char cmd[256];
    
    host[sizeof(host)-1] = '\0';
    if(gethostname(host, sizeof(host)-1) == -1)
        error("error [gethostname: %s]", strerror(errno));

    fprintf(stderr, "%s: start [%s:%s level %d datestamp %s]\n",
	    pname, host, disk, level, datestamp);

    NAUGHTY_BITS;

    write_tapeheader(host, disk, level, compress, datestamp, dataf);

    if(compress)
	comppid = pipespawn(COMPRESS_PATH, &indexout, dataf, mesgf,
			    COMPRESS_PATH,
#if defined(COMPRESS_BEST_OPT) && defined(COMPRESS_FAST_OPT)
			    compress == COMPR_BEST? 
			        COMPRESS_BEST_OPT : COMPRESS_FAST_OPT,
#endif
			    (char *)0);
    else {
	indexout = dataf;
	comppid = -1;
    }

    if(createindex) {
	char cmd2[256];
	char level_str[12];

	sprintf(cmd2, "createindex-dump%s", versionsuffix());
        sprintf(cmd, "%s/%s", libexecdir, cmd2);
	sprintf(level_str, "%d", level);

	indexpid = pipespawn(cmd, &dumpout, indexout, mesgf,
			     cmd2, host, disk, level_str, datestamp,
			     (char *)0);
	dbprintf(("sendbackup-dump: pid %d: %s %s %s %s %s\n",
		  indexpid, cmd, host, disk, level_str, datestamp));
    } else {
	dumpout = indexout;
	indexpid = -1;
    }

    /* invoke dump */

    if(disk[0] == '/')
	strcpy(device, disk);
    else
	sprintf(device, "%s%s", DEV_PREFIX, disk);

#ifdef USE_RUNDUMP
    sprintf(cmd, "%s/rundump%s", libexecdir, versionsuffix());
#else
    sprintf(cmd, "%s", DUMP);
#endif

#ifndef AIX_BACKUP
    /* normal dump */
#ifdef XFSDUMP
    if (!strcmp(amname_to_fstype(device), "xfs"))
    {
	sprintf(dumpkeys, "%d", level);
	if (no_record)
	{
	    dumppid = pipespawn(XFSDUMP, &dumpin, dumpout, mesgf,
				"xfsdump", "-J", "-F", "-l", dumpkeys, "-",
				device, (char *)0);
	}
	else
	{
	    dumppid = pipespawn(XFSDUMP, &dumpin, dumpout, mesgf,
				"xfsdump", "-F", "-l", dumpkeys, "-",
				device, (char *)0);
	}
    }
    else
#endif
    {
	sprintf(dumpkeys, "%d%ssf", level, no_record ? "" : "u");

	dumppid = pipespawn(cmd, &dumpin, dumpout, mesgf, 
			    "dump", dumpkeys, "100000", "-", device,
			    (char *)0);
    }
#else
    /* AIX backup program */
    sprintf(dumpkeys, "-%d%sf", level, no_record ? "" : "u");
    dumppid = pipespawn(cmd, &dumpin, dumpout, mesgf, 
			"backup", dumpkeys, "-", device, (char *)0);
#endif

    /* close the write ends of the pipes */

    close(dumpin);
    close(dumpout);
    close(indexout);
    close(dataf);
    close(mesgf);
}

void end_backup(status)
int status;
{
    /* don't need to do anything for dump */
}
