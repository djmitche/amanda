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
 * $Id: fileheader.c,v 1.17 1999/06/02 21:42:47 kashmir Exp $
 */

#include "amanda.h"
#include "fileheader.h"

static const char *filetype2str P((filetype_t));
static filetype_t str2filetype P((const char *));

void
fh_init(file)
    dumpfile_t *file;
{
    memset(file, '\0', sizeof(*file));
}

void
parse_file_header(buffer, file, buflen)
    const char *buffer;
    dumpfile_t *file;
    int buflen;
{
    char *buf, *line, *tok;

    /* put the buffer into a writable chunk of memory and nul-term it */
    buf = alloc(buflen + 1);
    memcpy(buf, buffer, buflen);
    buf[buflen] = '\0';

    fh_init(file); 

    tok = strtok(buf, " ");
    if (tok == NULL)
	goto weird_header;
    if (strcmp(tok, "NETDUMP:") != 0 && strcmp(tok, "AMANDA:") != 0) {
	amfree(buf);
	file->type = F_UNKNOWN;
	return;
    }

    tok = strtok(NULL, " ");
    if (tok == NULL)
	goto weird_header;
    file->type = str2filetype(tok);

    switch (file->type) {
    case F_TAPESTART:
	tok = strtok(NULL, " ");
	if (tok == NULL || strcmp(tok, "DATE") != 0)
	    goto weird_header;

	tok = strtok(NULL, " ");
	if (tok == NULL)
	    goto weird_header;
	strncpy(file->datestamp, tok, sizeof(file->datestamp) - 1);

	tok = strtok(NULL, " ");
	if (tok == NULL || strcmp(tok, "TAPE") != 0)
	    goto weird_header;

	tok = strtok(NULL, " \n");
	if (tok == NULL)
	    goto weird_header;
	strncpy(file->name, tok, sizeof(file->name) - 1);
	break;

    case F_DUMPFILE:
    case F_CONT_DUMPFILE:
	tok = strtok(NULL, " ");
	if (tok == NULL)
	    goto weird_header;
	strncpy(file->datestamp, tok, sizeof(file->datestamp) - 1);

	tok = strtok(NULL, " ");
	if (tok == NULL)
	    goto weird_header;
	strncpy(file->name, tok, sizeof(file->name) - 1);

	tok = strtok(NULL, " ");
	if (tok == NULL)
	    goto weird_header;
	strncpy(file->disk, tok, sizeof(file->disk) - 1);

	tok = strtok(NULL, " ");
	if (tok == NULL || strcmp(tok, "lev") != 0)
	    goto weird_header;

	tok = strtok(NULL, " ");
	if (tok == NULL || sscanf(tok, "%d", &file->dumplevel) != 1)
	    goto weird_header;

	tok = strtok(NULL, " ");
	if (tok == NULL || strcmp(tok, "comp") != 0)
	    goto weird_header;

	tok = strtok(NULL, " \n");
	if (tok == NULL)
	    goto weird_header;
	strncpy(file->comp_suffix, tok, sizeof(file->comp_suffix) - 1);

	file->compressed = strcmp(file->comp_suffix, "N");
	/* compatibility with pre-2.2 amanda */
	if (strcmp(file->comp_suffix, "C") == 0)
	    strncpy(file->comp_suffix, ".Z", sizeof(file->comp_suffix) - 1);

	tok = strtok(NULL, " \n");
	/* "program" is optional */
	if (tok == NULL || strcmp(tok, "program") != 0)
	    return;

	tok = strtok(NULL, " \n");
	if (tok == NULL)
	    goto weird_header;
	strncpy(file->program, tok, sizeof(file->program) - 1);

	if (file->program[0] == '\0')
	    strncpy(file->program, "RESTORE", sizeof(file->program) - 1);

	/* iterate through the rest of the lines */
	while ((line = strtok(NULL, "\n")) != NULL) {

#define SC "CONT_FILENAME="
	    if (strncmp(line, SC, sizeof(SC) - 1) == 0) {
		line += sizeof(SC) - 1;
		strncpy(file->cont_filename, line,
		    sizeof(file->cont_filename) - 1);
		continue;
	    }
#undef SC

#define SC "PARTIAL="
	    if (strncmp(line, SC, sizeof(SC) - 1) == 0) {
		line += sizeof(SC) - 1;
		file->is_partial = !strcasecmp(line, "yes");
		continue;
	    }
#undef SC

#define SC "To restore, position tape at start of file and run:"
	    if (strncmp(line, SC, sizeof(SC) - 1) == 0)
		continue;
#undef SC

#define SC "\tdd if=<tape> bs="
	    if (strncmp(line, SC, sizeof(SC) - 1) == 0) {
		char *cmd1, *cmd2;

		/* skip over dd command */
		if ((cmd1 = strchr(line, '|')) == NULL) {

		    strncpy(file->recover_cmd, "BUG",
			sizeof(file->recover_cmd) - 1);
		    continue;
		}
		cmd1++;

		/* block out first pipeline command */
		if ((cmd2 = strchr(cmd1, '|')) != NULL)
		    *cmd2++ = '\0';

		/*
		 * If there's a second cmd then the first is the uncompress cmd.
		 * Otherwise, the first cmd is the recover cmd, and there
		 * is no uncompress cmd.
		 */
		if (cmd2 == NULL) {
		    strncpy(file->recover_cmd, cmd1,
			sizeof(file->recover_cmd) - 1);
		} else {
		    snprintf(file->uncompress_cmd,
			sizeof(file->uncompress_cmd), "%s|", cmd1);
		    strncpy(file->recover_cmd, cmd2,
			sizeof(file->recover_cmd) - 1);
		}
		continue;
	    }
#undef SC
	    /* XXX complain about weird lines? */
	}
	break;

    case F_TAPEEND:
	tok = strtok(NULL, " \n");
	/* DATE is optional */
	if (tok == NULL || strcmp(tok, "DATE") != 0)
	    return;
	strncpy(file->datestamp, tok, sizeof(file->datestamp) - 1);
	break;

    default:
	goto weird_header;
    }

    amfree(buf);
    return;

weird_header:
    fprintf(stderr, "%s: strange amanda header: \"%.*s\"\n", get_pname(),
	buflen, buffer);
    file->type = F_WEIRD;
    amfree(buf);
}

void
write_header(buffer, file, buflen)
    char *buffer;
    const dumpfile_t *file;
    int buflen;
{
    int n;

    memset(buffer,'\0',buflen);

    switch (file->type) {
    case F_TAPESTART:
	snprintf(buffer, buflen, "AMANDA: TAPESTART DATE %s TAPE %s\n\014\n",
	    file->datestamp, file->name);
	break;

    case F_CONT_DUMPFILE:
    case F_DUMPFILE :
	n = snprintf(buffer, buflen,
	    "AMANDA: %s %s %s %s lev %d comp %s program %s\n",
	    filetype2str(file->type), file->datestamp, file->name, file->disk,
	    file->dumplevel, file->comp_suffix, file->program);
	buffer += n;
	buflen -= n;

	if (file->cont_filename[0] != '\0') {
	    n = snprintf(buffer, buflen, "CONT_FILENAME=%s\n",
		file->cont_filename);
	    buffer += n;
	    buflen -= n;
	}
	if (file->is_partial != 0) {
	    n = snprintf(buffer, buflen, "PARTIAL=YES\n");
	    buffer += n;
	    buflen -= n;
	}
	n = snprintf(buffer, buflen, 
	    "To restore, position tape at start of file and run:\n");
	buffer += n;
	buflen -= n;

	/* \014 == ^L */
	n = snprintf(buffer, buflen,
	    "\tdd if=<tape> bs=%dk skip=1 |%s %s\n\014\n",
	    TAPE_BLOCK_SIZE, file->uncompress_cmd, file->recover_cmd);
	buffer += n;
	buflen -= n;
	break;

    case F_TAPEEND:
	snprintf(buffer, buflen, "AMANDA: TAPEEND DATE %s\n\014\n",
	    file->datestamp);
	break;

    case F_UNKNOWN:
    case F_WEIRD:
	break;
    }
}

/*
 * Prints the contents of the file structure.
 */
void
print_header(outf, file)
    FILE *outf;
    const dumpfile_t *file;
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
    case F_CONT_DUMPFILE:
	fprintf(outf, "%s: date %s host %s disk %s lev %d comp %s",
	    filetype2str(file->type), file->datestamp, file->name,
	    file->disk, file->dumplevel, file->comp_suffix);
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

int
known_compress_type(file)
    const dumpfile_t *file;
{
    if(strcmp(file->comp_suffix, ".Z") == 0)
	return 1;
#ifdef HAVE_GZIP
    if(strcmp(file->comp_suffix, ".gz") == 0)
	return 1;
#endif
    return 0;
}

static const struct {
    filetype_t type;
    const char *str;
} filetypetab[] = {
    { F_UNKNOWN, "UNKNOWN" },
    { F_WEIRD, "WEIRD" },
    { F_TAPESTART, "TAPESTART" },
    { F_TAPEEND,  "TAPEEND" },
    { F_DUMPFILE, "FILE" },
    { F_CONT_DUMPFILE, "CONT_FILE" },
};
#define	NFILETYPES	(sizeof(filetypetab) / sizeof(filetypetab[0]))

static const char *
filetype2str(type)
    filetype_t type;
{
    int i;

    for (i = 0; i < NFILETYPES; i++)
	if (filetypetab[i].type == type)
	    return (filetypetab[i].str);
    return ("UNKNOWN");
}

static filetype_t
str2filetype(str)
    const char *str;
{
    int i;

    for (i = 0; i < NFILETYPES; i++)
	if (strcmp(filetypetab[i].str, str) == 0)
	    return (filetypetab[i].type);
    return (F_UNKNOWN);
}
