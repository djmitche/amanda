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
 * $Id: amtape.c,v 1.22 1998/10/24 03:02:02 martinea Exp $
 *
 * tape changer interface program
 */
#include "amanda.h"
#include "conffile.h"
#include "tapefile.h"
#include "tapeio.h"
#include "clock.h"
#include "changer.h"
#include "version.h"

/* local functions */
void usage P((void));
int main P((int argc, char **argv));
void reset_changer P((int argc, char **argv));
void eject_tape P((int argc, char **argv));
void clean_tape P((int argc, char **argv));
void load_slot P((int argc, char **argv));
void load_label P((int argc, char **argv));
void show_slots P((int argc, char **argv));
void show_current P((int argc, char **argv));
void taper_scan P((int argc, char **argv));
void show_device P((int argc, char **argv));
int scan_init P((int rc, int ns, int bk));
int loadlabel_slot P((int rc, char *slotstr, char *device));
int show_init P((int rc, int ns, int bk));
int show_init_all P((int rc, int ns, int bk));
int show_init_current P((int rc, int ns, int bk));
int show_slot P((int rc, char *slotstr, char *device));
int taperscan_slot P((int rc, char *slotstr, char *device));

void usage()
{
    fprintf(stderr, "Usage: amtape%s <conf> <command>\n", versionsuffix());
    fprintf(stderr, "\tValid commands are:\n");
    fprintf(stderr, "\t\treset                Reset changer to known state\n");
    fprintf(stderr, "\t\teject                Eject current tape from drive\n");
    fprintf(stderr, "\t\tclean                Clean the drive\n");
    fprintf(stderr, "\t\tshow                 Show contents of all slots\n");
    fprintf(stderr, "\t\tcurrent              Show contents of current slot\n");
    fprintf(stderr, "\t\tslot <slot #>        load tape from slot <slot #>\n");
    fprintf(stderr, "\t\tslot current         load tape from current slot\n");
    fprintf(stderr, "\t\tslot prev            load tape from previous slot\n");
    fprintf(stderr, "\t\tslot next            load tape from next slot\n");
    fprintf(stderr, "\t\tslot first           load tape from first slot\n");
    fprintf(stderr, "\t\tslot last            load tape from last slot\n");
    fprintf(stderr, "\t\tlabel <label>        find and load labeled tape\n");
    fprintf(stderr, "\t\ttaper                perform taper's scan alg.\n");
    fprintf(stderr, "\t\tdevice               show current tape device\n");

    exit(1);
}

int main(argc, argv)
int argc;
char **argv;
{
    char *confdir;
    char *confname;
    char *argv0 = argv[0];
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    int fd;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    set_pname("amtape");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    erroutput_type = ERR_INTERACTIVE;

    if(argc < 3) usage();

    confname = argv[1];

    confdir = vstralloc(CONFIG_DIR, "/", confname, NULL);
    if(chdir(confdir) != 0)
	error("could not cd to confdir %s: %s", confdir, strerror(errno));

    if(read_conffile(CONFFILE_NAME))
	error("could not read amanda config file");

    if(!changer_init())
	error("no tpchanger specified in %s/%s", confdir, CONFFILE_NAME);

    /* switch on command name */

    argc -= 2; argv += 2;
    if(strcmp(argv[0], "reset") == 0) reset_changer(argc, argv);
    else if(strcmp(argv[0], "clean") == 0) clean_tape(argc, argv);
    else if(strcmp(argv[0], "eject") == 0) eject_tape(argc, argv);
    else if(strcmp(argv[0], "slot") == 0) load_slot(argc, argv);
    else if(strcmp(argv[0], "label") == 0) load_label(argc, argv);
    else if(strcmp(argv[0], "current") == 0)  show_current(argc, argv);
    else if(strcmp(argv[0], "show") == 0)  show_slots(argc, argv);
    else if(strcmp(argv[0], "taper") == 0) taper_scan(argc, argv);
    else if(strcmp(argv[0], "device") == 0) show_device(argc, argv);
    else {
	fprintf(stderr, "%s: unknown command \"%s\"\n", argv0, argv[0]);
	usage();
    }

    amfree(changer_resultstr);
    amfree(confdir);

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    return 0;
}

/* ---------------------------- */

void reset_changer(argc, argv)
int argc;
char **argv;
{
    char *slotstr = NULL;

    switch(changer_reset(&slotstr)) {
    case 0:
	fprintf(stderr, "%s: changer is reset, slot %s is loaded.\n",
		get_pname(), slotstr);
	break;
    case 1:
	fprintf(stderr, "%s: changer is reset, but slot %s not loaded: %s\n",
		get_pname(), slotstr, changer_resultstr);
	break;
    default:
	error("could not reset changer: %s", changer_resultstr);
    }
    amfree(slotstr);
}


/* ---------------------------- */
void clean_tape(argc, argv)
int argc;
char **argv;
{
    char *devstr = NULL;

    if(changer_clean(&devstr) == 0) {
	fprintf(stderr, "%s: device %s is clean.\n", get_pname(), devstr);
    } else {
	fprintf(stderr, "%s: device %s not clean: %s\n",
		get_pname(), devstr ? devstr : "??", changer_resultstr);
    }
    amfree(devstr);
}


/* ---------------------------- */
void eject_tape(argc, argv)
int argc;
char **argv;
{
    char *slotstr = NULL;

    if(changer_eject(&slotstr) == 0) {
	fprintf(stderr, "%s: slot %s is ejected.\n", get_pname(), slotstr);
    } else {
	fprintf(stderr, "%s: slot %s not ejected: %s\n",
		get_pname(), slotstr ? slotstr : "??", changer_resultstr);
    }
    amfree(slotstr);
}


/* ---------------------------- */

void load_slot(argc, argv)
int argc;
char **argv;
{
    char *slotstr = NULL, *devicename = NULL;

    if(argc != 2)
	usage();

    if(changer_loadslot(argv[1], &slotstr, &devicename)) {
	error("could not load slot %s: %s", slotstr, changer_resultstr);
    }

    fprintf(stderr, "%s: changed to slot %s on %s\n",
	    get_pname(), slotstr, devicename);
    amfree(slotstr);
    amfree(devicename);
}


/* ---------------------------- */

int nslots, backwards, found, got_match, tapedays;
char *datestamp;
char *label = NULL, *first_match_label = NULL, *first_match = NULL;
char *searchlabel, *labelstr;
tape_t *tp;

int scan_init(rc, ns, bk)
int rc, ns, bk;
{
    if(rc)
	error("could not get changer info: %s", changer_resultstr);

    nslots = ns;
    backwards = bk;

    return 0;
}

int loadlabel_slot(rc, slotstr, device)
int rc;
char *slotstr;
char *device;
{
    char *errstr;

    if(rc > 1)
	error("could not load slot %s: %s", slotstr, changer_resultstr);
    else if(rc == 1)
	fprintf(stderr, "%s: slot %s: %s\n",
		get_pname(), slotstr, changer_resultstr);
    else if((errstr = tape_rdlabel(device, &datestamp, &label)) != NULL)
	fprintf(stderr, "%s: slot %s: %s\n", get_pname(), slotstr, errstr);
    else {
	fprintf(stderr, "%s: slot %s: date %-8s label %s",
		get_pname(), slotstr, datestamp, label);
	if(strcmp(label, searchlabel))
	    fprintf(stderr, " (wrong tape)\n");
	else {
	    fprintf(stderr, " (exact label match)\n");
	    found = 1;
	    amfree(datestamp);
	    amfree(label);
	    return 1;
	}
    }
    amfree(datestamp);
    amfree(label);
    return 0;
}

void load_label(argc, argv)
int argc;
char **argv;
{
    if(argc != 2)
	usage();

    searchlabel = argv[1];

    fprintf(stderr, "%s: scanning for tape with label %s\n",
	    get_pname(), searchlabel);

    found = 0;

    changer_scan(scan_init, loadlabel_slot);

    if(found)
	fprintf(stderr, "%s: label %s is now loaded.\n",
		get_pname(), searchlabel);
    else
	fprintf(stderr, "%s: could not find label %s in tape rack.\n",
		get_pname(), searchlabel);
}


/* ---------------------------- */

int show_init(rc, ns, bk)
int rc, ns, bk;
{
    if(rc)
	error("could not get changer info: %s", changer_resultstr);

    nslots = ns;
    backwards = bk;
    return 0;
}

int show_init_all(rc, ns, bk)
int rc, ns, bk;
{
    int ret = show_init(rc, ns, bk);
    fprintf(stderr, "%s: scanning all %d slots in tape-changer rack:\n",
	    get_pname(), nslots);
    return ret;
}

int show_init_current(rc, ns, bk)
int rc, ns, bk;
{
    int ret = show_init(rc, ns, bk);
    fprintf(stderr, "%s: scanning current slot in tape-changer rack:\n",
	    get_pname());
    return ret;
}

int show_slot(rc, slotstr, device)
int rc;
char *slotstr, *device;
{
    char *errstr;

    if(rc > 1)
	error("could not load slot %s: %s", slotstr, changer_resultstr);
    else if(rc == 1)
	fprintf(stderr, "slot %s: %s\n", slotstr, changer_resultstr);
    else if((errstr = tape_rdlabel(device, &datestamp, &label)) != NULL)
	fprintf(stderr, "slot %s: %s\n", slotstr, errstr);
    else {
	fprintf(stderr, "slot %s: date %-8s label %s\n",
		slotstr, datestamp, label);
    }
    amfree(datestamp);
    amfree(label);
    return 0;
}

void show_current(argc, argv)
int argc;
char **argv;
{
    if(argc != 1)
	usage();

    changer_current(show_init_current, show_slot);
}

void show_slots(argc, argv)
int argc;
char **argv;
{
    if(argc != 1)
	usage();

    changer_scan(show_init_all, show_slot);
}


/* ---------------------------- */

int taperscan_slot(rc, slotstr, device)
int rc;
char *slotstr;
char *device;
{
    char *errstr;

    if(rc == 2)
	error("could not load slot %s: %s", slotstr, changer_resultstr);
    else if(rc == 1)
	fprintf(stderr, "%s: slot %s: %s\n",
		get_pname(), slotstr, changer_resultstr);
    else {
	if((errstr = tape_rdlabel(device, &datestamp, &label)) != NULL) {
	    fprintf(stderr, "%s: slot %s: %s\n", get_pname(), slotstr, errstr);
	} else {
	    /* got an amanda tape */
	    fprintf(stderr, "%s: slot %s: date %-8s label %s",
		    get_pname(), slotstr, datestamp, label);
	    if(searchlabel != NULL && strcmp(label, searchlabel) == 0) {
		/* it's the one we are looking for, stop here */
		fprintf(stderr, " (exact label match)\n");
		found = 1;
		amfree(datestamp);
		amfree(label);
		return 1;
	    }
	    else if(!match(labelstr, label))
		fprintf(stderr, " (no match)\n");
	    else {
		/* not an exact label match, but a labelstr match */
		/* check against tape list */
		tp = lookup_tapelabel(label);
		if(tp != NULL && !reusable_tape(tp))
		    fprintf(stderr, " (active tape)\n");
		else if(got_match)
		    fprintf(stderr, " (labelstr match)\n");
		else {
		    got_match = 1;
		    first_match = newstralloc(first_match, slotstr);
		    first_match_label = newstralloc(first_match_label, label);
		    fprintf(stderr, " (first labelstr match)\n");
		    if(!backwards || !searchlabel) {
			found = 2;
			amfree(datestamp);
			amfree(label);
			return 1;
		    }
		}
	    }
	}
    }
    amfree(datestamp);
    amfree(label);
    return 0;
}

void taper_scan(argc, argv)
int argc;
char **argv;
{
    char *slotstr = NULL, *device = NULL;

    if(read_tapelist(getconf_str(CNF_TAPELIST)))
	error("could not load \"%s\"\n", getconf_str(CNF_TAPELIST));

    if((tp = lookup_last_reusable_tape(0)) == NULL)
	searchlabel = NULL;
    else
	searchlabel = stralloc(tp->label);

    tapedays	= getconf_int(CNF_TAPECYCLE);
    labelstr	= getconf_str(CNF_LABELSTR);
    found = 0;
    got_match = 0;

    fprintf(stderr, "%s: scanning for ", get_pname());
    if(searchlabel) fprintf(stderr, "tape label %s or ", searchlabel);
    fprintf(stderr, "a new tape.\n");

    changer_scan(scan_init, taperscan_slot);

    if(found == 2) {
	fprintf(stderr, "%s: %s: settling for first labelstr match\n",
		get_pname(),
		searchlabel? "gravity stacker": "looking only for new tape");
	searchlabel = newstralloc(searchlabel, first_match_label);
    }
    else if(!found && got_match) {
	fprintf(stderr,
		"%s: %s not found, going back to first labelstr match %s\n",
		get_pname(), searchlabel, first_match_label);
	searchlabel = newstralloc(searchlabel, first_match_label);
	if(changer_loadslot(first_match, &slotstr, &device) == 0) {
	    found = 1;
	} else {
	    fprintf(stderr, "%s: could not load labelstr match in slot %s: %s\n",
		    get_pname(), first_match, changer_resultstr);
	}
	amfree(device);
	amfree(slotstr);
    }
    else if(!found) {
	fprintf(stderr, "%s: could not find ", get_pname());
	if(searchlabel) fprintf(stderr, "tape %s or ", searchlabel);
	fprintf(stderr, "a new tape in the tape rack.\n");
    }

    if(found)
	fprintf(stderr, "%s: label %s is now loaded.\n",
		get_pname(), searchlabel);

    amfree(searchlabel);
    amfree(first_match);
    amfree(first_match_label);
}

/* ---------------------------- */

void show_device(argc, argv)
int argc;
char **argv;
{
    char *slot, *device;

    if(changer_loadslot("current", &slot, &device))
	error("Could not load current slot.\n");

    printf("%s\n", device);
}
