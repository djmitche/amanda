/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1999 University of Maryland at College Park
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
 * $Id: amandad.c,v 1.18 2006/08/21 20:17:09 martinea Exp $
 *
 * handle client-host side of Amanda network communications, including
 * security checks, execution of the proper service, and acking the
 * master side
 */

#include "amanda.h"
#include "amandad.h"
#include "clock.h"
#include "event.h"
#include "amfeatures.h"
#include "packet.h"
#include "version.h"
#include "auth.h"
#include "stream.h"
#include "util.h"
#include "conffile.h"
#include "security.h"

#define	REQ_TIMEOUT	(60)		/* secs for client to send request */
#define	REP_TIMEOUT	(6*60*60)	/* secs for service to reply */
#define	ACK_TIMEOUT  	10		/* seconds to wait for an ACK to our REP */
#define STDERR_PIPE (DATA_FD_COUNT + 1)

#define amandad_debug(i, ...) do {	\
	if ((i) <= debug_amandad) {	\
		dbprintf(__VA_ARGS__);	\
	}				\
} while (0)

/*
 * These are the actions for entering the state machine
 */
typedef enum { A_START, A_RECVPKT, A_RECVREP, A_PENDING, A_FINISH, A_CONTINUE,
    A_SENDNAK, A_TIMEOUT } action_t;

/*
 * This is a state in the state machine.  It is a function pointer to
 * the function that actually implements the state.
 */
struct active_service;
typedef action_t (*state_t)(struct active_service *, action_t, pkt_t *);

/* string that we scan for in sendbackup's MESG stream */
static const char info_end_str[] = "sendbackup: info end\n";
#define INFO_END_LEN (sizeof(info_end_str)-1)

/* 
 * Here are the services that we allow.
 * Must be in the same order as services[].
 */
typedef enum {
    SERVICE_NOOP,
    SERVICE_SENDSIZE,
    SERVICE_SENDBACKUP,
    SERVICE_SELFCHECK,
    SERVICE_AMINDEXD,
    SERVICE_AMIDXTAPED,
    SERVICE_AMDUMPD
} service_t;

static struct services {
    char *name;
    int  active;
    service_t service;
} services[] = {
   { "noop", 1, SERVICE_NOOP },
   { "sendsize", 1, SERVICE_SENDSIZE },
   { "sendbackup", 1, SERVICE_SENDBACKUP },
   { "selfcheck", 1, SERVICE_SELFCHECK },
   { "amindexd", 0, SERVICE_AMINDEXD },
   { "amidxtaped", 0, SERVICE_AMIDXTAPED },
   { "amdumpd", 0, SERVICE_AMDUMPD }
};
#define NSERVICES G_N_ELEMENTS(services)

/*
 * This structure describes an active running service.
 *
 * An active service is something running that we have received a request for.
 * This structure holds info on that service, including file descriptors for
 * data, etc, as well as the auth connection for communications with the amanda
 * server.
 */
struct active_service {
    service_t service;			/* service name */
    char *cmd;				/* name of command we ran */
    char *arguments;			/* arguments we sent it */
    AuthConn *conn;			/* remote server */
    state_t state;			/* how far this has progressed */
    pid_t pid;				/* pid of subprocess */
    int send_partial_reply;		/* send PREP packet */
    int reqfd;				/* pipe to write requests */
    int repfd;				/* pipe to read replies */
    int errfd;				/* pipe to read stderr */
    event_handle_t *ev_repfd;		/* read event handle for repfd */
    event_handle_t *ev_reptimeout;	/* timeout for rep data */
    event_handle_t *ev_errfd;		/* read event handle for errfd */
    pkt_t rep_pkt;			/* rep packet we're sending out */
    char *errbuf;			/* buffer to read the err into */
    char *repbuf;			/* buffer to read the rep into */
    size_t bufsize;			/* length of repbuf */
    size_t repbufsize;			/* length of repbuf */
    int repretry;			/* times we'll retry sending the rep */
    int seen_info_end;			/* have we seen "sendbackup info end\n"? */
    char info_end_buf[INFO_END_LEN];	/* last few bytes read, used for scanning for info end */

    /*
     * General user streams to the process, and their equivalent
     * network streams.
     */
    struct datafd_handle {
	int fd_read;			/* pipe to child process */
	int fd_write;			/* pipe to child process */
	event_handle_t *ev_read;	/* it's read event handle */
	AuthStream *stream;		/* stream to amanda server */
	struct active_service *as;	/* pointer back to our enclosure */
    } data[DATA_FD_COUNT];
    char databuf[NETWORK_BLOCK_BYTES];	/* buffer to relay data between pipes and streams */
};

/*
 * Globals
 */

/* Queue of outstanding requests that we are running.  Don't be deceived:
 * amandad can only support one service at a time. */
static GSList *serviceq = NULL;

/* current authentication mechansim and connection */
static char *auth = NULL;
static AuthConn *amandad_conn = NULL;

/* Timer until forced amandad exit (used for UDP-based auths) */
static event_handle_t *exit_timer = NULL;

static kencrypt_type amandad_kencrypt = KENCRYPT_NONE;

/*
 * Prototypes
 */

int main(int argc, char **argv);

static int allocstream(struct active_service *, int);
static void log_packet(AuthConn *conn, pkt_t *pkt);
static void listen_callback(gpointer arg, AuthConn *conn, auth_operation_status_t status);
static void initial_recvpkt_callback(gpointer arg, AuthConn *conn,
    pkt_t *pkt, auth_operation_status_t status);
static void state_machine(struct active_service *, action_t, pkt_t *);

static action_t s_sendack(struct active_service *, action_t, pkt_t *);
static action_t s_repwait(struct active_service *, action_t, pkt_t *);
static action_t s_processrep(struct active_service *, action_t, pkt_t *);
static action_t s_sendrep(struct active_service *, action_t, pkt_t *);
static action_t s_ackwait(struct active_service *, action_t, pkt_t *);
static action_t s_pipewait(struct active_service *, action_t, pkt_t *);

static void repfd_recv(void *);
static void process_errfd(struct active_service *as);
static void errfd_recv(void *);
static void timeout_repfd(void *);
static void protocol_recv(gpointer arg, AuthConn *conn, pkt_t *pkt,
	auth_operation_status_t status);
static void process_pipe_to_stream(void *);
static void process_stream_to_pipe(gpointer arg, AuthStream *stream,
	gpointer buf, gsize size);
static struct active_service *service_new(AuthConn *,
    const char *, service_t, const char *);
static void service_delete(struct active_service *);
static int writebuf(struct active_service *, const void *, size_t);
static gboolean do_sendpkt(AuthConn *conn, pkt_t *pkt);
static char *amandad_conf_getter (char *, void *);

static void clear_exit_timer(void);

static const char *state2str(state_t);
static const char *action2str(action_t);

int
main(
    int		argc,
    char **	argv)
{
    int i;
    guint j;
    int have_services;
    char *pgm = "amandad";		/* in case argv[0] is not set */
    char **auth_argv;
    int auth_argc;

    /*
     * Configure program for internationalization:
     *   1) Only set the message locale for now.
     *   2) Set textdomain for all amanda related programs to "amanda"
     *      We don't want to be forced to support dozens of message catalogs.
     */  
    setlocale(LC_MESSAGES, "C");
    textdomain("amanda"); 

    safe_fd(-1, 0);
    safe_cd();

    /*
     * Nexenta needs the SUN_PERSONALITY env variable to be unset, otherwise
     * the Sun version of tar in /usr/sun/sbin/tar is called instead.
     *
     * On other operating systems this will have no effect.
     */

#ifdef HAVE_UNSETENV
    unsetenv("SUN_PERSONALITY");
#endif

    /*
     * When called via inetd, it is not uncommon to forget to put the
     * argv[0] value on the config line.  On some systems (e.g. Solaris)
     * this causes argv and/or argv[0] to be NULL, so we have to be
     * careful getting our name.
     */
    if (! (argc >= 1 && argv != NULL && argv[0] != NULL)) {
	dbprintf(_("WARNING: argv[0] not defined: check inetd.conf\n"));
    }

    if ((argv == NULL) || (argv[0] == NULL)) {
	    pgm = "amandad";		/* in case argv[0] is not set */
    } else {
	    pgm = basename(argv[0]);	/* Strip of leading path get debug name */
    }
    set_pname(pgm);
    dbopen(DBG_SUBDIR_AMANDAD);

    g_assert(argv != NULL);

    /* Don't die when child closes pipe */
    signal(SIGPIPE, SIG_IGN);

    /* Parse the configuration; we'll handle errors later */
    config_init(CONFIG_INIT_CLIENT, NULL);

    /* use check_running_as according to how we find ourselves run; the auth
     * mechanisms will check in more detail later */
    if (geteuid() == 0) {
	check_running_as(RUNNING_AS_ROOT);
	initgroups(CLIENT_LOGIN, get_client_gid());
	setgid(get_client_gid());
	setegid(get_client_gid());
	seteuid(get_client_uid());
    } else {
	check_running_as(RUNNING_AS_CLIENT_LOGIN);
    }

    add_amanda_log_handler(amanda_log_stderr);
    add_amanda_log_handler(amanda_log_syslog);

    /*
     * ad-hoc argument parsing
     *
     * We accept	-auth=[authentication type]
     *			-no-exit
     *			service names
     * Anything else is passed to the auth's accept method (auth_argv/auth_argc)
     */
    have_services = 0;
    auth_argv = g_new0(char *, argc);
    auth_argc = 0;
    for (i = 1; i < argc; i++) {
	/*
	 * Get a driver for a security type specified after -auth=
	 */
	if (strncmp(argv[i], "-auth=", strlen("-auth=")) == 0) {
	    argv[i] += strlen("-auth=");
	    auth = argv[i];

	    /* activate all services for local, rsh, or ssh */
	    /* TODO: do not special-case this in amandad! */
	    if (strcmp(auth, "local") == 0 ||
		strcmp(auth, "rsh") == 0 ||
		strcmp(auth, "ssh") == 0) {
		guint i;
		for (i=0; i < NSERVICES; i++) {
		    services[i].active = 1;
		}
	    }
	    continue;
	}

	/*
	 * -no-exit is no longer used, but still recognized for backward-compat
	 */
	else if (strcmp(argv[i], "-no-exit") == 0) {
	    g_debug("ignoring -no-exit");
	    continue;
	}

	/*
	 * If it does not begin with a dash, it must be a service name
	 */
	else if (argv[i][0] != '-') {
	    /* clear the default active services */
	    if(!have_services) {
		for (j = 0; j < NSERVICES; j++)
		    services[j].active = 0;
	    }
	    have_services = 1;

	    if(strcmp(argv[i],"amdump") == 0) {
		services[0].active = 1;
		services[1].active = 1;
		services[2].active = 1;
		services[3].active = 1;
	    }
	    else {
		for (j = 0; j < NSERVICES; j++)
		    if (strcmp(services[j].name, argv[i]) == 0)
			break;
		if (j == NSERVICES) {
		    dbprintf(_("%s: invalid service\n"), argv[i]);
		    exit(1);
		}
		services[j].active = 1;
	    }
	}

	/* otherwise, pass it to the auth */
	else {
	    g_debug("passing %s to the authentication implementation", argv[i]);
	    auth_argv[auth_argc++] = argv[i];
	}
    }

    /*
     * If no authentication type specified, use BSDTCP
     */
    if (auth == NULL) {
	auth = "bsdtcp";
    }

    /* initialize */

    startclock();

    dbprintf(_("version %s\n"), VERSION);
    for (i = 0; version_info[i] != NULL; i++) {
	dbprintf("    %s", version_info[i]);
    }

    /*
     * Create a connection object for this invocation
     */
    auth_listen(auth, listen_callback, NULL,
	    amandad_conf_getter, NULL, auth_argc, auth_argv);

    /* run the event loop forever, or until someone calls event_loop_stop. */
    event_loop_forever();

    dbclose();
    return(0);
}

static void
log_packet(
    AuthConn *conn,
    pkt_t *pkt)
{
    amandad_debug(1, "conn %p received %s packet:\n<<<<<\n%s>>>>>",
	conn, pkt_type2str(pkt->type), pkt->body);
}

static void
listen_callback(
    gpointer arg G_GNUC_UNUSED,
    AuthConn *conn,
    auth_operation_status_t status)
{
    if (status != AUTH_OK) {
	g_warning("error listening for new connection - exiting");
	event_loop_stop();
	return;
    }

    AUTH_DEBUG(1, "new connection %p: authenticated peer name '%s'",
		conn, auth_conn_get_authenticated_peer_name(conn));
    amandad_conn = conn;

    /* and start receiving a packet on it */
    auth_conn_recvpkt(conn, initial_recvpkt_callback, NULL, REQ_TIMEOUT);
}

static void
initial_recvpkt_callback(
    gpointer arg G_GNUC_UNUSED,
    AuthConn *conn,
    pkt_t *pkt,
    auth_operation_status_t status)
{
    pkt_t pkt_out;
    GSList *iter;
    struct active_service *as;
    char *pktbody, *tok, *service, *arguments;
    char *service_path = NULL;
    GSList *errlist = NULL;
    guint i;

    if (status == AUTH_TIMEOUT) {
	/* for auths that cannot detect and underlying EOF, this is how we
	 * exit.  This also provides a nice timeout for stream-based auths. */
	g_debug("no packet received in %d seconds; exiting", REQ_TIMEOUT);
	g_object_unref(conn);
	event_loop_stop();
	return;
    } else if (status == AUTH_ERROR) {
	g_warning("error waiting for packet on connection %p: %s",
		conn, auth_conn_error_message(conn));
	g_object_unref(conn);
	return;
    } else if (pkt == NULL) {
	/* EOF on the underlying layer - exit cleanly */
	g_debug("EOF from remote system; exiting");
	g_object_unref(conn);
	event_loop_stop();
	return;
    }
    g_assert(pkt != NULL);

    log_packet(conn, pkt);

    /* If we're already running a service, we can't run another - the packets
     * aren't tagged to a specific service, so things would get all jumbled.
     * This should never happen, since this callback is only registered when no
     * service is active. */
    if (serviceq) {
	g_warning("a service is already active; ignoring incoming packet");
	return;
    }

    pkt_out.body = NULL;

    /*
     * If we have errors (not warnings) from the config file, let the remote system
     * know immediately.  Unfortunately, we only get one ERROR line, so if there
     * are multiple errors, we just show the first.
     */
    if (config_errors(&errlist) >= CFGERR_ERRORS) {
	GSList *iter = errlist;
	char *errmsg;
	gboolean multiple_errors = FALSE;

	if (iter) {
	    errmsg = (char *)iter->data;
	    if (iter->next)
		multiple_errors = TRUE;
	} else {
	    errmsg = "(no error message)";
	}

	pkt_init(&pkt_out, P_NAK, "ERROR %s%s", errmsg,
	    multiple_errors? _(" (additional errors not displayed)"):"");
	(void)do_sendpkt(conn, &pkt_out);
	amfree(pkt_out.body);
	g_object_unref(conn);
	return;
    }

    pktbody = service = arguments = NULL;
    as = NULL;

    /*
     * Parse out the service and arguments
     */

    if (pkt->type != P_REQ)
	goto badreq;

    pktbody = g_strdup(pkt->body);

    tok = strtok(pktbody, " ");
    if (tok == NULL)
	goto badreq;
    if (strcmp(tok, "SERVICE") != 0)
	goto badreq;

    tok = strtok(NULL, " \n");
    if (tok == NULL)
	goto badreq;
    service = g_strdup(tok);

    /* we call everything else 'arguments' */
    tok = strtok(NULL, "");
    if (tok == NULL)
	goto badreq;
    arguments = g_strdup(tok);

    /* see if it's one we allow */
    for (i = 0; i < NSERVICES; i++)
	if (services[i].active == 1 && strcmp(services[i].name, service) == 0)
	    break;
    if (i == NSERVICES) {
	dbprintf(_("%s: invalid service\n"), service);
	pkt_init(&pkt_out, P_NAK, _("ERROR %s: invalid service, add '%s' as argument to amandad\n"), service, service);
	goto send_pkt_out;
    }

    service_path = g_strjoin(NULL, amlibexecdir, "/", service, NULL);
    if (access(service_path, X_OK) < 0) {
	dbprintf(_("can't execute %s: %s\n"), service_path, strerror(errno));
	    pkt_init(&pkt_out, P_NAK,
		     _("ERROR execute access to \"%s\" denied\n"),
		     service_path);
	goto send_pkt_out;
    }

    /* see if its already running */
    for (iter = serviceq; iter != NULL; iter = g_slist_next(iter)) {
	as = (struct active_service *)iter->data;
	    if (strcmp(as->cmd, service_path) == 0 &&
		strcmp(as->arguments, arguments) == 0) {
		    dbprintf(_("%s %s: already running, acking req\n"),
			service, arguments);
		    pkt_init_empty(&pkt_out, P_ACK);
		    clear_exit_timer();
		    goto send_pkt_out_no_delete;
	    }
    }

    /*
     * create a new service instance, and send the arguments down
     * the request pipe.
     */
    dbprintf(_("creating new service: %s\n%s\n"), service, arguments);
    as = service_new(conn, service_path,
		     services[i].service, arguments);
    if (writebuf(as, arguments, strlen(arguments)) < 0) {
	const char *errmsg = strerror(errno);
	dbprintf(_("error sending arguments to %s: %s\n"), service, errmsg);
	pkt_init(&pkt_out, P_NAK, _("ERROR error writing arguments to %s: %s\n"),
	    service, errmsg);
	goto send_pkt_out;
    }
    aclose(as->reqfd);

    amfree(pktbody);
    amfree(service);
    amfree(service_path);
    amfree(arguments);

    /*
     * Move to the sendack state, and start up the state
     * machine.
     */
    as->state = s_sendack;
    state_machine(as, A_START, NULL);
    return;

badreq:
    pkt_init(&pkt_out, P_NAK, _("ERROR invalid REQ\n"));
    dbprintf(_("%p: received invalid %s packet:\n<<<<<\n%s>>>>>\n\n"),
	conn, pkt_type2str(pkt->type), pkt->body);
    /* fall through */

send_pkt_out:
    if(as)
	service_delete(as);
    /* fall through */

send_pkt_out_no_delete:
    amfree(pktbody);
    amfree(service_path);
    amfree(service);
    amfree(arguments);
    (void)do_sendpkt(conn, &pkt_out);
    amfree(pkt_out.body);
}

static void
clear_exit_timer(void)
{
    if (exit_timer) {
	event_release(exit_timer);
	exit_timer = NULL;
    }
}

/*
 * Handles incoming protocol packets.  Routes responses to the proper
 * running service.
 */
static void
state_machine(
    struct active_service *	as,
    action_t			action,
    pkt_t *			pkt)
{
    action_t retaction;
    state_t curstate;
    pkt_t nak;

    amandad_debug(1, _("state_machine: %p entering\n"), as);
    for (;;) {
	curstate = as->state;
	amandad_debug(1, _("state_machine: %p curstate=%s action=%s\n"), as,
			  state2str(curstate), action2str(action));
	retaction = (*curstate)(as, action, pkt);
	amandad_debug(1, _("state_machine: %p curstate=%s returned %s (nextstate=%s)\n"),
			  as, state2str(curstate), action2str(retaction),
			  state2str(as->state));

	switch (retaction) {
	/*
	 * State has queued up and is now blocking on input.
	 */
	case A_PENDING:
	    amandad_debug(1, _("state_machine: %p leaving (A_PENDING)\n"), as);
	    return;

	/*
	 * service has switched states.  Loop.
	 */
	case A_CONTINUE:
	    break;

	/*
	 * state has determined that the packet it received was bogus.
	 * Send a nak, and return.
	 */
	case A_SENDNAK:
	    dbprintf(_("received unexpected %s packet\n"),
		pkt_type2str(pkt->type));
	    dbprintf(_("<<<<<\n%s----\n\n"), pkt->body);
	    pkt_init(&nak, P_NAK, _("ERROR unexpected packet type %s\n"),
		pkt_type2str(pkt->type));
	    (void)do_sendpkt(as->conn, &nak);
	    amfree(nak.body);
	    auth_conn_recvpkt(as->conn, protocol_recv, as, 0);
	    amandad_debug(1, _("state_machine: %p leaving (A_SENDNAK)\n"), as);
	    return;

	/*
	 * Service is done.  Remove it, and listen for a new protocol transaction
	 */
	case A_FINISH:
	    amandad_debug(1, _("state_machine: %p leaving (A_FINISH)\n"), as);
	    service_delete(as);
	    auth_conn_recvpkt(amandad_conn, initial_recvpkt_callback, NULL, REQ_TIMEOUT);
	    return;

	default:
	    assert(0);
	    break;
	}
    }
    /*NOTREACHED*/
}

/*
 * This state just sends an ack.  After that, we move to the repwait
 * state to wait for REP data to arrive from the subprocess.
 */
static action_t
s_sendack(
    struct active_service *	as,
    action_t			action,
    pkt_t *			pkt)
{
    pkt_t ack;

    (void)action;	/* Quiet unused parameter warning */
    (void)pkt;		/* Quiet unused parameter warning */

    pkt_init_empty(&ack, P_ACK);
    if (!do_sendpkt(as->conn, &ack)) {
	dbprintf(_("error sending ACK: %s\n"),
	    auth_conn_error_message(as->conn));
	amfree(ack.body);
	return (A_FINISH);
    }
    amfree(ack.body);

    /*
     * move to the repwait state
     * Setup a listener for data on the reply fd, but also
     * listen for packets over the wire, as the server may
     * poll us if we take a long time.
     * Setup a timeout that will fire if it takes too long to
     * receive rep data.
     */
    as->state = s_repwait;
    as->ev_repfd = event_register((event_id_t)as->repfd, EV_READFD, repfd_recv, as);
    as->ev_reptimeout = event_register(REP_TIMEOUT, EV_TIME,
	timeout_repfd, as);
    as->errbuf = NULL;
    as->ev_errfd = event_register((event_id_t)as->errfd, EV_READFD, errfd_recv, as);
    auth_conn_recvpkt(as->conn, protocol_recv, as, 0);
    return (A_PENDING);
}

/**
 * Ensure enough space in a reply buffer, account for the final '\0'. The base
 * size of a buffer will be NETWORK_BLOCK_BYTES. If not enough place is
 * available, we double the buffer size, always. This function RELIES on the
 * 'repbufsize' and 'bufsize' members being correct. For added safety, we
 * require that if the buffer is declared to be empty according to its declared
 * size, its offset is 0 and its data pointer is NULL.
 *
 * @param as: the active_service
 * @param size: the size we need to write (without the final '\0')
 * @returns: TRUE if the buffer had to be expanded, FALSE otherwise
 */

static gboolean expand_reply_buffer(struct active_service *as, gsize size)
{
    gsize newsize = as->bufsize;
    gsize requested = as->repbufsize + size + 1;
    char *newbuf;

    if (newsize >= requested)
        return FALSE;

    if (!newsize) {
        g_assert(as->repbufsize == 0);
        g_assert(as->repbuf == NULL);
        newsize = NETWORK_BLOCK_BYTES;
    }

    while (newsize < requested)
        newsize *= 2;

    newbuf = g_realloc(as->repbuf, newsize);
    as->repbuf = newbuf;
    as->bufsize = newsize;
    return TRUE;
}

/*
 * This is the repwait state.  We have responded to the initial REQ with
 * an ACK, and we are now waiting for the process we spawned to pass us 
 * data to send in a REP.
 */
static action_t
s_repwait(
    struct active_service *	as,
    action_t			action,
    pkt_t *			pkt)
{
    ssize_t   n;
    char     *what;
    char     *msg;
    int       code = 0;
    int       pid;
    amwait_t  retstat;
    gboolean expanded;

    /*
     * We normally shouldn't receive any packets while waiting
     * for our REP data, but in some cases we do.
     */
    if (action == A_RECVPKT) {
	assert(pkt != NULL);
	/*
	 * Another req for something that's running.  Just send an ACK
	 * and go back and wait for more data.
	 */
	if (pkt->type == P_REQ) {
	    dbprintf(_("received dup P_REQ packet, ACKing it\n"));
	    amfree(as->rep_pkt.body);
	    pkt_init_empty(&as->rep_pkt, P_ACK);
	    (void)do_sendpkt(as->conn, &as->rep_pkt);
	    auth_conn_recvpkt(as->conn, protocol_recv, as, 0);
	    return (A_PENDING);
	}
	/* something unexpected.  Nak it */
	return (A_SENDNAK);
    }

    if (action == A_TIMEOUT) {
	amfree(as->rep_pkt.body);
	pkt_init(&as->rep_pkt, P_NAK, _("ERROR timeout on reply pipe\n"));
	dbprintf(_("%s timed out waiting for REP data\n"), as->cmd);
	(void)do_sendpkt(as->conn, &as->rep_pkt);
	return (A_FINISH);
    }

    assert(action == A_RECVREP);

    /*
     * Ensure we have at least NETWORK_BLOCK_BYTES available in the reply buffer
     */
    expanded = expand_reply_buffer(as, NETWORK_BLOCK_BYTES);

    do {
	n = read(as->repfd, as->repbuf + as->repbufsize,
		 as->bufsize - as->repbufsize - 1);
    } while ((n < 0) && ((errno == EINTR) || (errno == EAGAIN)));
    if (n < 0) {
	const char *errstr = strerror(errno);
	dbprintf(_("read error on reply pipe: %s\n"), errstr);
	amfree(as->rep_pkt.body);
	pkt_init(&as->rep_pkt, P_NAK, _("ERROR read error on reply pipe: %s\n"),
		 errstr);
	(void)do_sendpkt(as->conn, &as->rep_pkt);
	return (A_FINISH);
    }

    /*
     * At this point, we know that n >= 0. The two operations below are
     * therefore pretty safe even if n is 0, in which case the current reply
     * buffer won't be changed at all.
     */

    as->repbufsize += n;
    as->repbuf[as->repbufsize] = '\0';

    /* If end of service, wait for process status */
    if (n == 0) {
	pid = waitpid(as->pid, &retstat, WNOHANG);
	if (as->service  == SERVICE_NOOP ||
	    as->service  == SERVICE_SENDSIZE ||
	    as->service  == SERVICE_SELFCHECK) {
	    int t = 0;
	    while (t<5 && pid == 0) {
		sleep(1);
		t++;
		pid = waitpid(as->pid, &retstat, WNOHANG);
	    }
	}

	process_errfd(as);

	if (pid == 0)
	    pid = waitpid(as->pid, &retstat, WNOHANG);

	if (pid > 0) {
	    what = NULL;
	    if (! WIFEXITED(retstat)) {
		what = _("signal");
		code = WTERMSIG(retstat);
	    } else if (WEXITSTATUS(retstat) != 0) {
		what = _("code");
		code = WEXITSTATUS(retstat);
	    }
	    if (what) {
		dbprintf(_("service %s failed: pid %u exited with %s %d\n"),
			 (as->cmd)?as->cmd:_("??UNKONWN??"),
			 (unsigned)as->pid,
			 what, code);
		msg = g_strdup_printf(
		     _("ERROR service %s failed: pid %u exited with %s %d\n"),
		     (as->cmd)?as->cmd:_("??UNKONWN??"), (unsigned)as->pid,
		     what, code);
		expand_reply_buffer(as, strlen(msg));
		strcpy(as->repbuf + as->repbufsize, msg);
		as->repbufsize += strlen(msg);
                amfree(msg);
	    }
	}
    }

    /*
     * If we got some data, go back and wait for more, or EOF.
     */

    if (n > 0) {
	if (!expanded && as->send_partial_reply) {
	    amfree(as->rep_pkt.body);
	    pkt_init(&as->rep_pkt, P_PREP, "%s", as->repbuf);
	    (void)do_sendpkt(as->conn, &as->rep_pkt);
	    amfree(as->rep_pkt.body);
	    pkt_init_empty(&as->rep_pkt, P_REP);
	}
 
	return (A_PENDING);
    }

    /*
     * If we got 0, then we hit EOF.  Process the data and release
     * the timeout.
     */
    assert(n == 0);

    assert(as->ev_repfd != NULL);
    event_release(as->ev_repfd);
    as->ev_repfd = NULL;

    assert(as->ev_reptimeout != NULL);
    event_release(as->ev_reptimeout);
    as->ev_reptimeout = NULL;

    as->state = s_processrep;
    aclose(as->repfd);
    return (A_CONTINUE);
}

/*
 * After we have read in all of the rep data, we process it and send
 * it out as a REP packet.
 */
static action_t
s_processrep(
    struct active_service *	as,
    action_t			action,
    pkt_t *			pkt)
{
    char *tok, *repbuf;

    (void)action;	/* Quiet unused parameter warning */
    (void)pkt;		/* Quiet unused parameter warning */

    /*
     * Copy the rep lines into the outgoing packet.
     *
     * If this line is a CONNECT, translate it
     * Format is "CONNECT <tag> <handle> <tag> <handle> etc...
     * Example:
     *
     *  CONNECT DATA 4 MESG 5 INDEX 6
     *
     * The tags are arbitrary.  The handles are in the DATA_FD pool.
     * We need to map these to security streams and pass them back
     * to the amanda server.  If the handle is -1, then we don't map.
     */
    if (strncmp_const(as->repbuf,"KENCRYPT\n") == 0) {
        amandad_kencrypt = KENCRYPT_WILL_DO;
	repbuf = g_strdup(as->repbuf + 9);
    } else {
	repbuf = g_strdup(as->repbuf);
    }
    amfree(as->rep_pkt.body);
    pkt_init_empty(&as->rep_pkt, P_REP);
    tok = strtok(repbuf, " ");
    if (tok == NULL)
	goto error;
    if (strcmp(tok, "CONNECT") == 0) {
	char *line, *nextbuf;

	/* Save the entire line */
	line = strtok(NULL, "\n");
	/* Save the buf following the line */
	nextbuf = strtok(NULL, "");

	if (line == NULL || nextbuf == NULL)
	    goto error;

	pkt_cat(&as->rep_pkt, "CONNECT");

	/* loop over the id/handle pairs */
	for (;;) {
	    /* id */
	    tok = strtok(line, " ");
	    line = NULL;	/* keep working from line */
	    if (tok == NULL)
		break;
	    pkt_cat(&as->rep_pkt, " %s", tok);

	    /* handle */
	    tok = strtok(NULL, " \n");
	    if (tok == NULL)
		goto error;
	    /* convert the handle into something the server can process */
	    pkt_cat(&as->rep_pkt, " %d", allocstream(as, atoi(tok)));
	}
	pkt_cat(&as->rep_pkt, "\n%s", nextbuf);
    } else {
error:
	pkt_cat(&as->rep_pkt, "%s", as->repbuf);
    }

    /*
     * We've setup our REP packet in as->rep_pkt.  Now move to the transmission
     * state.
     */
    as->state = s_sendrep;
    as->repretry = getconf_int(CNF_REP_TRIES);
    amfree(repbuf);
    return (A_CONTINUE);
}

/*
 * This is the state where we send the REP we just collected from our child.
 */
static action_t
s_sendrep(
    struct active_service *	as,
    action_t			action,
    pkt_t *			pkt)
{
    (void)action;	/* Quiet unused parameter warning */
    (void)pkt;		/* Quiet unused parameter warning */

    /*
     * Transmit it and move to the ack state.
     */
    (void)do_sendpkt(as->conn, &as->rep_pkt);
    auth_conn_recvpkt(as->conn, protocol_recv, as, ACK_TIMEOUT);
    as->state = s_ackwait;
    return (A_PENDING);
}

/*
 * This is the state in which we wait for the server to ACK the REP
 * we just sent it.
 */
static action_t
s_ackwait(
    struct active_service *	as,
    action_t			action,
    pkt_t *			pkt)
{
    struct datafd_handle *dh;
    int npipes;

    /*
     * If we got a timeout, try again, but eventually give up.
     */
    if (action == A_TIMEOUT) {
	if (--as->repretry > 0) {
	    as->state = s_sendrep;
	    return (A_CONTINUE);
	}
	dbprintf(_("timeout waiting for ACK for our REP\n"));
	return (A_FINISH);
    }
    amandad_debug(1, _("received ACK, now opening streams\n"));

    assert(action == A_RECVPKT);

    if (pkt->type == P_REQ) {
	dbprintf(_("received dup P_REQ packet, resending REP\n"));
	as->state = s_sendrep;
	return (A_CONTINUE);
    }

    if (pkt->type != P_ACK)
	return (A_SENDNAK);

    if (amandad_kencrypt == KENCRYPT_WILL_DO) {
	amandad_kencrypt = KENCRYPT_YES;
    }

    /*
     * Got the ack, now open the pipes
     */
    for (dh = &as->data[0]; dh < &as->data[DATA_FD_COUNT]; dh++) {
	if (dh->stream == NULL)
	    continue;
	dbprintf("opening security stream for fd %d\n", (int)(dh - as->data) + DATA_FD_OFFSET);
	if (auth_stream_accept(dh->stream) < 0) {
	    dbprintf(_("stream %td accept failed: %s\n"),
		dh - &as->data[0], auth_stream_error_message(dh->stream));
	    g_object_unref(dh->stream);
	    dh->stream = NULL;
	    continue;
	}

	/* setup an event for reads from it.  As a special case, don't start
	 * listening on as->data[0] until we read some data on another fd, if
	 * the service is sendbackup.  This ensures that we send a MESG or 
	 * INDEX token before any DATA tokens, as dumper assumes. This is a
	 * hack, if that wasn't already obvious! */
	if (dh != &as->data[0] || as->service != SERVICE_SENDBACKUP) {
	    dh->ev_read = event_register((event_id_t)dh->fd_read, EV_READFD,
					 process_pipe_to_stream, dh);
	} else {
	    amandad_debug(1, "Skipping registration of sendbackup's data FD\n");
	}

	auth_stream_read(dh->stream, process_stream_to_pipe, dh);

    }

    /*
     * Pipes are open, so count them.
     */
    for (npipes = 0, dh = &as->data[0]; dh < &as->data[DATA_FD_COUNT]; dh++) {
	if (dh->stream != NULL)
	    npipes++;
    }

    /*
     * If no pipes are open, then we're done.  Otherwise, just start running.
     * The event handlers on all of the pipes will take it from here.
     */
    amandad_debug(1, _("at end of s_ackwait, npipes is %d\n"), npipes);
    if (npipes == 0) {
	return (A_FINISH);
    } else {
	as->state = s_pipewait;
	return (A_PENDING);
    }
}

/*
 * In this state, we are waiting for npipes to become 0, then finish.
 */
static action_t
s_pipewait(
    struct active_service *	as,
    action_t			action,
    pkt_t *			pkt)
{
    struct datafd_handle *dh;
    int npipes;

    /* we should only get A_FINISH in thi state - from process_pipe_to_stream
     * and process_stream_to_pipe.  No recvpkt call is active. */
    g_assert(!pkt);
    g_assert(action == A_FINISH);

    npipes = 0;
    for (dh = &as->data[0]; dh < &as->data[DATA_FD_COUNT]; dh++) {
	if (dh->stream)
	    npipes++;
    }
    amandad_debug(1, _("s_pipewait: npipes is %d\n"), npipes);

    if (npipes == 0) {
	return (A_FINISH);
    } else {
	/* keep waiting */
	return (A_PENDING);
    }
}

/*
 * Called when a repfd has received data
 */
static void
repfd_recv(
    void *	cookie)
{
    struct active_service *as = cookie;

    assert(as != NULL);
    assert(as->ev_repfd != NULL);

    state_machine(as, A_RECVREP, NULL);
}

static void
process_errfd(
    struct active_service *as)
{
    /* Process errfd before sending the REP packet */
    if (as->ev_errfd) {
	SELECT_ARG_TYPE readset;
	struct timeval  tv;
	int             nfound;

	memset(&tv, 0, sizeof(tv));
	FD_ZERO(&readset);
	FD_SET(as->errfd, &readset);
	nfound = select(as->errfd+1, &readset, NULL, NULL, &tv);
	if (nfound && FD_ISSET(as->errfd, &readset)) {
	    errfd_recv(as);
	}
    }
}

/*
 * Called when a errfd has received data
 */
static void
errfd_recv(
    void *	cookie)
{
    struct active_service *as = cookie;
    char  buf[NETWORK_BLOCK_BYTES + 1];
    int   n;
    gsize buflen = 0;
    char *r;

    assert(as != NULL);
    assert(as->ev_errfd != NULL);

    n = read(as->errfd, buf, NETWORK_BLOCK_BYTES);

    /*
     * Append whatever was read from the error file descriptor into the error
     * buffer - or the read error message if there is one.
     *
     * In case when no data is read, or a read error, we release the event.
     */

    switch (n) {
        case -1:
            g_snprintf(buf, NETWORK_BLOCK_BYTES + 1,
                "error reading stderr or service: %s\n", strerror(errno));
            buflen = strlen(buf) + 1;
            /* Fall through */
        case 0:
            event_release(as->ev_errfd);
            as->ev_errfd = NULL;
            break;
        default:
            buf[n] = '\0';
            buflen = n;
    }

    if (buflen) {
	if (as->errbuf) {
	    as->errbuf = vstrextend(&as->errbuf, buf, NULL);
	} else {
	    as->errbuf = g_strdup(buf);
	}
    }

    /* for each line terminate by '\n' */
    while (as->errbuf != NULL  && (r = strchr(as->errbuf, '\n')) != NULL) {
	char *s;

	*r = '\0';
	s = g_strdup_printf("ERROR service %s: %s\n",
		       services[as->service].name, as->errbuf);

	/* Add to repbuf, error message will be in the REP packet if it
	 * is not already sent
	 */
	n = strlen(s);
	expand_reply_buffer(as, n);

	memcpy(as->repbuf + as->repbufsize, s, n);
	as->repbufsize += n;

	dbprintf("%s", s);
	amfree(s);

	/* remove first line from buffer */
	r++;
	s = g_strdup(r);
	amfree(as->errbuf);
	as->errbuf = s;
    }
}

/*
 * Called when a repfd has timed out
 */
static void
timeout_repfd(
    void *	cookie)
{
    struct active_service *as = cookie;

    assert(as != NULL);
    assert(as->ev_reptimeout != NULL);

    state_machine(as, A_TIMEOUT, NULL);
}

/*
 * Called when a handle has received data
 */
static void
protocol_recv(
    gpointer arg,
    AuthConn *conn G_GNUC_UNUSED,
    pkt_t *pkt,
    auth_operation_status_t status)
{
    struct active_service *as = arg;

    assert(as != NULL);

    switch (status) {
    case AUTH_OK:
	amandad_debug(1, _("received %s pkt:\n<<<<<\n%s>>>>>\n"),
			pkt_type2str(pkt->type), pkt->body);
	state_machine(as, A_RECVPKT, pkt);
	break;
    case AUTH_TIMEOUT:
	g_debug("recvpkt timeout");
	state_machine(as, A_TIMEOUT, NULL);
	break;
    case AUTH_ERROR:
	g_debug("recvpkt error: %s",
	    auth_conn_error_message(as->conn));
	break;
    }
}

/*
 * This is a generic relay function that just reads data from one of
 * the process's pipes and passes it up the equivalent AuthStream
 */
static void
process_pipe_to_stream(
    void *	cookie)
{
    pkt_t nak;
    struct datafd_handle *dh = cookie;
    struct active_service *as = dh->as;
    ssize_t n;

    nak.body = NULL;

    do {
	n = read(dh->fd_read, as->databuf, sizeof(as->databuf));
    } while ((n < 0) && ((errno == EINTR) || (errno == EAGAIN)));

    amandad_debug(1, "process_pipe_to_stream; read returned %zd\n", n);

    /*
     * Process has died.
     */
    if (n < 0) {
	pkt_init(&nak, P_NAK, _("A ERROR data descriptor %d broken: %s\n"),
	    dh->fd_read, strerror(errno));
	goto sendnak;
    }
    /*
     * Process has closed the pipe.  Just remove this event handler.
     * If all pipes are closed, shut down this service.
     */
    if (n == 0) {
	goto close_and_bail;
    }

    /* Handle the special case of recognizing "sendbackup info end"
     * from sendbackup's MESG fd */
    if (as->service == SERVICE_SENDBACKUP && !as->seen_info_end && dh == &as->data[1]) {
	/* make a buffer containing the combined data from info_end_buf
	 * and what we've read this time, and search it for info_end_strj
	 * This includes a NULL byte for strstr's sanity. */
	char *combined_buf = malloc(INFO_END_LEN + n + 1);
	memcpy(combined_buf, as->info_end_buf, INFO_END_LEN);
	memcpy(combined_buf+INFO_END_LEN, as->databuf, n);
	combined_buf[INFO_END_LEN+n] = '\0';

	as->seen_info_end = (strstr(combined_buf, info_end_str) != NULL);

	/* fill info_end_buf from the tail end of combined_buf */
	memcpy(as->info_end_buf, combined_buf + n, INFO_END_LEN);
	amfree(combined_buf);

	/* if we did see info_end_str, start reading the data fd (fd 0) */
	if (as->seen_info_end) {
	    struct datafd_handle *dh = &as->data[0];
	    amandad_debug(1, "Opening datafd to sendbackup (delayed until sendbackup sent header info)\n");
	    dh->ev_read = event_register((event_id_t)dh->fd_read, EV_READFD,
					 process_pipe_to_stream, dh);
	} else {
	    amandad_debug(1, "sendbackup header info still not complete\n");
	}
    }

    if (!auth_stream_write(dh->stream, as->databuf, (size_t)n)) {
	/* stream has croaked */
	g_object_unref(dh->stream);
	dh->stream = NULL;

	pkt_init(&nak, P_NAK, _("ERROR write error on stream %d: %s\n"),
	    dh->stream->id, auth_stream_error_message(dh->stream));
	goto sendnak;
    }
    return;

sendnak:
    (void)do_sendpkt(as->conn, &nak);
    amfree(nak.body);

close_and_bail:
    /* close down this fd and possibly the stream, and then let the state
     * machine know things have changed*/

    amandad_debug(1, "closing from-service side of fd %d\n",
		    (int)(dh - dh->as->data) + DATA_FD_OFFSET);

    if (dh->ev_read) {
	event_release(dh->ev_read);
	dh->ev_read = NULL;
    }
    aclose(dh->fd_read);

    /* close the stream uncondtionally (XXX what if the client was still
     * sending data?  This doesn't happen in current Amanda services, but it
     * might..) */
    if (dh->stream) {
	g_object_unref(dh->stream);
	dh->stream = NULL;
    }

    /* run the state machine to see if we're done */
    state_machine(as, A_FINISH, NULL);
}

/*
 * This is a generic relay function that just read data from one of
 * the connections and passes it up the equivalent process's pipes
 */
static void
process_stream_to_pipe(
    gpointer arg,
    AuthStream *stream,
    gpointer buf,
    gsize size)
{
    struct datafd_handle *dh = arg;

    g_assert(dh != NULL);
    g_assert(dh->stream == stream);

    if (dh->fd_write <= 0) {
	dbprintf(_("process_stream_to_pipe: dh->fd_write <= 0\n"));
    } else if (size > 0) {
	full_write(dh->fd_write, buf, (size_t)size);
	auth_stream_read(dh->stream, process_stream_to_pipe, dh);
    }
    else {
	amandad_debug(1, "got EOF; closing both sides of fd %d\n",
			(int)(dh - dh->as->data) + DATA_FD_OFFSET);

	aclose(dh->fd_read);
	aclose(dh->fd_write);

	if (dh->ev_read) {
	    event_release(dh->ev_read);
	    dh->ev_read = NULL;
	}

	if (dh->stream) {
	    g_object_unref(dh->stream);
	    dh->stream = NULL;
	}

	/* run the state machine to see if we're done */
	state_machine(dh->as, A_FINISH, NULL);
    }
}


/*
 * Convert a local stream handle (DATA_FD...) into something that
 * can be sent to the amanda server.
 *
 * Returns a number that should be sent to the server in the REP packet.
 */
static int
allocstream(
    struct active_service *	as,
    int				handle)
{
    struct datafd_handle *dh;

    /* note that handle is in the range DATA_FD_OFFSET to DATA_FD_COUNT, but
     * it is NOT a file descriptor! */

    /* if the handle is -1, then we don't bother */
    if (handle < 0)
	return (-1);

    /* make sure the handle's kosher */
    if (handle < DATA_FD_OFFSET || handle >= DATA_FD_OFFSET + DATA_FD_COUNT)
	return (-1);

    /* get a pointer into our handle array */
    dh = &as->data[handle - DATA_FD_OFFSET];

    /* make sure we're not already using the net handle */
    if (dh->stream != NULL)
	return (-1);

    /* allocate a stream from the auth API and return */
    dh->stream = auth_conn_stream_listen(as->conn);
    if (dh->stream == NULL) {
	g_debug("couldn't open stream to server: %s",
	    auth_conn_error_message(as->conn));
	return (-1);
    }

    /*
     * convert the stream into a numeric id that can be sent to the
     * remote end.
     */
    return dh->stream->id;
}

/*
 * Create a new service instance
 */
static struct active_service *
service_new(
    AuthConn *conn,
    const char *cmd,
    service_t service,
    const char *arguments)
{
    int i;
    int data_read[DATA_FD_COUNT + 2][2];
    int data_write[DATA_FD_COUNT + 2][2];
    struct active_service *as;
    pid_t pid;
    int newfd;
    char *peer_name;
    char *amanda_remote_host_env[2];

    assert(conn != NULL);
    assert(cmd != NULL);
    assert(arguments != NULL);

    /* a plethora of pipes */
    /* data_read[0]                : stdin
     * data_write[0]               : stdout
     * data_read[1], data_write[1] : first  stream
     * data_read[2], data_write[2] : second stream
     * data_read[3], data_write[3] : third stream
     * data_write[4]               : stderr
     */
    for (i = 0; i < DATA_FD_COUNT + 1; i++) {
	if (pipe(data_read[i]) < 0) {
	    error(_("pipe: %s\n"), strerror(errno));
	    /*NOTREACHED*/
	}
	if (pipe(data_write[i]) < 0) {
	    error(_("pipe: %s\n"), strerror(errno));
	    /*NOTREACHED*/
	}
    }
    if (pipe(data_write[STDERR_PIPE]) < 0) {
	error(_("pipe: %s\n"), strerror(errno));
	/*NOTREACHED*/
    }

    switch(pid = fork()) {
    case -1:
	error(_("could not fork service %s: %s\n"), cmd, strerror(errno));
	/*NOTREACHED*/
    default:
	/*
	 * The parent.  Close the far ends of our pipes and return.
	 */
	as = g_new0(struct active_service, 1);
	as->cmd = g_strdup(cmd);
	as->arguments = g_strdup(arguments);
	as->conn = conn;
	as->state = NULL;
	as->service = service;
	as->pid = pid;
	as->send_partial_reply = 0;
	as->seen_info_end = FALSE;
	/* fill in info_end_buf with non-null characters */
	memset(as->info_end_buf, '-', sizeof(as->info_end_buf));
	if(service == SERVICE_SENDSIZE) {
	    g_option_t *g_options;
	    char *option_str, *p;

	    option_str = g_strdup(as->arguments+8);
	    p = strchr(option_str,'\n');
	    if(p) *p = '\0';

	    g_options = parse_g_options(option_str, 1);
	    if(am_has_feature(g_options->features, fe_partial_estimate)) {
		as->send_partial_reply = 1;
	    }
	    free_g_options(g_options);
	    amfree(option_str);
	}

	/* write to the request pipe */
	aclose(data_read[0][0]);
	as->reqfd = data_read[0][1];

	/*
	 * read from the reply pipe
	 */
	as->repfd = data_write[0][0];
	aclose(data_write[0][1]);
	as->ev_repfd = NULL;
	as->repbuf = NULL;
	as->repbufsize = 0;
	as->bufsize = 0;
	as->repretry = 0;
	as->rep_pkt.body = NULL;

	/*
	 * read from the stderr pipe
	 */
	as->errfd = data_write[STDERR_PIPE][0];
	aclose(data_write[STDERR_PIPE][1]);
	as->ev_errfd = NULL;
	as->errbuf = NULL;

	/*
	 * read from the rest of the general-use pipes
	 * (streams are opened as the client requests them)
	 */
	for (i = 0; i < DATA_FD_COUNT; i++) {
	    aclose(data_read[i + 1][1]);
	    aclose(data_write[i + 1][0]);
	    as->data[i].fd_read = data_read[i + 1][0];
	    as->data[i].fd_write = data_write[i + 1][1];
	    as->data[i].ev_read = NULL;
	    as->data[i].stream = NULL;
	    as->data[i].as = as;
	}

	/* add it to the service queue */
	serviceq = g_slist_append(serviceq, (gpointer)as);

	/* and stop exiting, if we were planning to */
	clear_exit_timer();

	return (as);
    case 0:
	/*
	 * The child.  Put our pipes in their advertised locations
	 * and start up.
	 */

	/* set up the AMANDA_AUTHENTICATED_PEER env var so child services
	 * can use it to authenticate */
	peer_name = auth_conn_get_authenticated_peer_name(conn);
	amanda_remote_host_env[0] = NULL;
	amanda_remote_host_env[1] = NULL;
	if (*peer_name) {
	    amanda_remote_host_env[0] =
		g_strdup_printf("AMANDA_AUTHENTICATED_PEER=%s", peer_name);
	}

	/*
	 * The data stream is stdin in the new process
	 */
        if (dup2(data_read[0][0], 0) < 0) {
	    error(_("dup %d to %d failed: %s\n"), data_read[0][0], 0,
		strerror(errno));
	    /*NOTREACHED*/
	}
	aclose(data_read[0][0]);
	aclose(data_read[0][1]);

	/*
	 * The reply stream is stdout
	 */
        if (dup2(data_write[0][1], 1) < 0) {
	    error(_("dup %d to %d failed: %s\n"), data_write[0][1], 1,
		strerror(errno));
	}
        aclose(data_write[0][0]);
        aclose(data_write[0][1]);

	for (i = 0; i < DATA_FD_COUNT; i++) {
	    aclose(data_read[i + 1][0]);
	    aclose(data_write[i + 1][1]);
	}

	/*
	 *  Make sure they are not open in the range DATA_FD_OFFSET to
	 *      DATA_FD_OFFSET + DATA_FD_COUNT*2 - 1
	 */
	for (i = 0; i < DATA_FD_COUNT; i++) {
	    while(data_read[i + 1][1] >= DATA_FD_OFFSET &&
		  data_read[i + 1][1] <= DATA_FD_OFFSET + DATA_FD_COUNT*2 - 1) {
		newfd = dup(data_read[i + 1][1]);
		if(newfd == -1)
		    error(_("Can't dup out off DATA_FD range"));
		data_read[i + 1][1] = newfd;
	    }
	    while(data_write[i + 1][0] >= DATA_FD_OFFSET &&
		  data_write[i + 1][0] <= DATA_FD_OFFSET + DATA_FD_COUNT*2 - 1) {
		newfd = dup(data_write[i + 1][0]);
		if(newfd == -1)
		    error(_("Can't dup out off DATA_FD range"));
		data_write[i + 1][0] = newfd;
	    }
	}
	while(data_write[4][0] >= DATA_FD_OFFSET &&
	      data_write[4][0] <= DATA_FD_OFFSET + DATA_FD_COUNT*2 - 1) {
	    newfd = dup(data_write[4][0]);
	    if (newfd == -1)
		error(_("Can't dup out off DATA_FD range"));
	    data_write[4][0] = newfd;
	}
	while(data_write[4][1] >= DATA_FD_OFFSET &&
	      data_write[4][1] <= DATA_FD_OFFSET + DATA_FD_COUNT*2 - 1) {
	    newfd = dup(data_write[4][1]);
	    if (newfd == -1)
		error(_("Can't dup out off DATA_FD range"));
	    data_write[4][1] = newfd;
	}

	for (i = 0; i < DATA_FD_COUNT*2; i++)
	    close(DATA_FD_OFFSET + i);

	/*
	 * The rest start at the offset defined in amandad.h, and continue
	 * through the internal defined.
	 */
	for (i = 0; i < DATA_FD_COUNT; i++) {
	    if (dup2(data_read[i + 1][1], i*2 + DATA_FD_OFFSET) < 0) {
		error(_("dup %d to %d failed: %s\n"), data_read[i + 1][1],
		    i + DATA_FD_OFFSET, strerror(errno));
	    }
	    aclose(data_read[i + 1][1]);

	    if (dup2(data_write[i + 1][0], i*2 + 1 + DATA_FD_OFFSET) < 0) {
		error(_("dup %d to %d failed: %s\n"), data_write[i + 1][0],
		    i + DATA_FD_OFFSET, strerror(errno));
	    }
	    aclose(data_write[i + 1][0]);
	}

	/* close all unneeded fd */
	close(STDERR_FILENO);
	dup2(data_write[STDERR_PIPE][1], 2);
        aclose(data_write[STDERR_PIPE][0]);
        aclose(data_write[STDERR_PIPE][1]);
	safe_fd(DATA_FD_OFFSET, DATA_FD_COUNT*2);

	execle(cmd, cmd, "amandad", auth, (char *)NULL, safe_env_full(amanda_remote_host_env));
	error(_("could not exec service %s: %s\n"), cmd, strerror(errno));
	/*NOTREACHED*/
    }
    return NULL;
}

/*
 * Unallocate a service instance
 */
static void
service_delete(
    struct active_service *	as)
{
    int i;
    int   count;
    pid_t pid;
    struct datafd_handle *dh;

    amandad_debug(1, _("closing service: %s\n"),
		      (as->cmd)?as->cmd:_("??UNKONWN??"));

    assert(as != NULL);

    assert(as->cmd != NULL);
    amfree(as->cmd);

    assert(as->arguments != NULL);
    amfree(as->arguments);

    if (as->reqfd != -1)
	aclose(as->reqfd);
    if (as->repfd != -1)
	aclose(as->repfd);
    if (as->errfd != -1) {
	process_errfd(as);
	aclose(as->errfd);
    }

    if (as->ev_repfd != NULL)
	event_release(as->ev_repfd);
    if (as->ev_reptimeout != NULL)
	event_release(as->ev_reptimeout);
    if (as->ev_errfd != NULL)
	event_release(as->ev_errfd);

    for (i = 0; i < DATA_FD_COUNT; i++) {
	dh = &as->data[i];

	aclose(dh->fd_read);
	aclose(dh->fd_write);

	if (dh->stream != NULL) {
	    g_object_unref(dh->stream);
	    dh->stream = NULL;
	}

	if (dh->ev_read != NULL)
	    event_release(dh->ev_read);
    }

    /* signal the end of this protocol transaction */
    if (!auth_conn_sendpkt(amandad_conn, NULL))
	g_warning("error terminating protocol transaction (ignored): %s",
		auth_conn_error_message(amandad_conn));

    /* try to kill the process; if this fails, then it's already dead and
     * likely some of the other zombie cleanup ate its brains, so we don't
     * bother to waitpid for it */
    assert(as->pid > 0);
    pid = waitpid(as->pid, NULL, WNOHANG);
    if (pid != as->pid && kill(as->pid, SIGTERM) == 0) {
	pid = waitpid(as->pid, NULL, WNOHANG);
	count = 5;
	while (pid != as->pid && count > 0) {
	    count--;
	    sleep(1);
	    pid = waitpid(as->pid, NULL, WNOHANG);
	}
	if (pid != as->pid) {
	    g_debug("Process %d failed to exit", (int)as->pid);
	}
    }

    serviceq = g_slist_remove(serviceq, (gpointer)as);

    amfree(as->cmd);
    amfree(as->arguments);
    amfree(as->repbuf);
    as->bufsize = as->repbufsize = 0;
    amfree(as->rep_pkt.body);
    amfree(as);
}

/*
 * Like 'fullwrite', but does the work in a child process so pipelines
 * do not hang.
 */
static int
writebuf(
    struct active_service *	as,
    const void *		bufp,
    size_t			size)
{
    pid_t pid;
    size_t    writesize;

    switch (pid=fork()) {
    case -1:
	break;

    default:
	waitpid(pid, NULL, WNOHANG);
	return 0;			/* this is the parent */

    case 0: 				/* this is the child */
	close(as->repfd);
	writesize = full_write(as->reqfd, bufp, size);
	exit(writesize != size);
	/* NOTREACHED */
    }
    return -1;
}

static gboolean
do_sendpkt(
    AuthConn *conn,
    pkt_t *pkt)
{
    g_assert(conn != NULL);
    amandad_debug(1, "sending %s pkt:\n<<<<<\n%s>>>>>\n",
	pkt_type2str(pkt->type), pkt->body);
    return auth_conn_sendpkt(conn, pkt);
}

/*
 * Convert a state into a string
 */
static const char *
state2str(
    state_t	state)
{
    static const struct {
	state_t state;
	const char str[13];
    } states[] = {
#define	X(state)	{ state, stringize(state) }
	X(s_sendack),
	X(s_repwait),
	X(s_processrep),
	X(s_sendrep),
	X(s_ackwait),
	X(s_pipewait),
#undef X
    };
    int i;

    for (i = 0; i < (int)(sizeof(states) / sizeof(states[0])); i++)
	if (state == states[i].state)
	    return (states[i].str);
    return (_("INVALID STATE"));
}

/*
 * Convert an action into a string
 */
static const char *
action2str(
    action_t	action)
{
    static const struct {
	action_t action;
	const char str[12];
    } actions[] = {
#define	X(action)	{ action, stringize(action) }
	X(A_START),
	X(A_RECVPKT),
	X(A_RECVREP),
	X(A_PENDING),
	X(A_FINISH),
	X(A_CONTINUE),
	X(A_SENDNAK),
	X(A_TIMEOUT),
#undef X
    };
    int i;

    for (i = 0; i < (int)(sizeof(actions) / sizeof(actions[0])); i++)
	if (action == actions[i].action)
	    return (actions[i].str);
    return (_("UNKNOWN ACTION"));
}

static char *
amandad_conf_getter(
    char *      string,
    void *      arg)
{
    (void)arg;      /* Quiet unused parameter warning */

    if (!string || !*string)
	return(NULL);

    if (strcmp(string, "kencrypt")==0) {
	if (amandad_kencrypt == KENCRYPT_YES)
	    return ("yes");
	else
	    return (NULL);
    }
    return(NULL);
}

