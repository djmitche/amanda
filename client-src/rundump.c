#include "amanda.h"
#include "version.h"

char *pname = "rundump";

int main P((int argc, char **argv));

int main(argc, argv)
int argc;
char **argv;
{
#ifdef USE_RUNDUMP
    char *noenv[1];
    int i;
    noenv[0] = (char *)0;
#endif

    dbopen("/tmp/rundump.debug");
    dbprintf(("%s: version %s\n", argv[0], version()));

#if !defined(DUMP) || !defined(USE_RUNDUMP)

#ifndef DUMP
#define ERRMSG "DUMP not available on this system.\n"
#else
#define ERRMSG "rundump not enabled on this system.\n"
#endif

    fprintf(stderr, ERRMSG);
    dbprintf(("%s: " ERRMSG, argv[0]));
    dbclose();
    return 1;

#else

#ifdef FORCE_USERID

    /* we should be invoked by CLIENT_LOGIN */
    {
	struct passwd *pwptr;
	char *pwname = CLIENT_LOGIN;
	if((pwptr = getpwnam(pwname)) == NULL)
	    error("error [cannot find user %s in passwd file]\n", pwname);

	if (getuid() != pwptr->pw_uid)
	    error("error [must be invoked by %s]\n", pwname);

	if (geteuid() != 0)
	    error("error [must be setuid root]\n", pwname);
    }
#endif	/* FORCE_USERID */

    dbprintf(("running: %s: ",DUMP));
    for (i=0; argv[i]; i++)
	dbprintf(("%s ", argv[i]));
    dbprintf(("\n"));

    execve(DUMP, argv, noenv);

    dbprintf(("failed (errno=%d)\n",errno));
    dbclose();

    fprintf(stderr, "rundump: could not exec %s: %s\n",
	    DUMP, strerror(errno));
    return 1;
#endif
}
