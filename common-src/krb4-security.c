/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1993 University of Maryland
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
 * krb4-security.c - helper functions for kerberos v4 security.
 */
#include "amanda.h"
#include "krb4-security.h"
#include "protocol.h"

#define HOSTNAME_INSTANCE inst

static char *ticketfilename = NULL;

int krb4_auth = 0;
int kencrypt = 0;
des_cblock session_key;
u_int32_t auth_cksum;		/* was 'long' on 32-bit platforms */

void krb4_killtickets(void)
{
    if(ticketfilename != NULL)
	unlink(ticketfilename);
    amfree(ticketfilename);
}

void kerberos_service_init()
{
    int rc;
    char hostname[MAX_HOSTNAME_LENGTH+1], inst[256], realm[256];
#if defined(HAVE_PUTENV)
    char *tkt_env = NULL;
#endif
    char uid_str[NUM_STR_SIZE];
    char pid_str[NUM_STR_SIZE];

    gethostname(hostname, sizeof(hostname)-1);
    hostname[sizeof(hostname)-1] = '\0';

    if(ticketfilename == NULL)
    	atexit(krb4_killtickets);

    host2krbname(hostname, inst, realm);

    /*
     * [XXX] It could be argued that if KRBTKFILE is set outside of amanda,
     * that it's value should be used instead of us setting one up.
     * This file also needs to be removed so that no extra tickets are
     * hanging around.
     */
    ap_snprintf(uid_str, sizeof(uid_str), "%ld", (long)getuid());
    ap_snprintf(pid_str, sizeof(pid_str), "%ld", (long)getpid());
    ticketfilename = newvstralloc(ticketfilename,
				  "/tmp/tkt",
				  uid_str, "-", pid_str,
				  ".amanda",
				  NULL);
    krb_set_tkt_string(ticketfilename);
#if defined(HAVE_PUTENV)
    tkt_env = stralloc2("KRBTKFILE=", ticketfilename);
    putenv(tkt_env);
    amfree(tkt_env);
#else
    setenv("KRBTKFILE",ticketfilename,1);
#endif

    rc = krb_get_svc_in_tkt(SERVER_HOST_PRINCIPLE, SERVER_HOST_INSTANCE,
			    realm, "krbtgt", realm, TICKET_LIFETIME,
			    SERVER_HOST_KEY_FILE);
    if(rc) error("could not get krbtgt for %s.%s@%s from %s: %s",
		 SERVER_HOST_PRINCIPLE, SERVER_HOST_INSTANCE, realm,
		 SERVER_HOST_KEY_FILE, krb_err_txt[rc]);

    krb_set_lifetime(TICKET_LIFETIME);
}


u_int32_t kerberos_cksum(str)
char *str;
{
    des_cblock seed;

    memset(seed, 0, sizeof(seed));
    return quad_cksum(str, NULL, strlen(str), 1, seed);
}

struct hostent *host2krbname(alias, inst, realm)
char *alias, *inst, *realm;
{
    struct hostent *hp;
    char *s, *d, *krb_realmofhost();
    char saved_hostname[1024];

    if((hp = gethostbyname(alias)) == 0) return 0;

    /* get inst name: like krb_get_phost, but avoid multiple gethostbyname */

    for(s = hp->h_name, d = inst; *s && *s != '.'; s++, d++)
	*d = isupper(*s)? tolower(*s) : *s;
    *d = '\0';

    /*
     * It isn't safe to pass hp->h_name to krb_realmofhost, since
     * it might use gethostbyname internally.
     */
    bzero(saved_hostname, sizeof(saved_hostname));
    strncpy(saved_hostname, hp->h_name, sizeof(saved_hostname)-1);

    /* get realm name: krb_realmofhost always returns *something* */
    strcpy(realm, krb_realmofhost(saved_hostname));

    return hp;
}

void encrypt_data(data, length, key)
void *data;
int length;
des_cblock key;
{
    des_key_schedule sched;

    des_key_sched(key, sched);
    des_pcbc_encrypt(data, data, length, sched, key, DES_ENCRYPT);
}


void decrypt_data(data, length, key)
void *data;
int length;
des_cblock key;
{
    des_key_schedule sched;

    des_key_sched(key, sched);
    des_pcbc_encrypt(data, data, length, sched, key, DES_DECRYPT);
}


/*
 * struct timeval is a host structure, and may not be used in
 * protocols, because members are defined as 'long', rather than
 * u_int32_t.
 */
typedef struct net_tv {
  int32_t tv_sec;
  int32_t tv_usec;
} net_tv;

int kerberos_handshake(fd, key)
int fd;
des_cblock key;
{
    int rc;
    struct timeval local;
    net_tv localenc, remote, rcvlocal;
    struct timezone tz;
    char *strerror();
    char *d;
    int l, n, s;

    /*
     * There are two mutual authentication transactions going at once:
     * one in which we prove the to peer that we are the legitimate
     * party, and one in which the peer proves to us that that they
     * are legitimate.
     *
     * In addition to protecting against spoofing, this exchange
     * ensures that the two peers have the same keys, protecting
     * against having data encrypted with one key and decrypted with
     * another on the backup tape.
     */

    gettimeofday(&local, &tz);

    /* 
     * Convert time to  network order and sizes, encrypt,  and send to
     * peer as the first step in  the peer proving to us that they are
     * legitimate.
     */
    localenc.tv_sec = (int32_t) local.tv_sec;
    localenc.tv_usec = (int32_t) local.tv_usec;
    localenc.tv_sec = htonl(localenc.tv_sec);
    localenc.tv_usec = htonl(localenc.tv_usec);
    assert(sizeof(localenc) == 8);
    encrypt_data(&localenc, sizeof localenc, key);

    d = (char *)&localenc;
    for(l = 0, n = sizeof(localenc); l < n; l += s) {
	if((s = write(fd, d+l, n-l)) < 0) {
	    error("kerberos_handshake write error: [%s]", strerror(errno));
	}
    }

    /*
     * Read block from peer and decrypt.  This is the first step in us
     * proving to the peer that we are legitimate.
     */
    d = (char *)&remote;
    assert(sizeof(remote) == 8);
    for(l = 0, n = sizeof(remote); l < n; l += s) {
	if((s = read(fd, d+l, n-l)) < 0) {
	    error("kerberos_handshake read error: [%s]", strerror(errno));
	}
    }
    if(l != n) {
	error("kerberos_handshake read error: [short read]");
    }

    decrypt_data(&remote, sizeof remote, key);

    /* XXX do timestamp checking here */

    /*
     * Add 1.000001 seconds to the peer's timestamp, leaving it in
     * network order, re-encrypt and send back.
     */
    remote.tv_sec = ntohl(remote.tv_sec);
    remote.tv_usec = ntohl(remote.tv_usec);
    remote.tv_sec += 1;
    remote.tv_usec += 1;
    remote.tv_sec = htonl(remote.tv_sec);
    remote.tv_usec = htonl(remote.tv_usec);

    encrypt_data(&remote, sizeof remote, key);

    d = (char *)&remote;
    for(l = 0, n = sizeof(remote); l < n; l += s) {
	if((s = write(fd, d+l, n-l)) < 0) {
	    error("kerberos_handshake write2 error: [%s]", strerror(errno));
	}
    }

    /*
     * Read the peers reply, decrypt, convert to host order, and
     * verify that the peer was able to add 1.000001 seconds, thus
     * showing that it knows the DES key.
     */
    d = (char *)&rcvlocal;
    for(l = 0, n = sizeof(rcvlocal); l < n; l += s) {
	if((s = read(fd, d+l, n-l)) < 0) {
	    error("kerberos_handshake read2 error: [%s]", strerror(errno));
	}
    }
    if(l != n) {
	error("kerberos_handshake read2 error: [short read]");
    }

    decrypt_data(&rcvlocal, sizeof rcvlocal, key);

    rcvlocal.tv_sec = ntohl(rcvlocal.tv_sec);
    rcvlocal.tv_usec = ntohl(rcvlocal.tv_usec);

    dbprintf(("handshake: %d %d %d %d\n",
	      local.tv_sec, local.tv_usec,
	      rcvlocal.tv_sec, rcvlocal.tv_usec));

    return (rcvlocal.tv_sec  == (int32_t) (local.tv_sec + 1)) &&
	   (rcvlocal.tv_usec == (int32_t) (local.tv_usec + 1));
}

des_cblock *host2key(hostname)
char *hostname;
{
    static des_cblock key;
    char inst[256], realm[256];
    CREDENTIALS cred;

    if(host2krbname(hostname, inst, realm))
	krb_get_cred(CLIENT_HOST_PRINCIPLE, CLIENT_HOST_INSTANCE, realm,&cred);

    memcpy(key, cred.session, sizeof key);
    return &key;
}

int check_mutual_authenticator(key, pkt, p)
des_cblock *key;
pkt_t *pkt;
proto_t *p;
{
    char *astr = NULL;
    union {
	char pad[8];
	u_int32_t i;
    } mutual;
    int len;
    char *s, *fp;
    int ch;

    if(pkt->security == NULL) {
	fprintf(stderr," pkt->security is NULL\n");
	return 0;
    }

    s = pkt->security;
    ch = *s++;

    skip_whitespace(s, ch);
    if(ch == '\0') {
        fprintf(stderr,"pkt->security is actually %s\n", pkt->security);
	return 0;
    }
    fp = s-1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    if(strcmp(fp, "MUTUAL-AUTH") != 0) {
	s[-1] = ch;
        fprintf(stderr,"pkt->security is actually %s\n", pkt->security);
	return 0;
    }
    s[-1] = ch;

    skip_whitespace(s, ch);
    if(ch == '\0') {
        fprintf(stderr,"pkt->security is actually %s\n", pkt->security);
	return 0;
    }
    astr = s-1;
    while(ch && ch != '\n') ch = *s++;
    s[-1] = '\0';

    /* XXX - goddamn it this is a worm-hole */
    astr2bin(astr, (unsigned char *)mutual.pad, &len);

    s[-1] = ch;

    decrypt_data(&mutual, len, *key);
    mutual.i = ntohl(mutual.i);
    return mutual.i == p->auth_cksum + 1;
}

char *get_krb_security(str, host_inst, realm, cksum)
char *str;
char *host_inst, *realm;
u_int32_t *cksum;
{
    KTEXT_ST ticket;
    int rc;
    char inst[INST_SZ];

    *cksum = kerberos_cksum(str);

#if CLIENT_HOST_INSTANCE == HOSTNAME_INSTANCE
#undef HOSTNAME_INSTANCE
#define HOSTNAME_INSTANCE host_inst
#endif

    /*
     * the instance must be in writable memory of size INST_SZ
     * krb_mk_req might change it
     */
    strncpy(inst, CLIENT_HOST_INSTANCE, sizeof(inst) - 1);
    inst[sizeof(inst) - 1] = '\0';
    if((rc = krb_mk_req(&ticket, CLIENT_HOST_PRINCIPLE, inst, realm, *cksum))) {
	if(rc == NO_TKT_FIL) {
	    /* It's been kdestroyed.  Get a new one and try again */
	    kerberos_service_init();
	    rc = krb_mk_req(&ticket, CLIENT_HOST_PRINCIPLE, 
			    CLIENT_HOST_INSTANCE, realm, *cksum);
	}
	if(rc) return NULL;
    }
    return stralloc2("SECURITY TICKET ",
		     bin2astr((unsigned char *)ticket.dat, ticket.length));
}


int krb4_security_ok(addr, str, cksum, errstr)
struct sockaddr_in *addr;
char *str;
u_int32_t cksum;
char **errstr;
{
    KTEXT_ST ticket;
    AUTH_DAT auth;
    char *ticket_str, *user, inst[INST_SZ], hname[256];
    struct passwd *pwptr;
    int myuid, rc;
    char *s;
    int ch;

    /* extract the ticket string from the message */

    s = str;
    ch = *s++;

    skip_whitespace(s, ch);
    if(ch == '\0') {
	*errstr = newstralloc(*errstr, "[bad krb4 security line]");
	return 0;
    }
#define sc "TICKET"
    if(strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	*errstr = newstralloc(*errstr, "[bad krb4 security line]");
	return 0;
    }
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc
    skip_whitespace(s, ch);
    ticket_str = s - 1;
    skip_line(s, ch);
    s[-1] = '\0';

    /* convert to binary ticket */

    astr2bin(ticket_str, (unsigned char *)ticket.dat, &ticket.length);

    /* consult kerberos server */

#if CLIENT_HOST_INSTANCE == HOSTNAME_INSTANCE
    if (gethostname(hname, sizeof(hname)) < 0) {
	*errstr = newvstralloc(*errstr,
	    "[kerberos error: can't get hostname: ", strerror(errno), "}",
	    NULL);
	return 0;
    }
#undef HOSTNAME_INSTANCE
#define HOSTNAME_INSTANCE krb_get_phost(hname)
#endif

    /*
     * the instance must be in writable memory of size INST_SZ
     * krb_rd_req might change it.
     */
    strncpy(inst, CLIENT_HOST_INSTANCE, sizeof(inst) - 1);
    inst[sizeof(inst) - 1] = '\0';
    rc = krb_rd_req(&ticket, CLIENT_HOST_PRINCIPLE, inst,
		    addr->sin_addr.s_addr, &auth, CLIENT_HOST_KEY_FILE);
    if(rc) {
	*errstr = newvstralloc(*errstr,
			       "[kerberos error: ", krb_err_txt[rc], "]",
			       NULL);
	return 0;
    }

    /* verify checksum */

    dbprintf(("msg checksum %d auth checksum %d\n", 
	      cksum, auth.checksum));

    if(cksum != auth.checksum) {
	*errstr = newstralloc(*errstr, "[kerberos error: checksum mismatch]");
	dbprintf(("checksum error: exp %d got %d\n", 
		  auth.checksum, cksum));
	return 0;
    }

    /* save key/cksum for mutual auth and dump encryption */

    memcpy(session_key, auth.session, sizeof(session_key));
    auth_cksum = auth.checksum;

    /* lookup our local user name */

#ifdef FORCE_USERID
    /*
     * if FORCE_USERID is set, then we want to check the uid that we're
     * forcing ourselves to.  Since we'll need root access to grab at the
     * srvtab file, we're actually root, although we'll be changing into
     * CLIENT_LOGIN once we're done the kerberos authentication.
     */
    if((pwptr = getpwnam(CLIENT_LOGIN)) == NULL)
        error("error [getpwnam(%s) fails]", CLIENT_LOGIN);
#else
    myuid = getuid();
    if((pwptr = getpwuid(myuid)) == NULL)
        error("error [getpwuid(%d) fails]", myuid);
#endif

    /* check the .klogin file */

    /*
     * some implementations of kerberos will call one of the getpw()
     * routines (getpwuid(), I think), which overwrites the value of 
     * pwptr->pw_name if the user you want to check disagrees with
     * who has the current uid.  (as in the case when we're still running
     * as root, and we have FORCE_USERID set).
     */
    user = stralloc(pwptr->pw_name);

    if(kuserok(&auth, user)) {
	*errstr = newvstralloc(*errstr,
			       "[",
			       "access as ", user, " not allowed from ",
			       auth.pname, ".", auth.pinst, "@", auth.prealm,
			       "]", NULL);
	dbprintf(("kuserok check failed: %s\n", *errstr));
	amfree(user);
	return 0;
    }
    amfree(user);

    dbprintf(("krb4 security check passed\n"));
    return 1;
}

/* ---------------- */

/* XXX - I'm getting astrs with the high bit set in the debug output!?? */

#define hex_digit(d)	("0123456789ABCDEF"[(d)])
#define unhex_digit(h)	(((h) - '0') > 9? ((h) - 'A' + 10) : ((h) - '0'))

char *bin2astr(buf, len)
unsigned char *buf;
int len;
{
    char *str, *q;
    unsigned char *p;
    int slen, i, needs_quote;

    /* first pass, calculate string len */

    slen = needs_quote = 0; p = buf;
    if(*p == '"') needs_quote = 1;	/* special case */
    for(i=0;i<len;i++) {
	if(!isgraph(*p)) needs_quote = 1;
	if(isprint(*p) && *p != '$' && *p != '"')
	    slen += 1;
	else
	    slen += 3;
	p++;
    }
    if(needs_quote) slen += 2;

    /* 2nd pass, allocate string and fill it in */

    str = (char *)alloc(slen+1);
    p = buf;
    q = str;
    if(needs_quote) *q++ = '"';
    for(i=0;i<len;i++) {
	if(isprint(*p) && *p != '$' && *p != '"')
	    *q++ = *p++;
	else {
	    *q++ = '$';
	    *q++ = hex_digit((*p >> 4) & 0xF);
	    *q++ = hex_digit(*p & 0xF);
	    p++;
	}
    }
    if(needs_quote) *q++ = '"';
    *q = '\0';
    if(q-str != slen)
	printf("bin2str: hmmm.... calculated %d got %d\n",
	       slen, q-str);
    return str;
}

void astr2bin(astr, buf, lenp)
char *astr;
unsigned char *buf;
int  *lenp;
{
    char *p;
    unsigned char *q;

    p = astr; q = buf;

    if(*p != '"') {
	/* strcpy, but without the null */
	while(*p) *q++ = *p++;
	*lenp = q-buf;
	return;
    }

    p++;
    while(*p != '"') {
	if(*p != '$')
	    *q++ = *p++;
	else {
	    *q++ = (unhex_digit(p[1]) << 4) + unhex_digit(p[2]);
	     p  += 3;
	}
    }
    if(p-astr+1 != strlen(astr))
	printf("astr2bin: hmmm... short inp exp %d got %d\n",
	       strlen(astr), p-astr+1);
    *lenp = q-buf;
}

/* -------------------------- */
/* debug routines */

void
print_hex(str,buf,len)
char *str;
unsigned char *buf;
int len;
{
    int i;

    printf("%s:", str);
    for(i=0;i<len;i++) {
	if(i%25 == 0) putchar('\n');
	printf(" %02X", buf[i]);
    }
    putchar('\n');
}

void
print_ticket(str, tktp)
char *str;
KTEXT tktp;
{
    int i;
    printf("%s: length %d chk %lX\n", str, tktp->length, tktp->mbz);
    print_hex("ticket data", tktp->dat, tktp->length);
    fflush(stdout);
}

void
print_auth(authp)
AUTH_DAT *authp;
{
    printf("\nAuth Data:\n");
    printf("  Principal \"%s\" Instance \"%s\" Realm \"%s\"\n",
	   authp->pname, authp->pinst, authp->prealm);
    printf("  cksum %d life %d keylen %d\n", authp->checksum,
	   authp->life, sizeof(authp->session));
    print_hex("session key", authp->session, sizeof(authp->session));
    fflush(stdout);
}

void
print_credentials(credp)
CREDENTIALS *credp;
{
    printf("\nCredentials:\n");
    printf("  service \"%s\" instance \"%s\" realm \"%s\" life %d kvno %d\n",
	   credp->service, credp->instance, credp->realm, credp->lifetime,
	   credp->kvno);
    print_hex("session key", credp->session, sizeof(credp->session));
    print_hex("ticket", credp->ticket_st.dat, credp->ticket_st.length);
    fflush(stdout);
}
