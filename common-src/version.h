/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1993 University of Maryland
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
 * version.h - the current Amanda version.
 *	The printed version string is <major>.<minor>[.<patch>[comment]]
 *      - Changes in comments imply a non-standard version of Amanda.
 *	- Changes in patchlevel imply mostly bugfixes.
 *	- Changes in minor version number imply significant code or protocol
 *	  changes or enhancements.
 *	- Changes in major version number imply major reworking or redesign.
 */

#ifndef VERSION_H
#define VERSION_H

#include "config.h"

extern const int   VERSION_MAJOR;
extern const int   VERSION_MINOR;
extern const int   VERSION_PATCH;
extern const char *VERSION_COMMENT;

/* versionsuffix returns an empty string or a string like -2.3.0.4b1.  */
extern char *versionsuffix();

/* version returns a string representing the version of Amanda.  */
extern char *version();

#endif
