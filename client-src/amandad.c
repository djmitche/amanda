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
 * $Id: amandad.c,v 1.32.2.4.4.1.2.5 2003/01/04 17:46:08 martinea Exp $
 *
 * handle client-host side of Amanda network communications, including
 * security checks, execution of the proper service, and acking the
 * master side
 */

#include "amanda.h"
#include "clock.h"
#include "dgram.h"
#include "amfeatures.h"
#include "version.h"
#include "protocol.h"
#include "util.h"

#define RECV_TIMEOUT 30
#define ACK_TIMEOUT  10		/* XXX should be configurable */
#define MAX_RETRIES   5

/* 
 * Here are the services that we understand.
 */
struct service_s {
    char *name;
    int flags;
#	define NONE		0
#	define IS_INTERNAL	1	/* service is internal */
#	define NEED_KEYPIPE	2	/* pass kerberos key in pipe */
#	define NO_AUTH		4	/* doesn't need authentication */
} service_table[] = {
    { "noop",		IS_INTERNAL },
    { "sendsize",	NONE },
    { "sendbackup",	NEED_KEYPIPE },
    { "sendfsinfo",	NONE },
    { "selfcheck",	NONE },
    { NULL, NONE }
};


int max_retry_count = MAX_RETRIES;
int ack_timeout     = ACK_TIMEOUT;

#ifdef KRB4_SECURITY
#  include "amandad-krb4.c"
#endif

/* local functions */
int main P((int argc, char **argv));
void sendack P((pkt_t *hdr, pkt_t *msg));
void sendnak P((pkt_t *hdr, pkt_t *msg, char *str));
void setup_rep P((pkt_t *hdr, pkt_t *msg));
char *strlower P((char *str));

int main(argc, argv)
int argc;
char **argv;
{
    int n;
    int fd;
    char *errstr = NULL;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    char *pgm = "amandad";		/* in case argv[0] is not set */

    /* in_msg: The first incoming request.
       dup_msg: Any other incoming message.
       out_msg: Standard, i.e. non-repeated, ACK and REP.
       rej_msg: Any other outgoing message.
     */
    pkt_t in_msg, out_msg, rej_msg, dup_msg;
    char *cmd = NULL, *base = NULL;
    char *noop_file = NULL;
    char **vp;
    char *s;
    ssize_t s_len;
    int retry_count, rc, reqlen;
    int req_pipe[2], rep_pipe[2];
    int dglen = 0;
    char number[NUM_STR_SIZE];
    am_feature_t *our_features = NULL;
    char *our_feature_string = NULL;

    struct service_s *servp;
    fd_set insock;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    safe_cd();

    /*
     * When called via inetd, it is not uncommon to forget to put the
     * argv[0] value on the config line.  On some systems (e.g. Solaris)
     * this causes argv and/or argv[0] to be NULL, so we have to be
     * careful getting our name.
     */
    if (argc >= 1 && argv != NULL && argv[0] != NULL) {
	if((pgm = strrchr(argv[0], '/')) != NULL) {
	    pgm++;
	} else {
	    pgm = argv[0];
	}
    }

    set_pname(pgm);

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);

#ifdef FORCE_USERID

    /* we'd rather not run as root */
    if(geteuid() == 0) {
#ifdef KRB4_SECURITY
        if(client_uid == (uid_t) -1) {
	    error("error [cannot find user %s in passwd file]\n", CLIENT_LOGIN);
	}

        /*
	 * if we're using kerberos security, we'll need to be root in
	 * order to get at the machine's srvtab entry, so we hang on to
	 * some root privledges for now.  We give them up entirely later.
	 */
	setegid(client_gid);
	seteuid(client_uid);
#else
	initgroups(CLIENT_LOGIN, client_gid);
	setgid(client_gid);
	setuid(client_uid);
#endif  /* KRB4_SECURITY */
    }
#endif	/* FORCE_USERID */

    /* initialize */

    dbopen();
    {
	int db_fd = dbfd();
	if(db_fd != -1) {
	    dup2(db_fd, 1);
	    dup2(db_fd, 2);
	}
    }

    startclock();

    dbprintf(("%s: version %s\n", get_pname(), version()));
    for(vp = version_info; *vp != NULL; vp++)
	dbprintf(("%s: %s", debug_prefix(NULL), *vp));

    if (! (argc >= 1 && argv != NULL && argv[0] != NULL)) {
	dbprintf(("%s: WARNING: argv[0] not defined: check inetd.conf\n",
		  debug_prefix(NULL)));
    }

    our_features = am_init_feature_set();
    our_feature_string = am_feature_to_string(our_features);

    dgram_zero(&in_msg.dgram); 
    dgram_socket(&in_msg.dgram, 0);

    dgram_zero(&dup_msg.dgram);
    dgram_socket(&dup_msg.dgram, 0);

    dgram_zero(&out_msg.dgram);
    dgram_socket(&out_msg.dgram, 0);

    dgram_zero(&rej_msg.dgram);
    dgram_socket(&rej_msg.dgram, 0);

    dgram_zero(&rej_msg.dgram);
    dgram_socket(&rej_msg.dgram, 0);

    /* set up input and response pipes */

#ifdef KRB4_SECURITY
    if(argc >= 2 && strcmp(argv[1], "-krb4") == 0) {
	krb4_auth = 1;
	dbprintf(("%s: using krb4 security\n", debug_prefix(NULL)));
    }
    else {
	dbprintf(("%s: using bsd security\n", debug_prefix(NULL)));
	krb4_auth = 0;
    }
#endif

    /* get request packet and attempt to parse it */

    if((n = dgram_recv(&in_msg.dgram, RECV_TIMEOUT, &in_msg.peer)) <= 0) {
	char *s;

	if (n == 0) {
	    s = "timeout";
	} else {
	    s = strerror(errno);
	}
	error("error receiving message: %s", s);
    }

    dbprintf(("%s: got packet:\n--------\n%s--------\n\n",
	      debug_prefix_time(NULL), in_msg.dgram.cur));

    parse_pkt_header(&in_msg);
    if(in_msg.type != P_REQ && in_msg.type != P_NAK && in_msg.type != P_ACK) {
	/* XXX */
	dbprintf(("%s: this is a %s packet, nak'ing it\n", 
		  debug_prefix_time(NULL),
		  in_msg.type == P_BOGUS? "bogus" : "unexpected"));
	if(in_msg.type != P_BOGUS) {
	    parse_errmsg = newvstralloc(parse_errmsg,"unexpected ",
		in_msg.type == P_ACK? "ack ":
		in_msg.type == P_REP? "rep ": "",
		"packet", NULL);
	}
	sendnak(&in_msg, &rej_msg, parse_errmsg);
	dbclose();
	return 1;
    }
    if(in_msg.type != P_REQ) {
	dbprintf(("%s: strange, this is not a request packet\n",
		  debug_prefix_time(NULL)));
	dbclose();
	return 1;
    }

    /* lookup service */

    for(servp = service_table; servp->name != NULL; servp++)
	if(strcmp(servp->name, in_msg.service) == 0) break;

    if(servp->name == NULL) {
	errstr = newstralloc2(errstr, "unknown service: ", in_msg.service);
	sendnak(&in_msg, &rej_msg, errstr);
	dbclose();
	return 1;
    }

    if((servp->flags & IS_INTERNAL) != 0) {
	cmd = stralloc(servp->name);
    } else {
	base = newstralloc(base, servp->name);
	cmd = newvstralloc(cmd, libexecdir, "/", base, versionsuffix(), NULL);

	if(access(cmd, X_OK) == -1) {
	    dbprintf(("%s: execute access to \"%s\" denied\n",
		      debug_prefix_time(NULL), cmd));
	    errstr = newvstralloc(errstr,
			          "service ", base, " unavailable",
			          NULL);
	    amfree(base);
	    sendnak(&in_msg, &rej_msg, errstr);
	    dbclose();
	    return 1;
	}
	amfree(base);
    }

    /* everything looks ok initially, send ACK */

    sendack(&in_msg, &out_msg);

    /* 
     * handle security check: this could take a long time, so it is 
     * done after the initial ack.
     */

#if defined(KRB4_SECURITY)
    /*
     * we need to be root to access the srvtab file, but only if we started
     * out that way.
     */
    setegid(getgid());
    seteuid(getuid());
#endif /* KRB4_SECURITY */

    amfree(errstr);
    if(!(servp->flags & NO_AUTH)
       && !security_ok(&in_msg.peer, in_msg.security, in_msg.cksum, &errstr)) {
	/* XXX log on authlog? */
	setup_rep(&in_msg, &out_msg);
	ap_snprintf(out_msg.dgram.cur,
		    sizeof(out_msg.dgram.data)-out_msg.dgram.len,
		    "ERROR %s\n", errstr);
	out_msg.dgram.len = strlen(out_msg.dgram.data);
	goto send_response;
    }

#if defined(KRB4_SECURITY) && defined(FORCE_USERID)

    /*
     * we held on to a root uid earlier for accessing files; since we're
     * done doing anything requiring root, we can completely give it up.
     */

    if(geteuid() == 0) {
	if(client_uid == (uid_t) -1) {
	    error("error [cannot find user %s in passwd file]\n", CLIENT_LOGIN);
	}
	initgroups(CLIENT_LOGIN, client_gid);
	setgid(client_gid);
	setuid(client_uid);
    }

#endif  /* KRB4_SECURITY && FORCE_USERID */

    dbprintf(("%s: running service \"%s\"\n", debug_prefix_time(NULL), cmd));

    if(strcmp(servp->name, "noop") == 0) {
	ap_snprintf(number, sizeof(number), "%ld", (long)getpid());
	noop_file = vstralloc(AMANDA_TMPDIR,
			      "/",
			      get_pname(),
			      ".noop.",
			      number,
			      NULL);
	rep_pipe[0] = open(noop_file, O_RDWR|O_EXCL|O_CREAT);
	if(rep_pipe[0] < 0) {
	    error("cannot open \"%s\": %s", noop_file, strerror(errno));
	}
	(void)unlink(noop_file);
	s = vstralloc("OPTIONS features=", our_feature_string, ";\n", NULL);
	s_len = strlen(s);
	if(fullwrite(rep_pipe[0], s, s_len) != s_len) {
	    error("cannot write %d bytes to %s", s_len, noop_file);
	}
	amfree(noop_file);
	amfree(s);
	(void)lseek(rep_pipe[0], (off_t)0, SEEK_SET);
    } else {
	if(pipe(req_pipe) == -1 || pipe(rep_pipe) == -1)
	    error("pipe: %s", strerror(errno));

	/* spawn first child to handle the request */

	switch(fork()) {
	case -1: error("could not fork for %s: %s", cmd, strerror(errno));

	default:		/* parent */

	    break; 

	case 0:		/* child */

            aclose(req_pipe[1]); 
            aclose(rep_pipe[0]);

            dup2(req_pipe[0], 0);
            dup2(rep_pipe[1], 1);

#ifdef  KRB4_SECURITY
	    transfer_session_key();
#endif

            aclose(req_pipe[0]);
            aclose(rep_pipe[1]);

	    /* run service */

	    execle(cmd, cmd, NULL, safe_env());
	    error("could not exec %s: %s", cmd, strerror(errno));
        }
        amfree(cmd);

        aclose(req_pipe[0]);
        aclose(rep_pipe[1]);

        /* spawn second child to handle writing the packet to the first child */

        switch(fork()) {
        case -1: error("could not fork for %s: %s", cmd, strerror(errno));

        default:		/* parent */

	    break;

        case 0:		/* child */

            aclose(rep_pipe[0]);
            reqlen = strlen(in_msg.dgram.cur);
	    if((rc = fullwrite(req_pipe[1], in_msg.dgram.cur, reqlen)) != reqlen) {
	        if(rc < 0) {
		    error("write to child pipe: %s", strerror(errno));
	        } else {
		    error("write to child pipe: %d instead of %d", rc, reqlen);
	        }
	    }
            aclose(req_pipe[1]);
	    exit(0);
        }

        aclose(req_pipe[1]);
    }

    setup_rep(&in_msg, &out_msg);
#ifdef KRB4_SECURITY
    add_mutual_authenticator(&out_msg.dgram);
#endif

    while(1) {

	FD_ZERO(&insock);
	FD_SET(rep_pipe[0], &insock);

	if((servp->flags & IS_INTERNAL) != 0) {
	    n = 0;
	} else {
	    FD_SET(0, &insock);
	    n = select(rep_pipe[0] + 1,
		       (SELECT_ARG_TYPE *)&insock,
		       NULL,
		       NULL,
		       NULL);
	}
	if(n < 0) {
	    error("select failed: %s", strerror(errno));
	}

	if(FD_ISSET(rep_pipe[0], &insock)) {
	    if(dglen >= MAX_DGRAM) {
		error("more than %d bytes received from child", MAX_DGRAM);
	    }
	    rc = read(rep_pipe[0], out_msg.dgram.cur+dglen, MAX_DGRAM-dglen);
	    if(rc <= 0) {
		if (rc < 0) {
		    error("reading response pipe: %s", strerror(errno));
		}
		break;
	    }
	    dglen += rc;
	}
	if(!FD_ISSET(0,&insock))
	    continue;

	if((n = dgram_recv(&dup_msg.dgram, RECV_TIMEOUT, &dup_msg.peer)) <= 0) {
	    char *s;

	    if (n == 0) {
		s = "timeout";
	    } else {
		s = strerror(errno);
	    }
	    error("error receiving message: %s", s);
	}

	/* 
	 * Under normal conditions, the master will resend the REQ packet
	 * to be sure we are still alive.  It expects an ACK back right away.
	 *
	 * XXX- Arguably we should parse and security check the new packet, 
	 * only sending an ACK if it passes and the request is identical to
	 * the original one.  However, that's too much work for now. :-) 
	 *
	 * It should suffice to ACK whenever the sender is identical.
	 */
	dbprintf(("%s: got packet:\n----\n%s----\n\n",
		  debug_prefix_time(NULL), dup_msg.dgram.data));
	parse_pkt_header(&dup_msg);
	if(dup_msg.peer.sin_addr.s_addr == in_msg.peer.sin_addr.s_addr &&
	   dup_msg.peer.sin_port == in_msg.peer.sin_port) {
	    if(dup_msg.type == P_REQ) {
		dbprintf(("%s: received dup P_REQ packet, ACKing it\n",
			  debug_prefix_time(NULL)));
		sendack(&in_msg, &rej_msg);
	    }
	    else {
		dbprintf(("%s: it is not a P_REQ, ignoring it\n",
			  debug_prefix_time(NULL)));
	    }
	}
	else {
	    dbprintf(("%s: received other packet, NAKing it\n",
		      debug_prefix_time(NULL)));
	    dbprintf(("  addr: peer %s dup %s, port: peer %d dup %d\n",
		      inet_ntoa(in_msg.peer.sin_addr),
		      inet_ntoa(dup_msg.peer.sin_addr),
		      (int)ntohs(in_msg.peer.sin_port),
		      (int)ntohs(dup_msg.peer.sin_port)));
	    /* XXX dup_msg filled in? */
	    sendnak(&dup_msg, &rej_msg, "amandad busy");
	}

    }

    /* XXX reap child?  log if non-zero status?  don't respond if non zero? */
    /* setup header for out_msg */

    out_msg.dgram.len += dglen;
    out_msg.dgram.data[out_msg.dgram.len] = '\0';
    aclose(rep_pipe[0]);

send_response:

    retry_count = 0;

    while(retry_count < max_retry_count) {
	if(!retry_count)
	    dbprintf(("%s: sending REP packet:\n----\n%s----\n\n",
		      debug_prefix_time(NULL), out_msg.dgram.data));
	dgram_send_addr(in_msg.peer, &out_msg.dgram);
	if((n = dgram_recv(&dup_msg.dgram, ack_timeout, &dup_msg.peer)) <= 0) {
	    char *s;

	    if (n == 0) {
		s = "timeout";
	    } else {
		s = strerror(errno);
	    }

	    /* timed out or error, try again */
	    retry_count++;

	    dbprintf(("%s: waiting for ack: %s", debug_prefix_time(NULL), s));
	    if(retry_count < max_retry_count) 
		dbprintf((", retrying\n"));
	    else 
		dbprintf((", giving up!\n"));

	    continue;
	}
	dbprintf(("%s: got packet:\n----\n%s----\n\n",
		  debug_prefix_time(NULL), dup_msg.dgram.data));
	parse_pkt_header(&dup_msg);

	
	if(dup_msg.peer.sin_addr.s_addr == in_msg.peer.sin_addr.s_addr &&
	   dup_msg.peer.sin_port == in_msg.peer.sin_port) {
	    if(dup_msg.type == P_ACK)
		break;
	    else
		dbprintf(("%s: it is not an ack\n", debug_prefix_time(NULL)));
	}
	else {
	    dbprintf(("%s: weird, it is not a proper ack\n",
		      debug_prefix_time(NULL)));
	    dbprintf(("  addr: peer %s dup %s, port: peer %d dup %d\n",
		      inet_ntoa(in_msg.peer.sin_addr),
		      inet_ntoa(dup_msg.peer.sin_addr),
		      (int)ntohs(in_msg.peer.sin_port),
		      (int)ntohs(dup_msg.peer.sin_port)));
	}		
    }
    /* XXX log if retry count exceeded */

    amfree(cmd);
    amfree(noop_file);
    amfree(our_feature_string);
    am_release_feature_set(our_features);
    our_features = NULL;
    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
#if defined(USE_DBMALLOC)
	malloc_list(dbfd(), malloc_hist_1, malloc_hist_2);
#endif
    }

    dbclose();
    return 0;
}


/* -------- */

void sendack(hdr, msg)
pkt_t *hdr;
pkt_t *msg;
{
    /* XXX this isn't very safe either: handle could be bogus */
    ap_snprintf(msg->dgram.data, sizeof(msg->dgram.data),
		"Amanda %d.%d ACK HANDLE %s SEQ %d\n",
		VERSION_MAJOR, VERSION_MINOR,
		hdr->handle ? hdr->handle : "",
		hdr->sequence);
    msg->dgram.len = strlen(msg->dgram.data);
    dbprintf(("%s: sending ack:\n----\n%s----\n\n",
	      debug_prefix_time(NULL), msg->dgram.data));
    dgram_send_addr(hdr->peer, &msg->dgram);
}

void sendnak(hdr, msg, str)
pkt_t *hdr;
pkt_t *msg;
char *str;
{
    /* XXX this isn't very safe either: handle could be bogus */
    ap_snprintf(msg->dgram.data, sizeof(msg->dgram.data),
		"Amanda %d.%d NAK HANDLE %s SEQ %d\nERROR %s\n",
		VERSION_MAJOR, VERSION_MINOR,
		hdr->handle ? hdr->handle : "",
		hdr->sequence, str ? str : "UNKNOWN");

    msg->dgram.len = strlen(msg->dgram.data);
    dbprintf(("%s: sending nack:\n----\n%s----\n\n",
	      debug_prefix_time(NULL), msg->dgram.data));
    dgram_send_addr(hdr->peer, &msg->dgram);
}

void setup_rep(hdr, msg)
pkt_t *hdr;
pkt_t *msg;
{
    /* XXX this isn't very safe either: handle could be bogus */
    ap_snprintf(msg->dgram.data, sizeof(msg->dgram.data),
		"Amanda %d.%d REP HANDLE %s SEQ %d\n",
		VERSION_MAJOR, VERSION_MINOR,
		hdr->handle ? hdr->handle : "",
		hdr->sequence);

    msg->dgram.len = strlen(msg->dgram.data);
    msg->dgram.cur = msg->dgram.data + msg->dgram.len;

}

/* -------- */

char *strlower(str)
char *str;
{
    char *s;
    for(s=str; *s; s++)
	if(isupper((int)*s)) *s = tolower(*s);
    return str;
}
