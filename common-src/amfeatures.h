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
 * $Id: amfeatures.h,v 1.2 2002/04/17 20:06:10 martinea Exp $
 *
 * Define feature test related items.
 */

#ifndef AMFEATURES_H
#define AMFEATURES_H

/*
 * !!!WARNING!!!    !!!WARNING!!!    !!!WARNING!!!    !!!WARNING!!!
 *
 * No matter **WHAT**, you **MUST** enter new features at the **END**
 * of this list (just before "last_feature").  If you do not, mass
 * confusion will ensue.
 *
 * And features must **NEVER** be removed (that is, their code number
 * must remain).  The bits are cheap.
 *
 * If you add a feature here, you probably also need to add a line to
 * am_init_feature_set() in features.c unless it is dynamic in some way.
 *
 * !!!WARNING!!!    !!!WARNING!!!    !!!WARNING!!!    !!!WARNING!!!
 */
typedef enum {
    /*
     * This bit will be set if the feature test code is supported.  It
     * will only be off for "old" (2.4.2p2 and earlier) systems.
     */
    have_feature_support = 0,

    /*
     * Amanda used to send authorization type information around like
     * this in the OPTIONS string:
     *
     *	bsd-auth
     *	krb4-auth
     *
     * To make it easier to add new authorization methods and parse,
     * this was changed to a keyword=value syntax:
     *
     *	auth=BSD
     *	auth=RSH
     *	auth=KRB4
     *	auth=krb5
     *
     * and so on.
     */

    amanda_feature_auth_keyword,

    /*
     * All new features must be inserted immediately *before* this entry.
     */
    last_feature
} am_feature_e;

typedef struct am_feature_s {
    size_t		size;
    unsigned char	*bytes;
} am_feature_t;

/*
 * Functions.
 */
extern am_feature_t *am_init_feature_set P((void));
extern am_feature_t *am_allocate_feature_set P((void));
extern void am_release_feature_set P((am_feature_t *));
extern int am_add_feature P((am_feature_t *f, am_feature_e n));
extern int am_remove_feature P((am_feature_t *f, am_feature_e n));
extern int am_has_feature P((am_feature_t *f, am_feature_e n));
extern char *am_feature_to_string P((am_feature_t *f));
extern am_feature_t *am_string_to_feature P((char *s));

#endif	/* !AMFEATURES_H */
