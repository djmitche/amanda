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
 * Authors: the Amanda Development Team.  Its members are listed in a
 * file named AUTHORS, in the root directory of this distribution.
 */
/*
 * $Id: holding.h,v 1.21 2003/06/03 20:14:29 martinea Exp $
 *
 */

#ifndef HOLDING_H
#define HOLDING_H

#include "amanda.h"
#include "diskfile.h"
#include "fileheader.h"
#include "sl.h"

/* local functions */
int is_dir P((char *fname));
int is_emptyfile P((char *fname));
int is_datestr P((char *fname));
int non_empty P((char *fname));
void free_holding_list P(( sl_t *holding_list));
sl_t *get_flush(sl_t *dateargs, char *datestamp, int amflush, int verbose);
sl_t *pick_datestamp P((int verbose));
sl_t *pick_all_datestamp P((int verbose));
filetype_t get_amanda_names P((char *fname,
			       char **hostname,
			       char **diskname,
			       int *level));
void get_dumpfile P((char *fname, dumpfile_t *file));
long size_holding_files P((char *holding_file));
int unlink_holding_files P((char *holding_file));
int rename_tmp_holding P((char *holding_file, int complete));
void cleanup_holdingdisk P((char *diskdir, int verbose));
int mkholdingdir P((char *diskdir));


#endif /* HOLDING_H */
