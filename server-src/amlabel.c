/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991,1993 University of Maryland
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
 * amlabel.c -- write an Amanda label on a tape
 */
#include "amanda.h"
#include "conffile.h"
#include "tapeio.h"
#include "changer.h"

char *pname = "amlabel";

int slotcommand;

/* local functions */

void usage P((char *argv0));

void usage(argv0)
char *argv0;
{
    fprintf(stderr, "Usage: %s <conf> <label> [slot <slot-number>]\n",
	    argv0);
    exit(1);
}

int main(argc, argv)
int argc;
char **argv;
{
    char confdir[256], tapedev[256], outslot[256];
    char *errstr, *confname, *label, *tapename, *labelstr, *slotstr;

    erroutput_type = ERR_INTERACTIVE;

    if(argc != 3 && argc != 5)
	usage(argv[0]);

    confname = argv[1];
    label = argv[2];

    if(argc == 5) {
	if(strcmp(argv[3], "slot"))
	    usage(argv[0]);
	slotstr = argv[4];
	slotcommand = 1;
    }
    else {
	slotstr = "current";
	slotcommand = 0;
    }

    sprintf(confdir, "%s/%s", CONFIG_DIR, confname);
    if(chdir(confdir) != 0)
	error("could not cd to confdir %s: %s", confdir, strerror(errno));

    if(read_conffile(CONFFILE_NAME))
	error("could not read amanda config file");

    labelstr = getconf_str(CNF_LABELSTR);

    if(!match(labelstr, label))
	error("label %s doesn't match labelstr \"%s\"", label, labelstr);

    if(!changer_init()) {
	if(slotcommand) {
	    fprintf(stderr,
	     "%s: no tpchanger specified in %s/%s, so slot command invalid\n",
		    argv[0], confdir, CONFFILE_NAME);
	    usage(argv[0]);
	}
	tapename = getconf_str(CNF_TAPEDEV);
    }
    else {
	if(changer_loadslot(slotstr, outslot, tapedev))
	    error("could not load slot \"%s\": %s", slotstr,changer_resultstr);
	tapename = tapedev;

	printf("labeling tape in slot %s (%s):\n", outslot, tapename);
    }

    printf("rewinding"); fflush(stdout);

    if((errstr = tape_rewind(tapename)) != NULL)
	error(errstr);

    printf(", writing label %s", label); fflush(stdout);

    if((errstr = tape_wrlabel(tapename, "X", label)) != NULL)
	error(errstr);

    printf(", writing end marker"); fflush(stdout);

    if((errstr = tape_wrendmark(tapename, "X")) != NULL)
	error(errstr);

    printf(", done.\n");
    return 0;
}
