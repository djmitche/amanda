/*
 *  $Id: chg-scsi.c,v 1.6.2.13 1999/03/16 20:43:23 th Exp $
 *
 *  chg-scsi.c -- generic SCSI changer driver
 *
 *  This program provides the framework to control
 *  SCSI changers. It is based on the original chg-scsi
 *  from Eric Schnoebelen <eric@cirr.com> (Original copyright below)
 *  The device dependent part is handled by scsi-changer-driver.c
 *  The SCSI OS interface is handled by scsi-ostype.c
 *
 *  Original copyrigths:
 *
 *  This program provides a driver to control generic
 *  SCSI changers, no matter what platform.  The host/OS
 *  specific portions of the interface are implemented
 *  in libscsi.a, which contains a module for each host/OS.
 *  The actual interface for HP/UX is in scsi-hpux.c;
 *  chio is in scsi-chio.c, etc..  A prototype system
 *  dependent scsi interface file is in scsi-proto.c.
 *
 *  Copyright 1997, 1998 Eric Schnoebelen <eric@cirr.com>
 *
 * This module based upon seagate-changer, by Larry Pyeatt
 *                  <pyeatt@cs.colostate.edu>
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
 *
 * Michael C. Povel 03.06.98 added ejetct_tape and sleep for external tape
 * devices, and changed some code to allow multiple drives to use their
 * own slots. Also added support for reserverd slots.
 * At the moment these parameters are hard coded and only tested under Linux
 * 
 */

#include "config.h"
#include "amanda.h"
#include "conffile.h"
#include "libscsi.h"
#include "scsi-defs.h"

char *tapestatfile = NULL;

/* So we have 3 devices, here will all the infos be stored after an
 * successfull open 
 */

OpenFiles_T *pChangerDev = NULL;
OpenFiles_T *pTapeDev = NULL;
OpenFiles_T *pTapeDevCtl = NULL;

/* Defined in scsi-changer-driver.c
 */
extern int ElementStatusValid;
extern ElementInfo_T *pMTE; /*Medium Transport Element */
extern ElementInfo_T *pSTE; /*Storage Element */
extern ElementInfo_T *pIEE; /*Import Export Element */
extern ElementInfo_T *pDTE; /*Data Transfer Element */
extern int MTE;             /*Counter for the above element types */
extern int STE;
extern int IEE;
extern int DTE;


/*----------------------------------------------------------------------------*/
/* Some stuff for our own configurationfile */
typedef struct {  /* The information we can get for any drive (configuration) */
  int drivenum;      /* Which drive to use in the library */
  int start;         /* Which is the first slot we may use */
  int end;           /* The last slot we are allowed to use */
  int cleanslot;     /* Where the cleaningcartridge stays */
  char *scsitapedev; /* Where can we send raw SCSI commands to the tape */
  char *device;      /* Which device is associated to the drivenum */
  char *slotfile;    /* Where we should have our memory */   
  char *cleanfile;   /* Where we count how many cleanings we did */
  char *timefile;    /* Where we count the time the tape was used*/
  char *tapestatfile;/* Where can we place some drive stats */
}config_t; 

typedef struct {
  int number_of_configs; /* How many different configurations are used */
  int eject;             /* Do the drives need an eject-command */
  int sleep;             /* How many seconds to wait for the drive to get ready */
  int cleanmax;          /* How many runs could be done with one cleaning tape */
  char *device;          /* Which device is our changer */
  char *labelfile;       /* Mapping from Barcode labels to volume labels */
  config_t *conf;
}changer_t;

typedef enum{
  NUMDRIVE,EJECT,SLEEP,CLEANMAX,DRIVE,START,END,CLEAN,DEVICE,STATFILE,CLEANFILE,DRIVENUM,
    CHANGERDEV,USAGECOUNT,SCSITAPEDEV, TAPESTATFILE, LABELFILE
    } token_t;

typedef struct {
  char *word;
  int token;
} tokentable_t;

tokentable_t t_table[]={
  { "number_configs",NUMDRIVE},
  { "eject",EJECT},
  { "sleep",SLEEP},
  { "cleanmax",CLEANMAX},
  { "config",DRIVE},
  { "startuse",START},
  { "enduse",END},
  { "cleancart",CLEAN},
  { "dev",DEVICE},
  { "statfile",STATFILE},
  { "cleanfile",CLEANFILE},
  { "drivenum",DRIVENUM},
  { "changerdev",CHANGERDEV},
  { "usagecount",USAGECOUNT},
  { "scsitapedev", SCSITAPEDEV},
  { "tapestatus", TAPESTATFILE},
  { "labelfile", LABELFILE},
  { NULL,-1 }
};

void init_changer_struct(changer_t *chg,int number_of_config)
     /* Initialize datasructures with default values */
{
  int i;
  
  chg->number_of_configs = number_of_config;
  chg->eject = 1;
  chg->sleep = 0;
  chg->cleanmax = 0;
  chg->device = NULL;
  chg->labelfile = NULL;
  chg->conf = malloc(sizeof(config_t)*number_of_config);
  if (chg->conf != NULL){
    for (i=0; i < number_of_config; i++){
      chg->conf[i].drivenum   = 0;
      chg->conf[i].start      = -1;
      chg->conf[i].end        = -1;
      chg->conf[i].cleanslot  = -1;
      chg->conf[i].device     = NULL;
      chg->conf[i].slotfile   = NULL;
      chg->conf[i].cleanfile  = NULL;
      chg->conf[i].timefile  = NULL;
      chg->conf[i].scsitapedev = NULL;
      chg->conf[i].tapestatfile = NULL;
    }
  }
}

void dump_changer_struct(changer_t chg)
     /* Dump of information for debug */
{
  int i;

  dbprintf(("Number of configurations: %d\n",chg.number_of_configs));
  dbprintf(("Tapes need eject: %s\n",(chg.eject>0?"Yes":"No")));
  dbprintf(("Tapes need sleep: %d seconds\n",chg.sleep));
  dbprintf(("Cleancycles     : %d\n",chg.cleanmax));
  dbprintf(("Changerdevice   : %s\n",chg.device));
  if (chg.labelfile != NULL)
    dbprintf(("Labelfile       : %s\n", chg.labelfile));
  for (i=0; i<chg.number_of_configs; i++){
    dbprintf(("Tapeconfig Nr: %d\n",i));
    dbprintf(("  Drivenumber   : %d\n",chg.conf[i].drivenum));
    dbprintf(("  Startslot     : %d\n",chg.conf[i].start));
    dbprintf(("  Endslot       : %d\n",chg.conf[i].end));
    dbprintf(("  Cleanslot     : %d\n",chg.conf[i].cleanslot));
    if (chg.conf[i].device != NULL)
      dbprintf(("  Devicename    : %s\n",chg.conf[i].device));
    else
      dbprintf(("  Devicename    : none\n"));
    if (chg.conf[i].scsitapedev != NULL)
      dbprintf(("  SCSITapedev   : %s\n",chg.conf[i].scsitapedev));
    else
      dbprintf(("  SCSITapedev   : none\n"));
    if (chg.conf[i].tapestatfile != NULL)
      dbprintf(("  statfile      : %s\n", chg.conf[i].tapestatfile));
    else
      dbprintf(("  statfile      : none\n"));
    if (chg.conf[i].slotfile != NULL)
      dbprintf(("  Slotfile      : %s\n",chg.conf[i].slotfile));
    else
      dbprintf(("  Slotfile      : none\n"));
    if (chg.conf[i].cleanfile != NULL)
      dbprintf(("  Cleanfile     : %s\n",chg.conf[i].cleanfile));
    else
      dbprintf(("  Cleanfile     : none\n"));
    if (chg.conf[i].timefile != NULL)
      dbprintf(("  Usagecount    : %s\n",chg.conf[i].timefile));
    else
      dbprintf(("  Usagecount    : none\n"));
  }
}

void free_changer_struct(changer_t *chg)
     /* Free all allocated memory */
{
  int i;

  if (chg->device != NULL)
    free(chg->device);
  for (i=0; i<chg->number_of_configs; i++){
    if (chg->conf[i].device != NULL)
      free(chg->conf[i].device);
    if (chg->conf[i].slotfile != NULL)
      free(chg->conf[i].slotfile);
    if (chg->conf[i].cleanfile != NULL)
      free(chg->conf[i].cleanfile);
    if (chg->conf[i].timefile != NULL)
      free(chg->conf[i].timefile);
  }
  if (chg->conf != NULL)
    free(chg->conf);
  chg->conf = NULL;
  chg->device = NULL;
}

void parse_line(char *linebuffer,int *token,char **value)
     /* This function parses a line, and returns the token an value */
{
  char *tok;
  int i;
  int ready = 0;
  *token = -1;  /* No Token found */
  tok=strtok(linebuffer," \t\n");

  while ((tok != NULL) && (tok[0]!='#')&&(ready==0)){
    if (*token != -1){
      *value=tok;
      ready=1;
    } else {
      i=0;
      while ((t_table[i].word != NULL)&&(*token==-1)){
        if (0==strcasecmp(t_table[i].word,tok)){
          *token=t_table[i].token;
        }
        i++;
      }
    }
    tok=strtok(NULL," \t\n");
  }
  return;
}

int read_config(char *configfile, changer_t *chg)
     /* This function reads the specified configfile and fills the structure */
{
  int numconf;
  FILE *file;
  int init_flag = 0;
  int drivenum=0;
  char linebuffer[256];
  int token;
  char *value;

  numconf = 1;  /* At least one configuration is assumed */
  /* If there are more, it should be the first entry in the configurationfile */

  if (NULL==(file=fopen(configfile,"r"))){
    return (-1);
  }
  while (!feof(file)){
    if (NULL!=fgets(linebuffer,255,file)){
      parse_line(linebuffer,&token,&value);
      if (token != -1){
        if (0==init_flag) {
          if (token != NUMDRIVE){
            init_changer_struct(chg,numconf);
          } else {
            numconf = atoi(value);
            init_changer_struct(chg,numconf);
          }
          init_flag=1;
        }
        switch (token){
        case NUMDRIVE: if (atoi(value) != numconf)
          fprintf(stderr,"Error: number_drives at wrong place, should be "\
                  "first in file\n");
        break;
        case EJECT:
          chg->eject = atoi(value);
          break;
        case SLEEP:
          chg->sleep = atoi(value);
          break;
        case LABELFILE:
          chg->labelfile = strdup(value);
          break;
        case CHANGERDEV:
          chg->device = strdup(value);
          break;
        case SCSITAPEDEV:
          chg->conf[drivenum].scsitapedev = strdup(value);
          break;
        case TAPESTATFILE:
          chg->conf[drivenum].tapestatfile = strdup(value);
          break;
        case CLEANMAX:
          chg->cleanmax = atoi(value);
          break;
        case DRIVE:
          drivenum = atoi(value);
          if(drivenum >= numconf){
            fprintf(stderr,"Error: drive must be less than number_drives\n");
          }
          break;
        case DRIVENUM:
          if (drivenum < numconf){
            chg->conf[drivenum].drivenum = atoi(value);
          } else {
            fprintf(stderr,"Error: drive is not less than number_drives"\
                    " drivenum ignored\n");
          }
          break;
        case START:
          if (drivenum < numconf){
            chg->conf[drivenum].start = atoi(value);
          } else {
            fprintf(stderr,"Error: drive is not less than number_drives"\
                    " startuse ignored\n");
          }
          break;
        case END:
          if (drivenum < numconf){
            chg->conf[drivenum].end = atoi(value);
          } else {
            fprintf(stderr,"Error: drive is not less than number_drives"\
                    " enduse ignored\n");
          }
          break;
        case CLEAN:
          if (drivenum < numconf){
            chg->conf[drivenum].cleanslot = atoi(value);
          } else {
            fprintf(stderr,"Error: drive is not less than number_drives"\
                    " cleanslot ignored\n");
          }
          break;
        case DEVICE:
          if (drivenum < numconf){
            chg->conf[drivenum].device = strdup(value);
          } else {
            fprintf(stderr,"Error: drive is not less than number_drives"\
                    " device ignored\n");
          }
          break;
        case STATFILE:
          if (drivenum < numconf){
            chg->conf[drivenum].slotfile = strdup(value);
          } else {
            fprintf(stderr,"Error: drive is not less than number_drives"\
                    " slotfile ignored\n");
          }
          break;
        case CLEANFILE:
          if (drivenum < numconf){
            chg->conf[drivenum].cleanfile = strdup(value);
          } else {
            fprintf(stderr,"Error: drive is not less than number_drives"\
                    " cleanfile ignored\n");
          }
          break;
        case USAGECOUNT:
          if (drivenum < numconf){
            chg->conf[drivenum].timefile = strdup(value);
          } else {
            fprintf(stderr,"Error: drive is not less than number_drives"\
                    " usagecount ignored\n");
          }
          break;
        default:
          fprintf(stderr,"Error: Unknown token\n");
          break;
        }
      }
    }
  }

  fclose(file);
  return 0;
}

/*----------------------------------------------------------------------------*/

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

/* Here we handle the mapping from Barcode to volume label
 */

char *MapBarCode(char *labelfile, char *vol, char *barcode, unsigned char action)
{
  FILE *fp;
  int version;
  LabelV1_T *plabel;
  int unusedpos = 0;
  int unusedrec = 0;
  int pos;
  int record = 0;
  int volseen = 0;

  if (( plabel = (LabelV1_T *)malloc(sizeof(LabelV1_T))) == NULL)
    {
      dbprintf(("MapBarCode : malloc failed\n"));
      return(NULL);
    }

  memset(plabel, 0, sizeof(LabelV1_T));

  if (access(labelfile, F_OK) == -1)
    {
      dbprintf(("MapBarCode : creating %s", labelfile));
      if ((fp = fopen(labelfile, "w+")) == NULL)
        {
          dbprintf((" failed\n"));
          return(NULL);
        }
      fprintf(fp,":%d:", LABEL_DB_VERSION);
      fclose(fp);
    }

  if ((fp = fopen(labelfile, "r+")) == NULL)
    {
      dbprintf(("MapBarCode : failed to open %s\n", labelfile));
      return(NULL);
    }
  
  fscanf(fp,":%d:", &version);
  dbprintf(("MapBarCode : DB version %d\n", version));

  pos = ftell(fp);

  while(fread(plabel, 1, sizeof(LabelV1_T), fp) > 0)
    {
      record++;
      dbprintf(("MapBarCode : (%d) VolTag %s, BarCode %s, inuse %d\n",record,
                plabel->voltag,
                plabel->barcode,
                plabel->valid));
      switch (action)
        {
        case BARCODE_PUT:
          if (plabel->valid == 0)
            {
              unusedpos = pos;
              unusedrec = record;
            }
          if (strcmp(plabel->voltag, vol) == 0)
            {
              volseen = record;
            }

          if (strcmp(plabel->barcode, barcode) == 0)
            {
              dbprintf(("MapBarCode : update entry\n"));
              fseek(fp, pos, SEEK_SET);
              plabel->valid = 1;
              strcpy(plabel->voltag, vol);
              fwrite(plabel, 1, sizeof(LabelV1_T), fp);
              fclose(fp);
              return(strdup(plabel->barcode));
            }
          break;
        case BARCODE_VOL:
          if (strcmp(plabel->voltag, vol) == 0)
            {
              dbprintf(("MapBarCode : VOL %s match\n", vol));
              fclose(fp);
              return(strdup(plabel->barcode));
            }
          break;
        case BARCODE_BARCODE:
          if (strcmp(plabel->barcode, barcode) == 0)
            {
              dbprintf(("MapBarCode : BARCODE %s match\n", barcode));
              fclose(fp);
              return(strdup(plabel->voltag));
            }
          
          break;
        default:
          break;
        }
      pos = ftell(fp);
    }

  if (action == BARCODE_PUT)
    {
      if (unusedpos != 0)
        {
          dbprintf(("MapBarCode : reuse record %d\n", unusedrec));
          fseek(fp, unusedpos, SEEK_SET);
        }
      
      strcpy(plabel->voltag, vol);
      strncpy(plabel->barcode, barcode, TAG_SIZE);
      plabel->valid = 1;
      fwrite(plabel, 1, sizeof(LabelV1_T), fp);
      fclose(fp);
      return(strdup(plabel->voltag));
    }
  return(NULL);
  fclose(fp);
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
#define COMCOUNT 8
#define COM_SLOT 0
#define COM_INFO 1
#define COM_RESET 2
#define COM_EJECT 3
#define COM_CLEAN 4
#define COM_LABEL 5
#define COM_SEARCH 6
#define COM_STATUS 7
argument argdefs[]={{"-slot",COM_SLOT,1},
                    {"-info",COM_INFO,0},
                    {"-reset",COM_RESET,0},
                    {"-eject",COM_EJECT,0},
                    {"-clean",COM_CLEAN,0},
                    {"-label",COM_LABEL,1},
                    {"-search",COM_SEARCH,1},
                    {"-status",COM_STATUS,1}};


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
  int cnt;
  printf("%s: Usage error.\n", argv[0]);
  for (cnt=0; cnt < COMCOUNT; cnt++){
    printf("      %s    %s",argv[0],argdefs[cnt].str);
    if (argdefs[cnt].takesparam)
      printf(" <param>\n");
    else
      printf("\n");
  }
  exit(2);
}


void parse_args(int argc, char *argv[],command *rval)
{
  int i=0;
  for (i=0; i < argc; i++)
    dbprintf(("ARG [%d] : %s\n", i, argv[i]));
  i = 0;
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
                        char *changer_file,int slot_offset,int maxslot)
{
  int current_slot,i;
  
  current_slot = get_current_slot(changer_file);

  if (current_slot > maxslot){
    current_slot = slot_offset;
  }
  if (current_slot < slot_offset){
    current_slot = slot_offset;
  }

  i=0;
  while((i<SLOTCOUNT)&&(strcmp(slotdefs[i].str,parameter)))
    i++;

  switch(i) {
  case SLOT_CUR:
    return current_slot;
    break;
  case SLOT_NEXT:
    if (++current_slot==nslots+slot_offset)
      return slot_offset;
    else
      return current_slot;
    break;
  case SLOT_PREV:
    if (--current_slot<slot_offset)
      return maxslot;
    else
      return current_slot;
    break;
  case SLOT_FIRST:
    return slot_offset;
    break;
  case SLOT_LAST:
    return maxslot;
    break;
  default: 
    printf("<none> no slot `%s'\n",parameter);
    close(fd);
    exit(2);
    break;
  };
}

int ask_clean(char *tapedev)
     /* This function should ask the drive if it wants to be cleaned */
{
  return get_clean_state(tapedev);
}

void clean_tape(int fd,char *tapedev,char *cnt_file, int drivenum, 
                int cleancart, int maxclean,char *usagetime)
     /* This function should move the cleaning cartridge into the drive */
{
  int counter=-1;
  if (cleancart == -1 ){
    return;
  }
  /* Now we should increment the counter */
  if (cnt_file != NULL){
    counter = get_current_slot(cnt_file);
    counter++;
    if (counter>=maxclean){
      /* Now we should inform the administrator */
      char *mail_cmd;
      FILE *mailf;
      mail_cmd = vstralloc(MAILER,
                           " -s", " \"", "AMANDA PROBLEM: PLEASE FIX", "\"",
                           " ", getconf_str(CNF_MAILTO),
                           NULL);
      if((mailf = popen(mail_cmd, "w")) == NULL){
        error("could not open pipe to \"%s\": %s",
              mail_cmd, strerror(errno));
        printf("Mail failed\n");
        return;
      }
      fprintf(mailf,"\nThe usage count of your cleaning tape in slot %d",
              cleancart);
      fprintf(mailf,"\nis more than %d. (cleanmax)",maxclean);
      fprintf(mailf,"\nTapedrive %s needs to be cleaned",tapedev);
      fprintf(mailf,"\nPlease insert a new cleaning tape and reset");
      fprintf(mailf,"\nthe countingfile %s",cnt_file);

      if(pclose(mailf) != 0)
        error("mail command failed: %s", mail_cmd);

      return;
    }
    put_current_slot(cnt_file,counter);
  }
  load(fd,drivenum,cleancart);
  /*
   * Hack, sleep for some time
   */

  sleep(60);

  if (drive_loaded(fd, drivenum))
    unload(fd,drivenum,cleancart);  
  unlink(usagetime);
}
/* ----------------------------------------------------------------------*/

int main(int argc, char *argv[])
{
  int loaded,target,oldtarget;
  command com;   /* a little DOS joke */
  changer_t chg;
  char *volstr;
  int x;

  /*
   * drive_num really should be something from the config file, but..
   * for now, it is set to zero, since most of the common changers
   * used by amanda only have one drive ( until someone wants to 
   * use an EXB60/120, or a Breece Hill Q45.. )
   */
  int    drive_num = 0;
  int need_eject = 0; /* Does the drive need an eject command ? */
  int need_sleep = 0; /* How many seconds to wait for the drive to get ready */
  int clean_slot = -1;
  int maxclean = 0;
  char *clean_file=NULL;
  char *time_file=NULL;

  int use_slots;
  int slot_offset;
  int confnum;

  int fd, slotcnt, drivecnt;
  int endstatus = 0;
  char *changer_dev, *tape_device;
  char *changer_file = NULL;
  char *scsitapedevice = NULL;

  set_pname("chg-scsi");
  dbopen();
  parse_args(argc,argv,&com);

  if(read_conffile(CONFFILE_NAME)) {
    perror(CONFFILE_NAME);
    exit(1);
  }

  changer_dev = getconf_str(CNF_CHNGRDEV);
  changer_file = getconf_str(CNF_CHNGRFILE);
  tape_device = getconf_str(CNF_TAPEDEV);

  /* Get the configuration parameters */
  if (strlen(tape_device)==1){
    read_config(changer_file,&chg);
    confnum=atoi(tape_device);
    use_slots    = chg.conf[confnum].end-chg.conf[confnum].start+1;
    slot_offset  = chg.conf[confnum].start;
    drive_num    = chg.conf[confnum].drivenum;
    need_eject   = chg.eject;
    need_sleep   = chg.sleep;
    clean_file   = strdup(chg.conf[confnum].cleanfile);
    clean_slot   = chg.conf[confnum].cleanslot;
    maxclean     = chg.cleanmax;
    if (NULL != chg.conf[confnum].timefile)
      time_file = strdup(chg.conf[confnum].timefile);
    if (NULL != chg.conf[confnum].slotfile)
      changer_file = strdup(chg.conf[confnum].slotfile);
    else
      changer_file = NULL;
    if (NULL != chg.conf[confnum].device)
      tape_device  = strdup(chg.conf[confnum].device);
    if (NULL != chg.device)
      changer_dev  = strdup(chg.device); 
    if (NULL != chg.conf[confnum].scsitapedev)
      scsitapedevice = strdup(chg.conf[confnum].scsitapedev);
    if (NULL != chg.conf[confnum].tapestatfile)
      tapestatfile = strdup(chg.conf[confnum].tapestatfile);
    dump_changer_struct(chg);
    /* get info about the changer */
    if (NULL == (pChangerDev = OpenDevice(changer_dev, "changer_dev"))) {
      int localerr = errno;
      fprintf(stderr, "%s: open: %s: %s\n", get_pname(), 
              changer_dev, strerror(localerr));
      printf("%s open: %s: %s\n", "<none>", changer_dev, strerror(localerr));
      dbprintf(("%s: open: %s: %s\n", get_pname(),
                changer_dev, strerror(localerr)));
      return 2;
    }

    fd = pChangerDev->fd;

    if (tape_device != NULL)
      {
        if ((pTapeDev = OpenDevice(tape_device, "tape_device")) == NULL)
          {
            dbprintf(("warning open of %s: failed\n",  tape_device));
          }
      }

    if (scsitapedevice != NULL)
      {
        if ((pTapeDevCtl = OpenDevice(scsitapedevice, "scsitapedevice")) == NULL)
          {
            printf("open: %s: failed\n", scsitapedevice);
            return(2);
          }
      } else {
        if (pTapeDev != NULL && pTapeDev->SCSI == 1)
          {
            pTapeDevCtl = pTapeDev;
          } else { 
            if (pChangerDev != NULL && pChangerDev->SCSI == 1)
              {
                pTapeDevCtl = pChangerDev;
              }
          }
      }

    if ((chg.conf[confnum].end == -1) || (chg.conf[confnum].start == -1)){
      slotcnt = get_slot_count(fd);
      use_slots    = slotcnt;
      slot_offset  = 0;
    }
    free_changer_struct(&chg);
  } else {
    /* get info about the changer */
    if (NULL == (pChangerDev = OpenDevice(changer_dev, "dev"))) {
      int localerr = errno;
      fprintf(stderr, "%s: open: %s: %s\n", get_pname(), 
              changer_dev, strerror(localerr));
      printf("%s open: %s: %s\n", "<none>", changer_dev, strerror(localerr));
      dbprintf(("%s: open: %s: %s\n", get_pname(),
                changer_dev, strerror(localerr)));
      return 2;
    }
    fd = pChangerDev->fd;
    slotcnt = get_slot_count(fd);
    use_slots    = slotcnt;
    slot_offset  = 0;
    drive_num    = 0;
    need_eject   = 0;
    need_sleep   = 0;
  }

  drivecnt = get_drive_count(fd);

  if (drive_num > drivecnt) {
    printf("%s drive number error (%d > %d)\n", "<none>", 
           drive_num, drivecnt);
    fprintf(stderr, "%s: requested drive number (%d) greater than "
            "number of supported drives (%d)\n", get_pname(), 
            drive_num, drivecnt);
    dbprintf(("%s: requested drive number (%d) greater than "
              "number of supported drives (%d)\n", get_pname(), 
              drive_num, drivecnt));
    if (pChangerDev != NULL)
      close(pChangerDev->fd);
    if (pTapeDev != NULL)
      close(pTapeDev->fd);
    if (pTapeDevCtl != NULL)
      close(pTapeDevCtl->fd);
    return 2;
  }

  loaded = drive_loaded(fd, drive_num);
  target = -1;

  switch(com.command_code) {
  case COM_STATUS:
    ChangerStatus(com.parameter);
    break;
  case COM_LABEL: /* Update BarCode/Label mapping file */
    MapBarCode(chg.labelfile, com.parameter, pDTE[drive_num].VolTag, BARCODE_PUT);
    printf("0 0 0\n");
    break;
  case COM_SEARCH:
    if (BarCode(fd) == 1)
      {
        dbprintf(("search : look for %s\n", com.parameter));
        if ((volstr = MapBarCode(chg.labelfile, com.parameter, "", BARCODE_VOL)) != NULL)
          {
            target = -1;
            for (x = 0; x < STE; x++)
              {
                if (strcmp(pSTE[x].VolTag, volstr) == 0)
                  {
                    dbprintf(("search : found slot %d\n", x));
                    target = x;
                  }
              }
            if (target == -1)
              {
                printf("Label %s not found \n",com.parameter);
                close(fd);
                endstatus = 2;
                break;
              }
          }
      }
  case COM_SLOT:  /* slot changing command */
    if (target == -1)
      {
        if (is_positive_number(com.parameter)) {
          if ((target = atoi(com.parameter))>=use_slots) {
            printf("<none> no slot `%d'\n",target);
            close(fd);
        endstatus = 2;
        break;
          } else {
            target = target+slot_offset;
          }
        } else
          target=get_relative_target(fd, use_slots,
                                     com.parameter,
                                     loaded, 
                                     changer_file,slot_offset,slot_offset+use_slots);
      }
    if (loaded) {
      oldtarget = get_current_slot(changer_file);
      
      if ((oldtarget)!=target) {
        if (need_eject)
          eject_tape(scsitapedevice, need_eject);
        (void)unload(fd, drive_num, oldtarget);
        if (ask_clean(scsitapedevice))
          clean_tape(fd,tape_device,clean_file,drive_num,
                     clean_slot,maxclean,time_file);
        loaded=0;
      }
    }
    
    put_current_slot(changer_file, target);
    
    if (!loaded && isempty(fd, target)) {
      printf("%d slot %d is empty\n",target-slot_offset,
             target-slot_offset);
      close(fd);
      endstatus = 1;
      break;
    }
    if (!loaded)
      {
        if (ask_clean(scsitapedevice))
          clean_tape(fd,tape_device,clean_file,drive_num,
                     clean_slot,maxclean,time_file);
        if (load(fd, drive_num, target) != 0) {
          printf("%d slot %d move failed\n",target-slot_offset,
                 target-slot_offset);  
          close(fd);
          endstatus = 2;
          break;
        }
      }

    if (need_sleep)
      if (Tape_Ready(scsitapedevice, need_sleep) == -1)
        {
          printf("tape not ready\n");
          endstatus = 2;
          break;
        }
    printf("%d %s\n", target-slot_offset, tape_device);
    break;

  case COM_INFO:
    loaded = get_current_slot(changer_file)-slot_offset;

    printf("%d %d 1", loaded, use_slots);

    if (BarCode(fd) == 1)
      {
        printf(" 1\n");
      } else {
        printf(" 0\n");
      }
    break;

  case COM_RESET:
    target=get_current_slot(changer_file);

    if (loaded) {
      if (!isempty(fd, target))
        target=find_empty(fd);
      if (need_eject)
        eject_tape(scsitapedevice, need_eject);
      (void)unload(fd, drive_num, target);
      if (ask_clean(scsitapedevice))
        clean_tape(fd,tape_device,clean_file,drive_num,clean_slot,
                   maxclean,time_file);
    }

    if (isempty(fd, slot_offset)) {
      printf("0 slot 0 is empty\n");
      close(fd);
      endstatus = 1;
      break;
    }

    if (load(fd, drive_num, slot_offset) != 0) {
      printf("%d slot %d move failed\n",drive_num,
             slot_offset);  
      close(fd);
      put_current_slot(changer_file, slot_offset);
      endstatus = 2;
      break;
    }
    
    put_current_slot(changer_file, slot_offset);

    if (need_sleep)
       if (Tape_Ready(scsitapedevice, need_sleep) == -1)
        {
          printf("tape not ready\n");
          endstatus = 2;
          break;
        }
    printf("%d %s\n", slot_offset, tape_device);
    break;

  case COM_EJECT:
    if (loaded) {
      target=get_current_slot(changer_file);
      
      if (need_eject)
        eject_tape(scsitapedevice, need_eject);
      (void)unload(fd, drive_num, target);
      if (ask_clean(scsitapedevice))
        clean_tape(fd,tape_device,clean_file,drive_num,clean_slot,
                   maxclean,time_file);
      printf("%d %s\n", target, tape_device);
    } else {
      printf("%d %s\n", target, "drive was not loaded");
      endstatus = 1;
    }
    break;
  case COM_CLEAN:
    if (loaded) {
      target=get_current_slot(changer_file);
      if (need_eject)
        eject_tape(scsitapedevice, need_eject);
      (void)unload(fd, drive_num, target);
    } 
    clean_tape(fd,tape_device,clean_file,drive_num,clean_slot,
               maxclean,time_file);
    printf("%s cleaned\n", tape_device);
    break;
  };
  
  if (pChangerDev != NULL)
    close(pChangerDev->fd);
 
  if (pTapeDev != NULL)
    close(pTapeDev->fd);

  if (pTapeDevCtl != NULL)
    close(pTapeDevCtl->fd);

  dbclose();
  return endstatus;
}
/*
 * Local variables:
 * indent-tabs-mode: nil
 * tab-width: 4
 * End:
 */
