/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991, 1995 University of Maryland at College Park
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
 * $Id: amandates.c,v 1.10.2.2 1998/04/08 16:25:47 amcore Exp $
 *
 * manage amandates file, that mimics /etc/dumpdates, but stores
 * GNUTAR dates
 */

#include "amanda.h"
#include "getfsent.h"

#include "amandates.h"

static amandates_t *amandates_list = NULL;
static FILE *amdf = NULL;
static int updated, readonly;
static void import_dumpdates P((amandates_t *));
static void enter_record P((char *, int , time_t));
static amandates_t *lookup P((char *name, int import));

int start_amandates(open_readwrite)
int open_readwrite;
{
    int rc, level;
    long ldate;
    char *line = NULL, *name = NULL;
    char *s;
    int ch;

    /* clean up from previous invocation */

    if(amdf != NULL)
	finish_amandates();
    if(amandates_list != NULL)
	free_amandates();

    /* initialize state */

    updated = 0;
    readonly = !open_readwrite;
    amdf = NULL;
    amandates_list = NULL;

    /* open the file */

    if (access(AMANDATES_FILE,F_OK))
	/* not yet existing */
	if ( (rc = open(AMANDATES_FILE,(O_CREAT|O_RDWR),0644)) != -1 )
	    /* open/create successfull */
	    aclose(rc);

    if(open_readwrite)
	amdf = fopen(AMANDATES_FILE, "r+");
    else
	amdf = fopen(AMANDATES_FILE, "r");

    /* create it if we need to */

    if(amdf == NULL && errno == EINTR && open_readwrite)
	amdf = fopen(AMANDATES_FILE, "w");

    if(amdf == NULL)
	return 0;

    if(open_readwrite)
	rc = amflock(fileno(amdf), "amandates");
    else
	rc = amroflock(fileno(amdf), "amandates");

    if(rc == -1)
	error("could not lock %s: %s", AMANDATES_FILE, strerror(errno));

    for(; (line = agets(amdf)) != NULL; free(line)) {
	s = line;
	ch = *s++;

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    continue;				/* no name field */
	}
	name = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';				/* terminate the name */

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf(s - 1, "%d %ld", &level, &ldate) != 2) {
	    continue;				/* no more fields */
	}

	if(level < 0 || level >= DUMP_LEVELS) {
	    continue;
	}

	enter_record(name, level, (time_t) ldate);
    }

    if(ferror(amdf))
	error("reading %s: %s", AMANDATES_FILE, strerror(errno));

    updated = 0;	/* reset updated flag */
    return 1;
}

void finish_amandates()
{
    amandates_t *amdp;
    int level;

    if(amdf == NULL)
	return;

    if(updated) {
	if(readonly)
	    error("updated amandates after opening readonly");

	rewind(amdf);
	for(amdp = amandates_list; amdp != NULL; amdp = amdp->next) {
	    for(level = 0; level < DUMP_LEVELS; level++) {
		if(amdp->dates[level] == EPOCH) continue;
		fprintf(amdf, "%s %d %ld\n",
			amdp->name, level, (long) amdp->dates[level]);
	    }
	}
    }

    if(amfunlock(fileno(amdf), "amandates") == -1)
	error("could not unlock %s: %s", AMANDATES_FILE, strerror(errno));
    afclose(amdf);
}

void free_amandates()
{
    amandates_t *amdp, *nextp;

    for(amdp = amandates_list; amdp != NULL; amdp = nextp) {
	nextp = amdp->next;
	amfree(amdp->name);
	amfree(amdp);
    }
    amandates_list = NULL;
}

static amandates_t *lookup(name, import)
char *name;
int import;
{
    amandates_t *prevp, *amdp, *newp;
    int rc, level;

    rc = 0;

    for(prevp=NULL,amdp=amandates_list;amdp!=NULL;prevp=amdp,amdp=amdp->next)
	if((rc = strcmp(name, amdp->name)) <= 0)
	    break;

    if(amdp && rc == 0)
	return amdp;

    newp = alloc(sizeof(amandates_t));
    newp->name = stralloc(name);
    for(level = 0; level < DUMP_LEVELS; level++)
	newp->dates[level] = EPOCH;
    newp->next = amdp;
    if(prevp) prevp->next = newp;
    else amandates_list = newp;

    import_dumpdates(newp);

    return newp;
}

amandates_t *amandates_lookup(name)
char *name;
{
    return lookup(name, 1);
}

static void enter_record(name, level, dumpdate)
char *name;
int level;
time_t dumpdate;
{
    amandates_t *amdp;

    amdp = lookup(name, 0);

    if(level < 0 || level >= DUMP_LEVELS || dumpdate < amdp->dates[level]) {
	/* this is not allowed, but we can ignore it */
        dbprintf(("amandates botch: %s lev %d: new dumpdate %ld old %ld\n",
		  name, level, (long) dumpdate, (long) amdp->dates[level]));
	return;
    }

    amdp->dates[level] = dumpdate;
}


void amandates_updateone(name, level, dumpdate)
char *name;
int level;
time_t dumpdate;
{
    amandates_t *amdp;

    assert(!readonly);

    amdp = lookup(name, 1);

    if(level < 0 || level >= DUMP_LEVELS || dumpdate < amdp->dates[level]) {
	/* this is not allowed, but we can ignore it */
	dbprintf(("amandates updateone: %s lev %d: new dumpdate %ld old %ld",
		  name, level, (long) dumpdate, (long) amdp->dates[level]));
	return;
    }

    amdp->dates[level] = dumpdate;
    updated = 1;
}


/* -------------------------- */

static void import_dumpdates(amdp)
amandates_t *amdp;
{
    char *devname = NULL, *line = NULL, *fname = NULL;
    int level;
    time_t dumpdate;
    FILE *dumpdf;
    char *s;
    int ch;

    devname = amname_to_devname(amdp->name);

    if((dumpdf = fopen("/etc/dumpdates", "r")) == NULL)
	return;
    for(; (line = agets(dumpdf)) != NULL; free(line)) {
	s = line;
	ch = *s++;

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    continue;				/* no fname field */
	}
	fname = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';				/* terminate fname */

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf(s - 1, "%d", &level) != 1) {
	    continue;				/* no level field */
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    continue;				/* no dumpdate field */
	}
	dumpdate = unctime(s-1);

	if(strcmp(fname, devname) != 0 || level < 0 || level >= DUMP_LEVELS) {
	    continue;
	}

	if(dumpdate != -1 && dumpdate > amdp->dates[level]) {
	    if(!readonly) updated = 1;
	    amdp->dates[level] = dumpdate;
	}
    }
    afclose(dumpdf);
}
