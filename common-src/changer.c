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
 * changer.c - interface routines for tape changers
 */
#include "amanda.h"
#include "conffile.h"
#include "version.h"

#include "changer.h"

/*
 * If we don't have the new-style wait access functions, use our own,
 * compatible with old-style BSD systems at least.  Note that we don't
 * care about the case w_stopval == WSTOPPED since we don't ask to see
 * stopped processes, so should never get them from wait.
 */
#ifndef WEXITSTATUS
#   define WEXITSTATUS(r)       (((union wait *) &(r))->w_retcode)
#   define WTERMSIG(r)          (((union wait *) &(r))->w_termsig)

#   undef  WIFSIGNALED
#   define WIFSIGNALED(r)       (((union wait *) &(r))->w_termsig != 0)
#endif


#define ERRSTR_LEN	1024

char changer_resultstr[ERRSTR_LEN];

static char *tapechanger = NULL;

/* local functions */
static int changer_command P((char *cmdstr, char *resultstr, int resultlen));

int changer_init()
{
    tapechanger = getconf_str(CNF_TPCHANGER);
    return strcmp(tapechanger, "") != 0;
}


int changer_reset(slotstr)
char *slotstr;
{
    int exitcode, rc;
    char result_copy[256];

    exitcode = changer_command("-reset", changer_resultstr, ERRSTR_LEN);
    if(exitcode) {
	rc=sscanf(changer_resultstr,"%[^ \n\t] %[^\n]",slotstr,result_copy);
	if(rc != 2) goto bad_resultstr;
	strcpy(changer_resultstr, result_copy);
	return exitcode;
    }

    rc = sscanf(changer_resultstr, "%[^ \t\n] ", slotstr);
    if(rc != 1) goto bad_resultstr;
    return 0;

bad_resultstr:
    strncpy(result_copy, changer_resultstr, 256);
    result_copy[255] = '\0';
    sprintf(changer_resultstr, 
	    "badly formed result from changer: \"%s\"", result_copy);
    return 2;
}

int changer_eject(slotstr)
char *slotstr;
{
    int exitcode, rc;
    char result_copy[256];

    exitcode = changer_command("-eject", changer_resultstr, ERRSTR_LEN);
    if(exitcode) {
	rc=sscanf(changer_resultstr,"%[^ \n\t] %[^\n]",slotstr,result_copy);
	if(rc != 2) goto bad_resultstr;
	strcpy(changer_resultstr, result_copy);
	return exitcode;
    }

    rc = sscanf(changer_resultstr, "%[^ \t\n] ", slotstr);
    if(rc != 1) goto bad_resultstr;
    return 0;

bad_resultstr:
    strncpy(result_copy, changer_resultstr, 256);
    result_copy[255] = '\0';
    sprintf(changer_resultstr, 
	    "badly formed result from changer: \"%s\"", result_copy);
    return 2;
}

int changer_loadslot(inslotstr, outslotstr, devicename)
char *inslotstr, *outslotstr, *devicename;
{
    char cmd[64], result_copy[256];
    int exitcode, rc;
    
    sprintf(cmd, "-slot %s", inslotstr);

    if((exitcode = changer_command(cmd, changer_resultstr, ERRSTR_LEN)) != 0) {
	rc=sscanf(changer_resultstr,"%[^ \n\t] %[^\n]",outslotstr,result_copy);
	if(rc != 2) goto bad_resultstr;
	strcpy(changer_resultstr, result_copy);
	return exitcode;
    }

    rc = sscanf(changer_resultstr, "%[^ \t\n] %[^\n]", outslotstr, devicename);
    if(rc != 2) goto bad_resultstr;
    return 0;

bad_resultstr:
    strncpy(result_copy, changer_resultstr, 256);
    result_copy[255] = '\0';
    sprintf(changer_resultstr, 
	    "badly formed result from changer: \"%s\"", result_copy);
    return 2;
}

int changer_info(nslotsp, curslotstr, backwardsp)
int *nslotsp, *backwardsp;
char *curslotstr;
{
    char result_copy[256];
    int rc, exitcode;
    
    exitcode = changer_command("-info", changer_resultstr, ERRSTR_LEN);
    if(exitcode != 0) {
	rc=sscanf(changer_resultstr,"%[^ \n\t] %[^\n]",curslotstr,result_copy);
	if(rc != 2) goto bad_resultstr;
	strcpy(changer_resultstr, result_copy);
	return exitcode;
    }

    rc = sscanf(changer_resultstr, "%[^ \t\n] %d %d", 
		curslotstr, nslotsp, backwardsp);
    if(rc != 3) goto bad_resultstr;
    return 0;

bad_resultstr:
    strncpy(result_copy, changer_resultstr, 256);
    result_copy[255] = '\0';
    sprintf(changer_resultstr, 
	    "badly formed result from changer: \"%s\"", result_copy);
    return 2;
}


/* ---------------------------- */

void changer_scan(user_init, user_slot)
int (*user_init) P((int rc, int nslots, int backwards));
int (*user_slot) P((int rc, char *slotstr, char *device));
{
    char *slotstr, device[1024], curslotstr[80];
    int nslots, checked, backwards, rc, done;

    rc = changer_info(&nslots, curslotstr, &backwards);
    done = user_init(rc, nslots, backwards);

    slotstr = "current";
    checked = 0;

    while(!done && checked < nslots) {
	rc = changer_loadslot(slotstr, curslotstr, device);
	if(rc > 0)
	    done = user_slot(rc, curslotstr, device);
	else if(!done) 
	    done = user_slot(0,  curslotstr, device);

	checked += 1;
	slotstr = "next";
    }
}


/* ---------------------------- */

void changer_current(user_init, user_slot)
int (*user_init) P((int rc, int nslots, int backwards));
int (*user_slot) P((int rc, char *slotstr, char *device));
{
    char *slotstr, device[1024], curslotstr[80];
    int nslots, checked, backwards, rc, done;

    rc = changer_info(&nslots, curslotstr, &backwards);
    done = user_init(rc, nslots, backwards);

    slotstr = "current";
    checked = 0;

    rc = changer_loadslot(slotstr, curslotstr, device);
    if(rc > 0)
	done = user_slot(rc, curslotstr, device);
    else if(!done) 
	done = user_slot(0,  curslotstr, device);
}

/* ---------------------------- */

static int changer_command(cmdstr, resultstr, resultlen)
char *cmdstr, *resultstr;
int resultlen;
{
    FILE *cmdpipe;
    char cmd[ERRSTR_LEN], *chp;
    int exitcode;

    sprintf(cmd, "%s/%s%s %s", libexecdir, tapechanger, versionsuffix(), cmdstr);

/* fprintf(stderr, "changer: opening pipe from: %s\n", cmd); */

    if((cmdpipe = popen(cmd, "r")) == NULL)
	error("could not open pipe to \"%s\": %s", cmd, strerror(errno));
    
    if(fgets(resultstr, resultlen, cmdpipe) == NULL)
	error("could not read result from \"%s\": %s", cmd, strerror(errno));

    /* clip newline */
    chp = resultstr + strlen(resultstr) - 1;
    if(*chp == '\n') *chp = '\0';

    exitcode = pclose(cmdpipe);
    /* mask out-of-control changers as fatal error */
    if(WIFSIGNALED(exitcode)) {
	sprintf(cmd, " (got signal %d)", WTERMSIG(exitcode));
	strcat(resultstr, cmd);
	exitcode = 2;
    }
    else exitcode = WEXITSTATUS(exitcode);

/* fprintf(stderr, "changer: got exit: %d str: %s\n", exitcode, resultstr); */

    return exitcode;
}

