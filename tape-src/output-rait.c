#include "output-rait.h"
#include <string.h>
#include <sys/errno.h>
#include <fcntl.h>
#include <stdio.h>

/*
** RAIT -- redundant array of (inexpensive?) tapes
**
** Author: Marc Mengel <mengel@fnal.gov>
**
** This package provides for striping input/output across
** multiple tape drives.
**
		 Table of Contents

  rait.c..................................................1
	MAX_RAITS.........................................2
        RAIT_OFFSET.......................................2
        rait_table........................................2
	rait_open(char *dev, int flags, int mode).........2
	rait_close(int fd)................................3
	rait_lseek(int fd, long pos, int whence)..........4
	rait_write(int fd, const char *buf, int len) .....5
	rait_read(int fd, char *buf, int len).............6
	rait_ioctl(int fd, int op, void *p)...............8
	rait_access(devname, R_OK|W_OK)...................8
	rait_stat(devname, struct statbuf*)...............8
	rait_copy(char *f1, char *f2).....................9
	ifndef NO_AMANDA
	    rait_tapefd_fsf(rait_tapefd, count)..........10
	    rait_tapefd_rewind(rait_tapefd)..............10
	    rait_tapefd_resetofs(rait_tapefd)............10
	    rait_tapefd_unload(rait_tapefd)..............10
	    rait_tapefd_status(rait_tapefd)..............10
	    rait_tapefd_weof(rait_tapefd, count).........10

	

   rait.h.................................................1
	MAXRAIT...........................................1
	MAXBUFSIZE .......................................1
        typedef RAIT......................................1
        ifdef RAIT_REDIRECT...............................1
             open.........................................1
	     close........................................1
             ioctl........................................1
	     read.........................................1
             write........................................1
*/

/**/

/*
** rait_open takes a string like:
** "/dev/rmt/tps0d{3,5,7,19}nrnsv"
** and opens
** "/dev/rmt/tps0d3nrnsv"
** "/dev/rmt/tps0d5nrnsv"
** "/dev/rmt/tps0d7nrnsv"
** "/dev/rmt/tps0d19nrnsv"
** as a RAIT.
**
** If it has no curly brace, we treat it as a plain device,
** and do a normal open, and do normal operations on it; we
** distinguish RAIT descriptors from regular file descriptors
** by whether they greater than RAIT_OFFSET
*/

#ifdef RAIT_DEBUG
#include <stdio.h>
#define rait_debug (0!=getenv("RAIT_DEBUG"))&&fprintf
#else
#define rait_debug (void)
#define stderr 1
#endif

#define MAX_RAITS 20			/* maximum simul. RAIT sets */
#define RAIT_OFFSET 2048		/* offset of RAIT fd's */
static RAIT rait_table[MAX_RAITS];	/* table to keep track of RAITS */

extern int errno;

int
rait_open(char *dev, int flags, int mode) {
    char *path;			/* pathname */
    int i;			/* index into drive slots in RAIT */
    RAIT *res;			/* resulting RAIT structure */
    char *p;			/* temp char pointer */

    if (0 == (p = strchr(dev, '{'))) {

	/*
        ** no percent sign, so a normal open...
        */
	return open(dev,flags,mode);

    } else {
	rait_debug(stderr,"rait_open( %s, %d, %d, )\n", dev, flags, mode);
	/* copy the strings so we can scribble on the copy */
        dev = rait_init_namelist(dev);
	if (0 == dev) {
	    return -1;
        }

	for ( i = 0; i < MAX_RAITS && rait_table[i].nopen; i++) {
	    ;
	}

        if( i < MAX_RAITS ) {
	    res = &rait_table[i];
        } else {
	    errno = ENOMEM;
	    free(dev);
	    return -1;
	}

	memset(res, 0, sizeof(RAIT));
	res->nopen = 1;


	while ( path = rait_next_name() ) {
	    res->fds[ res->nfds ] = open(path,flags,mode);
	    rait_debug(stderr,"rait_open:opening %s yeilds %d\n",
			path, res->fds[res->nfds] );
	    if ( res->fds[res->nfds] < 0 ) {
		rait_close(RAIT_OFFSET + (res - rait_table));
		return -1;
	    }
	    res->nfds++;
	}
    }

    /* clean up our copied string */
    free(dev);

    rait_debug(stderr,"rait_open:returning %d\n",
    	RAIT_OFFSET + (res - rait_table));

    return RAIT_OFFSET + (res - rait_table);
}

static char 
    *fmt, 		/* component of sprintf format */
    *strlist, 		/* pointer to list of substrings */
    *str;		/* pointer to current substring */
static char fmt_buf[255];/* sprintf format for generating paths */
static char path[255];	/* individual path name */

char *
rait_init_namelist(char * dev) {

    dev = strdup(dev);
    if (0 == dev) { 
	return 0;
    }


    if ( 0 == strchr(dev, '{') || 0 == strchr(dev, '}') ) {
	/* we dont have a {} pair */
	errno = EINVAL;
	return 0;
    }

    /* make a printf format of whats outside the curlys */

    strlist = strrchr(dev, '{');
    fmt = strrchr(dev,'}') + 1;

    *strlist = 0;

    strcpy(fmt_buf, dev);
    strcat(fmt_buf, "%s");
    if ( *fmt ) {
	strcat(fmt_buf, fmt);
    }
    fmt = fmt_buf;

    /* and find the start of the list of substititions */
    str = strlist = strlist + 1;
    return dev;
}

char *
rait_next_name() {
    if (0 != (strlist = strchr(str, ',')) || 0 != (strlist = strchr(str, '}'))){
    
	*strlist = 0;
	strlist++;

	/* 
	** we have one string picked out, sprintf() it into the
	** buffer and open it...
	*/
	sprintf(path,fmt,str);
        str = strlist;
        return path;
    } else {
	return 0;
    }
}

/*
** close everything we opened and free our memory.
*/
int 
rait_close(int fd) {
    int i;			/* index into RAIT drives */
    int res;			/* result from close */
    RAIT *pr;			/* RAIT entry from table */

    if ( fd < RAIT_OFFSET ) {
	return close(fd);
    } else {
	pr = rait_table + (fd - RAIT_OFFSET);
        res = 0;
	for( i = 0; i < pr->nfds; i++ ) {
	    res |= close(pr->fds[i]);
	}
	pr->nopen = 0;
    }
    return res;
}

/**/

/*
** seek out to the nth byte on the RAIT set.
** this is assumed to be evenly divided across all the stripes
*/
int 
rait_lseek(int fd, long pos, int whence) {
    int i;			/* drive number in RAIT */
    long res, 			/* result of lseeks */
	 total;			/* total of results */
    RAIT *pr;			/* RAIT slot in table */
    
    if ( fd < RAIT_OFFSET ) {
	return lseek(fd, pos, whence);
    } else {
	pr = rait_table + (fd - RAIT_OFFSET);
	if ((pos % (pr->nfds-1)) != 0) {
	    errno = EDOM;
	    return -1;
	}
	total = 0;
	pos = pos / pr->nfds;
	for( i = 0; i < pr->nfds; i++ ) {
	    res = lseek(pr->fds[i], pos, whence);
	    total += res;
	    if (res < 0) {
		return res;
	    }
	}
	return total;
    }
}

/**/

/*
** if we only have one stream, just do a write, 
** otherwise compute an xor sum, and do several
** writes...
*/
int 
rait_write(int fd, const char *buf, int len) {
    int i, j;		/* drive number, byte offset */
    RAIT *pr;		/* RAIT structure for this RAIT */
    int res, total = 0;

    if (fd < RAIT_OFFSET ) {

	return write(fd, buf, len);

    } else {

	rait_debug(stderr, "rait_write(%d,%lx,%d)\n",fd,buf,len);
	pr = rait_table + (fd - RAIT_OFFSET);

	/* need to be able to slice it up evenly... */
	if ((len % (pr->nfds - 1)) != 0) {
	    errno = EDOM;
	    return -1;
	}

	/* each slice gets an even portion */
	len = len / (pr->nfds - 1);

	/* but it has to fit in our buffer */
	if (len > MAXBUFSIZE) {
	    errno = EINVAL;
	    return -1;
        }

	/* compute the sum */
	memset(pr->xorbuf, 0, len);
	for( i = 0; i < (pr->nfds - 1); i++ ) {
	    for( j = 0; j < len; j++ ) {
		pr->xorbuf[j] ^= buf[len * i + j];
	    }
	}

        /* write the chunks in the main buffer */
	for( i = 0; i < (pr->nfds - 1); i++ ) {
	    res = write(pr->fds[i], buf + len*i , len);
	    rait_debug(stderr, "rait_write: write(%d,%lx,%d) returns %d\n",
		    pr->fds[i], buf + len*i , len, res);
	    if (res < 0) return res;
	    total += res;
	}
        /* write the sum, don't include it in the total bytes written */
	res = write(pr->fds[i], pr->xorbuf, len);
	if (res < 0) return res;
    }
    rait_debug(stderr, "rait_write: returning %d\n", total);

    return total;
}

/**/

/*
** once again, if there is one data stream do a read, otherwise
** do all n reads, and if any of the first n - 1 fail, compute
** the missing block from the other three, then return the data.
** there's some silliness here for reading tape with bigger buffers
** than we wrote with, (thus the extra bcopys down below).  On disk if
** you read with a bigger buffer size than you wrote with, you just 
** garble the data...
*/
int 
rait_read(int fd, char *buf, int len) {
    int nerrors, 
        neofs, 
        total, 
        errorblock;
    int readres[MAXRAIT];
    int i,j,res;
    RAIT *pr;

    if (fd < RAIT_OFFSET ) {
	return read(fd, buf, len);
    } else {
	rait_debug(stderr, "rait_read(%d,%lx,%d)\n",fd,buf,len);
	pr = rait_table + (fd - RAIT_OFFSET);

	nerrors = 0;
        neofs = 0;
	total = 0;
	/* once again , we slice it evenly... */
	if ((len % (pr->nfds - 1)) != 0) {
	    errno = EDOM;
	    return -1;
	}
	len = len / (pr->nfds - 1);

	/* try all the reads, save the result codes */
	/* count the eof/errors */
	for( i = 0; i < (pr->nfds - 1); i++ ) {
	    readres[i] = read(pr->fds[i], buf + len*i , len);
	    rait_debug(stderr, "rait_read: read on fd %d returns %d",fd,readres[i]);
	    if ( readres[i] <= 0 ) {
		nerrors++;
		errorblock = i;
	    }
            if ( readres[i] == 0 ) {
		neofs++;
            }
	}
	readres[i] = read(pr->fds[i], pr->xorbuf , len);

	/* don't count this as an error read if none of the
           data blocks failed ... */

	if ( nerrors > 0 && readres[i] <= 0 ) {
	    nerrors++;
	    errorblock = i;
	}

	if ( readres[i] == 0 ) {
	    neofs++;
	}

	/* 
	** now decide what "really" happened --
	** all n getting eof is a "real" eof
	** just one getting an error/eof is recoverable
	** anything else fails
	*/

	if (neofs == pr->nfds) {
	    return 0;
        }
	if (nerrors > 1 ) {
	     return -1;
	}

	/*
        ** so now if we failed on a data block, we need to do a recovery
        ** if we failed on the xor block -- who cares?
	*/
	if (nerrors == 1 && errorblock != (pr->nfds - 1)) {

	    /* the reads were all *supposed* to be the same size, so... */
	    if (0 == errorblock ) {
		readres[0] = readres[1];
	    } else {
		readres[errorblock] = readres[0];
	    }

	    /* fill it in first with the xor sum */
	    for( j = 0; j < len ; j++ ) {
		buf[j + len * errorblock] = pr->xorbuf[j];
            }

	    /* xor back out the other blocks */
	    for( i = 0; i < (pr->nfds - 1); i++ ) {
		if( i != errorblock ) {
		    for( j = 0; j < len ; j++ ) {
			    buf[j + len * errorblock] ^= buf[j + len * i];
		    }
		}
	    }
	    /* there, now the block is back as if it never failed... */
	}

	/* pack together partial reads... */
	total = readres[0];
	for( i = 1; i < (pr->nfds - 1); i++ ) {
	    if (total != len * i) {
		for( j = 0; j < readres[i]; j++ ) {
		    (buf+total)[j] = (buf + len*i)[j];
		}
            }
	    total += readres[i];
	}
	rait_debug(stderr, "rait_read: returning %d",total);
	return total;
    }
}

/**/

int rait_ioctl(int fd, int op, void *p) {
    int i, res;
    RAIT *pr;
    int errors = 0;

    if (fd < RAIT_OFFSET) {
	return ioctl(fd, op, p);
    } else {
	pr = rait_table + (fd - RAIT_OFFSET);
	for( i = 0; i < pr->nfds ; i++ ) {
	    res = ioctl(pr->fds[i], op, p);
	    if ( res != 0 ) { 
		 errors++;
		 if (errors > 1) {
		    return res;
		 } else {
                    res = 0;
                 }
	    }
	}
    }
    return res;
}

/*
** access() all the devices, returning if any fail
*/
int rait_access(char *devname, int flags) {
    char *path;
    int res;
    char *p;			/* temp char pointer */
    RAIT *pr;

    if (0 == (p = strchr(devname, '{'))) {
        return access(devname, flags);
    } else {
	devname = rait_init_namelist(devname);
	if ( 0 == devname ) { 
	   free(devname);
	   return -1;
	}
	while( path = rait_next_name() ) {
	   res = access(path, flags);
	    rait_debug(stderr,"rait_access:access( %s, %d ) yeilds %d\n",
			path, flags, res );
	   if (res < 0) { 
	        free(devname);
		return res;
           }
        }
        free(devname);
	return 0;
    }
}

/*
** stat all the devices, returning the last one unless one fails
*/
int rait_stat(char *devname, struct stat *buf) {
    char *path;
    int res;
    char *p;			/* temp char pointer */
    RAIT *pr;

    if (0 == (p = strchr(devname, '{'))) {
        return stat(devname, buf);
    } else {
	devname = rait_init_namelist(devname);
	if ( 0 == devname ) { 
	   free(devname);
	   return -1;
	}
	while( path = rait_next_name() ) {
	   res = stat(path, buf);
	   rait_debug(stderr,"rait_stat:stat( %s, %lx ) yeilds %d\n",
			path, buf, res );
	   if (res < 0) { 
	        free(devname);
		return res;
           }
        }
        free(devname);
	return 0;
    }
}

/**/

rait_copy(char *f1, char *f2) {
    int t1, t2;
    int len, wres;
    static char buf[MAXBUFSIZE*MAXRAIT];
    int i;

    t1 = rait_open(f1,O_RDONLY,0644);
    t2 = rait_open(f2,O_CREAT|O_RDWR,0644);
    if (t1 == 0 || t2 == 0) {
	return -1;
    }
    do {
	len = rait_read(t1,buf,21);
	if (len > 0 ) {
	    wres = rait_write(t2, buf, len);
	    if (wres < 0) {
		return wres;
	    }
	}
    } while( len > 0 );
    if (len < 0) {
	return len;
    }
    rait_close(t1);
    rait_close(t2);
    return 0;
}

/**/
#ifndef NO_AMANDA

/*
** Amanda Tape API routines:
*/

int rait_tapefd_fsf(int rait_tapefd, int count) {
    return tapefd_fsf_ioctl(rait_tapefd, count, &rait_ioctl);
}

int rait_tapefd_rewind(int rait_tapefd) {
    return tapefd_rewind_ioctl(rait_tapefd, &rait_ioctl);
}

void rait_tapefd_resetofs(int rait_tapefd) {
    rait_lseek(rait_tapefd,  0L, SEEK_SET);
}

int rait_tapefd_unload(int rait_tapefd) {
    return tapefd_unload_ioctl(rait_tapefd, &rait_ioctl);
}

int rait_tapefd_status(int rait_tapefd) {
    return tapefd_status_ioctl(rait_tapefd, &rait_ioctl);
}

int rait_tapefd_weof(int rait_tapefd, int count) {
    return tapefd_weof(rait_tapefd, count, &rait_ioctl);
}

int rait_tape_open(char *name,  int mode) {
    return rait_open(name, mode, 0644);
}

#endif
