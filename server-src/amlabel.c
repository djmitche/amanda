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
 * $Id: amlabel.c,v 1.26 1999/09/15 00:32:32 jrj Exp $
 *
 * write an Amanda label on a tape
 */
#include "amanda.h"
#include "conffile.h"
#include "tapefile.h"
#include "tapeio.h"
#include "changer.h"

#ifdef HAVE_LIBVTBLC
#include <vtblc.h>
#endif /* HAVE_LIBVTBLC */

char *config_name = NULL;
char *config_dir = NULL;

/* local functions */

int main P((int, char **));
void usage P((char *argv0));

void usage(argv0)
char *argv0;
{
    fprintf(stderr, "Usage: %s [-f] <conf> <label> [slot <slot-number>]\n",
	    argv0);
    exit(1);
}

int main(argc, argv)
    int argc;
    char **argv;
{
    char *conffile;
    char *conf_tapelist;
    char *outslot = NULL;
    char *errstr, *label, *oldlabel=NULL, *tapename = NULL;
    char *labelstr, *slotstr;
    char *olddatestamp=NULL;
    char *conf_tapelist_old;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    int fd;
    int have_changer;
    int force, tape_ok;
    tape_t *tp;
    int slotcommand;
    uid_t uid_me;
    uid_t uid_dumpuser;
    char *dumpuser;
    struct passwd *pw;

#ifdef HAVE_LIBVTBLC
    int vtbl_no      = -1;
    char *datestr    = NULL;
    char *rawtapedev = NULL;
    int first_seg, last_seg;
#endif /* HAVE_LIBVTBLC */

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    safe_cd();

    set_pname("amlabel");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    erroutput_type = ERR_INTERACTIVE;

    if(argc > 1 && strcmp(argv[1],"-f") == 0)
	 force=1;
    else force=0;

    if(argc != 3+force && argc != 5+force)
	usage(argv[0]);

    config_name = argv[1+force];
    label = argv[2+force];

    if(argc == 5+force) {
	if(strcmp(argv[3+force], "slot"))
	    usage(argv[0]);
	slotstr = argv[4+force];
	slotcommand = 1;
    } else {
	slotstr = "current";
	slotcommand = 0;
    }

    config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if (read_conffile(conffile)) {
	error("could not read config file \"%s\"", conffile);
    }

    conf_tapelist = getconf_str(CNF_TAPELIST);
    if (*conf_tapelist == '/') {
	conf_tapelist = stralloc(conf_tapelist);
    } else {
	conf_tapelist = stralloc2(config_dir, conf_tapelist);
    }
    if (read_tapelist(conf_tapelist)) {
	error("could not load tapelist \"%s\"", conf_tapelist);
    }

    uid_me = getuid();
    uid_dumpuser = uid_me;
    dumpuser = getconf_str(CNF_DUMPUSER);

    if ((pw = getpwnam(dumpuser)) == NULL) {
	error("cannot look up dump user \"%s\"", dumpuser);
	/* NOTREACHED */
    }
    uid_dumpuser = pw->pw_uid;
    if ((pw = getpwuid(uid_me)) == NULL) {
	error("cannot look up my own uid %ld", (long)uid_me);
	/* NOTREACHED */
    }
    if (uid_me != uid_dumpuser) {
	error("running as user \"%s\" instead of \"%s\"",
	      pw->pw_name, dumpuser);
	/* NOTREACHED */
    }

    labelstr = getconf_str(CNF_LABELSTR);

    if(!match(labelstr, label))
	error("label %s doesn't match labelstr \"%s\"", label, labelstr);

    if((tp = lookup_tapelabel(label))!=NULL) {
	if(!force)
	    error("label %s already on a tape\n",label);
    }

    if((have_changer = changer_init()) == 0) {
	if(slotcommand) {
	    fprintf(stderr,
	     "%s: no tpchanger specified in \"%s\", so slot command invalid\n",
		    argv[0], conffile);
	    usage(argv[0]);
	}
	tapename = stralloc(getconf_str(CNF_TAPEDEV));
#ifdef HAVE_LIBVTBLC
	rawtapedev = stralloc(getconf_str(CNF_RAWTAPEDEV));
#endif /* HAVE_LIBVTBLC */
    } else if(have_changer != 1) {
	error("changer initialization failed: %s", strerror(errno));
    } else {
	if(changer_loadslot(slotstr, &outslot, &tapename)) {
	    error("could not load slot \"%s\": %s", slotstr, changer_resultstr);
	}

	printf("labeling tape in slot %s (%s):\n", outslot, tapename);
    }

#ifdef HAVE_LINUX_ZFTAPE_H
    if (is_zftape(tapename) == 1){
	if((fd = tape_open(tapename, O_WRONLY)) == -1) {
	    errstr = newstralloc2(errstr, "amlabel: ",
				  (errno == EACCES) ? "tape is write-protected"
				  : strerror(errno));
	    error(errstr);
	}
    }
#endif /* HAVE_LINUX_ZFTAPE_H */

    printf("rewinding"); fflush(stdout);

#ifdef HAVE_LINUX_ZFTAPE_H
    if (is_zftape(tapename) == 1){
	if(tapefd_rewind(fd) == -1) {
	    putchar('\n');
	    error(strerror(errno));
	}
    }
    else
#endif /* HAVE_LINUX_ZFTAPE_H */
    if((errstr = tape_rewind(tapename)) != NULL) {
	putchar('\n');
	error(errstr);
    }

    tape_ok=1;
    printf(", reading label");fflush(stdout);
    if((errstr = tape_rdlabel(tapename, &olddatestamp, &oldlabel)) != NULL) {
	printf(", %s\n",errstr);
	tape_ok=1;
    }
    else {
	/* got an amanda tape */
	printf(" %s",oldlabel);
	if(!match(labelstr, oldlabel)) {
	    printf(", tape is in another amanda configuration");
	    if(!force)
		tape_ok=0;
	}
	else {
	    if((tp = lookup_tapelabel(oldlabel)) != NULL) {
		printf(", tape is active");
		if(!force)
		    tape_ok=0;
	    }
	}
	printf("\n");
    }
	
    printf("rewinding"); fflush(stdout);

#ifdef HAVE_LINUX_ZFTAPE_H
    if (is_zftape(tapename) == 1){
	if(tapefd_rewind(fd) == -1) {
	    putchar('\n');
	    error(strerror(errno));
	}
    }
    else
#endif /* HAVE_LINUX_ZFTAPE_H */
    if((errstr = tape_rewind(tapename)) != NULL) {
	putchar('\n');
	error(errstr);
    }

    if(tape_ok) {
	printf(", writing label %s", label); fflush(stdout);

#ifdef HAVE_LINUX_ZFTAPE_H
	if (is_zftape(tapename) == 1){
	    if((errstr = tapefd_wrlabel(fd, "X", label)) != NULL) {
		putchar('\n');
		error(errstr);
	    }
	}
	else
#endif /* HAVE_LINUX_ZFTAPE_H */
	if((errstr = tape_wrlabel(tapename, "X", label)) != NULL) {
	    putchar('\n');
	    error(errstr);
	} else {
	    printf(", checking label"); fflush(stdout);

	    if((errstr = tape_rdlabel(tapename, &olddatestamp, &oldlabel)) != NULL) {
		putchar('\n');
		if (strcmp(errstr, "not an amanda tape") != 0)
		    error(errstr);
		error("no label found, are you sure %s is non-rewinding?",
		      tapename);
	    }

	    if (strcmp("X", olddatestamp) != 0 ||
		strcmp(label, oldlabel) != 0) {
		putchar('\n');
		error("read label %s back, timestamp %s (expected X), what now?",
		      oldlabel, olddatestamp);
	    }

	    /* write tape list */

    	    /* XXX add cur_tape number to tape list structure */
	    remove_tapelabel(label);
    	    add_tapelabel(0, label);
       	    conf_tapelist_old = stralloc2(conf_tapelist, ".amlabel");
	    if (rename(conf_tapelist, conf_tapelist_old) != 0) {
		error("could not rename \"%s\" to \"%s\": %s",
		      conf_tapelist, conf_tapelist_old, strerror(errno));
	    }
	    amfree(conf_tapelist_old);
	    if(write_tapelist(conf_tapelist)) {
	        error("couldn't write tapelist: %s", strerror(errno));
	    }
	} /* write tape list */

#ifdef HAVE_LINUX_ZFTAPE_H
	if (is_zftape(tapename) == 1){
	    tapefd_weof(fd, 1);
	}
#endif /* HAVE_LINUX_ZFTAPE_H */

#ifdef HAVE_LINUX_ZFTAPE_H
	if (is_zftape(tapename) == 1){
	    if((errstr = tapefd_wrendmark(fd, "X")) != NULL) {
		putchar('\n');
		error(errstr);
	    }
	}
	else
#endif /* HAVE_LINUX_ZFTAPE_H */
	if((errstr = tape_wrendmark(tapename, "X")) != NULL) {
	    putchar('\n');
	    error(errstr);
	}

#ifdef HAVE_LINUX_ZFTAPE_H
	if (is_zftape(tapename) == 1){
	    tapefd_weof(fd, 1);

	    printf(",\nrewinding"); fflush(stdout); 
     
	    if(tapefd_rewind(fd) == -1) { 
		putchar('\n'); 
		error(strerror(errno)); 
	    } 
	    close(fd);
#ifdef HAVE_LIBVTBLC
	    /* update volume table */
	    printf(", updating volume table"); fflush(stdout);
    
	    if ((fd = raw_tape_open(rawtapedev, O_RDWR)) == -1) {
		if(errno == EACCES) {
		    errstr = newstralloc(errstr,
					 "updating volume table: raw tape device is write protected");
		} else {
		    errstr = newstralloc2(errstr,
					  "updating volume table: ", strerror(errno));
		}
		putchar('\n');
		error(errstr);
	    }
	    /* read volume table */
	    if ((num_volumes = read_vtbl(fd, volumes, vtbl_buffer,
					 &first_seg, &last_seg)) == -1 ) {
		errstr = newstralloc2(errstr,
				      "reading volume table: ", strerror(errno));
		putchar('\n');
		error(errstr);
	    }
	    /* set date and volume label for first entry */
	    vtbl_no = 0;
	    datestr = NULL; 
	    if (set_date(datestr, volumes, num_volumes, vtbl_no)){
		errstr = newstralloc2(errstr,
				      "setting date for entry 1: ", strerror(errno));
		putchar('\n');
		error(errstr);
	    }
	    if(set_label(label, volumes, num_volumes, vtbl_no)){
		errstr = newstralloc2(errstr,
				      "setting label for entry 1: ", strerror(errno));
		putchar('\n');
		error(errstr);
	    }
	    /* set date and volume label for last entry */
	    vtbl_no = 1;
	    datestr = NULL; 
	    if (set_date(datestr, volumes, num_volumes, vtbl_no)){
		errstr = newstralloc2(errstr,
				      "setting date for entry 2: ", strerror(errno));
		putchar('\n');
		error(errstr);
	    }
	    if(set_label("AMANDA Tape End", volumes, num_volumes, vtbl_no)){
		errstr = newstralloc2(errstr,
				      "setting label for entry 2: ", strerror(errno));
		putchar('\n');
		error(errstr);
	    }
	    /* write volume table back */
	    if (write_vtbl(fd, volumes, vtbl_buffer, num_volumes, first_seg,
			   op_mode == trunc)) {
		errstr = newstralloc2(errstr,
				      "writing volume table: ", strerror(errno));
		putchar('\n');
		error(errstr);
	    }  
	    close(fd);
#endif /* HAVE_LIBVTBLC */
	}
#endif /* HAVE_LINUX_ZFTAPE_H */

        if(have_changer) {
	    /*	Now we try to inform the changer, about the new label */
	    /* changer_label(outslot,label); */
	}
	printf(", done.\n");
    } else {
	printf("\ntape not labeled\n");
    }

    amfree(outslot);
    amfree(tapename);
    amfree(conffile);
    amfree(conf_tapelist);
    amfree(config_dir);
    amfree(config_name);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    return 0;
}
