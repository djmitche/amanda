#ifndef RAIT_H

#define RAIT_H

#define MAXRAIT 6
#define MAXBUFSIZE 32768

typedef struct {
    int nopen;
    int nfds;
    int fds[MAXRAIT];
    char xorbuf[MAXBUFSIZE];
} RAIT;

extern int rait_open();
extern int rait_access(char *, int);
extern int rait_stat();
extern int rait_close(int );		
extern int rait_lseek(int , long, int);
extern int rait_write(int , const char *, int);
extern int rait_read(int , char *, int);
extern int rait_ioctl(int , int, void *);
extern int rait_copy(char *f1, char *f2);

extern char *rait_init_namelist(char *);
extern char *rait_next_name();

#ifndef NO_AMANDA
extern int  rait_tape_open(char *, int);
extern int  rait_tapefd_fsf(int rait_tapefd, int count);
extern int  rait_tapefd_rewind(int rait_tapefd);
extern void rait_tapefd_resetofs(int rait_tapefd);
extern int  rait_tapefd_unload(int rait_tapefd);
extern int  rait_tapefd_status(int rait_tapefd);
extern int  rait_tapefd_weof(int rait_tapefd, int count);
#endif

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
