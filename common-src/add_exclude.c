#include "amanda.h"

extern char *x_buffer;
extern int free_x_buffer;
extern int size_x_buffer;
extern char **exclude;
extern int size_exclude;
extern int free_exclude;
extern char **re_exclude;
extern int size_re_exclude;
extern int free_re_exclude;

static int
is_regex (str)
const char *str;
{
  return strchr (str, '*') || strchr (str, '[') || strchr (str, '?');
}

int
unquote_string (string)
char *string;
{
  int result;
  char *source;
  char *dest;
  int value;

  result = 1;
  dest = string;
  source = string;

  while (*source)
    if (*source == '\\')
      switch (*++source)
	{
	case '\\':
	  *dest++ = '\\';
	  source++;
	  break;

	case 'n':
	  *dest++ = '\n';
	  source++;
	  break;

	case 't':
	  *dest++ = '\t';
	  source++;
	  break;

	case 'f':
	  *dest++ = '\f';
	  source++;
	  break;

	case 'b':
	  *dest++ = '\b';
	  source++;
	  break;

	case 'r':
	  *dest++ = '\r';
	  source++;
	  break;

	case '?':
	  *dest++ = 0177;
	  source++;
	  break;

	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	  value = *source - '0';
	  source++;
	  if (*source < '0' || *source > '7')
	    {
	      *dest++ = value;
	      break;
	    }
	  value = value * 8 + *source - '0';
	  source++;
	  if (*source < '0' || *source > '7')
	    {
	      *dest++ = value;
	      break;
	    }
	  value = value * 8 + *source - '0';
	  source++;
	  *dest++ = value;
	  break;

	default:
	  result = 0;
	  *dest++ = '\\';
	  if (*source)
	    *dest++ = *source++;
	  break;
	}
    else if (source != dest)
      *dest++ = *source++;
    else
      source++, dest++;

  if (source != dest)
    *dest = '\0';
  return result;
}

void add_exclude (name)
char *name;
{
  int size_buf;

  unquote_string (name);
  size_buf = strlen (name);

  if (x_buffer == 0)
    {
      x_buffer = (char *) malloc ((size_t) (size_buf + 1024));
      free_x_buffer = 1024;
    }
  else if (free_x_buffer <= size_buf)
    {
      char *old_x_buffer;
      char **tmp_ptr;

      old_x_buffer = x_buffer;
      x_buffer = (char *) realloc (x_buffer, (size_t) (size_x_buffer + 1024));
      free_x_buffer = 1024;
      for (tmp_ptr = exclude; tmp_ptr < exclude + size_exclude; tmp_ptr++)
	*tmp_ptr = x_buffer + ((*tmp_ptr) - old_x_buffer);
      for (tmp_ptr = re_exclude;
	   tmp_ptr < re_exclude + size_re_exclude;
	   tmp_ptr++)
	*tmp_ptr = x_buffer + ((*tmp_ptr) - old_x_buffer);
    }

  if (is_regex (name))
    {
      if (re_exclude == 0) {
	    re_exclude = (char**) malloc((size_exclude + 32) * sizeof (char *));
	    free_re_exclude = 32;
	    size_re_exclude = 0;
      }
      if (free_re_exclude == 0)
	{
	  re_exclude = (char **)
	    realloc (re_exclude, (size_re_exclude + 32) * sizeof (char *));
	  free_re_exclude += 32;
	}
      re_exclude[size_re_exclude] = x_buffer + size_x_buffer;
      size_re_exclude++;
      free_re_exclude--;
    }
  else
    {
      if (exclude == 0) {
	    exclude = (char**) malloc((size_exclude + 32) * sizeof (char *));
	    free_exclude = 32;
	    size_exclude = 0;
      }
      if (free_exclude == 0)
	{
	  exclude = (char **)
	    realloc (exclude, (size_exclude + 32) * sizeof (char *));
	  free_exclude += 32;
	}
      exclude[size_exclude] = x_buffer + size_x_buffer;
      size_exclude++;
      free_exclude--;
    }
  strcpy (x_buffer + size_x_buffer, name);
  size_x_buffer += size_buf + 1;
  free_x_buffer -= size_buf + 1;
}
