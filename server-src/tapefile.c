/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1994 University of Maryland
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
 * tapefile.c  - routines to read and write the amanda active tape list
 */
#include "amanda.h"
#include "tapefile.h"
#include "conffile.h"

#define MAXLINE 4096
static tape_t *tape_list = NULL;

/* local functions */
static tape_t *parse_tapeline P((char *line));
static tape_t *insert P((tape_t *list, tape_t *tp));
static time_t stamp2time P((int datestamp));



int read_tapelist(tapefile)
char *tapefile;
{
    tape_t *tp;
    FILE *tapef;
    int pos;
    char buffer[MAXLINE];

    tape_list = NULL;
    if((tapef = fopen(tapefile,"r")) != NULL) {

        while(fgets(buffer, MAXLINE, tapef) != NULL) {
	    tp = parse_tapeline(buffer);
	    if(tp == NULL) return 1;
	    tape_list = insert(tape_list, tp);
        }

        fclose(tapef);
    }

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

    if((tapef = fopen(tapefile,"w")) == NULL)
	return 1;

    for(tp = tape_list; tp != NULL; tp = tp->next) {
	fprintf(tapef, "%d %s\n", tp->datestamp, tp->label);
    }

    fclose(tapef);
    return 0;
}


tape_t *lookup_tapelabel(label)
char *label;
{
    tape_t *tp;

    for(tp = tape_list; tp != NULL; tp = tp->next) {
	if(!strcmp(label, tp->label)) return tp;
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
int datestamp;
{
    tape_t *tp;

    for(tp = tape_list; tp != NULL; tp = tp->next) {
	if(tp->datestamp == datestamp) return tp;
    }
    return NULL;
}


tape_t *shift_tapelist(datestamp, label, tapedays)
int datestamp;
char *label;
int tapedays;
{
    tape_t *prev, *cur, *new;

    /* insert a new record to the front of the list */

    new = (tape_t *) alloc(sizeof(tape_t));

    new->datestamp = datestamp;
    new->position = 0;
    new->label = stralloc(label);

    new->next = tape_list;
    tape_list = new;

    /* scan list, updating positions and looking for cutoff */
    prev = NULL;
    cur = tape_list;
    while(cur != NULL && cur->position < tapedays) {
	cur->position++;
	prev = cur;
	cur = cur->next;
    }

    /* new list cuts off at prev */
    if(prev) prev->next = NULL;
    else tape_list = NULL;

    return cur;
}

int guess_runs_from_tapelist()
{
    tape_t *tp;
    int i, ntapes, tape_ndays, dumpcycle, runtapes;
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

    return (ntapes + runtapes - 1) / runtapes;
}

static tape_t *parse_tapeline(line)
char *line;
{
    tape_t *tp;
    int len;

    tp = (tape_t *) alloc(sizeof(tape_t));

    tp->next = NULL;
    tp->datestamp = atoi(line);
    /* skip past blanks */
    while(isspace(*line)) line++;
    /* skip past number */
    while(isdigit(*line)) line++;
    /* skip past blanks */
    while(isspace(*line)) line++;

    len = strlen(line);
    if(len) line[len-1] = '\0';

    tp->label = alloc(len);
    strcpy(tp->label,line);

    return tp;
}


static tape_t *insert(list, tp)
tape_t *list, *tp;
{
    tape_t *prev, *cur;

    prev = NULL;
    cur = list;

    while(cur != NULL && cur->datestamp >= tp->datestamp) {
	prev = cur;
	cur = cur->next;
    }
    tp->next = cur;
    if(prev == NULL) list = tp;
    else prev->next = tp;

    return list;
}


static time_t stamp2time(datestamp)
int datestamp;
/*
 * Converts datestamp (an int of the form YYYYMMDD) into a real time_t value.
 * Since the datestamp contains no timezone or hh/mm/ss information, the
 * value is approximate.  This is ok for our purposes, since we round off
 * scheduling calculations to the nearest day.
 */
{
    struct tm tm;
    time_t now;

    now = time(0);
    tm = *localtime(&now);	/* initialize sec/min/hour & gmtoff */

    tm.tm_year = ( datestamp          / 10000) - 1900;
    tm.tm_mon  = ((datestamp % 10000) /   100) - 1;
    tm.tm_mday = ((datestamp %   100)        );

    return mktime(&tm);
}
