/*
 *
 * Support routines for Amanda SAMBA support.
 */

#include "findpass.h"

char *findpass(char *disk, char *pass)
{
  FILE *fp;
  static char buffer[256];
  char *ptr, *ret=0;
  
  if ( (fp = fopen("/etc/amandapass", "r")) ) {
    while (fgets(buffer, sizeof(buffer)-1, fp)) {
      ptr = buffer;
      while (*ptr && !isspace(*ptr))
	ptr++;
      if (*ptr) {
	*ptr++=0;
	if (!strcmp(disk, buffer)) {
	  while (*ptr && isspace(*ptr))
	    ptr++;
	  if (*ptr) {
	    ret=pass;
	    while (*ptr && !isspace(*ptr))
	      *pass++=*ptr++;
	    *pass=0;
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
