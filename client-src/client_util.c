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
 * $Id: client_util.c,v 1.1.2.2 2002/02/13 20:54:15 martinea Exp $
 *
 */

#include "client_util.h"
#include "getfsent.h"

option_t *parse_options(str, disk, verbose)
char *str;
char *disk;
int verbose;
{
    char *exc;
    option_t *options;
    char *p, *tok;

    options = malloc(sizeof(option_t));
    options->str = stralloc(str);
    options->compress = NO_COMPR;
    options->no_record = 0;
    options->bsd_auth = 0;
    options->createindex = 0;
#ifdef KRB4_SECURITY
    options->krb4_auth = 0;
    options->kencrypt = 0;
#endif
    options->exclude_file = NULL;
    options->exclude_list = NULL;

    p = stralloc(str);
    tok = strtok(p,";");

    while (tok != NULL) {
	if(strcmp(tok, "compress-fast") == 0) {
	    options->compress = COMPR_FAST;
	}
	else if(strcmp(tok, "compress-best") == 0) {
	    options->compress = COMPR_BEST;
	}
	else if(strcmp(tok, "srvcomp-fast") == 0) {
	}
	else if(strcmp(tok, "srvcomp-best") == 0) {
	}
	else if(strcmp(tok, "no-record") == 0) {
	    options->no_record = 1;
	}
	else if(strcmp(tok, "bsd-auth") == 0) {
	    options->bsd_auth = 1;
	}
	else if(strcmp(tok, "index") == 0) {
	    options->createindex = 1;
	}
#ifdef KRB4_SECURITY
	else if(strcmp(tok, "krb4-auth") == 0) {
	    options->krb4_auth = 1;
	}
	else if(strcmp(tok, "kencrypt") == 0) {
	    options->kencrypt = 1;
	}
#endif
	else if(strncmp(tok,"exclude-file=", 13) == 0) {
	    exc = &tok[13];
	    options->exclude_file = append_sl(options->exclude_file,exc);
	}
	else if(strncmp(tok,"exclude-list=", 13) == 0) {
	    exc = &tok[13];
	    if(*exc != '/') {
		char *dirname = amname_to_dirname(disk);
		char *efile = vstralloc(dirname,"/",exc, NULL);
		if(access(efile, F_OK) != 0) {
		    /* if exclude list file does not exist, ignore it.
		     * Should not test for R_OK, because the file may be
		     * readable by root only! */
		    dbprintf(("%s: exclude list file \"%s\" does not exist, ignoring\n",
			      get_pname(), efile));
		    if(verbose) {
			printf("ERROR [exclude list file \"%s\" does not exist]\n", efile);
		    }
		}
		else {
		    options->exclude_list =
			append_sl(options->exclude_list, efile);
		}
		amfree(dirname);
		amfree(efile);
	    }
	    else {
		options->exclude_list = append_sl(options->exclude_list,exc);
	    }
	}
	else if(strcmp(tok,"|") == 0) {
	}
	else {
	    dbprintf(("%s: unknown option \"%s\"\n",
                                  get_pname(), tok));
	    if(verbose) {
		printf("ERROR [unknown option \"%s\"]\n", tok);
	    }
	}
	tok = strtok(NULL, ";");
    }
    return options;
}

