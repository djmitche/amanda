/* chg-chio

   This program was written to control the Seagate/Conner/Archive
   autoloading DAT drive.  This drive normally has 4 tape capacity
   but can be expanded to 12 tapes with an optional tape cartridge.
   This program may also work on other drives.  Try it and let me
   know of successes/failures.

   I have attempted to conform to the requirements for Amanda tape
   changer interface.  There could be some bugs.  

   This program works for me under Linux with Gerd Knorr's 
   <kraxel@cs.tu-berlin.de> SCSI media changer driver installed 
   as a kernel module.  The kernel module is available at 
   http://sunsite.unc.edu/pub/Linux/kernel/patches/scsi/scsi-changer*
   Since the Linux media changer is based on NetBSD, this program
   should also work for NetBSD, although I have not tried it.
   It may be necessary to change the IOCTL calls to work on other
   OS's.  


   (c) 1897 Larry Pyeatt,  pyeatt@cs.colostate.edu 
   All Rights Reserved.

   Permission to use, copy, modify, distribute, and sell this software and its
   documentation for any purpose is hereby granted without fee, provided that
   the above copyright notice appear in all copies and that both that
   copyright notice and this permission notice appear in supporting
   documentation.  The author makes no representations about the
   suitability of this software for any purpose.   It is provided "as is"
   without express or implied warranty.
   */

#include "amanda.h"
#include "conffile.h"

#if defined(HAVE_CHIO_H) || defined(HAVE_SYS_CHIO_H)

/* This include comes with Gerd Knor's SCSI media changer driver.
   If you are porting to another system, this is the file that defines
   ioctl calls for the changer.  You will have to track it down yourself
   and possibly change all the ioctl() calls in this program.  
   */
#if defined(HAVE_CHIO_H)
#  include <chio.h>
#else /* HAVE_SYS_CHIO_H must be defined */
#  include <sys/chio.h>
#endif

int loaded;

/*  The tape drive does not have an idea of current slot so
    we use a file to store the current slot.  It is not ideal
    but it gets the job done  */
int get_current_slot(count_file)
    char *count_file;
{
    FILE *inf;
    int retval;
    if ((inf=fopen(count_file,"r")) == NULL) {
	printf("unable to open current slot file\n");
	return 0;
    }
    fscanf(inf,"%d",&retval);
    afclose(inf);
    return retval;
}

void put_current_slot(count_file, slot)
    char *count_file;
    int slot;
{
    FILE *inf;
    if ((inf=fopen(count_file,"w")) == NULL) {
	printf("unable to open current slot file\n");
	exit(2);
    }
    fprintf(inf,"%d",slot);
    afclose(inf);
}


/* this routine checks a specified slot to see if it is empty */
int isempty(fd, slot, nslots)
    int fd, slot, nslots;
{
    struct changer_element_status  ces;
    int                            i,rc;
    int type=CHET_ST;

    ces.ces_type = type;
    ces.ces_data = alloc(nslots);

    rc = ioctl(fd,CHIOGSTATUS,&ces);
    if (rc) {
	fprintf(stderr,"ioctl failed: 0x%x %s\n",rc,strerror(errno));
	exit(1);
    }

    i = ces.ces_data[slot] & CESTATUS_FULL;

    amfree(ces.ces_data);
    return !i;
}

/* find the first empty slot */
int find_empty(fd, count)
    int fd, count;
{
    struct changer_element_status  ces;
    int                            i,rc;
    int type=CHET_ST;

    ces.ces_type = type;
    ces.ces_data = alloc(count);

    rc = ioctl(fd,CHIOGSTATUS,&ces);
    if (rc) {
	fprintf(stderr,"ioctl failed: 0x%x %s\n",rc,strerror(errno));
	exit(1);
    }

    i = 0; 
    while ((i < count)&&(ces.ces_data[i] & CESTATUS_FULL))
	i++;
    amfree(ces.ces_data);
    return i;
}

/* returns one if there is a tape loaded in the drive */
int drive_loaded(fd, drivenum)
    int fd, drivenum;
{
    struct changer_element_status  ces;
    int                            i,rc;
    int type=CHET_DT;

    ces.ces_type = type;
    ces.ces_data = alloc(1);

    rc = ioctl(fd,CHIOGSTATUS,&ces);
    if (rc) {
	fprintf(stderr,"ioctl failed: 0x%x %s\n",rc,strerror(errno));
	exit(1);
    }

    i = (ces.ces_data[0] & CESTATUS_FULL);

    amfree(ces.ces_data);
    return i;
}


/* unloads the drive, putting the tape in the specified slot */
void unload(int fd, int drive, int slot)
{
    struct changer_move  move;
    int rc;

    if (loaded) {
      move.cm_fromtype = CHET_DT;
      move.cm_fromunit = drive;
      move.cm_totype = CHET_ST;
      move.cm_tounit = slot;
      move.cm_flags = 0;

      rc = ioctl(fd,CHIOMOVE,&move);
      if (rc){
	fprintf(stderr,"ioctl failed (MOVE): 0x%x %s\n",
		rc,strerror(errno));
	exit(2);
      }
    }
}


/* moves tape from the specified slot into the drive */
void load(int fd, int drive, int slot)
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
	fprintf(stderr,"ioctl failed (MOVE): 0x%x %s\n",
		rc,strerror(errno));
	exit(2);
    }
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
#define SLOT_ADVANCE 5
argument slotdefs[]={{"current",SLOT_CUR,0},
		     {"next",SLOT_NEXT,0},
		     {"prev",SLOT_PREV,0},
		     {"first",SLOT_FIRST,0},
		     {"last",SLOT_LAST,0},
		     {"advance",SLOT_ADVANCE,0}};

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
int get_relative_target(fd, nslots, parameter, loaded, changer_file)
    int fd, nslots, loaded;
    char *parameter, *changer_file;
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
	case SLOT_ADVANCE:
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
	    aclose(fd);
	    exit(2);
    };
}

/* ----------------------------------------------------------------------*/

int main(argc, argv)
    int argc;
    char *argv[];
{
    int target,oldtarget;
    command com;   /* a little DOS joke */
    int fd, rc;

    struct changer_params params;
    char *changer_dev, *changer_file, *tape_device;

    for(fd = 3; fd < FD_SETSIZE; fd++) {
	/*
	 * Make sure nobody spoofs us with a lot of extra open files
	 * that would cause an open we do to get a very high file
	 * descriptor, which in turn might be used as an index into
	 * an array (e.g. an fd_set).
	 */
	close(fd);
    }

    set_pname("chg-chio");

    parse_args(argc,argv,&com);

    if (read_conffile(CONFFILE_NAME)) {
	perror(CONFFILE_NAME);
	exit(1);
    }

    changer_dev = getconf_str(CNF_CHNGRDEV);
    changer_file = getconf_str(CNF_CHNGRFILE);
    tape_device = getconf_str(CNF_TAPEDEV);

    /* get info about the changer */
    if (-1 == (fd = open(changer_dev,O_RDWR))) {
	perror("open");
	return 2;
    }

    rc = ioctl(fd,CHIOGPARAMS,&params);
    if (rc) {
	fprintf(stderr,"ioctl failed: 0x%x %s\n",rc,strerror(errno));
	return 2;
    }

    loaded = drive_loaded(fd,0);

    switch(com.command_code) {
	case COM_SLOT:  /* slot changing command */
	    if (is_positive_number(com.parameter)) {
		if ((target = atoi(com.parameter))>=params.cp_nslots) {
		    printf("<none> no slot `%d'\n",target);
		    aclose(fd);
		    return 2;
		}
	    } else
		target=get_relative_target(fd,params.cp_nslots,
					   com.parameter,loaded,changer_file);
	    if (loaded) {
		oldtarget=get_current_slot(changer_file);
		if (oldtarget!=target) {
		    unload(fd,0,oldtarget);
		    loaded=0;
		}
	    }
	    put_current_slot(changer_file, target);
	    if (!loaded&&isempty(fd,target,params.cp_nslots)) {
		printf("%d slot %d is empty\n",target,target);
		aclose(fd);
		return 1;
	    }
	    if (strcmp(com.parameter,"advance")==0) {
		tape_device = "/dev/null";
	    } else {
		if (!loaded) {
		    load(fd,0,target);
		}
	    }
	    printf("%d %s\n", target, tape_device);
	    break;

	case COM_INFO:
	    printf("%d ",get_current_slot(changer_file));
	    printf("%d 1\n",params.cp_nslots);
	    break;

	case COM_RESET:
	    target=get_current_slot(changer_file);
	    if (loaded) {
		if (!isempty(fd,target,params.cp_nslots))
		    target=find_empty(fd,params.cp_nslots);
		unload(fd,0,target);
	    }

	    if (isempty(fd,0,params.cp_nslots)) {
		printf("0 slot 0 is empty\n");
		aclose(fd);
		return 1;
	    }

	    load(fd,0,0);
	    put_current_slot(changer_file, 0);
	    printf("%d %s\n", get_current_slot(changer_file), tape_device);
	    break;

	case COM_EJECT:
	    if (loaded) {
		target=get_current_slot(changer_file);
		unload(fd, 0, target);
		printf("%d %s\n", 0, tape_device);
	    } else {
	        printf("%d %s\n", 0, "drive was not loaded");
	    }
	    break;
      };

    aclose(fd);
    return 0;
}

#else
int main(argc, argv)
    int argc;
    char *argv[];
{
	fprintf(stderr, "%s: no changer support compiled in.\n", argv[0]);
	fprintf(stderr, "See chg-chio.c for more information.\n");
	return 2;
}
#endif /* HAVE_CHIO_H */
