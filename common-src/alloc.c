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
 * $Id: alloc.c,v 1.4 1997/08/31 18:02:19 amcore Exp $
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
