/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1993 University of Maryland
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
 * $Id: genversion.c,v 1.14 1998/01/22 21:36:54 amcore Exp $
 *
 * dump the current Amanda version info
 */
#include "amanda.h"
#include "version.h"

int main P((void));

#define MARGIN 	70

#define newline() do {							\
    printf("  \"%s\\n\",\n", line);					\
    *line = '\0';							\
    linelen = 0;							\
} while (0)

/* Print a string */
#define prstr(string) do {						\
    int len = strlen(string);						\
    if(linelen+len >= MARGIN) { 					\
	newline(); 							\
	ap_snprintf(line, sizeof(line), "%*s", indent, "");		\
	linelen = indent;						\
    }									\
    line[sizeof(line)-1] = '\0';					\
    strncat(line, (string), sizeof(line)-strlen(line));			\
    linelen += len;							\
} while (0)

/* Print a "variable" */
#define prvar(var, val) do {						\
    str = newvstralloc(str, (var), "=\\\"", (val), "\\\"", NULL);	\
    prstr(str);								\
} while(0)

char *pname = "genversion";

int main()
{
    char line[STR_SIZE], *str = NULL;
    int  linelen, indent;
    int fd;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    printf("/* version.c - generated by genversion.c - DO NOT EDIT! */\n");
    printf("char *version_info[] = {\n");

    *line = '\0', linelen = 0, indent = 0;


    prstr("build:"); indent = linelen;

    {
	char version_str[STR_SIZE];

	ap_snprintf(version_str, sizeof(version_str), "Amanda-%s", version());
	prvar(" VERSION", version_str);
    }

#ifdef BUILT_DATE
    prvar(" BUILT_DATE", BUILT_DATE);
#endif

#ifdef BUILT_MACH
    prvar(" BUILT_MACH", BUILT_MACH);
#endif

#ifdef CC
    prvar(" CC", CC);
#endif

    newline();


    prstr("paths:"); indent = linelen;

    prvar(" bindir", bindir);
    prvar(" sbindir", sbindir);
    prvar(" libexecdir", libexecdir);
    prvar(" mandir", mandir);
    prvar(" CONFIG_DIR", CONFIG_DIR);

#ifdef DEV_PREFIX
    prvar(" DEV_PREFIX", DEV_PREFIX);
#endif

#ifdef RDEV_PREFIX
    prvar(" RDEV_PREFIX", RDEV_PREFIX);
#endif

#ifdef DUMP
    prvar(" DUMP", DUMP);
#endif

#ifdef RESTORE
    prvar(" RESTORE", RESTORE);
#endif

#ifdef VDUMP
    prvar(" VDUMP", VDUMP);
#endif

#ifdef VRESTORE
    prvar(" VRESTORE", VRESTORE);
#endif

#ifdef XFSDUMP
    prvar(" XFSDUMP", XFSDUMP);
#endif

#ifdef XFSRESTORE
    prvar(" XFSRESTORE", XFSRESTORE);
#endif

#ifdef VXDUMP
    prvar(" VXDUMP", VXDUMP);
#endif

#ifdef VXRESTORE
    prvar(" VXRESTORE", VXRESTORE);
#endif

#ifdef SAMBA_CLIENT
    prvar(" SAMBA_CLIENT", SAMBA_CLIENT);
#endif

#ifdef GNUTAR
    prvar(" GNUTAR", GNUTAR);
#endif

#ifdef COMPRESS_PATH
    prvar(" COMPRESS_PATH", COMPRESS_PATH);
#endif

#ifdef UNCOMPRESS_PATH
    prvar(" UNCOMPRESS_PATH", UNCOMPRESS_PATH);
#endif

    prvar(" MAILER", MAILER);

#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
    prvar(" listed_incr_dir", GNUTAR_LISTED_INCREMENTAL_DIR);
#endif

    newline();


    prstr("defs: "); indent = linelen;

    prvar(" DEFAULT_SERVER", DEFAULT_SERVER);
    prvar(" DEFAULT_CONFIG", DEFAULT_CONFIG);
    prvar(" DEFAULT_TAPE_SERVER", DEFAULT_TAPE_SERVER);
    prvar(" DEFAULT_TAPE_DEVICE", DEFAULT_TAPE_DEVICE);

#ifdef AIX_BACKUP
    prstr(" AIX_BACKUP");
#endif

#ifdef AIX_TAPEIO
    prstr(" AIX_TAPEIO");
#endif

#ifdef BROKEN_VOID
    prstr(" BROKEN_VOID");
#endif

#ifdef DUMP_RETURNS_1
    prstr(" DUMP_RETURNS_1");
#endif

#ifdef HAVE_MMAP
    prstr(" HAVE_MMAP");
#endif

#ifndef HAVE_STRERROR
    prstr(" NEED_STRERROR");
#endif

#ifndef HAVE_STRSTR
    prstr(" NEED_STRSTR");
#endif

#ifdef HAVE_SYSVSHM
    prstr(" HAVE_SYSVSHM");
#endif

#ifdef USE_POSIX_FCNTL
    prstr(" LOCKING=POSIX_FCNTL");
#endif
#ifdef USE_FLOCK
    prstr(" LOCKING=FLOCK");
#endif
#ifdef USE_LOCKF
    prstr(" LOCKING=LOCKF");
#endif
#ifdef USE_LNLOCK
    prstr(" LOCKING=LNLOCK");
#endif
#if !defined(USE_POSIX_FCNTL) && !defined(USE_FLOCK) && !defined(USE_LOCK) && !defined(USE_LNLOCK)
    prstr(" LOCKING=**NONE**");
#endif

#ifdef STATFS_BSD
    prstr(" STATFS_BSD");
#endif

#ifdef STATFS_OSF1
    prstr(" STATFS_OSF1");
#endif

#ifdef STATFS_ULTRIX
    prstr(" STATFS_ULTRIX");
#endif

#ifdef SETPGRP_VOID
    prstr(" SETPGRP_VOID");
#endif

#ifdef ASSERTIONS
    prstr(" ASSERTIONS");
#endif

#ifdef DEBUG_CODE
    prstr(" DEBUG_CODE");
#endif

#ifdef DEBUG_FILE_WITH_PID
    prstr(" DEBUG_FILE_WITH_PID");
#endif

#ifdef BSD_SECURITY
    prstr(" BSD_SECURITY");
#endif

#ifdef USE_AMANDAHOSTS
    prstr(" USE_AMANDAHOSTS");
#endif

#ifdef USE_RUNDUMP
    prstr(" USE_RUNDUMP");
#endif

#ifdef KRB4_SECURITY
#define HOSTNAME_INSTANCE "<hostname>"
    {
	char lifetime_str[NUM_STR_SIZE];

	prstr(" KRB4_SECURITY");
	prvar(" SERVER_HOST_PRINCIPLE", SERVER_HOST_PRINCIPLE);
	prvar(" SERVER_HOST_INSTANCE", SERVER_HOST_INSTANCE);
	prvar(" SERVER_HOST_KEY_FILE", SERVER_HOST_KEY_FILE);
	prvar(" CLIENT_HOST_PRINCIPLE", CLIENT_HOST_PRINCIPLE);
	prvar(" CLIENT_HOST_INSTANCE", CLIENT_HOST_INSTANCE);
	prvar(" CLIENT_HOST_KEY_FILE", CLIENT_HOST_KEY_FILE);
	ap_snprintf(lifetime_str, sizeof(lifetime_str), "%d", TICKET_LIFETIME);
	prvar(" TICKET_LIFETIME", lifetime_str);
    }
#endif

#ifdef CLIENT_LOGIN
    prvar(" CLIENT_LOGIN", CLIENT_LOGIN);
#endif

#ifdef FORCE_USERID
    prstr(" FORCE_USERID");
#endif

#ifdef USE_VERSION_SUFFIXES
    prstr(" USE_VERSION_SUFFIXES");
#endif

#ifdef HAVE_GZIP
    prstr(" HAVE_GZIP");
#endif

#ifdef COMPRESS_SUFFIX
    prvar(" COMPRESS_SUFFIX", COMPRESS_SUFFIX);
#endif

#ifdef COMPRESS_FAST_OPT
    prvar(" COMPRESS_FAST_OPT", COMPRESS_FAST_OPT);
#endif

#ifdef COMPRESS_BEST_OPT
    prvar(" COMPRESS_BEST_OPT", COMPRESS_BEST_OPT);
#endif

#ifdef UNCOMPRESS_OPT
    prvar(" UNCOMPRESS_OPT", UNCOMPRESS_OPT);
#endif

    newline();


    printf("  0\n};\n");

    return 0;
}
