/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1993 University of Maryland
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
 * $Id: fileheader.c,v 1.5.2.2 1998/04/11 13:10:36 martinea Exp $
 *
 */

#include "amanda.h"
#include "fileheader.h"


void fh_init(file)
dumpfile_t *file;
{
    memset(file,'\0',sizeof(*file));
}


void parse_file_header(buffer, file, buflen)
char *buffer;
dumpfile_t *file;
int buflen;
{
    string_t line, save_line;
    char *bp, *str;
    int nchars;
    char *verify;
    char *s;
    int ch;

    /* isolate first line */

    nchars = buflen<sizeof(line)? buflen : sizeof(line) - 1;
    for(s=line, bp=buffer; bp < buffer+nchars; bp++, s++) {
	ch = *bp;
	if(ch == '\n') {
	    *s = '\0';
	    break;
	}
	*s = ch;
    }
    line[sizeof(line)-1] = '\0';
    strncpy(save_line, line, sizeof(save_line));

    fh_init(file); 
    s = line;
    ch = *s++;

    skip_whitespace(s, ch);
    str = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    if(strcmp(str, "NETDUMP:") != 0 && strcmp(str,"AMANDA:") != 0) {
	file->type = F_UNKNOWN;
	return;
    }

    skip_whitespace(s, ch);
    if(ch == '\0') {
	goto weird_header;
    }
    str = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';

    if(strcmp(str, "TAPESTART") == 0) {
	file->type = F_TAPESTART;

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto weird_header;
	}
	verify = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';
	if(strcmp(verify, "DATE") != 0) {
	    goto weird_header;
	}

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto weird_header;
	}
	copy_string(s, ch, file->datestamp, sizeof(file->datestamp), bp);
	if(bp == NULL) {
	    goto weird_header;
	}

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto weird_header;
	}
	verify = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';
	if(strcmp(verify, "TAPE") != 0) {
	    goto weird_header;
	}

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto weird_header;
	}
	copy_string(s, ch, file->name, sizeof(file->name), bp);
	if(bp == NULL) {
	    goto weird_header;
	}
    } else if(strcmp(str, "FILE") == 0) {
	file->type = F_DUMPFILE;

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto weird_header;
	}
	copy_string(s, ch, file->datestamp, sizeof(file->datestamp), bp);
	if(bp == NULL) {
	    goto weird_header;
	}

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto weird_header;
	}
	copy_string(s, ch, file->name, sizeof(file->name), bp);
	if(bp == NULL) {
	    goto weird_header;
	}

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto weird_header;
	}
	copy_string(s, ch, file->disk, sizeof(file->disk), bp);
	if(bp == NULL) {
	    goto weird_header;
	}

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto weird_header;
	}
	verify = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';
	if(strcmp(verify, "lev") != 0) {
	    goto weird_header;
	}

	skip_whitespace(s, ch);
	if(ch == '\0' || sscanf(s - 1, "%d", &file->dumplevel) != 1) {
	    goto weird_header;
	}
	skip_integer(s, ch);

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto weird_header;
	}
	verify = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';
	if(strcmp(verify, "comp") != 0) {
	    goto weird_header;
	}

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto weird_header;
	}
	copy_string(s, ch, file->comp_suffix, sizeof(file->comp_suffix), bp);
	if(bp == NULL) {
	    goto weird_header;
	}

	file->compressed = strcmp(file->comp_suffix, "N");
	/* compatibility with pre-2.2 amanda */
	if(strcmp(file->comp_suffix, "C") == 0) {
	    strncpy(file->comp_suffix, ".Z", sizeof(file->comp_suffix)-1);
	    file->comp_suffix[sizeof(file->comp_suffix)-1] = '\0';
	}

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto weird_header;
	}
	verify = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';
	if(strcmp(verify, "program") != 0) {
	    return;				/* "program" is optional */
	}

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto weird_header;
	}
	copy_string(s, ch, file->program, sizeof(file->program), bp);
	if(bp == NULL) {
	    goto weird_header;
	}

	if(file->program[0]=='\0') {
	    strncpy(file->program, "RESTORE", sizeof(file->program)-1);
	    file->program[sizeof(file->program)-1] = '\0';
	}
    } else if(strcmp(str, "TAPEEND") == 0) {
	file->type = F_TAPEEND;

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto weird_header;
	}
	verify = s - 1;
	skip_non_whitespace(s, ch);
	s[-1] = '\0';
	if(strcmp(verify, "DATE") != 0) {
	    return;				/* "program" is optional */
	}

	skip_whitespace(s, ch);
	if(ch == '\0') {
	    goto weird_header;
	}
	copy_string(s, ch, file->datestamp, sizeof(file->datestamp), bp);
	if(bp == NULL) {
	    goto weird_header;
	}
    } else {
	goto weird_header;
    }
    return;

 weird_header:

    fprintf(stderr, "%s: strange amanda header: \"%s\"\n", pname, save_line);
    file->type = F_WEIRD;
    return;
}


void write_header(buffer, file, buflen)
char *buffer;
dumpfile_t *file;
int buflen;
{
    char *line = NULL;
    char number[NUM_STR_SIZE];

    memset(buffer,'\0',buflen);

    switch (file->type) {
    case F_TAPESTART: ap_snprintf(buffer, buflen,
				  "AMANDA: TAPESTART DATE %s TAPE %s\n\014\n",
				  file->datestamp, file->name);
		      break;
    case F_DUMPFILE : ap_snprintf(buffer, buflen,
				  "AMANDA: FILE %s %s %s lev %d comp %s program %s\n",
				  file->datestamp, file->name, file->disk,
				  file->dumplevel, file->comp_suffix,
				  file->program);
		      buffer[buflen-1] = '\0';
		      strncat(buffer,
			"To restore, position tape at start of file and run:\n",
			buflen-strlen(buffer));
		      ap_snprintf(number, sizeof(number),
				  "%d", TAPE_BLOCK_SIZE);
		      line = vstralloc("\t",
				       "dd",
				       " if=<tape>",
				       " bs=", number, "k",
				       " skip=1",
				       " |", file->uncompress_cmd,
				       " ", file->recover_cmd,
				       "\n",
				       "\014\n",	/* ?? */
				       NULL);
		      strncat(buffer, line, buflen-strlen(buffer));
		      amfree(line);
		      break;
    case F_TAPEEND  : ap_snprintf(buffer, buflen,
				  "AMANDA: TAPEEND DATE %s\n\014\n",
				  file->datestamp);
		      break;
    case F_UNKNOWN  : break;
    case F_WEIRD    : break;
    }
}


void print_header(outf, file)
FILE *outf;
dumpfile_t *file;
/*
 * Prints the contents of the file structure.
 */
{
    switch(file->type) {
    case F_UNKNOWN:
	fprintf(outf, "UNKNOWN file\n");
	break;
    case F_WEIRD:
	fprintf(outf, "WEIRD file\n");
	break;
    case F_TAPESTART:
	fprintf(outf, "start of tape: date %s label %s\n",
	       file->datestamp, file->name);
	break;
    case F_DUMPFILE:
	fprintf(outf, "dumpfile: date %s host %s disk %s lev %d comp %s",
		file->datestamp, file->name, file->disk, file->dumplevel, 
		file->comp_suffix);
	if(*file->program)
	    printf(" program %s\n",file->program);
	else
	    printf("\n");
	break;
    case F_TAPEEND:
	fprintf(outf, "end of tape: date %s\n", file->datestamp);
	break;
    }
}


int known_compress_type(file)
dumpfile_t *file;
{
    if(strcmp(file->comp_suffix, ".Z") == 0)
	return 1;
#ifdef HAVE_GZIP
    if(strcmp(file->comp_suffix, ".gz") == 0)
	return 1;
#endif
    return 0;
}
