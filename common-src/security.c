/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1994 University of Maryland
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
 * $Id: security.c,v 1.6 1998/01/14 22:41:25 amcore Exp $
 *
 * wrapper file for kerberos security
 */

#include "amanda.h"

#ifdef KRB4_SECURITY
#include "krb4-security.c"
#endif

int bsd_security_ok P((struct sockaddr_in *addr,
		       char *str, unsigned long cksum, char **errstr));

char *get_bsd_security()
{
    struct passwd *pwptr;

    if((pwptr = getpwuid(getuid())) == NULL)
	error("can't get login name for my uid %ld", (long)getuid());
    return stralloc2("SECURITY USER ", pwptr->pw_name);
}

int security_ok(addr, str, cksum, errstr)
struct sockaddr_in *addr;
char *str;
unsigned long cksum;
char **errstr;
{
#ifdef KRB4_SECURITY
    if(krb4_auth)
	return krb4_security_ok(addr, str, cksum, errstr);
    else
#endif
	return bsd_security_ok(addr, str, cksum, errstr);
}

#ifdef BSD_SECURITY

int bsd_security_ok(addr, str, cksum, errstr)
struct sockaddr_in *addr;
char *str;
unsigned long cksum;
char **errstr;
{
    char *remotehost = NULL, *remoteuser = NULL, *localuser = NULL;
    char *bad_bsd = NULL;
    struct hostent *hp;
    struct passwd *pwptr;
    int myuid, i;
    char *s, *fp;
    int ch;
#ifdef USE_AMANDAHOSTS
    FILE *fPerm;
    char *pbuf = NULL;
    char *ptmp;
    int pbuf_len;
    int amandahostsauth = 0;
#endif
    *errstr = NULL;

    /* what host is making the request? */

    hp = gethostbyaddr((char *)&addr->sin_addr, sizeof(addr->sin_addr),
		       AF_INET);
    if(hp == NULL) {
	/* XXX include remote address in message */
	*errstr = vstralloc("[",
			    "addr ", inet_ntoa(addr->sin_addr), ": ",
			    "hostname lookup failed",
			    "]", NULL);
	return 0;
    }
    remotehost = stralloc(hp->h_name);

    /* Now let's get the hostent for that hostname */
    hp = gethostbyname( remotehost );
    if(hp == NULL) {
	/* XXX include remote hostname in message */
	*errstr = vstralloc("[",
			    "addr ", remotehost, ": ",
			    "hostname lookup failed",
			    "]", NULL);
	afree(remotehost);
	return 0;
    }

    /* Verify that the hostnames match -- they should theoretically */
    if( strcasecmp( remotehost, hp->h_name ) ) {
	*errstr = vstralloc("[",
			    "hostnames do not match: ",
			    remotehost, " ", hp->h_name,
			    "]", NULL);
	afree(remotehost);
	return 0;
    }

    /* Now let's verify that the ip which gave us this hostname
     * is really an ip for this hostname; or is someone trying to
     * break in? (THIS IS THE CRUCIAL STEP)
     */
    for (i = 0; hp->h_addr_list[i]; i++) {
	if (memcmp(hp->h_addr_list[i],
		   (char *) &addr->sin_addr, sizeof(addr->sin_addr)) == 0)
	    break;                     /* name is good, keep it */
    }

    /* If we did not find it, your DNS is messed up or someone is trying
     * to pull a fast one on you. :(
     */
    if( !hp->h_addr_list[i] ) {
	*errstr = vstralloc("[",
			    "ip address ", inet_ntoa(addr->sin_addr),
			    " is not in the ip list for ", remotehost,
			    "]",
			    NULL);
	afree(remotehost);
	return 0;
    }

    /* next, make sure the remote port is a "reserved" one */

    if(ntohs(addr->sin_port) >= IPPORT_RESERVED) {
	char number[NUM_STR_SIZE];

	ap_snprintf(number, sizeof(number), "%d", ntohs(addr->sin_port));
	*errstr = vstralloc("[",
			    "host ", remotehost, ": ",
			    "port ", number, " not secure",
			    "]", NULL);
	afree(remotehost);
	return 0;
    }

    /* extract the remote user name from the message */

    s = str;
    ch = *s++;

    bad_bsd = vstralloc("[",
			"host ", remotehost, ": ",
			"bad bsd security line",
			"]", NULL);

#define sc "USER"
    if(strncmp(s - 1, sc, sizeof(sc)-1) != 0) {
	*errstr = bad_bsd;
	bad_bsd = NULL;
	afree(remotehost);
	return 0;
    }
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc

    skip_whitespace(s, ch);
    if(ch == '\0') {
	*errstr = bad_bsd;
	bad_bsd = NULL;
	afree(remotehost);
	return 0;
    }
    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    remoteuser = stralloc(fp);
    s[-1] = ch;
    afree(bad_bsd);

    /* lookup our local user name */

    myuid = getuid();
    if((pwptr = getpwuid(myuid)) == NULL)
        error("error [getpwuid(%d) fails]", myuid);

    localuser = stralloc(pwptr->pw_name);

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
    close(2);			/*  " */

    if(ruserok(remotehost, myuid == 0, remoteuser, localuser) == -1) {
	dup2(1,2);
	*errstr = vstralloc("[",
			    "access as ", localuser, " not allowed",
			    " from ", remoteuser, "@", remotehost,
			    "]", NULL);
	dbprintf(("check failed: %s\n", *errstr));
	afree(remotehost);
	afree(localuser);
	afree(remoteuser);
	return 0;
    }

    dup2(1,2);
    chdir("/");		/* now go someplace where I can't drop core :-) */
    dbprintf(("bsd security check passed\n"));
    afree(remotehost);
    afree(localuser);
    afree(remoteuser);
    return 1;
#else
    /* We already chdired to ~amandauser */
    if((fPerm = fopen(".amandahosts", "r")) == NULL) {
	*errstr = vstralloc("[",
			    "access as ", localuser, " not allowed",
			    " from ", remoteuser, "@", remotehost,
			    "]", NULL);
	dbprintf(("check failed: %s\n", *errstr));
	afree(remotehost);
	afree(localuser);
	afree(remoteuser);
	return 0;
    }

    for(; (pbuf = agets(fPerm)) != NULL; free(pbuf)) {
	pbuf_len = strlen(pbuf);
	s = pbuf;
	ch = *s++;

	/* Find end of remote host */
	skip_non_whitespace(s, ch);
	if(ch == '\0') {
	    memset(pbuf, '\0', pbuf_len);	/* leave no trace */
	    continue;				/* no remoteuser field */
	}
	s[-1] = '\0';				/* terminate remotehost field */

	/* Find start of remote user */
	skip_whitespace(s, ch);
	if(ch == '\0') {
	    memset(pbuf, '\0', pbuf_len);	/* leave no trace */
	    continue;				/* no remoteuser field */
	}
	ptmp = s-1;				/* start of remoteuser field */

	/* Find end of remote user */
	skip_non_whitespace(s, ch);
	s[-1] = '\0';				/* terminate remoteuser field */

	if(strcmp(pbuf, remotehost) == 0 && strcmp(ptmp, remoteuser) == 0) {
	    amandahostsauth = 1;
	    break;
	}
	memset(pbuf, '\0', pbuf_len);		/* leave no trace */
    }
    afclose(fPerm);

    if( amandahostsauth ) {
	chdir("/");      /* now go someplace where I can't drop core :-) */
	dbprintf(("amandahosts security check passed\n"));
	afree(remotehost);
	afree(localuser);
	afree(remoteuser);
	return 1;
    }

    *errstr = vstralloc("[",
			"access as ", localuser, " not allowed",
			" from ", remoteuser, "@", remotehost,
			"]", NULL);
    dbprintf(("check failed: %s\n", *errstr));

    afree(remotehost);
    afree(localuser);
    afree(remoteuser);
    return 0;

#endif
}

#else	/* ! BSD_SECURITY */

int bsd_security_ok(addr, str, cksum, errstr)
struct sockaddr_in *addr;
char *str;
unsigned long cksum;
char **errstr;
{
    return 1;
}

#endif /* ! BSD_SECURITY */
