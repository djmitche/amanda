/* Pipespawn can create up to three pipes; These defines set which pointers
 * should have the other end assigned for a new pipe. If not set, then
 * pipespawn will use a preexisting fd.
 */
#ifndef PIPESPAWN_H
#define PIPESPAWN_H 1

extern char skip_argument[1];

#define STDIN_PIPE 1
#define STDOUT_PIPE 2
#define STDERR_PIPE 4
int pipespawn P((char *prog, int pipedef, int *stdinfd, int *stdoutfd,
		 int *stderrfd, ...));


#endif /* PIPESPAWN_H */
