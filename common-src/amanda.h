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
 * $Id: amanda.h,v 1.64.2.1 1998/10/31 06:19:34 oliva Exp $
 *
 * the central header file included by all amanda sources
 */
#ifndef AMANDA_H
#define AMANDA_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*
 * I would prefer that each Amanda module include only those system headers
 * that are locally needed, but on most Unixes the system header files are not
 * protected against multiple inclusion, so this can lead to problems.
 *
 * Also, some systems put key files in different places, so by including 
 * everything here the rest of the system is isolated from such things.
 */
#ifdef HAVE_ALLOCA_H
#  include <alloca.h>
#endif

#if 0
/* an explanation for this is available in the CHANGES file for
   amanda-2.4.0b5 */
#ifdef HAVE_ASM_BYTEORDER_H
#  include <asm/byteorder.h>
#endif
#endif

#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

/* from the autoconf documentation */
#ifdef HAVE_DIRENT_H
#  include <dirent.h>
#  define NAMLEN(dirent) strlen((dirent)->d_name)
#else
#  define dirent direct
#  define NAMLEN(dirent) (dirent)->d_namlen
#  if HAVE_SYS_NDIR_H
#    include <sys/ndir.h>
#  endif
#  if HAVE_SYS_DIR_H
#    include <sys/dir.h>
#  endif
#  if HAVE_NDIR_H
#    include <ndir.h>
#  endif
#endif

#ifdef HAVE_FCNTL_H
#  include <fcntl.h>
#endif

#ifdef HAVE_GRP_H
#  include <grp.h>
#endif

#if defined(USE_DB_H)
#  include <db.h>
#else
#if defined(USE_DBM_H)
#  include <dbm.h>
#else
#if defined(USE_GDBM_H)
#  include <gdbm.h>
#else
#if defined(USE_NDBM_H)
#  include <ndbm.h>
#endif
#endif
#endif
#endif

#ifdef HAVE_NETDB_H
#  include <netdb.h>
#endif

#ifdef TIME_WITH_SYS_TIME
#  include <sys/time.h>
#  include <time.h>
#else
#  ifdef HAVE_SYS_TIME_H
#    include <sys/time.h>
#  else
#    include <time.h>
#  endif
#endif

#ifdef HAVE_LIBC_H
#  include <libc.h>
#endif

#ifdef HAVE_STDLIB_H
#  include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#  include <string.h>
#endif

#ifdef HAVE_SYSLOG_H
#  include <syslog.h>
#endif

#ifdef HAVE_SYS_FILE_H
#  include <sys/file.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#  include <sys/ioctl.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#  include <sys/param.h>
#endif

#if defined(HAVE_SYS_IPC_H) && defined(HAVE_SYS_SHM_H)
#  include <sys/ipc.h>
#  include <sys/shm.h>
#else
#  ifdef HAVE_SYS_MMAN_H
#    include <sys/mman.h>
#  endif
#endif

#ifdef HAVE_SYS_SELECT_H
#  include <sys/select.h>
#endif

#ifdef HAVE_SYS_STAT_H
#  include <sys/stat.h>
#endif

#ifdef HAVE_WAIT_H
#  include <wait.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#  include <sys/wait.h>
#endif

#ifdef WAIT_USES_UNION
  typedef union wait amwait_t;
# ifndef WEXITSTATUS
#  define WEXITSTATUS(stat_val) (((amwait_t*)&(stat_val))->w_retcode)
# endif
# ifndef WTERMSIG
#  define WTERMSIG(stat_val) (((amwait_t*)&(stat_val))->w_termsig)
# endif
# ifndef WIFEXITED
#  define WIFEXITED(stat_val) (WTERMSIG(stat_val) == 0)
# endif
#else
  typedef int amwait_t;
# ifndef WEXITSTATUS
#  define WEXITSTATUS(stat_val) (*(unsigned*)&(stat_val) >> 8)
# endif
# ifndef WTERMSIG
#  define WTERMSIG(stat_val) (*(unsigned*)&(stat_val) & 0x7F)
# endif
# ifndef WIFEXITED
#  define WIFEXITED(stat_val) ((*(unsigned*)&(stat_val) & 255) == 0)
# endif
#endif

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

/*
 * At present, the kerberos routines require atexit(), or equivilent.  If 
 * you're not using kerberos, you don't need it at all.  If you just null
 * out the definition, you'll end up with ticket files hanging around in
 * /tmp.
 */
#if defined(KRB4_SECURITY)
#   if !defined(HAVE_ATEXIT) 
#      if defined(HAVE_ON_EXIT)
#          define atexit(func) on_exit(func, 0)
#      else
#	   define atexit(func) (you must to resolve lack of atexit in amanda.h)
#      endif
#   endif
#endif

#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <pwd.h>
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/socket.h>

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef KRB4_SECURITY
#  include <des.h>
#  include <krb.h>
#endif

/*
 * The dbmalloc package comes from:
 *
 *  http://www.clark.net/pub/dickey/dbmalloc/dbmalloc.tar.gz
 *
 * or
 *
 *  ftp://gatekeeper.dec.com/pub/usenet/comp.sources.misc/volume32/dbmalloc/
 *
 * The following functions are sprinkled through the code, but are
 * disabled unless USE_DBMALLOC is defined:
 *
 *  malloc_enter(char *) -- stack trace for malloc reports
 *  malloc_leave(char *) -- stack trace for malloc reports
 *  malloc_leave(void *) -- mark an area as never to be free-d
 *  malloc_chain_check(void) -- check the malloc area now
 *  malloc_dump(int fd) -- report the malloc contents to a file descriptor
 *  malloc_list(int fd, ulong a, ulong b) -- report memory activated since
 *	history stamp a that is still active as of stamp b (leak check)
 *  malloc_inuse(ulong *h) -- create history stamp h and return the amount
 *	of memory currently in use.
 */

#ifdef USE_DBMALLOC
#include "dbmalloc.h"
#else
#define	malloc_enter(func)
#define	malloc_leave(func)
#define	malloc_mark(ptr)
#define	malloc_chain_check()
#define	malloc_dump(fd)
#define	malloc_list(a,b,c)
#define	malloc_inuse(hist)		(*(hist) = 0, 0)
#define	dbmalloc_caller_loc(x,y)	(x)
#endif

#if !defined(HAVE_SIGACTION) && defined(HAVE_SIGVEC)
/* quick'n'dirty hack for NextStep31 */
#  define sa_flags sv_flags
#  define sa_handler sv_handler
#  define sa_mask sv_mask
#  define sigaction sigvec
#  define sigemptyset(mask) /* no way to clear pending signals */
#endif

/*
 * Most Unixen declare errno in <errno.h>, some don't.  Some multithreaded
 * systems have errno as a per-thread macro.  So, we have to be careful.
 */
#ifndef errno
extern int errno;
#endif


/*
 * Some older BSD systems don't have these FD_ macros, so if not, provide them.
 */
#ifndef FD_SET
#  define FD_SETSIZE      (sizeof(fd_set) * 8)
#  define FD_SET(n, p)    (((fd_set *) (p))->fds_bits[0] |= (1 << ((n) % 32)))
#  define FD_CLR(n, p)    (((fd_set *) (p))->fds_bits[0] &= ~(1 << ((n) % 32)))
#  define FD_ISSET(n, p)  (((fd_set *) (p))->fds_bits[0] & (1 << ((n) % 32)))
#  define FD_ZERO(p)      memset((p), 0, sizeof(*(p)))
#endif


/*
 * Define MAX_HOSTNAME_LENGTH as the size of arrays to hold hostname's.
 */
#undef  MAX_HOSTNAME_LENGTH
#define MAX_HOSTNAME_LENGTH 1025

/*
 * If void is broken, substitute char.
 */
#ifdef BROKEN_VOID
#  define void char
#endif

/*
 * define prototype macro so that prototypes can be declared in both ANSI
 * and classic C environments.
 */
#if STDC_HEADERS
#  define P(parms)	parms
#  define stringize(x) #x
#else
#  define P(parms)	()
#  define stringize(x) "x"
#endif

/*
 * So that we can use GNUC attributes (such as to get -Wall warnings
 * for printf-like functions).  Only do this in gcc 2.7 or later ...
 * it may work on earlier stuff, but why chance it.
 */
#if !defined(__GNUC__) || __GNUC__ < 2 || __GNUC_MINOR__ < 7
#define __attribute__(__x)
#endif

/*
 * assertions, but call error() instead of abort 
 */
#ifndef ASSERTIONS

#define assert(exp) ((void)0)

#else	/* ASSERTIONS */

#define assert(exp) {if(!(exp)) error("assert: %s false, file %s, line %d", \
				   stringize(exp), __FILE__, __LINE__);}

#endif	/* ASSERTIONS */

/*
 * print debug output, else compile to nothing.
 */

#ifdef DEBUG_CODE
#   define dbopen()    debug_open()
#   define dbclose()    debug_close()
#   define dbprintf(p)  (debug? (debug_printf p, 0) : 0)
#   define dbfd()    debug_fd()

extern void debug_open P((void));
extern void debug_close P((void));
extern void debug_printf P((char *format, ...))
    __attribute__ ((format (printf, 1, 2)));
extern int  debug_fd P((void));
#else
#   define dbopen()
#   define dbclose()
#   define dbprintf(p)
#   define dbfd()    (-1)
#endif

/* amanda #days calculation, with roundoff */

#define SECS_PER_DAY	(24*60*60)
#define days_diff(a, b)	(((b) - (a) + SECS_PER_DAY/2) / SECS_PER_DAY)

/* Global constants.  */
#ifndef AMANDA_SERVICE_NAME
#define AMANDA_SERVICE_NAME "amanda"
#endif
#ifndef KAMANDA_SERVICE_NAME
#define KAMANDA_SERVICE_NAME "kamanda"
#endif
#ifndef SERVICE_SUFFIX
#define SERVICE_SUFFIX ""
#endif
#ifndef AMANDA_SERVICE_DEFAULT
#define AMANDA_SERVICE_DEFAULT	10080
#endif
#ifndef KAMANDA_SERVICE_DEFAULT
#define KAMANDA_SERVICE_DEFAULT	10081
#endif

/* Size of a tape block in kbytes.  Do not change lightly.  */
#define TAPE_BLOCK_SIZE 32
#define TAPE_BLOCK_BYTES (TAPE_BLOCK_SIZE*1024)

/* Define miscellaneous amanda functions.  */
#define ERR_INTERACTIVE	1
#define ERR_SYSLOG	2
#define ERR_AMANDALOG	4

/* For static buffer manager [alloc.c:sbuf_man()] */
#define SBUF_MAGIC 42
#define SBUF_DEF(name, len) static SBUF2_DEF(len) name = {SBUF_MAGIC, len, -1}
#define SBUF2_DEF(len) 		\
    struct {			\
	int magic;		\
	int max, cur;		\
	void *bufp[len];	\
    }

extern void   set_logerror P((void (*f)(char *)));
extern void   set_pname P((char *pname));
extern char  *get_pname P((void));
extern int    erroutput_type;
extern void   error     P((char *format, ...))
    __attribute__ ((format (printf, 1, 2)));
extern int    onerror         P((void (*errf)(void)));

#if defined(USE_DBMALLOC)
extern void  *debug_alloc           P((char *c, int l, int size));
extern void  *debug_newalloc        P((char *c, int l, void *old, int size));
extern char  *debug_stralloc        P((char *c, int l, char *str));
extern char  *debug_newstralloc     P((char *c, int l, char *oldstr, char *newstr));
extern char  *dbmalloc_caller_loc   P((char *file, int line));
extern int   debug_alloc_push	    P((char *file, int line));
extern void  debug_alloc_pop	    P((void));

#define	alloc(s)		debug_alloc(__FILE__, __LINE__, (s))
#define	newalloc(p,s)		debug_newalloc(__FILE__, __LINE__, (p), (s))
#define	stralloc(s)		debug_stralloc(__FILE__, __LINE__, (s))
#define	newstralloc(p,s)	debug_newstralloc(__FILE__, __LINE__, (p), (s))

/*
 * Voodoo time.  We want to be able to mark these calls with the source
 * line, but CPP does not handle variable argument lists so we cannot
 * do what we did above (e.g. for alloc()).
 *
 * What we do is call a function to save the file and line number
 * and have it return "false".  That triggers the "?" operator to
 * the right side of the ":" which is a call to the debug version of
 * vstralloc/newvstralloc but without parameters.  The compiler gets
 * those from the next input tokens:
 *
 *  xx = vstralloc(a,b,NULL);
 *
 * becomes:
 *
 *  xx = debug_alloc_push(__FILE__,__LINE__)?0:debug_vstralloc(a,b,NULL);
 *
 * This works as long as vstralloc/newvstralloc are not part of anything
 * very complicated.  Assignment is fine, as is an argument to another
 * function (but you should not do that because it creates a memory leak).
 * This will not work in arithmetic or comparison, but it is unlikely
 * they are used like that.
 *
 *  xx = vstralloc(a,b,NULL);			OK
 *  return vstralloc(j,k,NULL);			OK
 *  sub(a, vstralloc(g,h,NULL), z);		OK, but a leak
 *  if(vstralloc(s,t,NULL) == NULL) { ...	NO, but unneeded
 *  xx = vstralloc(x,y,NULL) + 13;		NO, but why do it?
 */

#define vstralloc debug_alloc_push(__FILE__,__LINE__)?0:debug_vstralloc
#define newvstralloc debug_alloc_push(__FILE__,__LINE__)?0:debug_newvstralloc

extern char  *debug_vstralloc       P((char *str, ...));
extern char  *debug_newvstralloc    P((char *oldstr, char *newstr, ...));

#else

extern void  *alloc           P((int size));
extern void  *newalloc        P((void *old, int size));
extern char  *stralloc        P((char *str));
extern char  *newstralloc     P((char *oldstr, char *newstr));
extern char  *vstralloc       P((char *str, ...));
extern char  *newvstralloc    P((char *oldstr, char *newstr, ...));

#define debug_alloc_push(s,l)
#define debug_alloc_pop()
#endif

#define	stralloc2(s1,s2)      vstralloc((s1),(s2),NULL)
#define	newstralloc2(p,s1,s2) newvstralloc((p),(s1),(s2),NULL)

extern void  *sbuf_man        P((void *bufs, void *ptr));
extern char **safe_env        P((void));
extern char  *validate_regexp P((char *regex));
extern char  *validate_glob   P((char *glob));
extern int    match           P((char *regex, char *str));
extern int    match_glob      P((char *glob, char *str));
extern char  *glob_to_regex   P((char *glob));
extern time_t unctime         P((char *timestr));
#if defined(USE_DBMALLOC)
extern char  *dbmalloc_agets  P((char *c, int l, FILE *file));
extern char  *dbmalloc_areads P((char *c, int l, int fd));
#define agets(f)	      dbmalloc_agets(__FILE__,__LINE__,(f))
#define areads(f)	      dbmalloc_areads(__FILE__,__LINE__,(f))
#else
extern char  *agets	      P((FILE *file));
extern char  *areads	      P((int fd));
#endif

/*
 * amfree(ptr) -- if allocated, release space and set ptr to NULL.
 *
 * In general, this should be called instead of just free(), unless
 * the very next source line sets the pointer to a new value.
 */

#define	amfree(ptr) do {						\
    if(ptr) {								\
	int e = errno;							\
	free(ptr);							\
	(ptr) = NULL;							\
	errno = e;							\
    }									\
} while(0)

#define strappend(s1,s2) do {						\
    char *t = (s1) ? stralloc2((s1),(s2)) : stralloc((s2));		\
    amfree((s1));							\
    (s1) = t;								\
} while(0)

/*
 * "Safe" close macros.  Close the object then set it to a value that
 * will cause an error if referenced.
 *
 * aclose(fd) -- close a file descriptor and set it to -1.
 * afclose(f) -- close a stdio file and set it to NULL.
 * apclose(p) -- close a stdio pipe file and set it to NULL.
 *
 * Note: be careful not to do the following:
 *
 *  for(fd = low; fd < high; fd++) {
 *      aclose(fd);
 *  }
 *
 * Since aclose() sets the argument to -1, this will loop forever.
 */

#define aclose(fd) do {							\
    assert(fd >= 0);							\
    close(fd);								\
    (fd) = -1;								\
} while(0)

#define afclose(f) do {							\
    assert(f != NULL);							\
    fclose(f);								\
    (f) = NULL;								\
} while(0)

#define apclose(p) do {							\
    assert(p != NULL);							\
    pclose(p);								\
    (p) = NULL;								\
} while(0)

/*
 * Utility string macros.  All assume a variable holds the current
 * character and the string pointer points to the next character to
 * be processed.  Typical setup is:
 *
 *  s = buffer;
 *  ch = *s++;
 *  skip_whitespace(s, ch);
 *  ...
 *
 * If you advance the pointer "by hand" to skip over something, do
 * it like this:
 *
 *  s += some_amount;
 *  ch = s[-1];
 *
 * Note that ch has the character at the end of the just skipped field.
 * It is often useful to terminate a string, make a copy, then restore
 * the input like this:
 *
 *  skip_whitespace(s, ch);
 *  fp = s-1;			## save the start
 *  skip_nonwhitespace(s, ch);	## find the end
 *  p[-1] = '\0';		## temporary terminate
 *  field = stralloc(fp);	## make a copy
 *  p[-1] = ch;			## restore the input
 *
 * The scanning macros are:
 *
 *  skip_whitespace (ptr, var)
 *    -- skip whitespace, but stops at a newline
 *  skip_non_whitespace (ptr, var)
 *    -- skip non whitespace
 *  skip_non_whitespace_cs (ptr, var)
 *    -- skip non whitespace, stop at comment
 *  skip_integer (ptr, var)
 *    -- skip an integer field
 *  skip_line (ptr, var)
 *    -- skip just past the next newline
 *
 * where:
 *
 *  ptr -- string pointer
 *  var -- current character
 *
 * These macros copy a non-whitespace field to a new buffer, and should
 * only be used if dynamic allocation is impossible (fixed size buffers
 * are asking for trouble):
 *
 *  copy_string (ptr, var, field, len, fldptr)
 *    -- copy a non-whitespace field
 *  copy_string_cs (ptr, var, field, len, fldptr)
 *    -- copy a non-whitespace field, stop at comment
 *
 * where:
 *
 *  ptr -- string pointer
 *  var -- current character
 *  field -- area to copy to
 *  len -- length of area (needs room for null byte)
 *  fldptr -- work pointer used in move
 *	      if NULL on exit, the field was too small for the input
 */

#define	STR_SIZE	1024		/* a generic string buffer size */
#define	NUM_STR_SIZE	32		/* a generic number buffer size */

#define	skip_whitespace(ptr,c) do {					\
    while((c) != '\n' && isspace(c)) (c) = *(ptr)++;			\
} while(0)

#define	skip_non_whitespace(ptr,c) do {					\
    while((c) != '\0' && !isspace(c)) (c) = *(ptr)++;			\
} while(0)

#define	skip_non_whitespace_cs(ptr,c) do {				\
    while((c) != '\0' && (c) != '#' && !isspace(c)) (c) = *(ptr)++;	\
} while(0)

#define	skip_non_integer(ptr,c) do {					\
    while((c) != '\0' && !isdigit(c)) (c) = *(ptr)++;			\
} while(0)

#define	skip_integer(ptr,c) do {					\
    if((c) == '+' || (c) == '-') (c) = *(ptr)++;			\
    while(isdigit(c)) (c) = *(ptr)++;					\
} while(0)

#define	skip_line(ptr,c) do {						\
    while((c) && (c) != '\n') (c) = *(ptr)++;				\
    if(c) (c) = *(ptr)++;						\
} while(0)

#define	copy_string(ptr,c,f,l,fp) do {					\
    (fp) = (f);								\
    while((c) != '\0' && !isspace(c)) {					\
	if((fp) >= (f) + (l) - 1) {					\
	    *(fp) = '\0';						\
	    (fp) = NULL;						\
	    break;							\
	}								\
	*(fp)++ = (c);							\
	(c) = *(ptr)++;							\
    }									\
    if(fp) *fp = '\0';							\
} while(0)

#define	copy_string_cs(ptr,c,f,l,fp) do {				\
    (fp) = (f);								\
    while((c) != '\0' && (c) != '#' && !isspace(c)) {			\
	if((fp) >= (f) + (l) - 1) {					\
	    *(fp) = '\0';						\
	    (fp) = NULL;						\
	    break;							\
	}								\
	*(fp)++ = (c);							\
	(c) = *(ptr)++;							\
    }									\
    if(fp) *fp = '\0';							\
} while(0)

/* from amflock.c */
extern int    amflock   P((int fd, char *resource));
extern int    amroflock P((int fd, char *resource));
extern int    amfunlock P((int fd, char *resource));

/* from file.c */
extern int    mkpdir    P((char *file, int mode, uid_t uid, gid_t gid));
extern int    rmpdir    P((char *file, char *topdir));
extern char  *sanitise_filename P((char *inp));

extern int debug;
extern char *version_info[];

/* from security.c */
extern int security_ok P((struct sockaddr_in *addr,
			  char *str, unsigned long cksum, char **errstr));
extern char *get_bsd_security P((void));

/*
 * Handle functions which are not always declared on all systems.  This
 * stops gcc -Wall and lint from complaining.
 */

/* AIX #defines accept, and provides a prototype for the alternate name */
#if !defined(HAVE_ACCEPT_DECL) && !defined(accept)
extern int accept P((int s, struct sockaddr *addr, int *addrlen));
#endif

#ifndef HAVE_ATOF_DECL
extern double atof P((const char *ptr));
#endif

#ifndef HAVE_BCOPY_DECL
extern void bcopy P((const void *s1, void *s2, size_t n));
#endif

#ifndef HAVE_BIND_DECL
extern int bind P((int s, const struct sockaddr *name, int namelen));
#endif

#ifndef HAVE_BZERO_DECL
extern void bzero P((void *s, size_t n));
#endif

#ifndef HAVE_CLOSELOG_DECL
extern void closelog P((void));
#endif

#ifndef HAVE_CONNECT_DECL
extern int connect P((int s, struct sockaddr *name, int namelen));
#endif

#if !defined(TEXTDB) && !defined(HAVE_DBM_OPEN_DECL)
#undef   DBM_INSERT
#define  DBM_INSERT  0

#undef   DBM_REPLACE
#define  DBM_REPLACE 1

    typedef struct {
	int dummy[10];
    } DBM;

#ifndef HAVE_STRUCT_DATUM
    typedef struct {
	char    *dptr;
	int     dsize;
    } datum;
#endif

    extern DBM   *dbm_open     P((char *file, int flags, int mode));
    extern void   dbm_close    P((DBM *db));
    extern datum  dbm_fetch    P((DBM *db, datum key));
    extern datum  dbm_firstkey P((DBM *db));
    extern datum  dbm_nextkey  P((DBM *db));
    extern int    dbm_delete   P((DBM *db, datum key));
    extern int    dbm_store    P((DBM *db, datum key, datum content, int flg));
#endif

#ifndef HAVE_FCLOSE_DECL
extern int fclose P((FILE *stream));
#endif

#ifndef HAVE_FFLUSH_DECL
extern int fflush P((FILE *stream));
#endif

#ifndef HAVE_FPRINTF_DECL
extern int fprintf P((FILE *stream, const char *format, ...));
#endif

#ifndef HAVE_FPUTC_DECL
extern int fputc P((int c, FILE *stream));
#endif

#ifndef HAVE_FPUTS_DECL
extern int fputs P((const char *s, FILE *stream));
#endif

#ifndef HAVE_FREAD_DECL
extern size_t fread P((void *ptr, size_t size, size_t nitems, FILE *stream));
#endif

#ifndef HAVE_FSEEK_DECL
extern int fseek P((FILE *stream, long offset, int ptrname));
#endif

#ifndef HAVE_FWRITE_DECL
extern size_t fwrite P((const void *ptr, size_t size, size_t nitems,
			FILE *stream));
#endif

#ifndef HAVE_GETHOSTNAME_DECL
extern int gethostname P((char *name, int namelen));
#endif

#ifndef HAVE_GETOPT_DECL
extern char *optarg;
extern int getopt P((int argc, char * const *argv, const char *optstring));
#endif

/* AIX #defines getpeername, and provides a prototype for the alternate name */
#if !defined(HAVE_GETPEERNAME_DECL) && !defined(getpeername)
extern int getpeername P((int s, struct sockaddr *name, int *namelen));
#endif

/* AIX #defines getsockname, and provides a prototype for the alternate name */
#if !defined(HAVE_GETSOCKNAME_DECL) && !defined(getsockname)
extern int getsockname P((int s, struct sockaddr *name, int *namelen));
#endif

#ifndef HAVE_GETSOCKOPT_DECL
extern int getsockopt P((int s, int level, int optname, char *optval,
			 int *optlen));
#endif

#ifndef HAVE_GETTIMEOFDAY_DECL
# ifdef HAVE_TWO_ARG_GETTIMEOFDAY
extern int gettimeofday P((struct timeval *tp, struct timezone *tzp));
# else
extern int gettimeofday P((struct timeval *tp));
# endif
#endif

#ifndef HAVE_INITGROUPS
# define initgroups(name,basegid) 0
#else
# ifndef HAVE_INITGROUPS_DECL
extern int initgroups P((const char *name, gid_t basegid));
# endif
#endif

#ifndef HAVE_IOCTL_DECL
extern int ioctl P((int fildes, int request, ...));
#endif

#ifndef HAVE_LISTEN_DECL
extern int listen P((int s, int backlog));
#endif

#ifndef HAVE_LSTAT_DECL
extern int lstat P((const char *path, struct stat *buf));
#endif

#ifndef HAVE_MALLOC_DECL
extern void *malloc P((size_t size));
#endif

#ifndef HAVE_MEMMOVE_DECL
#ifdef HAVE_MEMMOVE
extern void *memmove P((void *to, const void *from, size_t n));
#else
extern char *memmove P((char *to, /*const*/ char *from, size_t n));
#endif
#endif

#ifndef HAVE_MEMSET_DECL
extern void *memset P((void *s, int c, size_t n));
#endif

#ifndef HAVE_MKTEMP_DECL
extern char *mktemp P((char *template));
#endif

#ifndef HAVE_MKTIME_DECL
extern time_t mktime P((struct tm *timeptr));
#endif

#ifndef HAVE_OPENLOG_DECL
#ifdef LOG_AUTH
extern void openlog P((const char *ident, int logopt, int facility));
#else
extern void openlog P((const char *ident, int logopt));
#endif
#endif

#ifndef HAVE_PCLOSE_DECL
extern int pclose P((FILE *stream));
#endif

#ifndef HAVE_PERROR_DECL
extern void perror P((const char *s));
#endif

#ifndef HAVE_PRINTF_DECL
extern int printf P((const char *format, ...));
#endif

#ifndef HAVE_PUTS_DECL
extern int puts P((const char *s));
#endif

#ifndef HAVE_REALLOC_DECL
extern void *realloc P((void *ptr, size_t size));
#endif

/* AIX #defines recvfrom, and provides a prototype for the alternate name */
#if !defined(HAVE_RECVFROM_DECL) && !defined(recvfrom)
extern int recvfrom P((int s, char *buf, int len, int flags,
		       struct sockaddr *from, int *fromlen));
#endif

#ifndef HAVE_REMOVE_DECL
extern int remove P((const char *path));
#endif

#ifndef HAVE_RENAME_DECL
extern int rename P((const char *old, const char *new));
#endif

#ifndef HAVE_REWIND_DECL
extern void rewind P((FILE *stream));
#endif

#ifndef HAVE_RUSEROK_DECL
extern int ruserok P((const char *rhost, int suser,
		      const char *ruser, const char *luser));
#endif

#ifndef HAVE_SELECT_DECL
extern int select P((int nfds,
		     SELECT_ARG_TYPE *readfds,
		     SELECT_ARG_TYPE *writefds,
		     SELECT_ARG_TYPE *exceptfds,
		     struct timeval *timeout));
#endif

#ifndef HAVE_SENDTO_DECL
extern int sendto P((int s, const char *msg, int len, int flags,
		     const struct sockaddr *to, int tolen));
#endif

#ifdef HAVE_SETRESGID
#define	setegid(x)	setresgid(-1,(x),-1)
#ifndef HAVE_SETRESGID_DECL
extern int setresgid P((gid_t rgid, gid_t egid, gid_t sgid));
#endif
#else
#ifndef HAVE_SETEGID_DECL
extern int setegid P((gid_t egid));
#endif
#endif

#ifdef HAVE_SETRESUID
#define	seteuid(x)	setresuid(-1,(x),-1)
#ifndef HAVE_SETRESUID_DECL
extern int setresuid P((uid_t ruid, uid_t euid, uid_t suid));
#endif
#else
#ifndef HAVE_SETEUID_DECL
extern int seteuid P((uid_t euid));
#endif
#endif

#ifndef HAVE_SETPGID_DECL
#ifdef HAVE_SETPGID
extern int setpgid P((int pid, int pgid));
#endif
#endif

#ifndef HAVE_SETPGRP_DECL
#ifdef SETPGRP_VOID
extern pid_t setpgrp P((void));
#else
extern pid_t setpgrp P((int pgrp, int pid));
#endif
#endif

#ifndef HAVE_SETSOCKOPT_DECL
extern int setsockopt P((int s, int level, int optname,
			 const char *optval, int optlen));
#endif

#ifdef HAVE_SHMGET
#ifndef HAVE_SHMAT_DECL
extern void *shmat P((int shmid, const SHM_ARG_TYPE *shmaddr, int shmflg));
#endif

#ifndef HAVE_SHMCTL_DECL
extern int shmctl P((int shmid, int cmd, struct shmid_ds *buf));
#endif

#ifndef HAVE_SHMDT_DECL
extern int shmdt P((SHM_ARG_TYPE *shaddr));
#endif

#ifndef HAVE_SHMGET_DECL
extern int shmget P((key_t key, size_t size, int shmflg));
#endif
#endif

#if defined(HAVE_SNPRINTF) && defined(HAVE_VSNPRINTF)
#define ap_snprintf	snprintf
#define ap_vsnprintf	vsnprintf
#endif
#ifndef HAVE_SNPRINTF_DECL
#include "arglist.h"
int ap_snprintf  P((char *buf, size_t len, const char *format,...))
		    __attribute__((format(printf,3,4)));
#endif
#ifndef HAVE_VSNPRINTF_DECL
#include "arglist.h"
int ap_vsnprintf P((char *buf, size_t len, const char *format, va_list ap));
#endif

#ifndef HAVE_SOCKET_DECL
extern int socket P((int domain, int type, int protocol));
#endif

#ifndef HAVE_SOCKETPAIR_DECL
extern int socketpair P((int domain, int type, int protocol, int sv[2]));
#endif

#ifndef HAVE_SSCANF_DECL
extern int sscanf P((const char *s, const char *format, ...));
#endif

#ifndef HAVE_STRCASECMP_DECL
extern int strcasecmp P((const char *s1, const char *s2));
#endif

#ifndef HAVE_STRERROR_DECL
extern char *strerror P((int errnum));
#endif

#ifndef HAVE_STRFTIME_DECL
extern size_t strftime P((char *s, size_t maxsize, const char *format,
			  const struct tm *timeptr));
#endif

#ifndef HAVE_STRNCASECMP_DECL
extern int strncasecmp P((const char *s1, const char *s2, int n));
#endif

#ifndef HAVE_SYSLOG_DECL
extern void syslog P((int priority, const char *logstring, ...))
    __attribute__ ((format (printf, 2, 3)));
#endif

#ifndef HAVE_SYSTEM_DECL
extern int system P((const char *string));
#endif

#ifndef HAVE_TIME_DECL
extern time_t time P((time_t *tloc));
#endif

#ifndef HAVE_TOLOWER_DECL
extern int tolower P((int c));
#endif

#ifndef HAVE_TOUPPER_DECL
extern int toupper P((int c));
#endif

#ifndef HAVE_UNGETC_DECL
extern int ungetc P((int c, FILE *stream));
#endif

#ifndef HAVE_VFPRINTF_DECL
#include "arglist.h"
extern int vfprintf P((FILE *stream, const char *format, va_list ap));
#endif

#ifndef HAVE_VPRINTF_DECL
#include "arglist.h"
extern int vprintf P((const char *format, va_list ap));
#endif

#if !defined(S_ISCHR) && defined(_S_IFCHR) && defined(_S_IFMT)
#define S_ISCHR(mode) (((mode) & _S_IFMT) == _S_IFCHR)
#endif

#if !defined(S_ISREG) && defined(_S_IFREG) && defined(_S_IFMT)
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif

#ifndef HAVE_WAITPID
#ifdef HAVE_WAIT4
#define waitpid(pid,status,options) wait4(pid,status,options,0)
#else
extern pid_t waitpid P((pid_t pid, amwait_t *stat_loc, int options));
#endif
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#endif	/* !AMANDA_H */
