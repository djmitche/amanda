#ifndef lint
static char rcsid[] = "$Id: chg-scsi.c,v 1.6.2.21.2.4 2001/08/13 22:23:32 jrjackson Exp $";
#endif
/*
 * 
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

#ifdef HAVE_DMALLOC_H
#include <dmalloc.h>
#endif


#include "conffile.h"
#include "libscsi.h"
#include "scsi-defs.h"
#include "tapeio.h"

char *tapestatfile = NULL;
FILE *debug_file = NULL;

char *config_name = NULL;
char *config_dir = NULL;

/* 
 * So we have 3 devices, here will all the infos be stored after an
 * successfull open 
 */

OpenFiles_T *pDev = NULL;

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

changer_t chg;

int do_inventory = 0;     /* Set if load/unload functions thinks an inventory should be done */
int clean_slot = -1;

typedef enum{
  NUMDRIVE,EJECT,SLEEP,CLEANMAX,DRIVE,START,END,CLEAN,DEVICE,STATFILE,CLEANFILE,DRIVENUM,
    CHANGERDEV,USAGECOUNT,SCSITAPEDEV, TAPESTATFILE, LABELFILE, CHANGERIDENT,
    TAPEIDENT, EMUBARCODE, HAVEBARCODE, DEBUGLEVEL
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
  { "changerident" , CHANGERIDENT},
  { "tapeident", TAPEIDENT},
  { "emubarcode", EMUBARCODE},
  { "havebarcode", HAVEBARCODE},
  { "debuglevel", DEBUGLEVEL},
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
  chg->havebarcode = 0;
  chg->emubarcode = 0;	
  chg->device = NULL;
  chg->labelfile = NULL;
  chg->debuglevel = NULL;
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
      chg->conf[i].changerident = NULL;
      chg->conf[i].tapeident = NULL;
    }
  } else {
   fprintf(stderr,"init_changer_struct malloc failed\n");
  }
}

void dump_changer_struct(changer_t chg)
     /* Dump of information for debug */
{
  int i;

  dbprintf(("Number of configurations: %d\n",chg.number_of_configs));
  dbprintf(("Tapes need eject: %s\n",(chg.eject>0?"Yes":"No")));
  dbprintf(("barcode reader  : %s\n",(chg.havebarcode>0?"Yes":"No")));
  if (chg.debuglevel != NULL)
     dbprintf(("debug level     : %s\n", chg.debuglevel));
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

    if (chg.conf[i].changerident != NULL)
      dbprintf(("  changerident  : %s\n",chg.conf[i].changerident));
    else
      dbprintf(("  changerident  : none\n"));

    if (chg.conf[i].scsitapedev != NULL)
      dbprintf(("  SCSITapedev   : %s\n",chg.conf[i].scsitapedev));
    else
      dbprintf(("  SCSITapedev   : none\n"));

    if (chg.conf[i].tapeident != NULL)
      dbprintf(("  tapeident     : %s\n",chg.conf[i].tapeident));
    else
      dbprintf(("  tapeident     : none\n"));

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
  char *p;

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
        case EMUBARCODE:
          chg->emubarcode = 1;
          break;
        case DEBUGLEVEL:
          chg->debuglevel = strdup(value);
          break;
        case EJECT:
          chg->eject = atoi(value);
          break;
        case HAVEBARCODE:
          chg->havebarcode = atoi(value);
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
        case CHANGERIDENT:
          chg->conf[drivenum].changerident = strdup(value);
          p = chg->conf[drivenum].changerident;
          while (*p != '\0')
          {
            if (*p == '_')
            {
              *p=' ';
            }
            p++;
          }
          break;
        case TAPEIDENT:
          chg->conf[drivenum].tapeident = strdup(value);
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
  int ret;          /* return value for the fscanf function */
  if ((inf=fopen(count_file,"r")) == NULL) {
    fprintf(stderr, "%s: unable to open current slot file (%s)\n",
            get_pname(), count_file);
    return 0;
  }
  ret = fscanf(inf,"%d",&retval);
  fclose(inf);
  
  /*
   * check if we got an result
   * if no set retval to -1 
  */
  if (ret == 0 || ret == EOF)
    {
      retval = -1;
    }

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

/* 
 * Here we handle the inventory DB
 * With this it should be possible to do an mapping
 * Barcode      -> Volume label
 * Volume Lable -> Barcode
 * Volume lable -> Slot number
 * 
 */

char *MapBarCode(char *labelfile, char *vol, char *barcode, unsigned char action, int slot, int from)
{
  FILE *fp;
  int version;
  LabelV1_T *plabel;
  LabelV2_T *plabelv2;
  int unusedpos = 0;
  int unusedrec = 0;
  int pos  = 0;
  int record = 0;
  int volseen = 0;
  char *retstr = malloc(17);    /* Return buffer to hold an int value formatted by an sprintf slot:from*/

  DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE,"MapBarCode : Parameter\n");
  DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE,"labelfile -> %s, vol -> %s, barcode -> %s, action -> %c, slot -> %d, from -> %d\n",
             labelfile,
             vol,
             barcode,
             action,
             slot,
             from);
  
  if (labelfile == NULL)
    {
      DebugPrint(DEBUG_ERROR,SECTION_MAP_BARCODE,"Got empty labelfile (NULL)\n");
      return(NULL);
    }
  if (access(labelfile, F_OK) == -1)
    {
      DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE, "MapBarCode : creating %s", labelfile);
      if ((fp = fopen(labelfile, "w+")) == NULL)
        {
          DebugPrint(DEBUG_ERROR,SECTION_MAP_BARCODE," failed\n");
          return(NULL);
        }
      fprintf(fp,":%d:", LABEL_DB_VERSION);
      fclose(fp);
    }
  
  if ((fp = fopen(labelfile, "r+")) == NULL)
    {
       DebugPrint(DEBUG_ERROR,SECTION_MAP_BARCODE,"MapBarCode : failed to open %s\n", labelfile);
      return(NULL);
    }
  
  fscanf(fp,":%d:", &version);
   DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE,"MapBarCode : DB version %d\n", version);

  pos = ftell(fp);

  if (version == 1)
    {
     if (( plabel = (LabelV1_T *)malloc(sizeof(LabelV1_T))) == NULL)
       {
         DebugPrint(DEBUG_ERROR,SECTION_MAP_BARCODE,"MapBarCode : malloc failed\n");
         return(NULL);
       }

     memset(plabel, 0, sizeof(LabelV1_T));
     while(fread(plabel, 1, sizeof(LabelV1_T), fp) > 0)
       {
         record++;
         DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE,"MapBarCode : (%d) VolTag %s, BarCode %s, inuse %d\n",record,
                   plabel->voltag,
                   plabel->barcode,
                   plabel->valid);
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
                 DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE,"MapBarCode : update entry\n");
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
                 DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE,"MapBarCode : VOL %s match\n", vol);
                 fclose(fp);
                 return(strdup(plabel->barcode));
               }
             break;
           case BARCODE_BARCODE:
             if (strcmp(plabel->barcode, barcode) == 0)
               {
                 DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE,"MapBarCode : BARCODE %s match\n", barcode);
                 fclose(fp);
                 return(strdup(plabel->voltag));
               }
          
             break;
           default:
             DebugPrint(DEBUG_ERROR,SECTION_MAP_BARCODE,"MapBarCode : unsupported function %d\n", action);
             break;
           }
         pos = ftell(fp);
       }
     /*
      * No match above, so create an new record ....
      */
     if (action == BARCODE_PUT)
       {
         if (unusedpos != 0)
           {
             DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE,"MapBarCode : reuse record %d\n", unusedrec);
             fseek(fp, unusedpos, SEEK_SET);
           }
         
         strcpy(plabel->voltag, vol);
         strncpy(plabel->barcode, barcode, TAG_SIZE);
         plabel->valid = 1;
         fwrite(plabel, 1, sizeof(LabelV1_T), fp);
         fclose(fp);
         return(strdup(plabel->voltag));
       }                                                                           
    }
  
  if (version == 2)
    {
     if (( plabelv2 = (LabelV2_T *)malloc(sizeof(LabelV2_T))) == NULL)
       {
         DebugPrint(DEBUG_ERROR,SECTION_MAP_BARCODE,"MapBarCode : malloc failed\n");
         return(NULL);
       }

     memset(plabelv2, 0, sizeof(LabelV2_T));
     while(fread(plabelv2, 1, sizeof(LabelV2_T), fp) > 0)
       {
         record++;
         DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE,"MapBarCode : (%d) VolTag %s, BarCode %s, inuse %d, slot %d, from %d\n",record,
                   plabelv2->voltag,
                   plabelv2->barcode,
                   plabelv2->valid,
		   plabelv2->slot,
		   plabelv2->from);
         switch (action)
           {
           case BARCODE_DUMP:
             printf("Slot -> %d, from -> %d, valid -> %d, Tag -> %s, Barcode -> %s\n",
                    plabelv2->slot,
                    plabelv2->from,
                    plabelv2->valid,
                    plabelv2->voltag,
                    plabelv2->barcode);
             break;
           case RESET_VALID:
             fseek(fp, pos, SEEK_SET);
             plabelv2->valid = 0;
             fwrite(plabelv2, 1, sizeof(LabelV2_T), fp);
             break;
           case BARCODE_PUT:
             if (plabelv2->valid == 0)
               {
                 unusedpos = pos;
                 unusedrec = record;
               }
             if (strcmp(plabelv2->voltag, vol) == 0)
               {
                 volseen = record;
               }

             if (strcmp(plabelv2->barcode, barcode) == 0)
               {
                 DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE,"MapBarCode : update entry\n");
                 fseek(fp, pos, SEEK_SET);
                 plabelv2->valid = 1;
                 plabelv2->from = from;
                 plabelv2->slot = slot;
                 strcpy(plabelv2->voltag, vol);
                 fwrite(plabelv2, 1, sizeof(LabelV2_T), fp);
                 fclose(fp);
                 return(strdup(plabelv2->barcode));
               }
             break;
           case FIND_SLOT:
             if (strcmp(plabelv2->voltag, vol) == 0)
               {
                 DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE,"MapBarCode FIND_SLOT : \n");
                 sprintf(retstr,"%d:%d",plabelv2->slot,plabelv2->from);
                 return(retstr);
               }
             break;
           case UPDATE_SLOT:
             if (strcmp(plabelv2->voltag, vol) == 0)
               {
                 DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE,"MapBarCode UPDATE_SLOT : update entry\n");
                 fseek(fp, pos, SEEK_SET);
                 strcpy(plabelv2->voltag, vol);
                 plabelv2->valid = 1;
                 plabelv2->slot = slot;
                 plabelv2->from = from;
                 fwrite(plabelv2, 1, sizeof(LabelV2_T), fp);
                 fclose(fp);
                 return(strdup(plabelv2->barcode));
               }
               break;
           case BARCODE_VOL:
             if (strcmp(plabelv2->voltag, vol) == 0)
               {
                 DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE,"MapBarCode : VOL %s match\n", vol);
                 fclose(fp);
                 return(strdup(plabelv2->barcode));
               }
             break;
           case BARCODE_BARCODE:
             if (strcmp(plabelv2->barcode, barcode) == 0)
               {
                 DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE,"MapBarCode : BARCODE %s match\n", barcode);
                 fclose(fp);
                 return(strdup(plabelv2->voltag));
               }
          
             break;
           default:
             break;
           }
         pos = ftell(fp);
       }
     if (action == BARCODE_PUT || action == UPDATE_SLOT )
       {
         memset(plabelv2, 0, sizeof(LabelV2_T));
         if (unusedpos != 0)
           {
             DebugPrint(DEBUG_INFO,SECTION_MAP_BARCODE,"MapBarCode : reuse record %d\n", unusedrec);
             fseek(fp, unusedpos, SEEK_SET);
           }
         
         strcpy(plabelv2->voltag, vol);
         strncpy(plabelv2->barcode, barcode, TAG_SIZE);
         plabelv2->valid = 1;
         plabelv2->from = from;
         plabelv2->slot = slot;
         fwrite(plabelv2, 1, sizeof(LabelV2_T), fp);
         fclose(fp);
         return(strdup(plabelv2->voltag));
       }                                                                           
    }

  fclose(fp);
  return(NULL);
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
#define COMCOUNT 13
#define COM_SLOT 0
#define COM_INFO 1
#define COM_RESET 2
#define COM_EJECT 3
#define COM_CLEAN 4
#define COM_LABEL 5
#define COM_SEARCH 6
#define COM_STATUS 7
#define COM_TRACE 8
#define COM_INVENTORY 9
#define COM_DUMPDB 10
#define COM_SCAN 11
#define COM_GEN_CONF 12
argument argdefs[]={{"-slot",COM_SLOT,1},
                    {"-info",COM_INFO,0},
                    {"-reset",COM_RESET,0},
                    {"-eject",COM_EJECT,0},
                    {"-clean",COM_CLEAN,0},
                    {"-label",COM_LABEL,1},
                    {"-search",COM_SEARCH,1},
                    {"-status",COM_STATUS,1},
                    {"-trace",COM_TRACE,1},
                    {"-inventory", COM_INVENTORY,0},
                    {"-dumpdb", COM_DUMPDB,0},
                    {"-scan", COM_SCAN,0},
                    {"-genconf", COM_GEN_CONF,0}
	};


/* minor command line args */
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
                     {"advance",SLOT_ADVANCE,0},
	};
#define SLOTCOUNT (sizeof(slotdefs) / sizeof(slotdefs[0]))

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
int get_relative_target(int fd,int nslots,char *parameter,int param_index,
			int loaded,char *changer_file,
			int slot_offset,int maxslot)
{
  int current_slot;
  
  current_slot = get_current_slot(changer_file);

  if (current_slot > maxslot){
    current_slot = slot_offset;
  }
  if (current_slot < slot_offset){
    current_slot = slot_offset;
  }

  switch(param_index) {
  case SLOT_CUR:
    return current_slot;
    break;
  case SLOT_NEXT:
  case SLOT_ADVANCE:
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
  int ret;

  ret = get_clean_state(tapedev);

  if (ret < 0) /* < 0 means query does not work ... */
  {
    return(0);
  }
  return ret;
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
  char *volstr;
  int x;

  /*
   * drive_num really should be something from the config file, but..
   * for now, it is set to zero, since most of the common changers
   * used by amanda only have one drive ( until someone wants to 
   * use an EXB60/120, or a Breece Hill Q45.. )
   */
  unsigned char emubarcode = 0;
  int drive_num = 0;
  int need_eject = 0; /* Does the drive need an eject command ? */
  int need_sleep = 0; /* How many seconds to wait for the drive to get ready */

  int maxclean = 0;
  char *clean_file=NULL;
  char *time_file=NULL;

  /*
   * For the label read fucntion
   */
  char *datestamp = NULL;
  char *label = NULL;
  char *result;

  /*
   * For the emubarcode stuff
   */
  int slot, from;

  int use_slots;
  int slot_offset;
  int confnum;

  int fd, slotcnt, drivecnt;
  int endstatus = 0;

  char *changer_dev;
  char *tape_device = NULL;
  char *changer_file = NULL;
  char *scsitapedevice = NULL;

  int param_index;

  chg.number_of_configs = 0;
  chg.eject = 0;
  chg.sleep = 0;
  chg.cleanmax = 0;
  chg.device = NULL;
  chg.labelfile = NULL;
  chg.conf = NULL;
#ifdef CHG_SCSI_STANDALONE
  printf("Ups standalone\n");
#else
  set_pname("chg-scsi");
  dbopen();

  dbprintf(("chg-scsi: %s\n",rcsid));
  ChangerDriverVersion();

  if (debug_file == NULL)
    {
        /*
      debug_file = fdopen((int)debug_fd(), "a");
        */
        debug_file = debug_fp();
    }
  
  parse_args(argc,argv,&com);

  pDev = (OpenFiles_T *)malloc(sizeof(OpenFiles_T) * CHG_MAXDEV);
  memset(pDev, 0, sizeof(OpenFiles_T) * CHG_MAXDEV );


  switch(com.command_code) 
    {
    case COM_SCAN:
      ScanBus(1);
      return(0);
      break;
    case COM_GEN_CONF:
      ScanBus(0);
      PrintConf();
      return(0);
      break;
    default:
      break;
    }

  if(read_conffile(CONFFILE_NAME)) {
    perror(CONFFILE_NAME);
    exit(1);
  }

  changer_dev = getconf_str(CNF_CHNGRDEV);
  changer_file = getconf_str(CNF_CHNGRFILE);
  tape_device = getconf_str(CNF_TAPEDEV);

  /* Get the configuration parameters */
  /* Attention, this will not support more than 10 tape devices 0-9 */
  /* */
  if (strlen(tape_device)==1){
    if (read_config(changer_file,&chg) == -1)
    {
      fprintf(stderr, "%s open: of %s failed\n", get_pname(), changer_file);
      return 2;
    }
    confnum=atoi(tape_device);
    if (chg.number_of_configs == 0)
    {
       fprintf(stderr,"%s: chg.conf[%d] == NULL\n",get_pname(), confnum);
       return(2);
    }
    use_slots    = chg.conf[confnum].end-chg.conf[confnum].start+1;
    slot_offset  = chg.conf[confnum].start;
    drive_num    = chg.conf[confnum].drivenum;
    need_eject   = chg.eject;
    need_sleep   = chg.sleep;
    clean_file   = strdup(chg.conf[confnum].cleanfile);
    clean_slot   = chg.conf[confnum].cleanslot;
    maxclean     = chg.cleanmax;
    emubarcode   = chg.emubarcode;
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


    /* 
     * The changer device.
     * If we can't open it fail with a message
     */

    if (OpenDevice(INDEX_CHANGER , changer_dev, "changer_dev", chg.conf[confnum].changerident) == 0)
      {
        int localerr = errno;
        fprintf(stderr, "%s: open: %s: %s\n", get_pname(), 
                changer_dev, strerror(localerr));
        printf("%s open: %s: %s\n", "<none>", changer_dev, strerror(localerr));
        dbprintf(("%s: open: %s: %s\n", get_pname(),
                  changer_dev, strerror(localerr)));
        return 2;
      }

    fd = INDEX_CHANGER;

    /*
     * The tape device.
     * We need it for:
     * eject if eject is set
     * inventory (reading of the labels) if emubarcode (not yet)
     */
    if (tape_device != NULL)
      {
        if (OpenDevice(INDEX_TAPE, tape_device, "tape_device", chg.conf[confnum].tapeident) == 0)
          {
            dbprintf(("warning open of %s: failed\n",  tape_device));
          }
      }

    /*
     * This is for the status pages of the SCSI tape, nice to have but no must....
     */
    if (scsitapedevice != NULL)
      {
        if (OpenDevice(INDEX_TAPECTL, scsitapedevice, "scsitapedevice", chg.conf[confnum].tapeident) == 0)
          {
            dbprintf(("warning open of %s: failed\n", scsitapedevice));
          }
      }
    

    /*
     * So if we need eject we need either an raw device to eject with an ioctl,
     * or an SCSI device to send the SCSI eject.
     */

    if (need_eject != 0 )
      {
        if (pDev[INDEX_TAPE].avail == 0 && pDev[INDEX_TAPECTL].avail == 0)
          {
            printf("No device found for tape eject");
            return(2);
          }
      }

	
    if ((chg.conf[confnum].end == -1) || (chg.conf[confnum].start == -1)){
      slotcnt = get_slot_count(fd);
      use_slots    = slotcnt;
      slot_offset  = 0;
    }
    /*
    free_changer_struct(&chg);
    */
  } else {
      printf("Please specify a number as tape_device [0-9]\n");
      dbprintf(("tape_device is not a number %s\n", tape_device));
      return 2;
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
    return 2;
  }

  loaded = drive_loaded(fd, drive_num);
  target = -1;

  switch(com.command_code) {
/* This is only for the experts ;-) */
  case COM_TRACE:
	ChangerReplay(com.parameter);
  break;
/*
*/
  case COM_DUMPDB:
    MapBarCode(chg.labelfile, "", "", BARCODE_DUMP, 0, 0);
    break;
  case COM_STATUS:
    ChangerStatus(com.parameter, chg.labelfile, BarCode(fd),changer_file, changer_dev, tape_device);
    break;
  case COM_LABEL: /* Update BarCode/Label mapping file */
    MapBarCode(chg.labelfile, com.parameter, pDTE[drive_num].VolTag, BARCODE_PUT, 0, 0);
    printf("0 0 0\n");
    break;
  case COM_INVENTORY:
    if (loaded)
      {
        oldtarget = get_current_slot(changer_file);
        if (oldtarget < 0)
          {
            dbprintf(("COM_SLOT: get_current_slot %d\n", oldtarget));
            oldtarget = find_empty(fd, slot_offset, use_slots);
            dbprintf(("COM_SLOT: find_empty %d\n", oldtarget));
          }
        if (need_eject)
          eject_tape(scsitapedevice, need_eject);
        (void)unload(fd, drive_num, oldtarget);
        if (ask_clean(scsitapedevice))
          clean_tape(fd,tape_device,clean_file,drive_num,
                     clean_slot,maxclean,time_file);
      }
    Inventory(chg.labelfile, drive_num, need_eject, 0, 0, clean_slot);
    break;
  case COM_SEARCH:
    if (BarCode(fd) == 1)
      {
        dbprintf(("search : look for %s\n", com.parameter));
        if ((volstr = MapBarCode(chg.labelfile, com.parameter, "", BARCODE_VOL,0 ,0)) != NULL)
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
    if (emubarcode == 1)
      {
        dbprintf(("search : look for %s\n", com.parameter));
        if ((volstr = MapBarCode(chg.labelfile, com.parameter, "", FIND_SLOT,0 ,0)) != NULL)
          {
            sscanf(volstr, "%d:%d", &slot, &from);
            target = slot;
          } else {
            printf("Label %s not found \n",com.parameter);
            close(fd);
            endstatus = 2;
            break;
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
        } else {
          param_index=0;
          while((param_index<SLOTCOUNT)
		&&(strcmp(slotdefs[param_index].str,com.parameter))) {
            param_index++;
	  }
          target=get_relative_target(fd, use_slots,
                                     com.parameter,param_index,
                                     loaded, 
                                     changer_file,
				     slot_offset,slot_offset+use_slots);
        }
      }

    if (loaded) {
      oldtarget = get_current_slot(changer_file);
      if (oldtarget < 0)
        {
          dbprintf(("COM_SLOT: get_current_slot %d\n", oldtarget));
          oldtarget = find_empty(fd, slot_offset, use_slots);
          dbprintf(("COM_SLOT: find_empty %d\n", oldtarget));
        }
      
      /*
       * TODO check if the request slot for the unload is empty
       */

      /*
       * If we have an SCSI path to the tape and an raw io path
       * try to read the Error Counter and the label
       */
      if (pDev[INDEX_TAPECTL].avail == 1 && pDev[INDEX_TAPE].avail == 1)
        {
          LogSense(INDEX_TAPE);
        }
      
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
    if (!loaded && param_index != SLOT_ADVANCE)
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
      {
        if (pDev[INDEX_TAPECTL].inqdone == 1)
          {
            if (Tape_Ready(INDEX_TAPECTL, need_sleep) == -1)
              {
                printf("tape not ready\n");
                endstatus = 2;
                break;
              }
          } else {
            if (Tape_Ready(INDEX_TAPECTL, need_sleep) == -1)
              {
                printf("tape not ready\n");
                endstatus = 2;
                break;
              }          
        }
      }

    printf("%d %s\n", target-slot_offset, tape_device);
    break;

  case COM_INFO:
    loaded = get_current_slot(changer_file);

    if (loaded < 0)
      {
        loaded = find_empty(fd, slot_offset, use_slots);
      }
    loaded = loaded - slot_offset;
      
    printf("%d %d 1", loaded, use_slots);

    if (BarCode(fd) == 1 || emubarcode == 1)
      {
        printf(" 1\n");
      } else {
        printf(" 0\n");
      }
    break;

  case COM_RESET:
    target=get_current_slot(changer_file);

    if (target < 0)
    {
	    	dbprintf(("COM_RESET: get_current_slot %d\n", target));
		target=find_empty(fd, slot_offset, use_slots);
		dbprintf(("COM_RESET: find_empty %d\n", target));
    }

    if (loaded) {
      if (pDev[INDEX_TAPECTL].avail == 1 && pDev[INDEX_TAPE].avail == 1)
        {
          LogSense(INDEX_TAPE);
        }

      if (!isempty(fd, target))
        target=find_empty(fd, slot_offset, use_slots);
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
      {
        if (pDev[INDEX_TAPECTL].inqdone == 1)
          {
            if (Tape_Ready(INDEX_TAPECTL, need_sleep) == -1)
              {
                printf("tape not ready\n");
                endstatus = 2;
                break;
              }
          } else {
            if (Tape_Ready(INDEX_TAPECTL, need_sleep) == -1)
              {
                printf("tape not ready\n");
                endstatus = 2;
                break;
              }          
          }
      }
    
    printf("%d %s\n", slot_offset, tape_device);
    break;

  case COM_EJECT:
    if (loaded) {
      target=get_current_slot(changer_file);
      if (target < 0)
        {
          dbprintf(("COM_EJECT: get_current_slot %d\n", target));
          target = find_empty(fd, slot_offset, use_slots);
          dbprintf(("COM_EJECT: find_empty %d\n", target));
        }
      
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
      if (target < 0)
        {
          dbprintf(("COM_CLEAN: get_current_slot %d\n", target));
          target = find_empty(fd, slot_offset, use_slots);
          dbprintf(("COM_CLEAN: find_empty %d\n",target));
        }

      if (need_eject)
        eject_tape(scsitapedevice, need_eject);
      (void)unload(fd, drive_num, target);
    } 
    clean_tape(fd,tape_device,clean_file,drive_num,clean_slot,
               maxclean,time_file);
    printf("%s cleaned\n", tape_device);
    break;
  };

  /* FIX ME, should be an function to close the device */  
/*   if (pChangerDev != NULL) */
/*     close(pChangerDev->fd); */
 
/*   if (pTapeDev != NULL) */
/*     close(pTapeDev->fd); */

/*   if (pTapeDevCtl != NULL) */
/*     close(pTapeDevCtl->fd); */


#endif
  if (do_inventory == 1 && endstatus == 0 && chg.labelfile != NULL)
    {
      Inventory(chg.labelfile, drive_num , chg.eject, 0, 0, clean_slot);
    }

  dbclose();
  return endstatus;
}
/*
 * Local variables:
 * indent-tabs-mode: nil
 * tab-width: 4
 * End:
 */

