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
 * $Id: tapeio.c,v 1.20.4.7.2.1 2000/12/13 19:25:29 jrjackson Exp $
 *
 * implements tape I/O functions
 */
#include "amanda.h"

#include "tapeio.h"
#include "fileheader.h"
#ifndef R_OK
#define R_OK 4
#define W_OK 2
#endif

extern int plain_tape_access(), plain_tape_open(), plain_tape_stat(), 
	plain_tapefd_close(), plain_tapefd_fsf(), plain_tapefd_fsf(),
	plain_tapefd_read(), plain_tapefd_rewind(), 
	plain_tapefd_unload(), plain_tapefd_status(), plain_tapefd_weof(),
        plain_tapefd_write();

extern void plain_tapefd_resetofs();

#include "output-rait.h"

static struct virtualtape {
    char *prefix;
    int (*xxx_tape_access)(char *, int);
    int (*xxx_tape_open)(char *, int);
    int (*xxx_tape_stat)(char *, struct stat *);
    int (*xxx_tapefd_close)(int);
    int (*xxx_tapefd_fsf)(int, int);
    int (*xxx_tapefd_read)(int, void *, int);
    int (*xxx_tapefd_rewind)(int);
    void (*xxx_tapefd_resetofs)(int);
    int (*xxx_tapefd_unload)(int);
    int (*xxx_tapefd_status)(int);
    int (*xxx_tapefd_weof)(int, int);
    int (*xxx_tapefd_write)(int, const void *, int);
} vtable[] = {
  /* note: "plain" has to be the zeroth entry, its the
  **        default if no prefix match is found.
  */
  {"plain", plain_tape_access, plain_tape_open, plain_tape_stat, 
	plain_tapefd_close, plain_tapefd_fsf, 
	plain_tapefd_read, plain_tapefd_rewind, plain_tapefd_resetofs,
	plain_tapefd_unload, plain_tapefd_status, plain_tapefd_weof,
        plain_tapefd_write },
  {"rait", rait_access, rait_tape_open, rait_stat, 
	rait_close, rait_tapefd_fsf, 
	rait_read, rait_tapefd_rewind, rait_tapefd_resetofs,
	rait_tapefd_unload, rait_tapefd_status, rait_tapefd_weof,
        rait_write },
  {0,},
};

/*
** When we manufacture pseudo-file-descriptors to be indexes into
** the fdtrans table below, here's the offset we add/subtract.
*/
#define TAPE_OFFSET 1024

/*
** if this is increased, make the initializer longer below.
*/
#define MAXTAPEFDS 10

static struct fdtrans {
   int virtslot;
   int descriptor;
} fdtable[MAXTAPEFDS] = {
  { 0, -1 },
  { 0, -1 },
  { 0, -1 },
  { 0, -1 },
  { 0, -1 },
  { 0, -1 },
  { 0, -1 },
  { 0, -1 },
  { 0, -1 },
  { 0, -1 },
};

static int name2slot(char *name, char **ntrans) {
    char *pc;
    int len, i;

    if (0 != (pc = strchr(name, ':'))) {
        len = pc - name;
	for( i = 0 ; vtable[i].prefix && vtable[i].prefix[0]; i++ ) {
	    if (0 == strncmp(vtable[i].prefix, name , len)) {
		*ntrans = pc + 1;
		return i;
            }
        }
    }
    *ntrans = name;
    return 0;
}

int tape_access(char *filename, int mode) {
    char *tname;
    return vtable[name2slot(filename, &tname)].xxx_tape_access(tname, mode);
}

int tape_stat(char *filename, struct stat *buf) {
    char *tname;
    return vtable[name2slot(filename, &tname)].xxx_tape_stat(tname, buf);
}

int tape_open(char *filename, int mode) {
    char *tname;
    int vslot;
    int i;

    for( i = 0; i < MAXTAPEFDS ; i++ ) {
        if ( -1 == fdtable[i].descriptor ) {
            break;
        }
    }
    if ( i == MAXTAPEFDS ) {
	/* no slot in the fdtable available */
	return -1;
    } else {
	vslot = fdtable[i].virtslot = name2slot(filename, &tname);
	if ( vslot > 0 ) {
	    fdtable[i].descriptor = vtable[vslot].xxx_tape_open(tname, mode);
	    return TAPE_OFFSET + i;
        } else {
	    /* don't redirect if it's plain tape anyway... */
            return plain_tape_open(tname, mode);
        }
   }
}

int tapefd_close(int tapefd) {
    int tfd;
    int vslot, i, res;

    if (tapefd >= TAPE_OFFSET) {
	i = tapefd - TAPE_OFFSET;
	vslot = fdtable[i].virtslot;
        res = vtable[vslot].xxx_tapefd_close(fdtable[i].descriptor);
        if ( 0 == res ) {
	    /* it closed, so free the slot */
	    fdtable[i].descriptor = -1;
        }
        return res;
    } else {
	return plain_tapefd_close(tapefd);
    }
}

int tapefd_fsf(int tapefd, int count) {
    int tfd;
    int vslot, i;

    if (tapefd >= TAPE_OFFSET) {
	i = tapefd - TAPE_OFFSET;
	vslot = fdtable[i].virtslot;
        return  vtable[vslot].xxx_tapefd_fsf(fdtable[i].descriptor, count);
    } else {
	return plain_tapefd_fsf(tapefd, count);
    }
}

int tapefd_rewind(tapefd) {
    int tfd;
    int vslot, i;

    if (tapefd >= TAPE_OFFSET) {
	i = tapefd - TAPE_OFFSET;
	vslot = fdtable[i].virtslot;
        return  vtable[vslot].xxx_tapefd_rewind(fdtable[i].descriptor);
    } else {
	return plain_tapefd_rewind(tapefd);
    }
}

void tapefd_resetofs(tapefd) {
    int tfd;
    int vslot, i;

    if (tapefd >= TAPE_OFFSET) {
	i = tapefd - TAPE_OFFSET;
	vslot = fdtable[i].virtslot;
        vtable[vslot].xxx_tapefd_resetofs(fdtable[i].descriptor);
    } else {
	plain_tapefd_resetofs(tapefd);
    }
}

int tapefd_unload(tapefd) {
    int tfd;
    int vslot, i;

    if (tapefd >= TAPE_OFFSET) {
	i = tapefd - TAPE_OFFSET;
	vslot = fdtable[i].virtslot;
        return  vtable[vslot].xxx_tapefd_unload(fdtable[i].descriptor);
    } else {
	return plain_tapefd_unload(tapefd);
    }
}

int tapefd_status(tapefd) {
    int tfd;
    int vslot, i;

    if (tapefd >= TAPE_OFFSET) {
	i = tapefd - TAPE_OFFSET;
	vslot = fdtable[i].virtslot;
        return  vtable[vslot].xxx_tapefd_status(fdtable[i].descriptor);
    } else {
	return plain_tapefd_status(tapefd);
    }
}

int tapefd_weof(tapefd, count){
    int tfd;
    int vslot, i;

    if (tapefd >= TAPE_OFFSET) {
	i = tapefd - TAPE_OFFSET;
	vslot = fdtable[i].virtslot;
        return  vtable[vslot].xxx_tapefd_weof(fdtable[i].descriptor, count);
    } else {
	return plain_tapefd_weof(tapefd, count);
    }
}

int tapefd_read(int tapefd, void *buffer, int count) {
    int tfd;
    int vslot, i;

    if (tapefd >= TAPE_OFFSET) {
	i = tapefd - TAPE_OFFSET;
	vslot = fdtable[i].virtslot;
        return  vtable[vslot].xxx_tapefd_read(fdtable[i].descriptor, buffer, count);
    } else {
	return plain_tapefd_read(tapefd, buffer, count);
    }
}
int tapefd_write(int tapefd, const void *buffer, int count){
    int tfd;
    int vslot, i;

    if (tapefd >= TAPE_OFFSET) {
	i = tapefd - TAPE_OFFSET;
	vslot = fdtable[i].virtslot;
        return  vtable[vslot].xxx_tapefd_write(fdtable[i].descriptor, buffer, count);
    } else {
	return plain_tapefd_write(tapefd, buffer, count);
    }
}

/*
=======================================================================
** implement ioctl based plain tape routines in terms of ioctl an 
** tapefd_xxx_ioctl() routines.  This way code like the RAIT code 
** can use the same code but pass in rait_ioctl instead of ioctl.
=======================================================================
*/
int plain_tapefd_fsf(int plain_tapefd, int count) {
    return tapefd_fsf_ioctl(plain_tapefd, count, &ioctl);
}

int plain_tapefd_weof(int plain_tapefd, int count) {
    return tapefd_weof_ioctl(plain_tapefd, count, &ioctl);
}

int plain_tapefd_rewind(int plain_tapefd) {
    return tapefd_rewind_ioctl(plain_tapefd, &ioctl);
}

int plain_tapefd_unload(int plain_tapefd) {
    return tapefd_unload_ioctl(plain_tapefd, &ioctl);
}

int plain_tapefd_status(int plain_tapefd) {
    return tapefd_status_ioctl(plain_tapefd, &ioctl);
}


/*
=======================================================================
** Now the really plain plain tape routines
=======================================================================
*/

static int no_op_tapefd = -1;

int plain_tapefd_read(tapefd, buffer, count)
int tapefd, count;
void *buffer;
{
    return read(tapefd, buffer, count);
}

int plain_tapefd_write(tapefd, buffer, count)
int tapefd, count;
const void *buffer;
{
    return write(tapefd, buffer, count);
}

int plain_tapefd_close(tapefd)
int tapefd;
{
    if (tapefd == no_op_tapefd) {
	no_op_tapefd = -1;
    }
    return close(tapefd);
}

void plain_tapefd_resetofs(tapefd)
int tapefd;
{
    /* 
     * this *should* be a no-op on the tape, but resets the kernel's view
     * of the file offset, preventing it from barfing should we pass the
     * filesize limit (eg OSes with 2 GB filesize limits) on a long tape.
     */
    lseek(tapefd, (off_t) 0L, SEEK_SET);
}
/*
=======================================================================
** Now the tapefd_xxx_ioctl() routines, which are #ifdef-ed
** heavily by platform.
=======================================================================
*/
static char *errstr = NULL;

#if defined(HAVE_BROKEN_FSF)
/*
 * tapefd_fsf_broken -- handle systems that have a broken fsf operation
 * and cannot do an fsf operation unless they are positioned at a tape
 * mark (or BOT).  This shows up in amrestore as I/O errors when skipping.
 */

static int
tapefd_fsf_broken_ioctl(tapefd, count, ioctl)
int tapefd;
int count;
int (*ioctl)();
{
    char buffer[TAPE_BLOCK_BYTES];
    int len = 0;

    if(tapefd == no_op_tapefd) {
	return 0;
    }
    while(--count >= 0) {
	while((len = tapefd_read(tapefd, buffer, sizeof(buffer))) > 0) {}
	if(len < 0) {
	    break;
	}
    }
    return len;
}
#endif

#ifdef UWARE_TAPEIO 

#include <sys/tape.h>

int tapefd_rewind_ioctl(tapefd, ioctl)
int tapefd;
int (*ioctl)();
{
    int st;
    return (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, T_RWD, &st);
}

int tapefd_unload_ioctl(tapefd, ioctl)
int tapefd;
int (*ioctl)();
{
    int st;
    return (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, T_OFFL, &st);
                           /* not sure of spelling here ^^^^^^ */
}

int tapefd_fsf_ioctl(tapefd, count, ioctl)
int tapefd, count;
int (*ioctl)();
/*
 * fast-forwards the tape device count files.
 */
{
#if defined(HAVE_BROKEN_FSF)
    return tapefd_fsf_broken_ioctl(tapefd, count, ioctl);
#else
    int st;
    int c;
    int status;

    for ( c = count; c ; c--)
        if (status = (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, T_SFF, &st))
            break;

    return status;
#endif
}

int tapefd_weof_ioctl(tapefd, count, ioctl)
int tapefd, count;
int (*ioctl)();
/*
 * write <count> filemarks on the tape.
 */
{
    int st;
    int c;
    int status;

    for ( c = count; c ; c--)
        if (status = (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, T_WRFILEM, &st))
            break;

    return status;
}

#else
#ifdef AIX_TAPEIO

#include <sys/tape.h>

int tapefd_rewind_ioctl(tapefd, ioctl)
int tapefd;
int (*ioctl)();
{
    struct stop st;

    st.st_op = STREW;
    st.st_count = 1;

    return (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, STIOCTOP, &st);
}

int tapefd_unload_ioctl(tapefd, ioctl)
int tapefd;
int (*ioctl)();
{
    struct stop st;

    st.st_op = STOFFL;
    st.st_count = 1;

    return (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, STIOCTOP, &st);
}

int tapefd_fsf_ioctl(tapefd, count, ioctl)
int tapefd, count;
int (*ioctl)();
/*
 * fast-forwards the tape device count files.
 */
{
#if defined(HAVE_BROKEN_FSF)
    return tapefd_fsf_broken(tapefd, count);
#else
    struct stop st;

    st.st_op = STFSF;
    st.st_count = count;

    return (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, STIOCTOP, &st);
#endif
}

int tapefd_weof_ioctl(tapefd, count, ioctl)
int tapefd, count;
int (*ioctl)();
/*
 * write <count> filemarks on the tape.
 */
{
    struct stop st;

    st.st_op = STWEOF;
    st.st_count = count;

    return (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, STIOCTOP, &st);
}

#else /* AIX_TAPEIO */
#ifdef XENIX_TAPEIO

#include <sys/tape.h>

int tapefd_rewind_ioctl(tapefd, ioctl)
int tapefd;
int (*ioctl)();
{
    int st;
    return (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, MT_REWIND, &st);
}

int tapefd_unload_ioctl(tapefd, ioctl)
int tapefd;
int (*ioctl)();
{
    int st;
#ifdef MT_OFFLINE
    return (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, MT_OFFLINE, &st);
#else
    return (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, MT_UNLOAD, &st);
#endif
}

int tapefd_fsf_ioctl(tapefd, count, ioctl)
int tapefd, count;
int (*ioctl)();
/*
 * fast-forwards the tape device count files.
 */
{
#if defined(HAVE_BROKEN_FSF)
    return tapefd_fsf_broken_ioctl(tapefd, count, ioctl);
#else
    int st;
    int c;
    int status;

    for ( c = count; c ; c--)
	if (status = (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, MT_RFM, &st))
	    break;

    return status;
#endif
}

int tapefd_weof_ioctl(tapefd, count, ioctl)
int tapefd, count;
int (*ioctl)();
/*
 * write <count> filemarks on the tape.
 */
{
    int st;
    int c;
    int status;

    for ( c = count; c ; c--)
	if (status = (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, MT_WFM, &st))
	    break;

    return status;
}


#else	/* ! AIX_TAPEIO && !XENIX_TAPEIO */


#include <sys/mtio.h>

int tapefd_rewind_ioctl(tapefd, ioctl)
int tapefd;
int (*ioctl)();
{
    struct mtop mt;
    int rc, cnt;

    mt.mt_op = MTREW;
    mt.mt_count = 1;

    /* EXB-8200 drive on FreeBSD can fail to rewind, but retrying
     * won't hurt, and it will usually even work! */
    for(cnt = 0; cnt < 10; ++cnt) {
	rc = (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, MTIOCTOP, &mt);
	if (rc == 0)
	    break;
	sleep(3);
    }
    return rc;
}

int tapefd_unload_ioctl(tapefd, ioctl)
int tapefd;
int (*ioctl)();
{
    struct mtop mt;
    int rc, cnt;

#ifdef MTUNLOAD
    mt.mt_op = MTUNLOAD;
#else
#ifdef MTOFFL
    mt.mt_op = MTOFFL;
#else
    mt.mt_op = syntax error;
#endif
#endif
    mt.mt_count = 1;

    /* EXB-8200 drive on FreeBSD can fail to rewind, but retrying
     * won't hurt, and it will usually even work! */
    for(cnt = 0; cnt < 10; ++cnt) {
	rc = (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, MTIOCTOP, &mt);
	if (rc == 0)
	    break;
	sleep(3);
    }
    return rc;
}

int tapefd_fsf_ioctl(tapefd, count, ioctl)
int tapefd, count;
int (*ioctl)();
/*
 * fast-forwards the tape device count files.
 */
{
#if defined(HAVE_BROKEN_FSF)
    return tapefd_fsf_broken(tapefd, count);
#else
    struct mtop mt;

    mt.mt_op = MTFSF;
    mt.mt_count = count;

    return (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, MTIOCTOP, &mt);
#endif
}

int tapefd_weof_ioctl(tapefd, count, ioctl)
int tapefd, count;
int (*ioctl)();
/*
 * write <count> filemarks on the tape.
 */
{
    struct mtop mt;

    mt.mt_op = MTWEOF;
    mt.mt_count = count;

    return (tapefd == no_op_tapefd) ? 0 : ioctl(tapefd, MTIOCTOP, &mt);
}




#endif /* !XENIX_TAPEIO */
#endif /* !AIX_TAPEIO */
#endif /* !UWARE_TAPEIO */

int
tapefd_status_ioctl(fd, ioctl) 
int fd;
int (*ioctl)();
{
   int res = 0;

#if defined(MTIOCGET)
   struct mtget buf;

   res = ioctl(fd,MTIOCGET,&buf);

#ifdef MT_ONL
   /* IRIX-ish system */
   printf("status: %s %s %s %s\n",
		(buf.mt.dposn & MT_ONL) ? "ONLINE" : "OFFLINE",
		(buf.mt.dposn & MT_EOT) ? "EOT" : "",
		(buf.mt.dposn & MT_BOT) ? "BOT" : "",
		(buf.mt.dposn & MT_WRPROT) ? "PROTECTED" : ""
        );
#endif
#ifdef GMT_ONLINE
   /* Linux-ish system */
   printf("status: %s %s %s %s\n",
		GMT_ONLINE(buf.mt_gstat) ? "ONLINE" : "OFFLINE",
		GMT_EOT(buf.mt_gstat) ? "EOT" : "",
		GMT_BOT(buf.mt_gstat) ? "BOT" : "",
		GMT_WR_PROT(buf.mt_gstat) ? "PROTECTED" : ""
        );
#endif

#ifdef DEV_BOM
   /* OSF1-ish system */
   printf("status: %s %s %s\n",
		~(DEV_OFFLINE & buf.mt_dsreg) ? "ONLINE" : "OFFLINE",
		(DEV_BOM & buf.mt_dsreg) ? "BOT" : "",
		(DEV_WRTLCK & buf.mt_dsreg) ? "PROTECTED" : ""
        );
#endif

   /* Solaris, minix, etc. */
   printf( "dsreg == 0x%x\n", buf.mt_dsreg  );
#endif

  return res;
}

int plain_tape_stat(filename, buf) 
     char *filename;
     struct stat *buf;
{
     return stat(filename, buf);
}

int plain_tape_access(filename, mode) 
     char *filename;
     int mode;
{
     return access(filename, mode);
}

int plain_tape_open(filename, mode)
     char *filename;
     int mode;
{
#ifdef HAVE_LINUX_ZFTAPE_H
    struct mtop mt;
#endif /* HAVE_LINUX_ZFTAPE_H */
    int ret = 0, delay = 2, timeout = 200;
    if (mode != O_RDONLY) {
	mode = O_RDWR;
    }
#if 0
    /* Since we're no longer using a special name for no-tape, we no
       longer need this */
    if (strcmp(filename, "/dev/null") == 0) {
	filename = "/dev/null";
    }
#endif
    do {
	ret = open(filename, mode, 0644);
	/* if tape open fails with errno==EAGAIN, EBUSY or EINTR, it
	 * is worth retrying a few seconds later.  */
	if (ret >= 0 ||
	    (1
#ifdef EAGAIN
	     && errno != EAGAIN
#endif
#ifdef EBUSY
	     && errno != EBUSY
#endif
#ifdef EINTR
	     && errno != EINTR
#endif
	     ))
	    break;
	sleep(delay);
	timeout -= delay;
	if (delay < 16)
	    delay *= 2;
    } while (timeout > 0);
    if (strcmp(filename, "/dev/null") == 0) {
	no_op_tapefd = ret;
    } else {
	no_op_tapefd = -1;
    }
#ifdef HAVE_LINUX_ZFTAPE_H
    /* 
     * switch the block size for the zftape driver (3.04d) 
     * (its default is 10kb and not TAPE_BLOCK_BYTES=32kb) 
     *        A. Gebhardt <albrecht.gebhardt@uni-klu.ac.at>
     */
    if (no_op_tapefd < 0 && ret >= 0 && is_zftape(filename) == 1)
	{
	    mt.mt_op = MTSETBLK;
	    mt.mt_count = TAPE_BLOCK_BYTES;
	    ioctl(ret, MTIOCTOP, &mt);    
	}
#endif /* HAVE_LINUX_ZFTAPE_H */
    return ret;
}


/*
=======================================================================
** now the generic routines
=======================================================================
*/

char *tape_rewind(devname)
char *devname;
{
    int fd;

    if((fd = tape_open(devname, O_RDONLY)) == -1) {
	errstr = newstralloc(errstr, "tape offline");
	return errstr;
    }

    if(tapefd_rewind(fd) == -1) {
	errstr = newstralloc2(errstr, "rewinding tape: ", strerror(errno));
	tapefd_close(fd);
	return errstr;
    }

    tapefd_close(fd);
    return NULL;
}

char *tape_unload(devname)
char *devname;
{
    int fd;

    if((fd = tape_open(devname, O_RDONLY)) == -1) {
	errstr = newstralloc(errstr, "tape offline");
	return errstr;
    }

    if(tapefd_unload(fd) == -1) {
	errstr = newstralloc2(errstr, "unloading tape: ", strerror(errno));
	tapefd_close(fd);
	return errstr;
    }

    tapefd_close(fd);
    return NULL;
}

char *tape_fsf(devname, count)
char *devname;
int count;
{
    int fd;
    char count_str[NUM_STR_SIZE];

    if((fd = tape_open(devname, O_RDONLY)) == -1) {
	errstr = newstralloc(errstr, "tape offline");
	return errstr;
    }

    if(tapefd_fsf(fd, count) == -1) {
	snprintf(count_str, sizeof(count_str), "%d", count);
	errstr = newvstralloc(errstr,
			      "fast-forward ", count_str, "files: ",
			      strerror(errno),
			      NULL);
	tapefd_close(fd);
	return errstr;
    }

    tapefd_close(fd);
    return NULL;
}

char *tapefd_rdlabel(tapefd, datestamp, label)
int tapefd;
char **datestamp, **label;
{
    int rc;
    char buffer[TAPE_BLOCK_BYTES];
    dumpfile_t file;

    amfree(*datestamp);
    amfree(*label);

    if(tapefd_rewind(tapefd) == -1) {
	errstr = newstralloc2(errstr, "rewinding tape: ", strerror(errno));
	return errstr;
    }

    if((rc = tapefd_read(tapefd, buffer, sizeof(buffer))) == -1) {
	errstr = newstralloc2(errstr, "reading label: ", strerror(errno));
	return errstr;
    }

    /* make sure buffer is null-terminated */
    if(rc == sizeof(buffer)) rc--;
    buffer[rc] = '\0';

    if (tapefd == no_op_tapefd) {
	strcpy(file.datestamp, "X");
	strcpy(file.name, "/dev/null");
    } else {
	parse_file_header(buffer, &file, sizeof(buffer));
	if(file.type != F_TAPESTART) {
	    errstr = newstralloc(errstr, "not an amanda tape");
	    return errstr;
	}
    }
    *datestamp = stralloc(file.datestamp);
    *label = stralloc(file.name);

    return NULL;
}


char *tape_rdlabel(devname, datestamp, label)
char *devname, **datestamp, **label;
{
    int fd;

    if((fd = tape_open(devname, O_RDONLY)) == -1) {
	errstr = newstralloc(errstr, "tape offline");
	return errstr;
    }

    if(tapefd_rdlabel(fd, datestamp, label) != NULL) {
	tapefd_close(fd);
	return errstr;
    }

    tapefd_close(fd);
    return NULL;
}


char *tapefd_wrlabel(tapefd, datestamp, label)
int tapefd;
char *datestamp, *label;
{
    int rc;
    char buffer[TAPE_BLOCK_BYTES];
    dumpfile_t file;

    if(tapefd_rewind(tapefd) == -1) {
	errstr = newstralloc2(errstr, "rewinding tape: ", strerror(errno));
	return errstr;
    }

    fh_init(&file);
    file.type=F_TAPESTART;
    strncpy(file.datestamp, datestamp, sizeof(file.datestamp)-1);
    file.datestamp[sizeof(file.datestamp)-1] = '\0';
    strncpy(file.name, label, sizeof(file.name)-1);
    file.name[sizeof(file.name)-1] = '\0';
    write_header(buffer,&file,sizeof(buffer));

    if((rc = tapefd_write(tapefd, buffer, sizeof(buffer))) != sizeof(buffer)) {
	errstr = newstralloc2(errstr, "writing label: ",
			      (rc != -1) ? "short write" : strerror(errno));
	return errstr;
    }

    return NULL;
}


char *tape_wrlabel(devname, datestamp, label)
char *devname, *datestamp, *label;
{
    int fd;

    if((fd = tape_open(devname, O_WRONLY)) == -1) {
	if(errno == EACCES) {
	    errstr = newstralloc(errstr,
				 "writing label: tape is write-protected");
	} else {
	    errstr = newstralloc2(errstr, "writing label: ", strerror(errno));
	}
	tapefd_close(fd);
	return errstr;
    }

    if(tapefd_wrlabel(fd, datestamp, label) != NULL) {
	tapefd_close(fd);
	return errstr;
    }

    tapefd_close(fd);
    return NULL;
}


char *tapefd_wrendmark(tapefd, datestamp)
int tapefd;
char *datestamp;
{
    int rc;
    char buffer[TAPE_BLOCK_BYTES];
    dumpfile_t file;

    fh_init(&file);
    file.type=F_TAPEEND;
    strncpy(file.datestamp, datestamp, sizeof(file.datestamp)-1);
    file.datestamp[sizeof(file.datestamp)-1] = '\0';
    write_header(buffer, &file,sizeof(buffer));

    if((rc = tapefd_write(tapefd, buffer, sizeof(buffer))) != sizeof(buffer)) {
	errstr = newstralloc2(errstr, "writing endmark: ",
			      (rc != -1) ? "short write" : strerror(errno));
	return errstr;
    }

    return NULL;
}


char *tape_wrendmark(devname, datestamp)
    char *devname, *datestamp;
{
    int fd;

    if((fd = tape_open(devname, O_WRONLY)) == -1) {
	errstr = newstralloc2(errstr, "writing endmark: ",
			      (errno == EACCES) ? "tape is write-protected"
						: strerror(errno));
	return errstr;
    }

    if(tapefd_wrendmark(fd, datestamp) != NULL) {
	tapefd_close(fd);
	return errstr;
    }

    tapefd_close(fd);
    return NULL;
}


char *tape_writable(devname)
char *devname;
{
    int fd;

    /* first, make sure the file exists and the permissions are right */

    if(tape_access(devname, R_OK|W_OK) == -1) {
	errstr = newstralloc(errstr, strerror(errno));
	return errstr;
    }

    if((fd = tape_open(devname, O_WRONLY)) == -1) {
	errstr = newstralloc(errstr,
			     (errno == EACCES) ? "tape write-protected"
					       : strerror(errno));
	return errstr;
    }

    if(tapefd_close(fd) == -1) {
	errstr = newstralloc(errstr, strerror(errno));
	return errstr;
    }

    return NULL;
}

#ifdef HAVE_LINUX_ZFTAPE_H
/*
 * is_zftape(filename) checks if filename is a valid ftape device name. 
 */
int is_zftape(filename)
     const char *filename;
{
    if (strncmp(filename, "/dev/nftape", 11) == 0) return(1);
    if (strncmp(filename, "/dev/nqft",    9) == 0) return(1);
    if (strncmp(filename, "/dev/nrft",    9) == 0) return(1);
    return(0);
}
#endif /* HAVE_LINUX_ZFTAPE_H */


char *tape_status(devname)
char *devname;
{
    int fd;

    if((fd = tape_open(devname, O_RDONLY)) == -1) {
	errstr = newstralloc(errstr, "tape offline or not readable");
	return errstr;
    }

    if(tapefd_status(fd) == -1) {
	errstr = newstralloc2(errstr, "tape status: ", strerror(errno));
	tapefd_close(fd);
	return errstr;
    }

    tapefd_close(fd);
    return NULL;
}
