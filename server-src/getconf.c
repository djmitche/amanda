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
 * $Id: getconf.c,v 1.8.4.4 2001/09/17 22:17:54 jrjackson Exp $
 *
 * a little wrapper to extract config variables for shell scripts
 */
#include "amanda.h"
#include "version.h"
#include "genversion.h"
#include "conffile.h"

int main P((int argc, char **argv));

char *config_name = NULL;
char *config_dir = NULL;

static struct build_info {
    char *symbol;
    char *value;
} build_info[] = {
    { "VERSION",			"" },	/* must be [0] */
    { "AMANDA_DEBUG_DAYS",		"" },	/* must be [1] */
    { "TICKET_LIFETIME",		"" },	/* must be [2] */

    { "bindir",				bindir },
    { "sbindir",			sbindir },
    { "libexecdir",			libexecdir },
    { "mandir",				mandir },
    { "AMANDA_TMPDIR",			AMANDA_TMPDIR },
    { "CONFIG_DIR",			CONFIG_DIR },
    { "MAILER",				MAILER },
    { "DEFAULT_SERVER",			DEFAULT_SERVER },
    { "DEFAULT_CONFIG",			DEFAULT_CONFIG },
    { "DEFAULT_TAPE_SERVER",		DEFAULT_TAPE_SERVER },
    { "DEFAULT_TAPE_DEVICE",		DEFAULT_TAPE_SERVER },

    { "BUILT_DATE",
#if defined(BUILT_DATE)
	BUILT_DATE
#endif
    },
    { "BUILT_MACH",
#if defined(BUILT_MACH)
	BUILT_MACH
#endif
    },
    { "CC",
#if defined(CC)
	CC
#endif
    },

    { "AMANDA_DBGDIR",
#if defined(AMANDA_DBGDIR)
	AMANDA_DBGDIR
#endif
    },
    { "DEV_PREFIX",
#if defined(DEV_PREFIX)
	DEV_PREFIX
#endif
    },
    { "RDEV_PREFIX",
#if defined(RDEV_PREFIX)
	RDEV_PREFIX },
#endif
    { "DUMP",
#if defined(DUMP)
	DUMP
#endif
    },
    { "RESTORE",
#if defined(DUMP)
	RESTORE
#endif
    },
    { "VDUMP",
#if defined(VDUMP)
	VDUMP
#endif
    },
    { "VRESTORE",
#if defined(VDUMP)
	VRESTORE
#endif
    },
    { "XFSDUMP",
#if defined(XFSDUMP)
	XFSDUMP
#endif
    },
    { "XFSRESTORE",
#if defined(XFSDUMP)
	XFSRESTORE
#endif
    },
    { "VXDUMP",
#if defined(VXDUMP)
	VXDUMP
#endif
    },
    { "VXRESTORE",
#if defined(VXDUMP)
	VXRESTORE
#endif
    },
    { "SAMBA_CLIENT",
#if defined(SAMBA_CLIENT)
	SAMBA_CLIENT
#endif
    },
    { "GNUTAR",
#if defined(GNUTAR)
	GNUTAR
#endif
    },
    { "COMPRESS_PATH",
#if defined(COMPRESS_PATH)
	COMPRESS_PATH
#endif
    },
    { "UNCOMPRESS_PATH",
#if defined(UNCOMPRESS_PATH)
	UNCOMPRESS_PATH
#endif
    },
    { "listed_incr_dir",
#if defined(GNUTAR_LISTED_INCREMENTAL_DIR)
	GNUTAR_LISTED_INCREMENTAL_DIR
#endif
    },
    { "GNUTAR_LISTED_INCREMENTAL_DIR",
#if defined(GNUTAR_LISTED_INCREMENTAL_DIR)
	GNUTAR_LISTED_INCREMENTAL_DIR
#endif
    },

    { "AIX_BACKUP",
#if defined(AIX_BACKUP)
	"1"
#endif
    },
    { "AIX_TAPEIO",
#if defined(AIX_TAPEIO)
	"1"
#endif
    },
    { "DUMP_RETURNS_1",
#if defined(DUMP_RETURNS_1)
	"1"
#endif
    },

    { "LOCKING",
#if defined(USE_POSIX_FCNTL)
	"POSIX_FCNTL"
#elif defined(USE_FLOCK)
	"FLOCK"
#elif defined(USE_LOCKF)
	"LOCKF"
#elif defined(USE_LNLOCK)
	"LNLOCK"
#else
	"NONE"
#endif
    },

    { "STATFS_BSD",
#if defined(STATFS_BSD)
	"1"
#endif
    },
    { "STATFS_OSF1",
#if defined(STATFS_OSF1)
	"1"
#endif
    },
    { "STATFS_ULTRIX",
#if defined(STATFS_ULTRIX)
	"1"
#endif
    },
    { "ASSERTIONS",
#if defined(ASSERTIONS)
	"1"
#endif
    },
    { "DEBUG_CODE",
#if defined(DEBUG_CODE)
	"1"
#endif
    },
    { "BSD_SECURITY",
#if defined(BSD_SECURITY)
	"1"
#endif
    },
    { "USE_AMANDAHOSTS",
#if defined(USE_AMANDAHOSTS)
	"1"
#endif
    },
    { "USE_RUNDUMP",
#if defined(USE_RUNDUMP)
	"1"
#endif
    },
    { "FORCE_USERID",
#if defined(FORCE_USERID)
	"1"
#endif
    },
    { "USE_VERSION_SUFFIXES",
#if defined(USE_VERSION_SUFFIXES)
	"1"
#endif
    },
    { "HAVE_GZIP",
#if defined(HAVE_GZIP)
	"1"
#endif
    },

    { "KRB4_SECURITY",
#if defined(KRB4_SECURITY)
	"1"
#endif
    },
    { "SERVER_HOST_PRINCIPLE",
#if defined(KRB4_SECURITY)
	SERVER_HOST_PRINCIPLE
#endif
    },
    { "SERVER_HOST_INSTANCE",
#if defined(KRB4_SECURITY)
	SERVER_HOST_INSTANCE
#endif
    },
    { "SERVER_HOST_KEY_FILE",
#if defined(KRB4_SECURITY)
	SERVER_HOST_KEY_FILE
#endif
    },
    { "CLIENT_HOST_PRINCIPLE",
#if defined(KRB4_SECURITY)
	CLIENT_HOST_PRINCIPLE
#endif
    },
    { "CLIENT_HOST_INSTANCE",
#if defined(KRB4_SECURITY)
	CLIENT_HOST_INSTANCE
#endif
    },
    { "CLIENT_HOST_KEY_FILE",
#if defined(KRB4_SECURITY)
	CLIENT_HOST_KEY_FILE
#endif
    },

    { "COMPRESS_SUFFIX",
#if defined(COMPRESS_SUFFIX)
	COMPRESS_SUFFIX
#endif
    },
    { "COMPRESS_FAST_OPT",
#if defined(COMPRESS_FAST_OPT)
	COMPRESS_FAST_OPT
#endif
    },
    { "COMPRESS_BEST_OPT",
#if defined(COMPRESS_BEST_OPT)
	COMPRESS_BEST_OPT
#endif
    },
    { "UNCOMPRESS_OPT",
#if defined(UNCOMPRESS_OPT)
	UNCOMPRESS_OPT
#endif
    },

    { NULL,			NULL }
};

int main(argc, argv)
int argc;
char **argv;
{
    char *result;
    int fd;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    char *pgm;
    char *conffile;
    char *parmname;
    int i;
    char number[NUM_STR_SIZE];

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    set_pname("amgetconf");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    if((pgm = strrchr(argv[0], '/')) == NULL) {
	pgm = argv[0];
    } else {
	pgm++;
    }

    if(argc < 2) {
	fprintf(stderr, "Usage: %s [config] <parmname>\n", pgm);
	exit(1);
    }

    if (argc > 2) {
	config_name = stralloc(argv[1]);
	config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
	parmname = argv[2];
    } else {
	char my_cwd[STR_SIZE];

	if (getcwd(my_cwd, sizeof(my_cwd)) == NULL) {
	    error("cannot determine current working directory");
	}
	config_dir = stralloc2(my_cwd, "/");
	if ((config_name = strrchr(my_cwd, '/')) != NULL) {
	    config_name = stralloc(config_name + 1);
	}
	parmname = argv[1];
    }

    safe_cd();

    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if(read_conffile(conffile)) {
	error("errors processing config file \"%s\"", conffile);
    }

    /*
     * Fill in the build values that need runtime help.
     */
    build_info[0].value = stralloc(version());
#if defined(AMANDA_DEBUG_DAYS)
    i = AMANDA_DEBUG_DAYS;
#else
    i = -1;
#endif
    ap_snprintf(number, sizeof(number), "%ld", (long)i);
    build_info[1].value = stralloc(number);
#if defined(KRB4_SECURITY)
    i = TICKET_LIFETIME;
#else
    i = -1;
#endif
    ap_snprintf(number, sizeof(number), "%ld", (long)i);
    build_info[2].value = stralloc(number);

#undef p
#define	p	"build."

    if(strncmp(parmname, p, sizeof(p) - 1) == 0) {
	char *s;
	char *t;

	t = stralloc(parmname + sizeof(p) - 1);
	for(i = 0; (s = build_info[i].symbol) != NULL; i++) {
	    if(strcasecmp(s, t) == 0) {
		break;
	    }
	}
	if(s == NULL) {
	    result = NULL;
	} else {
	    result = build_info[i].value;
	    result = stralloc(result ? result : "");
	}

#undef p
#define	p	"dbopen."

    } else if(strncmp(parmname, p, sizeof(p) - 1) == 0) {
	char *pname;
	char *dbname;

	if((pname = strrchr(parmname + sizeof(p) - 1, '/')) == NULL) {
	    pname = parmname + sizeof(p) - 1;
	} else {
	    pname++;
	}
	set_pname(pname);
	dbopen();
	if((dbname = dbfn()) == NULL) {
	    result = stralloc("/dev/null");
	} else {
	    result = stralloc(dbname);
	}

#undef p
#define	p	"dbclose."

    } else if(strncmp(parmname, p, sizeof(p) - 1) == 0) {
	char *t;
	char *pname;
	char *dbname;
	int old_fd2;
	int new_fd2;

	t = stralloc(parmname + sizeof(p) - 1);
	if((dbname = strchr(t, ':')) == NULL) {
	    error("%s: cannot parse %s", pgm, parmname);
	}
	*dbname++ = '\0';
	if((pname = strrchr(t, '/')) == NULL) {
	    pname = t;
	} else {
	    pname++;
	}
	fflush(stderr);
	if((old_fd2 = dup(2)) < 0) {
	    error("%s: cannot dup2 fd2", pgm);
	}
	close(2);
	if((new_fd2 = open(dbname, O_RDWR|O_APPEND, 0600)) != 2) {
	    int save_errno = errno;
	    FILE *f;

	    if((f = fdopen(old_fd2, "a")) == NULL) {
		/* give up */
		return 3;
	    }
	    fprintf(f, "%s: cannot open %s: ", pgm, dbname);
	    if(new_fd2 < 0) {
		fputs(strerror(save_errno), f);
	    } else {
		fprintf(f, "got %d instead of 2", new_fd2);
	    }
	    fputc('\n', f);
	    fclose(f);
	}
	/*
	 * Because we have not called dbopen(), dbclose() will write the
	 * end line to stderr, which we just redirected to the file on
	 * the command line.
	 */
	set_pname(pname);
	dbclose();
	result = stralloc(dbname);
	amfree(t);

    } else {
	result = getconf_byname(parmname);
    }
    if(result == NULL) {
	result = stralloc("BUGGY");
	fprintf(stderr, "%s: no such parameter \"%s\"\n", pgm, parmname);
	fflush(stderr);
    }

    puts(result);

    amfree(result);
    amfree(config_dir);
    amfree(config_name);
    for(i = 0; i < 3; i++) {
	amfree(build_info[i].value);
    }

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    return 0;
}
