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

#ifdef SAMBA_CLIENT
#include "findpass.h"
#endif

#define MAXLINE 4096

char line[MAXLINE];
char *pname = "selfcheck-dump";

/* local functions */
int main P((int argc, char **argv));

static void check_disk P((char *disk, int level));
static void check_overall P((void));
static void check_file P((char *filename, int mode));
static void check_space P((char *dir, long kbytes));

int main(argc, argv)
int argc;
char **argv;
{
    int level;
    char disk[256];

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

	if(sscanf(line, "%s %d\n", disk, &level) != 2) goto err;
	check_disk(disk, level);
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


static void check_disk(disk, level)
char *disk;
int level;
{
    int tstfd;
    char device[80];

    if(disk[0] == '/') {
#ifdef SAMBA_CLIENT
	if (disk[1] == '/') {
	    char cmd[256], pass[256];

	    if (!findpass(disk, pass)) {
		printf("ERROR [can't find password for %s]\n", disk);
		return;
	    }
	    makesharename(disk, device, 1);
	    sprintf(cmd, "%s %s %s -U backup -c quit", SAMBA_CLIENT,
		    device, pass);
	    printf("running %s %s XXXX -U backup -c quit",
		   SAMBA_CLIENT, device);
	    if (system(cmd) & 0xff00)
		printf("ERROR [PC SHARE %s access error: host down or invalid password?]\n", disk);
	    else
		printf("OK %s\n", disk);
	    return;
	}
#endif

	/* XXX better check in this case */
	if(access(disk, R_OK) == -1)
	    printf("ERROR [can not access %s: %s]\n",
		   disk, strerror(errno));
	else
	    printf("OK %s\n", disk);
	return;
    }

    sprintf(device, "%s%s", RDEV_PREFIX, disk);

    if((tstfd = open(device, O_RDONLY)) == -1)
	printf("ERROR [could not open %s: %s]\n",
	       device, strerror(errno));
    else
	printf("OK %s\n", device);
    close(tstfd);

    /* XXX perhaps do something with level: read dumpdates and sanity check */
}

static void check_overall()
{
#ifdef SAMBA_CLIENT
    struct stat buf;
    int testfd;
#endif
    check_file(DUMP, X_OK);
#ifdef XFSDUMP
    check_file(XFSDUMP, X_OK);
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
#ifdef OSF1_VDUMP
    check_file("/etc/vdumpdates", R_OK|W_OK);
#else
    check_file("/etc/dumpdates", R_OK|W_OK);
#endif
    check_file("/dev/null", R_OK|W_OK);
    check_space("/tmp", 64);		/* for amandad i/o */
    if ( !strcmp("/tmp", INDEX_TMP_DIR) )
	check_space(INDEX_TMP_DIR, 64);	/* for dump/gnutar index files */
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

