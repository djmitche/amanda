/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1994 University of Maryland
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
 * $Id: amtape.c,v 1.9 1997/12/17 21:03:45 jrj Exp $
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

char *pname = "amtape";

/* local functions */
void usage P((void));
int main P((int argc, char **argv));
void reset_changer P((int argc, char **argv));
void eject_tape P((int argc, char **argv));
void load_slot P((int argc, char **argv));
void load_label P((int argc, char **argv));
void show_slots P((int argc, char **argv));
void show_current P((int argc, char **argv));
void taper_scan P((int argc, char **argv));
int scan_init P((int rc, int ns, int bk));
int loadlabel_slot P((int rc, char *slotstr, char *device));
int show_init P((int rc, int ns, int bk));
int show_slot P((int rc, char *slotstr, char *device));
int taperscan_slot P((int rc, char *slotstr, char *device));

void usage()
{
    fprintf(stderr, "Usage: amtape%s <conf> <command>\n", versionsuffix());
    fprintf(stderr, "\tValid commands are:\n");
    fprintf(stderr, "\t\treset                Reset changer to known state\n");
    fprintf(stderr, "\t\teject                Eject current tape from drive\n");
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

    exit(1);
}

int main(argc, argv)
int argc;
char **argv;
{
    char confdir[256];
    char *confname;
    char *argv0 = argv[0];

    erroutput_type = ERR_INTERACTIVE;

    if(argc < 3) usage();

    confname = argv[1];

    ap_snprintf(confdir, sizeof(confdir), "%s/%s", CONFIG_DIR, confname);
    if(chdir(confdir) != 0)
	error("could not cd to confdir %s: %s", confdir, strerror(errno));

    if(read_conffile(CONFFILE_NAME))
	error("could not read amanda config file");

    if(!changer_init())
	error("no tpchanger specified in %s/%s", confdir, CONFFILE_NAME);

    /* switch on command name */

    argc -= 2; argv += 2;
    if(!strcmp(argv[0], "reset")) reset_changer(argc, argv);
    else if(!strcmp(argv[0], "eject")) eject_tape(argc, argv);
    else if(!strcmp(argv[0], "slot")) load_slot(argc, argv);
    else if(!strcmp(argv[0], "label")) load_label(argc, argv);
    else if(!strcmp(argv[0], "current"))  show_current(argc, argv);
    else if(!strcmp(argv[0], "show"))  show_slots(argc, argv);
    else if(!strcmp(argv[0], "taper")) taper_scan(argc, argv);
    else {
	fprintf(stderr, "%s: unknown command \"%s\"\n", argv0, argv[0]);
	usage();
    }
    return 0;
}

/* ---------------------------- */

void reset_changer(argc, argv)
int argc;
char **argv;
{
    char slotstr[256];
    switch(changer_reset(slotstr)) {
    case 0:
	fprintf(stderr, "%s: changer is reset, slot %s is loaded.\n",
		pname, slotstr);
	break;
    case 1:
	fprintf(stderr, "%s: changer is reset, but slot %s not loaded: %s\n",
		pname, slotstr, changer_resultstr);
	break;
    default:
	error("could not reset changer: %s", changer_resultstr);
    }

}


/* ---------------------------- */
void eject_tape(argc, argv)
int argc;
char **argv;
{
    char slotstr[256];

    if(changer_eject(slotstr) == 0)
	fprintf(stderr, "%s: slot %s is ejected.\n", pname, slotstr);
    else
	fprintf(stderr, "%s: slot %s not ejected: %s\n",
		pname, slotstr, changer_resultstr);
}


/* ---------------------------- */

void load_slot(argc, argv)
int argc;
char **argv;
{
    char slotstr[1024], devicename[1024];

    if(argc != 2)
	usage();

    if(changer_loadslot(argv[1], slotstr, devicename))
	error("could not load slot %s: %s", slotstr, changer_resultstr);

    fprintf(stderr, "%s: changed to slot %s on %s\n",
	    pname, slotstr, devicename);
}


/* ---------------------------- */

int nslots, backwards, found, got_match, tapedays;
extern char datestamp[80];
char label[80], first_match_label[64], first_match[256];
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
	fprintf(stderr, "%s: slot %s: %s\n", pname, slotstr,changer_resultstr);
    else if((errstr = tape_rdlabel(device,
				   datestamp, sizeof(datestamp),
				   label, sizeof(label))) != NULL)
	fprintf(stderr, "%s: slot %s: %s\n", pname, slotstr, errstr);
    else {
	fprintf(stderr, "%s: slot %s: date %-8s label %s",
		pname, slotstr, datestamp, label);
	if(strcmp(label, searchlabel))
	    fprintf(stderr, " (wrong tape)\n");
	else {
	    fprintf(stderr, " (exact label match)\n");
	    found = 1;
	    return 1;
	}
    }
    return 0;
}

void load_label(argc, argv)
int argc;
char **argv;
{
    if(argc != 2)
	usage();

    searchlabel = argv[1];

    fprintf(stderr, "%s: scanning for tape with label %s\n",pname,searchlabel);

    found = 0;

    changer_scan(scan_init, loadlabel_slot);

    if(found)
	fprintf(stderr, "%s: label %s is now loaded.\n", pname, searchlabel);
    else
	fprintf(stderr, "%s: could not find label %s in tape rack.\n",
		pname, searchlabel);
}


/* ---------------------------- */

int show_init(rc, ns, bk)
int rc, ns, bk;
{
    if(rc)
	error("could not get changer info: %s", changer_resultstr);

    nslots = ns;
    backwards = bk;
    fprintf(stderr, "%s: scanning all %d slots in tape-changer rack:\n",
	    pname, nslots);
    return 0;
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
    else if((errstr = tape_rdlabel(device,
				   datestamp, sizeof(datestamp),
				   label, sizeof(label))) != NULL)
	fprintf(stderr, "slot %s: %s\n", slotstr, errstr);
    else {
	fprintf(stderr, "slot %s: date %-8s label %s\n",
		slotstr, datestamp, label);
    }
    return 0;
}

void show_current(argc, argv)
int argc;
char **argv;
{
    if(argc != 1)
	usage();

    changer_current(show_init, show_slot);
}

void show_slots(argc, argv)
int argc;
char **argv;
{
    if(argc != 1)
	usage();

    changer_scan(show_init, show_slot);
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
	fprintf(stderr, "%s: slot %s: %s\n", pname, slotstr,changer_resultstr);
    else {
	if((errstr = tape_rdlabel(device,
				  datestamp, sizeof(datestamp),
				  label, sizeof(label))) != NULL)
	    fprintf(stderr, "%s: slot %s: %s\n", pname, slotstr, errstr);
	else {
	    /* got an amanda tape */
	    fprintf(stderr, "%s: slot %s: date %-8s label %s",
		    pname, slotstr, datestamp, label);
	    if(searchlabel != NULL && !strcmp(label, searchlabel)) {
		/* it's the one we are looking for, stop here */
		fprintf(stderr, " (exact label match)\n");
		found = 1;
		return 1;
	    }
	    else if(!match(labelstr, label))
		fprintf(stderr, " (no match)\n");
	    else {
		/* not an exact label match, but a labelstr match */
		/* check against tape list */
		tp = lookup_tapelabel(label);
		if(tp != NULL && tp->position < tapedays)
		    fprintf(stderr, " (active tape)\n");
		else if(got_match)
		    fprintf(stderr, " (labelstr match)\n");
		else {
		    got_match = 1;
		    strncpy(first_match, slotstr, sizeof(first_match)-1);
		    first_match[sizeof(first_match)-1] = '\0';
		    strncpy(first_match_label, label,
			    sizeof(first_match_label)-1);
		    first_match_label[sizeof(first_match_label)-1] = '\0';
		    fprintf(stderr, " (first labelstr match)\n");
		    if(!backwards || !searchlabel) {
			found = 2;
			return 1;
		    }
		}
	    }
	}
    }
    return 0;
}

void taper_scan(argc, argv)
int argc;
char **argv;
{
    char slotstr[32], device[1024];

    if(read_tapelist(getconf_str(CNF_TAPELIST)))
	error("could not load \"%s\"\n", getconf_str(CNF_TAPELIST));

    if((tp = lookup_tapepos(getconf_int(CNF_TAPECYCLE))) == NULL)
	searchlabel = NULL;
    else
	searchlabel = tp->label;

    tapedays	= getconf_int(CNF_TAPECYCLE);
    labelstr	= getconf_str(CNF_LABELSTR);
    found = 0;
    got_match = 0;

    fprintf(stderr, "%s: scanning for ", pname);
    if(searchlabel) fprintf(stderr, "tape label %s or ", searchlabel);
    fprintf(stderr, "a new tape.\n");

    changer_scan(scan_init, taperscan_slot);

    if(found == 2) {
	fprintf(stderr, "%s: %s: settling for first labelstr match\n", pname,
		searchlabel? "gravity stacker": "looking only for new tape");
	searchlabel = first_match_label;
    }
    else if(!found && got_match) {
	fprintf(stderr,
		"%s: %s not found, going back to first labelstr match %s\n",
		pname, searchlabel, first_match_label);
	searchlabel = first_match_label;
	if(changer_loadslot(first_match, slotstr, device) == 0)
	    found = 1;
	else {
	    fprintf(stderr, "%s: could not load labelstr match in slot %s: %s\n",
		    pname, first_match, changer_resultstr);
	}
    }
    else if(!found) {
	fprintf(stderr, "%s: could not find ", pname);
	if(searchlabel) fprintf(stderr, "tape %s or ", searchlabel);
	fprintf(stderr, "a new tape in the tape rack.\n");
    }

    if(found)
	fprintf(stderr, "%s: label %s is now loaded.\n", pname, searchlabel);
}
