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
 * $Id: amanda.h,v 1.18 1997/10/08 05:33:20 george Exp $
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

#ifdef HAVE_ASM_BYTEORDER_H
#  include <asm/byteorder.h>
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
#elif defined(USE_DBM_H)
#  include <dbm.h>
#elif defined(USE_GDBM_H)
#  include <gdbm.h>
#elif defined(USE_NDBM_H)
#  include <ndbm.h>
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

#ifdef HAVE_SYS_WAIT_H
#  include <sys/wait.h>
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

#ifdef KRB4_SECURITY
#  include <des.h>
#  include <krb.h>
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
 * assertions, but call error() instead of abort 
 */
#ifndef ASSERTIONS

#define assert(exp) (0)

#else	/* ASSERTIONS */

#define assert(exp) {if(!(exp)) error("assert: %s false, file %s, line %d", \
				   stringize(exp), __FILE__, __LINE__);}

#endif	/* ASSERTIONS */

/*
 * print debug output, else compile to nothing.
 */

/*
 * This if the file descriptor for the debugging output.  If debugging
 * output is not on, then it is set to stderr.
 */
extern int db_file;

#ifdef DEBUG_CODE
#   define dbopen(filename)    debug_open(filename)
#   define dbclose()    debug_close()
#   define dbprintf(p)  (debug? (debug_printf p, 0) : 0)

extern void debug_open P((char *filename));
extern void debug_close P((void));
extern void debug_printf P((char *format, ...))
#ifdef __GNUC__
     __attribute__ ((format (printf, 1, 2)))
#endif
     ;
#else
#   define dbopen(filename)
#   define dbclose()
#   define dbprintf(p)
#endif

/* amanda #days calculation, with roundoff */

#define SECS_PER_DAY	(24*60*60)
#define days_diff(a, b)	(((b) - (a) + SECS_PER_DAY/2) / SECS_PER_DAY)


/* Global constants. */

#ifndef SERVICE_SUFFIX
#define SERVICE_SUFFIX ""
#endif

#define AMANDA_SERVICE_NAME "amanda" SERVICE_SUFFIX
#define AMANDA_SERVICE_DEFAULT	10080

#define KAMANDA_SERVICE_NAME "kamanda" SERVICE_SUFFIX
#define KAMANDA_SERVICE_DEFAULT	10081

/* Size of a tape block in kbytes.  Do not change lightly. */
#define TAPE_BLOCK_SIZE 32
#define TAPE_BLOCK_BYTES (TAPE_BLOCK_SIZE*1024)


/* Define misc amanda functions.  */
#define ERR_INTERACTIVE	1
#define ERR_SYSLOG	2
#define ERR_AMANDALOG	4

extern int    erroutput_type;
extern void   error     P((char *format, ...))
#ifdef __GNUC__
     __attribute__ ((format (printf, 1, 2)))
#endif
     ;
extern int    onerror   P((void (*errf)(void)));
extern void  *alloc     P((int size));
extern void  *newalloc  P((void *old, int size));
extern char  *stralloc  P((char *str));
extern char  *newstralloc P((char *oldstr, char *newstr));
extern char  *validate_regexp P((char *regex));
extern int    match     P((char *regex, char *str));
extern time_t unctime   P((char *timestr));
extern int    amflock   P((int fd, char *resource));
extern int    amroflock P((int fd, char *resource));
extern int    amfunlock P((int fd, char *resource));

extern int debug;
extern char *version_info[];

/*
 * Handle functions which are not always declared on all systems.  This
 * stops gcc -Wall and lint from complaining.
 */

#ifndef HAVE_ACCEPT_DECL
extern int accept P((int s, struct sockaddr *addr, int *addrlen));
#endif

#ifndef HAVE_ATOF_DECL
extern double atof P((const char *ptr));
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

#ifndef HAVE_FWRITE_DECL
extern size_t fwrite P((const void *ptr, size_t size, size_t nitems,
			FILE *stream));
#endif

#ifndef HAVE_GETHOSTNAME_DECL
extern int gethostname P((char *name, int namelen));
#endif

#ifndef HAVE_GETOPT_DECL
extern int getopt P((int argc, char * const *argv, const char *optstring));
#endif

#ifndef HAVE_GETPEERNAME_DECL
extern int getpeername P((int s, struct sockaddr *name, int *namelen));
#endif

#ifndef HAVE_GETSOCKNAME_DECL
extern int getsockname P((int s, struct sockaddr *name, int *namelen));
#endif

#ifndef HAVE_GETSOCKOPT_DECL
extern int getsockopt P((int s, int level, int optname, char *optval,
			 int *optlen));
#endif

#ifndef HAVE_GETTIMEOFDAY_DECL
#ifdef HAVE_TWO_ARG_GETTIMEOFDAY
extern int gettimeofday P((struct timeval *tp, struct timezone *tzp));
#else
extern int gettimeofday P((struct timeval *tp));
#endif
#endif

#ifndef HAVE_INITGROUPS_DECL
extern int initgroups P((const char *name, gid_t basegid));
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

#ifndef HAVE_SOCKET_DECL
extern int socket P((int domain, int type, int protocol));
#endif

#ifndef HAVE_SOCKETPAIR_DECL
extern int socketpair P((int domain, int type, int protocol, int sv[2]));
#endif

#ifndef HAVE_SSCANF_DECL
extern int sscanf P((const char *s, const char *format, ...));
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
#ifdef __GNUC__
     __attribute__ ((format (printf, 2, 3)))
#endif
     ;
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

#ifndef HAVE_VSPRINTF_DECL
#include "arglist.h"
extern int vsprintf P((char *s, const char *format, va_list ap));
#endif

#endif	/* !AMANDA_H */
