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
 * $Id: dgram.c,v 1.11.2.5 2001/06/18 22:23:19 jrjackson Exp $
 *
 * library routines to marshall/send, recv/unmarshall UDP packets
 */
#include "amanda.h"
#include "dgram.h"
#include "util.h"

void dgram_socket(dgram, socket)
dgram_t *dgram;
int socket;
{
    if(socket < 0 || socket >= FD_SETSIZE) {
	error("dgram_socket: socket %d out of range (0 .. %d)\n",
	      socket, FD_SETSIZE-1);
    }
    dgram->socket = socket;
}


int dgram_bind(dgram, portp)
dgram_t *dgram;
int *portp;
{
    int s, len;
    struct sockaddr_in name;
    int save_errno;

    if((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
	save_errno = errno;
	dbprintf(("%s: dgram_bind: socket() failed: %s\n",
		  get_pname(),
		  strerror(save_errno)));
	errno = save_errno;
	return -1;
    }
    if(s < 0 || s >= FD_SETSIZE) {
	dbprintf(("%s: dgram_bind: socket out of range: %d\n",
		  get_pname(),
		  s));
	aclose(s);
	errno = EMFILE;				/* out of range */
	return -1;
    }

    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;

    /*
     * If a port range was specified, we try to get a port in that
     * range first.  Next, we try to get a reserved port.  If that
     * fails, we just go for any port.
     *
     * It is up to the caller to make sure we have the proper permissions
     * to get the desired port, and to make sure we return a port that
     * is within the range it requires.
     */
#ifdef UDPPORTRANGE
    if (bind_portrange(s, &name, UDPPORTRANGE) == 0)
	goto out;
#endif

    if (bind_portrange(s, &name, 512, IPPORT_RESERVED - 1) == 0)
	goto out;

    name.sin_port = INADDR_ANY;
    if (bind(s, (struct sockaddr *)&name, sizeof name) == -1) {
	save_errno = errno;
	dbprintf(("%s: dgram_bind: bind(INADDR_ANY) failed: %s\n",
		  get_pname(),
		  strerror(save_errno)));
	errno = save_errno;
	aclose(s);
	return -1;
    }

out:
    /* find out what name was actually used */

    len = sizeof name;
    if(getsockname(s, (struct sockaddr *)&name, &len) == -1) {
	save_errno = errno;
	dbprintf(("%s: dgram_bind: getsockname() failed: %s\n",
		  get_pname(),
		  strerror(save_errno)));
	errno = save_errno;
	aclose(s);
	return -1;
    }
    *portp = ntohs(name.sin_port);
    dgram->socket = s;
    dbprintf(("%s: dgram_bind: socket bound to %s.%d\n",
	      get_pname(),
	      inet_ntoa(name.sin_addr),
	      *portp));
    return 0;
}


int dgram_send_addr(addr, dgram)
struct sockaddr_in addr;
dgram_t *dgram;
{
    int s;
    int socket_opened;
    struct sockaddr_in addr_save;
    int save_errno;
    int max_wait;
    int wait_count;

    if(dgram->socket != -1) {
	s = dgram->socket;
	socket_opened = 0;
    } else {
	if((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
	    save_errno = errno;
	    dbprintf(("%s: dgram_send_addr: socket() failed: %s\n",
		      get_pname(),
		      strerror(save_errno)));
	    errno = save_errno;
	    return -1;
	}
	socket_opened = 1;
    }

    if(s < 0 || s >= FD_SETSIZE) {
	dbprintf(("%s: dgram_send_addr: socket out of range: %d\n",
		  get_pname(),
		  s));
	if(socket_opened) {
	    aclose(s);
	}
	errno = EMFILE;				/* out of range */
	return -1;
    }

    memcpy(&addr_save, &addr, sizeof(addr));
    max_wait = 300 / 5;				/* five minutes */
    wait_count = 0;
    while(sendto(s,
		 dgram->data,
		 dgram->len,
		 0, 
		 (struct sockaddr *)&addr,
		 sizeof(struct sockaddr_in)) == -1) {
#ifdef ECONNREFUSED
	if(errno == ECONNREFUSED && wait_count++ < max_wait) {
	    sleep(5);
	    dbprintf(("%s: dgram_send_addr: sendto(%s.%d): retry %d after ECONNREFUSED\n",
		      get_pname(),
		      inet_ntoa(addr_save.sin_addr),
		      (int) ntohs(addr.sin_port),
		      wait_count));
	    continue;
	}
#endif
	save_errno = errno;
	dbprintf(("%s: dgram_send_addr: sendto(%s.%d) failed: %s \n",
		  get_pname(),
		  inet_ntoa(addr_save.sin_addr),
		  (int) ntohs(addr.sin_port),
		  strerror(save_errno)));
	errno = save_errno;
        return -1;
    }

    if(socket_opened) {
	if(close(s) == -1) {
	    dbprintf(("%s: dgram_send_addr: close(%s.%d): failed: %s\n",
		      get_pname(),
		      inet_ntoa(addr_save.sin_addr),
		      (int) ntohs(addr.sin_port),
		      strerror(save_errno)));
	    return -1;
	}
	s = -1;
    }

    return 0;
}


int dgram_send(hostname, port, dgram)
char *hostname;
int port;
dgram_t *dgram;
{
    struct sockaddr_in name;
    struct hostent *hp;
    int save_errno;

    if((hp = gethostbyname(hostname)) == 0) {
	save_errno = errno;
	dbprintf(("%s: dgram_send: gethostbyname(%s) failed\n",
		  get_pname(),
		  hostname));
	errno = save_errno;
	return -1;
    }
    memcpy(&name.sin_addr, hp->h_addr, hp->h_length);
    name.sin_family = AF_INET;
    name.sin_port = htons(port);

    return dgram_send_addr(name, dgram);
}


int dgram_recv(dgram, timeout, fromaddr)
dgram_t *dgram;
int timeout;
struct sockaddr_in *fromaddr;
{
    fd_set ready;
    struct timeval to;
    int size, addrlen, sock;
    int nfound;
    int save_errno;

    sock = dgram->socket;

    FD_ZERO(&ready);
    FD_SET(sock, &ready);
    to.tv_sec = timeout;
    to.tv_usec = 0;

    nfound = select(sock+1, (SELECT_ARG_TYPE *)&ready, NULL, NULL, &to);
    if(nfound <= 0 || !FD_ISSET(sock, &ready)) {
	save_errno = errno;
	if(nfound < 0) {
	    dbprintf(("%s: dgram_recv: select() failed: %s\n",
		      get_pname(),
		      strerror(save_errno)));
	} else if(nfound == 0) {
	    dbprintf(("%s: dgram_recv: timeout after %d second%s\n",
		      get_pname(),
		      timeout,
		      (timeout == 1) ? "" : "s"));
	    nfound = 0;
	} else if (!FD_ISSET(sock, &ready)) {
	    int i;

	    for(i = 0; i < sock + 1; i++) {
		if(FD_ISSET(i, &ready)) {
		    dbprintf(("%s: dgram_recv: got fd %d instead of %d\n",
			      get_pname(),
			      i,
			      sock));
		}
	    }
	    save_errno = EBADF;
	    nfound = -1;
	}
	errno = save_errno;
	return nfound;
    }

    addrlen = sizeof(struct sockaddr_in);
    size = recvfrom(sock, dgram->data, MAX_DGRAM, 0,
		    (struct sockaddr *)fromaddr, &addrlen);
    if(size == -1) {
	save_errno = errno;
	dbprintf(("%s: dgram_recv: recvfrom() failed: %s\n",
		  get_pname(),
		  strerror(save_errno)));
	errno = save_errno;
	return -1;
    }
    dgram->len = size;
    dgram->data[size] = '\0';
    dgram->cur = dgram->data;
    return size;
}


void dgram_zero(dgram)
dgram_t *dgram;
{
    dgram->cur = dgram->data;
    dgram->len = 0;
    *(dgram->cur) = '\0';
}

#if defined(USE_DBMALLOC)
dgram_t *dbmalloc_dgram_alloc(s, l)
char *s;
int l;
#else
dgram_t *dgram_alloc()
#endif
{
    dgram_t *p;

    malloc_enter(dbmalloc_caller_loc(s, l));
    p = (dgram_t *) alloc(sizeof(dgram_t));
    dgram_zero(p);
    p->socket = -1;
    malloc_leave(dbmalloc_caller_loc(s, l));

    return p;
}


void dgram_cat(dgram, str)
dgram_t *dgram;
const char *str;
{
    int len = strlen(str);

    if(dgram->len + len > MAX_DGRAM) len = MAX_DGRAM - dgram->len;
    strncpy(dgram->cur, str, len);
    dgram->cur += len;
    dgram->len += len;
    *(dgram->cur) = '\0';
}

void dgram_eatline(dgram)
dgram_t *dgram;
{
    char *p = dgram->cur;
    char *end = dgram->data + dgram->len;

    while(p < end && *p && *p != '\n') p++;
    if(*p == '\n') p++;
    dgram->cur = p;
}
