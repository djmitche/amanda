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
 * $Id: amregex.h,v 1.5 1997/11/07 20:43:22 amcore Exp $
 *
 * compatibility header file for Henry Spencer's regex library.
 */
#ifndef AMREGEX_H
#define AMREGEX_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#include <limits.h>

#ifndef HAVE__POSIX2_RE_DUP_MAX
#define _POSIX2_RE_DUP_MAX 255
#endif

#ifndef HAVE_CHAR_MIN
#define CHAR_MIN (-128)
#endif

#ifndef HAVE_CHAR_MAX
#define CHAR_MAX 127
#endif

#ifndef HAVE_CHAR_BIT
#define CHAR_BIT 8
#endif

#if STDC_HEADERS
#  define P(parms)	parms
#else
#  define P(parms)	()
#endif

#ifndef HAVE_BCOPY_DECL
extern void bcopy P((const void *from, void *to, size_t n));
#endif

#ifndef HAVE_MEMMOVE_DECL
extern char *memmove P((char *to, char *from, size_t n));
#endif

#if !defined(HAVE_MEMMOVE) && defined(HAVE_BCOPY)
#define USEBCOPY
#endif

#define POSIX_MISTAKE

#ifdef HAVE_UNSIGNED_LONG_CONSTANTS
#undef NO_UL_CNSTS
#else
#define NO_UL_CNSTS
#endif

#endif
