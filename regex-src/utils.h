/* utility definitions */
#ifdef _POSIX2_RE_DUP_MAX
#define	DUPMAX	_POSIX2_RE_DUP_MAX
#else
#define	DUPMAX	255
#endif
#define	INFINITY	(DUPMAX + 1)
#define	NC		(CHAR_MAX - CHAR_MIN + 1)
typedef unsigned char uch;

/* switch off assertions (if not already off) if no REDEBUG */
#ifndef REDEBUG
#ifndef NDEBUG
#define	NDEBUG	/* no assertions please */
#endif
#endif
#include <assert.h>

/* for old systems with bcopy() but no memmove() */
#ifdef USEBCOPY
#define	memmove(d, s, c)	bcopy(s, d, c)
#endif

#include "config.h"
 
/* String concatenation.  */
#undef Concatenate
#ifdef HAVE_ANSI_CONCATENATE
#define Concatenate(a,b)a##b
#else
#define Concatenate(a,b)a/**/b
#endif

#ifdef HAVE_UNSIGNED_LONG_CONSTANTS
#define AM_UNSIGNED_LONG(num) (Concatenate(num,ul))
#else
#define AM_UNSIGNED_LONG(num) ((unsigned long) Concatenate(num,l))
#endif
