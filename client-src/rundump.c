#include "amanda.h"
#include "version.h"

char *pname = "rundump";

int main P((int argc, char **argv));

int main(argc, argv)
int argc;
char **argv;
{
    char *dump_program;
#ifdef USE_RUNDUMP
    char *noenv[1];
    int i;
    noenv[0] = (char *)0;
#endif /* USE_RUNDUMP */

    dbopen("/tmp/rundump.debug");
    dbprintf(("%s: version %s\n", argv[0], version()));

#if (!defined(DUMP) && !defined(XFSDUMP)) || !defined(USE_RUNDUMP)

#if !defined(DUMP) && !defined(XFSDUMP)
#define ERRMSG "DUMP not available on this system.\n"
#else
#define ERRMSG "rundump not enabled on this system.\n"
#endif

    fprintf(stderr, ERRMSG);
    dbprintf(("%s: " ERRMSG, argv[0]));
    dbclose();
    return 1;

#else

    /* we should be invoked by CLIENT_LOGIN */
    {
	struct passwd *pwptr;
	char *pwname = CLIENT_LOGIN;
	if((pwptr = getpwnam(pwname)) == NULL)
	    error("error [cannot find user %s in passwd file]\n", pwname);

	chown("/tmp/runtar.debug", pwptr->pw_uid);

#ifdef FORCE_USERID
	if (getuid() != pwptr->pw_uid)
	    error("error [must be invoked by %s]\n", pwname);

	if (geteuid() != 0)
	    error("error [must be setuid root]\n", pwname);
#endif	/* FORCE_USERID */
    }

    /* If XFSDUMP is defined but DUMP isn't, XFSDUMP is used by default.
       If XFSDUMP is not defined, DUMP is used by default.
       If both are defined, DUMP is used unless argv[0] is "xfsdump". */

#if defined(XFSDUMP) || !defined(DUMP)

#ifdef DUMP
    if (strcmp(argv[0], "xfsdump") == 0)
#endif /* DUMP */

        dump_program = XFSDUMP;

#ifdef DUMP
    else /* strcmp(argv[0], "xfsdump") != 0 */
#endif /* DUMP */

#endif /* defined(XFSDUMP) || !defined(DUMP) */

#ifdef DUMP
        dump_program = DUMP;
#endif /* DUMP */

    dbprintf(("running: %s: ",dump_program));
    for (i=0; argv[i]; i++)
	dbprintf(("%s ", argv[i]));
    dbprintf(("\n"));

    execve(dump_program, argv, noenv);

    dbprintf(("failed (errno=%d)\n",errno));
    dbclose();

    fprintf(stderr, "rundump: could not exec %s: %s\n",
	    dump_program, strerror(errno));
    return 1;
#endif
}
