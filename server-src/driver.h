/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991 University of Maryland
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
 * driver.h - defines and globals for the Amanda driver
 */
#define MAX_DUMPERS 15
#define MAX_ARGS 10
#define MAX_LINE 1024

#ifndef GLOBAL
#define GLOBAL extern
#endif

/* dumper process structure */

typedef struct dumper_s {
    int busy, down, pid;
    int infd, outfd;
    disk_t *dp;
} dumper_t;

/* schedule structure */

typedef struct sched_s {
    int attempted, priority;
    int level, degr_level;
    int est_time, degr_time;
    unsigned long est_size, degr_size, act_size;
    char *dumpdate, *degr_dumpdate;
    int est_kps, degr_kps;
    char destname[128];				/* file/port name */
    dumper_t *dumper;
    holdingdisk_t *holdp;
    time_t timestamp;
} sched_t;

#define sched(dp)	((sched_t *) (dp)->up)


/* holding disk reservation structure */

typedef struct holdalloc_s {
    int allocated_dumpers;
    int allocated_space;
} holdalloc_t;

#define holdalloc(hp)	((holdalloc_t *) (hp)->up)


GLOBAL dumper_t dmptable[MAX_DUMPERS];
GLOBAL int inparallel, big_dumpers;
GLOBAL int degraded_mode;

/* command/result tokens */

typedef enum {
    BOGUS, QUIT, DONE,
    FILE_DUMP, PORT_DUMP, CONTINUE, ABORT,		/* dumper cmds */
    FAILED, TRYAGAIN, NO_ROOM, ABORT_FINISHED,		/* dumper results */
    FATAL_TRYAGAIN,
    START_TAPER, FILE_WRITE, PORT_WRITE, 		/* taper cmds */
    PORT, TAPE_ERROR, TAPER_OK,				/* taper results */
    LAST_TOK
} tok_t;

extern char *cmdstr[];
extern char *pname;

GLOBAL tok_t tok;
GLOBAL fd_set readset;
GLOBAL disklist_t waitq, runq, stoppedq, tapeq;
GLOBAL int pending_aborts, inside_dump_to_tape;
GLOBAL int verbose;

GLOBAL int taper, taper_busy, taper_pid;
GLOBAL disk_t *taper_disk;

GLOBAL int argc;
GLOBAL char *argv[MAX_ARGS+1];
GLOBAL int maxfd;
GLOBAL int force_parameters, use_lffo;
GLOBAL char datestamp[80], taper_program[80], dumper_program[80];

/* driver.c functions */

int driver_main P((int argc, char **argv));
int client_constrained P((disk_t *dp));
int sort_by_priority_reversed P((disk_t *a, disk_t *b));
int sort_by_time P((disk_t *a, disk_t *b));
int sort_by_size_reversed P((disk_t *a, disk_t *b));
int start_some_dumps P((disklist_t *rq));
void dump_schedule P((disklist_t *qp, char *str));
void start_degraded_mode P((disklist_t *queuep));
void handle_taper_result P((void));
dumper_t *idle_dumper P((void));
int some_dumps_in_progress P((void));
int num_busy_dumpers P((void));
dumper_t *lookup_dumper P((int fd));
void construct_datestamp P((char *buf));
void handle_dumper_result P((int fd));
disklist_t read_schedule P((disklist_t *waitqp));
int free_kps P((interface_t *ip));
void allocate_bandwidth P((interface_t *ip, int kps));
void deallocate_bandwidth P((interface_t *ip, int kps));
int free_space P((void));
holdingdisk_t *find_diskspace P((unsigned long size));
char *diskname2filename P((char *dname));
void assign_holdingdisk P((holdingdisk_t *holdp, disk_t *diskp));
void adjust_diskspace P((disk_t *diskp, tok_t tok));
void delete_diskspace P((disk_t *diskp));
void holdingdisk_state P((char *time_str));
int dump_to_tape P((disk_t *dp));
int queue_length P((disklist_t q));
void short_dump_state P((void));
void dump_state P((char *str));


/* driverio.c functions */

int main P((int argc, char **argv));
void addfd P((int fd));
char *childstr P((int fd));
void startup_tape_process P((void));
void startup_dump_process P((dumper_t *dumper));
void startup_dump_processes P((void));
tok_t getresult P((int fd));
void taper_cmd P((tok_t cmd, void *ptr));
char *optionstr P((disk_t *dp));
void dumper_cmd P((dumper_t *dumper, tok_t cmd, disk_t *dp));
disk_t *serial2disk P((char *str));
void free_serial P((char *str));
char *disk2serial P((disk_t *dp));
