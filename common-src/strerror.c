/*
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/* $Id: strerror.c,v 1.4 1997/12/30 05:24:24 jrj Exp $ */

#include "amanda.h"

/*
 * Return the error message corresponding to some error number.
 */
char *
strerror(e)
	int e;
{
	extern int sys_nerr;
	extern char *sys_errlist[];
	char number[NUM_STR_SIZE];
	static char *unknown = NULL;

	if ((unsigned)e < sys_nerr)
		return sys_errlist[e];
	ap_snprintf(number, sizeof(number), "%d", e);
	unknown = newstralloc2(unknown, "Unknown error: ", number);
	return unknown;
}
