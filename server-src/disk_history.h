/***************************************************************************
*
* File:          $RCSfile: disk_history.h,v $
* Part of:       
*
* Revision:      $Revision: 1.1 $
* Last Edited:   $Date: 1997/03/15 21:30:10 $
* Author:        $Author: amcore $
*
* Description:   
* Public Func:   
* History:       $Log: disk_history.h,v $
* History:       Revision 1.1  1997/03/15 21:30:10  amcore
* History:       Initial revision
* History:
* History:       Revision 1.1  1996/12/04 13:17:59  th
* History:       Add amindex
* History:
* History:       Revision 1.1  1996/05/13 09:14:12  alan
* History:       Initial revision
* History:
*
***************************************************************************/

typedef struct DUMP_ITEM
{
    char date[11];
    int  level;
    char tape[256];
    int  file;

    struct DUMP_ITEM *next;
}
DUMP_ITEM;

extern void clear_list P((void));
extern void add_dump P((char *date, int level, char *tape, int file));
extern DUMP_ITEM *first_dump P((void));
extern DUMP_ITEM *next_dump P((DUMP_ITEM *item));
