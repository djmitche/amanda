/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1999 University of Maryland at College Park
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
 * $Id: packet.c,v 1.4 1999/05/14 19:31:19 kashmir Exp $
 *
 * Routines for modifying the amanda protocol packet type
 */
#include "amanda.h"
#include "arglist.h"
#include "packet.h"

/*
 * Table of packet types and their printable forms
 */
static const struct {
    const char name[4];
    pktype_t type;
} pktypes[] = {
    { "REQ", P_REQ },
    { "REP", P_REP },
    { "ACK", P_ACK },
    { "NAK", P_NAK }
};
#define	NPKTYPES	(sizeof(pktypes) / sizeof(pktypes[0]))

/*
 * Initialize a packet
 */
arglist_function2(void pkt_init, pkt_t *, pkt, pktype_t, type,
    const char *, fmt)
{
    va_list argp;

    assert(pkt != NULL);
    assert(strcmp(pkt_type2str(type), "BOGUS") != 0);
    assert(fmt != NULL);

    pkt->type = type;

    arglist_start(argp, fmt);
    vsnprintf(pkt->body, sizeof(pkt->body), fmt, argp);
    arglist_end(argp);
}

/*
 * Append data to a packet
 */
arglist_function1(void pkt_cat, pkt_t *, pkt, const char *, fmt)
{
    size_t len, bufsize;
    va_list argp;

    assert(pkt != NULL);
    assert(fmt != NULL);

    len = strlen(pkt->body);
    assert(len < sizeof(pkt->body));

    bufsize = sizeof(pkt->body) - len;
    if (bufsize <= 0)
	return;

    arglist_start(argp, fmt);
    vsnprintf(pkt->body + len, bufsize, fmt, argp);
    arglist_end(argp);
}

/*
 * Converts a string into a packet type
 */
pktype_t
pkt_str2type(typestr)
    const char *typestr;
{
    int i;

    assert(typestr != NULL);

    for (i = 0; i < NPKTYPES; i++)
	if (strcmp(typestr, pktypes[i].name) == 0)
	    return (pktypes[i].type);
    return ((pktype_t)-1);
}

/*
 * Converts a packet type into a string
 */
const char *
pkt_type2str(type)
    pktype_t type;
{
    int i;

    for (i = 0; i < NPKTYPES; i++)
	if (pktypes[i].type == type)
	    return (pktypes[i].name);
    return ("BOGUS");
}
