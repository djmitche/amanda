#include "amanda.h"
#include "pipespawn.h"
#include "arglist.h"

char skip_argument[1];

#ifdef STDC_HEADERS
int pipespawn(char *prog, int pipedef, int *stdinfd, int *stdoutfd,
	      int *stderrfd, ...)
#else
int pipespawn(prog, pipedef, stdinfd, stdoutfd, stderrfd, va_alist)
char *prog;
int pipedef;
int *stdinfd, *stdoutfd, *stderrfd;
va_dcl
#endif
{
    va_list ap;
    int argc;
    char **argv;
    int pid, i, inpipe[2], outpipe[2], errpipe[2], passwdpipe[2];
    char *passwdvar = NULL;
    int *passwdfd = NULL;
    char number[NUM_STR_SIZE];
    char *arg;
    char *e;
    int ch;
    char **env;
    char **newenv;

    /*
     * Log the command line and count the args.
     */
    dbprintf(("%s: spawning %s in pipeline\n", get_pname(), prog));
    dbprintf(("%s: argument list:", get_pname()));
    arglist_start(ap, stderrfd);
    if ((pipedef & PASSWD_PIPE) != 0) {
	passwdvar = arglist_val(ap, char *);
	passwdfd = arglist_val(ap, int *);
    }
    argc = 0;
    while((arg = arglist_val(ap, char *)) != NULL) {
	if (arg == skip_argument) {
	    continue;
	}
	argc++;
	dbprintf((" "));
	for(i = 0; (ch = arg[i]) != '\0' && isprint(ch) && ch != ' '; i++) {}
	if(ch != '\0' || i == 0) {
	    dbprintf(("\""));
	}
	dbprintf(("%s", arg));
	if(ch != '\0' || i == 0) {
	    dbprintf(("\""));
	}
    }
    arglist_end(ap);
    dbprintf(("\n"));

    /*
     * Create the pipes
     */
    if ((pipedef & STDIN_PIPE) != 0) {
	if(pipe(inpipe) == -1) {
	    error("error [open pipe to %s: %s]", prog, strerror(errno));
	}
    }
    if ((pipedef & STDOUT_PIPE) != 0) {
	if(pipe(outpipe) == -1) {
	    error("error [open pipe to %s: %s]", prog, strerror(errno));
	}
    }
    if ((pipedef & STDERR_PIPE) != 0) {
	if(pipe(errpipe) == -1) {
	    error("error [open pipe to %s: %s]", prog, strerror(errno));
	}
    }
    if ((pipedef & PASSWD_PIPE) != 0) {
	if(pipe(passwdpipe) == -1) {
	    error("error [open pipe to %s: %s]", prog, strerror(errno));
	}
    }

    /*
     * Fork and set up the return or run the program.
     */
    switch(pid = fork()) {
    case -1:
	e = strerror(errno);
	error("error [fork %s: %s]", prog, e);
    default:	/* parent process */
	if ((pipedef & STDIN_PIPE) != 0) {
	    aclose(inpipe[0]);		/* close input side of pipe */
	    *stdinfd = inpipe[1];
	}
	if ((pipedef & STDOUT_PIPE) != 0) {
	    aclose(outpipe[1]);		/* close output side of pipe */
	    *stdoutfd = outpipe[0];
	}
	if ((pipedef & STDERR_PIPE) != 0) {
	    aclose(errpipe[1]);		/* close output side of pipe */
	    *stderrfd = errpipe[0];
	}
	if ((pipedef & PASSWD_PIPE) != 0) {
	    aclose(passwdpipe[0]);	/* close input side of pipe */
	    *passwdfd = passwdpipe[1];
	}
	break;
    case 0:		/* child process */
	if ((pipedef & STDIN_PIPE) != 0) {
	    aclose(inpipe[1]);		/* close output side of pipe */
	} else {
	    inpipe[0] = *stdinfd;
	}
	if ((pipedef & STDOUT_PIPE) != 0) {
	    aclose(outpipe[0]);		/* close input side of pipe */
	} else {
	    outpipe[1] = *stdoutfd;
	}
	if ((pipedef & STDERR_PIPE) != 0) {
	    aclose(errpipe[0]);		/* close input side of pipe */
	} else {
	    errpipe[1] = *stderrfd;
	}
	if ((pipedef & PASSWD_PIPE) != 0) {
	    aclose(passwdpipe[1]);	/* close output side of pipe */
	}

	/*
	 * Shift the pipes to the standard file descriptors as requested.
	 */
	if(dup2(inpipe[0], 0) == -1) {
	    error("error [spawn %s: dup2 in: %s]", prog, strerror(errno));
	}
	if(dup2(outpipe[1], 1) == -1) {
	    error("error [spawn %s: dup2 out: %s]", prog, strerror(errno));
	}
	if(dup2(errpipe[1], 2) == -1) {
	    error("error [spawn %s: dup2 err: %s]", prog, strerror(errno));
	}

	/*
	 * Create the argument vector.
	 */
	arglist_start(ap, stderrfd);
	if ((pipedef & PASSWD_PIPE) != 0) {
	    passwdvar = arglist_val(ap, char *);
	    passwdfd = arglist_val(ap, int *);
	}
	argv = (char **)alloc((argc + 1) * sizeof(*argv));
	i = 0;
	while((argv[i] = arglist_val(ap, char *)) != NULL) {
	    if (argv[i] != skip_argument) {
		i++;
	    }
	}
	arglist_end(ap);

	/*
	 * Get the "safe" environment.  If we are sending a password to
	 * the child via a pipe, add the environment variable for that.
	 */
	env = safe_env();
	if ((pipedef & PASSWD_PIPE) != 0) {
	    for(i = 0; env[i] != NULL; i++) {}
	    newenv = (char **)alloc((i + 1 + 1) * sizeof(*newenv));
	    snprintf(number, sizeof(number), "%d", passwdpipe[0]);
	    newenv[0] = vstralloc(passwdvar, "=", number, NULL);
	    for(i = 0; (newenv[i + 1] = env[i]) != NULL; i++) {}
	    env = newenv;
	}

	execve(prog, argv, env);
	e = strerror(errno);
	error("error [exec %s: %s]", prog, e);
	/* NOTREACHED */
    }
    return pid;
}

int pipespawnv(prog, pipedef, stdinfd, stdoutfd, stderrfd, my_argv)
char *prog;
int pipedef;
int *stdinfd, *stdoutfd, *stderrfd;
char **my_argv;
{
    int argc;
    int pid, i, inpipe[2], outpipe[2], errpipe[2], passwdpipe[2];
    char *passwdvar = NULL;
    int *passwdfd = NULL;
    char number[NUM_STR_SIZE];
    char **arg;
    char *e;
    int ch;
    char **env;
    char **newenv;

    /*
     * Log the command line and count the args.
     */
    dbprintf(("%s: spawning %s in pipeline\n", get_pname(), prog));
    dbprintf(("%s: argument list:", get_pname()));
    if ((pipedef & PASSWD_PIPE) != 0) {
	passwdvar = *my_argv++;
	passwdfd = (int *)*my_argv++;
    }
    argc = 0;
    for(arg = my_argv; *arg != NULL; arg++) {
	if (*arg == skip_argument) {
	    continue;
	}
	argc++;
	dbprintf((" "));
	for(i = 0; (ch = (*arg)[i]) != '\0' && isprint(ch) && ch != ' '; i++) {}
	if(ch != '\0' || i == 0) {
	    dbprintf(("\""));
	}
	dbprintf(("%s", *arg));
	if(ch != '\0' || i == 0) {
	    dbprintf(("\""));
	}
    }
    dbprintf(("\n"));

    /*
     * Create the pipes
     */
    if ((pipedef & STDIN_PIPE) != 0) {
	if(pipe(inpipe) == -1) {
	    error("error [open pipe to %s: %s]", prog, strerror(errno));
	}
    }
    if ((pipedef & STDOUT_PIPE) != 0) {
	if(pipe(outpipe) == -1) {
	    error("error [open pipe to %s: %s]", prog, strerror(errno));
	}
    }
    if ((pipedef & STDERR_PIPE) != 0) {
	if(pipe(errpipe) == -1) {
	    error("error [open pipe to %s: %s]", prog, strerror(errno));
	}
    }
    if ((pipedef & PASSWD_PIPE) != 0) {
	if(pipe(passwdpipe) == -1) {
	    error("error [open pipe to %s: %s]", prog, strerror(errno));
	}
    }

    /*
     * Fork and set up the return or run the program.
     */
    switch(pid = fork()) {
    case -1:
	e = strerror(errno);
	error("error [fork %s: %s]", prog, e);
    default:	/* parent process */
	if ((pipedef & STDIN_PIPE) != 0) {
	    aclose(inpipe[0]);		/* close input side of pipe */
	    *stdinfd = inpipe[1];
	}
	if ((pipedef & STDOUT_PIPE) != 0) {
	    aclose(outpipe[1]);		/* close output side of pipe */
	    *stdoutfd = outpipe[0];
	}
	if ((pipedef & STDERR_PIPE) != 0) {
	    aclose(errpipe[1]);		/* close output side of pipe */
	    *stderrfd = errpipe[0];
	}
	if ((pipedef & PASSWD_PIPE) != 0) {
	    aclose(passwdpipe[0]);	/* close input side of pipe */
	    *passwdfd = passwdpipe[1];
	}
	break;
    case 0:		/* child process */
	if ((pipedef & STDIN_PIPE) != 0) {
	    aclose(inpipe[1]);		/* close output side of pipe */
	} else {
	    inpipe[0] = *stdinfd;
	}
	if ((pipedef & STDOUT_PIPE) != 0) {
	    aclose(outpipe[0]);		/* close input side of pipe */
	} else {
	    outpipe[1] = *stdoutfd;
	}
	if ((pipedef & STDERR_PIPE) != 0) {
	    aclose(errpipe[0]);		/* close input side of pipe */
	} else {
	    errpipe[1] = *stderrfd;
	}

	/*
	 * Shift the pipes to the standard file descriptors as requested.
	 */
	if(dup2(inpipe[0], 0) == -1) {
	    error("error [spawn %s: dup2 in: %s]", prog, strerror(errno));
	}
	if(dup2(outpipe[1], 1) == -1) {
	    error("error [spawn %s: dup2 out: %s]", prog, strerror(errno));
	}
	if(dup2(errpipe[1], 2) == -1) {
	    error("error [spawn %s: dup2 err: %s]", prog, strerror(errno));
	}

	/*
	 * Get the "safe" environment.  If we are sending a password to
	 * the child via a pipe, add the environment variable for that.
	 */
	env = safe_env();
	if ((pipedef & PASSWD_PIPE) != 0) {
	    for(i = 0; env[i] != NULL; i++) {}
	    newenv = (char **)alloc((i + 1 + 1) * sizeof(*newenv));
	    snprintf(number, sizeof(number), "%d", passwdpipe[0]);
	    newenv[0] = vstralloc(passwdvar, "=", number, NULL);
	    for(i = 0; (newenv[i + 1] = env[i]) != NULL; i++) {}
	    env = newenv;
	}

	execve(prog, my_argv, env);
	e = strerror(errno);
	error("error [exec %s: %s]", prog, e);
	/* NOTREACHED */
    }
    return pid;
}
