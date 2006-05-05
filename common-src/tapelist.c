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
 * $Id: tapelist.c,v 1.6 2006/05/05 14:37:10 martinea Exp $
 *
 * Support code for amidxtaped and amindexd.
 */

#include "amanda.h"
#include "tapelist.h"

/*
 * Count the number of entries in this tapelist
 */
int num_entries (tapelist)
tapelist_t *tapelist;
{
    tapelist_t *cur_tape;
    int count = 0;

    for(cur_tape = tapelist ; cur_tape ; cur_tape = cur_tape->next) count++;

    return(count);
}

/*
 * Add a tape entry with the given label to the given tapelist, creating a new
 * tapelist if handed a NULL one.  Squashes duplicates.
 */
tapelist_t *append_to_tapelist (tapelist, label, file, isafile)
tapelist_t *tapelist;
char *label;
int file, isafile;
{
    tapelist_t *new_tape, *cur_tape;
    int c;

    /* see if we have this tape already, and if so just add to its file list */
    for(cur_tape = tapelist; cur_tape; cur_tape = cur_tape->next){
	if(!strcmp(label, cur_tape->label)){
	    int d_idx = 0;
	    int *newfiles;
	    if(file >= 0){
		newfiles = alloc(sizeof(int) * (cur_tape->numfiles + 1));
		for(c = 0; c < cur_tape->numfiles ; c++){
		    if(cur_tape->files[c] > file && c == d_idx){
			newfiles[d_idx] = file;
			d_idx++;
		    }
		    newfiles[d_idx] = cur_tape->files[c];
		    d_idx++;
		}
		if(c == d_idx) newfiles[d_idx] = file;
		cur_tape->numfiles++;
		amfree(cur_tape->files);
		cur_tape->files = newfiles;
	    }
	    return(tapelist);
	}
    }

    new_tape = alloc(sizeof(tapelist_t));
    memset(new_tape, 0, sizeof(tapelist_t));
    new_tape->label = stralloc(label);
    if(file >= 0){
	new_tape->files = alloc(sizeof(int));
	new_tape->files[0] = file;
	new_tape->numfiles = 1;
	new_tape->isafile = isafile;
    }

    /* first instance of anything, start our tapelist with it */
    if(!tapelist){
	tapelist = new_tape;
	return(tapelist);
    }

    /* new tape, tack it onto the end of the list */
    for(cur_tape = tapelist; cur_tape->next; cur_tape = cur_tape->next);
    cur_tape->next = new_tape;

    return(tapelist);
}

/*
 * Backslash-escape all of the commas (and backslashes) in a label string.
 */
char *escape_label (label)
char *label;
{
    char *cooked_str, *temp_str;
    int s_idx = 0, d_idx = 0;

    if(!label) return(NULL);

    temp_str = alloc(strlen(label) * 2);

    do{
	if(label[s_idx] == ',' || label[s_idx] == '\\' ||
		label[s_idx] == ';' || label[s_idx] == ':'){
	    temp_str[d_idx] = '\\';
	    d_idx++;
	}
	temp_str[d_idx] = label[s_idx];
	s_idx++;
	d_idx++;
    } while(label[s_idx] != '\0');
    temp_str[d_idx] = '\0';

    cooked_str = stralloc(temp_str);
    amfree(temp_str);
    
    return(cooked_str);
}

/*
 * Strip out any escape characters (backslashes)
 */
char *unescape_label(label)
char *label;
{
    char *cooked_str, *temp_str;
    int s_idx = 0, d_idx = 0, prev_esc = 0;
    
    if(!label) return(NULL);

    temp_str = alloc(strlen(label));

    do{
	if(label[s_idx] == '\\' && !prev_esc){
	    s_idx++;
	    prev_esc = 1;
	    continue;
	}
	prev_esc = 0;
	temp_str[d_idx] = label[s_idx];
	s_idx++;
	d_idx++;
    } while(label[s_idx] != '\0');
    temp_str[d_idx] = '\0';

    cooked_str = stralloc(temp_str);
    amfree(temp_str);
    
    return(cooked_str);
}

/*
 * Convert a tapelist into a parseable string of tape labels and file numbers.
 */
char *marshal_tapelist (tapelist, do_escape)
tapelist_t *tapelist;
int do_escape;
{
    tapelist_t *cur_tape;
    char *str = NULL;

    for(cur_tape = tapelist; cur_tape; cur_tape = cur_tape->next){
	char *esc_label;
	char *files_str = NULL;
	int c;

	if(do_escape) esc_label = escape_label(cur_tape->label);
	else esc_label = stralloc(cur_tape->label);

	for(c = 0; c < cur_tape->numfiles ; c++){
	    char num_str[NUM_STR_SIZE];
	    snprintf(num_str, sizeof(num_str), "%d", cur_tape->files[c]);
	    if(!files_str) files_str = stralloc(num_str);
	    else files_str = vstralloc(files_str, ",", num_str, NULL);
	}

	if(!str) str = vstralloc(esc_label, ":", files_str, NULL);
	else str = vstralloc(str, ";", esc_label, ":", files_str, NULL);

	amfree(esc_label);
	amfree(files_str);
    }

    return(str);
}

/*
 * Convert a previously str-ified and escaped list of tapes back into a
 * tapelist structure.
 */
tapelist_t *unmarshal_tapelist_str (tapelist_str)
char *tapelist_str;
{
    char *temp_label, *temp_filenum;
    int l_idx, n_idx, input_length;
    tapelist_t *tapelist = NULL;

    if(!tapelist_str) return(NULL);

    input_length = strlen(tapelist_str);

    temp_label = alloc(input_length+1);
    temp_filenum = alloc(input_length+1);

    do{
	/* first, read the label part */
	memset(temp_label, '\0', input_length+1);
        l_idx = 0;
	while(*tapelist_str != ':' && *tapelist_str != '\0'){
	    if(*tapelist_str == '\\') tapelist_str++; /* skip escapes */
	    temp_label[l_idx] = *tapelist_str;
	    if(*tapelist_str == '\0') break; /* bad format, should kvetch */
	    tapelist_str++;
	    l_idx++;
	}
	if(*tapelist_str != '\0') tapelist_str++;
	tapelist = append_to_tapelist(tapelist, temp_label, -1, 0);

	/* now read the list of file numbers */
	while(*tapelist_str != ';' && *tapelist_str != '\0'){
	    int filenum = -1;
	    memset(temp_filenum, '\0', input_length+1);
	    n_idx = 0;
	    while(*tapelist_str != ';' && *tapelist_str != ',' &&
		    *tapelist_str != '\0'){
		temp_filenum[n_idx] = *tapelist_str; 
		tapelist_str++;
		n_idx++;
	    }
	    filenum = atoi(temp_filenum);

	    tapelist = append_to_tapelist(tapelist, temp_label, filenum, 0);
	    if(*tapelist_str != '\0' && *tapelist_str != ';') tapelist_str++;
	}
	if(*tapelist_str != '\0') tapelist_str++;

    } while(*tapelist_str != '\0');

    amfree(temp_label);
    amfree(temp_filenum);

    return(tapelist);
}

/*
 * Free up a list of tapes
 */
void free_tapelist (tapelist)
tapelist_t *tapelist;
{
    tapelist_t *cur_tape;
    tapelist_t *prev = NULL;

    for(cur_tape = tapelist ; cur_tape ; cur_tape = cur_tape->next){
	if(cur_tape->label) amfree(cur_tape->label);
	if(cur_tape->files) amfree(cur_tape->files);
	if(prev) amfree(prev);
	prev = cur_tape;
    }
    if(prev) amfree(prev);

}
