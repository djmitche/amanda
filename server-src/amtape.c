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
 * $Id: amtape.c,v 1.36 2003/11/25 12:20:54 martinea Exp $
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

static const struct {
    const char *name;
    void (*fn) P((int, char **));
    const char *usage;
} cmdtab[] = {
    { "reset", reset_changer,
	"reset                Reset changer to known state" },
    { "eject", eject_tape,
	"eject                Eject current tape from drive" },
    { "clean", clean_tape,
	"clean                Clean the drive" },
    { "show", show_slots,
	"show                 Show contents of all slots" },
    { "current", show_current,
	"current              Show contents of current slot" },
    { "slot" , load_slot,
	"slot <slot #>        load tape from slot <slot #>" },
    { "slot" , load_slot,
	"slot current         load tape from current slot" },
    { "slot" , load_slot,
	"slot prev            load tape from previous slot" },
    { "slot" , load_slot,
	"slot next            load tape from next slot" },
    { "slot" , load_slot,
	"slot advance         advance to next slot but do not load" },
    { "slot" , load_slot,
	"slot first           load tape from first slot" },
    { "slot" , load_slot,
	"slot last            load tape from last slot" },
    { "label", load_label,
	"label <label>        find and load labeled tape" },
    { "taper", taper_scan,
	"taper                perform taper's scan alg." },
    { "device", show_device,
	"device               show current tape device" },
};
#define	NCMDS	(sizeof(cmdtab) / sizeof(cmdtab[0]))

void usage()
{
    int i;

    fprintf(stderr, "Usage: amtape%s <conf> <command>\n", versionsuffix());
    fprintf(stderr, "\tValid commands are:\n");
    for (i = 0; i < NCMDS; i++)
	fprintf(stderr, "\t\t%s\n", cmdtab[i].usage);
    exit(1);
}

int main(argc, argv)
int argc;
char **argv;
{
    char *conffile;
    char *conf_tapelist;
    char *argv0 = argv[0];
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    int fd, i;
    int have_changer;
    uid_t uid_me;
    uid_t uid_dumpuser;
    char *dumpuser;
    struct passwd *pw;

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

    set_pname("amtape");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    erroutput_type = ERR_INTERACTIVE;

    if(argc < 3) usage();

    config_name = argv[1];

    config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if (read_conffile(conffile)) {
	error("errors processing config file \"%s\"", conffile);
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
    amfree(conf_tapelist);

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

    if((have_changer = changer_init()) == 0) {
	error("no tpchanger specified in \"%s\"", conffile);
    } else if (have_changer != 1) {
	error("changer initialization failed: %s", strerror(errno));
    }

    /* switch on command name */

    argc -= 2; argv += 2;
    for (i = 0; i < NCMDS; i++)
	if (strcmp(argv[0], cmdtab[i].name) == 0) {
	    (*cmdtab[i].fn)(argc, argv);
	    break;
	}
    if (i == NCMDS) {
	fprintf(stderr, "%s: unknown command \"%s\"\n", argv0, argv[0]);
	usage();
    }

    amfree(changer_resultstr);
    amfree(conffile);
    amfree(config_dir);

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
    char *errstr;
    int is_advance;

    if(argc != 2)
	usage();

    is_advance = (strcmp(argv[1], "advance") == 0);
    if(changer_loadslot(argv[1], &slotstr, &devicename)) {
	error("could not load slot %s: %s", slotstr, changer_resultstr);
    }
    if(! is_advance && (errstr = tape_rewind(devicename)) != NULL) {
	fprintf(stderr,
		"%s: could not rewind %s: %s", get_pname(), devicename, errstr);
	amfree(errstr);
    }

    fprintf(stderr, "%s: changed to slot %s", get_pname(), slotstr);
    if(! is_advance) {
	fprintf(stderr, " on %s", devicename);
    }
    fputc('\n', stderr);
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
	if(strcmp(label, FAKE_LABEL) != 0
	   && strcmp(label, searchlabel) != 0)
	    fprintf(stderr, " (wrong tape)\n");
	else {
	    fprintf(stderr, " (exact label match)\n");
	    if((errstr = tape_rewind(device)) != NULL) {
		fprintf(stderr,
			"%s: could not rewind %s: %s",
			get_pname(), device, errstr);
		amfree(errstr);
	    }
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

    changer_find(scan_init, loadlabel_slot, searchlabel);

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
	    if(searchlabel != NULL
	       && (strcmp(label, FAKE_LABEL) == 0
		   || strcmp(label, searchlabel) == 0)) {
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
		if(tp == NULL)
		    fprintf(stderr, " (not in tapelist)\n");
		else if(!reusable_tape(tp))
		    fprintf(stderr, " (active tape)\n");
		else if(got_match == 0 && tp->datestamp == 0) {
		    got_match = 1;
		    first_match = newstralloc(first_match, slotstr);
		    first_match_label = newstralloc(first_match_label, label);
		    fprintf(stderr, " (new tape)\n");
		    found = 3;
		    amfree(datestamp);
		    amfree(label);
		    return 1;
		}
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

    if (searchlabel != NULL)
      changer_find(scan_init, taperscan_slot, searchlabel);
    else
      changer_scan(scan_init, taperscan_slot);

    if(found == 3) {
	fprintf(stderr, "%s: settling for new tape\n", get_pname());
	searchlabel = newstralloc(searchlabel, first_match_label);
    }
    else if(found == 2) {
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
    char *slot = NULL, *device = NULL;

    if(changer_loadslot("current", &slot, &device))
	error("Could not load current slot.\n");

    printf("%s\n", device);
    amfree(slot);
    amfree(device);
}
