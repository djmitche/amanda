/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1996 University of Maryland at College Park
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
 * $Id: findpass.c,v 1.4 1997/08/27 08:11:30 amcore Exp $
 *
 * Support routines for Amanda SAMBA support
 */

#include "findpass.h"

char *findpass(char *disk, char *pass, char *domain)
{
  FILE *fp;
  static char buffer[256];
  char *ptr, *ret=0;
  
  if ( (fp = fopen("/etc/amandapass", "r")) ) {
    while (fgets(buffer, sizeof(buffer)-1, fp)) {
      ptr = buffer;
      while (*ptr && isspace(*ptr))
	ptr++;
      if (!*ptr || *ptr == '#')
	continue;
      while (*ptr && !isspace(*ptr) && *ptr != '#')
	ptr++;
      if (*ptr) {
	*ptr++=0;
	if (!strcmp(disk, buffer)) {
	  while (*ptr && isspace(*ptr) && *ptr != '#')
	    ptr++;
	  if (*ptr && *ptr != '#') {
	    ret=pass;
	    while (*ptr && !isspace(*ptr) && *ptr != '#')
	      *pass++=*ptr++;
	    *pass=0;
	    while(*ptr && isspace(*ptr) && *ptr != '#')
	      ptr++;
	    if (*ptr && *ptr != '#') {
	      while (*ptr && !isspace(*ptr) && *ptr != '#')
		*domain++=*ptr++;
	    }
	    *domain = 0;
	  }
	  break;
	}
      }
    }
    fclose(fp);
  }
  return ret;
}

/* 
 * Expand an amanda disk-name into a samba sharename,
 * optionally for a shell execution (\'s are escaped).
 */
char *makesharename(char *disk, char *buffer, int shell)
{
  int c;
  char *ret = buffer;

  do {
    c=*disk++;
    if (c=='/') {
      c='\\';
      if (shell)
	*buffer++=c;
    }
  } while ((*buffer++ = c));

  return ret;
}
