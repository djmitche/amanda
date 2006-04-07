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
 * $Id: tapefile.c,v 1.31 2006/04/07 18:00:13 martinea Exp $
 *
 * routines to read and write the amanda active tape list
 */
#include "amanda.h"
#include "tapefile.h"
#include "conffile.h"

static tape_t *tape_list = NULL;

/* local functions */
static tape_t *parse_tapeline P((int *status, char *line));
static tape_t *insert P((tape_t *list, tape_t *tp));
static time_t stamp2time P((char *datestamp));



int read_tapelist(tapefile)
char *tapefile;
{
    tape_t *tp;
    FILE *tapef;
    int pos;
    char *line = NULL;
    int status;

    tape_list = NULL;
    if((tapef = fopen(tapefile,"r")) == NULL) {
	return 1;
    }

    while((line = agets(tapef)) != NULL) {
	tp = parse_tapeline(&status, line);
	amfree(line);
	if(tp == NULL && status != 0) return 1;
	if(tp != NULL) tape_list = insert(tape_list, tp);
    }
    afclose(tapef);

    for(pos=1,tp=tape_list; tp != NULL; pos++,tp=tp->next) {
	tp->position = pos;
    }

    return 0;
}

int write_tapelist(tapefile)
char *tapefile;
{
    tape_t *tp;
    FILE *tapef;
    char *newtapefile;
    int rc;

    newtapefile = stralloc2(tapefile, ".new");

    if((tapef = fopen(newtapefile,"w")) == NULL) {
	amfree(newtapefile);
	return 1;
    }

    for(tp = tape_list; tp != NULL; tp = tp->next) {
	fprintf(tapef, "%s %s", tp->datestamp, tp->label);
	if(tp->reuse) fprintf(tapef, " reuse");
	else fprintf(tapef, " no-reuse");
	fprintf(tapef, "\n");
    }

    if (fclose(tapef) == EOF) {
	fprintf(stderr,"error [closing %s: %s]", newtapefile, strerror(errno));
	return 1;
    }
    rc = rename(newtapefile, tapefile);
    amfree(newtapefile);

    return(rc != 0);
}

void clear_tapelist()
{
    tape_t *tp, *next;

    for(tp = tape_list; tp; tp = next) {
	amfree(tp->label);
	next = tp->next;
	amfree(tp);
    }
    tape_list = NULL;
}

tape_t *lookup_tapelabel(label)
char *label;
{
    tape_t *tp;

    for(tp = tape_list; tp != NULL; tp = tp->next) {
	if(strcmp(label, tp->label) == 0) return tp;
    }
    return NULL;
}



tape_t *lookup_tapepos(pos)
int pos;
{
    tape_t *tp;

    for(tp = tape_list; tp != NULL; tp = tp->next) {
	if(tp->position == pos) return tp;
    }
    return NULL;
}


tape_t *lookup_tapedate(datestamp)
char *datestamp;
{
    tape_t *tp;

    for(tp = tape_list; tp != NULL; tp = tp->next) {
	if(strcmp(tp->datestamp, datestamp) == 0) return tp;
    }
    return NULL;
}

int lookup_nb_tape()
{
    tape_t *tp;
    int pos=0;

    for(tp = tape_list; tp != NULL; tp = tp->next) {
	pos=tp->position;
    }
    return pos;
}

tape_t *lookup_last_reusable_tape(skip)
     int skip;
{
    tape_t *tp, **tpsave;
    int count=0;
    int s;
    int tapecycle = getconf_int(CNF_TAPECYCLE);
    char *labelstr = getconf_str (CNF_LABELSTR);

    /*
     * The idea here is we keep the last "several" reusable tapes we
     * find in a stack and then return the n-th oldest one to the
     * caller.  If skip is zero, the oldest is returned, if it is
     * one, the next oldest, two, the next to next oldest and so on.
     */
    tpsave = alloc((skip + 1) * sizeof (*tpsave));
    for(s = 0; s <= skip; s++) {
	tpsave[s] = NULL;
    }
    for(tp = tape_list; tp != NULL; tp = tp->next) {
	if(tp->reuse == 1 && strcmp(tp->datestamp,"0") != 0 && match (labelstr, tp->label)) {
	    count++;
	    for(s = skip; s > 0; s--) {
	        tpsave[s] = tpsave[s - 1];
	    }
	    tpsave[0] = tp;
	}
    }
    s = tapecycle - count;
    if(s < 0) s = 0;
    if(count < tapecycle - skip) tp = NULL;
    else tp = tpsave[skip - s];
    amfree(tpsave);
    return tp;
}

int reusable_tape(tp)
    tape_t *tp;
{
    int count = 0;

    if(tp == NULL) return 0;
    if(tp->reuse == 0) return 0;
    if( strcmp(tp->datestamp,"0") == 0) return 1;
    while(tp != NULL) {
	if(tp->reuse == 1) count++;
	tp = tp->prev;
    }
    return (count >= getconf_int(CNF_TAPECYCLE));
}

void remove_tapelabel(label)
char *label;
{
    tape_t *tp, *prev, *next;

    tp = lookup_tapelabel(label);
    if(tp != NULL) {
	prev = tp->prev;
	next = tp->next;
	if(prev != NULL)
	    prev->next = next;
	else /* begin of list */
	    tape_list = next;
	if(next != NULL)
	    next->prev = prev;
	while (next != NULL) {
	    next->position--;
	    next = next->next;
	}
	amfree(tp->label);
	amfree(tp);
    }
}

tape_t *add_tapelabel(datestamp, label)
char *datestamp;
char *label;
{
    tape_t *cur, *new;

    /* insert a new record to the front of the list */

    new = (tape_t *) alloc(sizeof(tape_t));

    new->datestamp = stralloc(datestamp);
    new->position = 0;
    new->reuse = 1;
    new->label = stralloc(label);

    new->prev  = NULL;
    if(tape_list != NULL) tape_list->prev = new;
    new->next = tape_list;
    tape_list = new;

    /* scan list, updating positions */
    cur = tape_list;
    while(cur != NULL) {
	cur->position++;
	cur = cur->next;
    }

    return new;
}

int guess_runs_from_tapelist()
{
    tape_t *tp;
    int i, ntapes, tape_ndays, dumpcycle, runtapes, runs;
    time_t tape_time, today;

    today = time(0);
    dumpcycle = getconf_int(CNF_DUMPCYCLE);
    runtapes = getconf_int(CNF_RUNTAPES);
    if(runtapes == 0) runtapes = 1;	/* just in case */

    ntapes = 0;
    tape_ndays = 0;
    for(i = 1; i < getconf_int(CNF_TAPECYCLE); i++) {
	if((tp = lookup_tapepos(i)) == NULL) break;

	tape_time  = stamp2time(tp->datestamp);
	tape_ndays = days_diff(tape_time, today);

	if(tape_ndays < dumpcycle) ntapes++;
	else break;
    }

    if(tape_ndays < dumpcycle)	{
	/* scale for best guess */
	if(tape_ndays == 0) ntapes = dumpcycle * runtapes;
	else ntapes = ntapes * dumpcycle / tape_ndays;
    }
    else if(ntapes == 0) {
	/* no dumps within the last dumpcycle, guess as above */
	ntapes = dumpcycle * runtapes;
    }

    runs = (ntapes + runtapes - 1) / runtapes;
    if (runs <= 0)
      runs = 1;
    return runs;
}

static tape_t *parse_tapeline(status, line)
int *status;
char *line;
{
    tape_t *tp = NULL;
    char *s, *s1;
    int ch;

    *status = 0;
    tp = (tape_t *) alloc(sizeof(tape_t));

    tp->prev = NULL;
    tp->next = NULL;

    s = line;
    ch = *s++;

    skip_whitespace(s, ch);
    if(ch == '\0') {
	amfree(tp);
	return NULL;
    }
    s1 = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    tp->datestamp = stralloc(s1);


    skip_whitespace(s, ch);
    s1 = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    tp->label = stralloc(s1);

    skip_whitespace(s, ch);
    tp->reuse = 1;
#define sc "reuse"
    if(strncmp(s - 1, sc, sizeof(sc)-1) == 0)
	tp->reuse = 1;
#undef sc
#define sc "no-reuse"
    if(strncmp(s - 1, sc, sizeof(sc)-1) == 0)
	tp->reuse = 0;
#undef sc

    return tp;
}


/* insert in reversed datestamp order */
static tape_t *insert(list, tp)
tape_t *list, *tp;
{
    tape_t *prev, *cur;

    prev = NULL;
    cur = list;

    while(cur != NULL && strcmp(cur->datestamp, tp->datestamp) >= 0) {
	prev = cur;
	cur = cur->next;
    }
    tp->prev = prev;
    tp->next = cur;
    if(prev == NULL) list = tp;
    else prev->next = tp;
    if(cur !=NULL) cur->prev = tp;

    return list;
}


static time_t stamp2time(datestamp)
char *datestamp;
/*
 * Converts datestamp (an char of the form YYYYMMDD or YYYYMMDDHHMMSS) into a real
 * time_t value.
 * Since the datestamp contains no timezone or hh/mm/ss information, the
 * value is approximate.  This is ok for our purposes, since we round off
 * scheduling calculations to the nearest day.
 */
{
    struct tm tm;
    time_t now;
    char date[9];
    int dateint;

    strncpy(date, datestamp, 8);
    date[8] = '\0';
    dateint = atoi(date);
    now = time(0);
    tm = *localtime(&now);	/* initialize sec/min/hour & gmtoff */

    tm.tm_year = ( dateint          / 10000) - 1900;
    tm.tm_mon  = ((dateint % 10000) /   100) - 1;
    tm.tm_mday = ((dateint %   100)        );

    return mktime(&tm);
}
