/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1996 University of Maryland at College Park
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
 * $Id: calcsize.c,v 1.22 1998/04/08 16:24:21 amcore Exp $
 *
 * traverse directory tree to get backup size estimates
 */
#include "amanda.h"
#include "statfs.h"

#define ROUND(n,x)	(x + (x % n) ? (n - x % n) : 0)

/*
static unsigned long round_function(n, x)
unsigned long n, x;
{
  unsigned long remainder = x % n;
  if (remainder)
    x += n-remainder;
  return x;
}
*/

# define ST_BLOCKS(s)	((s).st_size / 512 + (((s).st_size % 512) ? 1 : 0))

#define	FILETYPES	(S_IFREG|S_IFLNK|S_IFDIR)

typedef struct name_s {
    struct name_s *next;
    char *str;
} Name;

Name *name_stack;

#define MAXDUMPS 10

struct {
    int max_inode;
    int total_dirs;
    int total_files;
    long total_size;
} dumpstats[MAXDUMPS];

time_t dumpdate[MAXDUMPS];
int  dumplevel[MAXDUMPS];
int ndumps;

void (*add_file) P((int, struct stat *));
long (*final_size) P((int, char *));


int main P((int, char **));
void traverse_dirs P((char *));


void add_file_dump P((int, struct stat *));
long final_size_dump P((int, char *));

void add_file_gnutar P((int, struct stat *));
long final_size_gnutar P((int, char *));

void add_file_unknown P((int, struct stat *));
long final_size_unknown P((int, char *));

#ifdef BUILTIN_EXCLUDE_SUPPORT
int use_gtar_excl = 0;
char exclude_string[] = "--exclude=";
char exclude_list_string[] = "--exclude-list=";
#endif

int main(argc, argv)
int argc;
char **argv;
{
#ifdef TEST
/* standalone test to ckeck wether the calculated file size is ok */
    struct stat finfo;
    int i;
    unsigned long dump_total=0, gtar_total=0;
    char *d;
    int l, w;
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

    set_pname("calcsize");

    if (argc < 2) {
	fprintf(stderr,"Usage: %s file[s]\n",argv[0]);
	return 1;
    }
    for(i=1; i<argc; i++) {
	if(lstat(argv[i], &finfo) == -1) {
	    fprintf(stderr, "%s: %s\n", argv[i], strerror(errno));
	    continue;
	}
	printf("%s: st_size=%lu", argv[i],(unsigned long)finfo.st_size);
	printf(": blocks=%lu\n", (unsigned long)ST_BLOCKS(finfo));
	dump_total += (ST_BLOCKS(finfo) + 1)/2 + 1;
	gtar_total += ROUND(4,(ST_BLOCKS(finfo) + 1));
    }
    printf("           gtar           dump\n");
    printf("total      %-9lu         %-9lu\n",gtar_total,dump_total);
    return 0;
#else
    int i;
    char *dirname, *amname;
    int fd;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

#if 0
    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);
#endif

    argc--, argv++;	/* skip program name */

    /* need at least program, amname, and directory name */

    if(argc < 2) {
#ifdef BUILTIN_EXCLUDE_SUPPORT
      usage:
#endif
	error("Usage: %s [DUMP|GNUTAR%s] name dir [level date] ...",
	      get_pname(),
#ifdef BUILTIN_EXCLUDE_SUPPORT
	      " [-X --exclude[-list]=regexp]"
#else
	      ""
#endif
	      );
	return 1;
    }

    /* parse backup program name */

    if(strcmp(*argv, "DUMP") == 0) {
#if !defined(DUMP) && !defined(XFSDUMP)
	error("%s: dump not available on this system", get_pname());
	return 1;
#else
	add_file = add_file_dump;
	final_size = final_size_dump;
#endif
    }
    else if(strcmp(*argv, "GNUTAR") == 0) {
#ifndef GNUTAR
	error("%s: gnutar not available on this system", get_pname());
	return 1;
#else
	add_file = add_file_gnutar;
	final_size = final_size_gnutar;
#ifdef BUILTIN_EXCLUDE_SUPPORT
	use_gtar_excl++;
#endif
#endif
    }
    else {
	add_file = add_file_unknown;
	final_size = final_size_unknown;
    }
    argc--, argv++;
#ifdef BUILTIN_EXCLUDE_SUPPORT
    if ((argc > 1) && strcmp(*argv,"-X") == 0) {
	char *result = NULL;
	char *cp = NULL;
	argv++;

	if (!use_gtar_excl) {
	  error("%s: exclusion specification not supported", get_pname());
	  return 1;
	}

	result = stralloc(*argv);
	if (*result && (cp = strrchr(result,';')))
	    /* delete trailing ; */
	    *cp = 0;
	if (strncmp(result, exclude_string, sizeof(exclude_string)-1) == 0)
	  add_exclude(result+sizeof(exclude_string)-1);
	else if (strncmp(result, exclude_list_string,
			 sizeof(exclude_list_string)-1) == 0) {
	  if (access(result + sizeof(exclude_list_string)-1, R_OK) != 0) {
	    fprintf(stderr,"Cannot open exclude file %s\n",cp+1);
	    use_gtar_excl = 0;
	  } else {
	    add_exclude_file(result + sizeof(exclude_list_string)-1);
	  }
	} else {
	  amfree(result);
	  goto usage;
	}
	amfree(result);
	argc -= 2;
	argv++;
    } else
	use_gtar_excl = 0;
#endif

    /* the amanda name can be different from the directory name */

    amname = *argv;
    argc--, argv++;

    /* the toplevel directory name to search from */
    dirname = *argv;
    argc--, argv++;

    /* the dump levels to calculate sizes for */

    ndumps = 0;
    while(argc >= 2) {
	if(ndumps < MAXDUMPS) {
	    dumplevel[ndumps] = atoi(argv[0]);
	    dumpdate [ndumps] = (time_t) atol(argv[1]);
	    ndumps++;
	    argc -= 2, argv += 2;
	}
    }

    if(argc)
	error("leftover arg \"%s\", expected <level> and <date>", *argv);

    traverse_dirs(dirname);
    for(i = 0; i < ndumps; i++) {

	amflock(1, "size");

	lseek(1, (off_t)0, SEEK_END);

	printf("%s %d SIZE %ld\n",
	       amname, dumplevel[i], final_size(i, dirname));
	fflush(stdout);

	amfunlock(1, "size");
    }

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    return 0;
#endif
}

/*
 * =========================================================================
 */

#ifndef HAVE_BASENAME
char *basename(file)
char *file;
{
    char *cp;

    if ( (cp = strrchr(file,'/')) )
	return cp+1;
    return file;
}
#endif

void push_name P((char *str));
char *pop_name P((void));

void traverse_dirs(parent_dir)
char *parent_dir;
{
    DIR *d;
    struct dirent *f;
    struct stat finfo;
    char *dirname, *newdir;
    char *newname = NULL;
    dev_t parent_dev = 0;
    int i;
    int l;

    if(parent_dir && chdir(parent_dir) != -1 && stat(".", &finfo) != -1)
	parent_dev = finfo.st_dev;

    push_name(parent_dir);

    for(dirname = pop_name(); dirname; free(dirname), dirname = pop_name()) {

#ifdef BUILTIN_EXCLUDE_SUPPORT
	if(use_gtar_excl &&
	   (check_exclude(basename(dirname)) ||
	    check_exclude(dirname)))
	    /* will not be added by gnutar */
	    continue;
#endif

	if(chdir(dirname) == -1 || (d = opendir(".")) == NULL) {
	    perror(dirname);
	    continue;
	}

	l = strlen(dirname);
	if(l > 0 && dirname[l - 1] != '/') {
	    newname = newstralloc2(newname, dirname, "/");
	} else {
	    newname = newstralloc(newname, dirname);
	}

	while((f = readdir(d)) != NULL) {
	    if(f->d_name[0] == '.' && f->d_name[1] == '\0')
		continue;
	    if(f->d_name[0] == '.' && f->d_name[1] == '.' &&
	       f->d_name[2] == '\0')
		continue;

	    if(lstat(f->d_name, &finfo) == -1) {
		fprintf(stderr, "%s/%s: %s\n",
			dirname, f->d_name, strerror(errno));
		continue;
	    }

	    if(finfo.st_dev != parent_dev)
		continue;

	    if((finfo.st_mode & S_IFMT) == S_IFDIR) {
		newdir = stralloc2(newname, f->d_name);
		push_name(newdir);
		amfree(newdir);
	    }

	    for(i = 0; i < ndumps; i++) {
		if(finfo.st_ctime >= dumpdate[i])
		    if (
#ifdef BUILTIN_EXCLUDE_SUPPORT
			!check_exclude(f->d_name) &&
#endif
			/* regular files */
			((finfo.st_mode & S_IFMT) == S_IFREG)
			  /* directories */
			  || ((finfo.st_mode & S_IFMT) == S_IFDIR)
#ifdef S_IFLNK
			  /* symlinks */
			  || ((finfo.st_mode & S_IFMT) == S_IFLNK)
#endif
			) add_file(i, &finfo);
	    }
	}

#ifdef CLOSEDIR_VOID
	closedir(d);
#else
	if(closedir(d) == -1)
	    perror(dirname);
#endif
    }
    amfree(newname);
}

void push_name(str)
char *str;
{
    Name *newp;

    newp = alloc(sizeof(*newp));
    newp->str = stralloc(str);

    newp->next = name_stack;
    name_stack = newp;
}

char *pop_name()
{
    Name *newp = name_stack;
    char *str;

    if(!newp) return NULL;

    name_stack = newp->next;
    str = newp->str;
    amfree(newp);
    return str;
}


/*
 * =========================================================================
 * Backup size calculations for DUMP program
 *
 * Given the system-dependent nature of dump, it's impossible to pin this
 * down accurately.  Luckily, that's not necessary.
 *
 * Dump rounds each file up to TP_BSIZE bytes, which is 1k in the BSD dump,
 * others are unknown.  In addition, dump stores three bitmaps at the
 * beginning of the dump: a used inode map, a dumped dir map, and a dumped
 * inode map.  These are sized by the number of inodes in the filesystem.
 *
 * We don't take into account the complexities of BSD dump's indirect block
 * requirements for files with holes, nor the dumping of directories that
 * are not themselves modified.
 */
void add_file_dump(level, sp)
int level;
struct stat *sp;
{
    /* keep the size in kbytes, rounded up, plus a 1k header block */
    dumpstats[level].total_size += (ST_BLOCKS(*sp) + 1)/2 + 1;
}

long final_size_dump(level, topdir)
int level;
char *topdir;
{
    generic_fs_stats_t stats;
    int mapsize;

    /* calculate the map sizes */

    if(chdir(topdir) == -1 || get_fs_stats(".", &stats) == -1)
	error("statfs %s: %s", topdir, strerror(errno));

    mapsize = (stats.files + 7) / 8;	/* in bytes */
    mapsize = (mapsize + 1023) / 1024;  /* in kbytes */

    /* the dump contains three maps plus the files */

    return 3*mapsize + dumpstats[level].total_size;
}

/*
 * =========================================================================
 * Backup size calculations for GNUTAR program
 *
 * Gnutar's basic blocksize is 512 bytes.  Each file is rounded up to that
 * size, plus one header block.  Gnutar stores directories' file lists in
 * incremental dumps - we'll pick up size of the modified dirs here.  These
 * will be larger than a simple filelist of their contents, but that's ok.
 *
 * As with DUMP, we only need a reasonable estimate, not an exact figure.
 */
void add_file_gnutar(level, sp)
int level;
struct stat *sp;
{
    /* the header takes one additional block */
    dumpstats[level].total_size += ROUND(4,(ST_BLOCKS(*sp) + 1));
}

long final_size_gnutar(level, topdir)
int level;
char *topdir;
{
    /* divide by two to get kbytes, rounded up */
    /* + 4 blocks for security */
    return (dumpstats[level].total_size + 5) / 2;
}

/*
 * =========================================================================
 * Backup size calculations for unknown backup programs.
 *
 * Here we'll just add up the file sizes and output that.
 */

void add_file_unknown(level, sp)
int level;
struct stat *sp;
{
    /* just add up the block counts */
    dumpstats[level].total_size += ST_BLOCKS(*sp);
}

long final_size_unknown(level, topdir)
int level;
char *topdir;
{
    /* divide by two to get kbytes, rounded up */
    return (dumpstats[level].total_size + 1) / 2;
}
