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
 * $Id: amlabel.c,v 1.9 1998/02/26 19:25:03 jrj Exp $
 *
 * write an Amanda label on a tape
 */
#include "amanda.h"
#include "conffile.h"
#include "tapeio.h"
#include "changer.h"

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
    char *confdir, *outslot = NULL;
    char *errstr, *confname, *label, *tapename = NULL, *labelstr, *slotstr;
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

    set_pname("amlabel");

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

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

    confdir = vstralloc(CONFIG_DIR, "/", confname, NULL);
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
	tapename = stralloc(getconf_str(CNF_TAPEDEV));
    }
    else {
	if(changer_loadslot(slotstr, &outslot, &tapename)) {
	    error("could not load slot \"%s\": %s", slotstr, changer_resultstr);
	}

	printf("labeling tape in slot %s (%s):\n", outslot, tapename);
    }

    printf("rewinding"); fflush(stdout);

    if((errstr = tape_rewind(tapename)) != NULL) {
	putchar('\n');
	error(errstr);
    }

    printf(", writing label %s", label); fflush(stdout);

    if((errstr = tape_wrlabel(tapename, "X", label)) != NULL) {
	putchar('\n');
	error(errstr);
    }

    printf(", writing end marker"); fflush(stdout);

    if((errstr = tape_wrendmark(tapename, "X")) != NULL) {
	putchar('\n');
	error(errstr);
    }

    afree(outslot);
    afree(tapename);
    afree(confdir);

    printf(", done.\n");

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    return 0;
}
