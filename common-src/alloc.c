/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1997 University of Maryland
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
 * Author: AMANDA core development group.
 */
/*
 * $Id: alloc.c,v 1.11 1998/01/14 23:48:46 george Exp $
 *
 * Memory allocators with error handling.  If the allocation fails,
 * error() is called, relieving the caller from checking the return
 * code
 */
#include "amanda.h"
#include "arglist.h"


/*
** alloc - a wrapper for malloc.
*/
void *alloc(size)
int size;
{
    void *addr;

    addr = (void *)malloc(size>0 ? size : 1);
    if(addr == NULL)
	error("memory allocation failed");
    return addr;
}


/*
** newalloc - free existing buffer and then alloc a new one.
*/
void *newalloc(old, size)
void *old;
int size;
{
    afree(old);
    return alloc(size);
}


/*
** stralloc - copies the given string into newly allocated memory.
**            Just like strdup()!
*/
char *stralloc(str)
char *str;
{
    char *addr;

    addr = alloc(strlen(str)+1);
    strcpy(addr, str);
    return addr;
}


/*
** vstralloc - copies up to MAX_STR_ARGS strings into newly allocated memory.
**
** The MAX_STR_ARGS limit is purely an efficiency issue so we do not have
** to scan the strings more than necessary.
*/

#define	MAX_VSTRALLOC_ARGS	32

static char *internal_vstralloc(str, argp)
char *str;
va_list argp;
{
    char *next;
    char *result;
    int a;
    int total_len;
    char *arg[MAX_VSTRALLOC_ARGS+1];
    int len[MAX_VSTRALLOC_ARGS+1];
    int l;
    char *s;

    if (str == NULL) {
	return NULL;				/* probably will not happen */
    }

    a = 0;
    arg[a] = str;
    l = strlen(str);
    total_len = len[a] = l;
    a++;

    while ((next = arglist_val(argp, char *)) != NULL) {
	if ((l = strlen(next)) == 0) {
	    continue;				/* minor optimisation */
	}
	if (a >= MAX_VSTRALLOC_ARGS) {
	    error ("more than %d args to vstralloc", MAX_VSTRALLOC_ARGS);
	}
	arg[a] = next;
	len[a] = l;
	total_len += l;
	a++;
    }
    arg[a] = NULL;
    len[a] = 0;

    next = result = alloc(total_len+1);
    for (a = 0; (s = arg[a]) != NULL; a++) {
	memcpy(next, s, len[a]);
	next += len[a];
    }
    *next = '\0';

    return result;
}


/*
** vstralloc - copies multiple strings into newly allocated memory.
*/
arglist_function(char *vstralloc, char *, str)
{
    va_list argp;
    char *result;

    arglist_start(argp, str);
    result = internal_vstralloc(str, argp);
    arglist_end(argp);
    return result;
}


/*
** newstralloc - free existing string and then stralloc a new one.
*/
char *newstralloc(oldstr, newstr)
char *oldstr;
char *newstr;
{
    afree(oldstr);
    return stralloc(newstr);
}


/*
** newvstralloc - free existing string and then vstralloc a new one.
*/
arglist_function1(char *newvstralloc, char *, oldstr, char *, newstr)
{
    va_list argp;
    char *result;

    afree(oldstr);
    arglist_start(argp, newstr);
    result = internal_vstralloc(newstr, argp);
    arglist_end(argp);
    return result;
}


/*
** sbuf_man - static buffer manager.
**
** Manage a bunch of static buffer pointers.
*/
void *sbuf_man(e_bufs, ptr)
void *e_bufs; /* XXX - I dont think this is right */
void *ptr;
{
	SBUF2_DEF(1) *bufs;
	int slot;

	bufs = e_bufs;

	/* try and trap bugs */
	assert(bufs->magic == SBUF_MAGIC);
	assert(bufs->max > 0);

	/* initialise first time through */
	if(bufs->cur == -1)
		for(slot=0; slot < bufs->max; slot++) {
			bufs->bufp[slot] = (void *)0;
		} 

	/* calculate the next slot */
	slot = bufs->cur + 1;
	if (slot >= bufs->max) slot = 0;

	/* free the previous inhabitant */
	if(bufs->bufp[slot] != (void *)0) free(bufs->bufp[slot]);

	/* store the new one */
	bufs->bufp[slot] = ptr;
	bufs->cur = slot;

	return ptr;
}


/*
** safe_env - build a "safe" environment list.
*/
char **safe_env()
{
    static char *safe_env_list[] = {
	"TZ",
	NULL
    };

    /*
     * If the initial environment pointer malloc fails, set up to
     * pass back a pointer to the NULL string pointer at the end of
     * safe_env_list so our result is always a valid, although possibly
     * empty, environment list.
     */
#define SAFE_ENV_CNT	(sizeof(safe_env_list) / sizeof(*safe_env_list))
    char **envp = safe_env_list + SAFE_ENV_CNT - 1;

    char **p;
    char **q;
    char *s;
    char *v;
    int l1, l2;

    if ((q = (char **)malloc(sizeof(safe_env_list))) != NULL) {
	envp = q;
	for (p = safe_env_list; *p != NULL; p++) {
	    if ((v = getenv(*p)) == NULL) {
		continue;			/* no variable to dup */
	    }
	    l1 = strlen(*p);			/* variable name w/o null */
	    l2 = strlen(v) + 1;			/* include null byte here */
	    if ((s = (char *)malloc(l1 + 1 + l2)) == NULL) {
		break;				/* out of memory */
	    }
	    *q++ = s;				/* save the new pointer */
	    memcpy(s, *p, l1);			/* left hand side */
	    s += l1;
	    *s++ = '=';
	    memcpy(s, v, l2);			/* right hand side and null */
	}
	*q = NULL;				/* terminate the list */
    }
    return envp;
}
