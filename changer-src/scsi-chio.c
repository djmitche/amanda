/*
 *	$Id: scsi-chio.c,v 1.5.4.1 1998/11/18 07:03:37 oliva Exp $
 *
 *	scsi-chio.c -- library routines to handle the changer
 *			support for chio based systems
 *
 *	Author: Eric Schnoebelen, eric@cirr.com
 *	based on work by: Larry Pyeatt,  pyeatt@cs.colostate.edu 
 *	Copyright: 1997, 1998 Eric Schnoebelen
 *		
 */

#include "config.h"
#include "amanda.h"

#if defined(HAVE_CHIO_H) || defined(HAVE_SYS_CHIO_H)

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>

/* This include comes with Gerd Knor's SCSI media changer driver.
 * If you are porting to another system, this is the file that defines
 * ioctl calls for the changer.  You will have to track it down yourself
 * and possibly change all the ioctl() calls in this program.  
 */

#if defined(HAVE_CHIO_H)
#  include <chio.h>
#else /* HAVE_SYS_CHIO_H must be defined */
#  include <sys/chio.h>
#endif

char *modname = "@(#)" __FILE__ 
		": SCSI support library for the chio(2) interface @(#)";

/*
 * cache the general changer information, for faster access elsewhere
 */
static struct changer_params changer_info;
static int changer_info_init = 0;

static int get_changer_info(fd)
{
int rc = 0;

    if ( !changer_info_init ) {
	rc = ioctl(fd, CHIOGPARAMS, &changer_info);
	changer_info_init++;
    }
    return (rc);
}

int get_clean_state(char *dev)
{
    int status;
    unsigned char *cmd;
    unsigned char buffer[255];
    int filenr;

    if ((filenr = open(dev, O_RDWR)) < 0) {
        perror(dev);
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));

    *((int *) buffer) = 0;      /* length of input data */
    *(((int *) buffer) + 1) = 100;     /* length of output buffer */

    cmd = (char *) (((int *) buffer) + 2);

    cmd[0] = 0x4d;         /* LOG SENSE  */
    cmd[2] = (1 << 6)|0x33;     /* PageControl, PageCode */
    cmd[7] = 00;                 /* allocation length hi */
    cmd[8] = 100;                 /* allocation length lo */

    status = ioctl(filenr, 1 /* SCSI_IOCTL_SEND_COMMAND */ , buffer);
/*
    if (status)
        printf("ioctl(SCSI_IOCTL_SEND_COMMAND) status\t= %u\n", status);
*/

    if (status)
        return 0;

    if ((buffer[16] & 0x1) == 1)
          return 1;

    return 0;

}
void eject_tape(char *tape)
/* This function ejects the tape from the drive */
{
int mtfd;
struct mtop mt_com;

    if ((mtfd = open(tape, O_RDWR)) < 0) {
        perror(tape);
        exit(2);
    }
    mt_com.mt_op = MTOFFL;
    mt_com.mt_count = 1;
    if (ioctl(mtfd, MTIOCTOP, (char *)&mt_com) < 0) {
/*
    If the drive already ejected the tape due an error, or because it
    was a cleaning tape, threre can be an error, which we should ignore 

       perror(tape);
       exit(2);
*/
    }
    close(mtfd);
}


/* 
 * this routine checks a specified slot to see if it is empty 
 */
int isempty(int fd, int slot)
{
struct changer_element_status  ces;
int                            i,rc;
int type=CHET_ST;

    get_changer_info(fd);

    ces.ces_type = type;
    ces.ces_data = malloc(changer_info.cp_nslots);

    rc = ioctl(fd, CHIOGSTATUS, &ces);
    if (rc) {
	fprintf(stderr,"%s: changer status query failed: 0x%x %s\n",
			get_pname(), rc,strerror(errno));
	return -1;
    }

    i = ces.ces_data[slot] & CESTATUS_FULL;

    free(ces.ces_data);
    return !i;
}

/*
 * find the first empty slot 
 */
int find_empty(int fd)
{
struct changer_element_status  ces;
int                            i,rc;
int type=CHET_ST;

    get_changer_info(fd);

    ces.ces_type = type;
    ces.ces_data = malloc(changer_info.cp_nslots);

    rc = ioctl(fd,CHIOGSTATUS,&ces);
    if (rc) {
	fprintf(stderr,"%s: changer status query failed: 0x%x %s\n",
			get_pname(), rc, strerror(errno));
	return -1;
    }

    i = 0; 
    while ((i < changer_info.cp_nslots)&&(ces.ces_data[i] & CESTATUS_FULL))
	i++;
    free(ces.ces_data);
    return i;
}

/*
 * returns one if there is a tape loaded in the drive 
 */
int drive_loaded(int fd, int drivenum)
{
struct changer_element_status  ces;
int                            i,rc;
int type=CHET_DT;

    get_changer_info(fd);

    ces.ces_type = type;
    ces.ces_data = malloc(changer_info.cp_ndrives);

    rc = ioctl(fd, CHIOGSTATUS, &ces);
    if (rc) {
	fprintf(stderr,"%s: drive status query failed: 0x%x %s\n",
			get_pname(), rc, strerror(errno));
	return -1;
    }

    i = (ces.ces_data[drivenum] & CESTATUS_FULL);

    free(ces.ces_data);
    return i;
}


/*
 * unloads the drive, putting the tape in the specified slot 
 */
int unload(int fd, int drive, int slot)
{
struct changer_move  move;
int rc;

    move.cm_fromtype = CHET_DT;
    move.cm_fromunit = drive;
    move.cm_totype = CHET_ST;
    move.cm_tounit = slot;
    move.cm_flags = 0;

    rc = ioctl(fd, CHIOMOVE, &move);
    if (rc){
	fprintf(stderr,"%s: drive unload failed (MOVE): 0x%x %s\n",
		get_pname(), rc, strerror(errno));
	return(-2);
    }
    return 0;
}


/*
 * moves tape from the specified slot into the drive 
 */
int load(int fd, int drive, int slot)
{
struct changer_move  move;
int rc;

    move.cm_fromtype = CHET_ST;
    move.cm_fromunit = slot;
    move.cm_totype = CHET_DT;
    move.cm_tounit = drive;
    move.cm_flags = 0;

    rc = ioctl(fd,CHIOMOVE,&move);
    if (rc){
	fprintf(stderr,"%s: drive load failed (MOVE): 0x%x %s\n",
		get_pname(), rc, strerror(errno));
	return(-2);
    }
    return(0);
}

int get_slot_count(int fd)
{ 
int rc;

    rc = get_changer_info(fd);
    if (rc) {
        fprintf(stderr, "%s: slot count query failed: 0x%x %s\n", 
			get_pname(), rc, strerror(errno));
        return -1;
    }

    return changer_info.cp_nslots;
}

int get_drive_count(int fd)
{ 
int rc;

    rc = get_changer_info(fd);
    if (rc) {
        fprintf(stderr, "%s: drive count query failed: 0x%x %s\n",
			get_pname(), rc, strerror(errno));
        return -1;
    }

    return changer_info.cp_ndrives;
}

/* This function should ask the drive if it is ready */
int Tape_Ready ( char *tapedev , char * changerdev, int changerfd, int wait)
{
  FILE *out=NULL;
  int cnt=0;
  
  if (strcmp(tapedev,changerdev) == 0)
    {
      sleep(wait);
      return(0);
    }
  
  while ((cnt < wait) && (NULL==(out=fopen(tapedev,"w+")))){
    cnt++;
    sleep(1);
  }
  if (out != NULL)
    fclose(out);
  return 0;
}

#endif
