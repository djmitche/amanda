/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1999 University of Maryland at College Park
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
 * $Id: util.c,v 1.13 2005/10/29 22:29:17 martinea Exp $
 */

#include "amanda.h"
#include "util.h"

/*
 * Keep calling read() until we've read buflen's worth of data, or EOF,
 * or we get an error.
 *
 * Returns the number of bytes read, 0 on EOF, or negative on error.
 */
ssize_t
fullread(fd, vbuf, buflen)
    int fd;
    void *vbuf;
    size_t buflen;
{
    ssize_t nread, tot = 0;
    char *buf = vbuf;	/* cast to char so we can ++ it */

    while (buflen > 0) {
	nread = read(fd, buf, buflen);
	if (nread < 0)
	    return (nread);
	if (nread == 0)
	    break;
	tot += nread;
	buf += nread;
	buflen -= nread;
    }
    return (tot);
}

/*
 * Keep calling write() until we've written buflen's worth of data,
 * or we get an error.
 *
 * Returns the number of bytes written, or negative on error.
 */
ssize_t
fullwrite(fd, vbuf, buflen)
    int fd;
    const void *vbuf;
    size_t buflen;
{
    ssize_t nwritten, tot = 0;
    const char *buf = vbuf;	/* cast to char so we can ++ it */

    while (buflen > 0) {
	nwritten = write(fd, buf, buflen);
	if (nwritten < 0)
	    return (nwritten);
	tot += nwritten;
	buf += nwritten;
	buflen -= nwritten;
    }
    return (tot);
}

/*
 * Bind to a port in the given range.  Takes a begin,end pair of port numbers.
 *
 * Returns negative on error (EGAIN if all ports are in use).  Returns 0
 * on success.
 */
int
bind_portrange(s, addrp, first_port, last_port, proto)
    int s;
    struct sockaddr_in *addrp;
    int first_port, last_port;
    char *proto;
{
    int port, cnt;
    const int num_ports = last_port - first_port + 1;
    int save_errno;
    struct servent *servPort;

    assert(first_port > 0 && first_port <= last_port && last_port < 65536);

    /*
     * We pick a different starting port based on our pid and the current
     * time to avoid always picking the same reserved port twice.
     */
    port = ((getpid() + time(0)) % num_ports) + first_port;
    /*
     * Scan through the range, trying all available ports that are either 
     * not taken in /etc/services or registered for *amanda*.  Wrap around
     * if we don't happen to start at the beginning.
     */
    for (cnt = 0; cnt < num_ports; cnt++) {
	servPort = getservbyport(htons(port), proto);
	if((servPort == NULL) || strstr(servPort->s_name, "amanda")){
	    dbprintf(("%s: bind_portrange2: trying port=%d\t",
		      debug_prefix_time(NULL), port));
	    addrp->sin_port = htons(port);
	    if (bind(s, (struct sockaddr *)addrp, sizeof(*addrp)) >= 0)
		return 0;
	    /*
	     * If the error was something other then port in use, stop.
	     */
	    if (errno != EADDRINUSE)
		break;
	}
	if (++port > last_port)
	    port = first_port;
    }
    if (cnt == num_ports) {
	dbprintf(("%s: bind_portrange: all ports between %d and %d busy\n",
		  debug_prefix_time(NULL),
		  first_port,
		  last_port));
	errno = EAGAIN;
    } else if (last_port < IPPORT_RESERVED
	       && getuid() != 0
	       && errno == EACCES) {
	/*
	 * Do not bother with an error message in this case because it
	 * is expected.
	 */
    } else {
	save_errno = errno;
	dbprintf(("%s: bind_portrange: port %d: %s\n",
		  debug_prefix_time(NULL),
		  port,
		  strerror(errno)));
	errno = save_errno;
    }
    return -1;
}

/*
 * Construct a datestamp (YYYYMMDD) from a time_t.
 */
char *
construct_datestamp(t)
    time_t *t;
{
    struct tm *tm;
    char datestamp[3*NUM_STR_SIZE];
    time_t when;

    if(t == NULL) {
	when = time((time_t *)NULL);
    } else {
	when = *t;
    }
    tm = localtime(&when);
    snprintf(datestamp, sizeof(datestamp),
             "%04d%02d%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
    return stralloc(datestamp);
}

/*
 * Construct a timestamp (YYYYMMDDHHMMSS) from a time_t.
 */
char *
construct_timestamp(t)
    time_t *t;
{
    struct tm *tm;
    char timestamp[6*NUM_STR_SIZE];
    time_t when;

    if(t == NULL) {
	when = time((time_t *)NULL);
    } else {
	when = *t;
    }
    tm = localtime(&when);
    snprintf(timestamp, sizeof(timestamp),
	     "%04d%02d%02d%02d%02d%02d",
	     tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
	     tm->tm_hour, tm->tm_min, tm->tm_sec);
    return stralloc(timestamp);
}
