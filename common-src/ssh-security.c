/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1999 University of Maryland
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
 * $Id: ssh-security.c,v 1.15 2006/05/12 22:42:48 martinea Exp $
 *
 * ssh-security.c - security and transport over ssh or a ssh-like command.
 *
 * XXX still need to check for initial keyword on connect so we can skip
 * over shell garbage and other stuff that ssh might want to spew out.
 */

#include "amanda.h"
#include "util.h"
#include "event.h"
#include "packet.h"
#include "queue.h"
#include "security.h"
#include "security-util.h"
#include "stream.h"
#include "version.h"

#ifdef SSH_SECURITY

#define	SSH_DEBUG

#ifdef SSH_DEBUG
#define	sshprintf(x)	dbprintf(x)
#else
#define	sshprintf(x)
#endif

/*
 * Path to the ssh binary.  This should be configurable.
 */
#define	SSH_PATH	"/usr/bin/ssh"

/*
 * Arguments to ssh.  This should also be configurable
 */
#define	SSH_ARGS	"-x", "-o", "PasswordAuthentication=no"

/*
 * Number of seconds ssh has to start up
 */
#define	CONNECT_TIMEOUT	20

/*
 * Magic values for ssh_conn->handle
 */
#define	H_TAKEN	-1		/* ssh_conn->tok was already read */
#define	H_EOF	-2		/* this connection has been shut down */

/*
 * Interface functions
 */
static void ssh_connect P((const char *,
    char *(*)(char *, void *), 
    void (*)(void *, security_handle_t *, security_status_t), void *, void *));

/*
 * This is our interface to the outside world.
 */
const security_driver_t ssh_security_driver = {
    "SSH",
    ssh_connect,
    sec_accept,
    sec_close,
    stream_sendpkt,
    stream_recvpkt,
    stream_recvpkt_cancel,
    tcpma_stream_server,
    tcpma_stream_accept,
    tcpma_stream_client,
    tcpma_stream_close,
    sec_stream_auth,
    sec_stream_id,
    tcpm_stream_write,
    tcpm_stream_read,
    tcpm_stream_read_sync,
    tcpm_stream_read_cancel,
};

static int newhandle = 1;

/*
 * Local functions
 */
static int runssh P((struct tcp_conn *, const char *, const char *));


/*
 * ssh version of a security handle allocator.  Logically sets
 * up a network "connection".
 */
static void
ssh_connect(hostname, conf_fn, fn, arg, datap)
    const char *hostname;
    char *(*conf_fn) P((char *, void *));
    void (*fn) P((void *, security_handle_t *, security_status_t));
    void *arg;
    void *datap;
{
    struct sec_handle *rh;
    struct hostent *he;
    char *amandad_path=NULL, *client_username=NULL;

    assert(fn != NULL);
    assert(hostname != NULL);

    sshprintf(("%s: ssh: ssh_connect: %s\n", debug_prefix_time(NULL),
	       hostname));

    rh = alloc(sizeof(*rh));
    security_handleinit(&rh->sech, &ssh_security_driver);
    rh->hostname = NULL;
    rh->rs = NULL;
    rh->ev_timeout = NULL;

    if ((he = gethostbyname(hostname)) == NULL) {
	security_seterror(&rh->sech,
	    "%s: could not resolve hostname", hostname);
	(*fn)(arg, &rh->sech, S_ERROR);
	return;
    }
    rh->hostname = he->h_name;	/* will be replaced */
    rh->rc = sec_tcp_conn_get(rh->hostname, 1);
    rh->rs = tcpma_stream_client(rh, newhandle++);

    if (rh->rs == NULL)
	goto error;

    rh->hostname = rh->rs->rc->hostname;

    /*
     * We need to open a new connection.
     *
     * XXX need to eventually limit number of outgoing connections here.
     */
    if(conf_fn) {
	amandad_path    = conf_fn("amandad_path", datap);
	client_username = conf_fn("client_username", datap);
    }
    if (runssh(rh->rs->rc, amandad_path, client_username) < 0) {
	security_seterror(&rh->sech, "can't connect to %s: %s",
			  hostname, rh->rs->rc->errmsg);
	goto error;
    }
    /*
     * The socket will be opened async so hosts that are down won't
     * block everything.  We need to register a write event
     * so we will know when the socket comes alive.
     *
     * Overload rh->rs->ev_read to provide a write event handle.
     * We also register a timeout.
     */
    rh->fn.connect = fn;
    rh->arg = arg;
    rh->rs->ev_read = event_register(rh->rs->rc->write, EV_WRITEFD,
	sec_connect_callback, rh);
    rh->ev_timeout = event_register(CONNECT_TIMEOUT, EV_TIME,
	sec_connect_timeout, rh);

    return;

error:
    (*fn)(arg, &rh->sech, S_ERROR);
}

/*
 * Forks a ssh to the host listed in rc->hostname
 * Returns negative on error, with an errmsg in rc->errmsg.
 */
static int
runssh(rc, amandad_path, client_username)
    struct tcp_conn *rc;
    const char *amandad_path;
    const char *client_username;
{
    int rpipe[2], wpipe[2];
    char *xamandad_path = (char *)amandad_path;
    char *xclient_username = (char *)client_username;

    if (pipe(rpipe) < 0 || pipe(wpipe) < 0) {
	rc->errmsg = newvstralloc("pipe: ", strerror(errno), NULL);
	return (-1);
    }

    switch (rc->pid = fork()) {
    case -1:
	rc->errmsg = newvstralloc("fork: ", strerror(errno), NULL);
	aclose(rpipe[0]);
	aclose(rpipe[1]);
	aclose(wpipe[0]);
	aclose(wpipe[1]);
	return (-1);
    case 0:
	dup2(wpipe[0], 0);
	dup2(rpipe[1], 1);
	dup2(rpipe[1], 2);
	break;
    default:
	rc->read = rpipe[0];
	aclose(rpipe[1]);
	rc->write = wpipe[1];
	aclose(wpipe[0]);
	return (0);
    }

    safe_fd(-1, 0);

    if(!xamandad_path || strlen(xamandad_path) <= 1) 
	xamandad_path = vstralloc(libexecdir, "/", "amandad",
				 versionsuffix(), NULL);
    if(!xclient_username || strlen(xclient_username) <= 1)
	xclient_username = CLIENT_LOGIN;

    execlp(SSH_PATH, SSH_PATH, SSH_ARGS, "-l", xclient_username,
	   rc->hostname, xamandad_path, "-auth=ssh", "amdump", "amindexd",
	   "amidxtaped", NULL);
    error("error: couldn't exec %s: %s", SSH_PATH, strerror(errno));

    /* should never go here, shut up compiler warning */
    return(-1);
}

#endif	/* SSH_SECURITY */
