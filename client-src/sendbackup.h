/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1994 University of Maryland
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
 * $Id: sendbackup.h,v 1.5 1997/08/27 08:11:42 amcore Exp $
 *
 * a few common decls for the sendbackup-* sources
 */
#include "amanda.h"

void write_tapeheader P((void));
int pipespawn P((char *prog, int *stdinfd, int stdoutfd, int stderrfd, ...));
void start_index P((int createindex, int input, int mesg, \
		    int index, char *cmd));

/*
 * Dump output lines are scanned for two types of regex matches.
 *
 * First, there are some cases, unfortunately, where dump detects an
 * error but does not return an error code.  We would like to bring these
 * errors to the attention of the operators anyway.  
 *
 * Second, we attempt to determine what dump thinks its output size is.
 * This is cheaper than putting a filter between dump and compress just
 * to determine the output size.  The re_size table contains regexes to
 * match the size output by various vendors' dump programs.  Some vendors
 * output the number in Kbytes, some in 512-byte blocks.  Whenever an
 * entry in re_size matches, the first integer in the dump line is
 * multiplied by the scale field to get the dump size.
 */

typedef enum { 
    DMP_NORMAL, DMP_STRANGE, DMP_SIZE, DMP_ERROR
} dmpline_t;

typedef struct regex_s {
    dmpline_t typ;
    char *regex;
    int scale;                  /* only used for size lines */
} regex_t;

extern regex_t re_table[];

extern int compress, comppid, dumppid, no_record;
extern int indexpid, createindex;

typedef struct backup_program_s {
    char *name, *backup_name, *restore_name;
    regex_t *re_table;
    void (*start_backup) P((char *disk, int level, char *dumpdate, \
			    int dataf, int mesgf, int indexf));
    void (*end_backup) P((int goterror));
} backup_program_t;

extern backup_program_t *programs[], *program;

extern char *pname;

#define COMPR_FAST 1
#define COMPR_BEST 2
