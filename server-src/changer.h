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
 * $Id: changer.h,v 1.7 1998/11/18 07:36:58 oliva Exp $
 *
 * interface routines for tape changers
 */
#include "amanda.h"

extern char *changer_resultstr;

int changer_init P((void));
int changer_reset P((char **slotstr));
int changer_clean P((char **slotstr));
int changer_eject P((char **slotstr));
int changer_label P((int slotsp, char *labelstr));
int changer_info P((int *nslotsp, char **curslotstr, int *backwards));
int changer_query P((int *nslotsp, char **curslotstr, int *backwards,
		     int *searchable));
int changer_search P((char *searchlabel, char **outslotstr, char **devicename));
int changer_loadslot P((char *inslotstr, char **outslotstr, char **devicename));
void changer_current P((int (*user_init)(int rc, int nslots, int backwards),
		     int (*user_slot)(int rc, char *slotstr, char *device)));
void changer_scan P((int (*user_init)(int rc, int nslots, int backwards),
		     int (*user_slot)(int rc, char *slotstr, char *device)));
void changer_find P((int (*user_init)(int rc, int nslots, int backwards),
		     int (*user_slot)(int rc, char *slotstr, char *device),
                     char *searchlabel));
