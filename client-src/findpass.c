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
 * $Id: findpass.c,v 1.7 1998/01/05 10:28:09 amcore Exp $
 *
 * Support routines for Amanda SAMBA support
 */

#include "findpass.h"

/*
 * Find the Samba password and optional domain for a given disk.
 * Returns pointers into an alloc-ed area.  The caller should clear them
 * as soon as reasonable.
 */

char *findpass(disk, domain)
char *disk, **domain;
{
  FILE *fp;
  static char *buffer = NULL;
  char *s, *d, *pw = NULL;
  int ch;

  *domain = NULL;				/* just to be sure */
  if ( (fp = fopen("/etc/amandapass", "r")) ) {
    afree(buffer);
    for (; (buffer = agets(fp)) != NULL; free(buffer)) {
      s = buffer;
      ch = *s++;
      skip_whitespace(s, ch);			/* find start of disk name */
      if (!ch || ch == '#') {
	continue;
      }
      d = s-1;					/* start of disk name */
      skip_non_whitespace_cs(s, ch);
      if (ch && ch != '#') {
	s[-1] = '\0';				/* terminate disk name */
	if (strcmp(disk, d) == 0) {
	  skip_whitespace(s, ch);		/* find start of password */
	  if (ch && ch != '#') {
	    pw = s - 1;				/* start of password */
	    skip_non_whitespace_cs(s, ch);
	    s[-1] = '\0';			/* terminate password */
	    skip_whitespace(s, ch);		/* find start of domain */
	    if (ch && ch != '#') {
	      *domain = s - 1;			/* start of domain */
	      skip_non_whitespace_cs(s, ch);
	      s[-1] = '\0';			/* terminate domain */
	    }
	  }
	  break;
	}
      }
    }
    afclose(fp);
  }
  return pw;
}

/* 
 * Convert an amanda disk-name into a Samba sharename,
 * optionally for a shell execution (\'s are escaped).
 * Returns a new name alloc-d that the caller is responsible
 * for free-ing.
 */
char *makesharename(disk, shell)
char *disk;
int shell;
{
  char *buffer;
  int buffer_size;
  char *s;
  int ch;
  
  buffer_size = 2 * strlen(disk) + 1;		/* worst case */
  buffer = alloc(buffer_size);

  s = buffer;
  while ((ch = *disk++) != '\0') {
    if (s >= buffer+buffer_size-2) {		/* room for escape */
      afree(buffer);				/* should never happen */
      return NULL;				/* buffer not big enough */
    }
    if (ch == '/') {
      ch = '\\';				/* convert '/' to '\\' */
    }
    if (ch == '\\' && shell) {
      *s++ = '\\';				/* add escape for shell */
    }
    *s++ = ch;
  }
  *s = '\0';					/* terminate the share name */
  return buffer;
}
