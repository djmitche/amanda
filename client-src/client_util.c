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
 * $Id: client_util.c,v 1.1.2.14 2002/03/24 19:23:23 jrjackson Exp $
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

    ts = construct_timestamp(&t);
    if(n == 0)
	number[0] = '\0';
    else
	ap_snprintf(number, sizeof(number), "%03d", n - 1);
	
    filename = vstralloc(get_pname(), ".", diskname, ".", ts, number, ".",
			 exin, NULL);
    amfree(ts);
    return filename;
}


static char *build_name(disk, exin, verbose)
char *disk, *exin;
{
    int n=0, fd=-1;
    char *filename = NULL;
    char *afilename = NULL;
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
    test_name = get_name(diskname, exin,
			 curtime - (AMANDA_DEBUG_DAYS * 24 * 60 * 60), 0);
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
    amfree(test_name);
    amfree(e);
    closedir(d);

    n=0;
    do {
	filename = get_name(diskname, exin, curtime, n);
	afilename = newvstralloc(afilename, dbgdir, filename, NULL);
	if((fd=open(afilename, O_WRONLY|O_CREAT|O_EXCL|O_APPEND, 0600)) < 0){
	    amfree(filename);
	    amfree(afilename);
	    n++;
	}
	else {
	    close(fd);
	}
    } while(!afilename && n < 1000);

    if(afilename == NULL) {
	filename = get_name(diskname, exin, curtime, 0);
	afilename = newvstralloc(afilename, dbgdir, filename, NULL);
	dbprintf(("%s: Cannot create '%s'\n", get_pname(), afilename));
	if(verbose)
	    printf("ERROR [cannot create: %s]\n", afilename);
	amfree(filename);
	amfree(afilename);
    }

    amfree(dbgdir);
    amfree(diskname);

    return afilename;
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
	    printf("ERROR [exclude too long: %s]\n", aexc);
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

static int add_include(disk, device, file_include, ainc, verbose)
char *disk, *device;
FILE *file_include;
char *ainc;
{
    int l;
    int nb_exp=0;

    l = strlen(ainc);
    if(ainc[l-1] == '\n') {
	ainc[l-1] = '\0';
	l--;
    }
    if(l > MAXPATHLEN-1) {
	dbprintf(("%s: include too long: %s\n", get_pname(), ainc));
	if(verbose)
	    printf("ERROR [include too long: %s]\n", ainc);
	return 0;
    }
    else if(l < 3) {
	dbprintf(("%s: include must be at least 3 character long: %s\n", get_pname(), ainc));
	if(verbose)
	    printf("ERROR [include must be at least 3 character long: %s]\n", ainc);
	return 0;
    }
    else if(ainc[0] != '.' && ainc[0] != '\0' && ainc[1] != '/') {
        dbprintf(("%s: include must start with './': %s\n", get_pname(), ainc));
	if(verbose)
	    printf("ERROR [include must start with './': %s]\n", ainc);
	return 0;
    }
    else {
	char *glob;
	char *regex;
	DIR *d;
	struct dirent *entry;

	glob = ainc+2;
	regex = glob_to_regex(glob);
	if((d = opendir(device)) == NULL) {
	    dbprintf(("%s: Can't open disk '%s']\n", get_pname(), device));
	    if(verbose)
		printf("ERROR [Can't open disk '%s']\n", device);
	    return 0;
	}
	else {
	    while((entry = readdir(d)) != NULL) {
		if(is_dot_or_dotdot(entry->d_name)) {
		    continue;
		}
		if(match(regex, entry->d_name)) {
		    fprintf(file_include, "./%s\n", entry->d_name);
		    nb_exp++;
		}
	    }
	}
    }
    return nb_exp;
}

char *build_exclude(disk, device, options, verbose)
char *disk, *device;
option_t *options;
int verbose;
{
    char *filename;
    FILE *file_exclude;
    FILE *exclude;
    char *aexc = NULL;
    sle_t *excl;
    int nb_exclude = 0;

    if(options->exclude_file) nb_exclude += options->exclude_file->nb_element;
    if(options->exclude_list) nb_exclude += options->exclude_list->nb_element;

    if(nb_exclude == 0) return NULL;

    if((filename = build_name(disk, "exclude", verbose)) != NULL) {
	if((file_exclude = fopen(filename,"w")) != NULL) {

	    if(options->exclude_file) {
		for(excl = options->exclude_file->first; excl != NULL;
		    excl = excl->next) {
		    add_exclude(file_exclude, excl->name,
				verbose && options->exclude_optional == 0);
		}
	    }

	    if(options->exclude_list) {
		for(excl = options->exclude_list->first; excl != NULL;
		    excl = excl->next) {
		    if((exclude = fopen(excl->name, "r")) != NULL) {
			while ((aexc = agets(exclude)) != NULL) {
			    add_exclude(file_exclude, aexc,
				        verbose && options->exclude_optional == 0);
			    amfree(aexc);
			}
			fclose(exclude);
		    }
		    else {
			dbprintf(("%s: Can't open exclude file '%s': %s\n",
				  get_pname(), excl->name, strerror(errno)));
			if(verbose && options->exclude_optional==0)
			    printf("ERROR [Can't open exclude file '%s': %s]\n",
				   excl->name, strerror(errno));
		    }
		}
	    }
            fclose(file_exclude);
	}
	else {
	    dbprintf(("%s: Can't create exclude file '%s': %s\n", get_pname(),
		      filename, strerror(errno)));
	    if(verbose && options->exclude_optional == 0)
		printf("ERROR [Can't create exclude file '%s': %s]\n", filename,
			strerror(errno));
	}
    }

    return filename;
}

char *build_include(disk, device, options, verbose)
char *disk;
char *device;
option_t *options;
int verbose;
{
    char *filename;
    FILE *file_include;
    FILE *include;
    char *ainc = NULL;
    sle_t *incl;
    int nb_include = 0;
    int nb_exp = 0;

    if(options->include_file) nb_include += options->include_file->nb_element;
    if(options->include_list) nb_include += options->include_list->nb_element;

    if(nb_include == 0) return NULL;

    if((filename = build_name(disk, "include", verbose)) != NULL) {
	if((file_include = fopen(filename,"w")) != NULL) {

	    if(options->include_file) {
		for(incl = options->include_file->first; incl != NULL;
		    incl = incl->next) {
		    nb_exp += add_include(disk, device, file_include,
				  incl->name,
				   verbose && options->include_optional == 0);
		}
	    }

	    if(options->include_list) {
		for(incl = options->include_list->first; incl != NULL;
		    incl = incl->next) {
		    if((include = fopen(incl->name, "r")) != NULL) {
			while ((ainc = agets(include)) != NULL) {
			    nb_exp += add_include(disk, device,
						  file_include, ainc,
						  verbose && options->include_optional == 0);
			    amfree(ainc);
			}
			fclose(include);
		    }
		    else {
			dbprintf(("%s: Can't open include file '%s': %s\n",
				  get_pname(), incl->name, strerror(errno)));
			if(verbose && options->include_optional == 0)
			    printf("ERROR [Can't open include file '%s': %s]\n",
				   incl->name, strerror(errno));
		   }
		}
	    }
            fclose(file_include);
	}
	else {
	    dbprintf(("%s: Can't open include file '%s': %s\n", get_pname(),
		      filename, strerror(errno)));
	    if(verbose && options->include_optional == 0)
		printf("ERROR [Can't open include file '%s': %s]\n", filename,
			strerror(errno));
	}
    }
	
    if(nb_exp == 0) {
	dbprintf(("%s: No include for '%s'\n", get_pname(), disk));
	if(verbose && options->include_optional == 0)
	    printf("ERROR [No include for '%s']\n", disk);
    }

    return filename;
}


void init_options(options)
option_t *options;
{
    options->str = NULL;
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
    options->include_file = NULL;
    options->include_list = NULL;
    options->exclude_optional = 0;
    options->include_optional = 0;
}


option_t *parse_options(str, disk, device, verbose)
char *str;
char *disk, *device;
int verbose;
{
    char *exc;
    option_t *options;
    char *p, *tok;

    options = alloc(sizeof(option_t));
    init_options(options);
    options->str = stralloc(str);

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
	else if(strcmp(tok, "exclude-optional") == 0) {
	    options->exclude_optional = 1;
	}
	else if(strcmp(tok, "include-optional") == 0) {
	    options->include_optional = 1;
	}
	else if(strncmp(tok,"exclude-file=", 13) == 0) {
	    exc = &tok[13];
	    options->exclude_file = append_sl(options->exclude_file,exc);
	}
	else if(strncmp(tok,"exclude-list=", 13) == 0) {
	    exc = &tok[13];
	    if(*exc != '/') {
		char *dirname = amname_to_dirname(device);
		char *efile = vstralloc(dirname,"/",exc, NULL);
		options->exclude_list =
		    append_sl(options->exclude_list, efile);
		amfree(dirname);
		amfree(efile);
	    }
	    else {
		options->exclude_list = append_sl(options->exclude_list, exc);
	    }
	}
	else if(strncmp(tok,"include-file=", 13) == 0) {
	    exc = &tok[13];
	    options->include_file = append_sl(options->include_file,exc);
	}
	else if(strncmp(tok,"include-list=", 13) == 0) {
	    exc = &tok[13];
	    if(*exc != '/') {
		char *dirname = amname_to_dirname(device);
		char *efile = vstralloc(dirname,"/",exc, NULL);
		options->include_list =
		    append_sl(options->include_list, efile);
		amfree(dirname);
		amfree(efile);
	    }
	    else {
		options->include_list = append_sl(options->include_list, exc);
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

