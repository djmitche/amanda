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
 * amandad.c - handle client-host side of Amanda network communications,
 *	       including security checks, execution of the proper service,
 *	       and acking the master side.
 */

#include "amanda.h"
#include "dgram.h"
#include "version.h"
#include "protocol.h"

#define RECV_TIMEOUT 30
#define ACK_TIMEOUT  10		/* XXX should be configurable */
#define MAX_RETRIES   5

/* 
 * Here are the services that we understand.  For each one we have to know,
 * whether to concatanate the PROGRAM parameter to the service name, e.g.
 * sendsize-DUMP and sendsize-GNUTAR.
 */
struct service_s {
    char *name;
    int flags;
#	define NONE		0
#	define USE_PROGRAM	1	/* use PROGRAM parm in command name */
#	define NEED_KEYPIPE	2	/* pass kerberos key in pipe */
#	define NO_AUTH		4	/* doesn't need authentication */
} service_table[] = {
    { "sendsize",	NONE },
    { "sendbackup",	USE_PROGRAM|NEED_KEYPIPE },
    { "sendfsinfo",	NONE },
    { "selfcheck",	NONE },
    { "sendindex",      NONE },
    { NULL, NONE }
};


int max_retry_count = MAX_RETRIES;
int ack_timeout     = ACK_TIMEOUT;

char response_fname[256];
char input_fname[256];
char local_hostname[MAX_HOSTNAME_LENGTH];
/*
 * FIXME: Amandad is sprintfing into this tiny buffer information that
 * could potentially very easily overflow it.  This may not be big
 * security risk since the variable is a global one, but damned messy
 * if it does strike.
 */
char errstr[30720];
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
int security_ok P((pkt_t *msg));

void sigchild_jump P((int sig));
void sigchild_flag P((int sig));

char *addrstr P((struct in_addr addr));


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
char cmd[256], base[256];
char *domain, *pwname, **vp;
struct passwd *pwptr;
int f, retry_count, rc, reqlen;

int main(argc, argv)
int argc;
char **argv;
{
    char *envv[2];
    char *envp[(sizeof(envv) / sizeof(*envv))];
    char **p = NULL;
    char **q = NULL;
    char *v = NULL;

    /* a list of environment variables to be passed to child processes */
    envv[0] = "TZ";
    envv[1] = (char *)0;

    erroutput_type = (ERR_INTERACTIVE|ERR_SYSLOG);

#ifdef FORCE_USERID

    /* we'd rather not run as root */

    if(geteuid() == 0) {
	pwname = CLIENT_LOGIN;
	if((pwptr = getpwnam(pwname)) == NULL)
	    error("error [cannot find user %s in passwd file]\n", pwname);

	initgroups(pwname, pwptr->pw_gid);
	setgid(pwptr->pw_gid);
	setuid(pwptr->pw_uid);
    }

#endif	/* FORCE_USERID */

    /* initialize */

    chdir("/tmp");
    umask(0);
    dbopen("/tmp/amandad.debug");
    {
	extern int db_file;
	dup2(db_file, 1);
	dup2(db_file, 2);
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

    /* set up input and response filenames */

    strcpy(input_fname, "/tmp/amandad.inpXXXXXX");     mktemp(input_fname);
    strcpy(response_fname, "/tmp/amandad.outXXXXXX");  mktemp(response_fname);

    /* who am I? */
    /* local_hostname[sizeof(local_hostname)-1] = '\0';*/ /* static variable */
    if(gethostname(local_hostname, sizeof(local_hostname)-1) == -1)
        error("gethostname: %s", strerror(errno));
    domain = strchr(local_hostname, '.');		/* XXX */
    if(domain) *domain++ = '\0';

#ifdef KRB4_SECURITY
    if(argc >= 2 && !strcmp(argv[1], "-krb4")) {
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

    dbprintf(("got packet:\n--------\n%s--------\n", in_msg.dgram.cur));

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
	if(!strcmp(servp->name, in_msg.service)) break;

    if(servp->name == NULL) {
	sprintf(errstr, "unknown service: %s", in_msg.service);
	sendnak(&in_msg, &out_msg, errstr);
	dbclose();
	return 1;
    }

    if(servp->flags & USE_PROGRAM)
	sprintf(base, "%s-%s", servp->name, strlower(in_msg.program));
    else
	strcpy(base, servp->name);

    sprintf(cmd, "%s/%s%s", libexecdir, base, versionsuffix());

    if(access(cmd, X_OK) == -1) {
	dbprintf(("execute access to \"%s\" denied\n", cmd));
	sprintf(errstr, "service %s unavailable", base);
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

    if(!(servp->flags & NO_AUTH) && !security_ok(&in_msg)) {
	/* XXX log on authlog? */
	setup_rep(&in_msg, &out_msg);
	sprintf(out_msg.dgram.cur, "ERROR %s\n", errstr);
	out_msg.dgram.len = strlen(out_msg.dgram.data);
	goto send_response;
    }

    dbprintf(("%s: running service \"%s\"\n", argv[0], cmd));

    /* spawn child to handle request */

    signal(SIGCHLD, sigchild_flag);

    switch(fork()) {
    case -1: error("could not fork service: %s", strerror(errno));

    default: break;	/* parent */
    case 0:		/* child */

	/* put packet in input file */

	if((f = open(input_fname, O_WRONLY|O_CREAT|O_TRUNC,0660)) == -1)
	    error("could not open temp file \"%s\": %s", 
		  input_fname, strerror(errno));

	dbprintf(("%s: cmd for \"%s\":\n----\n%s----\n", argv[0], cmd, 
		  in_msg.dgram.cur));

	reqlen = strlen(in_msg.dgram.cur);
	if(write(f, in_msg.dgram.cur, reqlen) < reqlen)
	    error("short write to temp file");
	if(close(f))
	    error("close temp file: %s", strerror(errno));

	/* reopen temp file for input, response file for output */
		  
	if((f = open(input_fname, O_RDONLY)) == -1)
	    error("could not open temp file \"%s\": %s", 
		  input_fname, strerror(errno));
	dup2(f,0);
	close(f);

	if((f = open(response_fname, O_WRONLY|O_CREAT,0660)) == -1)
	    error("could not open temp file \"%s\": %s", 
		  response_fname, strerror(errno));
	dup2(f,1);
	/* dup2(f,2); no, because of ld.so caca in the output */
	close(f);

#ifdef  KRB4_SECURITY
	transfer_session_key();
#endif
	/* initialize the environment passed to the command */
	for (p = envv, q = envp; *p != 0; p++) {
	    v = getenv(*p);

	    if ( v == NULL ) {
		continue;
	    }

	    /*
	     * create an entry in the array of environment variables
	     */
	    *q = (char *)malloc(strlen(*p) + strlen(v) + 2);
	    if ( *q == NULL ) {
		/* there's no more memory for environment variables! */
		    break;
	    }
	    sprintf(*q, "%s=%s", *p, v);

	    q++;
	}
	*q = NULL;

	/* run service */

	execle(cmd, cmd, NULL, envp);
	error("could not fork service: %s", strerror(errno));
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

    /* read response packet from file: up to MAX_DGRAM bytes are sent */

    if((f = open(response_fname, O_RDONLY)) < 0)
	error("could not open response file \"%s\": %s",
	      response_fname, strerror(errno));
    if((rc = read(f, out_msg.dgram.cur, MAX_DGRAM-out_msg.dgram.len)) < 0)
	error("reading response file: %s", strerror(errno));
    out_msg.dgram.len += rc;
    out_msg.dgram.data[out_msg.dgram.len] = '\0';
    close(f);

send_response:

    retry_count = 0;

    while(retry_count < max_retry_count) {
	if(!retry_count)
	    dbprintf(("%s: sending REP packet:\n----\n%s----\n",
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
	dbprintf(("%s: got ack:\n----\n%s----\n", argv[0],
		  dup_msg.dgram.data));
	parse_pkt_header(&dup_msg);
		
	if(dup_msg.type == P_ACK && 
	   dup_msg.peer.sin_addr.s_addr == in_msg.peer.sin_addr.s_addr &&
	   dup_msg.peer.sin_port == in_msg.peer.sin_port)
	    break;
	else {
	    dbprintf(("%s: weird, it's not a proper ack\n", argv[0]));
	    dbprintf(("  addr: peer %X dup %X, port: peer %X dup %X\n",
		      in_msg.peer.sin_addr.s_addr,
		      dup_msg.peer.sin_addr.s_addr,
		      in_msg.peer.sin_port,
		      dup_msg.peer.sin_port));
	}		
    }
    /* XXX log if retry count exceeded */

    /* remove temp files */

    unlink(input_fname);
    unlink(response_fname);

    dbclose();
    return 0;
}


/* -------- */

void sendack(hdr, msg)
pkt_t *hdr;
pkt_t *msg;
{
    /* XXX this isn't very safe either: handle could be bogus */
    sprintf(msg->dgram.data, 
	    "Amanda %d.%d ACK HANDLE %s SEQ %d\n",
	    VERSION_MAJOR, VERSION_MINOR,
	    hdr->handle ? hdr->handle : "",
	    hdr->sequence);
    msg->dgram.len = strlen(msg->dgram.data);
    dbprintf(("sending ack:\n----\n%s----\n", msg->dgram.data));
    dgram_send_addr(hdr->peer, &msg->dgram);
}

void sendnak(hdr, msg, str)
pkt_t *hdr;
pkt_t *msg;
char *str;
{
    /* XXX this isn't very safe either: handle could be bogus */
    sprintf(msg->dgram.data, "Amanda %d.%d NAK HANDLE %s SEQ %d\nERROR %s\n",
	    VERSION_MAJOR, VERSION_MINOR,
	    hdr->handle ? hdr->handle : "",
	    hdr->sequence, str);

    msg->dgram.len = strlen(msg->dgram.data);
    dbprintf(("sending nack:\n----\n%s----\n", msg->dgram.data));
    dgram_send_addr(hdr->peer, &msg->dgram);
}

void setup_rep(hdr, msg)
pkt_t *hdr;
pkt_t *msg;
{
    /* XXX this isn't very safe either: handle could be bogus */
    sprintf(msg->dgram.data, "Amanda %d.%d REP HANDLE %s SEQ %d\n",
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

/* -------- */

int bsd_security_ok P((pkt_t *msg));

int security_ok(msg)
pkt_t *msg;
{
#ifdef KRB4_SECURITY
    if(krb4_auth)
	return krb4_security_ok(msg);
    else
#endif
	return bsd_security_ok(msg);
}

char *addrstr(addr)
struct in_addr addr;
{
    static char str[32];
    union {			/* XXX we rely on inet addrs being 32 bits */
	unsigned char c[4];	/*   but not on fields of struct in_addr   */
	int i;			/*   so we use our own union		   */
    } u;

    u.i = (int) addr.s_addr;
    sprintf(str, "%d.%d.%d.%d", u.c[0], u.c[1], u.c[2], u.c[3]);
    return str;
}

#ifdef BSD_SECURITY

int bsd_security_ok(msg)
pkt_t *msg;
{
    char remotehost[256], remoteuser[80], localuser[80];
    struct hostent *hp;
    struct passwd *pwptr;
    int myuid, rc, i;
#ifdef USE_AMANDAHOSTS
    FILE *fPerm;
    char pbuf[512];
    char *ptmp;
    int amandahostsauth = 0;
#endif

    /* what host is making the request? */

    hp = gethostbyaddr((char *)&(msg->peer.sin_addr),
		       sizeof(msg->peer.sin_addr), AF_INET);
    if(hp == NULL) {
	/* XXX include remote address in message */
	sprintf(errstr, "[addr %s: hostname lookup failed]", 
		addrstr(msg->peer.sin_addr));
	return 0;
    }
    strncpy(remotehost, hp->h_name, sizeof(remotehost));

    /* Now let's get the hostent for that hostname */
    hp = gethostbyname( remotehost );
    if(hp == NULL) {
	/* XXX include remote hostname in message */
	sprintf(errstr, "[addr %s: hostname lookup failed]", 
		remotehost);
	return 0;
    }

    /* Verify that the hostnames match -- they should theoretically */
    if( strcmp( remotehost, hp->h_name ) ) {
	sprintf(errstr, "[hostnames do not match: %s %s]", remotehost,
		hp->h_name );
	return 0;
    }

    /* Now let's verify that the ip which gave us this hostname
     * is really an ip for this hostname; or is someone trying to
     * break in? (THIS IS THE CRUCIAL STEP)
     */
    for (i = 0; hp->h_addr_list[i]; i++) {
	if (memcmp(hp->h_addr_list[i], (char *) &msg->peer.sin_addr,
	    sizeof(msg->peer.sin_addr)) == 0)
	    break;                     /* name is good, keep it */
    }

    /* If we did not find it, your DNS is messed up or someone is trying
     * to pull a fast one on you. :(
     */
    if( !hp->h_addr_list[i] ) {
	sprintf(errstr, "[ip address %s is not found in %s's ip list]",
		addrstr(msg->peer.sin_addr), remotehost );
	return 0;
    }

    /* next, make sure the remote port is a "reserved" one */

    if(ntohs(msg->peer.sin_port) >= IPPORT_RESERVED) {
	sprintf(errstr, "[host %s: port %d not secure]",
		remotehost, ntohs(msg->peer.sin_port));
	return 0;
    }

    /* extract the remote user name from the message */

    if((rc = sscanf(msg->security, "USER %[^ \n]", remoteuser)) != 1) {
	sprintf(errstr, "[host %s: bad bsd security line]",
		remotehost);
	return 0;
    }

    /* lookup our local user name */

    myuid = getuid();
    if((pwptr = getpwuid(myuid)) == NULL)
        error("error [getpwuid(%d) fails]", myuid);

    strncpy(localuser, pwptr->pw_name, sizeof(localuser));

    dbprintf(("bsd security: remote host %s user %s local user %s\n",
	      remotehost, remoteuser, localuser));

    /*
     * note that some versions of ruserok (eg SunOS 3.2) look in
     * "./.rhosts" rather than "~localuser/.rhosts", so we have to
     * chdir ourselves.  Sigh.
     *
     * And, beleive it or not, some ruserok()'s try an initgroup just
     * for the hell of it.  Since we probably aren't root at this point
     * it'll fail, and initgroup "helpfully" will blatt "Setgroups: Not owner"
     * into our stderr output even though the initgroup failure is not a
     * problem and is expected.  Thanks a lot.  Not.
     */
    chdir(pwptr->pw_dir);       /* pamper braindead ruserok's */
#ifndef USE_AMANDAHOSTS
    close(2);                   /*  " */

    if(ruserok(remotehost, myuid == 0, remoteuser, localuser) == -1) {
	dup2(1,2);
	sprintf(errstr, "[access as %s not allowed from %s@%s]",
		localuser, remoteuser, remotehost);
	dbprintf(("check failed: %s\n", errstr));
	return 0;
    }

    dup2(1,2);
    chdir("/tmp");      /* now go someplace where I can drop core :-) */
    dbprintf(("bsd security check passed\n"));
    return 1;
#else
    /* We already chdired to ~amandauser */
    fPerm = fopen(".amandahosts", "r");
    if(!fPerm) {
	sprintf(errstr, 
	    "[access as %s not allowed from %s@%s] could not open .amandahosts", 
		localuser, remoteuser, remotehost);
	dbprintf(("check failed: %s\n", errstr));
	return 0;
    }

    while(fgets(pbuf, sizeof(pbuf), fPerm)) {
   	/* Wipe out \n */
	if( (ptmp = strchr(pbuf, '\n')) )
	    *ptmp = '\0';

	/* Find first non white space */
	for(ptmp = pbuf; *ptmp && *ptmp!=' ' && *ptmp!='\t' ; ptmp++ )
	    ;

	/* If there is no whitespace skip this line */
	if( !*ptmp )
	    continue;
	*ptmp = '\0';

	/* Find first non-white space */
	for( ptmp++; *ptmp == ' ' || *ptmp == '\t' ; ptmp++ )
	    ;

	/* If we reach the NUL terminator first, skip this line */
	if( !*ptmp )
	    continue;

	if( !strcmp(pbuf, remotehost) && !strcmp(ptmp, remoteuser) ) {
	    amandahostsauth = 1;
	    break;
	}
    }
    fclose(fPerm);

    if( amandahostsauth ) {
	chdir("/tmp");      /* now go someplace where I can drop core :-) */
	dbprintf(("amandahosts security check passed\n"));
	return 1;
    }

    sprintf(errstr, "[access as %s not allowed from %s@%s]",
	    localuser, remoteuser, remotehost);
    dbprintf(("check failed: %s\n", errstr));

    return 0;

#endif
}

#else	/* ! BSD_SECURITY */

int bsd_security_ok(msg)
pkt_t *msg;
{
    return 1;
}

#endif /* ! BSD_SECURITY */

