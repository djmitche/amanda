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
 * $Id: tapelist.h,v 1.1 2005/10/11 01:17:00 vectro Exp $
 *
 */

#ifndef TAPELIST_H
#define TAPELIST_H

#include "amanda.h"

/* XXX This looks like a lot of other things, apart from the string
 * marshalling and unmarshalling.  Things like the EXTRACT_LIST in amrecover's
 * innards are functionally similar, so there's probably a lot of opportunity
 * to pare down extraneous code here by mushing things like that in.  Rainy
 * day project, perhaps.
 */

typedef struct tapelist_s {
    struct tapelist_s *next;
    char *label;
    int isafile; /* set to 1 and make *label the path to the file */
    int *files;
    int numfiles;
} tapelist_t;

extern int num_entries P((tapelist_t *tapelist));
extern tapelist_t *append_to_tapelist P((tapelist_t *tapelist, char *label,
					int file, int isafile));
extern char *marshal_tapelist P((tapelist_t *tapelist, int escape));
extern tapelist_t *unmarshal_tapelist_str P((char *tapelist_str));
extern char *escape_label P((char *label));
extern char *unescape_label P((char *label));
extern void free_tapelist P((tapelist_t *tapelist));

#endif /* !TAPELIST_H */
