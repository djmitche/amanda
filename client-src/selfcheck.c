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
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/* 
 * $Id: selfcheck.c,v 1.39 1998/09/19 00:04:06 oliva Exp $
 *
 * do self-check and send back any error messages
 */

#include "amanda.h"
#include "statfs.h"
#include "version.h"
#include "getfsent.h"
#include "amandates.h"

#ifdef SAMBA_CLIENT
#include "findpass.h"
#endif

int need_samba=0;
int need_rundump=0;
int need_dump=0;
int need_restore=0;
int need_vdump=0;
int need_vrestore=0;
int need_xfsdump=0;
int need_xfsrestore=0;
int need_vxdump=0;
int need_vxrestore=0;
int need_runtar=0;
int need_gnutar=0;
int need_compress_path=0;

/* local functions */
int main P((int argc, char **argv));

static void check_options P((char *program, char *disk, char *str));
static void check_disk P((char *program, char *disk, int level));
static void check_overall P((void));
static void check_file P((char *filename, int mode));
static void check_dir P((char *dirname, int mode));
static void check_suid P((char *filename));
static void check_space P((char *dir, long kbytes));

int main(argc, argv)
int argc;
char **argv;
{
    int level;
    char *line = NULL;
    char *program = NULL;
    char *disk = NULL;
    char *optstr = NULL;
    char *s;
    int ch;
    int fd;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;

    /* initialize */

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    set_pname("selfcheck");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    chdir("/tmp");
    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);
    umask(0007);
    dbopen();
    dbprintf(("%s: version %s\n", argv[0], version()));

    /* handle all service requests */

    for(; (line = agets(stdin)) != NULL; free(line)) {
#define sc "OPTIONS"
	if(strncmp(line, sc, sizeof(sc)-1) == 0) {
#undef sc
	    /* we don't recognize any options yet */
	    printf("OPTIONS ;\n");
	    continue;
	}

	s = line;
	ch = *s++;

	skip_whitespace(s, ch);			/* find program name */
	if (ch == '\0') {
	    goto err;				/* no program */
	}
	program = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';				/* terminate the program name */

	skip_whitespace(s, ch);			/* find disk name */
	if (ch == '\0') {
	    goto err;				/* no disk */
	}
	disk = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';				/* terminate the disk name */

	skip_whitespace(s, ch);			/* find level number */
	if (ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	    goto err;				/* bad level */
	}
	skip_integer(s, ch);

#define sc "OPTIONS"
	skip_whitespace(s, ch);
	if (ch && strncmp (s - 1, sc, sizeof(sc)-1) == 0) {
	    s += sizeof(sc)-1;
	    ch = s[-1];
#undef sc
	    skip_whitespace(s, ch);		/* find the option string */
	    if(ch == '\0') {
		goto err;			/* bad options string */
	    }
	    optstr = s - 1;
	    skip_non_whitespace(s, ch);
	    s[-1] = '\0';			/* terminate the options */
	    check_options(program, disk, optstr);
	    check_disk(program, disk, level);
	} else if (ch == '\0') {
	    /* check all since no option */
	    need_samba=1;
	    need_rundump=1;
	    need_dump=1;
	    need_restore=1;
	    need_vdump=1;
	    need_vrestore=1;
	    need_xfsdump=1;
	    need_xfsrestore=1;
	    need_vxdump=1;
	    need_vxrestore=1;
	    need_runtar=1;
	    need_gnutar=1;
	    need_compress_path=1;
	    check_disk(program, disk, level);
	} else {
	    goto err;				/* bad syntax */
	}
    }

    check_overall();

    amfree(line);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
#if defined(USE_DBMALLOC)
	extern int db_fd;

	malloc_list(db_fd, malloc_hist_1, malloc_hist_2);
#endif
    }

    dbclose();
    return 0;

 err:
    amfree(line);
    printf("ERROR [BOGUS REQUEST PACKET]\n");
    dbprintf(("REQ packet is bogus\n"));
    dbclose();
    return 1;
}


static void
check_options(program, disk, str)
     char *program, *disk, *str;
{
    int as_index = 0;

    if(strstr(str,"index") != NULL)
	as_index=1;
    if(strcmp(program,"GNUTAR") == 0) {
	need_gnutar=1;
        if(disk[0] == '/' && disk[1] == '/')
	  need_samba=1;
	else
	  need_runtar=1;
    }
    if(strcmp(program,"DUMP") == 0) {
#ifdef USE_RUNDUMP
	need_rundump=1;
#endif
#ifndef AIX_BACKUP
#ifdef VDUMP
#ifdef DUMP
	if (strcmp(amname_to_fstype(disk), "advfs") == 0)
#else
	if (1)
#endif
	{
	    need_vdump=1;
	    need_rundump=1;
	    if (as_index)
		need_vrestore=1;
	}
	else
#endif /* VDUMP */
#ifdef XFSDUMP
#ifdef DUMP
	if (strcmp(amname_to_fstype(disk), "xfs") == 0)
#else
	if (1)
#endif
	{
	    need_xfsdump=1;
	    need_rundump=1;
	    if (as_index)
		need_xfsrestore=1;
	}
	else
#endif /* XFSDUMP */
#ifdef VXDUMP
#ifdef DUMP
	if (strcmp(amname_to_fstype(disk), "vxfs") == 0)
#else
	if (1)
#endif
	{
	    need_vxdump=1;
	    if (as_index)
		need_vxrestore=1;
	}
	else
#endif /* VXDUMP */
	{
	    need_dump=1;
	    if (as_index)
		need_restore=1;
	}
#else
	/* AIX backup program */
	need_dump=1;
	if (as_index)
	    need_restore=1;
#endif
    }
    if(strstr(str, "compress") != NULL)
	need_compress_path=1;
    if(strstr(str, "index") != NULL) {
	/* do nothing */
    }
}

static void check_disk(program, disk, level)
char *program, *disk;
int level;
{
    char *device = NULL;
    char *err = NULL;
    int amode;

    if (strcmp(program, "GNUTAR") == 0) {
#ifdef SAMBA_CLIENT
        if(disk[0] == '/' && disk[1] == '/') {
	    char *cmd, *pass, *domain = NULL;

	    if ((pass = findpass(disk, &domain)) == NULL) {
		printf("ERROR [can't find password for %s]\n", disk);
		return;
	    }
	    if ((device = makesharename(disk, 1)) == NULL) {
		memset(pass, '\0', strlen(pass));
		amfree(pass);
		if(domain) {
		    memset(domain, '\0', strlen(domain));
		    amfree(domain);
		}
		printf("ERROR [can't make share name of %s]\n", disk);
		return;
	    }
	    cmd = vstralloc(SAMBA_CLIENT,
			    " ", device,
			    " \'", pass, "\'",
			    " -E",
			    " -U ", SAMBA_USER,
			    /* if domain is NULL, just quit */
			    domain ? " -W " : " -c quit",
			    domain ? domain : NULL,
			    " -c quit",
			    NULL);
	    memset(pass, '\0', strlen(pass));
	    amfree(pass);
	    printf("running %s %s XXXX -E -U %s%s%s -c quit\n",
		   SAMBA_CLIENT, device, SAMBA_USER,
		   domain ? " -W " : "", domain ? domain : "");
	    if(domain) {
		memset(domain, '\0', strlen(domain));
		amfree(domain);
	    }
	    if (system(cmd) & 0xff00)
		printf("ERROR [PC SHARE %s access error: host down or invalid password?]\n", disk);
	    else
		printf("OK %s\n", disk);
	    memset(cmd, '\0', strlen(cmd));
	    amfree(cmd);
	    return;
	}
#endif
	amode = F_OK;
	device = amname_to_dirname(disk);
    } else {
        device = amname_to_devname(disk);
#ifdef VDUMP
#ifdef DUMP
        if (strcmp(amname_to_fstype(device), "advfs") == 0) {
#else
	if (1) {
#endif
	    device = newstralloc(device, amname_to_dirname(disk));
	    amode = F_OK;
	} else
#endif
	{
	    device = stralloc(amname_to_devname(disk));
#ifdef USE_RUNDUMP
	    amode = F_OK;
#else
	    amode = R_OK;
#endif
	}
    }

    dbprintf(("checking disk %s: device %s", disk, device));

#ifndef CHECK_FOR_ACCESS_WITH_OPEN
    if(access(device, amode) == -1) {
	    err = strerror(errno);
	    printf("ERROR [can not access %s (%s): %s]\n", device, disk, err);
    } else {
	    printf("OK %s\n", disk);
    }

#else
    {
	/* XXX better check in this case */
	int tstfd;
	if((tstfd = open(device, O_RDONLY)) == -1) {
	    err = strerror(errno);
	    printf("ERROR [could not open %s (%s): %s]\n", device, disk, err);
	} else {
	    printf("OK %s\n", device);
	}
	aclose(tstfd);
    }
#endif

    dbprintf((": %s\n", err ? err : "OK"));
    amfree(device);

    /* XXX perhaps do something with level: read dumpdates and sanity check */
}

static void check_overall()
{
    char *cmd;
    struct stat buf;
    int testfd;

    if( need_runtar )
    {
	cmd = vstralloc(libexecdir, "/", "runtar", versionsuffix(), NULL);
	check_file(cmd,X_OK);
	check_suid(cmd);
	amfree(cmd);
    }

    if( need_rundump )
    {
	cmd = vstralloc(libexecdir, "/", "rundump", versionsuffix(), NULL);
	check_file(cmd,X_OK);
	check_suid(cmd);
	amfree(cmd);
    }

    if( need_dump ) {
#ifdef DUMP
	check_file(DUMP, X_OK);
#else
	printf("ERROR [DUMP program not available]\n");
#endif
    }

    if( need_restore ) {
#ifdef RESTORE
	check_file(RESTORE, X_OK);
#else
	printf("ERROR [RESTORE program not available]\n");
#endif
    }

    if ( need_vdump ) {
#ifdef VDUMP
	check_file(VDUMP, X_OK);
#else
	printf("ERROR [VDUMP program not available]\n");
#endif
    }

    if ( need_vrestore ) {
#ifdef VRESTORE
	check_file(VRESTORE, X_OK);
#else
	printf("ERROR [VRESTORE program not available]\n");
#endif
    }

    if( need_xfsdump ) {
#ifdef XFSDUMP
	check_file(XFSDUMP, F_OK);
#else
	printf("ERROR [XFSDUMP program not available]\n");
#endif
    }

    if( need_xfsrestore ) {
#ifdef XFSRESTORE
	check_file(XFSRESTORE, X_OK);
#else
	printf("ERROR [XFSRESTORE program not available]\n");
#endif
    }

    if( need_vxdump ) {
#ifdef VXDUMP
	check_file(VXDUMP, X_OK);
#else
	printf("ERROR [VXDUMP program not available]\n");
#endif
    }

    if( need_vxrestore ) {
#ifdef VXRESTORE
	check_file(VXRESTORE, X_OK);
#else
	printf("ERROR [VXRESTORE program not available]\n");
#endif
    }

    if( need_gnutar ) {
#ifdef GNUTAR
	check_file(GNUTAR, X_OK);
#else
	printf("ERROR [GNUTAR program not available]\n");
#endif
#ifdef AMANDATES_FILE
	check_file(AMANDATES_FILE, R_OK|W_OK);
#endif
#ifdef GNUTAR_LISTED_INCREMENTAL_DIR
	check_dir(GNUTAR_LISTED_INCREMENTAL_DIR,R_OK|W_OK);
#endif
    }

    if( need_samba ) {
#ifdef SAMBA_CLIENT
	check_file(SAMBA_CLIENT, X_OK);
#else
	printf("ERROR [SMBCLIENT program not available]\n");
#endif
	testfd = open("/etc/amandapass", R_OK);
	if (testfd >= 0) {
	    if(!fstat(testfd, &buf)) {
		if (buf.st_mode & 0x7)
		    printf("ERROR [/etc/amandapass is world readable!]\n");
		else
		    printf("OK [/etc/amandapass is readable, but not by all]\n");
	    } else {
		printf("OK [unable to access /etc/amandapass?]\n");
	    }
	    aclose(testfd);
	} else {
	    printf("ERROR [unable to access /etc/amandapass?]\n");
	}
    }

    if( need_compress_path )
	check_file(COMPRESS_PATH, X_OK);

    if( need_dump || need_xfsdump )
	check_file("/etc/dumpdates",
#ifdef USE_RUNDUMP
		   F_OK
#else
		   R_OK|W_OK
#endif
		   );

    if (need_vdump)
        check_file("/etc/vdumpdates", F_OK);

    check_file("/dev/null", R_OK|W_OK);
    check_space("/tmp", 64);		/* for amandad i/o */

#ifdef DEBUG_DIR
    check_space(DEBUG_DIR, 64);		/* for amandad i/o */
#endif

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

    if(mode == F_OK)
        noun = "find", adjective = "exists";
    else if((mode & X_OK) == X_OK)
	noun = "execute", adjective = "executable";
    else if((mode & (W_OK|R_OK)) == (W_OK|R_OK))
	noun = "read/write", adjective = "read/writable";
    else 
	noun = "access", adjective = "accessible";

    if(access(filename, mode) == -1)
	printf("ERROR [can not %s %s: %s]\n", noun, filename, strerror(errno));
    else
	printf("OK %s %s\n", filename, adjective);
}

static void check_dir(dirname, mode)
char *dirname;
int mode;
{
    char *dir = stralloc2(dirname, "/.");
    check_file(dir, mode);
    amfree(dir);
}

static void check_suid(filename)
char *filename;
{
    struct stat stat_buf;
    if(!stat(filename, &stat_buf)) {
	if(stat_buf.st_uid != 0 ) {
	    printf("ERROR [%s is not owned by root]\n",filename);
	}
	if((stat_buf.st_mode & S_ISUID) != S_ISUID) {
	    printf("ERROR [%s is not SUID root]\n",filename);
	}
    }
    else {
	printf("ERROR [can not stat %s]\n",filename);
    }
}
