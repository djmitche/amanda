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
 * Author: James da Silva, Systems Design and Analysis Group
 *			   Computer Science Department
 *			   University of Maryland at College Park
 */
/*
 * $Id: alloc.c,v 1.6 1997/11/12 23:06:23 blair Exp $
 *
 * Memory allocators with error handling.  If the allocation fails,
 * error() is called, relieving the caller from checking the return
 * code
 */
#include "amanda.h"


/*
** alloc - a wrapper for malloc.
*/
void *alloc(size)
int size;
{
    void *addr;

    addr = (void *)malloc(size ? size : 1);
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
    if (old != (void *)0) free(old);
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
** stralloc2 - copies the two given strings into newly allocated memory.
*/
char *stralloc2(str1, str2)
char *str1;
char *str2;
{
    char *addr;

    addr = alloc(strlen(str1)+strlen(str2)+1);
    strcpy(addr, str1);
    strcat(addr, str2);
    return addr;
}


/*
** newstralloc - free existing string and then stralloc a new one.
*/
char *newstralloc(oldstr, newstr)
char *oldstr;
char *newstr;
{
    if (oldstr != (char *)0) free(oldstr);
    return stralloc(newstr);
}


/*
** newstralloc2 - free existing string and then stralloc2 a new one.
*/
char *newstralloc2(oldstr, newstr1, newstr2)
char *oldstr;
char *newstr1;
char *newstr2;
{
    if (oldstr != (char *)0) free(oldstr);
    return stralloc2(newstr1, newstr2);
}
