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
 * $Id: conffile.h,v 1.24.2.1 1999/01/22 20:51:22 oliva Exp $
 *
 * interface for config file reading code
 */
#ifndef CONFFILE_H
#define CONFFILE_H

#include "amanda.h"

#define CONFFILE_NAME "amanda.conf"

typedef enum conf_e {
    CNF_ORG, CNF_MAILTO, CNF_DUMPUSER, CNF_TAPEDEV,
    CNF_CHNGRDEV, CNF_CHNGRFILE, CNF_LABELSTR,
    CNF_TAPELIST, CNF_DISKFILE, CNF_INFOFILE, CNF_LOGDIR, CNF_DISKDIR,
    CNF_INDEXDIR, CNF_TAPETYPE, CNF_DUMPCYCLE, CNF_RUNSPERCYCLE,
    CNF_MAXCYCLE, CNF_TAPECYCLE,
    CNF_DISKSIZE, CNF_NETUSAGE, CNF_INPARALLEL, CNF_TIMEOUT,
    CNF_BUMPSIZE, CNF_BUMPMULT, CNF_BUMPDAYS, CNF_TPCHANGER, CNF_RUNTAPES,
    CNF_MAXDUMPS, CNF_ETIMEOUT, CNF_DTIMEOUT, 
    CNF_TAPEBUFS, CNF_RAWTAPEDEV, CNF_PRINTER, CNF_RESERVE
} confparm_t;

typedef enum auth_e {
    AUTH_BSD, AUTH_KRB4
} auth_t;


typedef struct tapetype_s {
    struct tapetype_s *next;
    int seen;
    char *name;

    char *comment;
    char *lbl_templ;
    unsigned long length;
    unsigned long filemark;
    long speed;

    /* seen flags */
    int s_comment;
    int s_lbl_templ;
    int s_length;
    int s_filemark;
    int s_speed;
} tapetype_t;

/* Dump strategies */
#define DS_SKIP		0	/* Don't do any dumps at all */
#define DS_STANDARD	1	/* Standard (0 1 1 1 1 2 2 2 ...) */
#define DS_NOFULL	2	/* No full's (1 1 1 ...) */
#define DS_NOINC	3	/* No inc's (0 0 0 ...) */
#define DS_4		4	/* ? (0 1 2 3 4 5 6 7 8 9 10 11 ...) */
#define DS_5		5	/* ? (0 1 1 1 1 1 1 1 1 1 1 1 ...) */
#define DS_HANOI	6	/* Tower of Hanoi (? ? ? ? ? ...) */
#define DS_INCRONLY	7	/* Forced fulls (0 1 1 2 2 FORCE0 1 1 ...) */

/* Compression types */
#define COMP_NONE	0	/* No compression */
#define COMP_FAST	1	/* Fast compression on client */
#define COMP_BEST	2	/* Best compression on client */
#define COMP_SERV_FAST	3	/* Fast compression on server */
#define COMP_SERV_BEST	4	/* Best compression on server */

typedef struct dumptype_s {
    struct dumptype_s *next;
    int seen;
    char *name;

    char *comment;
    char *program;
    char *exclude;
    int exclude_list;
    long priority;
    long dumpcycle;
    int maxcycle;
    long frequency;
    auth_t auth;
    int maxdumps;
    time_t start_t;
    int strategy;
    int compress;
    float comprate[2]; /* first is full, second is incremental */
    /* flag options */
    int record:1;
    int skip_incr:1;
    int skip_full:1;
    int no_hold:1;
    int kencrypt:1;
    int ignore:1;
    int index:1;

    /* seen flags */
    int s_comment;
    int s_program;
    int s_exclude;
    int s_priority;
    int s_dumpcycle;
    int s_maxcycle;
    int s_frequency;
    int s_auth;
    int s_maxdumps;
    int s_start_t;
    int s_strategy;
    int s_compress;
    int s_comprate;
    int s_record;
    int s_skip_incr;
    int s_skip_full;
    int s_no_hold;
    int s_kencrypt;
    int s_ignore;
    int s_index;
} dumptype_t;

/* A network interface */
typedef struct interface_s {
    struct interface_s *next;
    int seen;
    char *name;

    char *comment;
    int maxusage;		/* bandwidth we can consume [kb/s] */

    /* seen flags */
    int s_comment;
    int s_maxusage;

    int curusage;		/* current usage */
} interface_t;

/* A holding disk */
typedef struct holdingdisk_s {
    struct holdingdisk_s *next;
    int seen;
    char *name;

    char *comment;
    char *diskdir;
    long disksize;
    long chunksize;

    int s_comment;
    int s_disk;
    int s_size;
    int s_csize;

    void *up;			/* generic user pointer */
} holdingdisk_t;

int read_conffile P((char *filename));
int getconf_seen P((confparm_t parameter));
int getconf_int P((confparm_t parameter));
double getconf_real P((confparm_t parameter));
char *getconf_str P((confparm_t parameter));
char *getconf_byname P((char *confname));
dumptype_t *lookup_dumptype P((char *identifier));
tapetype_t *lookup_tapetype P((char *identifier));
interface_t *lookup_interface P((char *identifier));
holdingdisk_t *getconf_holdingdisks();

#endif /* ! CONFFILE_H */
