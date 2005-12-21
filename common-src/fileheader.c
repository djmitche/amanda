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
 * $Id: fileheader.c,v 1.32 2005/12/21 19:07:49 paddy_s Exp $
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
    file->blocksize = DISK_BLOCK_BYTES;
}

void
parse_file_header(buffer, file, buflen)
    const char *buffer;
    dumpfile_t *file;
    size_t buflen;
{
    char *buf, *line, *tok, *line1=NULL;
    int lsize;
    /* put the buffer into a writable chunk of memory and nul-term it */
    buf = alloc(buflen + 1);
    memcpy(buf, buffer, buflen);
    buf[buflen] = '\0';

    fh_init(file); 

    for(line=buf,lsize=0; *line != '\n' && lsize < buflen; line++) {lsize++;};
    *line = '\0';
    line1 = alloc(lsize+1);
    strncpy(line1,buf,lsize);
    line1[lsize] = '\0';
    *line = '\n';

    tok = strtok(line1, " ");
    if (tok == NULL)
	goto weird_header;
    if (strcmp(tok, "NETDUMP:") != 0 && strcmp(tok, "AMANDA:") != 0) {
	amfree(buf);
	file->type = F_UNKNOWN;
	amfree(line1);
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

	tok = strtok(NULL, " ");
	if (tok == NULL)
	    goto weird_header;
	strncpy(file->name, tok, sizeof(file->name) - 1);
	break;

    case F_DUMPFILE:
    case F_CONT_DUMPFILE:
    case F_SPLIT_DUMPFILE:
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
	
	if(file->type == F_SPLIT_DUMPFILE){
	    tok = strtok(NULL, " ");
	    if (tok == NULL || strcmp(tok, "part") != 0)
		goto weird_header;

	    tok = strtok(NULL, "/");
	    if (tok == NULL || sscanf(tok, "%d", &file->partnum) != 1)
		goto weird_header;

	    tok = strtok(NULL, " ");
	    if (tok == NULL)
		goto weird_header;
	    if(strcmp(tok, "UNKNOWN") == 0){
		file->totalparts = -1;
	    } else if(sscanf(tok, "%d", &file->totalparts) != 1){
		goto weird_header;
	    }
	}
	

	tok = strtok(NULL, " ");
	if (tok == NULL || strcmp(tok, "lev") != 0)
	    goto weird_header;

	tok = strtok(NULL, " ");
	if (tok == NULL || sscanf(tok, "%d", &file->dumplevel) != 1)
	    goto weird_header;

	tok = strtok(NULL, " ");
	if (tok == NULL || strcmp(tok, "comp") != 0)
	    goto weird_header;

	tok = strtok(NULL, " ");
	if (tok == NULL)
	    goto weird_header;
	strncpy(file->comp_suffix, tok, sizeof(file->comp_suffix) - 1);

	file->compressed = strcmp(file->comp_suffix, "N");
	/* compatibility with pre-2.2 amanda */
	if (strcmp(file->comp_suffix, "C") == 0)
	    strncpy(file->comp_suffix, ".Z", sizeof(file->comp_suffix) - 1);
	       
	tok = strtok(NULL, " ");
        /* "program" is optional */
        if (tok == NULL || strcmp(tok, "program") != 0)
            return;

        tok = strtok(NULL, " ");
        if (tok == NULL)
            goto weird_header;
        strncpy(file->program, tok, sizeof(file->program) - 1);
        if (file->program[0] == '\0')
            strncpy(file->program, "RESTORE", sizeof(file->program) - 1);

	if ((tok = strtok(NULL, " ")) == NULL)
             break;          /* reach the end of the buf */

	/* "encryption" is optional */
	if (BSTRNCMP(tok, "crypt") == 0) {
	    tok = strtok(NULL, " ");
	    if (tok == NULL)
		goto weird_header;
	    strncpy(file->encrypt_suffix, tok,
		    sizeof(file->encrypt_suffix) - 1);
	    file->encrypted = BSTRNCMP(file->encrypt_suffix, "N");
	    if ((tok = strtok(NULL, " ")) == NULL)
		break;
	}

	/* "srvcompprog" is optional */
	if (BSTRNCMP(tok, "server_custom_compress") == 0) {
	    tok = strtok(NULL, " ");
	    if (tok == NULL)
		goto weird_header;
	    strncpy(file->srvcompprog, tok, sizeof(file->srvcompprog) - 1);
	    if ((tok = strtok(NULL, " ")) == NULL)
		break;      
	}

	/* "clntcompprog" is optional */
	if (BSTRNCMP(tok, "client_custom_compress") == 0) {
	    tok = strtok(NULL, " ");
	    if (tok == NULL)
		goto weird_header;
	    strncpy(file->clntcompprog, tok, sizeof(file->clntcompprog) - 1);
	    if ((tok = strtok(NULL, " ")) == NULL)
		break;
	}

	/* "srv_encrypt" is optional */
	if (BSTRNCMP(tok, "server_encrypt") == 0) {
	    tok = strtok(NULL, " ");
	    if (tok == NULL)
		goto weird_header;
	    strncpy(file->srv_encrypt, tok, sizeof(file->srv_encrypt) - 1);
	    if ((tok = strtok(NULL, " ")) == NULL) 
		break;
	}

	/* "clnt_encrypt" is optional */
	if (BSTRNCMP(tok, "client_encrypt") == 0) {
	    tok = strtok(NULL, " ");
	    if (tok == NULL)
		goto weird_header;
	    strncpy(file->clnt_encrypt, tok, sizeof(file->clnt_encrypt) - 1);
	    if ((tok = strtok(NULL, " ")) == NULL) 
		break;
	}

	/* "srv_decrypt_opt" is optional */
	if (BSTRNCMP(tok, "server_decrypt_option") == 0) {
	    tok = strtok(NULL, " ");
	    if (tok == NULL)
		goto weird_header;
	    strncpy(file->srv_decrypt_opt, tok,
		    sizeof(file->srv_decrypt_opt) - 1);
	    if ((tok = strtok(NULL, " ")) == NULL) 
		break;
	}

	/* "clnt_decrypt_opt" is optional */
	if (BSTRNCMP(tok, "client_decrypt_option") == 0) {
	    tok = strtok(NULL, " ");
	    if (tok == NULL)
		goto weird_header;
	    strncpy(file->clnt_decrypt_opt, tok,
		    sizeof(file->clnt_decrypt_opt) - 1);
	    if ((tok = strtok(NULL, " ")) == NULL) 
		break;
	}
      break;
      

    case F_TAPEEND:
	tok = strtok(NULL, " ");
	/* DATE is optional */
	if (tok == NULL || strcmp(tok, "DATE") != 0)
	    return;
	strncpy(file->datestamp, tok, sizeof(file->datestamp) - 1);
	break;

    default:
	goto weird_header;
    }

    line = strtok(buf, "\n"); /* this is the first line */
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
	    char *cmd1=NULL, *cmd2=NULL, *cmd3=NULL;

	    /* skip over dd command */
	    if ((cmd1 = strchr(line, '|')) == NULL) {

	        strncpy(file->recover_cmd, "BUG",
		        sizeof(file->recover_cmd) - 1);
	        continue;
	    }
	    *cmd1++ = '\0';

	    /* block out first pipeline command */
	    if ((cmd2 = strchr(cmd1, '|')) != NULL) {
	      *cmd2++ = '\0';
	      if ((cmd3 = strchr(cmd2, '|')) != NULL)
		*cmd3++ = '\0';
	    }
	   
	    /* three cmds: decrypt    | uncompress | recover
	     * two   cmds: uncompress | recover
	     * XXX note that if there are two cmds, the first one 
	     * XXX could be either uncompress or decrypt. Since no
	     * XXX code actually call uncompress_cmd/decrypt_cmd, it's ok
	     * XXX for header information.
	     * one   cmds: recover
	     */

	    if (cmd3 == NULL) {
	      if (cmd2 == NULL) {
		strncpy(file->recover_cmd, cmd1,
			sizeof(file->recover_cmd) - 1);
	      } else {
		snprintf(file->uncompress_cmd,
			 sizeof(file->uncompress_cmd), "%s|", cmd1);
		strncpy(file->recover_cmd, cmd2,
			sizeof(file->recover_cmd) - 1);
	      }
	    } else {    /* cmd3 presents:  decrypt | uncompress | recover */
	      snprintf(file->decrypt_cmd,
		       sizeof(file->decrypt_cmd), "%s|", cmd1);
	      snprintf(file->uncompress_cmd,
		       sizeof(file->uncompress_cmd), "%s|", cmd2);
	      strncpy(file->recover_cmd, cmd3,
		      sizeof(file->recover_cmd) - 1);
	    }
	    continue;
	}
#undef SC
	/* XXX complain about weird lines? */
    }
    amfree(buf);
    amfree(line1);
    return;

weird_header:
    fprintf(stderr, "%s: strange amanda header: \"%.*s\"\n", get_pname(),
	(int) buflen, buffer);
    file->type = F_WEIRD;
    amfree(buf);
    amfree(line1);
}

void
build_header(buffer, file, buflen)
    char *buffer;
    const dumpfile_t *file;
    size_t buflen;
{
    int n;
    char split_data[128] = "";

    memset(buffer,'\0',buflen);

    switch (file->type) {
    case F_TAPESTART:
	snprintf(buffer, buflen,
	    "AMANDA: TAPESTART DATE %s TAPE %s\n014\n",
	    file->datestamp, file->name);
	break;

    case F_SPLIT_DUMPFILE:
	if(file->totalparts > 0) {
	    snprintf(split_data, sizeof(split_data),
		     " part %d/%d ", file->partnum, file->totalparts);
	} else {
	    snprintf(split_data, sizeof(split_data),
		     " part %d/%d ", file->partnum, file->totalparts);
	}
    /* FALLTHROUGH */
	
    case F_CONT_DUMPFILE:
    case F_DUMPFILE :
        n = snprintf(buffer, buflen,
                     "AMANDA: %s %s %s %s %s lev %d comp %s program %s",
			 filetype2str(file->type),
			 file->datestamp, file->name, file->disk,
			 split_data,
		         file->dumplevel, file->comp_suffix, file->program); 
	if ( n ) {
	  buffer += n;
	  buflen -= n;
	  n = 0;
	}
     
	if (strcmp(file->encrypt_suffix, "enc") == 0) {  /* only output crypt if it's enabled */
	  n = snprintf(buffer, buflen, " crypt %s", file->encrypt_suffix);
	}
	if ( n ) {
	  buffer += n;
	  buflen -= n;
	  n = 0;
	}

	if (*file->srvcompprog) {
	    n = snprintf(buffer, buflen, " server_custom_compress %s", file->srvcompprog);
	} else if (*file->clntcompprog) {
	    n = snprintf(buffer, buflen, " client_custom_compress %s", file->clntcompprog);
	} 

	if ( n ) {
	  buffer += n;
	  buflen -= n;
	  n = 0;
	}

	if (*file->srv_encrypt) {
	    n = snprintf(buffer, buflen, " server_encrypt %s", file->srv_encrypt);
	} else if (*file->clnt_encrypt) {
	    n = snprintf(buffer, buflen, " client_encrypt %s", file->clnt_encrypt);
	} 

	if ( n ) {
	  buffer += n;
	  buflen -= n;
	  n = 0;
	}
	
	if (*file->srv_decrypt_opt) {
	    n = snprintf(buffer, buflen, " server_decrypt_option %s", file->srv_decrypt_opt);
	} else if (*file->clnt_decrypt_opt) {
	    n = snprintf(buffer, buflen, " client_decrypt_option %s", file->clnt_decrypt_opt);
	} 

	if ( n ) {
	  buffer += n;
	  buflen -= n;
	  n = 0;
	}

	n = snprintf(buffer, buflen, "\n");
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
	    "\tdd if=<tape> bs=%ldk skip=1 |%s %s %s\n\014\n",
	    file->blocksize / 1024, file->decrypt_cmd, file->uncompress_cmd, file->recover_cmd);
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
    char number[NUM_STR_SIZE*2];
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
	if (*file->program)
	    fprintf(outf, " program %s",file->program);
	if (strcmp(file->encrypt_suffix, "enc") == 0)
	    fprintf(outf, " crypt %s", file->encrypt_suffix);
	if (*file->srvcompprog)
	    fprintf(outf, " server_custom_compress %s", file->srvcompprog);
	if (*file->clntcompprog)
	    fprintf(outf, " client_custom_compress %s", file->clntcompprog);
	if (*file->srv_encrypt)
	    fprintf(outf, " server_encrypt %s", file->srv_encrypt);
	if (*file->clnt_encrypt)
	    fprintf(outf, " client_encrypt %s", file->clnt_encrypt);
	if (*file->srv_decrypt_opt)
	    fprintf(outf, " server_decrypt_option %s", file->srv_decrypt_opt);
	if (*file->clnt_decrypt_opt)
	    fprintf(outf, " client_decrypt_option %s", file->clnt_decrypt_opt);
	fprintf(outf, "\n");
	break;
    case F_SPLIT_DUMPFILE:
        if(file->totalparts > 0){
            snprintf(number, sizeof(number), "%d", file->totalparts);
        }   
        else snprintf(number, sizeof(number), "UNKNOWN");
        fprintf(outf, "split dumpfile: date %s host %s disk %s part %d/%s lev %d comp %s",
                      file->datestamp, file->name, file->disk, file->partnum,
                      number, file->dumplevel, file->comp_suffix);
        if (*file->program)
            fprintf(outf, " program %s",file->program);
	if (strcmp(file->encrypt_suffix, "enc") == 0)
	    fprintf(outf, " crypt %s", file->encrypt_suffix);
	if (*file->srvcompprog)
	    fprintf(outf, " server_custom_compress %s", file->srvcompprog);
	if (*file->clntcompprog)
	    fprintf(outf, " client_custom_compress %s", file->clntcompprog);
	if (*file->srv_encrypt)
	    fprintf(outf, " server_encrypt %s", file->srv_encrypt);
	if (*file->clnt_encrypt)
	    fprintf(outf, " client_encrypt %s", file->clnt_encrypt);
	if (*file->srv_decrypt_opt)
	    fprintf(outf, " server_decrypt_option %s", file->srv_decrypt_opt);
	if (*file->clnt_decrypt_opt)
	    fprintf(outf, " client_decrypt_option %s", file->clnt_decrypt_opt);
        fprintf(outf, "\n");
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
    if(strcmp(file->comp_suffix, "cust") == 0)
	return 1;
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
    { F_SPLIT_DUMPFILE, "SPLIT_FILE" }
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
