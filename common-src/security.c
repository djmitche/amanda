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
 * Authors: the Amanda Development Team.  Its members are listed in a
 * file named AUTHORS, in the root directory of this distribution.
 */
/*
 * $Id: security.c,v 1.16 1998/07/06 18:17:29 jrj Exp $
 *
 * wrapper file for kerberos security
 */

#include "amanda.h"

#if defined(TEST)
#undef dbprintf
#define dbprintf(p)	printf p

void show_stat_info(a, b)
    char *a, *b;
{
    char *name = vstralloc(a, b, NULL);
    struct stat sbuf;
    struct passwd *pwptr;
    char *owner;
    struct group *grptr;
    char *group;

    if (stat(name, &sbuf) != 0) {
	dbprintf(("cannot stat %s: %s\n", name, strerror(errno)));
	amfree(name);
	return;
    }
    if ((pwptr = getpwuid(sbuf.st_uid)) == NULL) {
	owner = alloc(32);
	ap_snprintf(owner, 32, "%ld", (long)sbuf.st_uid);
    } else {
	owner = stralloc(pwptr->pw_name);
    }
    if ((grptr = getgrgid(sbuf.st_gid)) == NULL) {
	group = alloc(32);
	ap_snprintf(owner, 32, "%ld", (long)sbuf.st_gid);
    } else {
	group = stralloc(grptr->gr_name);
    }
    dbprintf(("processing file: %s\n", name));
    dbprintf(("                 owner=%s group=%s mode=%03o\n",
	      owner, group, (int) (sbuf.st_mode & 0777)));
    amfree(name);
    amfree(owner);
    amfree(group);
}
#endif

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
    int myuid, i, j;
    char *s, *fp;
    int ch;
#ifdef USE_AMANDAHOSTS
    FILE *fPerm;
    char *pbuf = NULL;
    char *ptmp;
    int pbuf_len;
    int amandahostsauth = 0;
#else
    int saved_stderr;
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
	amfree(remotehost);
	return 0;
    }

    /* Verify that the hostnames match -- they should theoretically */
    if( strncasecmp( remotehost, hp->h_name, strlen(remotehost)+1 ) != 0 ) {
	*errstr = vstralloc("[",
			    "hostnames do not match: ",
			    remotehost, " ", hp->h_name,
			    "]", NULL);
	amfree(remotehost);
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

   /*   Check even the aliases list. Work around for Solaris if dns goes over NIS */

    if( !hp->h_addr_list[i] ) {
        for (j = 0; hp->h_aliases[j] !=0 ; j++) {
	     if ( strcmp(hp->h_aliases[j],inet_ntoa(addr->sin_addr)) == 0)
	         break;                          /* name is good, keep it */
        }
    }
    if( !hp->h_addr_list[i] && !hp->h_aliases[j] ) {
	*errstr = vstralloc("[",
			    "ip address ", inet_ntoa(addr->sin_addr),
			    " is not in the ip list for ", remotehost,
			    "]",
			    NULL);
	amfree(remotehost);
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
	amfree(remotehost);
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
	amfree(remotehost);
	return 0;
    }
    s += sizeof(sc)-1;
    ch = s[-1];
#undef sc

    skip_whitespace(s, ch);
    if(ch == '\0') {
	*errstr = bad_bsd;
	bad_bsd = NULL;
	amfree(remotehost);
	return 0;
    }
    fp = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    remoteuser = stralloc(fp);
    s[-1] = ch;
    amfree(bad_bsd);

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
     * And, believe it or not, some ruserok()'s try an initgroup just
     * for the hell of it.  Since we probably aren't root at this point
     * it'll fail, and initgroup "helpfully" will blatt "Setgroups: Not owner"
     * into our stderr output even though the initgroup failure is not a
     * problem and is expected.  Thanks a lot.  Not.
     */
    chdir(pwptr->pw_dir);       /* pamper braindead ruserok's */
#ifndef USE_AMANDAHOSTS
    saved_stderr = dup(2);
    close(2);			/*  " */

#if defined(TEST)
    {
	char *dir = stralloc(pwptr->pw_dir);

	dbprintf(("calling ruserok(%s, %d, %s, %s)\n",
	          remotehost, myuid == 0, remoteuser, localuser));
	if (myuid == 0) {
	    dbprintf(("because you are running as root, "));
	    dbprintf(("/etc/hosts.equiv will not be used\n"));
	} else {
	    show_stat_info("/etc/hosts.equiv", NULL);
	}
	show_stat_info(dir, "/.rhosts");
	amfree(dir);
    }
#endif

    if(ruserok(remotehost, myuid == 0, remoteuser, localuser) == -1) {
	dup2(saved_stderr,2);
	close(saved_stderr);
	*errstr = vstralloc("[",
			    "access as ", localuser, " not allowed",
			    " from ", remoteuser, "@", remotehost,
			    "]", NULL);
	dbprintf(("check failed: %s\n", *errstr));
	amfree(remotehost);
	amfree(localuser);
	amfree(remoteuser);
	return 0;
    }

    dup2(saved_stderr,2);
    close(saved_stderr);
    chdir("/");		/* now go someplace where I can't drop core :-) */
    dbprintf(("bsd security check passed\n"));
    amfree(remotehost);
    amfree(localuser);
    amfree(remoteuser);
    return 1;
#else
    /* We already chdired to ~amandauser */

#if defined(TEST)
    show_stat_info(pwptr->pw_dir, "/.amandahosts");
#endif

    if((fPerm = fopen(".amandahosts", "r")) == NULL) {
#if defined(TEST)
	dbprintf(("fopen failed: %s\n", strerror(errno)));
#endif
	*errstr = vstralloc("[",
			    "access as ", localuser, " not allowed",
			    " from ", remoteuser, "@", remotehost,
			    "]", NULL);
	dbprintf(("check failed: %s\n", *errstr));
	amfree(remotehost);
	amfree(localuser);
	amfree(remoteuser);
	return 0;
    }

    for(; (pbuf = agets(fPerm)) != NULL; free(pbuf)) {
#if defined(TEST)
	dbprintf(("processing line: <%s>\n", pbuf));
#endif
	pbuf_len = strlen(pbuf);
	s = pbuf;
	ch = *s++;

	/* Find end of remote host */
	skip_non_whitespace(s, ch);
	if(s - 1 == pbuf) {
	    memset(pbuf, '\0', pbuf_len);	/* leave no trace */
	    continue;				/* no remotehost field */
	}
	s[-1] = '\0';				/* terminate remotehost field */

	/* Find start of remote user */
	skip_whitespace(s, ch);
	if(ch == '\0') {
	    ptmp = localuser;			/* no remoteuser field */
	} else {
	    ptmp = s-1;				/* start of remoteuser field */

	    /* Find end of remote user */
	    skip_non_whitespace(s, ch);
	    s[-1] = '\0';			/* terminate remoteuser field */
	}
#if defined(TEST)
	dbprintf(("comparing %s with\n", pbuf));
	dbprintf(("          %s (%s)\n",
		  remotehost,
		  (strcasecmp(pbuf, remotehost) == 0) ? "match" : "no match"));
	dbprintf(("      and %s with\n", ptmp));
	dbprintf(("          %s (%s)\n",
		  remoteuser,
		  (strcasecmp(ptmp, remoteuser) == 0) ? "match" : "no match"));
#endif
	if(strcasecmp(pbuf, remotehost) == 0 && strcasecmp(ptmp, remoteuser) == 0) {
	    amandahostsauth = 1;
	    break;
	}
	memset(pbuf, '\0', pbuf_len);		/* leave no trace */
    }
    afclose(fPerm);
    amfree(pbuf);

    if( amandahostsauth ) {
	chdir("/");      /* now go someplace where I can't drop core :-) */
	dbprintf(("amandahosts security check passed\n"));
	amfree(remotehost);
	amfree(localuser);
	amfree(remoteuser);
	return 1;
    }

    *errstr = vstralloc("[",
			"access as ", localuser, " not allowed",
			" from ", remoteuser, "@", remotehost,
			"]", NULL);
    dbprintf(("check failed: %s\n", *errstr));

    amfree(remotehost);
    amfree(localuser);
    amfree(remoteuser);
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
#if defined(TEST)
    dbprintf(("You configured Amanda using --without-bsd-security, so it\n"));
    dbprintf(("will let anyone on the Internet connect and do dumps of\n"));
    dbprintf(("your system unless you have some other kind of protection,\n"));
    dbprintf(("such as a firewall or TCP wrappers.\n"));
#endif
    return 1;
}

#endif /* ! BSD_SECURITY */

#if defined(TEST)

int
main (argc, argv)
{
    char *remoteuser;
    char *remotehost;
    struct hostent *hp;
    struct sockaddr_in fake;
    char *str;
    char *errstr;
    int r;

    fputs("Remote user: ", stdout);
    fflush(stdout);
    if ((remoteuser = agets(stdin)) == NULL) {
	return 0;
    }
    str = stralloc2("USER ", remoteuser);

    fputs("Remote host: ", stdout);
    fflush(stdout);
    if ((remotehost = agets(stdin)) == NULL) {
	return 0;
    }
    if ((hp = gethostbyname(remotehost)) == NULL) {
	dbprintf(("cannot look up remote host %s\n", remotehost));
	return 1;
    }
    memcpy((char *)&fake.sin_addr, (char *)hp->h_addr, sizeof(hp->h_addr));
    fake.sin_port = htons(IPPORT_RESERVED - 1);

    if ((r = bsd_security_ok(&fake, str, 0, &errstr)) == 0) {
	dbprintf(("security check of %s@%s failed\n", remoteuser, remotehost));
	dbprintf(("%s\n", errstr));
    } else {
	dbprintf(("security check of %s@%s passed\n", remoteuser, remotehost));
    }
    return r;
}

#endif
