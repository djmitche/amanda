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
 * $Id: fileheader.c,v 1.11.4.1.4.1.2.5 2002/02/11 01:30:42 jrjackson Exp $
 *
 */

#include "amanda.h"
#include "fileheader.h"


void fh_init(file)
dumpfile_t *file;
{
    memset(file,'\0',sizeof(*file));
    file->blocksize = DISK_BLOCK_BYTES;
}


void
parse_file_header(buffer, file, buflen)
    char *buffer;
    dumpfile_t *file;
    size_t buflen;
{
    string_t line, save_line;
    char *bp, *str, *ptr_buf, *start_buf;
    int nchars;
    char *verify;
    char *s, *s1, *s2;
    int ch;
    int done;

    /* isolate first line */

    nchars = buflen<sizeof(line)? buflen : sizeof(line) - 1;
    for(s=line, ptr_buf=buffer; ptr_buf < buffer+nchars; ptr_buf++, s++) {
	ch = *ptr_buf;
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
    } else if(strcmp(str, "FILE") == 0 || 
	      strcmp(str, "CONT_FILE") == 0) {
	if(strcmp(str, "FILE") == 0)
	    file->type = F_DUMPFILE;
	else if(strcmp(str, "CONT_FILE") == 0)
	    file->type = F_CONT_DUMPFILE;

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
	    return;				/* "program" is optional */
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

    done=0;
    do {
	/* isolate the next line */
	int max_char;
	ptr_buf++;
	start_buf = ptr_buf;
	max_char = buflen - (ptr_buf - buffer);
	nchars = max_char<sizeof(line)? max_char : sizeof(line) - 1;
	for(s=line ; ptr_buf < start_buf+nchars; ptr_buf++, s++) {
	    ch = *ptr_buf;
	    if(ch == '\n') {
		*s = '\0';
		break;
	    }
	    else if(ch == '\0' || ch == '\014') {
		done=1;
		break;
	    }
	    *s = ch;
	}
	if (done == 1) break;
	if(ptr_buf >= start_buf+nchars) done = 1;
	line[sizeof(line)-1] = '\0';
	s = line;
	ch = *s++;
#define SC "CONT_FILENAME="
	if(strncmp(line,SC,strlen(SC)) == 0) {
	    s = line + strlen(SC);
	    ch = *s++;
	    copy_string(s, ch, file->cont_filename, 
			sizeof(file->cont_filename), bp);
	}
#undef SC
#define SC "PARTIAL="
	else if(strncmp(line,SC,strlen(SC)) == 0) {
	    s = line + strlen(SC);
	    if(strncmp(s,"yes",3)==0 || strncmp(s,"YES",3)==0)
		file->is_partial=1;
	    ch = *s++;
	}
#undef SC
#define SC "To restore, position tape at start of file and run:"
	else if(strncmp(line,SC,strlen(SC)) == 0) {
	}
#undef SC
#define SC "\tdd if=<tape> bs="
	else if(strncmp(line,SC,strlen(SC)) == 0) {
	    s = strtok(line, "|");
	    s1 = strtok(NULL, "|");
	    s2 = strtok(NULL, "|");
	    if(!s1) {
		strncpy(file->recover_cmd,"BUG",sizeof(file->recover_cmd));
		file->recover_cmd[sizeof(file->recover_cmd)-1] = '\0';
	    }
	    else if(!s2) {
		strncpy(file->recover_cmd,s1+1,sizeof(file->recover_cmd));
		file->recover_cmd[sizeof(file->recover_cmd)-1] = '\0';
	    }
	    else {
		strncpy(file->uncompress_cmd,s1, sizeof(file->uncompress_cmd));
		file->uncompress_cmd[sizeof(file->uncompress_cmd)-2] = '\0';
		strcat(file->uncompress_cmd,"|");
		strncpy(file->recover_cmd,s2+1,sizeof(file->recover_cmd));
		file->recover_cmd[sizeof(file->recover_cmd)-1] = '\0';
	    }
	}
#undef SC
	else { /* ignore unknown line */
	}
    } while(!done);

    return;

 weird_header:

    fprintf(stderr, "%s: strange amanda header: \"%s\"\n", get_pname(), save_line);
    file->type = F_WEIRD;
    return;
}


void
build_header(buffer, file, buflen)
    char *buffer;
    dumpfile_t *file;
    size_t buflen;
{
    char *line = NULL;
    char number[NUM_STR_SIZE*2];

    memset(buffer,'\0',buflen);

    switch (file->type) {
    case F_TAPESTART: ap_snprintf(buffer, buflen,
				  "AMANDA: TAPESTART DATE %s TAPE %s\n\014\n",
				  file->datestamp, file->name);
		      break;
    case F_CONT_DUMPFILE:
    case F_DUMPFILE : if( file->type == F_DUMPFILE) {
			ap_snprintf(buffer, buflen,
				  "AMANDA: FILE %s %s %s lev %d comp %s program %s\n",
				  file->datestamp, file->name, file->disk,
				  file->dumplevel, file->comp_suffix,
				  file->program);
		      }
		      else if( file->type == F_CONT_DUMPFILE) {
			ap_snprintf(buffer, buflen,
				  "AMANDA: CONT_FILE %s %s %s lev %d comp %s program %s\n",
				  file->datestamp, file->name, file->disk,
				  file->dumplevel, file->comp_suffix,
				  file->program);
		      }
		      buffer[buflen-1] = '\0';
		      if(strlen(file->cont_filename) != 0) {
			line = newvstralloc(line, "CONT_FILENAME=",
					    file->cont_filename, "\n", NULL);
		        strncat(buffer,line,buflen-strlen(buffer));
		      }
		      if(file->is_partial != 0) {
			strncat(buffer,"PARTIAL=YES\n",buflen-strlen(buffer));
		      }
		      strncat(buffer,
			"To restore, position tape at start of file and run:\n",
			buflen-strlen(buffer));
		      ap_snprintf(number, sizeof(number),
				  "%ld", file->blocksize / 1024);
		      line = newvstralloc(line, "\t",
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
		      buffer[buflen-1] = '\0';
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
    case F_CONT_DUMPFILE:
	fprintf(outf, "cont dumpfile: date %s host %s disk %s lev %d comp %s",
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
