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
 * $Id: selfcheck.c,v 1.40.2.3.4.4.2.11 2002/04/13 19:24:16 jrjackson Exp $
 *
 * do self-check and send back any error messages
 */

#include "amanda.h"
#include "clock.h"
#include "statfs.h"
#include "version.h"
#include "getfsent.h"
#include "amandates.h"
#include "util.h"
#include "pipespawn.h"
#include "features.h"
#include "client_util.h"

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

static am_feature_t *our_features = NULL;
static char *our_feature_string = NULL;
static am_feature_t *their_features = NULL;

/* local functions */
int main P((int argc, char **argv));

static void check_options P((char *program, char *disk, char *device, option_t *options));
static void check_disk P((char *program, char *disk, char *amdevice, int level));
static void check_overall P((void));
static void check_access P((char *filename, int mode));
static void check_file P((char *filename, int mode));
static void check_dir P((char *dirname, int mode));
static void check_suid P((char *filename));
static void check_space P((char *dir, long kbytes));

int main(argc, argv)
int argc;
char **argv;
{
    int level;
    char *host = NULL;
    char *line = NULL;
    char *program = NULL;
    char *disk = NULL;
    char *device = NULL;
    char *optstr = NULL;
    char *err_extra = NULL;
    char *s, *fp;
    int ch;
    int fd;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    option_t *options;

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

    safe_cd();

    set_pname("selfcheck");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);
    dbopen();
    startclock();
    dbprintf(("%s: version %s\n", argv[0], version()));

    our_features = am_init_feature_set();
    our_feature_string = am_feature_to_string(our_features);

    host = alloc(MAX_HOSTNAME_LENGTH+1);
    gethostname(host, MAX_HOSTNAME_LENGTH);
    host[MAX_HOSTNAME_LENGTH] = '\0';

    /* handle all service requests */

    for(; (line = agets(stdin)) != NULL; free(line)) {
#define sc "OPTIONS "
	if(strncmp(line, sc, sizeof(sc)-1) == 0) {
#undef sc

#define sc "features="
	    s = strstr(line, sc);
	    if(s != NULL) {
		s += sizeof(sc)-1;
#undef sc
		am_release_feature_set(their_features);
		if((their_features = am_string_to_feature(s)) == NULL) {
		    err_extra = "bad features value";
		    goto err;
		}
	    }

#define sc "hostname="
	    s = strstr(line, sc);
	    if(s != NULL) {
		s += sizeof(sc)-1;
		ch = *s++;
#undef sc
		fp = s-1;
		while(ch != '\0' && ch != ';') ch = *s++;
		s[-1] = '\0';
		host = newstralloc(host, fp);
	    }

	    printf("OPTIONS features=%s;hostname=%s;\n",
		   our_feature_string, host);
	    fflush(stdout);
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

	skip_whitespace(s, ch);                 /* find the device or level */
	if (ch == '\0') {
	    goto err;				/* no device or level */
	}
	if(!isdigit((int)s[-1])) {
	    device = s - 1;
	    skip_non_whitespace(s, ch);
	     s[-1] = '\0';			/* terminate the device */
	    skip_whitespace(s, ch);		/* find level number */
	}
	else {
	    device = stralloc(disk);
	}

						/* find level number */
	if (ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	    goto err;				/* bad level */
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
#define sc "OPTIONS "
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
	    options = parse_options(optstr, disk, device, 1);
	    check_options(program, disk, device, options);
	    check_disk(program, disk, device, level);
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
	    check_disk(program, disk, device, level);
	} else {
	    goto err;				/* bad syntax */
	}
    }

    check_overall();

    amfree(line);
    amfree(our_feature_string);
    am_release_feature_set(our_features);
    our_features = NULL;
    am_release_feature_set(their_features);
    their_features = NULL;

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
    printf("ERROR [BOGUS REQUEST PACKET]\n");
    dbprintf(("%s: REQ packet is bogus%s%s\n",
	      debug_prefix_time(NULL),
	      err_extra ? ": " : "",
	      err_extra ? err_extra : ""));
    dbclose();
    return 1;
}


static void
check_options(program, disk, device, options)
    char *program, *disk, *device;
    option_t *options;
{
    if(strcmp(program,"GNUTAR") == 0) {
	need_gnutar=1;
        if(disk[0] == '/' && disk[1] == '/') {
	    if(options->exclude_file && options->exclude_file->nb_element > 1) {
		printf("ERROR [samba support only one exclude file]\n");
	    }
	    if(options->exclude_list && options->exclude_list->nb_element > 0 &&
	       options->exclude_optional==0) {
		printf("ERROR [samba does not support exclude list]\n");
	    }
	    if(options->include_file && options->include_file->nb_element > 0) {
		printf("ERROR [samba does not support include file]\n");
	    }
	    if(options->include_list && options->include_list->nb_element > 0 &&
	       options->include_optional==0) {
		printf("ERROR [samba does not support include list]\n");
	    }
	    need_samba=1;
	}
	else {
	    int nb_exclude = 0;
	    int nb_include = 0;
	    char *file_exclude = NULL;
	    char *file_include = NULL;

	    if(options->exclude_file) nb_exclude += options->exclude_file->nb_element;
	    if(options->exclude_list) nb_exclude += options->exclude_list->nb_element;
	    if(options->include_file) nb_include += options->include_file->nb_element;
	    if(options->include_list) nb_include += options->include_list->nb_element;

	    if(nb_exclude > 0) file_exclude = build_exclude(disk, device, options, 1);
	    if(nb_include > 0) file_include = build_include(disk, device, options, 1);

	    amfree(file_exclude);
	    amfree(file_include);

	    need_runtar=1;
	}
    }
    if(strcmp(program,"DUMP") == 0) {
	if(options->exclude_file && options->exclude_file->nb_element > 0) {
	    printf("ERROR [DUMP does not support exclude file]\n");
	}
	if(options->exclude_list && options->exclude_list->nb_element > 0) {
	    printf("ERROR [DUMP does not support exclude list]\n");
	}
	if(options->include_file && options->include_file->nb_element > 0) {
	    printf("ERROR [DUMP does not support include file]\n");
	}
	if(options->include_list && options->include_list->nb_element > 0) {
	    printf("ERROR [DUMP does not support include list]\n");
	}
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
	    if (options->createindex)
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
	    if (options->createindex)
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
	    if (options->createindex)
		need_vxrestore=1;
	}
	else
#endif /* VXDUMP */
	{
	    need_dump=1;
	    if (options->createindex)
		need_restore=1;
	}
#else
	/* AIX backup program */
	need_dump=1;
	if (options->createindex)
	    need_restore=1;
#endif
    }
    if(options->compress != NO_COMPR) 
	need_compress_path=1;
}

static void check_disk(program, disk, amdevice, level)
char *program, *disk, *amdevice;
int level;
{
    char *device = NULL;
    char *err = NULL;
    char *user_and_password = NULL, *domain = NULL;
    char *share = NULL, *subdir = NULL;
    int lpass = 0;
    int amode;
    int access_result;
    char *access_type;
    char *extra_info = NULL;

    dbprintf(("%s: checking disk %s\n", debug_prefix_time(NULL), disk));

    if (strcmp(program, "GNUTAR") == 0) {
        if(amdevice[0] == '/' && amdevice[1] == '/') {
#ifdef SAMBA_CLIENT
	    int nullfd, checkerr;
	    int passwdfd;
	    char *pwtext;
	    int pwtext_len;
	    int checkpid;
	    amwait_t retstat;
	    char number[NUM_STR_SIZE];
	    int wpid;
	    int ret, sig, rc;
	    char *line;
	    char *sep;
	    FILE *ferr;
	    char *pw_fd_env;
	    int errdos;

	    parsesharename(amdevice, &share, &subdir);
	    if (!share) {
		err = stralloc2("cannot parse for share/subdir disk entry ", amdevice);
		goto common_exit;
	    }
	    if ((subdir) && (SAMBA_VERSION < 2)) {
		err = vstralloc("subdirectory specified for share '",
				amdevice,
				"' but samba not v2 or better",
				NULL);
		goto common_exit;
	    }
	    if ((user_and_password = findpass(share, &domain)) == NULL) {
		err = stralloc2("cannot find password for ", amdevice);
		goto common_exit;
	    }
	    lpass = strlen(user_and_password);
	    if ((pwtext = strchr(user_and_password, '%')) == NULL) {
		err = stralloc2("password field not \'user%pass\' for ", amdevice);
		goto common_exit;
	    }
	    *pwtext++ = '\0';
	    pwtext_len = strlen(pwtext);
	    if ((device = makesharename(share, 0)) == NULL) {
		err = stralloc2("cannot make share name of ", share);
		goto common_exit;
	    }

	    nullfd = open("/dev/null", O_RDWR);
	    if (pwtext_len > 0) {
		pw_fd_env = "PASSWD_FD";
	    } else {
		pw_fd_env = "dummy_PASSWD_FD";
	    }
	    checkpid = pipespawn(SAMBA_CLIENT, STDERR_PIPE|PASSWD_PIPE,
				 &nullfd, &nullfd, &checkerr,
				 pw_fd_env, &passwdfd,
				 "smbclient",
				 device,
				 *user_and_password ? "-U" : skip_argument,
				 *user_and_password ? user_and_password : skip_argument,
				 "-E",
				 domain ? "-W" : skip_argument,
				 domain ? domain : skip_argument,
#if SAMBA_VERSION >= 2
				 subdir ? "-D" : skip_argument,
				 subdir ? subdir : skip_argument,
#endif
				 "-c", "quit",
				 NULL);
	    if (domain) {
		memset(domain, '\0', strlen(domain));
		amfree(domain);
	    }
	    aclose(nullfd);
	    if (pwtext_len > 0 && fullwrite(passwdfd, pwtext, pwtext_len) < 0) {
		err = vstralloc("password write failed: ",
				amdevice,
				": ",
				strerror(errno),
				NULL);
		aclose(passwdfd);
		goto common_exit;
	    }
	    memset(user_and_password, '\0', lpass);
	    amfree(user_and_password);
	    aclose(passwdfd);
	    ferr = fdopen(checkerr, "r");
	    sep = "";
	    errdos = 0;
	    for(sep = ""; (line = agets(ferr)) != NULL; free(line)) {
		strappend(extra_info, sep);
		strappend(extra_info, line);
		sep = ": ";
		if(strstr(line, "ERRDOS") != NULL) {
		    errdos = 1;
		}
	    }
	    afclose(ferr);
	    checkerr = -1;
	    rc = 0;
	    while ((wpid = wait(&retstat)) != -1) {
		if (WIFSIGNALED(retstat)) {
		    ret = 0;
		    rc = sig = WTERMSIG(retstat);
		} else {
		    sig = 0;
		    rc = ret = WEXITSTATUS(retstat);
		}
		if (rc != 0) {
		    strappend(err, sep);
		    if (ret == 0) {
			strappend(err, "got signal ");
			ret = sig;
		    } else {
			strappend(err, "returned ");
		    }
		    ap_snprintf(number, sizeof(number), "%d", ret);
		    strappend(err, number);
		}
	    }
	    if (errdos != 0 || rc != 0) {
		err = newvstralloc(err,
				   "samba access error: ",
				   amdevice,
				   ": ",
				   extra_info ? extra_info : "",
				   err,
				   NULL);
		amfree(extra_info);
	    }
#else
	    err = stralloc2("This client is not configured for samba: ", amdevice);
#endif
	    goto common_exit;
	}
	amode = F_OK;
	device = amname_to_dirname(amdevice);
    } else {
	if(amdevice[0] == '/' && amdevice[1] == '/') {
	    err = vstralloc("The DUMP program cannot handle samba shares,",
			    " use GNUTAR: ",
			    amdevice,
			    NULL);
	    goto common_exit;
	}
#ifdef VDUMP								/* { */
#ifdef DUMP								/* { */
        if (strcmp(amname_to_fstype(amdevice), "advfs") == 0)
#else									/* }{ */
	if (1)
#endif									/* } */
	{
	    device = amname_to_dirname(amdevice);
	    amode = F_OK;
	} else
#endif									/* } */
	{
	    device = amname_to_devname(amdevice);
#ifdef USE_RUNDUMP
	    amode = F_OK;
#else
	    amode = R_OK;
#endif
	}
    }

    dbprintf(("%s: device %s\n", debug_prefix_time(NULL), device));

#ifdef CHECK_FOR_ACCESS_WITH_OPEN
    access_result = open(device, O_RDONLY);
    access_type = "open";
#else
    access_result = access(device, amode);
    access_type = "access";
#endif
    if(access_result == -1) {
	err = vstralloc("could not ", access_type, " ", device,
			" (", disk, "): ", strerror(errno), NULL);
    }
#ifdef CHECK_FOR_ACCESS_WITH_OPEN
    aclose(access_result);
#endif

common_exit:

    amfree(share);
    amfree(subdir);
    if(user_and_password) {
	memset(user_and_password, '\0', lpass);
	amfree(user_and_password);
    }
    if(domain) {
	memset(domain, '\0', strlen(domain));
	amfree(domain);
    }

    if(err) {
	printf("ERROR [%s]\n", err);
	dbprintf(("%s: %s\n", debug_prefix_time(NULL), err));
	amfree(err);
    } else {
	printf("OK %s\n", disk);
	dbprintf(("%s: disk \"%s\" OK\n", debug_prefix_time(NULL), disk));
	printf("OK %s\n", amdevice);
	dbprintf(("%s: amdevice \"%s\" OK\n",
		  debug_prefix_time(NULL), amdevice));
	printf("OK %s\n", device);
	dbprintf(("%s: device \"%s\" OK\n", debug_prefix_time(NULL), device));
    }
    if(extra_info) {
	dbprintf(("%s: extra info: %s\n", debug_prefix_time(NULL), extra_info));
	amfree(extra_info);
    }
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
	    if(fstat(testfd, &buf) == 0) {
		if ((buf.st_mode & 0x7) != 0) {
		    printf("ERROR [/etc/amandapass is world readable!]\n");
		} else {
		    printf("OK [/etc/amandapass is readable, but not by all]\n");
		}
	    } else {
		printf("OK [unable to stat /etc/amandapass: %s]\n",
		       strerror(errno));
	    }
	    aclose(testfd);
	} else {
	    printf("ERROR [unable to open /etc/amandapass: %s]\n",
		   strerror(errno));
	}
    }

    if( need_compress_path ) {
	check_file(COMPRESS_PATH, X_OK);
    }

    if( need_dump || need_xfsdump )
	check_file("/etc/dumpdates",
#ifdef USE_RUNDUMP
		   F_OK
#else
		   R_OK|W_OK
#endif
		   );

    if (need_vdump) {
        check_file("/etc/vdumpdates", F_OK);
    }

    check_access("/dev/null", R_OK|W_OK);
    check_space(AMANDA_TMPDIR, 64);	/* for amandad i/o */

#ifdef AMANDA_DBGDIR
    check_space(AMANDA_DBGDIR, 64);	/* for amandad i/o */
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

static void check_access(filename, mode)
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

static void check_file(filename, mode)
char *filename;
int mode;
{
    struct stat stat_buf;
    if(!stat(filename, &stat_buf)) {
	if(!S_ISREG(stat_buf.st_mode)) {
	    printf("ERROR [%s is not a file]\n", filename);
	}
    }
    check_access(filename, mode);
}

static void check_dir(dirname, mode)
char *dirname;
int mode;
{
    struct stat stat_buf;
    char *dir;

    if(!stat(dirname, &stat_buf)) {
	if(!S_ISDIR(stat_buf.st_mode)) {
	    printf("ERROR [%s is not a directory]\n", dirname);
	}
    }
    dir = stralloc2(dirname, "/.");
    check_access(dir, mode);
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
