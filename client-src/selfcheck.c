/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991 University of Maryland
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
 * selfcheck.c - do self-check and send back any error messages
 */

#include "amanda.h"
#include "statfs.h"
#include "version.h"
#include "getfsent.h"

#ifdef SAMBA_CLIENT
#include "findpass.h"
#endif

#define MAXLINE 4096

char line[MAXLINE];
char *pname = "selfcheck";

/* local functions */
int main P((int argc, char **argv));

static void check_disk P((char *program, char *disk, int level));
static void check_overall P((void));
static void check_file P((char *filename, int mode));
static void check_space P((char *dir, long kbytes));

int main(argc, argv)
int argc;
char **argv;
{
    int level;
    char program[128], disk[256];

    /* initialize */

    chdir("/tmp");
    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);
    umask(0);
    dbopen("/tmp/selfcheck.debug");
    dbprintf(("%s: version %s\n", argv[0], version()));

    /* handle all service requests */

    while(fgets(line, MAXLINE, stdin)) {
	if(!strncmp(line, "OPTIONS", 7)) {
	    /* we don't recognize any options yet */
	    printf("OPTIONS ;\n");
	    continue;
	}

	if(sscanf(line, "%s %s %d\n", program, disk, &level) != 3) goto err;
	check_disk(program, disk, level);
    }

    check_overall();

    dbclose();
    return 0;

 err:
    printf("ERROR [BOGUS REQUEST PACKET]\n");
    dbprintf(("REQ packet is bogus\n"));
    dbclose();
    return 1;
}


static void check_disk(program, disk, level)
char *program, *disk;
int level;
{
    int tstfd;
    char *device;

    if (strcmp(program, "GNUTAR") == 0) {
#ifdef SAMBA_CLIENT
        if(disk[0] == '/' && disk[1] == '/') {
	    char cmd[256], pass[256], domain[256];

	    if (!findpass(disk, pass, domain)) {
		printf("ERROR [can't find password for %s]\n", disk);
		return;
	    }
	    makesharename(disk, device, 1);
	    sprintf(cmd, "%s %s %s -U backup%s%s -c quit", SAMBA_CLIENT,
		    device, pass, domain[0] ? " -W " : "", domain);
	    printf("running %s %s XXXX -U backup%s%s -c quit",
		   SAMBA_CLIENT, device, domain[0] ? " -W " : "", domain);
	    if (system(cmd) & 0xff00)
		printf("ERROR [PC SHARE %s access error: host down or invalid password?]\n", disk);
	    else
		printf("OK %s\n", disk);
	    return;
	}
#endif
	device = amname_to_dirname(disk);
    } else {
#ifdef OSF1_VDUMP
        device = amname_to_dirname(disk);
#else
        device = amname_to_devname(disk);
#endif
    }
    
#ifndef CHECK_FOR_ACCESS_WITH_OPEN
    if(access(device, R_OK) == -1)
	    printf("ERROR [can not access %s (%s): %s]\n",
		   device, disk, strerror(errno));
    else {
	    printf("OK %s\n", disk);
    }

#else
    /* XXX better check in this case */
    if((tstfd = open(device, O_RDONLY)) == -1)
	printf("ERROR [could not open %s (%s): %s]\n",
	       device, disk, strerror(errno));
    else
	printf("OK %s\n", device);
    close(tstfd);
#endif

    /* XXX perhaps do something with level: read dumpdates and sanity check */
}

static void check_overall()
{
#ifdef SAMBA_CLIENT
    struct stat buf;
    int testfd;
#endif
#ifdef DUMP
    check_file(DUMP, X_OK);
#endif
#ifdef XFSDUMP
    check_file(XFSDUMP, X_OK);
#endif
#ifdef VXDUMP
    check_file(VXDUMP, X_OK);
#endif
#ifdef GNUTAR
    check_file(GNUTAR, X_OK);
#endif
#ifdef SAMBA_CLIENT
    check_file(SAMBA_CLIENT, X_OK);
    testfd = open("/etc/amandapass", R_OK);
    if (testfd >= 0) {
	if(!fstat(testfd, &buf)) {
	    if (buf.st_mode & 0x7)
		printf("ERROR [/etc/amandapass is world readable!]\n");
	    else
		printf("OK [/etc/amandapass is readable, but not by all]\n");
	} else
	    printf("OK [unable to access /etc/amandapass?]\n");
	close(testfd);
    }
    else
	printf("ERROR [unable to access /etc/amandapass?]\n");
#endif
    check_file(COMPRESS_PATH, X_OK);
#if defined(DUMP) || defined(XFSDUMP)
#ifdef OSF1_VDUMP
    check_file("/etc/vdumpdates", R_OK|W_OK);
#else
    check_file("/etc/dumpdates", R_OK|W_OK);
#endif
#endif
    check_file("/dev/null", R_OK|W_OK);
    check_space("/tmp", 64);		/* for amandad i/o */
    check_space("/etc", 64);		/* for /etc/dumpdates writing */
}

static void check_space(dir, kbytes)
char *dir;
long kbytes;
{
    generic_fs_stats_t statp;

    if(get_fs_stats(dir, &statp) == -1)
	printf("ERROR [cannot statfs %s: %s]\n", dir, strerror(errno));
    else if(statp.avail < kbytes)
	printf("ERROR [dir %s needs %ldKB, only has %ldKB available.]\n",
	       dir, kbytes, statp.avail);
    else
	printf("OK %s has more than %ld KB available.\n", dir, kbytes);
}

static void check_file(filename, mode)
char *filename;
int mode;
{
    char *noun, *adjective;

    if((mode & X_OK) == X_OK)
	noun = "execute", adjective = "executable";
    else if((mode & (W_OK|R_OK)) == (W_OK|R_OK))
	noun = "read/write", adjective = "read/writeable";
    else 
	noun = "access", adjective = "accessible";

    if(access(filename, mode) == -1)
	printf("ERROR [can not %s %s: %s]\n", noun, filename, strerror(errno));
    else
	printf("OK %s %s\n", filename, adjective);
}

