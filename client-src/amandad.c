/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991 University of Maryland
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
 * $Id: amandad.c,v 1.20 1998/01/08 19:33:30 jrj Exp $
 *
 * handle client-host side of Amanda network communications, including
 * security checks, execution of the proper service, and acking the
 * master side
 */

#include "amanda.h"
#include "dgram.h"
#include "version.h"
#include "protocol.h"

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
#	define NEED_KEYPIPE	2	/* pass kerberos key in pipe */
#	define NO_AUTH		4	/* doesn't need authentication */
} service_table[] = {
    { "sendsize",	NONE },
    { "sendbackup",	NEED_KEYPIPE },
    { "sendfsinfo",	NONE },
    { "selfcheck",	NONE },
    { NULL, NONE }
};


int max_retry_count = MAX_RETRIES;
int ack_timeout     = ACK_TIMEOUT;

char *pname = "amandad";

#ifdef KRB4_SECURITY
#  include "amandad-krb4.c"
#endif

/* local functions */
int main P((int argc, char **argv));
void sendack P((pkt_t *hdr, pkt_t *msg));
void sendnak P((pkt_t *hdr, pkt_t *msg, char *str));
void setup_rep P((pkt_t *hdr, pkt_t *msg));
char *strlower P((char *str));

void sigchild_jump P((int sig));
void sigchild_flag P((int sig));


/*
 * We trap SIGCHLD to break out of our select loop.
 */

jmp_buf sigjmp;
int got_sigchild = 0;

void sigchild_jump(sig) 
int sig;
{ 
    got_sigchild = 1;
    longjmp(sigjmp, 1);
}

void sigchild_flag(sig) 
int sig;
{ 
    got_sigchild = 1;
}

/* XXX these moved out of main because of the setjmp.  restructure! */
struct service_s *servp;
fd_set insock;
pkt_t in_msg, out_msg, dup_msg;
char *cmd = NULL, *base = NULL;
char *pwname, **vp;
struct passwd *pwptr;
int retry_count, rc, reqlen;
int req_pipe[2], rep_pipe[2];

int main(argc, argv)
int argc;
char **argv;
{
    int l, n, s;
    int fd;
    char *errstr = NULL;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);

    pwname = CLIENT_LOGIN;
    pwptr = getpwnam(pwname);

#ifdef FORCE_USERID

    /* we'd rather not run as root */
    if(geteuid() == 0) {
#ifdef KRB4_SECURITY
        if(pwptr == NULL)
	    error("error [cannot find user %s in passwd file]\n", pwname);
        /*
	 * if we're using kerberos security, we'll need to be root in
	 * order to get at the machine's srvtab entry, so we hang on to
	 * some root privledges for now.  We give them up entirely later.
	 */
	seteuid(pwptr->pw_uid);
	setegid(pwptr->pw_gid);
#else
	initgroups(pwname, pwptr->pw_gid);
	setgid(pwptr->pw_gid);
	setuid(pwptr->pw_uid);
#endif  /* KRB4_SECURITY */
    }
#endif	/* FORCE_USERID */

    /* initialize */

    chdir("/");		/* someplace not world writable */
    umask(077);
    dbopen();
    {
	extern int db_fd;
	dup2(db_fd, 1);
	dup2(db_fd, 2);
    }

    dbprintf(("%s: version %s\n", argv[0], version()));
    for(vp = version_info; *vp != NULL; vp++)
	dbprintf(("%s: %s", argv[0], *vp));

    dgram_zero(&in_msg.dgram); 
    dgram_socket(&in_msg.dgram, 0);

    dgram_zero(&dup_msg.dgram);
    dgram_socket(&dup_msg.dgram, 0);

    dgram_zero(&out_msg.dgram);
    dgram_socket(&out_msg.dgram, 0);

    /* set up input and response pipes */

    if(pipe(req_pipe) == -1 || pipe(rep_pipe) == -1)
      error("pipe: %s", strerror(errno));

#ifdef KRB4_SECURITY
    if(argc >= 2 && strcmp(argv[1], "-krb4") == 0) {
	krb4_auth = 1;
	dbprintf(("using krb4 security\n"));
    }
    else {
	dbprintf(("using bsd security\n"));
	krb4_auth = 0;
    }
#endif

    /* get request packet and attempt to parse it */

    if(dgram_recv(&in_msg.dgram, RECV_TIMEOUT, &in_msg.peer) <= 0)
	error("error receiving message: %s", strerror(errno));

    dbprintf(("got packet:\n--------\n%s--------\n\n", in_msg.dgram.cur));

    parse_pkt_header(&in_msg);
    if(in_msg.type != P_REQ && in_msg.type != P_NAK) {
	/* XXX */
	dbprintf(("this is a %s packet, nak'ing it\n", 
		  in_msg.type == P_BOGUS? "bogus" : "unexpected"));
	sendnak(&in_msg, &out_msg, parse_errmsg);
	dbclose();
	return 1;
    }
    if(in_msg.type != P_REQ) {
	dbprintf(("strange, this is not a request packet\n"));
	dbclose();
	return 1;
    }

    /* lookup service */

    for(servp = service_table; servp->name != NULL; servp++)
	if(strcmp(servp->name, in_msg.service) == 0) break;

    if(servp->name == NULL) {
	errstr = newstralloc2(errstr, "unknown service: ", in_msg.service);
	sendnak(&in_msg, &out_msg, errstr);
	dbclose();
	return 1;
    }

    base = newstralloc(base, servp->name);
    cmd = newvstralloc(cmd, libexecdir, "/", base, versionsuffix(), NULL);

    if(access(cmd, X_OK) == -1) {
	dbprintf(("execute access to \"%s\" denied\n", cmd));
	errstr = newvstralloc(errstr,
			      "service ", base, " unavailable",
			      NULL);
	sendnak(&in_msg, &out_msg, errstr);
	dbclose();
	return 1;
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
    seteuid(getuid());
    setegid(getgid());
#endif /* KRB4_SECURITY */

    afree(errstr);
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
	pwname = CLIENT_LOGIN;
	if((pwptr = getpwnam(pwname)) == NULL)
	    error("error [cannot find user %s in passwd file]\n", pwname);
	initgroups(pwname, pwptr->pw_gid);
	setgid(pwptr->pw_gid);
	setuid(pwptr->pw_uid);
    }

#endif  /* KRB4_SECURITY && FORCE_USERID */

    dbprintf(("%s: running service \"%s\"\n", argv[0], cmd));

    /* spawn child to handle request */

    signal(SIGCHLD, sigchild_flag);

    switch(fork()) {
    case -1: error("could not fork service: %s", strerror(errno));

    default:		/* parent */

        aclose(req_pipe[0]);
        aclose(rep_pipe[1]);

        reqlen = strlen(in_msg.dgram.cur);
	for(l = 0, n = reqlen; l < n; l += s) {
            if((s = write(req_pipe[1], in_msg.dgram.cur + l, n - l)) < 0) {
		error("write to child pipe: %s", strerror(errno));
	    }
	}
        aclose(req_pipe[1]);

        break; 

    case 0:		/* child */

        aclose(req_pipe[1]); 
        aclose(rep_pipe[0]);

        dup2(req_pipe[0], 0);
        dup2(rep_pipe[1], 1);

#ifdef  KRB4_SECURITY
	transfer_session_key();
#endif

	/* run service */

	execle(cmd, cmd, NULL, safe_env());
	error("could not exec service: %s", strerror(errno));
    }

    if(!setjmp(sigjmp))
	signal(SIGCHLD, sigchild_jump);
    else /* got jump, turn it off now */
	signal(SIGCHLD, sigchild_flag);

    while(!got_sigchild) {

	FD_ZERO(&insock);
	FD_SET(0, &insock);
	if(select(1, (SELECT_ARG_TYPE *)&insock, NULL, NULL, NULL) < 0)
	    error("select failed: %s", strerror(errno));

	if(dgram_recv(&dup_msg.dgram, RECV_TIMEOUT, &dup_msg.peer) <= 0)
	    error("error receiving message: %s", strerror(errno));

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
	if(dup_msg.peer.sin_addr.s_addr == in_msg.peer.sin_addr.s_addr &&
	   dup_msg.peer.sin_port == in_msg.peer.sin_port) {
	    dbprintf(("%s: received dup packet, ACKing it\n", argv[0]));
	    sendack(&in_msg, &out_msg);
	}
	else {
	    dbprintf(("%s: received other packet, NAKing it\n", argv[0]));
	    /* XXX dup_msg filled in? */
	    sendnak(&dup_msg, &out_msg, "amandad busy");
	}

    }

    /* XXX reap child?  log if non-zero status?  don't respond if non zero? */

    /* setup header for out_msg */

    setup_rep(&in_msg, &out_msg);
#ifdef KRB4_SECURITY
    add_mutual_authenticator(&out_msg.dgram);
#endif

    /* read response packet from pipe: up to MAX_DGRAM bytes are sent */

    if((rc = read(rep_pipe[0], out_msg.dgram.cur,
                  MAX_DGRAM-out_msg.dgram.len)) < 0)
        error("reading response pipe: %s", strerror(errno));

    out_msg.dgram.len += rc;
    out_msg.dgram.data[out_msg.dgram.len] = '\0';
    aclose(rep_pipe[0]);

send_response:

    retry_count = 0;

    while(retry_count < max_retry_count) {
	if(!retry_count)
	    dbprintf(("%s: sending REP packet:\n----\n%s----\n\n",
		      argv[0], out_msg.dgram.data));
	dgram_send_addr(in_msg.peer, &out_msg.dgram);
	if(dgram_recv(&dup_msg.dgram, ack_timeout, &dup_msg.peer) <= 0) {
	    /* timed out or error, try again */
	    retry_count++;

	    dbprintf(("%s: timeout waiting for ack", argv[0]));
	    if(retry_count < max_retry_count) 
		dbprintf((", retrying\n"));
	    else 
		dbprintf((", giving up!\n"));

	    continue;
	}
	dbprintf(("%s: got ack:\n----\n%s----\n\n", argv[0],
		  dup_msg.dgram.data));
	parse_pkt_header(&dup_msg);

	if(dup_msg.type == P_ACK && 
	   dup_msg.peer.sin_addr.s_addr == in_msg.peer.sin_addr.s_addr &&
	   dup_msg.peer.sin_port == in_msg.peer.sin_port)
	    break;
	else {
	    dbprintf(("%s: weird, it's not a proper ack\n", argv[0]));
	    dbprintf(("  addr: peer %lX dup %lX, port: peer %lX dup %lX\n",
		      (unsigned long)in_msg.peer.sin_addr.s_addr,
		      (unsigned long)dup_msg.peer.sin_addr.s_addr,
		      (unsigned long)in_msg.peer.sin_port,
		      (unsigned long)dup_msg.peer.sin_port));
	}		
    }
    /* XXX log if retry count exceeded */

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
    dbprintf(("sending ack:\n----\n%s----\n\n", msg->dgram.data));
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
		hdr->sequence, str);

    msg->dgram.len = strlen(msg->dgram.data);
    dbprintf(("sending nack:\n----\n%s----\n\n", msg->dgram.data));
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
	if(isupper(*s)) *s = tolower(*s);
    return str;
}
