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

#if (!defined(DUMP) && !defined(XFSDUMP)) && !defined(VXDUMP) \
    || !defined(USE_RUNDUMP)

#if !defined(USE_RUNDUMP)
#define ERRMSG "rundump not enabled on this system.\n"
#else
#define ERRMSG "DUMP not available on this system.\n"
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

	chown("/tmp/rundump.debug", pwptr->pw_uid, getgid());

#ifdef FORCE_USERID
	if (getuid() != pwptr->pw_uid)
	    error("error [must be invoked by %s]\n", pwname);

	if (geteuid() != 0)
	    error("error [must be setuid root]\n", pwname);
#endif	/* FORCE_USERID */
    }

#ifdef XFSDUMP
    
    if (strcmp(argv[0], "xfsdump") == 0)
        dump_program = XFSDUMP;
    else /* strcmp(argv[0], "xfsdump") != 0 */

#endif

#ifdef VXDUMP

    if (strcmp(argv[0], "vxdump") == 0)
        dump_program = VXDUMP;
    else /* strcmp(argv[0], "vxdump") != 0 */

#endif

#if defined(DUMP)
        dump_program = DUMP;
#elif defined(XFSDUMP)
        dump_program = XFSDUMP;
#elif defined(VXDUMP)
	dump_program = VXDUMP;
#else
        dump_program = "dump";
#endif

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
