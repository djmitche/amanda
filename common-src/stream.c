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
 * $Id: stream.c,v 1.30 2006/01/12 01:57:05 paddy_s Exp $
 *
 * functions for managing stream sockets
 */
#include "amanda.h"
#include "dgram.h"
#include "stream.h"
#include "util.h"

/* local functions */
static void try_socksize P((int sock, int which, int size));

int stream_server(portp, sendsize, recvsize)
int *portp;
int sendsize, recvsize;
{
    int server_socket;
    socklen_t len;
#ifdef SO_KEEPALIVE
    int on = 1;
    int r;
#endif
    struct sockaddr_in server;
    int save_errno;
    struct linger linger;
    int	ndelay;

    *portp = -1;				/* in case we error exit */
    if((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
	save_errno = errno;
	dbprintf(("%s: stream_server: socket() failed: %s\n",
		  debug_prefix(NULL),
		  strerror(save_errno)));
	errno = save_errno;
	return -1;
    }
    if(server_socket < 0 || server_socket >= FD_SETSIZE) {
	aclose(server_socket);
	errno = EMFILE;				/* out of range */
	save_errno = errno;
	dbprintf(("%s: stream_server: socket out of range: %d\n",
		  debug_prefix(NULL),
		  server_socket));
	errno = save_errno;
	return -1;
    }
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;

    if(sendsize >= 0) 
        try_socksize(server_socket, SO_SNDBUF, sendsize);
    if(recvsize >= 0) 
        try_socksize(server_socket, SO_RCVBUF, recvsize);

    /*
     * If a port range was specified, we try to get a port in that
     * range first.  Next, we try to get a reserved port.  If that
     * fails, we just go for any port.
     * 
     * In all cases, not to use port that's assigned to other services. 
     *
     * It is up to the caller to make sure we have the proper permissions
     * to get the desired port, and to make sure we return a port that
     * is within the range it requires.
     */
#ifdef TCPPORTRANGE
    if (bind_portrange(server_socket, &server, TCPPORTRANGE, "tcp") == 0)
	goto out;
#endif

    if (bind_portrange(server_socket, &server, 512, IPPORT_RESERVED - 1, "tcp") == 0)
	goto out;

    server.sin_port = INADDR_ANY;
    if (bind(server_socket, (struct sockaddr *)&server, (socklen_t) sizeof(server)) == -1) {
	save_errno = errno;
	dbprintf(("%s: stream_server: bind(INADDR_ANY) failed: %s\n",
		  debug_prefix(NULL),
		  strerror(save_errno)));
	aclose(server_socket);
	errno = save_errno;
	return -1;
    }

out:
    listen(server_socket, 1);

    /* find out what port was actually used */

    len = sizeof(server);
    if(getsockname(server_socket, (struct sockaddr *)&server, &len) == -1) {
	save_errno = errno;
	dbprintf(("%s: stream_server: getsockname() failed: %s\n",
		  debug_prefix(NULL),
		  strerror(save_errno)));
	aclose(server_socket);
	errno = save_errno;
	return -1;
    }

#ifdef SO_KEEPALIVE
    r = setsockopt(server_socket, SOL_SOCKET, SO_KEEPALIVE,
	(void *)&on, (socklen_t)sizeof(on));
    if(r == -1) {
	save_errno = errno;
	dbprintf(("%s: stream_server: setsockopt(SO_KEEPALIVE) failed: %s\n",
		  debug_prefix(NULL),
		  strerror(save_errno)));
        aclose(server_socket);
	errno = save_errno;
        return -1;
    }
#endif

    linger.l_onoff = 1;
    linger.l_linger = 60;
    (void)setsockopt(server_socket, SOL_SOCKET, SO_LINGER,
	(void *)&linger, (socklen_t)sizeof(linger));
    ndelay = 1;
    (void)setsockopt(server_socket, SOL_TCP, TCP_NODELAY,
		     &ndelay, sizeof(ndelay));

    *portp = (int) ntohs(server.sin_port);
    dbprintf(("%s: stream_server: waiting for connection: %s.%d\n",
	      debug_prefix_time(NULL),
	      inet_ntoa(server.sin_addr),
	      *portp));
    return server_socket;
}

static int
stream_client_internal(hostname,
		       port,
		       sendsize,
		       recvsize,
		       localport,
		       nonblock,
		       priv)
    const char *hostname;
    int port, sendsize, recvsize, *localport, nonblock, priv;
{
    int client_socket;
    socklen_t len;
#ifdef SO_KEEPALIVE
    int on = 1;
    int r;
#endif
    struct sockaddr_in svaddr, claddr;
    struct hostent *hostp;
    int save_errno;
    char *f;
    struct linger linger;
    int	ndelay;

    f = priv ? "stream_client_privileged" : "stream_client";

    if((hostp = gethostbyname(hostname)) == NULL) {
	save_errno = errno;
	dbprintf(("%s: %s: gethostbyname(%s) failed\n",
		  debug_prefix(NULL),
		  f,
		  hostname));
	errno = save_errno;
	return -1;
    }

    memset(&svaddr, 0, sizeof(svaddr));
    svaddr.sin_family = AF_INET;
    svaddr.sin_port = htons(port);
    memcpy(&svaddr.sin_addr, hostp->h_addr, hostp->h_length);

    if((client_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
	save_errno = errno;
	dbprintf(("%s: %s: socket() failed: %s\n",
		  debug_prefix(NULL),
		  f,
		  strerror(save_errno)));
	errno = save_errno;
	return -1;
    }
    if(client_socket < 0 || client_socket >= FD_SETSIZE) {
	aclose(client_socket);
	errno = EMFILE;				/* out of range */
	return -1;
    }

#ifdef SO_KEEPALIVE
    r = setsockopt(client_socket, SOL_SOCKET, SO_KEEPALIVE,
	(void *)&on, sizeof(on));
    if(r == -1) {
	save_errno = errno;
	dbprintf(("%s: %s: setsockopt() failed: %s\n",
		  debug_prefix(NULL),
		  f,
		  strerror(save_errno)));
        aclose(client_socket);
	errno = save_errno;
        return -1;
    }
#endif
    linger.l_onoff = 1;
    linger.l_linger = 60;
    (void)setsockopt(client_socket, SOL_SOCKET, SO_LINGER,
	(void *)&linger, (socklen_t)sizeof(linger));
    ndelay = 1;
    (void)setsockopt(client_socket, SOL_TCP, TCP_NODELAY,
		     &ndelay, sizeof(ndelay));
    memset(&claddr, 0, sizeof(claddr));
    claddr.sin_family = AF_INET;
    claddr.sin_addr.s_addr = INADDR_ANY;

    /*
     * If a privileged port range was requested, we try to get a port in
     * that range first and fail if it is not available.  Next, we try
     * to get a port in the range built in when Amanda was configured.
     * If that fails, we just go for any port.
     *
     * It is up to the caller to make sure we have the proper permissions
     * to get the desired port, and to make sure we return a port that
     * is within the range it requires.
     */
    if (priv) {
	int b;

	b = bind_portrange(client_socket, &claddr, 512, IPPORT_RESERVED - 1, "tcp");
	if (b == 0) {
	    goto out;				/* got what we wanted */
	}
	save_errno = errno;
	dbprintf(("%s: %s: bind(IPPORT_RESERVED) failed: %s\n",
		  debug_prefix(NULL),
		  f,
		  strerror(save_errno)));
	aclose(client_socket);
	errno = save_errno;
	return -1;
    }

#ifdef TCPPORTRANGE
    if (bind_portrange(client_socket, &claddr, TCPPORTRANGE, "tcp") == 0)
	goto out;
#endif

    claddr.sin_port = INADDR_ANY;
    if (bind(client_socket, (struct sockaddr *)&claddr, sizeof(claddr)) == -1) {
	save_errno = errno;
	dbprintf(("%s: %s: bind(INADDR_ANY) failed: %s\n",
		  debug_prefix(NULL),
		  f,
		  strerror(save_errno)));
	aclose(client_socket);
	errno = save_errno;
	return -1;
    }

out:

    /* find out what port was actually used */

    len = sizeof(claddr);
    if(getsockname(client_socket, (struct sockaddr *)&claddr, &len) == -1) {
	save_errno = errno;
	dbprintf(("%s: %s: getsockname() failed: %s\n",
		  debug_prefix(NULL),
		  f,
		  strerror(save_errno)));
	aclose(client_socket);
	errno = save_errno;
	return -1;
    }

    if (nonblock)
	fcntl(client_socket, F_SETFL,
	    fcntl(client_socket, F_GETFL, 0)|O_NONBLOCK);

    if(connect(client_socket, (struct sockaddr *)&svaddr, sizeof(svaddr))
       == -1 && !nonblock) {
	save_errno = errno;
	dbprintf(("%s: %s: connect to %s.%d failed: %s\n",
		  debug_prefix_time(NULL),
		  f,
		  inet_ntoa(svaddr.sin_addr),
		  ntohs(svaddr.sin_port),
		  strerror(save_errno)));
	aclose(client_socket);
	errno = save_errno;
	return -1;
    }

    dbprintf(("%s: %s: connected to %s.%d\n",
	      debug_prefix_time(NULL),
	      f,
	      inet_ntoa(svaddr.sin_addr),
	      ntohs(svaddr.sin_port)));
    dbprintf(("%s: %s: our side is %s.%d\n",
	      debug_prefix(NULL),
	      f,
	      inet_ntoa(claddr.sin_addr),
	      ntohs(claddr.sin_port)));

    if(sendsize >= 0) 
	try_socksize(client_socket, SO_SNDBUF, sendsize);
    if(recvsize >= 0) 
	try_socksize(client_socket, SO_RCVBUF, recvsize);

    if (localport != NULL)
	*localport = ntohs(claddr.sin_port);

    return client_socket;
}

int
stream_client_privileged(hostname,
			 port,
			 sendsize,
			 recvsize,
			 localport,
			 nonblock)
    const char *hostname;
    int port, sendsize, recvsize, *localport, nonblock;
{
    return stream_client_internal(hostname,
				  port,
				  sendsize,
				  recvsize,
				  localport,
				  nonblock,
				  1);
}

int
stream_client(hostname, port, sendsize, recvsize, localport, nonblock)
    const char *hostname;
    int port, sendsize, recvsize, *localport, nonblock;
{
    return stream_client_internal(hostname,
				  port,
				  sendsize,
				  recvsize,
				  localport,
				  nonblock,
				  0);
}

/* don't care about these values */
static struct sockaddr_in addr;
static socklen_t addrlen;

int stream_accept(server_socket, timeout, sendsize, recvsize)
int server_socket, timeout, sendsize, recvsize;
{
    fd_set readset;
    struct timeval tv;
    int nfound, connected_socket;
    int save_errno;

    assert(server_socket >= 0);

    tv.tv_sec = timeout;
    tv.tv_usec = 0;
    FD_ZERO(&readset);
    FD_SET(server_socket, &readset);
    nfound = select(server_socket+1, (SELECT_ARG_TYPE *)&readset, NULL, NULL, &tv);
    if(nfound <= 0 || !FD_ISSET(server_socket, &readset)) {
	save_errno = errno;
	if(nfound < 0) {
	    dbprintf(("%s: stream_accept: select() failed: %s\n",
		      debug_prefix_time(NULL),
		      strerror(save_errno)));
	} else if(nfound == 0) {
	    dbprintf(("%s: stream_accept: timeout after %d second%s\n",
		      debug_prefix_time(NULL),
		      timeout,
		      (timeout == 1) ? "" : "s"));
	    save_errno = ENOENT;			/* ??? */
	} else if (!FD_ISSET(server_socket, &readset)) {
	    int i;

	    for(i = 0; i < server_socket + 1; i++) {
		if(FD_ISSET(i, &readset)) {
		    dbprintf(("%s: stream_accept: got fd %d instead of %d\n",
			      debug_prefix_time(NULL),
			      i,
			      server_socket));
		}
	    }
	    save_errno = EBADF;
	}
	errno = save_errno;
	return -1;
    }

    while(1) {
	addrlen = sizeof(struct sockaddr);
	connected_socket = accept(server_socket,
				  (struct sockaddr *)&addr,
				  &addrlen);
	if(connected_socket < 0) {
	    save_errno = errno;
	    dbprintf(("%s: stream_accept: accept() failed: %s\n",
		      debug_prefix_time(NULL),
		      strerror(save_errno)));
	    errno = save_errno;
	    return -1;
	}
	dbprintf(("%s: stream_accept: connection from %s.%d\n",
	          debug_prefix_time(NULL),
	          inet_ntoa(addr.sin_addr),
	          ntohs(addr.sin_port)));
	/*
	 * Make certain we got an inet connection and that it is not
	 * from port 20 (a favorite unauthorized entry tool).
	 */
	if(addr.sin_family == AF_INET && ntohs(addr.sin_port) != 20) {
	    break;
	}
	if(addr.sin_family != AF_INET) {
	    dbprintf(("%s: family is %d instead of %d(AF_INET): ignored\n",
		      debug_prefix_time(NULL),
		      addr.sin_family,
		      AF_INET));
	}
	if(ntohs(addr.sin_port) == 20) {
	    dbprintf(("%s: remote port is %d: ignored\n",
		      debug_prefix_time(NULL),
		      ntohs(addr.sin_port)));
	}
	aclose(connected_socket);
    }

    if(sendsize >= 0) 
	try_socksize(connected_socket, SO_SNDBUF, sendsize);
    if(recvsize >= 0) 
	try_socksize(connected_socket, SO_RCVBUF, recvsize);

    return connected_socket;
}

static void try_socksize(sock, which, size)
int sock, which, size;
{
    int origsize;

    origsize = size;
    /* keep trying, get as big a buffer as possible */
    while(size > 1024 &&
	  setsockopt(sock, SOL_SOCKET, which, (void *) &size, sizeof(int)) < 0)
	size -= 1024;
    if(size > 1024) {
	dbprintf(("%s: try_socksize: %s buffer size is %d\n",
		  debug_prefix(NULL),
		  (which == SO_SNDBUF) ? "send" : "receive",
		  size));
    } else {
	dbprintf(("%s: try_socksize: could not allocate %s buffer of %d\n",
		  debug_prefix(NULL),
		  (which == SO_SNDBUF) ? "send" : "receive",
		  origsize));
    }
}
