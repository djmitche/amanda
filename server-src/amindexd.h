/***************************************************************************
*
* File:          $RCSfile: amindexd.h,v $
* Part of:       
*
* Revision:      $Revision: 1.2 $
* Last Edited:   $Date: 1997/05/01 19:03:41 $
* Author:        $Author: oliva $
*
* Description:   
* Public Func:   
* History:       $Log: amindexd.h,v $
* History:       Revision 1.2  1997/05/01 19:03:41  oliva
* History:       Integrated amgetidx into sendbackup&dumper.
* History:
* History:       New command in configuration file: indexdir; it indicates the
* History:       subdirectory of amanda-index where index files should be stored.
* History:
* History:       Index files are now compressed in the server (since the server will
* History:       have to decompress them)
* History:
* History:       Removed RSH configuration from configure
* History:
* History:       Added check for index directory to self check
* History:
* History:       Changed amindexd and amtrmidx to use indexdir option.
* History:
* History:       Revision 1.1.1.1  1997/03/15 21:30:10  amcore
* History:       Mass import of 2.3.0.4 as-is.  We can remove generated files later.
* History:
* History:       Revision 1.7  1996/11/05 08:50:18  alan
* History:       removed #define of GREP since not needed
* History:
* History:       Revision 1.6  1996/10/29 08:32:14  alan
* History:       Pete Geenhuizen inspired changes to support logical disk names etc
* History:
* History:       Revision 1.5  1996/10/02 18:42:29  alan
* History:       removed define of WC since no longer needed
* History:
* History:       Revision 1.4  1996/10/01 18:21:34  alan
* History:       synchronization with Blair's changes
* History:
* History:       Revision 1.3  1996/07/18 10:16:09  alan
* History:       compress prog path now picked from config
* History:
* History:       Revision 1.2  1996/06/09 10:01:59  alan
* History:       made command paths specifiable
* History:
* History:       Revision 1.1  1996/05/22 09:09:53  alan
* History:       Initial revision
* History:
*
***************************************************************************/

#ifndef AMINDEXD
#define AMINDEXD

#define LONG_LINE 256

#include "disk_history.h"

/* state */
extern char local_hostname[MAX_HOSTNAME_LENGTH];	/* me! */
extern char remote_hostname[LONG_LINE];	/* the client */
extern char dump_hostname[LONG_LINE];	/* the machine we are restoring */
extern char disk_name[LONG_LINE];	/* the disk we are restoring */
extern char config[LONG_LINE];		/* the config we are restoring */
extern char date[LONG_LINE];

extern void reply P((int n, char *fmt, ...));
extern void lreply P((int n, char *fmt, ...));

extern int opaque_ls P((char *dir));
extern int translucent_ls P((char *dir));

#endif /* AMINDEXD */
