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
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/*
 * $Id: driverio.h,v 1.26 2001/01/24 04:02:59 martinea Exp $
 *
 * driver-related helper functions
 */

#include "event.h"

#include "holding.h"

#define MAX_DUMPERS 63
#define MAX_ARGS 10

#ifndef GLOBAL
#define GLOBAL extern
#endif

/* chunker process structure */

typedef struct chunker_s {
    char *name;			/* name of this chunker */
    int pid;			/* its pid */
    int fd;			/* read/write */
    int result;
    event_handle_t *ev_read;	/* read event handle */
    struct dumper_s *dumper;
} chunker_t;

/* dumper process structure */

typedef struct dumper_s {
    char *name;			/* name of this dumper */
    int pid;			/* its pid */
    int busy, down;		/* state */
    int fd;			/* read/write */
    int result;
    int output_port;		/* output port */
    event_handle_t *ev_read;	/* read event handle */
    event_handle_t *ev_wait;	/* for delayed disks */
    disk_t *dp;			/* disk currently being dumped */
    chunker_t *chunker;
} dumper_t;

typedef struct assignedhd_s {
    holdingdisk_t	*disk;
    long		used;
    long		reserved;
    char		*destname;
} assignedhd_t;

/* schedule structure */

typedef struct sched_s {
    int attempted, priority;
    int level, degr_level;
    long est_time, degr_time;
    unsigned long est_size, degr_size, act_size;
    unsigned long origsize, dumpsize;
    unsigned long dumptime, tapetime;
    char *dumpdate, *degr_dumpdate;
    int est_kps, degr_kps;
    char *destname;				/* file/port name */
    dumper_t *dumper;
    assignedhd_t **holdp;
    time_t timestamp;
    int activehd;
    int no_space;
} sched_t;

#define sched(dp)	((sched_t *) (dp)->up)


/* holding disk reservation structure */

typedef struct holdalloc_s {
    int allocated_dumpers;
    long allocated_space;
} holdalloc_t;

#define holdalloc(hp)	((holdalloc_t *) (hp)->up)

GLOBAL dumper_t dmptable[MAX_DUMPERS];
GLOBAL chunker_t chktable[MAX_DUMPERS];

/* command/result tokens */

typedef enum {
    BOGUS, QUIT, DONE,
    FILE_DUMP, PORT_DUMP, CONTINUE, ABORT,		/* dumper cmds */
    FAILED, TRYAGAIN, NO_ROOM, RQ_MORE_DISK,		/* dumper results */
    ABORT_FINISHED,					/* dumper results */
    START_TAPER, FILE_WRITE, PORT_WRITE,		/* taper cmds */
    PORT, TAPE_ERROR, TAPER_OK,				/* taper results */
    LAST_TOK
} tok_t;

GLOBAL int taper, taper_busy, taper_pid;
GLOBAL event_handle_t *taper_ev_read;

void init_driverio P((void));
void startup_tape_process P((char *taper_program));
void startup_dump_process P((dumper_t *dumper, char *dumper_program));
void startup_dump_processes P((char *dumper_program, int inparallel));
void startup_chunk_process P((chunker_t *chunker, char *chunker_program));

tok_t getresult P((int fd, int show, int *result_argc, char **result_argv, int max_arg));
int taper_cmd P((tok_t cmd, void *ptr, char *destname, int level, char *datestamp));
int dumper_cmd P((dumper_t *dumper, tok_t cmd, disk_t *dp));
int chunker_cmd P((chunker_t *chunker, tok_t cmd, disk_t *dp));
disk_t *serial2disk P((char *str));
void free_serial P((char *str));
char *disk2serial P((disk_t *dp));
void update_info_dumper P((disk_t *dp, long origsize, long dumpsize, long dumptime));
void update_info_taper P((disk_t *dp, char *label, int filenum, int level));
void free_assignedhd P((assignedhd_t **holdp));
