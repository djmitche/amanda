/*
 *	$Id: chg-scsi.c,v 1.2 1998/02/26 19:24:01 jrj Exp $
 *
 *	chg-scsi.c -- generic SCSI changer driver
 *
 * 	This program provides a driver to control generic
 *	SCSI changers, no matter what platform.  The host/OS
 *	specific portions of the interface are implemented
 *	in libscsi.a, which contains a module for each host/OS.
 *	The actual interface for HP/UX is in scsi-hpux.c;
 *	chio is in scsi-chio.c, etc..  A prototype system
 *	dependent scsi interface file is in scsi-proto.c.
 *
 *	Copyright 1997, 1998 Eric Schnoebelen <eric@cirr.com>
 *
 * This module based upon seagate-changer, by Larry Pyeatt
 *					<pyeatt@cs.colostate.edu>
 *
 * The original introductory comments follow:
 *
 * This program was written to control the Seagate/Conner/Archive
 * autoloading DAT drive.  This drive normally has 4 tape capacity
 * but can be expanded to 12 tapes with an optional tape cartridge.
 * This program may also work on onther drives.  Try it and let me
 * know of successes/failures.
 *
 * I have attempted to conform to the requirements for Amanda tape
 * changer interface.  There could be some bugs.  
 *
 * This program works for me under Linux with Gerd Knorr's 
 * <kraxel@cs.tu-berlin.de> SCSI media changer driver installed 
 * as a kernel module.  The kernel module is available at 
 * http://sunsite.unc.edu/pub/Linux/kernel/patches/scsi/scsi-changer*
 * Since the Linux media changer is based on NetBSD, this program
 * should also work for NetBSD, although I have not tried it.
 * It may be necessary to change the IOCTL calls to work on other
 * OS's.  
 *
 * (c) 1897 Larry Pyeatt,  pyeatt@cs.colostate.edu 
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  The author makes no representations about the
 * suitability of this software for any purpose.   It is provided "as is"
 * without express or implied warranty.
 */

#include "config.h"
#include "amanda.h"
#include "conffile.h"
#include "libscsi.h"

/*  The tape drive does not have an idea of current slot so
 *  we use a file to store the current slot.  It is not ideal
 *  but it gets the job done  
 */
int get_current_slot(char *count_file)
{
    FILE *inf;
    int retval;
    if ((inf=fopen(count_file,"r")) == NULL) {
	fprintf(stderr, "%s: unable to open current slot file (%s)\n",
				get_pname(), count_file);
	return 0;
    }
    fscanf(inf,"%d",&retval);
    fclose(inf);
    return retval;
}

void put_current_slot(char *count_file,int slot)
{
    FILE *inf;

    if ((inf=fopen(count_file,"w")) == NULL) {
	fprintf(stderr, "%s: unable to open current slot file (%s)\n",
				get_pname(), count_file);
	exit(2);
    }
    fprintf(inf, "%d\n", slot);
    fclose(inf);
}

/* ---------------------------------------------------------------------- 
   This stuff deals with parsing the command line */

typedef struct com_arg
{
  char *str;
  int command_code;
  int takesparam;
} argument;


typedef struct com_stru
{
  int command_code;
  char *parameter;
} command;


/* major command line args */
#define COMCOUNT 4
#define COM_SLOT 0
#define COM_INFO 1
#define COM_RESET 2
#define COM_EJECT 3
argument argdefs[]={{"-slot",COM_SLOT,1},
		    {"-info",COM_INFO,0},
		    {"-reset",COM_RESET,0},
		    {"-eject",COM_EJECT,0}};


/* minor command line args */
#define SLOTCOUNT 5
#define SLOT_CUR 0
#define SLOT_NEXT 1
#define SLOT_PREV 2
#define SLOT_FIRST 3
#define SLOT_LAST 4
argument slotdefs[]={{"current",SLOT_CUR,0},
		     {"next",SLOT_NEXT,0},
		     {"prev",SLOT_PREV,0},
		     {"first",SLOT_FIRST,0},
		     {"last",SLOT_LAST,0}};

int is_positive_number(char *tmp) /* is the string a valid positive int? */
{
    int i=0;
    if ((tmp==NULL)||(tmp[0]==0))
	return 0;
    while ((tmp[i]>='0')&&(tmp[i]<='9')&&(tmp[i]!=0))
	i++;
    if (tmp[i]==0)
	return 1;
    else
	return 0;
}

void usage(char *argv[])
{
    printf("%s: Usage error.\n", argv[0]);
    exit(2);
}


void parse_args(int argc, char *argv[],command *rval)
{
    int i=0;
    if ((argc<2)||(argc>3))
	usage(argv);
    while ((i<COMCOUNT)&&(strcmp(argdefs[i].str,argv[1])))
	i++;
    if (i==COMCOUNT)
	usage(argv);
    rval->command_code = argdefs[i].command_code;
   if (argdefs[i].takesparam) {
	if (argc<3)
	    usage(argv);
	rval->parameter=argv[2];      
    }
    else {
	if (argc>2)
	    usage(argv);
	rval->parameter=0;
    }
}

/* used to find actual slot number from keywords next, prev, first, etc */
int get_relative_target(int fd,int nslots,char *parameter,int loaded, 
				char *changer_file)
{
    int current_slot,i;
    current_slot=get_current_slot(changer_file);

    i=0;
    while((i<SLOTCOUNT)&&(strcmp(slotdefs[i].str,parameter)))
	i++;

    switch(i) {
	case SLOT_CUR:
	    return current_slot;
	    break;
	case SLOT_NEXT:
	    if (++current_slot==nslots)
		return 0;
	    else
		return current_slot;
	    break;
	case SLOT_PREV:
	    if (--current_slot<0)
		return nslots-1;
	    else
		return current_slot;
	    break;
	case SLOT_FIRST:
	    return 0;
	    break;
	case SLOT_LAST:
	    return nslots-1;
	    break;
	default: 
	    printf("<none> no slot `%s'\n",parameter);
	    close(fd);
	    exit(2);
    };
}

/* ----------------------------------------------------------------------*/

int main(int argc, char *argv[])
{
    int loaded,target,oldtarget;
    command com;   /* a little DOS joke */
  
    /*
     * drive_num really should be something from the config file, but..
     * for now, it is set to zero, since most of the common changers
     * used by amanda only have one drive ( until someone wants to 
     * use an EXB60/120, or a Breece Hill Q45.. )
     */
    int	drive_num = 0;
    int fd, rc, slotcnt, drivecnt;
    int endstatus = 0;
    char *changer_dev, *changer_file, *tape_device;

    set_pname("chg-scsi");

    parse_args(argc,argv,&com);

    if(read_conffile(CONFFILE_NAME)) {
        perror(CONFFILE_NAME);
        exit(1);
    }

    changer_dev = getconf_str(CNF_CHNGRDEV);
    changer_file = getconf_str(CNF_CHNGRFILE);
    tape_device = getconf_str(CNF_TAPEDEV);

    /* get info about the changer */
    if (-1 == (fd = open(changer_dev,O_RDWR))) {
	int localerr = errno;
	fprintf(stderr, "%s: open: %s: %s\n", get_pname(), 
				changer_dev, strerror(localerr));
	printf("%s open: %s: %s\n", "<none>", changer_dev, strerror(localerr));
	return 2;
    }
    
    slotcnt = get_slot_count(fd);
    drivecnt = get_drive_count(fd);

    if (drive_num > drivecnt) {
	int localerr = errno;
	printf("%s drive number error (%d > %d)\n", "<none>", 
						drive_num, drivecnt);
	fprintf(stderr, "%s: requested drive number (%d) greater than "
			"number of supported drives (%d)\n", get_pname(), 
			drive_num, drivecnt);
	return 2;
    }

    loaded = drive_loaded(fd, drive_num);

    switch(com.command_code) {
	case COM_SLOT:  /* slot changing command */
	    if (is_positive_number(com.parameter)) {
		if ((target = atoi(com.parameter))>=slotcnt) {
		    printf("<none> no slot `%d'\n",target);
		    close(fd);
		    endstatus = 2;
		    break;
		}
	    } else
		target=get_relative_target(fd, slotcnt,
				   com.parameter, loaded, changer_file);
	    if (loaded) {
		oldtarget=get_current_slot(changer_file);
		if (oldtarget!=target) {
		    (void)unload(fd, drive_num, oldtarget);
		    loaded=0;
		}
	    }
	    put_current_slot(changer_file, target);
	    if (!loaded && isempty(fd, target)) {
		printf("%d slot %d is empty\n",target,target);
		close(fd);
		endstatus = 1;
		break;
	    }
	    if (!loaded)
		(void)load(fd, drive_num, target);
	    printf("%d %s\n", target, tape_device);
	    break;

	case COM_INFO:
	    printf("%d ", get_current_slot(changer_file));
	    printf("%d 1\n", slotcnt);
	    break;

	case COM_RESET:
	    target=get_current_slot(changer_file);
	    if (loaded) {
		if (!isempty(fd, target))
		    target=find_empty(fd);
		(void)unload(fd, drive_num, target);
	    }

	    if (isempty(fd, 0)) {
		printf("0 slot 0 is empty\n");
		close(fd);
		endstatus = 1;
		break;
	    }

	    (void)load(fd, 0, 0);
	    put_current_slot(changer_file, 0);
	    printf("%d %s\n", get_current_slot(changer_file), tape_device);
	    break;

	case COM_EJECT:
	    if (loaded) {
		target=get_current_slot(changer_file);
		(void)unload(fd, drive_num, target);
		printf("%d %s\n", target, tape_device);
	    } else {
		printf("%d %s\n", target, "drive was not loaded");
		endstatus = 1;
	    }
	    break;
      };

    close(fd);
    return endstatus;
}
