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
 * $Id: fileheader.c,v 1.2 1997/12/16 17:54:59 jrj Exp $
 *
 */

#include "amanda.h"
#include "fileheader.h"


void fh_init(file)
dumpfile_t *file;
{
    memset(file,'\0',sizeof(*file));
}


char *eatword(linep)
char **linep;
/*
 * Given a pointer into a character string, eatword advances the pointer
 * past the next word (possibly preceeded by whitespace) in the string.
 * The first space after the word is set to '\0'.  A pointer to the start
 * of the word is returned.
 */
{
    char *str, *lp;

    lp = *linep;

    while(*lp && isspace(*lp)) lp++;	/* eat leading whitespace */

    str = lp;
    while(*lp && !isspace(*lp)) lp++;	/* eat word */

    if(*lp) {				/* zero char after word, if needed */
	*lp = '\0';
	lp++;
    }
    *linep = lp;
    return str;
}


void parse_file_header(buffer, file, buflen)
char *buffer;
dumpfile_t *file;
int buflen;
{
    string_t line;
    char *lp, *bp, *str;
    int nchars;
    char *verify;

    /* isolate first line */

    nchars = buflen<sizeof(line)? buflen : sizeof(line) - 1;
    for(lp=line, bp=buffer; bp < buffer+nchars; bp++, lp++) {
	*lp = *bp;
	if(*bp == '\n') {
	    *lp = '\0';
	    break;
	}
    }
    line[STRMAX-1] = '\0';


    fh_init(file); 
    lp = line;
    str = eatword(&lp);
    if(strcmp(str, "NETDUMP:") && strcmp(str,"AMANDA:")) {
	file->type = F_UNKNOWN;
	return;
    }
   
    str = eatword(&lp);
    if(!strcmp(str, "TAPESTART")) {
	file->type = F_TAPESTART;
	verify=eatword(&lp);			/* ignore "DATE" */
	if(strcmp(verify,"DATE")) {
	    file->type=F_WEIRD;
	    return;
	}
	strncpy(file->datestamp, eatword(&lp), sizeof(file->datestamp)-1);
	file->datestamp[sizeof(file->datestamp)-1] = '\0';
	verify=eatword(&lp);			/* ignore "TAPE" */
	if(strcmp(verify,"TAPE")) {
	    file->type=F_WEIRD;
	    return;
	}
	strncpy(file->name, eatword(&lp), sizeof(file->name)-1);
	file->name[sizeof(file->name)-1] = '\0';
    }
    else if(!strcmp(str, "FILE")) {
	file->type = F_DUMPFILE;
	strncpy(file->datestamp, eatword(&lp), sizeof(file->datestamp)-1);
	file->datestamp[sizeof(file->datestamp)-1] = '\0';
	strncpy(file->name, eatword(&lp), sizeof(file->name)-1);
	file->name[sizeof(file->name)-1] = '\0';
	strncpy(file->disk, eatword(&lp), sizeof(file->disk)-1);
	file->disk[sizeof(file->disk)-1] = '\0';
	verify=eatword(&lp);			/* ignore "lev" */
	if(strcmp(verify,"lev")) {
	    file->type=F_WEIRD;
	    return;
	}
	file->dumplevel = atoi(eatword(&lp));
	verify=eatword(&lp);			/* ignore "comp" */
	if(strcmp(verify,"comp")) {
	    file->type=F_WEIRD;
	    return;
	}
	strncpy(file->comp_suffix, eatword(&lp), sizeof(file->comp_suffix)-1);
	file->comp_suffix[sizeof(file->comp_suffix)-1] = '\0';
	file->compressed = strcmp(file->comp_suffix, "N");
	/* compatibility with pre-2.2 amanda */
	if(!strcmp(file->comp_suffix, "C")) {
	    strncpy(file->comp_suffix, ".Z", sizeof(file->comp_suffix)-1);
	    file->comp_suffix[sizeof(file->comp_suffix)-1] = '\0';
	}
	verify=eatword(&lp);			/* ignore "program" */
	if(strcmp(verify,"program")) {		/* "program" is optionnal */
	    return;
	}
	strncpy(file->program, eatword(&lp), sizeof(file->program)-1);
	file->program[sizeof(file->program)-1] = '\0';
	if(file->program[0]=='\0') {
	    strncpy(file->program, "RESTORE", sizeof(file->program)-1);
	    file->program[sizeof(file->program)-1] = '\0';
	}
    }
    else if(!strcmp(str, "TAPEEND")) {
	file->type = F_TAPEEND;
	verify=eatword(&lp);				/* ignore "DATE" */
	if(strcmp(verify,"DATE")) {
	    file->type=F_WEIRD;
	    return;
	}
	strncpy(file->datestamp, eatword(&lp), sizeof(file->datestamp)-1);
	file->datestamp[sizeof(file->datestamp)-1] = '\0';
    }
    else {
	fprintf(stderr, "%s: strange amanda header: \"%s\"\n", pname, line);
	file->type = F_WEIRD;
	return;
    }
}


void write_header(buffer, file, buflen)
char *buffer;
dumpfile_t *file;
int buflen;
{
    char line[256];
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
		      buffer[sizeof(buffer)-1] = '\0';
		      strncat(buffer,
			"To restore, position tape at start of file and run:\n",
			sizeof(buffer)-strlen(buffer));
		      ap_snprintf(line, sizeof(line),
				  "\tdd if=<tape> bs=%dk skip=1 |%s %s\n\014\n",
				  TAPE_BLOCK_SIZE, file->uncompress_cmd,
				  file->recover_cmd);
		      line[sizeof(line)-1] = '\0';
		      strncat(buffer, line, sizeof(line)-strlen(line));
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
	if(strcmp(file->program,"")) 
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
    if(!strcmp(file->comp_suffix, ".Z"))
	return 1;
#ifdef HAVE_GZIP
    if(!strcmp(file->comp_suffix, ".gz"))
	return 1;
#endif
    return 0;
}

