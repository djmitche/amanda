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
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/*
 * $Id: amindex.c,v 1.11 1999/04/10 06:19:36 kashmir Exp $
 *
 * index control
 */

#include "amindex.h"

char *getindexfname(host, disk, date, level)
char *host, *disk, *date;
int level;
{
  static char *buf = NULL;
  char level_str[NUM_STR_SIZE];
  char datebuf[8 + 1];
  char *dc;
  char *pc;
  int ch;

  dc = date;
  pc = datebuf;
  while (pc < datebuf + sizeof (datebuf))
  {
    if ((*pc++ = ch = *dc++) == '\0')
    {
      break;
    }
    else if (! isdigit (ch))
    {
      pc--;
    }
  }
  datebuf[sizeof(datebuf)-1] = '\0';

  snprintf(level_str, sizeof(level_str), "%d", level);

  host = stralloc(sanitise_filename(host));
  disk = stralloc(sanitise_filename(disk));

  buf = newvstralloc(buf,
		     host, "/",
		     disk, "/",
		     datebuf, "_",
		     level_str, COMPRESS_SUFFIX,
		     NULL);

  amfree(host); amfree(disk);

  return buf;
}
