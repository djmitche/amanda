/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1997 University of Maryland
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
 * Author: George Scott, Computer Centre, Monash University
 */
/*
 * $Id: token.h,v 1.5 1997/08/27 08:12:24 amcore Exp $
 *
 * interface to token module
 */
#ifndef TOKEN_H
#define TOKEN_H

typedef struct {char *word; int value;} table_t;

extern int split P((char *str, char **token, int toklen, char *sep));
extern char *squotef P((char *format, ...))
#ifdef __GNUC__
     __attribute__ ((format (printf, 1, 2)))
#endif
     ;
extern char *squote P((char *str));
extern char *quotef P((char *sep, char *format, ...));
extern char *quote P((char *sep, char *str));
extern int table_lookup P((table_t *table, char *str));
extern char *table_lookup_r P((table_t *table, int val));

#endif
