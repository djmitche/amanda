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
 * $Id: packet.h,v 1.1 1998/11/04 20:17:31 kashmir Exp $
 *
 * interfaces for modifying amanda protocol packet type
 */
#ifndef PACKET_H
#define PACKET_H

/*
 * We limit our body length to 50k.
 */
#define	MAX_PACKET	(50*1024)

typedef enum { P_REQ, P_REP, P_ACK, P_NAK } pktype_t;
typedef struct {
    pktype_t type;				/* type of packet */
    char body[MAX_PACKET];			/* body of packet */
} pkt_t;

/*
 * Initialize a packet
 */
void pkt_init P((pkt_t *, pktype_t, const char *, ...));

/*
 * Append data to a packet
 */
void pkt_cat P((pkt_t *, const char *, ...));

/*
 * Convert the packet type to and from a string
 */
const char *pkt_type2str P((pktype_t));
pktype_t pkt_str2type P((const char *));

#endif /* PACKET_H */
