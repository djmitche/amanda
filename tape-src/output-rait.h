#ifndef RAIT_H

#define RAIT_H

typedef struct {
    int nopen;
    int nfds;
    int fd_count;
    int *fds;
    int *readres;
    int xorbuflen;
    char *xorbuf;
} RAIT;

#ifdef NO_AMANDA

#define stralloc strdup
#define P(x)	x			/* for function prototypes */

/*
 * Tape drive status structure.  This abstracts the things we are
 * interested in from the free-for-all of what the various drivers
 * supply.
 */

struct am_mt_status {
    char online_valid;			/* is the online flag valid? */
    char bot_valid;			/* is the BOT flag valid? */
    char eot_valid;			/* is the EOT flag valid? */
    char protected_valid;		/* is the protected flag valid? */
    char flags_valid;			/* is the flags field valid? */
    char fileno_valid;			/* is the fileno field valid? */
    char blkno_valid;			/* is the blkno field valid? */
    char device_status_valid;		/* is the device status field valid? */
    char error_status_valid;		/* is the device status field valid? */

    char online;			/* true if device is online/ready */
    char bot;				/* true if tape is at the beginning */
    char eot;				/* true if tape is at end of medium */
    char protected;			/* true if tape is write protected */
    long flags;				/* device flags, whatever that is */
    long fileno;			/* tape file number */
    long blkno;				/* block within file */
    int device_status_size;		/* size of orig device status field */
    unsigned long device_status;	/* "device status", whatever that is */
    int error_status_size;		/* size of orig error status field */
    unsigned long error_status;		/* "error status", whatever that is */
};
#endif

extern int rait_open ();
extern int rait_access P((char *, int));
extern int rait_stat P((char *, struct stat *));
extern int rait_close P((int ));
extern int rait_lseek P((int , long, int));
extern ssize_t rait_write P((int , const void *, size_t));
extern ssize_t rait_read P((int , void *, size_t));
extern int rait_ioctl P((int , int, void *));
extern int rait_copy P((char *f1, char *f2, int buflen));

extern char *rait_init_namelist P((char * dev,
				   char **dev_left,
				   char **dev_right,
				   char **dev_next));
extern int rait_next_name P((char * dev_left,
       			     char * dev_right,
       			     char **dev_next,
       			     char * dev_real));

extern int  rait_tape_open ();
extern int  rait_tapefd_fsf P((int rait_tapefd, int count));
extern int  rait_tapefd_rewind P((int rait_tapefd));
extern void rait_tapefd_resetofs P((int rait_tapefd));
extern int  rait_tapefd_unload P((int rait_tapefd));
extern int  rait_tapefd_status P((int rait_tapefd, struct am_mt_status *stat));
extern int  rait_tapefd_weof P((int rait_tapefd, int count));

#ifdef RAIT_REDIRECT

/* handle ugly Solaris stat mess */

#ifdef _FILE_OFFSET_BITS
#include <sys/stat.h>
#undef stat
#undef open
#if _FILE_OFFSET_BITS == 64
struct	stat {
	dev_t	st_dev;
	long	st_pad1[3];	/* reserved for network id */
	ino_t	st_ino;
	mode_t	st_mode;
	nlink_t st_nlink;
	uid_t 	st_uid;
	gid_t 	st_gid;
	dev_t	st_rdev;
	long	st_pad2[2];
	off_t	st_size;
	timestruc_t st_atim;
	timestruc_t st_mtim;
	timestruc_t st_ctim;
	long	st_blksize;
	blkcnt_t st_blocks;
	char	st_fstype[_ST_FSTYPSZ];
	long	st_pad4[8];	/* expansion area */
};
#endif

#endif

#define access(p,f)	rait_access(p,f)
#define stat(a,b)	rait_stat(a,b)
#define open		rait_open
#define	close(a)	rait_close(a)
#define read(f,b,l)	rait_read(f,b,l)
#define write(f,b,l)	rait_write(f,b,l)
#define	ioctl(f,n,x)	rait_ioctl(f,n,x)
#endif

#endif
