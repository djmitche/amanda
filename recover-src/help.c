/***************************************************************************
*
* File:          $RCSfile: help.c,v $
* Part of:       amrecover
*
* Revision:      $Revision: 1.1 $
* Last Edited:   $Date: 1997/05/13 02:15:33 $
* Author:        $Author: george $
*
* Notes:         
* Private Func:  
* History:       $Log: help.c,v $
* History:       Revision 1.1  1997/05/13 02:15:33  george
* History:       Move amrecover from client-src to recover-src.
* History:       Affected files are:
* History:          amrecover.c
* History:          amrecover.h
* History:          display_commands.c
* History:          extract_list.c
* History:          help.c
* History:          set_commands.c
* History:          uparse.c
* History:          uparse.h
* History:          uparse.y
* History:          uscan.c
* History:          uscan.l
* History:
* History:       Revision 1.1.1.1  1997/03/15 21:29:58  amcore
* History:       Mass import of 2.3.0.4 as-is.  We can remove generated files later.
* History:
* History:       Revision 1.2  1996/11/29 09:52:12  alan
* History:       rearranged, added lpwd, lcd, extract
* History:
* History:       Revision 1.1  1996/11/11 08:21:30  alan
* History:       Initial revision
* History:
*
***************************************************************************/

#include "amrecover.h"

/* print a list of valid commands */
void help_list P((void))
{
    printf("valid commands are:\n\n");

    printf("add path1 ...    - add to extraction list\n");
    printf("cd directory     - change cwd on virtual file system\n");
    printf("clear            - clear extraction list\n");
    printf("delete path1 ... - delete from extraction list\n");
    printf("extract          - extract selected files from tapes\n");
    printf("exit\n");
    printf("help\n");
    printf("history          - show dump history of disk\n");
    printf("list [filename]  - show extraction list, optionally writing to file\n");
    printf("lcd directory    - change cwd on local file system\n");
    printf("ls               - list directory on virtual file system\n");
    printf("lpwd             - show cwd on local file system\n");
    printf("pwd              - show cwd on virtual file system\n");
    printf("quit\n");
    printf("setdate {YYYY-MM-DD|--MM-DD|---DD} - set date of look\n");
    printf("setdisk diskname [mountpoint] - select disk on dump host\n");
    printf("sethost host     - select dump host\n");

    printf("\n");
}
