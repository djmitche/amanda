/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991 University of Maryland
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
 * $Id: amindex.c,v 1.5 1997/12/16 18:02:14 jrj Exp $
 *
 * index control
 */

#include "amindex.h"

char *getindexfname(host, disk, date, level)
char *host, *disk, *date;
int level;
{
  static char buf[1024];
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
  datebuf[sizeof (datebuf) - 1] = '\0';
  sprintf(buf, "%s_%s_%s_%d%s", host, disk, datebuf, level, COMPRESS_SUFFIX);

  for (pc = buf; *pc != '\0'; pc++)
    if ((*pc == '/') || (*pc == ' '))
      *pc = '_';

  return buf;
}

char *getindexname(dir, host, disk, date, level, len)
char *dir, *host, *disk, *date;
int level;
int *len;
{
  static char name[1024];

  *len = sizeof(name);
  sprintf(name, "%s/%s", dir, getindexfname(host, disk, date, level));

  return name;
}

