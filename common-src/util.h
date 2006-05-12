/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1999 University of Maryland at College Park
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
 * $Id: util.h,v 1.7 2006/05/12 22:42:48 martinea Exp $
 */
#ifndef UTIL_H
#define	UTIL_H

#include "amanda.h"

#define BSTRNCMP(a,b)  strncmp(a, b, strlen(b)) 
                                                  
						  
ssize_t fullread P((int, void *, size_t));
ssize_t fullwrite P((int, const void *, size_t));

int connect_portrange P((struct sockaddr_in *, int, int, char *, struct sockaddr_in *, int));
int bind_portrange P((int, struct sockaddr_in *, int, int, char *));

char *construct_datestamp P((time_t *t));
char *construct_timestamp P((time_t *t));

/*
   validate_email return 0 if the following characters are present
   * ( ) < > [ ] , ; : ! $ \ / "
   else returns 1
*/
int validate_mailto P((const char *mailto));

#endif	/* UTIL_H */
