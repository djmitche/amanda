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
 * $Id: amqde.c,v 1.4 2005/12/01 00:00:00 martinea Exp $
 *
 * the central header file included by all amanda sources
 */

#include "amanda.h"

#ifdef HAVE_FNMATCH_H
#include <fnmatch.h>
#endif

/*
 * amanda's version of things.
 */
#define emalloc alloc
#define estrdup stralloc

typedef struct _exclude_entry {
        char *glob;
        struct _exclude_entry *next;
} exent_t;

exent_t *parse_exclude_path(char *, char *);
int should_exclude(exent_t *excl, char *path);

typedef struct __dirtrax {
	char *dirname;
	struct __dirtrax *next;
}         dirtrax_t;

typedef struct __diretrax_track_ll {
	dirtrax_t *first, *last;
}                   dirtrax_ll_t;

int
main(argc, argv)
	int argc;
	char *argv[];
{
	extern char *optarg;
	extern int optind;

	int ch;
	char buf[8192];
	struct stat top, st;
	char *path;
	time_t since = 0;
	DIR *d;
	struct dirent *de;
#if defined(HAVE_UNSIGNED_LONG_LONG) && !defined(__alpha)
	unsigned long long total = 0;
#else
	unsigned long total = 0;
#endif
	dirtrax_ll_t ll;
	dirtrax_t *trax = NULL, *new, *c;
	char *exclude_path = NULL;
	int havesince = 0;
	exent_t *excl = NULL;


	while ((ch = getopt(argc, argv, "s:x:")) != EOF) {
		switch (ch) {
		case 's':
			since = atoi(optarg);
			havesince = 1;
			break;
		case 'x':
			exclude_path = (optarg);
			break;
		default:
			fprintf(stderr, "unknown argument \'%c\'", ch);
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0) {
		fprintf(stderr, "must specify a path to dump\n");
		exit(1);
	}
	if (!havesince) {
		fprintf(stderr, "must specify -s\n");
		exit(1);
	}
	path = argv[0];

	if (exclude_path)
		excl = parse_exclude_path(path, exclude_path);
	
	if (chdir(path) != 0) {
		fprintf(stderr, "could not chdir to %s\n", path);
		exit(1);
	}
	if (lstat(".", &top) != 0) {
		fprintf(stderr, "could not stat %s\n", path);
		exit(1);
	}
	trax = emalloc(sizeof(*trax));
	trax->dirname = estrdup(".");
	trax->next = NULL;

	ll.first = ll.last = trax;

	for (new = ll.first; new; new = ll.first) {
		if (!(d = opendir(new->dirname))) {
			goto forcleanup;	/* basically continue; */
		}
		/*
		 * skip directories if we cross a device
		 */
		if (lstat(new->dirname, &st) != 0)
			goto forwclosedircleanup;
		if (top.st_dev != st.st_dev) {
			goto forwclosedircleanup;
		}

		while ((de = readdir(d))) {
			total += 505;
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
				continue;

			snprintf(buf, sizeof(buf) - 1, "%s/%s", new->dirname, de->d_name);

			if (excl && should_exclude(excl, buf))
				continue;

			if (lstat(buf, &st) != 0)
				continue;


			if (S_ISDIR(st.st_mode)) {
				c = emalloc(sizeof(*c));
				c->dirname = estrdup(buf);
				c->next = NULL;
				ll.last->next = c;
				ll.last = c;
			} else if(S_ISREG(st.st_mode)) {
				if (st.st_mtime < since && st.st_ctime < since)
					continue;
				if ((st.st_blocks * 512) < st.st_size)
					total += (st.st_blocks * 512);
				else
					total += st.st_size;
				/*
				 * add in some overhead, these are estimates
				 * after all
				 */
				total += 505;
			}
		}
forwclosedircleanup:
		closedir(d);
forcleanup:
		ll.first = new->next;
		free(new->dirname);
		free(new);
	}

	/*
	 * This is dumped out in k so upstream utilies can handle it without
	 * having to handle unsigned long long.  The theory is that if you
	 * need to use these estimates, then you proably have a system that
	 * uses long long.
	 */

#if defined(HAVE_UNSIGNED_LONG_LONG) && !defined(__alpha)
	fprintf(stderr, "amqde estimate: %llu kb\n", total/1024);
#else
	fprintf(stderr, "amqde estimate: %lu kb\n", total/1024);
#endif
	chdir("/");

	return (0);
}

/*
 * at the moment, we don't actually parse the include file because it means
 * implementing globbing, which is a pain in the arse.
 *
 * This is quick and dirty, after all.
 */
exent_t *
parse_exclude_path(rootpath, infile)
	char *rootpath;
	char *infile;
{
	FILE *f;
	char buf[4096], *fname;
	exent_t *fe = NULL, *e = NULL;
	struct stat st;

	if(infile[0] == '/') {
		if( stat(infile, &st) != 0 ) {
			fprintf(stderr, "could not find exclude file %s\n",
				infile);
			exit(1);
		}
		fname = infile;
	} else {
		snprintf(buf, sizeof(buf), "%s/%s", rootpath, infile);
		if( stat(buf, &st) != 0 ) {
			fprintf(stderr, "could not find exclude file %s\n",
				buf);
			exit(1);
		}
		fname = buf;
	}	

	/*
	 * zero length files don't need the overhead
	 */
	if(st.st_size == 0)
		return(NULL);

	if(!(f = fopen(fname, "r"))) {
		fprintf(stderr, "could not open exclude file \'%s\': %s",
			fname, strerror(errno));
		exit(1);
	}

	while(fgets(buf, sizeof(buf)-1, f)) {
		buf[strlen(buf)-1] = '\0';
		if(!fe) {
			fe = e = emalloc(sizeof(*e));
		} else {
			fe->next = emalloc(sizeof(*e));
			fe = fe->next;
		}
		fe->glob = estrdup(buf);
		fe->next = NULL;
	}

	fclose(f);

	return (e);
}

int
should_exclude(excl, path)
	exent_t *excl;
	char *path;
{
	if(!excl)
		return(0);

#ifdef HAVE_FNMATCH
	for( ; excl; excl = excl->next) {
		if(fnmatch(excl->glob, path, 0) == 0) {
			return(1);
		}
	}
#endif

	return (0);
}
