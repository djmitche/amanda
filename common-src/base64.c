/*
 * Copyright (c) 1996, 1998 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

/*
 * Portions Copyright (c) 1995 by International Business Machines, Inc.
 *
 * International Business Machines, Inc. (hereinafter called IBM) grants
 * permission under its copyrights to use, copy, modify, and distribute this
 * Software with or without fee, provided that the above copyright notice and
 * all paragraphs of this notice appear in all copies, and that the name of IBM
 * not be used in connection with the marketing of any product incorporating
 * the Software or modifications thereof, without specific, written prior
 * permission.
 *
 * To the extent it has a right to do so, IBM grants an immunity from suit
 * under its patents, if any, for the use, sale or manufacture of products to
 * the extent that such products are used for performing Domain Name System
 * dynamic updates in TCP/IP networks by means of the Software.  No immunity is
 * granted for any product per se or for any other function of any product.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", AND IBM DISCLAIMS ALL WARRANTIES,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE.  IN NO EVENT SHALL IBM BE LIABLE FOR ANY SPECIAL,
 * DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE, EVEN
 * IF IBM IS APPRISED OF THE POSSIBILITY OF SUCH DAMAGES.
 */

/*
 * $Id: base64.c,v 1.1 1998/12/02 20:19:27 kashmir Exp $
 *
 * Routines for encoding and decoding data into base64
 */

#include "amanda.h"
#include "base64.h"

const char *
base64encode(src, srclength)
	const void *src;
	size_t srclength;
{
	/* this array maps a 6 bit value to a character */
	static const char Base64[] =
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	static char *target = NULL;
	size_t datalength = 0;
	unsigned char input[3];
	unsigned char output[4];
	size_t targsize;
	int i;

	assert(src != NULL);
	assert(srclength > 0);

	if (target != NULL) {
		amfree(target);
		target = NULL;
	}

	targsize = (srclength * 4) / 3 + 5;
	target = alloc(targsize);

	while (2 < srclength) {
		input[0] = *(char *)src++;
		input[1] = *(char *)src++;
		input[2] = *(char *)src++;
		srclength -= 3;

		output[0] = input[0] >> 2;
		output[1] = ((input[0] & 0x03) << 4) + (input[1] >> 4);
		output[2] = ((input[1] & 0x0f) << 2) + (input[2] >> 6);
		output[3] = input[2] & 0x3f;

		target[datalength++] = Base64[output[0]];
		target[datalength++] = Base64[output[1]];
		target[datalength++] = Base64[output[2]];
		target[datalength++] = Base64[output[3]];
	}

	/* Now we worry about padding. */
	if (0 != srclength) {
		/* Get what's left. */
		input[0] = input[1] = input[2] = '\0';
		for (i = 0; i < srclength; i++)
			input[i] = *(char *)src++;

		output[0] = input[0] >> 2;
		output[1] = ((input[0] & 0x03) << 4) + (input[1] >> 4);
		output[2] = ((input[1] & 0x0f) << 2) + (input[2] >> 6);

		target[datalength++] = Base64[output[0]];
		target[datalength++] = Base64[output[1]];
		if (srclength == 1)
			target[datalength++] = '=';
		else
			target[datalength++] = Base64[output[2]];
		target[datalength++] = '=';
	}
	target[datalength] = '\0';	/* Returned value doesn't count \0. */
	return (target);
}

void
base64decode(bufcoded, bufdecoded, bufdecoded_size)
	const char *bufcoded;
	void *bufdecoded;
	size_t bufdecoded_size;
{
	/*
	 * this array maps a character from the Base64 array above to a
	 * six bit value.  characters not in the Base64 array are given the
	 * value 64
	 */
	static const int pr2six[256] = {
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64,
	    64, 64, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64,
	    64, 64, 64, 64, 64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
	    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
	    25, 64, 64, 64, 64, 64, 64, 26, 27, 28, 29, 30, 31, 32, 33,
	    34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
	    49, 50, 51, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
	    64
	};
	int nbytesdecoded;
	char *bufplain;
	unsigned char *bufin;
	unsigned char *bufout;
	int nprbytes;

	assert(bufcoded != NULL);
	assert(bufdecoded != NULL);
	assert(bufdecoded_size > 0);

	/* Strip leading whitespace. */
	while (*bufcoded == ' ' || *bufcoded == '\t')
		bufcoded++;

	/*
	 * Figure out how many characters are in the input buffer. Allocate
	 * this many from the per-transaction pool for the result.
	 */
	bufin = (unsigned char *) bufcoded;
	while (pr2six[*(bufin++)] < 64)
		continue;
	nprbytes = (char *) bufin - bufcoded - 1;
	nbytesdecoded = ((nprbytes + 3) / 4) * 3;

	bufplain = alloc(nbytesdecoded + 1);
	bufout = (unsigned char *)bufplain;
	bufin = (unsigned char *) bufcoded;

	while (nprbytes > 0) {
		*(bufout++) = (unsigned char)(pr2six[*bufin] << 2 |
		    pr2six[bufin[1]] >> 4);
		*(bufout++) = (unsigned char) (pr2six[bufin[1]] << 4 |
		    pr2six[bufin[2]] >> 2);
		*(bufout++) = (unsigned char) (pr2six[bufin[2]] << 6 |
		    pr2six[bufin[3]]);
		bufin += 4;
		nprbytes -= 4;
	}

	if (nprbytes & 03) {
		if (pr2six[bufin[-2]] > 63)
			nbytesdecoded -= 2;
		else
			nbytesdecoded -= 1;
	}
	/* truncate if provided buffer is too small */
	if (nbytesdecoded > bufdecoded_size)
		nbytesdecoded = bufdecoded_size;
	memcpy(bufdecoded, bufplain, nbytesdecoded);
	amfree(bufplain);
}
