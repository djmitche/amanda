/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1993 University of Maryland
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
 * $Id: protocol.c,v 1.20 1998/01/26 21:16:04 jrj Exp $
 *
 * implements amanda protocol
 */
#include "amanda.h"
#include "protocol.h"
#include "version.h"
#ifdef KRB4_SECURITY
#  include "krb4-security.h"
#endif

#define ACK_WAIT	10	 /* time (secs) to wait for ACK - keep short */
#define ACK_TRIES	 3     /* # times we'll retry after ACK_WAIT timeout */
#define REQ_TRIES	 2   /* # times client can start over (reboot/crash) */

#define DROP_DEAD_TIME	(60*60)	   /* If no reply in an hour, just forget it */

#define MAX_HANDLES	4096
#define OFS_DIGITS	   3	/* log2(MAX_HANDLES)/4 */

proto_t *pending_head = NULL;
proto_t *pending_tail = NULL;
int pending_qlength = 0;

int proto_socket = -1;
int proto_global_seq = 0;
#define relseq(s) (s-proto_global_seq)

proto_t **proto_handle_table;
proto_t **proto_next_handle;
int proto_handles;

time_t proto_init_time;
#define CURTIME	(time(0)-proto_init_time)

/* local functions */
static char *prnpstate P((pstate_t s));
static char *prnaction P((action_t s));
#ifdef PROTO_DEBUG
static char *prnpktype P((pktype_t s));
#endif
static void pending_enqueue P((proto_t *newp));
static proto_t *pending_dequeue P((void));
static void pending_remove P((proto_t *p));
static void alloc_handle P((proto_t *p));
static void free_handle P((proto_t *p));
static void hex P((char *str, int digits, unsigned int v));
static int unhex P((char *str, int digits));
static proto_t *handle2ptr P((char *str));
static char *ptr2handle P((proto_t *p));
static void eat_string P((dgram_t *msg, char *str));
static int parse_integer P((dgram_t *msg));
static char *parse_string P((dgram_t *msg));
static char *parse_line P((dgram_t *msg));
void parse_pkt_header P((pkt_t *pkt));
static void setup_dgram P((proto_t *p, dgram_t *msg, 
			   char *security, char *typestr));
static void send_req P((proto_t *p));
static void send_ack P((proto_t *p));
static void send_ack_repl P((pkt_t *pkt));
static void state_machine P((proto_t *p, action_t action, pkt_t *pkt));
static void add_bsd_security P((proto_t *p));
static int select_til P((time_t waketime));
static int packet_arrived P((void));
static void handle_incoming_packet P((void)); 


/* -------------- */


static char *prnpstate(s)
pstate_t s;
{
    static char str[80];

    switch(s) {
    case S_BOGUS: return "S_BOGUS";
    case S_STARTUP:  return "S_STARTUP";
    case S_SENDREQ:  return "S_SENDREQ";
    case S_ACKWAIT:  return "S_ACKWAIT";
    case S_REPWAIT:  return "S_REPWAIT";
    case S_SUCCEEDED:  return "S_SUCCEEDED";
    case S_FAILED: return "S_FAILED";
    default:
	ap_snprintf(str, sizeof(str), "<bad state %d>", s);
	return str;
    }
}

static char *prnaction(s)
action_t s;
{
    static char str[80];

    switch(s) {
    case A_BOGUS:  return "A_BOGUS";
    case A_START:  return "A_START";
    case A_TIMEOUT:  return "A_TIMEOUT";
    case A_RCVDATA: return "A_RCVDATA";
    default:
	ap_snprintf(str, sizeof(str), "<bad action %d>", s);
	return str;
    }
}

#ifdef PROTO_DEBUG

static char *prnpktype(s)
pktype_t s;
{
    static char str[80];

    switch(s) {
    case P_BOGUS: return "P_BOGUS";
    case P_REQ: return "P_REQ";
    case P_REP: return "P_REP";
    case P_ACK: return "P_ACK";
    case P_NAK: return "P_NAK";
    default:
	ap_snprintf(str, sizeof(str), "<bad pktype %d>", s);
	return str;
    }
}

#endif


void proto_init(socket, startseq, handles)
int socket, startseq, handles;
{
    int i;

#ifdef PROTO_DEBUG
    fprintf(stderr, "proto_init(socket %d, startseq %d, handles %d)\n",
	    socket, startseq, handles);
#endif
    if(socket < 0 || socket >= FD_SETSIZE) {
	error("proto_init: socket %d out of range (0 .. %d)\n",
	      socket, FD_SETSIZE-1);
    }
    proto_socket = socket;
    proto_global_seq = startseq;
    proto_handles = handles;

    proto_handle_table = alloc(proto_handles * sizeof(proto_t *));
    proto_next_handle = proto_handle_table;
    for(i = 0; i < proto_handles; i++)
	proto_handle_table[i] = NULL;
    proto_init_time = time(0);
}


static void pending_enqueue(newp)
proto_t *newp;
{
    proto_t *curp;

    /* common case shortcut: check if adding to end of list */

    if(pending_tail && pending_tail->timeout <= newp->timeout)
	curp = NULL;
    else {
	/* scan list for insert-sort */
	curp = pending_head;
	while(curp && curp->timeout <= newp->timeout)
	    curp = curp->next;
    }

    newp->next = curp;
    if(curp) {
	newp->prev = curp->prev;
	curp->prev = newp;
    }
    else {
	newp->prev = pending_tail;
	pending_tail = newp;
    }

    if(newp->prev) newp->prev->next = newp;
    else pending_head = newp;

    pending_qlength++;
}

static proto_t *pending_dequeue()
{
    proto_t *p;

    p = pending_head;
    if(p) {
	pending_head = p->next;
	p->next = NULL;
	if(pending_head) 
	    pending_head->prev = NULL;
	else
	    pending_tail = NULL;
	pending_qlength--;
    }

    return p;
}

static void pending_remove(p)
proto_t *p;
{
    if(p->next) p->next->prev = p->prev;
    else pending_tail = p->prev;

    if(p->prev) p->prev->next = p->next;
    else pending_head = p->next;

    p->prev = p->next = NULL;
    pending_qlength--;
}

/* -------- */

#define PTR_CHARS    sizeof(proto_t *)		    /* chars in a pointer */
#define CHAR_DIGITS  2				    /* hex digits in a char */
#define HANDLE_CHARS (OFS_DIGITS+1+PTR_CHARS*CHAR_DIGITS) /* "xxx-yyyyyyyy" */
union handle_u {
    unsigned char c[PTR_CHARS];
    proto_t *p;
} hu;

static void alloc_handle(p)
proto_t *p;
{
    int i;
    proto_t **hp;

    hp = proto_next_handle;
    for(i = 0; i < proto_handles; i++) {
	if(*hp == NULL) break;
	hp++;
	if(hp >= proto_handle_table + proto_handles)
	    hp = proto_handle_table;
    }
    if(i == proto_handles)
	error("protocol out of handles");
    p->handleofs = hp-proto_handle_table;
    *hp = p;
}

static void free_handle(p)
proto_t *p;
{
    if(proto_handle_table[p->handleofs] == p)
	proto_handle_table[p->handleofs] = NULL;
    p->handleofs = -1;
}

static void hex(str, digits, v)
char *str;
int digits;
unsigned int v;
{
    str = str + digits - 1;

    while(digits--) {
	*str-- = "0123456789ABCDEF"[v % 16];
	v /= 16;
    }
}

static int unhex(str, digits)
char *str;
int digits;
{
    int d, v = 0;

    while(*str && digits--) {
	d = *str >= 'A'? *str - 'A' + 10 : *str - '0';
	v = v * 16 + d;
	str++;
    }
    return v;
}


static proto_t *handle2ptr(str)
char *str;
{
    int ofs, i;

    if(strlen(str) != HANDLE_CHARS)
	return NULL;

    ofs = unhex(str, OFS_DIGITS);
    str += OFS_DIGITS;
    if(ofs < 0 || ofs >= proto_handles)
	return NULL;

    if(*str++ != '-')
	return NULL;

    for(i=0; i < PTR_CHARS; i++) {
	hu.c[i] = unhex(str, CHAR_DIGITS);
	str += CHAR_DIGITS;
    }

    if(proto_handle_table[ofs] != hu.p)
	return NULL;

    return hu.p;
}


static char *ptr2handle(p)
proto_t *p;
{
    int i;
    char *s;
    static char hstr[HANDLE_CHARS+1];

    assert(p->handleofs != -1 && proto_handle_table[p->handleofs] == p);

    hu.p = p;

    hex(hstr, OFS_DIGITS, p->handleofs);
    s = &hstr[OFS_DIGITS];
    *s++ = '-';

    for(i=0;i<PTR_CHARS;i++) {
	hex(s, CHAR_DIGITS, hu.c[i]);
	s += CHAR_DIGITS;
    }
    *s = '\0';
    return hstr;
}

/* -------- */

jmp_buf parse_failed;
char *parse_errmsg = NULL;

static void eat_string(msg, str)
dgram_t *msg;
char *str;
{
    char *saved_str, *saved_msg;

    /* eat leading whitespace */
    while(isspace(*msg->cur)) msg->cur++;

    saved_msg = msg->cur;
    saved_str = str;

    /* eat any characters that match str */
    while(*str && *msg->cur++ == *str++);

    /* if we didn't eat all of str, we've failed */
    if(*str) {
	int len = strlen(saved_str);
	char *tmp = NULL;
	
	tmp = alloc(len+1);
	strncpy(tmp, saved_msg, len);
	tmp[len] = '\0';
	parse_errmsg = newvstralloc(parse_errmsg,
				    "expected \"", saved_str, "\",",
				    " got \"", tmp, "\"",
				    NULL);
	afree(tmp);
	longjmp(parse_failed,1);
    }
}

static int parse_integer(msg)
dgram_t *msg;
{
    int i = 0;

    /* eat leading whitespace */
    while(isspace(*msg->cur)) msg->cur++;

    /* must have at least one digit */
    if(*msg->cur < '0' || *msg->cur > '9') {
	char non_digit[2];

	non_digit[0] = *msg->cur;
	non_digit[1] = '\0';
	parse_errmsg = newvstralloc(parse_errmsg,
				    "expected digit, got \"", non_digit, "\"",
				    NULL);
	longjmp(parse_failed,1);
    }

    while(*msg->cur >= '0' && *msg->cur <= '9') {
	i = i * 10 + (*msg->cur - '0');
	msg->cur++;
    }
    return i;
}

static char *parse_string(msg)
dgram_t *msg;
{
    char *str;

    /* eat leading whitespace */
    while(isspace(*msg->cur)) msg->cur++;

    /* mark start of string */
    str = msg->cur;

    /* stop at whitespace (including newlines) or end-of-packet */
    while(*msg->cur && !isspace(*msg->cur)) msg->cur++;

    /* empty fields not allowed */
    if(msg->cur == str) {
	parse_errmsg = newstralloc(parse_errmsg,
				   "expected string, got empty field");
	longjmp(parse_failed,1);
    }

    /* mark end of string in the packet, but don't fall off the end of it */
    if(*msg->cur) *msg->cur++ = '\0';

    return str;
}

static char *parse_line(msg)
dgram_t *msg;
{
    char *str;

    /* eat leading whitespace */
    while(isspace(*msg->cur)) msg->cur++;

    /* mark start of string */
    str = msg->cur;

    /* stop at end of line or end-of-packet */
    while(*msg->cur && *msg->cur != '\n') msg->cur++;

    /* empty fields not allowed */
    if(msg->cur == str) {
	parse_errmsg = newstralloc(parse_errmsg,
				   "expected string, got empty field");
	longjmp(parse_failed,1);
    }

    /* mark end of string in the packet, but don't fall off the end of it */
    if(*msg->cur) *msg->cur++ = '\0';

    return str;
}

void parse_pkt_header(pkt)
pkt_t *pkt;
{
    dgram_t *msg;
    char *typestr;

    if(setjmp(parse_failed)) {
/*	dbprintf(("%s: leftover:\n----\n%s----\n\n", errmsg, msg->cur)); */
	pkt->type = P_BOGUS;
	return;
    }

    msg = &pkt->dgram;

#ifdef PROTO_DEBUG
    fprintf(stderr, "parsing packet:\n-------\n%s-------\n\n", msg->cur);
#endif

    eat_string(msg, "Amanda");	    pkt->version_major = parse_integer(msg);
    eat_string(msg, ".");	    pkt->version_minor = parse_integer(msg);
    typestr = parse_string(msg);

    if(strcmp(typestr, "REQ") == 0) pkt->type = P_REQ;
    else if(strcmp(typestr, "REP") == 0) pkt->type = P_REP;
    else if(strcmp(typestr, "ACK") == 0) pkt->type = P_ACK;
    else if(strcmp(typestr, "NAK") == 0) pkt->type = P_NAK;
    else pkt->type = P_BOGUS;

    eat_string(msg, "HANDLE");	    pkt->handle = parse_string(msg);
    eat_string(msg, "SEQ");	    pkt->sequence = parse_integer(msg);

    eat_string(msg, "");
#define sc "SECURITY"
    if(strncmp(msg->cur, sc, sizeof(sc)-1) == 0) {
	/* got security tag */
	eat_string(msg, sc);
#undef sc
	pkt->security = parse_line(msg);
    }
    else pkt->security = NULL;

    if(pkt->type == P_REQ) {

#ifdef KRB4_SECURITY
        eat_string(msg, "");
        pkt->cksum = kerberos_cksum(msg->cur);
#ifdef PROTO_DEBUG
        fprintf(stderr, "parse_pkt/cksum %ld over \'%s\'\n\n",
		pkt->cksum, msg->cur); 
#endif
        fflush(stdout);
#endif
	eat_string(msg, "SERVICE");     pkt->service = parse_string(msg);
    }

    eat_string(msg, "");
    pkt->body = msg->cur;
}

static void setup_dgram(p, msg, security, typestr)
proto_t *p;
dgram_t *msg;
char *security, *typestr;
{
    char *linebuf = NULL;
    char major_str[NUM_STR_SIZE];
    char minor_str[NUM_STR_SIZE];
    char seq_str[NUM_STR_SIZE];

    ap_snprintf(major_str, sizeof(major_str), "%d", VERSION_MAJOR);
    ap_snprintf(minor_str, sizeof(minor_str), "%d", VERSION_MINOR);
    ap_snprintf(seq_str, sizeof(seq_str), "%d", p->curseq);

    dgram_zero(msg);
    dgram_socket(msg,proto_socket);
    linebuf = vstralloc("Amanda ", major_str, ".", minor_str,
			" ", typestr,
			" HANDLE ", ptr2handle(p),
			" SEQ ", seq_str,
			"\n",
			security ? security : "",
			security ? "\n" : "",
			NULL);
    dgram_cat(msg, linebuf);
    afree(linebuf);
}

static void send_req(p)
proto_t *p;
{
    dgram_t outmsg;

    setup_dgram(p, &outmsg, p->security, "REQ");
    dgram_cat(&outmsg, p->req);

#ifdef PROTO_DEBUG
    fprintf(stderr, "time %d: send_req: len %d: packet:\n----\n%s----\n\n", 
	    CURTIME, outmsg.len, outmsg.data);
#endif

    if(dgram_send_addr(p->peer, &outmsg))
	error("send req failed: %s", strerror(errno));
}

static void send_ack(p)
proto_t *p;
{
    dgram_t outmsg;

    setup_dgram(p, &outmsg, NULL, "ACK");

#ifdef PROTO_DEBUG
    fprintf(stderr, "time %d: send_ack: len %d: packet:\n----\n%s----\n\n", 
	    CURTIME, outmsg.len, outmsg.data);
#endif

    if(dgram_send_addr(p->peer, &outmsg))
	error("send ack failed: %s", strerror(errno));
}

static void send_ack_repl(pkt)
pkt_t *pkt;
{
    dgram_t outmsg;
    char *linebuf = NULL;
    char major_str[NUM_STR_SIZE];
    char minor_str[NUM_STR_SIZE];
    char seq_str[NUM_STR_SIZE];

    ap_snprintf(major_str, sizeof(major_str), "%d", VERSION_MAJOR);
    ap_snprintf(minor_str, sizeof(minor_str), "%d", VERSION_MINOR);
    ap_snprintf(seq_str, sizeof(seq_str), "%d", pkt->sequence);

    dgram_zero(&outmsg);
    dgram_socket(&outmsg,proto_socket);

    linebuf = vstralloc("Amanda ", major_str, ".", minor_str,
			" ACK HANDLE ", pkt->handle,
			" SEQ ", seq_str,
			"\n", NULL);

    dgram_cat(&outmsg, linebuf);
    afree(linebuf);

#ifdef PROTO_DEBUG
    fprintf(stderr, "time %d: send_ack_repl: len %d: packet:\n----\n%s----\n\n", 
	    CURTIME, outmsg.len, outmsg.data);
#endif

    if(dgram_send_addr(pkt->peer, &outmsg))
	error("send ack failed: %s", strerror(errno));
}


static void state_machine(p, action, pkt)
proto_t *p;
action_t action;
pkt_t *pkt;
{

#ifdef PROTO_DEBUG
    fprintf(stderr, 
	    "time %d: state_machine: p %X state %s action %s%s%s\n",
	    CURTIME,(int)p, prnpstate(p->state), prnaction(action),
	    pkt == NULL? "" : " pktype ",
	    pkt == NULL? "" : prnpktype(pkt->type));
#endif

    while(1) {
	switch(p->state) {
	case S_STARTUP: 
	    if(action != A_START) goto badaction;
	    p->origseq = p->curseq = proto_global_seq++;
	    p->reqtries = REQ_TRIES;
	    p->state = S_SENDREQ;
	    p->acktries = ACK_TRIES;
	    alloc_handle(p);
	    break;

	case S_SENDREQ:
	    send_req(p);
	    p->curtime = CURTIME;
	    if(p->curseq == p->origseq) p->origtime = p->curtime;
	    p->timeout = time(0) + ACK_WAIT;
	    p->state = S_ACKWAIT;
	    pending_enqueue(p);
	    return;

	case S_ACKWAIT:
	    if(action == A_TIMEOUT) {
		if(--p->acktries == 0) {
		    p->state = S_FAILED;
		    free_handle(p);
		    p->continuation(p, NULL);
		    afree(p->req);
		    afree(p->security);
		    afree(p);
		    return;
		}
		else {
		    p->state = S_SENDREQ;
		    break;
		}
	    }
	    else if(action != A_RCVDATA)
		goto badaction;

	    /* got the packet with the right handle, now check it */

#ifdef PROTO_DEBUG
	    fprintf(stderr, 
         "RESPTIME p %X pkt %s (t %d s %d) orig (t %d s %d) cur (t %d s %d)\n",
		    (int)p, prnpktype(pkt->type), 
		    CURTIME, relseq(pkt->sequence),
		    p->origtime, relseq(p->origseq), 
		    p->curtime, relseq(p->curseq));
#endif

	    if(pkt->type == P_ACK) {
		if(pkt->sequence != p->origseq)
		    p->reqtries--;
		p->state = S_REPWAIT;
		p->timeout = time(0) + p->repwait;
		pending_enqueue(p);
		return;
	    }
	    else if(pkt->type == P_NAK) {
		p->state = S_FAILED;
		free_handle(p);
		p->continuation(p, pkt);
		afree(p->req);
		afree(p->security);
		afree(p);
		return;
	    }
	    else if(pkt->type == P_REP) {
		/* no ack, just rep */
		p->state = S_REPWAIT;
		break;
	    }
	    /* else unexpected packet, put back on queue */
	    pending_enqueue(p);
	    return;

	case S_REPWAIT:
	    if(action == A_TIMEOUT) {
		if(p->reqtries == 0 || 
		   (CURTIME - p->origtime > DROP_DEAD_TIME)) {
		    p->state = S_FAILED;
		    free_handle(p);
		    p->continuation(p, NULL);
		    afree(p->req);
		    afree(p->security);
		    afree(p);
		    return;
		}
		else {
		    p->reqtries--;
		    p->state = S_SENDREQ;
		    p->acktries = ACK_TRIES;
		    break;
		}
	    }
	    else if(action != A_RCVDATA)
		goto badaction;
	    /* got the packet with the right handle, now check it */
	    if(pkt->type != P_REP) {
		pending_enqueue(p);
		return;
	    }
	    send_ack(p);
	    p->state = S_SUCCEEDED;
	    free_handle(p);
	    p->continuation(p, pkt);
	    afree(p->req);
	    afree(p->security);
	    afree(p);
	    return;

	default:
	badaction:
	    error("protocol error: no handler for state %s action %s\n",
		  prnpstate(p->state), prnaction(action));
	}
    }
}

static void add_bsd_security(p)
proto_t *p;
{
    p->security = get_bsd_security();
}

int make_request(hostname, port, req, datap, repwait, continuation)
char *hostname;
int port;
char *req;
void *datap;
time_t repwait;
void (*continuation) P((proto_t *p, pkt_t *pkt));
{
    proto_t *p;
    struct hostent *hp;


    p = alloc(sizeof(proto_t));
    p->state = S_STARTUP;
    p->continuation = continuation;
    p->req = req;
    p->repwait = repwait;
    p->datap = datap;

#ifdef PROTO_DEBUG
    fprintf(stderr, "time %d: make_request: host %s -> p %X\n", 
	    CURTIME, hostname, (int)p);
#endif

    if((hp = gethostbyname(hostname)) == 0) return -1;
    memcpy(&p->peer.sin_addr, hp->h_addr, hp->h_length);
    p->peer.sin_family = AF_INET;
    p->peer.sin_port = htons(port);

    add_bsd_security(p);

    state_machine(p, A_START, NULL);
    return 0;
}

#ifdef KRB4_SECURITY

static int add_krb_security P((proto_t *p, char *host_inst, char *realm));

static int add_krb_security(p, host_inst, realm)
proto_t *p;
char *host_inst, *realm;
{
    p->security = get_krb_security(p->req, host_inst, realm, &p->auth_cksum);

#ifdef PROTO_DEBUG
    fprintf(stderr, "add_krb_security() cksum: %lu: \'%s\'\n",
	p->auth_cksum, p->req);
#endif

    return p->security == NULL;
}

int make_krb_request(hostname, port, req, datap, repwait, continuation)
char *hostname;
int port;
char *req;
void *datap;
time_t repwait;
void (*continuation) P((proto_t *p, pkt_t *pkt));
{
    proto_t *p;
    struct hostent *hp;
    char inst[256], realm[256];
    int rc;

    p = alloc(sizeof(proto_t));
    p->state = S_STARTUP;
    p->continuation = continuation;
    p->req = req;
    p->repwait = repwait;
    p->datap = datap;

#ifdef PROTO_DEBUG
    fprintf(stderr, "time %d: make_krb_request: host %s -> p %X\n", 
	    CURTIME, hostname, req, (int)p);
#endif

    if((hp = host2krbname(hostname, inst, realm)) == 0)
	return -1;
    memcpy(&p->peer.sin_addr, hp->h_addr, hp->h_length);
    p->peer.sin_family = AF_INET;
    p->peer.sin_port = htons(port);

    if((rc = add_krb_security(p, inst, realm)))
	return rc;

    state_machine(p, A_START, NULL);
    return 0;
}

#endif

static int select_til(waketime) 
time_t waketime;
{
    fd_set ready;
    struct timeval to;
    time_t waittime;
    int rc;

    waittime = waketime - time(0);
    if(waittime < 0) waittime = 0;	/* just poll */

    FD_ZERO(&ready);
    FD_SET(proto_socket, &ready);
    to.tv_sec = waittime;
    to.tv_usec = 0;

    rc = select(proto_socket+1, (SELECT_ARG_TYPE *)&ready, NULL, NULL, &to);
    if(rc == -1) error("protocol socket select: %s", strerror(errno));
    return rc;
}

static int packet_arrived() 
{
    return select_til(0);
}

static void handle_incoming_packet() 
{
    pkt_t inpkt;
    proto_t *p;

    dgram_zero(&inpkt.dgram);
    dgram_socket(&inpkt.dgram, proto_socket);
    if(dgram_recv(&inpkt.dgram, 0, &inpkt.peer) == -1)
	error("protocol packet receive: %s", strerror(errno));

#ifdef PROTO_DEBUG
    fprintf(stderr, "time %d: got packet:\n----\n%s----\n\n",
	    CURTIME, inpkt.dgram.data);
#endif

    parse_pkt_header(&inpkt);
    if(inpkt.type == P_BOGUS)
	return;
    if((p = handle2ptr(inpkt.handle)) == NULL) {
	/* ack succeeded reps */
	if(inpkt.type == P_REP)
	    send_ack_repl(&inpkt);
	return;
    }

#ifdef PROTO_DEBUG
    fprintf(stderr, "time %d: handle %s p %X got packet type %s\n", CURTIME,
	    inpkt.handle, (int)p, prnpktype(inpkt.type));
#endif

    pending_remove(p);
    state_machine(p, A_RCVDATA, &inpkt);
}



void check_protocol()
{
    time_t curtime;
    proto_t *p;

    while(packet_arrived())
	handle_incoming_packet();

    curtime = time(0);
    while(pending_head && curtime >= pending_head->timeout) {
	p = pending_dequeue();
	state_machine(p, A_TIMEOUT, NULL);
    }
}


void run_protocol()
{
    time_t wakeup_time;
    proto_t *p;

    while(pending_head) {
	wakeup_time = pending_head->timeout;

#ifdef PROTO_DEBUG
	fprintf(stderr, 
		"time %d: run_protocol: waiting %d secs for %d pending reqs\n",
		CURTIME, wakeup_time - time(0), pending_qlength);
#endif

	if(select_til(wakeup_time))
	    handle_incoming_packet();
	else {
	    p = pending_dequeue();
	    state_machine(p, A_TIMEOUT, NULL);
	}
    }
    afree(proto_handle_table);
}
