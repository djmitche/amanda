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
 * $Id: client_util.c,v 1.4 2002/02/15 14:19:37 martinea Exp $
 *
 */

#include "client_util.h"
#include "getfsent.h"
#include "util.h"

static char *get_name(diskname, exin, t, n)
char *diskname, *exin;
time_t t;
int n;
{
    char number[NUM_STR_SIZE];
    char *filename;
    char *ts;

    if(n<0 || n>1000)
	return NULL;
    ts = construct_timestamp(&t);
    if(n == 0)
	number[0] = '\0';
    else
	snprintf(number, sizeof(number), "%03d", n - 1);
	
    filename = vstralloc(get_pname(), ".", diskname, ".", ts, number, ".",
			 exin, NULL);
    amfree(ts);
    return filename;
}


static char *build_name(disk, exin)
char *disk, *exin;
{
    int n=0;
    char *filename = NULL;
    char *diskname;
    time_t curtime;
    char *dbgdir = NULL;
    char *e = NULL;
    DIR *d;
    struct dirent *entry;
    char *test_name = NULL;
    int test_name_len, match_len, d_name_len;


    time(&curtime);
    diskname = sanitise_filename(disk);

    dbgdir = stralloc2(AMANDA_DBGDIR, "/");
    if((d = opendir(AMANDA_DBGDIR)) == NULL) {
	error("open debug directory \"%s\": %s",
	AMANDA_DBGDIR, strerror(errno));
    }
    test_name = get_name(diskname, exin, curtime - (AMANDA_DEBUG_DAYS * 24 * 60 * 60), 0);
    test_name_len = strlen(test_name);
    match_len = strlen(get_pname()) + strlen(diskname) + 2;
    while((entry = readdir(d)) != NULL) {
	if(is_dot_or_dotdot(entry->d_name)) {
	    continue;
	}
	d_name_len = strlen(entry->d_name);
	if(strncmp(test_name, entry->d_name, match_len) != 0
	   || d_name_len < match_len + 14 + 8
	   || strcmp(entry->d_name+ d_name_len - 7, exin) != 0) {
	    continue;				/* not one of our files */
	}
	if(strcmp(entry->d_name, test_name) < 0) {
	    e = newvstralloc(e, dbgdir, entry->d_name, NULL);
	    (void) unlink(e);                   /* get rid of old file */
	}
    }
    amfree(dbgdir);
    amfree(test_name);
    amfree(e);
    closedir(d);

    do {
	amfree(filename);
	filename = get_name(diskname, exin, curtime, n);
	n++;
    } while(access(filename, F_OK) == 0 && n<1000);

    if(n==1000) {
	error("Can't create filename %s\n", filename);
    }

    amfree(diskname);

    return filename;
}


static int add_exclude(file_exclude, aexc, verbose)
FILE *file_exclude;
char *aexc;
{
    int l;

    l = strlen(aexc);
    if(l > MAXPATHLEN-1) {
	dbprintf(("%s: exclude too long: %s\n", get_pname(), aexc));
	if(verbose)
	    printf("ERROR [exclude too long: %s\n", aexc);
	return 0;
    }
    else {
        if(aexc[l-1] != '\n') {
	    aexc[l] = '\n';
	    aexc[l+1] = '\0';
	 }
	 fprintf(file_exclude, "%s", aexc);
    }
    return 1;
}

static int add_include(file_include, ainc, verbose)
FILE *file_include;
char *ainc;
{
    int l;

    l = strlen(ainc);
    if(l > MAXPATHLEN-1) {
	dbprintf(("%s: exclude too long: %s\n", get_pname(), ainc));
	if(verbose)
	    printf("ERROR [exclude too long: %s\n", ainc);
	return 0;
    }
    else if(ainc[0] != '.' && ainc[0] != '\0' && ainc[1] != '/') {
        dbprintf(("%s: include must start with './': %s\n", get_pname(), ainc));
	if(verbose)
	    printf("ERROR [include must start with './': %s\n", ainc);
	return 0;
    }
    else {
        if(ainc[l-1] != '\n') {
	    ainc[l] = '\n';
	    ainc[l+1] = '\0';
	 }
	 fprintf(file_include, "%s", ainc);
    }
    return 1;
}

char *build_exclude(disk, options, verbose)
char *disk;
option_t *options;
int verbose;
{
    char *filename, *f;
    FILE *file_exclude;
    FILE *exclude;
    char aexc[MAXPATHLEN+1];
    sle_t *excl;
    int nb_exclude = 0;

    if(options->exclude_file) nb_exclude += options->exclude_file->nb_element;
    if(options->exclude_list) nb_exclude += options->exclude_list->nb_element;

    if(nb_exclude == 0) return NULL;

    filename = build_name(disk, "exclude");
    file_exclude = fopen(filename,"w");

    if(options->exclude_file) {
	for(excl = options->exclude_file->first; excl != NULL;
	    excl = excl->next) {
	    add_exclude(file_exclude, excl->name, verbose);
	}
    }

    if(options->exclude_list) {
	for(excl = options->exclude_list->first; excl != NULL;
	    excl = excl->next) {
	    exclude = fopen(excl->name, "r");
	    while (!feof(file_exclude)) {
		fgets(aexc, MAXPATHLEN, exclude); /* \n might not be there */
		add_exclude(file_exclude, aexc, verbose);
	    }
	    fclose(exclude);
	}
    }

    fclose(file_exclude);

    f = vstralloc(AMANDA_DBGDIR, "/", filename, NULL);
    amfree(filename);
    return f;
}

char *build_include(disk, options, verbose)
char *disk;
option_t *options;
int verbose;
{
    char *filename, *f;
    FILE *file_include;
    FILE *include;
    char ainc[MAXPATHLEN+1];
    sle_t *incl;
    int nb_include = 0;

    if(options->include_file) nb_include += options->include_file->nb_element;
    if(options->include_list) nb_include += options->include_list->nb_element;

    if(nb_include == 0) return NULL;

    filename = build_name(disk, "include");
    file_include = fopen(filename,"w");

    if(options->include_file) {
	for(incl = options->include_file->first; incl != NULL;
	    incl = incl->next) {
	    add_include(file_include, incl->name, verbose);
	}
    }

    if(options->include_list) {
	for(incl = options->include_list->first; incl != NULL;
	    incl = incl->next) {
	    include = fopen(incl->name, "r");
	    while (!feof(file_include)) {
		fgets(ainc, MAXPATHLEN, include); /* \n might not be there */
		add_include(file_include, ainc, verbose);
	    }
	    fclose(include);
	}
    }

    fclose(file_include);

    f = vstralloc(AMANDA_DBGDIR, "/", filename, NULL);
    amfree(filename);
    return f;
}


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
    options->exclude_file = NULL;
    options->exclude_list = NULL;
    options->include_file = NULL;
    options->include_list = NULL;

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
	else if(strncmp(tok,"include-file=", 13) == 0) {
	    exc = &tok[13];
	    options->include_file = append_sl(options->include_file,exc);
	}
	else if(strncmp(tok,"include-list=", 13) == 0) {
	    exc = &tok[13];
	    if(*exc != '/') {
		char *dirname = amname_to_dirname(disk);
		char *efile = vstralloc(dirname,"/",exc, NULL);
		if(access(efile, F_OK) != 0) {
		    /* if include list file does not exist, ignore it.
		     * Should not test for R_OK, because the file may be
		     * readable by root only! */
		    dbprintf(("%s: include list file \"%s\" does not exist, ignoring\n",
			      get_pname(), efile));
		    if(verbose) {
			printf("ERROR [include list file \"%s\" does not exist]\n", efile);
		    }
		}
		else {
		    options->include_list =
			append_sl(options->include_list, efile);
		}
		amfree(dirname);
		amfree(efile);
	    }
	    else {
		options->include_list = append_sl(options->include_list,exc);
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

