#ifndef _CONFIG_H
#define _CONFIG_H

@TOP@
@BOTTOM@

/* Define to empty if the compiler does not support volatile.  */
#undef volatile

/* Define to the name of the distribution.  */
#undef PACKAGE

/* Define to the version of the distribution.  */
#undef VERSION

/* Location of Amanda directories and files.  */
#undef bindir
#undef sbindir
#undef libexecdir
#undef mandir
#undef CONFIG_DIR
#undef DEBUG_DIR

/* This is the default Amanda index server.  */
#undef DEFAULT_SERVER

/* This is the default Amanda configuration.  */
#undef DEFAULT_CONFIG

/* This is the default restoring Amanda tape server.  */
#undef DEFAULT_TAPE_SERVER

/* This is the default no-rewinding tape device.  */
#undef DEFAULT_TAPE_DEVICE

/* This is the default changer device.  */
#undef DEFAULT_CHANGER_DEVICE

/* Define if you want to use the .amandahosts file instead of .rhosts.  */
#undef USE_AMANDAHOSTS

/* Define the location of smbclient for backing up Samba PC clients.  */
#undef SAMBA_CLIENT

/* Define the location of smbclient for backing up Samba PC clients.  */
#undef SAMBA_USER

/* Define the location of the grep program.  */
#undef GREP

/* Define GNUTAR to be the location of the Gnu tar program if you want
 * Gnu tar backup support.  */
#undef GNUTAR

#ifdef GNUTAR
/* Used in sendbackup-gnutar.c  */
#undef GNUTAR_LISTED_INCREMENTAL_DIR
#undef ENABLE_GNUTAR_ATIME_PRESERVE
#endif

/* For AIX systems.  */
#undef AIX_TAPEIO
#undef AIX_BACKUP

/* For Ultrix systems.  */
#undef STATFS_ULTRIX

/* For OSF systems.  */
#undef STATFS_OSF1

/* For XENIX/ISC systems.  */
#undef XENIX_TAPEIO

/* For UnixWare systems.  */
#undef UWARE_TAPEIO

/* For SCO OS5 systems.  */
#undef STATFS_SCO_OS5

/* For HPUX tape-changer support.  */
#undef HAVE_HPUX_SCSI

/* For Linux systems with floppy tapes: 
 * QIC volume table support via raw tape device.  */
#undef DEFAULT_RAW_TAPE_DEVICE

/*
 * Decide whether to invoke rundump (setuid-root) or DUMP program directly.
 */
#undef USE_RUNDUMP

/* Define this if this system's dump exits with 1 as a success code.  */
#undef DUMP_RETURNS_1

/* Define this if dump accepts -E for estimates.  */
#undef HAVE_DUMP_ESTIMATE

/* Define this if dump accepts -h for honoring nodump.  */
#undef HAVE_HONOR_NODUMP

/* Define the location of the ufsdump, backup, or dump program.  */
#undef DUMP

/* Define the location of the ufsrestore or restore program.  */
#undef RESTORE

/* Define the location of the vdump program.  */
#undef VDUMP

/* Define the location of the vrestore program.  */
#undef VRESTORE

/* Define the location of the xfsdump program on Irix hosts.  */
#undef XFSDUMP

/* Define the location of the xfsrestore program on Irix hosts.  */
#undef XFSRESTORE

/* Define the location of the vxdump program on HPUX and SINIX hosts.  */
#undef VXDUMP

/* Define the location of the vxrestore program on HPUX and SINIX hosts.  */
#undef VXRESTORE

/* Define if Amanda is using the gzip program.  */
#undef HAVE_GZIP

/* Define to the exact path to the gzip or the compress program.  */
#undef COMPRESS_PATH

/* Define to the suffix for the COMPRESS_PATH compression program.  */
#undef COMPRESS_SUFFIX

/* Define as the command line option for fast compression.  */
#undef COMPRESS_FAST_OPT

/* Define as the command line option for best compression.  */
#undef COMPRESS_BEST_OPT

/* Define as the exact path to the gzip or compress command.  */
#undef UNCOMPRESS_PATH

/* Define as any optional arguments to get UNCOMPRESS_PATH to uncompress.  */
#undef UNCOMPRESS_OPT

/* mail sendmail */
#undef MAILER

/* Define as the prefix for disk devices, commonly /dev/ or /dev/dsk/  */
#undef DEV_PREFIX

/* Define as the prefix for raw disk devices, commonly /dev/r or /dev/rdsk/  */
#undef RDEV_PREFIX

/* Define to use BSD .rhosts security.  */
#undef BSD_SECURITY

/* Define to have programs use version suffixes when calling other programs. */
#undef USE_VERSION_SUFFIXES

/* Define to force to another user on client machines.  */
#undef FORCE_USERID

/* Define as a the user to force to on client machines.  */
#undef CLIENT_LOGIN

/* Define as the user who owns installed binaries. */
#undef BINARY_OWNER

/* Define for backups being done on a multiple networks and FQDNs are used.  */
#undef USE_FQDN

/* Define if dumper should buffer the sockets for faster throughput.  */
#undef DUMPER_SOCKET_BUFFERING

/* Define if you want debugging.  */
#undef DEBUG_CODE

/* Define if you want the debugging files that appear in /tmp to have
 * the process ID appended to the filename.  */
#undef DEBUG_FILE_WITH_PID

/* Define if you want assertion checking.  */
#undef ASSERTIONS

/* Defined to the switch to be used when invoking mt to specify the
 * tape device.  */
#undef MT_FILE_FLAG

/* Command for starting printing jobs */
#undef LPRCMD

/* LPRCMD switch for specifying a printer name */
#undef LPRFLAG

/* Define the names for the Amanda and Kerberos Amanda services.  If you
 * wish to use an alternate service name, then the alternate service name
 * must be appended to the Amanda and Kamanda service names.  This is
 * useful when you are trying to run a test installation in parallel to a
 * production one.  */
#undef AMANDA_SERVICE_NAME
#undef KAMANDA_SERVICE_NAME
#undef SERVICE_SUFFIX

/* Kerberos security defines.  */
#undef KRB4_SECURITY
#undef SERVER_HOST_PRINCIPLE
#undef SERVER_HOST_INSTANCE
#undef SERVER_HOST_KEY_FILE
#undef CLIENT_HOST_PRINCIPLE
#undef CLIENT_HOST_INSTANCE
#undef CLIENT_HOST_KEY_FILE
#undef TICKET_LIFETIME

/* Define only one of these as the header file for the database routines.  */
#undef USE_DB_H
#undef USE_DBM_H
#undef USE_GDBM_H
#undef USE_NDBM_H
#undef TEXTDB

/* System function characteristics.  */
#undef HAVE_SYSVSHM
#undef SELECT_ARG_TYPE
#undef SHM_ARG_TYPE
#undef HAVE_TWO_ARG_GETTIMEOFDAY
#undef HAVE_STRUCT_DATUM

/* Compiler characteristics.  */
#undef HAVE_UNSIGNED_LONG_CONSTANTS

/* Type of file locking.  Define none or one of these.  */
#undef USE_POSIX_FCNTL
#undef USE_FLOCK
#undef USE_LOCKF
#undef USE_LNLOCK

/* Definitions in header files.  */
#undef HAVE__POSIX2_RE_DUP_MAX
#undef HAVE_CHAR_MIN
#undef HAVE_CHAR_MAX
#undef HAVE_CHAR_BIT

/* Declarations of functions.  */
#undef HAVE_ACCEPT_DECL
#undef HAVE_ATOF_DECL
#undef HAVE_BCOPY_DECL
#undef HAVE_BIND_DECL
#undef HAVE_BZERO_DECL
#undef HAVE_CLOSELOG_DECL
#undef HAVE_CONNECT_DECL
#undef HAVE_DBM_OPEN_DECL
#undef HAVE_FCLOSE_DECL
#undef HAVE_FFLUSH_DECL
#undef HAVE_FLOCK_DECL
#undef HAVE_FPRINTF_DECL
#undef HAVE_FPUTC_DECL
#undef HAVE_FPUTS_DECL
#undef HAVE_FREAD_DECL
#undef HAVE_FSEEK_DECL
#undef HAVE_FWRITE_DECL
#undef HAVE_GETHOSTNAME_DECL
#undef HAVE_GETOPT_DECL
#undef HAVE_GETPEERNAME_DECL
#undef HAVE_GETSOCKNAME_DECL
#undef HAVE_GETSOCKOPT_DECL
#undef HAVE_GETTIMEOFDAY_DECL
#undef HAVE_INITGROUPS_DECL
#undef HAVE_IOCTL_DECL
#undef HAVE_LISTEN_DECL
#undef HAVE_LSTAT_DECL
#undef HAVE_MALLOC_DECL
#undef HAVE_MEMMOVE_DECL
#undef HAVE_MEMSET_DECL
#undef HAVE_MKTEMP_DECL
#undef HAVE_MKTIME_DECL
#undef HAVE_OPENLOG_DECL
#undef HAVE_PCLOSE_DECL
#undef HAVE_PERROR_DECL
#undef HAVE_PRINTF_DECL
#undef HAVE_PUTS_DECL
#undef HAVE_REALLOC_DECL
#undef HAVE_RECVFROM_DECL
#undef HAVE_REMOVE_DECL
#undef HAVE_RENAME_DECL
#undef HAVE_REWIND_DECL
#undef HAVE_RUSEROK_DECL
#undef HAVE_SELECT_DECL
#undef HAVE_SENDTO_DECL
#undef HAVE_SETEGID_DECL
#undef HAVE_SETEUID_DECL
#undef HAVE_SETPGID_DECL
#undef HAVE_SETPGRP_DECL
#undef HAVE_SETRESGID_DECL
#undef HAVE_SETRESUID_DECL
#undef HAVE_SETSOCKOPT_DECL
#undef HAVE_SHMAT_DECL
#undef HAVE_SHMCTL_DECL
#undef HAVE_SHMDT_DECL
#undef HAVE_SHMGET_DECL
#undef HAVE_SNPRINTF_DECL
#undef HAVE_SOCKET_DECL
#undef HAVE_SOCKETPAIR_DECL
#undef HAVE_SSCANF_DECL
#undef HAVE_STRERROR_DECL
#undef HAVE_STRFTIME_DECL
#undef HAVE_STRNCASECMP_DECL
#undef HAVE_SYSLOG_DECL
#undef HAVE_SYSTEM_DECL
#undef HAVE_SSCANF_DECL
#undef HAVE_TIME_DECL
#undef HAVE_TOLOWER_DECL
#undef HAVE_TOUPPER_DECL
#undef HAVE_UNGETC_DECL
#undef HAVE_VFPRINTF_DECL
#undef HAVE_VPRINTF_DECL
#undef HAVE_VSNPRINTF_DECL

/* Defined if wait() puts the status in a union wait instead of int.  */
#undef WAIT_USES_UNION

#endif
