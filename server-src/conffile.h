/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-2000 University of Maryland at College Park
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
 * $Id: conffile.h,v 1.62 2006/04/10 11:22:25 martinea Exp $
 *
 * interface for config file reading code
 */
#ifndef CONFFILE_H
#define CONFFILE_H

#include "amanda.h"
#include "sl.h"

#define CONFFILE_NAME "amanda.conf"

typedef enum conf_e {
    CNF_ORG,
    CNF_MAILTO,
    CNF_DUMPUSER,
    CNF_TAPEDEV,
    CNF_CHNGRDEV,
    CNF_CHNGRFILE,
    CNF_LABELSTR,
    CNF_TAPELIST,
    CNF_DISKFILE,
    CNF_INFOFILE,
    CNF_LOGDIR,
    CNF_DISKDIR,
    CNF_INDEXDIR,
    CNF_TAPETYPE,
    CNF_DUMPCYCLE,
    CNF_RUNSPERCYCLE,
    CNF_MAXCYCLE,
    CNF_TAPECYCLE,
    CNF_DISKSIZE,
    CNF_NETUSAGE,
    CNF_INPARALLEL,
    CNF_DUMPORDER,
    CNF_TIMEOUT,
    CNF_BUMPPERCENT,
    CNF_BUMPSIZE,
    CNF_BUMPMULT,
    CNF_BUMPDAYS,
    CNF_TPCHANGER,
    CNF_RUNTAPES,
    CNF_MAXDUMPS,
    CNF_ETIMEOUT,
    CNF_DTIMEOUT,
    CNF_CTIMEOUT,
    CNF_TAPEBUFS,
    CNF_RAWTAPEDEV,
    CNF_PRINTER,
    CNF_AUTOFLUSH,
    CNF_RESERVE,
    CNF_MAXDUMPSIZE,
    CNF_COLUMNSPEC,
    CNF_AMRECOVER_DO_FSF,
    CNF_AMRECOVER_CHECK_LABEL,
    CNF_AMRECOVER_CHANGER,
    CNF_TAPERALGO,
    CNF_DISPLAYUNIT,
    CNF_KRB5KEYTAB,
    CNF_KRB5PRINCIPAL,
    CNF_LABEL_NEW_TAPES,
    CNF_USETIMESTAMPS
} confparm_t;

typedef struct tapetype_s {
    struct tapetype_s *next;
    int seen;
    char *name;

    char *comment;
    char *lbl_templ;
    long blocksize;
    unsigned long length;
    unsigned long filemark;
    int speed;
    int file_pad;

    /* seen flags */
    int s_comment;
    int s_lbl_templ;
    int s_blocksize;
    int s_file_pad;
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

/* Estimate strategies */
#define ES_CLIENT	0	/* client estimate */
#define ES_SERVER	1	/* server estimate */
#define ES_CALCSIZE	2	/* calcsize estimate */

/* Compression types */
typedef enum {
    COMP_NONE,		/* No compression */
    COMP_FAST,		/* Fast compression on client */
    COMP_BEST,		/* Best compression on client */
    COMP_CUST,		/* Custom compression on client */
    COMP_SERV_FAST,	/* Fast compression on server */
    COMP_SERV_BEST,	/* Best compression on server */
    COMP_SERV_CUST	/* Custom compression on server */
} comp_t;

/* Encryption types */
typedef enum {
    ENCRYPT_NONE,		/* No encryption */
    ENCRYPT_CUST,		/* Custom encryption on client */
    ENCRYPT_SERV_CUST,	        /* Custom encryption on server */
} encrypt_t;

#define ALGO_FIRST	0
#define ALGO_FIRSTFIT	1
#define ALGO_LARGEST	2
#define ALGO_LARGESTFIT	3
#define ALGO_SMALLEST	4
#define ALGO_LAST	5

typedef struct dumptype_s {
    struct dumptype_s *next;
    int seen;
    char *name;

    char *comment;
    char *program;
    char *srvcompprog;
    char *clntcompprog;
    char *srv_encrypt;
    char *clnt_encrypt;
    char *amandad_path;
    char *client_username;
    sl_t *exclude_file;
    sl_t *exclude_list;
    sl_t *include_file;
    sl_t *include_list;
    int exclude_optional;
    int include_optional;
    int priority;
    int dumpcycle;
    int maxcycle;
    int frequency;
    char *security_driver;
    int maxdumps;
    int maxpromoteday;
    int bumppercent;
    int bumpsize;
    int bumpdays;
    double bumpmult;
    time_t start_t;
    int strategy;
    int estimate;
    comp_t compress;
    encrypt_t encrypt;
    char *srv_decrypt_opt;
    char *clnt_decrypt_opt;
    float comprate[2]; /* first is full, second is incremental */
    long tape_splitsize;
    char *split_diskbuffer;
    long fallback_splitsize;
    /* flag options */
    unsigned int record:1;
    unsigned int skip_incr:1;
    unsigned int skip_full:1;
    unsigned int no_hold:1;
    unsigned int kencrypt:1;
    unsigned int ignore:1;
    unsigned int index:1;

    /* seen flags */
    int s_comment;
    int s_program;
    int s_srvcompprog;
    int s_clntcompprog;
    int s_srv_encrypt;
    int s_clnt_encrypt;
    int s_amandad_path;
    int s_client_username;
    int s_exclude_file;
    int s_exclude_list;
    int s_include_file;
    int s_include_list;
    int s_exclude_optional;
    int s_include_optional;
    int s_priority;
    int s_dumpcycle;
    int s_maxcycle;
    int s_frequency;
    int s_security_driver;
    int s_maxdumps;
    int s_maxpromoteday;
    int s_bumppercent;
    int s_bumpsize;
    int s_bumpdays;
    int s_bumpmult;
    int s_start_t;
    int s_strategy;
    int s_estimate;
    int s_compress;
    int s_encrypt;
    int s_srv_decrypt_opt;
    int s_clnt_decrypt_opt;
    int s_comprate;
    int s_record;
    int s_skip_incr;
    int s_skip_full;
    int s_no_hold;
    int s_kencrypt;
    int s_ignore;
    int s_index;
    int s_tape_splitsize;
    int s_split_diskbuffer;
    int s_fallback_splitsize;
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

/* for each column we define some values on how to
 * format this column element
 */
typedef struct {
    char *Name;		/* column name */
    char PrefixSpace;	/* the blank space to print before this
   			 * column. It is used to get the space
			 * between the colums
			 */
    char Width;		/* the widht of the column itself */
    char Precision;	/* the precision if its a float */
    char MaxWidth;	/* if set, Width will be recalculated
    			 * to the space needed */
    char *Format;	/* the printf format string for this
   			 * column element
			 */
    char *Title;	/* the title to use for this column */
} ColumnInfo;

/* this corresponds to the normal output of amanda, but may
 * be adapted to any spacing as you like.
 */
extern ColumnInfo ColumnData[];

extern char *config_name;
extern char *config_dir;

extern holdingdisk_t *holdingdisks;
extern int num_holdingdisks;

int read_conffile P((char *filename));
int getconf_seen P((confparm_t parameter));
int getconf_int P((confparm_t parameter));
am64_t getconf_am64 P((confparm_t parameter));
double getconf_real P((confparm_t parameter));
char *getconf_str P((confparm_t parameter));
char *getconf_byname P((char *confname));
dumptype_t *lookup_dumptype P((char *identifier));
dumptype_t *read_dumptype P((char *name, FILE *from, char *fname, int *linenum));
tapetype_t *lookup_tapetype P((char *identifier));
interface_t *lookup_interface P((char *identifier));
holdingdisk_t *getconf_holdingdisks P((void));
long int getconf_unit_divisor P((void));

int ColumnDataCount P((void));
int StringToColumn P((char *s));
char LastChar P((char *s));
int SetColumDataFromString P((ColumnInfo* ci, char *s, char **errstr));

char *taperalgo2str P((int taperalgo));

/* this is in securityconf.h */
char *generic_get_security_conf P((char *, void *));
#endif /* ! CONFFILE_H */
