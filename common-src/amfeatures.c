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
 * $Id: amfeatures.c,v 1.3 2002/04/19 00:49:19 martinea Exp $
 *
 * Feature test related code.
 */

#include "amanda.h"
#include "amfeatures.h"

/*
 *=====================================================================
 * Initialize the base feature set for this version of Amanda.
 *
 * am_feature_t *am_init_feature_set()
 *
 * entry:	none
 * exit:	dynamically allocated feature set structure
 *=====================================================================
 */

am_feature_t *
am_init_feature_set()
{
    am_feature_t		*f = NULL;

    if ((f = am_allocate_feature_set()) != NULL) {
	/*
	 * Whenever a new feature is added, a new line usually needs
	 * to be added here to show that we support it.
	 */
	am_add_feature(f, have_feature_support);
	am_add_feature(f, amanda_feature_auth_keyword);
    }
    return f;
}

/*
 *=====================================================================
 * Allocate space for a feature set.
 *
 * am_feature_t *am_allocate_feature_set()
 *
 * entry:	none
 * exit:	dynamically allocated feature set structure
 *=====================================================================
 */

am_feature_t *
am_allocate_feature_set()
{
    size_t			nbytes;
    am_feature_t		*result;

    result = (am_feature_t *)alloc(sizeof(*result));
    memset(result, 0, sizeof(*result));
    nbytes = (((size_t)last_feature) + 8) >> 3;
    result->size = nbytes;
    result->bytes = (unsigned char *)alloc(nbytes);
    memset(result->bytes, 0, nbytes);
    return result;
}

/*
 *=====================================================================
 * Release space allocated to a feature set.
 *
 * void am_release_feature_set(am_feature_t *f)
 *
 * entry:	f = feature set to release
 * exit:	none
 *=====================================================================
 */

void
am_release_feature_set(f)
    am_feature_t		*f;
{
    if (f != NULL) {
	amfree(f->bytes);
	f->size = 0;
    }
}

/*
 *=====================================================================
 * Add a feature to a feature set.
 *
 * int am_add_feature(am_feature_t *f, am_feature_e n)
 *
 * entry:	f = feature set to add to
 *		n = feature to add
 * exit:	non-zero if feature added, else zero (e.g. if the feature
 *		is beyond what is currently supported)
 *=====================================================================
 */

int
am_add_feature(f, n)
    am_feature_t		*f;
    am_feature_e		n;
{
    size_t			byte;
    int				bit;
    int				result = 0;

    if (f != NULL && (int)n >= 0) {
	byte = ((size_t)n) >> 3;
	if (byte < f->size) {
	    bit = ((int)n) & 0x7;
	    f->bytes[byte] |= (1 << bit);
	    result = 1;
	}
    }
    return result;
}

/*
 *=====================================================================
 * Remove a feature from a feature set.
 *
 * int am_remove_feature(am_feature_t *f, am_feature_e n)
 *
 * entry:	f = feature set to remove from
 *		n = feature to remove
 * exit:	non-zero if feature removed, else zero (e.g. if the feature
 *		is beyond what is currently supported)
 *=====================================================================
 */

int
am_remove_feature(f, n)
    am_feature_t		*f;
    am_feature_e		n;
{
    size_t			byte;
    int				bit;
    int				result = 0;

    if (f != NULL && (int)n >= 0) {
	byte = ((size_t)n) >> 3;
	if (byte < f->size) {
	    bit = ((int)n) & 0x7;
	    f->bytes[byte] &= ~(1 << bit);
	    result = 1;
	}
    }
    return result;
}

/*
 *=====================================================================
 * Return true if a given feature is available.
 *
 * int am_has_feature(am_feature_t *f, am_feature_e n)
 *
 * entry:	f = feature set to test
 *		n = feature to test
 * exit:	non-zero if feature is enabled
 *=====================================================================
 */

int
am_has_feature(f, n)
    am_feature_t		*f;
    am_feature_e		n;
{
    size_t			byte;
    int				bit;
    int				result = 0;

    if (f != NULL && (int)n >= 0) {
	byte = ((size_t)n) >> 3;
	if (byte < f->size) {
	    bit = ((int)n) & 0x7;
	    result = ((f->bytes[byte] & (1 << bit)) != 0);
	}
    }
    return result;
}

/*
 *=====================================================================
 * Convert a feature set to string.
 *
 * char *am_feature_to_string(am_feature_t *f)
 *
 * entry:	f = feature set to convet
 * exit:	dynamically allocated string
 *=====================================================================
 */

char *
am_feature_to_string(f)
    am_feature_t		*f;
{
    char			*result;
    size_t			i;

    if (f == NULL) {
	result = stralloc("");
    } else {
	result = alloc((f->size * 2) + 1);
	for (i = 0; i < f->size; i++) {
	    snprintf(result + (i * 2), 2 + 1, "%02x", f->bytes[i]);
	}
	result[i * 2] = '\0';
    }
    return result;
}

/*
 *=====================================================================
 * Convert a sting back to a feature set.
 *
 * am_feature_t *am_string_to_feature(char *s)
 *
 * entry:	s = string to convert
 * exit:	dynamically allocated feature set
 *
 * Note: if the string is longer than the list of features we support,
 * the remaining input features are ignored.  If it is shorter, the
 * missing features are disabled.
 *
 * If the string is not formatted properly (not a multiple of two bytes),
 * NULL is returned.
 *
 * Conversion stops at the first non-hex character.
 *=====================================================================
 */

am_feature_t *
am_string_to_feature(s)
    char			*s;
{
    am_feature_t		*f = NULL;
    size_t			i;
    int				ch1, ch2;

    if (s != NULL) {
	f = am_allocate_feature_set();
	for (i = 0; i < f->size && (ch1 = *s++) != '\0'; i++) {
	    if (isdigit(ch1)) {
		ch1 -= '0';
	    } else if (ch1 >= 'a' && ch1 <= 'f') {
		ch1 -= 'a';
		ch1 += 10;
	    } else if (ch1 >= 'A' && ch1 <= 'F') {
		ch1 -= 'a';
		ch1 += 10;
	    } else {
		break;
	    }
	    ch2 = *s++;
	    if (isdigit(ch2)) {
		ch2 -= '0';
	    } else if (ch2 >= 'a' && ch2 <= 'f') {
		ch2 -= 'a';
		ch2 += 10;
	    } else if (ch2 >= 'A' && ch2 <= 'F') {
		ch2 -= 'a';
		ch2 += 10;
	    } else {
		amfree(f);				/* bad conversion */
		break;
	    }
	    f->bytes[i] = (ch1 << 4) | ch2;
	}
    }
    return f;
}

#if defined(TEST)
int
main(argc, argv)
    int				argc;
    char			**argv;
{
    am_feature_t		*f;
    am_feature_t		*f1;
    char			*s;
    char			*s1;
    int				i;
    int				n;

    f = am_init_feature_set();
    if (f == NULL) {
	fprintf(stderr, "cannot initialize feature set\n");
	return 1;
    }

    s = am_feature_to_string(f);
    printf("base features=%s\n", s);

    f1 = am_string_to_feature(s);
    s1 = am_feature_to_string(f1);
    if (strcmp(s, s1) != 0) {
	fprintf(stderr, "base feature -> string -> feature set mismatch\n");
	fprintf(stderr, "conv features=%s\n", s);
    }

    amfree(s1);
    amfree(s);

    for (i = 1; i < argc; i++) {
	if (argv[i][0] == '+') {
	    n = atoi(&argv[i][1]);
	    if (am_add_feature(f, (am_feature_e)n)) {
		printf("added feature number %d\n", n);
	    } else {
		printf("could not add feature number %d\n", n);
	    }
	} else if (argv[i][0] == '-') {
	    n = atoi(&argv[i][1]);
	    if (am_remove_feature(f, (am_feature_e)n)) {
		printf("removed feature number %d\n", n);
	    } else {
		printf("could not remove feature number %d\n", n);
	    }
	} else {
	    n = atoi(argv[i]);
	    if (am_has_feature(f, (am_feature_e)n)) {
		printf("feature %d is set\n", n);
	    } else {
		printf("feature %d is not set\n", n);
	    }
	}
    }

    s = am_feature_to_string(f);
    printf(" new features=%s\n", s);
    amfree(s);

    return 0;
}
#endif
