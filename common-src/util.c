/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1999 University of Maryland at College Park
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
 * $Id: util.c,v 1.2.2.1 1999/06/07 16:36:37 kashmir Exp $
 */

#include "amanda.h"
#include "util.h"

/*
 * Bind to a port in the given range.  Takes a begin,end pair of port numbers.
 *
 * Returns negative on error (EGAIN if all ports are in use).  Returns 0
 * on success.
 */
int
bind_portrange(s, addrp, first_port, last_port)
    int s;
    struct sockaddr_in *addrp;
    int first_port, last_port;
{
    int port, cnt;
    const int num_ports = last_port - first_port;

    assert(first_port < last_port);

    /*
     * We pick a different starting port based on our pid to avoid
     * always picking the same reserved port twice.
     */
    port = (first_port + getpid()) % num_ports;

    /*
     * Scan through the range, trying all available ports.  Wrap around
     * if we don't happen to start at the beginning.
     */
    for (cnt = 0; cnt < num_ports; cnt++, port = (port + 1) % num_ports) {
	addrp->sin_port = htons(port);
	if (bind(s, (struct sockaddr *)addrp, sizeof(*addrp)) >= 0)
	    break;
	/*
	 * If the error was something other then port in use, stop.
	 */
	if (errno != EADDRINUSE)
	    return (-1);
    }
    if (cnt == num_ports) {
	errno = EAGAIN;
	return (-1);
    }
    return (0);
}
