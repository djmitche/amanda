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
 * Authors: the Amanda Development Team.  Its members are listed in a
 * file named AUTHORS, in the root directory of this distribution.
 */
/*
 * $Id: alloc.c,v 1.17.2.1.2.2 2001/07/19 22:53:50 jrjackson Exp $
 *
 * Memory allocators with error handling.  If the allocation fails,
 * errordump() is called, relieving the caller from checking the return
 * code
 */
#include "amanda.h"
#include "arglist.h"

#if defined(USE_DBMALLOC)

/*
 *=====================================================================
 * dbmalloc_caller_loc -- keep track of all allocation callers
 *
 * char *dbmalloc_caller_loc(char *s, int l)
 *
 * entry:	s = source file
 *		l = source line
 * exit:	a string like "genversion.c@999"
 *
 * The debug malloc library has a concept of a call stack that can be used
 * to fine tune what was running when a particular allocation was done.
 * We use it to tell who called our various allocation wrappers since
 * it wouldn't do much good to tell us a problem happened because of
 * the malloc call in alloc (they are all from there at some point).
 *
 * But the library expects the string passed to malloc_enter/malloc_leave
 * to be static, so we build a linked list of each one we get (there are
 * not really that many during a given execution).  When we get a repeat
 * we return the previously allocated string.  For a bit of performance,
 * we keep the list in least recently used order, which helps because
 * the calls to us come in pairs (one for malloc_enter and one right
 * after for malloc_leave).
 *=====================================================================
 */

char *
dbmalloc_caller_loc(s, l)
char *s;
int l;
{
    struct loc_str {
	char *str;
	struct loc_str *next;
    };
    static struct loc_str *root = NULL;
    struct loc_str *ls, *ls_last;
    int len;
    int size;
    char *p;
    static char *loc = NULL;
    static int loc_size = 0;

    if ((p = strrchr(s, '/')) == NULL) {
	p = s;					/* keep the whole name */
    } else {
	p++;					/* just the last path element */
    }

    len = strlen (p);
    size = len + 1 + NUM_STR_SIZE + 1;
    if (size > loc_size) {
	size = ((size + 64 - 1) / 64) * 64;	/* might as well get a bunch */
	/*
	 * We should free the previous loc area, but we have marked it
	 * as a non-leak and the library considers it an error to free
	 * such an area, so we just ignore it.  We probably grabbed
	 * enough the first time that this will not even happen.
	 */
	loc = malloc (size);
	if (loc == NULL) {
	    return "??";			/* not much better than abort */
	}
	malloc_mark (loc);
	loc_size = size;
    }

    strcpy (loc, p);
    ap_snprintf(loc + len, 1 + NUM_STR_SIZE, "@%d", l);

    for (ls_last = NULL, ls = root; ls != NULL; ls_last = ls, ls = ls->next) {
	if (strcmp (loc, ls->str) == 0) {
	    break;
	}
    }

    if (ls == NULL) {
	/*
	 * This is a new entry.  Put it at the head of the list.
	 */
	ls = malloc (sizeof (*ls));
	if (ls == NULL) {
	    return "??";			/* not much better than abort */
	}
	malloc_mark (ls);
	size = strlen (loc) + 1;
	ls->str = malloc (size);
	if (ls->str == NULL) {
	    free (ls);
	    return "??";			/* not much better than abort */
	}
	malloc_mark (ls->str);
	strcpy (ls->str, loc);
	ls->next = root;
	root = ls;
    } else if (ls_last != NULL) {
	/*
	 * This is a repeat and was not at the head of the list.
	 * Unlink it and move it to the front.
	 */
	ls_last->next = ls->next;
	ls->next = root;
	root = ls;
    } else {
	/*
	 * This is a repeat but was already at the head of the list,
	 * so nothing else needs to be done.
	 */
    }
    return ls->str;
}

/*
 *=====================================================================
 * Save the current source line for vstralloc/newvstralloc.
 *
 * int debug_alloc_push (char *s, int l)
 *
 * entry:	s = source file
 *		l = source line
 * exit:	always zero
 * 
 * See the comments in amanda.h about what this is used for.
 *=====================================================================
 */

#define	DEBUG_ALLOC_SAVE_MAX	10

static struct {
	char		*file;
	int		line;
} debug_alloc_loc_info[DEBUG_ALLOC_SAVE_MAX];
static int debug_alloc_ptr = 0;

static char		*saved_file;
static int		saved_line;

int
debug_alloc_push (s, l)
char *s;
int l;
{
    debug_alloc_loc_info[debug_alloc_ptr].file = s;
    debug_alloc_loc_info[debug_alloc_ptr].line = l;
    debug_alloc_ptr = (debug_alloc_ptr + 1) % DEBUG_ALLOC_SAVE_MAX;
    return 0;
}

/*
 *=====================================================================
 * Pop the current source line information for vstralloc/newvstralloc.
 *
 * int debug_alloc_pop (void)
 *
 * entry:	none
 * exit:	none
 * 
 * See the comments in amanda.h about what this is used for.
 *=====================================================================
 */

void
debug_alloc_pop ()
{
    debug_alloc_ptr =
      (debug_alloc_ptr + DEBUG_ALLOC_SAVE_MAX - 1) % DEBUG_ALLOC_SAVE_MAX;
    saved_file = debug_alloc_loc_info[debug_alloc_ptr].file;
    saved_line = debug_alloc_loc_info[debug_alloc_ptr].line;
}

#endif

/*
** alloc - a wrapper for malloc.
*/
#if defined(USE_DBMALLOC)
void *debug_alloc(s, l, size)
char *s;
int l;
#else
void *alloc(size)
#endif
int size;
{
    void *addr;

    malloc_enter(dbmalloc_caller_loc(s, l));
    addr = (void *)malloc(size>0 ? size : 1);
    if(addr == NULL) {
	errordump("memory allocation failed");
    }
    malloc_leave(dbmalloc_caller_loc(s, l));
    return addr;
}


/*
** newalloc - free existing buffer and then alloc a new one.
*/
#if defined(USE_DBMALLOC)
void *debug_newalloc(s, l, old, size)
char *s;
int l;
#else
void *newalloc(old, size)
#endif
void *old;
int size;
{
    char *addr;

    malloc_enter(dbmalloc_caller_loc(s, l));
    addr = alloc(size);
    amfree(old);
    malloc_leave(dbmalloc_caller_loc(s, l));
    return addr;
}


/*
** stralloc - copies the given string into newly allocated memory.
**            Just like strdup()!
*/
#if defined(USE_DBMALLOC)
char *debug_stralloc(s, l, str)
char *s;
int l;
#else
char *stralloc(str)
#endif
const char *str;
{
    char *addr;

    malloc_enter(dbmalloc_caller_loc(s, l));
    addr = alloc(strlen(str)+1);
    strcpy(addr, str);
    malloc_leave(dbmalloc_caller_loc(s, l));
    return addr;
}


/*
** internal_vstralloc - copies up to MAX_STR_ARGS strings into newly
** allocated memory.
**
** The MAX_STR_ARGS limit is purely an efficiency issue so we do not have
** to scan the strings more than necessary.
*/

#define	MAX_VSTRALLOC_ARGS	32

static char *internal_vstralloc(str, argp)
const char *str;
va_list argp;
{
    char *next;
    char *result;
    int a;
    int total_len;
    const char *arg[MAX_VSTRALLOC_ARGS+1];
    int len[MAX_VSTRALLOC_ARGS+1];
    int l;
    const char *s;

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
	    errordump("more than %d arg%s to vstralloc",
		      MAX_VSTRALLOC_ARGS, (MAX_VSTRALLOC_ARGS == 1) ? "" : "s");
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
#if defined(USE_DBMALLOC)
arglist_function(char *debug_vstralloc, const char *, str)
#else
arglist_function(char *vstralloc, const char *, str)
#endif
{
    va_list argp;
    char *result;

    debug_alloc_pop();
    malloc_enter(dbmalloc_caller_loc(saved_file, saved_line));
    arglist_start(argp, str);
    result = internal_vstralloc(str, argp);
    arglist_end(argp);
    malloc_leave(dbmalloc_caller_loc(saved_file, saved_line));
    return result;
}


/*
** newstralloc - free existing string and then stralloc a new one.
*/
#if defined(USE_DBMALLOC)
char *debug_newstralloc(s, l, oldstr, newstr)
char *s;
int l;
#else
char *newstralloc(oldstr, newstr)
#endif
char *oldstr;
const char *newstr;
{
    char *addr;

    malloc_enter(dbmalloc_caller_loc(s, l));
    addr = stralloc(newstr);
    amfree(oldstr);
    malloc_leave(dbmalloc_caller_loc(s, l));
    return addr;
}


/*
** newvstralloc - free existing string and then vstralloc a new one.
*/
#if defined(USE_DBMALLOC)
arglist_function1(char *debug_newvstralloc, char *, oldstr, const char *,
    newstr)
#else
arglist_function1(char *newvstralloc, char *, oldstr, const char *, newstr)
#endif
{
    va_list argp;
    char *result;

    debug_alloc_pop();
    malloc_enter(dbmalloc_caller_loc(saved_file, saved_line));
    arglist_start(argp, newstr);
    result = internal_vstralloc(newstr, argp);
    arglist_end(argp);
    amfree(oldstr);
    malloc_leave(dbmalloc_caller_loc(saved_file, saved_line));
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

/*
 * amtable_alloc -- (re)allocate enough space for some number of elements.
 *
 * input:	table -- pointer to pointer to table
 *		elsize -- size of a table element
 *		count -- desired number of elements
 *		current -- pointer to current number of elements
 *		bump -- round up factor
 * output:	table -- possibly adjusted to point to new table area
 *		current -- possibly adjusted to new number of elements
 */

int
amtable_alloc(table, elsize, count, current, bump, init_func)
    void **table;
    size_t elsize;
    int count;
    int *current;
    int bump;
    void (*init_func)(void *);
{
    void *table_new;
    int table_count_new;
    int i;

    if (count >= *current) {
	table_count_new = ((count + bump) / bump) * bump;
	table_new = malloc(table_count_new * elsize);
	if (0 == table_new) {
	    errno = ENOMEM;
	    return -1;
	}
	if (0 != *table) {
	    memcpy(table_new, *table, *current * elsize);
	    free(*table);
	}
	*table = table_new;
	memset(((char *)*table) + *current * elsize,
	       0,
	       (table_count_new - *current) * elsize);
	if (init_func != NULL) {
	    for (i = *current; i < table_count_new; i++) {
		(*init_func)(((char *)*table) + i * elsize);
	    }
	}
	*current = table_count_new;
    }
    return 0;
}
