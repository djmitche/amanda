#include "amanda.h"

char *x_buffer;
int free_x_buffer;
int size_x_buffer;
char **exclude;
int size_exclude;
int free_exclude;
char **re_exclude;
int size_re_exclude;
int free_re_exclude;

#ifndef FNM_LEADING_DIR
# include "fnmatch.h"
#endif

void
add_exclude_file (file)
char *file;
{
  FILE *fp;
  static char buf[1024];

  fp = fopen (file, "r");

  while (fgets (buf, 1024, fp))
    {
      char *end_str;

      end_str = strrchr (buf, '\n');
      if (end_str)
	*end_str = '\0';
      add_exclude (buf);

    }
  fclose(fp);
/*
  if (fclose (fp) == EOF)
    ERROR ((0, errno, "%s", file));
*/
}

/*------------------------------------------------------------------.
| Returns true if the file NAME should not be added nor extracted.  |
`------------------------------------------------------------------*/

int
check_exclude (name)
const char *name;
{
  int n;
  char *str;

/*
fprintf(stderr,"check_exclude(%s)\n",name);
*/

  for (n = 0; n < size_re_exclude; n++)
    {
      if (fnmatch (re_exclude[n], name, FNM_LEADING_DIR) == 0)
	return 1;
    }
  for (n = 0; n < size_exclude; n++)
    {

      /* Accept the output from strstr only if it is the last part of the
	 string.  There is certainly a faster way to do this.  */

      if (str = strstr (name, exclude[n]),
	  (str && (str == name || str[-1] == '/')
	   && str[strlen (exclude[n])] == '\0'))
	return 1;
    }
/*
fprintf(stderr,"FAILED\n");
*/
  return 0;
}
