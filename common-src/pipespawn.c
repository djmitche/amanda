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
#define MAX_PIPESPAWN_ARGS	32
    char *argv[MAX_PIPESPAWN_ARGS+1];
    int pid, i, inpipe[2], outpipe[2], errpipe[2];
    char *e;

    dbprintf(("%s: spawning \"%s\" in pipeline\n", get_pname(), prog));
    dbprintf(("%s: argument list:", get_pname()));
    arglist_start(ap, stderrfd);		/* setup argv */
    for(i = 0; i < MAX_PIPESPAWN_ARGS; i++) {
	char *arg = arglist_val(ap, char *);
	if (arg == NULL)
	  break;
	if (arg == skip_argument)
	  continue;
	dbprintf((" \"%s\"", arg));
    }
    arglist_end(ap);
    dbprintf(("\n"));


    if ((pipedef & STDIN_PIPE) != 0)
      if(pipe(inpipe) == -1) {
	e = strerror(errno);
	dbprintf(("error [open pipe to %s: %s]\n", prog, e));
	error("error [open pipe to %s: %s]", prog, e);
      }
    if ((pipedef & STDOUT_PIPE) != 0)
      if(pipe(outpipe) == -1) {
	e = strerror(errno);
	dbprintf(("error [open pipe to %s: %s]\n", prog, e));
	error("error [open pipe to %s: %s]", prog, e);
      }
    if ((pipedef & STDERR_PIPE) != 0)
      if(pipe(errpipe) == -1) {
	e = strerror(errno);
	dbprintf(("error [open pipe to %s: %s]\n", prog, e));
	error("error [open pipe to %s: %s]", prog, e);
      }
    

    switch(pid = fork()) {
    case -1:
      e = strerror(errno);
      dbprintf(("error [fork %s: %s]\n", prog, e));
      error("error [fork %s: %s]", prog, e);
    default:	/* parent process */
      if ((pipedef & STDIN_PIPE) != 0) {
	aclose(inpipe[0]);	/* close input side of pipe */
	*stdinfd = inpipe[1];
      }
      if ((pipedef & STDOUT_PIPE) != 0) {
	aclose(outpipe[1]);	/* close output side of pipe */
	*stdoutfd = outpipe[0];
      }
      if ((pipedef & STDERR_PIPE) != 0) {
	aclose(errpipe[1]);	/* close output side of pipe */
	*stderrfd = errpipe[0];
      }
      
	break;
    case 0:		/* child process */
      if ((pipedef & STDIN_PIPE) != 0)
	aclose(inpipe[1]);	/* close output side of pipe */
      else
	inpipe[0] = *stdinfd;
      if ((pipedef & STDOUT_PIPE) != 0)
	aclose(outpipe[0]);	/* close input side of pipe */
      else
	outpipe[1] = *stdoutfd;
      if ((pipedef & STDERR_PIPE) != 0)
	aclose(errpipe[0]);	/* close input side of pipe */
      else
	errpipe[1] = *stderrfd;

	if(dup2(inpipe[0], 0) == -1) {
	  e = strerror(errno);
	  dbprintf(("error [spawn %s: dup2 in: %s]\n", prog, e));
	  error("error [spawn %s: dup2 in: %s]", prog, e);
	}
	if(dup2(outpipe[1], 1) == -1) {
	  e = strerror(errno);
	  dbprintf(("error [spawn %s: dup2 out: %s]\n", prog, e));
	  error("error [spawn %s: dup2 out: %s]", prog, e);
	}
	if(dup2(errpipe[1], 2) == -1) {
	  e = strerror(errno);
	  dbprintf(("error [spawn %s: dup2 err: %s]\n", prog, e));
	  error("error [spawn %s: dup2 err: %s]", prog, e);
	}
	
	arglist_start(ap, stderrfd);		/* setup argv */
	for(i = 0; i < MAX_PIPESPAWN_ARGS; i++) {
	  char * tmp = arglist_val(ap, char *);
	  if (tmp == NULL)
	    break;
	  else if (tmp == skip_argument)
	    i --;
	  else
	    argv[i] = tmp;
	}
	argv[i] = NULL;
	arglist_end(ap);

	execve(prog, argv, safe_env());
	e = strerror(errno);
	dbprintf(("error [exec %s: %s]\n", prog, e));
	error("error [exec %s: %s]", prog, e);
	/* NOTREACHED */
    }
    return pid;
}
