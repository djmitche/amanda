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
 * $Id: dgram.c,v 1.9 1998/07/04 00:18:39 oliva Exp $
 *
 * library routines to marshall/send, recv/unmarshall UDP packets
 */
#include "amanda.h"
#include "dgram.h"

int bind_reserved(sock, addrp)
int sock;
struct sockaddr_in *addrp;
{
/*
 * When using kerberos, we don't care about reserved ports or not.
 */
#   define FIRST_PORT 512
#   define NUM_PORTS		(IPPORT_RESERVED-FIRST_PORT)
    int port, count;
    static int port_base = FIRST_PORT;

    /* 
     * we pick a different starting point based on our pid to avoid always
     * picking the same reserved port twice.
     */

    port = FIRST_PORT + ((getpid() + port_base) % NUM_PORTS);

    for(count = 0; count < NUM_PORTS; count++) {
	addrp->sin_port = htons(port);
	if(bind(sock, (struct sockaddr *)addrp, sizeof(struct sockaddr_in)) 
	   != -1) break;
	else if(errno != EADDRINUSE) {
	    /* something weird, probably EACCES */
	    return -1;
	}
	if(++port == IPPORT_RESERVED)
	    port = FIRST_PORT;
    }

    if(count >= NUM_PORTS) {
	/* all ports busy */
	errno = EAGAIN;
	return -1;
    }
    port_base = port + 1;
    return 0;
}


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

    if((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) 
	return -1;
    if(s < 0 || s >= FD_SETSIZE) {
	aclose(s);
	errno = EMFILE;				/* out of range */
	return -1;
    }

    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_addr.s_addr = INADDR_ANY;

/*
 * under krb4, it really doesn't matter where you're coming from, since
 * exchanges are all wrapped in kerberos bits.  It actually causes a problem
 * for sites that block lower ports at a firewall.  perhaps this should just
 * be configured to try to optionally bind to a compile time or config file
 * range.  next version of amanda...  XXX  people mixing bsd-auth and
 * krb4-auth run into problems here.
 */
#ifndef KRB4_SECURITY
    if(geteuid() == 0) {
	if(bind_reserved(s, &name) == -1) {
	    aclose(s);
	    return -1;
	}
    }
    else {
#else
   { /* pesky language symantics */
#endif
	/* pick any available non-reserved port */
	name.sin_port = INADDR_ANY;
	if(bind(s, (struct sockaddr *)&name, sizeof name) == -1) {
	    aclose(s);
	    return -1;
	}
    }

    /* find out what name was actually used */

    len = sizeof name;
    if(getsockname(s, (struct sockaddr *)&name, &len) == -1)
	return -1;
    *portp = ntohs(name.sin_port);
    dgram->socket = s;

    return 0;
}


int dgram_send_addr(addr, dgram)
struct sockaddr_in addr;
dgram_t *dgram;
{
    int s;

    if(dgram->socket != -1)
	s = dgram->socket;
    else if((s = socket(AF_INET, SOCK_DGRAM, 0)) == -1) 
	return -1;

    if(s < 0 || s >= FD_SETSIZE) {
	aclose(s);
	errno = EMFILE;				/* out of range */
	return -1;
    }

    if(sendto(s, dgram->data, dgram->len, 0, 
              (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) == -1)
        return -1;

    if(dgram->socket == -1) {
	if(close(s) == -1)
	    return -1;
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

    if((hp = gethostbyname(hostname)) == 0) return -1;
    memcpy(&name.sin_addr, hp->h_addr, hp->h_length);
    name.sin_family = AF_INET;
    name.sin_port = htons(port);

    return dgram_send_addr(name, dgram);
}


int dgram_recv(dgram, timeout, fromaddr)
int timeout;
dgram_t *dgram;
struct sockaddr_in *fromaddr;
{
    fd_set ready;
    struct timeval to;
    int size, addrlen, sock;

    sock = dgram->socket;

    FD_ZERO(&ready);
    FD_SET(sock, &ready);
    to.tv_sec = timeout;
    to.tv_usec = 0;

    if(select(sock+1, (SELECT_ARG_TYPE *)&ready, NULL, NULL, &to) == -1)
	return -1;

    if(!FD_ISSET(sock, &ready))	return 0;      	/* timed out */

    addrlen = sizeof(struct sockaddr_in);

    size = recvfrom(sock, dgram->data, MAX_DGRAM, 0,
		    (struct sockaddr *)fromaddr, &addrlen);
    if(size == -1) return -1;
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
char *str;
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
