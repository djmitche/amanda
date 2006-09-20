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
 * conffile.h -  interface for config file reading code
 */
#ifndef CONFFILE_H
#define CONFFILE_H

#include "amanda.h"

#define CONFFILE_NAME "amanda.conf"

typedef enum conf_e {
    CNF_ORG, CNF_MAILTO, CNF_DUMPUSER, CNF_TAPEDEV, CNF_LABELSTR,
    CNF_TAPELIST, CNF_DISKFILE, CNF_INFOFILE, CNF_LOGFILE,
    CNF_DISKDIR, CNF_INDEXDIR, CNF_TAPETYPE, CNF_DUMPCYCLE, CNF_TAPECYCLE,
    CNF_DISKSIZE, CNF_NETUSAGE, CNF_INPARALLEL, CNF_TIMEOUT,
    CNF_BUMPSIZE, CNF_BUMPMULT, CNF_BUMPDAYS, CNF_TPCHANGER, CNF_RUNTAPES,
    CNF_MAXDUMPS
} confparm_t;

typedef enum auth_e {
    AUTH_BSD, AUTH_KRB4
} auth_t;


typedef struct tapetype_s {
    struct tapetype_s *next;
    int seen;
    char *name;

    char *comment;
    unsigned long length;
    unsigned long filemark;
    long speed;
} tapetype_t;

typedef struct dumptype_s {
    struct dumptype_s *next;
    int seen;
    char *name;

    char *comment;
    char *program;
    char *exclude;
    long priority;
    long dumpcycle;
    long frequency;
    auth_t auth;
    int maxdumps;
    time_t start_t;
    /* flag options */
    int compress_best:1;
    int compress_fast:1;
    int srvcompress:1;
    int record:1;
    int skip_incr:1;
    int skip_full:1;
    int no_full:1;
    int no_hold:1;
    int kencrypt:1;
    int index:1;
} dumptype_t;

typedef struct holdingdisk_s {
    struct holdingdisk_s *next;
    char *diskdir;
    long disksize;
    void *up;			/* generic user pointer */
} holdingdisk_t;

extern holdingdisk_t *holdingdisks;
extern int num_holdingdisks;

int read_conffile P((char *filename));
int getconf_int P((confparm_t parameter));
double getconf_real P((confparm_t parameter));
char *getconf_str P((confparm_t parameter));
char *getconf_byname P((char *confname));
dumptype_t *lookup_dumptype P((char *identifier));
tapetype_t *lookup_tapetype P((char *identifier));

#endif /* ! CONFFILE_H */
