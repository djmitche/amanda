#include "amanda.h"
#include "version.h"

char *pname = "runtar";

int main P((int argc, char **argv));

int main(argc, argv)
int argc;
char **argv;
{
#ifdef GNUTAR
    char *noenv[1];
    int i;
    noenv[0] = (char *)0;
#endif

    dbopen("/tmp/runtar.debug");
    dbprintf(("%s: version %s\n", argv[0], version()));

#ifndef GNUTAR
    fprintf(stderr,"gnutar not available on this system.\n");
    dbprintf(("%s: gnutar not available on this system.\n", argv[0]));
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
#endif

    dbprintf(("running: %s: ",GNUTAR));
    for (i=0; argv[i]; i++)
	dbprintf(("%s ", argv[i]));
    dbprintf(("\n"));

    execve(GNUTAR, argv, noenv);

    dbprintf(("failed (errno=%d)\n",errno));
    dbclose();

    fprintf(stderr, "runtar: could not exec %s: %s\n",
	    GNUTAR, strerror(errno));
    return 1;
#endif
}
