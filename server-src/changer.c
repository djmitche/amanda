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
 * $Id: changer.c,v 1.19 1999/05/14 21:52:41 kashmir Exp $
 *
 * interface routines for tape changers
 */
#include "amanda.h"
#include "conffile.h"
#include "version.h"

#include "changer.h"

char *changer_resultstr = NULL;

static char *tapechanger = NULL;

/* local functions */
static int changer_command P((char *cmdstr));
static int report_bad_resultstr P((void));
static int run_changer_command P((char *, char *, char **, char **));

int changer_init()
{
    tapechanger = getconf_str(CNF_TPCHANGER);
    return strcmp(tapechanger, "") != 0;
}


static int report_bad_resultstr()
{
    char *s;

    s = vstralloc("badly formed result from changer: ",
		  "\"", changer_resultstr, "\"",
		  NULL);
    amfree(changer_resultstr);
    changer_resultstr = s;
    return 2;
}

static int run_changer_command(cmd, arg, slotstr, rest)
char *cmd;
char *arg;
char **slotstr;
char **rest;
{
    int exitcode;
    char *changer_cmd = NULL;
    char *result_copy;
    char *slot;
    char *s;
    int ch;

    *slotstr = NULL;
    *rest = NULL;
    if(arg) {
	changer_cmd = vstralloc(cmd, " ", arg, NULL);
    } else {
	changer_cmd = cmd;
    }
    exitcode = changer_command(changer_cmd);
    if(changer_cmd != cmd) {
	amfree(changer_cmd);
    }
    s = changer_resultstr;
    ch = *s++;

    skip_whitespace(s, ch);
    if(ch == '\0') return report_bad_resultstr();
    slot = s - 1;
    skip_non_whitespace(s, ch);
    s[-1] = '\0';
    *slotstr = newstralloc(*slotstr, slot);
    s[-1] = ch;

    skip_whitespace(s, ch);
    *rest = s - 1;

    if(exitcode) {
	if(ch == '\0') return report_bad_resultstr();
	result_copy = stralloc(s - 1);
	amfree(changer_resultstr);
	changer_resultstr = result_copy;
	return exitcode;
    }
    return 0;
}

int changer_reset(slotstr)
char **slotstr;
{
    char *rest;

    return run_changer_command("-reset", (char *) NULL, slotstr, &rest);
}

int changer_clean(slotstr)
char **slotstr;
{
    char *rest;

    return run_changer_command("-clean", (char *) NULL, slotstr, &rest);
}

int changer_eject(slotstr)
char **slotstr;
{
    char *rest;

    return run_changer_command("-eject", (char *) NULL, slotstr, &rest);
}

int changer_loadslot(inslotstr, outslotstr, devicename)
char *inslotstr, **outslotstr, **devicename;
{
    char *rest;
    int rc;

    rc = run_changer_command("-slot", inslotstr, outslotstr, &rest);
    if(rc) return rc;

    if(*rest == '\0') return report_bad_resultstr();

    *devicename = newstralloc(*devicename, rest);
    return 0;
}

/* This function is somewhat equal to changer_info with one additional
   parameter, to get information, if the changer is able to search for
   tapelabels himself. E.g. Barcodereader
   The changer_script answers with an additional parameter, if it is able
   to search. This one should be 1, if it is able to search, and 0 if it
   knows about the extension. If the additional answer is omitted, the
   changer is not able to search for a tape. 
*/
int changer_query(nslotsp, curslotstr, backwardsp, searchable)
int *nslotsp, *backwardsp, *searchable;
char **curslotstr;
{
    char *rest;
    int rc;

    rc = run_changer_command("-info", (char *) NULL, curslotstr, &rest);
    if(rc) return rc;

    if (sscanf(rest, "%d %d %d", nslotsp, backwardsp, searchable) != 3) {
      if (sscanf(rest, "%d %d", nslotsp, backwardsp) != 2) {
        return report_bad_resultstr();
      } else {
        *searchable = 0;
      }
    }
    return 0;
}

int changer_info(nslotsp, curslotstr, backwardsp)
int *nslotsp, *backwardsp;
char **curslotstr;
{
    char *rest;
    int rc;

    rc = run_changer_command("-info", (char *) NULL, curslotstr, &rest);
    if(rc) return rc;

    if (sscanf(rest, "%d %d", nslotsp, backwardsp) != 2) {
	return report_bad_resultstr();
    }
    return 0;
}


/* ---------------------------- */

void changer_scan(user_init, user_slot)
int (*user_init) P((int rc, int nslots, int backwards));
int (*user_slot) P((int rc, char *slotstr, char *device));
{
    char *slotstr, *device = NULL, *curslotstr = NULL;
    int nslots, checked, backwards, rc, done;

    rc = changer_info(&nslots, &curslotstr, &backwards);
    done = user_init(rc, nslots, backwards);
    amfree(curslotstr);

    slotstr = "current";
    checked = 0;

    while(!done && checked < nslots) {
	rc = changer_loadslot(slotstr, &curslotstr, &device);
	if(rc > 0)
	    done = user_slot(rc, curslotstr, device);
	else if(!done)
	    done = user_slot(0,  curslotstr, device);
	amfree(curslotstr);
	amfree(device);

	checked += 1;
	slotstr = "next";
    }
}

/* This function first uses searchlabel and changer_search, if
   the library is able to find a tape itself. If it is not, or if 
   the tape could not be found, then the normal scan is done like 
   in changer_scan.
*/
void changer_find(user_init, user_slot,searchlabel)
int (*user_init) P((int rc, int nslots, int backwards));
int (*user_slot) P((int rc, char *slotstr, char *device));
char *searchlabel;
{
    char *slotstr, *device = NULL, *curslotstr = NULL;
    int nslots, checked, backwards, rc, done, searchable;

    rc = changer_query(&nslots, &curslotstr, &backwards,&searchable);
    done = user_init(rc, nslots, backwards);
    amfree(curslotstr);

    if ((searchlabel!=NULL) && searchable && !done){
      rc=changer_search(searchlabel,&curslotstr,&device);
      if(rc == 0)
        done = user_slot(rc,curslotstr,device);
    }
 
    slotstr = "current";
    checked = 0;

    while(!done && checked < nslots) {
	rc = changer_loadslot(slotstr, &curslotstr, &device);
	if(rc > 0)
	    done = user_slot(rc, curslotstr, device);
	else if(!done)
	    done = user_slot(0,  curslotstr, device);
	amfree(curslotstr);
	amfree(device);

	checked += 1;
	slotstr = "next";
    }
}

/* ---------------------------- */

void changer_current(user_init, user_slot)
int (*user_init) P((int rc, int nslots, int backwards));
int (*user_slot) P((int rc, char *slotstr, char *device));
{
    char *device = NULL, *curslotstr = NULL;
    int nslots, checked, backwards, rc, done;

    rc = changer_info(&nslots, &curslotstr, &backwards);
    done = user_init(rc, nslots, backwards);
    amfree(curslotstr);

    checked = 0;

    rc = changer_loadslot("current", &curslotstr, &device);
    if(rc > 0) {
	done = user_slot(rc, curslotstr, device);
    } else if(!done) {
	done = user_slot(0,  curslotstr, device);
    }
    amfree(curslotstr);
    amfree(device);
}

/* ---------------------------- */

static int changer_command(cmdstr)
char *cmdstr;
{
    FILE *cmdpipe;
    char *cmd = NULL;
    char *cmd_and_io = NULL;
    int exitcode;
    char number[NUM_STR_SIZE];

    if (*tapechanger != '/') {
	cmd = vstralloc(libexecdir, "/", tapechanger, versionsuffix(),
			" ", cmdstr,
			NULL);
    } else {
	cmd = vstralloc(tapechanger, " ", cmdstr, NULL);
    }
    cmd_and_io = stralloc2(cmd, " 2>&1");

/* fprintf(stderr, "changer: opening pipe from: %s\n", cmd); */

    amfree(changer_resultstr);

    if((cmdpipe = popen(cmd_and_io, "r")) == NULL) {
	changer_resultstr = vstralloc ("<error> ",
				       "could not open pipe to \"",
				       cmd,
				       "\": ",
				       strerror(errno),
				       NULL);
	amfree(cmd);
	amfree(cmd_and_io);
	return 2;
    }
    amfree(cmd_and_io);

    if((changer_resultstr = agets(cmdpipe)) == NULL) {
	changer_resultstr = vstralloc ("<error> ",
				       "could not read result from \"",
				       cmd,
				       errno ? "\": " : "\"",
				       errno ? strerror(errno) : "",
				       NULL);
    }

    exitcode = pclose(cmdpipe);
    cmdpipe = NULL;
    /* mark out-of-control changers as fatal error */
    if(WIFSIGNALED(exitcode)) {
	snprintf(number, sizeof(number), "%d", WTERMSIG(exitcode));
	cmd = newvstralloc(cmd,
			   "<error> ",
			   changer_resultstr,
			   " (got signal ", number, ")",
			   NULL);
	amfree(changer_resultstr);
	changer_resultstr = cmd;
	cmd = NULL;
	exitcode = 2;
    } else {
	exitcode = WEXITSTATUS(exitcode);
    }

/* fprintf(stderr, "changer: got exit: %d str: %s\n", exitcode, resultstr); */

    amfree(cmd);
    return exitcode;
}

/* This function commands the changerscript to look for a tape named
   searchlabel. If is found, the changerscript answers with the device,
   in which the tape can be accessed.
*/
int changer_search(searchlabel, outslotstr, devicename)
char *searchlabel, **outslotstr, **devicename;
{
    char *rest;
    int rc;

    rc = run_changer_command("-search", searchlabel, outslotstr, &rest);
    if(rc) return rc;

    if(*rest == '\0') return report_bad_resultstr();

    *devicename = newstralloc(*devicename, rest);
    return 0;
}

/* Because barcodelabel are short, and may not be the same as the 
   amandalabels, the changerscript should be informed, which tapelabel
   is associated with a tape. This function should be called after 
   giving a label for a tape. (Maybe also, when the label and the associated
   slot is known. e.g. during library scan.
*/
void changer_label (slotsp,labelstr)
int slotsp; 
char *labelstr;
{
/* only dummy at the moment */
}
