#ifndef lint
static char rcsid[] = "$Id: scsi-changer-driver.c,v 1.19 2001/01/17 17:08:57 ant Exp $";
#endif
/*
 * Interface to control a tape robot/library connected to the SCSI bus
 *
 * Copyright (c) Thomas Hepper th@ant.han.de
 */

#include <amanda.h>
/*
#ifdef HAVE_STDIO_H
*/
#include <stdio.h>
/*
#endif
*/
#ifdef  HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#

#include "scsi-defs.h"

#include "tapeio.h"

extern int TapeEject(int DeviceFD);

extern FILE *debug_file;

int PrintInquiry(SCSIInquiry_T *);
int GenericElementStatus(int DeviceFD, int InitStatus);
int DLT448ElementStatus(int DeviceFD, int InitStatus);
int SDXElementStatus(int DeviceFD, int InitStatus);
ElementInfo_T *LookupElement(int addr);
int GenericResetStatus(int DeviceFD);
int RequestSense(int, ExtendedRequestSense_T *, int  );
void dump_hex(char *, int);
void TerminateString(char *string, int length);
void ChgExit(char *, char *, int);
int BarCode(int fd);
int LogSense(int fd);
int SenseHandler(int fd, unsigned char flag, char *buffer);

int SCSI_AlignElements(int DeviceFD, int MTE, int DTE, int STE);

int DoNothing();
int GenericMove(int, int, int);
int SDXMove(int, int, int);
int CheckMove(ElementInfo_T *from, ElementInfo_T *to);
int GenericRewind(int);
int GenericStatus();
int GenericFree();
void TapeStatus();                   /* Is the tape loaded ? */
int DLT4000Eject(char *Device, int type);
int GenericEject(char *Device, int type);
int GenericClean(char *Device);                 /* Does the tape need a clean */
int GenericBarCode(int DeviceFD);               /* Do we have Barcode reader support */
int NoBarCode(int DeviceFD);

int GenericSearch();
void Inventory(char *labelfile, int drive, int eject, int start, int stop, int clean);

int TreeFrogBarCode(int DeviceFD);
int EXB120BarCode(int DeviceFD);
int GenericSenseHandler(int fd, int flags, char *);

ElementInfo_T *LookupElement(int address);
int eject_tape(char *tapedev, int type);
int unload(int fd, int drive, int slot);
int load(int fd, int drive, int slot);

/*
 * Log Pages Decode
 */
void WriteErrorCountersPage(LogParameter_T *, int);
void ReadErrorCountersPage(LogParameter_T *, int);
void C1553APage30(LogParameter_T *, int);
void C1553APage37(LogParameter_T *, int);
void EXB85058HEPage39(LogParameter_T *, int);
void EXB85058HEPage3c(LogParameter_T *, int);
int Decode(LogParameter_T *, int *); 
int DecodeModeSense(char *buffer, int offset, char *pstring, char block, FILE *out);

int SCSI_Move(int DeviceFD, unsigned char chm, int from, int to);
int SCSI_LoadUnload(int DeviceFD, RequestSense_T *pRequestSense, unsigned char byte1, unsigned char load);
int SCSI_TestUnitReady(int, RequestSense_T *);
int SCSI_ModeSense(int DeviceFD, char *buffer, u_char size, u_char byte1, u_char byte2);
int SCSI_ModeSelect(int DeviceFD, 
                    char *buffer,
                    unsigned char length,
                    unsigned char save,
                    unsigned char mode,
                    unsigned char lun);

int SCSI_ReadElementStatus(int DeviceFD, 
                           unsigned char type,
                           unsigned char lun,
                           unsigned char VolTag,
                           int StartAddress,
                           int NoOfElements,
                           char **data);

FILE *StatFile;

SC_COM_T SCSICommand[] = {
  {0x00,
   6,
   "TEST UNIT READY"},
  {0x01,
   6,
   "REWIND"},
  {0x03,
   6,
   "REQUEST SENSE"},
  {0x07,
   6,
   "INITIALIZE ELEMENT STATUS"},
  {0x12,
   6,
   "INQUIRY"},
  {0x13,
   6,
   "ERASE"},
  {0x15,
   6,
   "MODE SELECT"},
  {0x1A,
   6,
   "MODE SENSE"},
  {0x1B,
   6,
   "UNLOAD"},
  {0x4D,
   10,
   "LOG SENSE"}, 
  {0xA5,
   12,
   "MOVE MEDIUM"},
  { 0xE5,
    12,
   "VENDOR SPECIFIC"},
  {0xB8,
   12,
   "READ ELEMENT STATUS"},
  {0, 0, 0}
};

ChangerCMD_T ChangerIO[] = {
	/* HP Devices */
  {"C1553A",
   "HP Auto Loader [C1553A]",
   GenericMove,
   GenericElementStatus,
   DoNothing,
   GenericFree,
   GenericEject,
   GenericClean,
   GenericRewind,
   GenericBarCode,
   GenericSearch,
   GenericSenseHandler}, 
	/* Exabyte Devices */
  {"EXB-10e",      
   "Exabyte Robot [EXB-10e]",
   GenericMove,
   GenericElementStatus,
   GenericResetStatus,
   GenericFree,
   GenericEject,
   GenericClean,
   GenericRewind,
   GenericBarCode,
   GenericSearch,
   GenericSenseHandler},
  {"EXB-120",   
   "Exabyte Robot [EXB-120]",
   GenericMove,
   GenericElementStatus,
   GenericResetStatus,
   GenericFree,
   GenericEject,
   GenericClean,
   GenericRewind,
   EXB120BarCode,
   GenericSearch,
   GenericSenseHandler},
  {"EXB-85058HE-0000",        
   "Exabyte Tape [EXB-85058HE-0000]",
   DoNothing,
   DoNothing,
   DoNothing,
   DoNothing,
   GenericEject,
   GenericClean,
   GenericRewind,
   GenericBarCode,
   GenericSearch,
   GenericSenseHandler},
    /* Tandberg Devices */
  {"TDS 1420",              
   "Tandberg Robot (TDS 1420)",
   GenericMove,
   GenericElementStatus,
   GenericResetStatus,
   GenericFree,
   GenericEject,
   GenericClean,
   GenericRewind,
   GenericBarCode,
   GenericSearch,
   GenericSenseHandler},
  /* And now the tape devices */
  {"DLT7000",        
   "DLT Tape [DLT7000]",
   DoNothing,
   DoNothing,
   DoNothing,
   DoNothing,
   DLT4000Eject,
   GenericClean,
   GenericRewind,
   GenericBarCode,
   GenericSearch,
   GenericSenseHandler},
  {"DLT4000",        
   "DLT Tape [DLT4000]",
   DoNothing,
   DoNothing,
   DoNothing,
   DoNothing,
   DLT4000Eject,
   GenericClean,
   GenericRewind,
   NoBarCode,
   GenericSearch,
   GenericSenseHandler},
    /* ADIC Devices */
  {"VLS DLT",               
   "ADIC VLS DLT Library [VLS DLT]",
   GenericMove,
   GenericElementStatus,
   GenericResetStatus,
   GenericFree,
   GenericEject,
   GenericClean,
   GenericRewind,
   NoBarCode,
   GenericSearch,
   GenericSenseHandler},
  {"VLS SDX",               
   "ADIC VLS DLT Library [VLS SDX]",
   SDXMove,
   SDXElementStatus,
   GenericResetStatus,
   GenericFree,
   GenericEject,
   GenericClean,
   GenericRewind,
   NoBarCode,
   GenericSearch,
   GenericSenseHandler},
  {"Scalar DLT 448",
   "ADIC DLT 448 [Scalar DLT 448]",
   GenericMove,
   DLT448ElementStatus,
   GenericResetStatus,
   GenericFree,
   GenericEject,
   GenericClean,
   GenericRewind,
   NoBarCode,
   GenericSearch,
   GenericSenseHandler},
   /* Sepctra Logic Devices */
  {"215",
   "Spectra Logic TreeFrog[215]",
   GenericMove,
   GenericElementStatus,
   GenericResetStatus,
   GenericFree,
   GenericEject,
   GenericClean,
   GenericRewind,
   TreeFrogBarCode,
   GenericSearch,
   GenericSenseHandler},
    /* The generic handler if nothing matches */
  {"generic",
   "Generic driver tape/robot [generic]",
   GenericMove,
   GenericElementStatus,
   GenericResetStatus,
   GenericFree,
   GenericEject,
   GenericClean,
   GenericRewind,
   NoBarCode,
   GenericSearch,
   GenericSenseHandler},
  {NULL, NULL, NULL,NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}
};           


LogPageDecode_T DecodePages[] = {
  {2,
   "C1553A",
   WriteErrorCountersPage},
  {3,
   "C1553A",
   ReadErrorCountersPage},
  {0x30,
   "C1553A",
   C1553APage30},
  {0x37,
   "C1553A",
   C1553APage37},
  {2,
   "*",
   WriteErrorCountersPage},
  {3,
   "*",
   ReadErrorCountersPage},
  {0x39,
   "EXB-85058HE-0000",
   EXB85058HEPage39},
  {0x3c,
   "EXB-85058HE-0000",
   EXB85058HEPage3c},
  {0, NULL, NULL}
};


int InErrorHandler = 0;
int ElementStatusValid = 0;
char *SlotArgs = 0;
/* Pointer to MODE SENSE Pages */
char *pModePage = NULL;
EAAPage_T *pEAAPage = NULL;
DeviceCapabilitiesPage_T *pDeviceCapabilitiesPage = NULL;
char *pVendorUnique = NULL;
/*
  New way, every element type has its on array
*/
ElementInfo_T *pMTE = NULL; /*Medium Transport Element */
ElementInfo_T *pSTE = NULL; /*Storage Element */
ElementInfo_T *pIEE = NULL; /*Import Export Element */
ElementInfo_T *pDTE = NULL; /*Data Transfer Element */
int MTE = 0; /*Counter for the above element types */
int STE = 0;
int IEE = 0;
int DTE = 0;
/*
 * First all functions which are called from extern
 */


/*
 * Print the scsi-changer-driver version
 */

void ChangerDriverVersion()
{
  dbprintf(("scsi-changer-driver: %s\n",rcsid));
}

/*
 * Try to create a list of tapes and labels which are in the current
 * magazin. The drive must be empty !!
 * 
 * labelfile -> file name of the db
 * drive -> which drive should we use
 * eject -> the tape device needs an eject before move
 * start -> start at slot start
 * stop  -> stop at slot stop
 * clean -> if we have an cleaning tape than this is the slot number of it
 *
 * return 
 * 0  -> fail
 * 1  -> successfull
 *
 * ToDo:
 * Check if the tape/changer is ready for the next move
 */
void Inventory(char *labelfile, int drive, int eject, int start, int stop, int clean)
{
  extern OpenFiles_T *pDev;
  int x;
  char *datestamp = malloc(1);   /* stupid, but tapefd_rdlabel does an free at the begining ... */
  char *label = malloc(1);       /* the same here ..... */
  char *result;

  MapBarCode(labelfile,  "", "", RESET_VALID, 0, 0);
  for (x = 0; x < STE; x++)
    {
      if (x == clean)
	{
	  continue;
	}
      if (load(INDEX_CHANGER, drive, x ) != 0)
	{
	}
      if (pDev[INDEX_TAPE].devopen == 0)
	{
	  SCSI_OpenDevice(INDEX_TAPE);
	}
      if ((result = (char *)tapefd_rdlabel(pDev[INDEX_TAPE].fd, &datestamp, &label)) == NULL)
	{
	  MapBarCode(labelfile, label, "", UPDATE_SLOT, x , 0);
	}
      SCSI_CloseDevice(INDEX_TAPE);
      if (eject)
	eject_tape("", eject);
      (void)unload(INDEX_CHANGER, drive, x);
    }
}

/*
 * Check if the slot ist empty
 * slot -> slot number to check
 */
int isempty(int fd, int slot)
{
  extern OpenFiles_T *pDev;
  dbprintf(("##### START isempty\n"));

  if (ElementStatusValid == 0)
    {
      if ( pDev[fd].functions->function_status(fd, 1) != 0)
        {
          dbprintf(("##### STOP isempty [-1]\n"));
          return(-1);
        }
    }

  if (pSTE[slot].status == 'E')
    {
      dbprintf(("##### STOP isempty [1]\n"));
      return(1);
    }
  dbprintf(("##### STOP isempty [0]\n"));
  return(0);
}

int get_clean_state(char *tapedev)
{
  extern OpenFiles_T *pDev;
  /* Return 1 if cleaning is needed */
  int ret;
  
  dbprintf(("##### START get_clean_state\n"));

  if (pDev[INDEX_TAPECTL].SCSI == 0)
    {
      dbprintf(("##### STOP get_clean_state [-1]\n"));
      return(-1);
    }
  ret=pDev[INDEX_TAPECTL].functions->function_clean(tapedev);
  dbprintf(("##### STOP get_clean_state [%d]\n", ret));
  return(ret);    
}

/*
 * eject the tape
 * The parameter tapedev is not used.
 * Type describes if we should force the SCSI eject if available
 * normal eject is done with the ioctl
 */
int eject_tape(char *tapedev, int type)
  /* This function ejects the tape from the drive */
{
  extern OpenFiles_T *pDev;
  int ret = 0;

  dbprintf(("##### START eject_tape\n"));

  if (pDev[INDEX_TAPECTL].SCSI == 1 && pDev[INDEX_TAPECTL].avail == 1 && type == 1)
    {
      ret=pDev[INDEX_TAPECTL].functions->function_eject(tapedev, type);
      dbprintf(("##### STOP (SCSI)eject_tape [%d]\n", ret));
      return(ret);
    }
  
  if (pDev[INDEX_TAPE].avail == 1)
    {
      ret=Tape_Eject(pDev[INDEX_TAPE].fd);
      dbprintf(("##### STOP (ioctl)eject_tape [%d]\n", ret));
      return(ret);
    }

  dbprintf(("##### STOP eject_tape [-1]\n"));
  return(-1);
}


/* Find an empty slot, starting at start, ending at start+count */
int find_empty(int fd, int start, int count)
{
  extern OpenFiles_T *pDev;
  int x;
  int end;

  dbprintf(("###### START find_empty\n"));

  if (ElementStatusValid == 0)
    {
      if ( pDev[fd].functions->function_status(fd , 1) != 0)
        {
          dbprintf(("###### END find_empty [%d]\n", -1));
          return(-1);
        }
    }
  
  if (count == 0)
    {
      end = STE;
    } else {
      end = start + count;
    }
  
  if (end > STE)
    {
      end = STE;
    }
  
  dbprintf(("start at %d, end at %d\n", start, end));

  for (x = start; x < end; x++)
    {
      if (pSTE[x].status == 'E')
        {
          dbprintf(("###### END find_empty [%d]\n", x));     
          return(x);
        }
    }
  dbprintf(("###### END find_empty [%d]\n", -1));
  return(-1);
}

/*
 * See if the tape is loaded based on the information we
 * got back from the ReadElementStatus
 * return values
 * -1 -> Error (Fatal)
 * 0  -> drive is empty
 * 1  -> drive is loaded
 */
int drive_loaded(int fd, int drivenum)
{
  extern OpenFiles_T *pDev;

  dbprintf(("###### START drive_loaded\n"));
  dbprintf(("%-20s : fd %d drivenum %d \n", "drive_loaded", fd, drivenum));
  
  
  if (ElementStatusValid == 0)
    {
      if (pDev[INDEX_CHANGER].functions->function_status(INDEX_CHANGER, 1) != 0)
	{
	  return(-1);
	}
    }
  
  if (pDTE[drivenum].status == 'E')
    return(0);
  
  return(1);
}


/*
 * unload the specified drive into the specified slot
 * (storage element)
 */
int unload(int fd, int drive, int slot) 
{
  extern OpenFiles_T *pDev;

  dbprintf(("###### START unload\n"));
  dbprintf(("%-20s : fd %d, slot %d, drive %d \n", "unload", fd, slot, drive));


  if (ElementStatusValid == 0)
      {
          if (pDev[INDEX_CHANGER].functions->function_status(INDEX_CHANGER , 1) != 0)
              {
                  return(-1);
              }
      }

  dbprintf(("%-20s : unload drive %d[%d] slot %d[%d]\n", "unload",
            drive,
            pDTE[drive].address,
            slot,
            pSTE[slot].address));
  
  
  if (pDTE[drive].status == 'E')
    {
      dbprintf(("unload : Drive %d address %d is empty\n", drive, pDTE[drive].address));
      return(-1);
    }
  
  if (pSTE[slot].status == 'F')
    {
      dbprintf(("unload : Slot %d address %d is full\n", drive, pSTE[slot].address));
      slot = find_empty(fd, 0, 0);
      dbprintf(("unload : try to unload to slot %d\n", slot));
    }
  
  pDev[INDEX_CHANGER].functions->function_move(INDEX_CHANGER , pDTE[drive].address, pSTE[slot].address);
  /*
   * Update the Status
   */
  if (pDev[INDEX_CHANGER].functions->function_status(INDEX_CHANGER , 1) != 0)
      {
          return(-1);
      }

  return(0);
}


/*
 * load the media from the specified element (slot) into the
 * specified data transfer unit (drive)
 * fd     -> pointer to the internal devie structure pDev
 * driver -> which drive in the library
 * slot   -> the slot number from where to load
 *
 * return -> 0 = success
 *           !0 = failure
 */
int load(int fd, int drive, int slot)
{
  int ret;
  extern OpenFiles_T *pDev;

  dbprintf(("###### START load\n"));
  dbprintf(("%-20s : fd %d, drive %d, slot %d \n", "load", fd, drive, slot));

  if (ElementStatusValid == 0)
      {
          if (pDev[fd].functions->function_status(fd, 1) != 0)
              {
                  return(-1);
              }
      }

  dbprintf(("load : load drive %d[%d] slot %d[%d]\n",drive,
          pDTE[drive].address,
          slot,
          pSTE[slot].address));

  if (pDTE[drive].status == 'F')
    {
      dbprintf(("load : Drive %d address %d is full\n", drive, pDTE[drive].address));
      return(-1);
    }

  if (pSTE[slot].status == 'E')
    {
      dbprintf(("load : Slot %d address %d is empty\n", drive, pSTE[slot].address));
      return(-1);
    }

  ret = pDev[fd].functions->function_move(fd, pSTE[slot].address, pDTE[drive].address);
  
  /*
   * Update the Status
   */
  if (pDev[fd].functions->function_status(fd, 1) != 0)
      {
          return(-1);
      }
  
  return(ret);
}

/*
 * Returns the number of Storage Slots which the library has
 * fd -> pointer to the internal devie structure pDev
 * return -> Number of slots
 */ 
int get_slot_count(int fd)
{
  extern OpenFiles_T *pDev;

  dbprintf(("###### START get_slot_count\n"));
  dbprintf(("%-20s : fd %d\n", "get_slot_count", fd));

  if (ElementStatusValid == 0)
    {
      pDev[fd].functions->function_status(fd, 1);
    }

  return(STE);
  /*
   * return the number of slots in the robot
   * to the caller
   */
  
}


/*
 * retreive the number of data-transfer devices /Tape drives)
 * fd     -> pointer to the internal devie structure pDev
 * return -> -1 on failure
 */ 
int get_drive_count(int fd)
{

  extern OpenFiles_T *pDev;

  dbprintf(("###### START get_drive_count\n"));
  dbprintf(("%-20s : fd %d\n", "get_drive_count", fd));


  if (ElementStatusValid == 0)
      {
          if ( pDev[fd].functions->function_status(fd, 1) != 0)
              {
                  return(-1);
              }
      }
  
  return(DTE);
}

/*
 * Now the internal functions
 */

/*
 * Open the device and placeit in the list of open files
 * The OS has to decide if it is an SCSI Commands capable device
 */

int OpenDevice(int ip , char *DeviceName, char *ConfigName, char *ident)
{
  extern OpenFiles_T *pDev;
  ChangerCMD_T *p = (ChangerCMD_T *)&ChangerIO;
  
  dbprintf(("##### START OpenDevice\n"));
  dbprintf(("OpenDevice : %s\n", DeviceName));
  
  pDev[ip].ConfigName = strdup(ConfigName);
  pDev[ip].dev = strdup(DeviceName);

  if (SCSI_OpenDevice(ip) != 0 )
    {
      
   
      if (ident != NULL)   /* Override by config */
      {
        while(p->ident != NULL)
          {
            if (strcmp(ident, p->ident) == 0)
              {
                pDev[ip].functions = p;
		strncpy(pDev[ip].ident, ident, 17);
                dbprintf(("override using ident = %s, type = %s\n",p->ident, p->type));
                return(1);
              }
            p++;
          }
	  ChgExit("OpenDevice", "ident not found", FATAL);
      } else {
        while(p->ident != NULL)
          {
            if (strcmp(pDev[ip].ident, p->ident) == 0)
              {
                pDev[ip].functions = p;
                dbprintf(("using ident = %s, type = %s\n",p->ident, p->type));
                return(1);
              }
            p++;
          }
      }
      /* Nothing matching found, try generic */
      p = (ChangerCMD_T *)&ChangerIO;
      while(p->ident != NULL)
        {
          if (strcmp("generic", p->ident) == 0)
            {
              pDev[ip].functions = p;
              dbprintf(("using ident = %s, type = %s\n",p->ident, p->type));
              return(1);
            }
          p++;
        }  
    }
  return(0); 
}


/*
 * This functions checks if the library has an barcode reader.
 * fd     -> pointer to the internal devie structure pDev
 */
int BarCode(int fd)
{
  int ret;
  extern OpenFiles_T *pDev;

  dbprintf(("##### START BarCode\n"));
  dbprintf(("%-20s : fd %d\n", "BarCode", fd));

  dbprintf(("Ident = [%s], function = [%s]\n", pDev[fd].ident,
        pDev[fd].functions->ident));
  ret = pDev[fd].functions->function_barcode(fd);
  return(ret);
}


/*
 * This functions check if the tape drive is ready
 *
 * fd     -> pointer to the internal devie structure pDev
 * wait -> time to wait for the ready status
 *
 */
int Tape_Ready(int fd, int wait_time)
{
  extern OpenFiles_T *pDev;
  int true = 1;
  int ret;
  int cnt = 0;

  RequestSense_T *pRequestSense;
  dbprintf(("##### START Tape_Ready\n"));
 
  
  if (pDev[fd].SCSI == 0)
      {
          dbprintf(("Tape_Ready : can#t send SCSI commands\n"));
          sleep(wait_time);
          return(0);
      }

  if ((pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T))) == NULL)
      {
        dbprintf(("Tape_Ready : malloc failed\n"));
        return(-1);
      }


  GenericRewind(fd);
 

  while (true && cnt < wait_time)
    {
      ret = SCSI_TestUnitReady(fd, pRequestSense );
      switch (SenseHandler(fd, 0, (char *)pRequestSense))
        {
	  case SENSE_NO:
		  true=0;
		  break;
          case SENSE_NO_TAPE:
            break;
          case SENSE_IGNORE:
            break;
          case SENSE_ABORT:
            return(-1);
            break;
          case SENSE_RETRY:
            break;
          default:
            break;
        }
      sleep(1);
      cnt++;
    }

  free(pRequestSense);

  dbprintf(("Tape_Ready after %d sec\n", cnt));
  return(0);
}


int DecodeSCSI(CDB_T CDB, char *string)
{
  SC_COM_T *pSCSICommand;
  int x;

  dbprintf(("##### START DecodeSCSI\n"));
  pSCSICommand = (SC_COM_T *)&SCSICommand;

  while (pSCSICommand->name != NULL)
    {
      if (CDB[0] == pSCSICommand->command)
        {
          dbprintf(("%s %s", string, pSCSICommand->name));
          for (x=0; x < pSCSICommand->length; x++)
            {
              dbprintf((" %02X", CDB[x]));
            }
          dbprintf(("\n"));
          dbprintf(("##### STOP DecodeSCSI\n"));
          return(0);
	}
      pSCSICommand++;
    }
  dbprintf(("Not found %X\n", CDB[0]));
  dbprintf(("##### STOP DecodeSCSI\n"));
  return(0);
}

int DecodeModeSense(char *buffer, int offset, char *pstring, char block, FILE *out)
{
  ReadWriteErrorRecoveryPage_T *prp;
  DisconnectReconnectPage_T *pdrp;
  int length = (unsigned char)*buffer - 4 - offset;

  dbprintf(("##### START DecodeModeSense\n"));

  dump_hex(buffer, 255);

  /* Jump over the Parameter List header  and an offset if we have something
   * Unknown at the start (ADIC-218) at the moment
   * */
  buffer = buffer + 4 + offset;

  dbprintf(("buffer length = %d\n", length));

  if (block) /* Do we have an block descriptor page ?*/
    {
      if (out != NULL)
     	 fprintf(out, "DecodeModeSense : Density Code %x\n", buffer[0]);
      buffer++;

      if (out != NULL)
      	fprintf(out, "DecodeModeSense : Number of Blocks %d\n", V3(buffer));
      buffer = buffer + 4;

      if (out != NULL)
      	fprintf(out, "DecodeModeSense : Block Length %d\n", V3(buffer));
      buffer = buffer + 3;
    }
  
  while (length > 0)
    {
      switch (*buffer & 0x3f)
        {
        case 0:
            pVendorUnique = buffer;
            buffer++;
            break;
        case 0x1:
          prp = (ReadWriteErrorRecoveryPage_T *)buffer;
	  if (out != NULL)
          { 
          	fprintf(out, "DecodeModeSense : Read/Write Error Recovery Page\n");
          	fprintf(out,"\tTransfer Block            %d\n", prp->tb);
          	fprintf(out,"\tEnable Early Recovery     %d\n", prp->eer);
          	fprintf(out,"\tPost Error                %d\n", prp->per);
          	fprintf(out,"\tDisable Transfer on Error %d\n", prp->dte);
          	fprintf(out,"\tDisable ECC Correction    %d\n", prp->dcr);
          	fprintf(out,"\tRead Retry Count          %d\n", prp->ReadRetryCount);
          	fprintf(out,"\tWrite Retry Count         %d\n", prp->WriteRetryCount);
	  }
          buffer++;
          break;
        case 0x2:
          pdrp = (DisconnectReconnectPage_T *)buffer;
	  if (out != NULL)
          {
          	fprintf(out, "DecodeModeSense : Disconnect/Reconnect Page\n");
          	fprintf(out,"\tBuffer Full Ratio     %d\n", pdrp->BufferFullRatio);
          	fprintf(out,"\tBuffer Empty Ratio    %d\n", pdrp->BufferEmptyRatio);
          	fprintf(out,"\tBus Inactivity Limit  %d\n", 
                  V2(pdrp->BusInactivityLimit));
          	fprintf(out,"\tDisconnect Time Limit %d\n", 
                  V2(pdrp->DisconnectTimeLimit));
          	fprintf(out,"\tConnect Time Limit    %d\n",
                  V2(pdrp->ConnectTimeLimit));
          	fprintf(out,"\tMaximum Burst Size    %d\n",
                  V2(pdrp->MaximumBurstSize));
          	fprintf(out,"\tDTDC                  %d\n", pdrp->DTDC);
	  }
          buffer++;
          break;
        case 0x1d:
          pEAAPage = (EAAPage_T *)buffer;
	  if (out != NULL)
	  {
          	fprintf(out,"DecodeModeSense : Element Address Assignment Page\n");
          	fprintf(out,"\tMedium Transport Element Address     %d\n", 
                    V2(pEAAPage->MediumTransportElementAddress));
          	fprintf(out,"\tNumber of Medium Transport Elements  %d\n", 
                    V2(pEAAPage->NoMediumTransportElements));
          	fprintf(out, "\tFirst Storage Element Address       %d\n", 
                    V2(pEAAPage->FirstStorageElementAddress));
          	fprintf(out, "\tNumber of  Storage Elements         %d\n", 
                    V2(pEAAPage->NoStorageElements));
          	fprintf(out, "\tFirst Import/Export Element Address %d\n", 
                    V2(pEAAPage->FirstImportExportElementAddress));
          	fprintf(out, "\tNumber of  ImportExport Elements    %d\n", 
                    V2(pEAAPage->NoImportExportElements));
          	fprintf(out, "\tFirst Data Transfer Element Address %d\n", 
                    V2(pEAAPage->FirstDataTransferElementAddress));
          	fprintf(out, "\tNumber of  Data Transfer Elements   %d\n", 
                    V2(pEAAPage->NoDataTransferElements));
	  }
          buffer++;
          break;
        case 0x1f:
          pDeviceCapabilitiesPage = (DeviceCapabilitiesPage_T *)buffer;
	  if (out != NULL)
	  {
          	fprintf(out, "DecodeModeSense : MT can store data cartridges %d\n",
                    pDeviceCapabilitiesPage->MT);
          	fprintf(out, "DecodeModeSense : ST can store data cartridges %d\n",
                    pDeviceCapabilitiesPage->ST);
          	fprintf(out, "DecodeModeSense : IE can store data cartridges %d\n",
                    pDeviceCapabilitiesPage->IE);
          	fprintf(out, "DecodeModeSense : DT can store data cartridges %d\n",
                    pDeviceCapabilitiesPage->DT);
          	fprintf(out, "DecodeModeSense : MT to MT %d\n",
                    pDeviceCapabilitiesPage->MT2MT);
          	fprintf(out, "DecodeModeSense : MT to ST %d\n",
                    pDeviceCapabilitiesPage->MT2ST);
          	fprintf(out, "DecodeModeSense : MT to IE %d\n",
                    pDeviceCapabilitiesPage->MT2IE);
          	fprintf(out, "DecodeModeSense : MT to DT %d\n",
                    pDeviceCapabilitiesPage->MT2DT);
          	fprintf(out, "DecodeModeSense : ST to MT %d\n",
                    pDeviceCapabilitiesPage->ST2ST);
          	fprintf(out, "DecodeModeSense : ST to MT %d\n",
                    pDeviceCapabilitiesPage->ST2ST);
          	fprintf(out, "DecodeModeSense : ST to DT %d\n",
                    pDeviceCapabilitiesPage->ST2DT);
          	fprintf(out, "DecodeModeSense : IE to MT %d\n",
                    pDeviceCapabilitiesPage->IE2MT);
          	fprintf(out, "DecodeModeSense : IE to ST %d\n",
                    pDeviceCapabilitiesPage->IE2IE);
          	fprintf(out, "DecodeModeSense : IE to ST %d\n",
                    pDeviceCapabilitiesPage->IE2DT);
          	fprintf(out, "DecodeModeSense : IE to ST %d\n",
                    pDeviceCapabilitiesPage->IE2DT);
          	fprintf(out, "DecodeModeSense : DT to MT %d\n",
                    pDeviceCapabilitiesPage->DT2MT);
          	fprintf(out, "DecodeModeSense : DT to ST %d\n",
                    pDeviceCapabilitiesPage->DT2ST);
          	fprintf(out, "DecodeModeSense : DT to IE %d\n",
                    pDeviceCapabilitiesPage->DT2IE);
          	fprintf(out, "DecodeModeSense : DT to DT %d\n",
                    pDeviceCapabilitiesPage->DT2DT);
	  }
          buffer++;
          break;
        default:
          buffer++;  /* set pointer to the length information */
          break;
        }
      /* Error if *buffer (length) is 0 */
      if (*buffer == 0)
        {
          /*           EAAPage = NULL; */
          /*           DeviceCapabilitiesPage = NULL; */
          return(-1);
        }
      length = length - *buffer - 2;
      buffer = buffer + *buffer + 1;      
    }
  return(0);
}

int DecodeSense(RequestSense_T *sense, char *pstring, FILE *out)
{
  if (out == NULL)
    {
      return(0);
    }
  fprintf(out,"##### START DecodeSense\n");
  fprintf(out,"%sSense Keys\n", pstring);
  fprintf(out,"\tErrorCode                     %02x\n", sense->ErrorCode);
  fprintf(out,"\tValid                         %d\n", sense->Valid);
  fprintf(out,"\tASC                           %02X\n", sense->AdditionalSenseCode);
  fprintf(out,"\tASCQ                          %02X\n", sense->AdditionalSenseCodeQualifier);
  fprintf(out,"\tSense key                     %02X\n", sense->SenseKey);
  switch (sense->SenseKey)
    {
    case 0:
      fprintf(out,"\t\tNo Sense\n");
      break;
    case 1:
      fprintf(out,"\t\tRecoverd Error\n");
      break;
    case 2:
      fprintf(out,"\t\tNot Ready\n");
      break;
    case 3:
      fprintf(out,"\t\tMedium Error\n");
      break;
    case 4:
      fprintf(out,"\t\tHardware Error\n");
      break;
    case 5:
      fprintf(out,"\t\tIllegal Request\n");
      break;
    case 6:
      fprintf(out,"\t\tUnit Attention\n");
      break;
    case 7:
      fprintf(out,"\t\tData Protect\n");
      break;
    case 8:
      fprintf(out,"\t\tBlank Check\n");
      break;
    case 9:
      fprintf(out,"\t\tVendor uniq\n");
      break;
    case 0xa:
      fprintf(out,"\t\tCopy Aborted\n");
      break;
    case 0xb:
      fprintf(out,"\t\tAborted Command\n");
      break;
    case 0xc:
      fprintf(out,"\t\tEqual\n");
      break;
    case 0xd:
      fprintf(out,"\t\tVolume Overflow\n");
      break;
    case 0xe:
      fprintf(out,"\t\tMiscompare\n");
      break;
    case 0xf:
      fprintf(out,"\t\tReserved\n");
      break;
    }
  return(0);      
}

int DecodeExtSense(ExtendedRequestSense_T *sense, char *pstring, FILE *out)
{
  ExtendedRequestSense_T *p;

  fprintf(out,"##### START DecodeExtSense\n");
  p = sense;

  fprintf(out,"%sExtended Sense\n", pstring);
  DecodeSense((RequestSense_T *)p, pstring, out);
  fprintf(out,"\tLog Parameter Page Code         %02X\n", sense->LogParameterPageCode);
  fprintf(out,"\tLog Parameter Code              %02X\n", sense->LogParameterCode);
  fprintf(out,"\tUnderrun/Overrun Counter        %02X\n", sense->UnderrunOverrunCounter);
  fprintf(out,"\tRead/Write Error Counter        %d\n", V3((char *)sense->ReadWriteDataErrorCounter)); 
  if (sense->PF)
    fprintf(out,"\tPower Fail\n");
  if (sense->BPE)
    fprintf(out,"\tSCSI Bus Parity Error\n");
  if (sense->FPE)
    fprintf(out,"\tFormatted Buffer parity Error\n");
  if (sense->ME)
    fprintf(out,"\tMedia Error\n");
  if (sense->ECO)
    fprintf(out,"\tError Counter Overflow\n");
  if (sense->TME)
    fprintf(out,"\tTapeMotion Error\n");
  if (sense->TNP)
    fprintf(out,"\tTape Not Present\n");
  if (sense->LBOT)
    fprintf(out,"\tLogical Beginning of tape\n");
  if (sense->TMD)
    fprintf(out,"\tTape Mark Detect Error\n");
  if (sense->WP)
    fprintf(out,"\tWrite Protect\n");
  if (sense->FMKE)
    fprintf(out,"\tFilemark Error\n");
  if (sense->URE)
    fprintf(out,"\tUnder Run Error\n");
  if (sense->WEI)
    fprintf(out,"\tWrite Error 1\n");
  if (sense->SSE)
    fprintf(out,"\tServo System Error\n");
  if (sense->FE)
    fprintf(out,"\tFormatter Error\n");
  if (sense->UCLN)
    fprintf(out,"\tCleaning Cartridge is empty\n");
  if (sense->RRR)
    fprintf(out,"\tReverse Retries Required\n");
  if (sense->CLND)
    fprintf(out,"\tTape Drive has been cleaned\n");
  if (sense->CLN)
    fprintf(out,"\tTape Drive needs to be cleaned\n");
  if (sense->PEOT)
    fprintf(out,"\tPhysical End of Tape\n");
  if (sense->WSEB)
    fprintf(out,"\tWrite Splice Error\n");
  if (sense->WSEO)
    fprintf(out,"\tWrite Splice Error\n");
  fprintf(out,"\tRemaing 1024 byte tape blocks   %d\n", V3((char *)sense->RemainingTape));
  fprintf(out,"\tTracking Retry Counter          %02X\n", sense->TrackingRetryCounter);
  fprintf(out,"\tRead/Write Retry Counter        %02X\n", sense->ReadWriteRetryCounter);
  fprintf(out,"\tFault Sympton Code              %02X\n", sense->FaultSymptomCode);
  return(0);
}

int PrintInquiry(SCSIInquiry_T *SCSIInquiry)
{
  dbprintf(("##### START PrintInquiry\n"));
  dbprintf(("%-15s %x\n", "qualifier", SCSIInquiry->qualifier));
  dbprintf(("%-15s %x\n", "type", SCSIInquiry->type));
  dbprintf(("%-15s %x\n", "data_format", SCSIInquiry->data_format));
  dbprintf(("%-15s %X\n", "ansi_version", SCSIInquiry->ansi_version));
  dbprintf(("%-15s %X\n", "ecma_version", SCSIInquiry->ecma_version));
  dbprintf(("%-15s %X\n", "iso_version", SCSIInquiry->iso_version));
  dbprintf(("%-15s %X\n", "type_modifier", SCSIInquiry->type_modifier));
  dbprintf(("%-15s %x\n", "removable", SCSIInquiry->removable));
  dbprintf(("%-15s %.8s\n", "vendor_info", SCSIInquiry->vendor_info));
  dbprintf(("%-15s %.16s\n", "prod_ident", SCSIInquiry->prod_ident));
  dbprintf(("%-15s %.4s\n", "prod_version", SCSIInquiry->prod_version));
  dbprintf(("%-15s %.19s\n", "vendor_specific", SCSIInquiry->vendor_specific));
  return(0);
}


int DoNothing()
{
  dbprintf(("##### START DoNothing\n"));
  return(0);
}

int GenericFree()
{
  dbprintf(("##### START GenericFree\n"));
  return(0);
}

int GenericSearch()
{
  dbprintf(("##### START GenericSearch\n"));
  return(0);
}

int TreeFrogBarCode(int DeviceFD)
{
  extern OpenFiles_T *pDev;

  ModePageTreeFrogVendorUnique_T *pVendor;

  dbprintf(("##### START TreeFrogBarCode\n"));
  if (pModePage == NULL)
    {
      if ((pModePage = malloc(0xff)) == NULL)
        {
          dbprintf(("TreeFrogBarCode : malloc failed\n"));
          return(-1);
        }
    }
  
  if (SCSI_ModeSense(DeviceFD, pModePage, 0xff, 0x0, 0x3f) == 0)
    {
      DecodeModeSense(pModePage, 0, "TreeFrogBarCode :", 0, debug_file);
      
      if (pVendorUnique == NULL)
      {
         dbprintf(("TreeFrogBarCode : no pVendorUnique\n"));
         return(0);
      }
      pVendor = ( ModePageTreeFrogVendorUnique_T *)pVendorUnique;
    
      dbprintf(("TreeFrogBarCode : EBARCO %d\n", pVendor->EBARCO));
      dbprintf(("TreeFrogCheckSum : CHKSUM  %d\n", pVendor->CHKSUM));

      dump_hex((char *)pDev[INDEX_CHANGER].inquiry, INQUIRY_SIZE);
      return(pVendor->EBARCO);
    }
  return(0);
}

int EXB120BarCode(int DeviceFD)
{
  extern OpenFiles_T *pDev;

  ModePageEXB120VendorUnique_T *pVendor;
  ModePageEXB120VendorUnique_T *pVendorWork;

  dbprintf(("##### START EXB120BarCode\n"));
  if (pModePage == NULL)
    {
      if ((pModePage = malloc(0xff)) == NULL)
        {
          dbprintf(("EXB120BarCode : malloc failed\n"));
          return(-1);
        }
    }
  
  if (SCSI_ModeSense(DeviceFD, pModePage, 0xff, 0x8, 0x3f) == 0)
    {
      DecodeModeSense(pModePage, 0, "EXB120BarCode :", 0, debug_file);
      
      if (pVendorUnique == NULL)
      {
         dbprintf(("EXB120BarCode : no pVendorUnique\n"));
         return(0);
      }
      pVendor = ( ModePageEXB120VendorUnique_T *)pVendorUnique;
    
      dbprintf(("EXB120BarCode : NBL %d\n", pVendor->NBL));
      dbprintf(("EXB120BarCode : PS  %d\n", pVendor->PS));
      if (pVendor->NBL == 1 && pVendor->PS == 1 )
        {
          if ((pVendorWork = ( ModePageEXB120VendorUnique_T *)malloc(pVendor->ParameterListLength + 2)) == NULL)
            {
              dbprintf(("EXB120BarCode : malloc failed\n"));
              return(-1);
            }
          dbprintf(("EXB120BarCode : setting NBL to 1\n"));
          memcpy(pVendorWork, pVendor, pVendor->ParameterListLength + 2);
          pVendorWork->NBL = 0;
          pVendorWork->PS = 0;
          pVendorWork->RSVD0 = 0;
          if (SCSI_ModeSelect(DeviceFD, (char *)pVendorWork, pVendorWork->ParameterListLength + 2, 0, 1, 0) == 0)
            {
              dbprintf(("EXB120BarCode : SCSI_ModeSelect OK\n"));
              /* Hack !!!!!!
               */
              pVendor->NBL = 0;
              
              /* And now again !!!
               */
              GenericResetStatus(DeviceFD);
            } else {
              dbprintf(("EXB120BarCode : SCSI_ModeSelect failed\n"));
            }
        }
    }
  
  dump_hex((char *)pDev[INDEX_CHANGER].inquiry, INQUIRY_SIZE);
  dbprintf(("EXB120BarCode : vendor_specific[19] %x\n",
            pDev[INDEX_CHANGER].inquiry->vendor_specific[19]));
  return(1);
}

int NoBarCode(int DeviceFD)
{
  dbprintf(("##### START NoBarCode\n"));
  return(0);
}

int GenericBarCode(int DeviceFD)
{
  extern changer_t chg;
  dbprintf(("##### START GenericBarCode\n"));
/*   dump_hex((char *)pChangerDev->inquiry, INQUIRY_SIZE); */
  if ( chg.havebarcode  >= 1)
    {
      dbprintf(("##### STOP GenericBarCode (havebarcode) => %d\n",chg.havebarcode));
      return(1);
    }
  dbprintf(("##### STOP GenericBarCode => 0\n"));
  return(0);
 }

int SenseHandler(int DeviceFD, unsigned char flag, char *buffer)
{
  extern OpenFiles_T *pDev;
  int ret;
  dbprintf(("##### START SenseHandler\n"));
  dbprintf(("Ident = [%s], function = [%s]\n", pDev[DeviceFD].ident,
	    pDev[DeviceFD].functions->ident));
  ret = pDev[DeviceFD].functions->function_error(DeviceFD, flag, buffer);
  dbprintf(("#### STOP SenseHandler\n"));
  return(ret);
}

/* Try to get information about the tape,    */
/* Tape loaded ? Online etc                  */
/* At the moment quick and dirty with ioctl  */
/* Hack for AIT                              */
/*                                           */
void TapeStatus()
{
  int ret;

  dbprintf(("##### START TapeStatus\n"));
  if (pDTE[0].status == 'F')
  {
    ret = Tape_Status(INDEX_TAPE);
    if ( ret & TAPE_ONLINE)
    {
      pDTE[0].status ='F'; 
      dbprintf(("##### FULL\n"));
    } else {
      pDTE[0].status = 'E';
      dbprintf(("##### EMPTY\n"));
    }
  }
  dbprintf(("##### STOP TapeStatus\n"));
}

int DLT4000Eject(char *Device, int type)
{
  extern OpenFiles_T *pDev;

  RequestSense_T *pRequestSense;
  ExtendedRequestSense_T *pExtendedRequestSense;
  int ret;
  int cnt = 0;
  int true = 1;

  dbprintf(("##### START DLT4000Eject\n"));

  if ((pRequestSense = malloc(sizeof(RequestSense_T))) == NULL)
    {
      dbprintf(("%-20s : malloc failed\n","GenericEject"));
      return(-1);
    }

  if ((pExtendedRequestSense = malloc(sizeof(ExtendedRequestSense_T))) == NULL)
    {
      dbprintf(("%-20s : malloc failed\n","GenericEject"));
      return(-1);
    }
    
  if ( type > 1)
    {
      dbprintf(("GenericEject : use mtio ioctl for eject on %s\n", pDev[INDEX_TAPE].dev));
      return(Tape_Eject(INDEX_TAPE));
    }
  
  
  
  if (pDev[INDEX_TAPECTL].SCSI == 0)
    {
      dbprintf(("DLT4000Eject : Device %s not able to receive SCSI commands\n", pDev[INDEX_TAPE].dev));
      return(Tape_Eject(INDEX_TAPE));
    }
  
  
  dbprintf(("DLT4000Eject : SCSI eject on %s = %s\n", pDev[INDEX_TAPECTL].dev, pDev[INDEX_TAPECTL].ConfigName));
  
  RequestSense(INDEX_TAPECTL, pExtendedRequestSense, 0); 
  DecodeExtSense(pExtendedRequestSense, "DLT4000Eject : ", debug_file);
  /* Unload the tape, 0 ==  wait for success
   * 0 == unload 
   */
  ret = SCSI_LoadUnload(INDEX_TAPECTL, pRequestSense, 0, 0);

  RequestSense(INDEX_TAPECTL, pExtendedRequestSense, 0); 
  DecodeExtSense(pExtendedRequestSense, "DLT4000Eject : ", debug_file);
  
  /* < 0 == fatal */
  if (ret < 0)
    return(-1);
  
  if ( ret > 0)
    {
    }
  
  true = 1;
  
  
  while (true && cnt < 300)
    {
      if (SCSI_TestUnitReady(INDEX_TAPECTL, pRequestSense))
        {
          true = 0;
          break;
        }
      if (SenseHandler(INDEX_TAPECTL, 0, (char *)pRequestSense) == SENSE_NO_TAPE)
        {
          true=0;
          break;
        } else {
          cnt++;
          sleep(2);
        }
    }
  
  free(pRequestSense);
  
  dbprintf(("DLT4000Eject : Ready after %d sec, true = %d\n", cnt * 2, true));
  return(0);
  
}

int GenericEject(char *Device, int type)
{
  extern OpenFiles_T *pDev;
  RequestSense_T *pRequestSense;
  int ret;
  int cnt = 0;
  int true = 1;
  
  dbprintf(("##### START GenericEject\n"));
  
  if ((pRequestSense = malloc(sizeof(RequestSense_T))) == NULL)
    {
      dbprintf(("%-20s : malloc failed\n","GenericEject"));
      return(-1);
    }
    
  dbprintf(("GenericEject : SCSI eject on %s = %s\n", pDev[INDEX_TAPECTL].dev, pDev[INDEX_TAPECTL].ConfigName));
  
  /* Unload the tape, 1 == don't wait for success
   * 0 == unload 
   */
  ret = SCSI_LoadUnload(INDEX_TAPECTL, pRequestSense, 1, 0);
  
  /* < 0 == fatal */
  if (ret < 0)
    return(-1);
  
  if ( ret > 0)
    {
    }
  
  true = 1;
  
  
  while (true && cnt < 300)
    {
      if (SCSI_TestUnitReady(INDEX_TAPECTL, pRequestSense))
        {
          true = 0;
          break;
        }
      if (SenseHandler(INDEX_TAPECTL, 0, (char *)pRequestSense) == SENSE_NO_TAPE)
        {
          true=0;
          break;
        } else {
          cnt++;
          sleep(2);
        }
    }
  
  free(pRequestSense);
  
  dbprintf(("GenericEject : Ready after %d sec, true = %d\n", cnt * 2, true));
  return(0);
  
}

int GenericRewind(int DeviceFD)
{
  CDB_T CDB;
  RequestSense_T *pRequestSense;
  int ret;
  int cnt = 0;
  int true = 1;

  dbprintf(("##### START GenericRewind\n"));

  if ((pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T))) == NULL)
      {
          dbprintf(("GenericRewind : malloc failed\n"));
          return(-1);
      }

  CDB[0] = SC_COM_REWIND;
  CDB[1] = 1;             
  CDB[2] = 0;
  CDB[3] = 0;
  CDB[4] = 0;
  CDB[5] = 0;

  while (true)
    {
      ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 6,
                                NULL, 0, 
                                (char *) pRequestSense,
                                sizeof(RequestSense_T));
      
      DecodeSense(pRequestSense, "GenericRewind : ", debug_file);
      
      if (ret > 0) 
        {
          if (pRequestSense->SenseKey != UNIT_ATTENTION)
            {
              true = 0;
            }
        }
      if (ret == 0)
        {
          true = 0;
        }
      if (ret < 0)
        {
          dbprintf(("GenericRewind : failed %d\n", ret));
          true = 0;
        }
    }

  true = 1;

  while (true && cnt < 300)
    {
      if (SCSI_TestUnitReady(DeviceFD, pRequestSense))
        {
          true = 0;
          break;
        }
      if (SenseHandler(DeviceFD, 0, (char *)pRequestSense) == SENSE_NO_TAPE)
        {
          return(-1);
          break;
        } else {
          cnt++;
          sleep(2);
        }
    }
  
  free(pRequestSense);
  
  dbprintf(("GenericRewind : Ready after %d sec, true = %d\n", cnt * 2, true));
  return(0);
}

int GenericClean(char * Device)
{
  extern OpenFiles_T *pDev;
  ExtendedRequestSense_T ExtRequestSense;

  dbprintf(("##### START GenericClean\n"));
  if (pDev[INDEX_TAPECTL].SCSI == 0)
      {
          dbprintf(("GenericClean : can't send SCSI commands\n"));
          return(0);
      }

  RequestSense(INDEX_TAPECTL, &ExtRequestSense, 0);
  dbprintf(("GenericClean :\n"));
  DecodeExtSense(&ExtRequestSense, "GenericClean : ", debug_file);
  if(ExtRequestSense.CLN) {
    return(1);
  } else {
    return(0);
  }
}

int GenericResetStatus(int DeviceFD)
{
  CDB_T CDB;
  RequestSense_T *pRequestSense;
  int ret;
  int retry = 1;
  
  dbprintf(("##### START GenericResetStatus\n"));
  
  if ((pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T))) == NULL)
      {
          dbprintf(("GenericResetStatus : malloc failed\n"));
          return(-1);
      }

  while (retry)
    {
      CDB[0] = SC_COM_IES;   /* */
      CDB[1] = 0;
      CDB[2] = 0;
      CDB[3] = 0;
      CDB[4] = 0;
      CDB[5] = 0;


      ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 6,
                                NULL, 0,
                                (char *) pRequestSense,
                                sizeof(RequestSense_T));

      if (ret < 0)
        {
          /*        fprintf(stderr, "%s: Request Sense[Inquiry]: %02X", */
          /*                "chs", ((unsigned char *) &pRequestSense)[0]); */
          /*        for (i = 1; i < sizeof(RequestSense_T); i++)                */
          /*          fprintf(stderr, " %02X", ((unsigned char *) &pRequestSense)[i]); */
          /*        fprintf(stderr, "\n");    */
          return(ret);
        }
      if ( ret > 0 && InErrorHandler == 0)
        {
          switch (SenseHandler(DeviceFD, 0, (char *)pRequestSense))
            {
            case SENSE_IGNORE:
              return(0);
              break;
            case SENSE_ABORT:
              return(-1);
              break;
            case SENSE_RETRY:
              retry++;
              if (retry < MAX_RETRIES )
                {
                  dbprintf(("ResetStatus : retry %d\n", retry));
                  sleep(2);
                } else {
                  return(-1);
                }
              break;
            default:
              return(-1);
              break;
            }
        }
      if (ret == 0)
        retry = 0;
    }
  return(ret);
}

/* GenericSenseHandler
 * Handles the conditions/sense wich is returned by an SCSI command
 * pwork is an pointer to the structure OpenFiles_T, which is filled with information
 * about the device to which we talk. Information are for example
 * The vendor, the ident, which fd, etc. This strucure is filled when we open the
 * device 
 * flag tells how to handle the information passed in the buffer,
 * 0 -> Sense Key available
 * 1 -> No Sense key available
 * buffer is a pointer to the data from the request sense result.
 *
 * TODO:
 * Limit recursion, may run in an infinite loop
 */
int GenericSenseHandler(int ip, int flag, char *buffer)
{ 
  extern OpenFiles_T *pDev;
  RequestSense_T *pRequestSense = (RequestSense_T *)buffer;
  int ret = 0;
  unsigned char *info = NULL;
  
  dbprintf(("##### START GenericSenseHandler\n"));
  
  DecodeSense(pRequestSense, "GenericSenseHandler : ", debug_file);
  
  ret = Sense2Action(pDev[ip].ident,
		     pDev[ip].inquiry->type,
		     flag, pRequestSense->SenseKey,
		     pRequestSense->AdditionalSenseCode,
		     pRequestSense->AdditionalSenseCodeQualifier,
		     (char **)&info);
  
  dbprintf(("##### STOP GenericSenseHandler\n"));
  return(ret);
}

/* ======================================================= */
int SDXMove(int DeviceFD, int from, int to)
{
  extern OpenFiles_T *pDev;
  ElementInfo_T *pfrom;
  ElementInfo_T *pto;
  int ret;
  int tapestat;
  int moveok = 0;
  int SDX_MTE = 0;      /* This are parameters  passed */
  int SDX_STE = -1;     /* to                          */
  int SDX_DTE = -1;     /* AlignElements               */

  dbprintf(("##### START SDXMove\n"));

  dbprintf(("%-20s : from = %d, to = %d\n", "SDXMove", from, to));

  /* FIXME
   * There should be an check if a tape is loaded !!
   */
 
  if ((pfrom = LookupElement(from)) == NULL)
    {
      dbprintf(("SDXMove : ElementInfo for %d not found\n", from));
    }
  
  if ((pto = LookupElement(to)) == NULL)
    {
      dbprintf(("SDXMove : ElementInfo for %d not found\n", to));
    }
  
  if (pfrom->status == 'E') 
    {
      dbprintf(("SDXMove : from %d is empty\n", from));
    }

  if (pto->status == 'F') 
    {
      switch (pto->status)
      {
         case CHANGER:
           break;
         case STORAGE:
           dbprintf(("SDXMove : Destination Element %d Type %d is full\n",
                pto->address, pto->type));
            to = find_empty(DeviceFD, 0, 0);
            dbprintf(("GenericMove : Unload to %d\n", to));
            if ((pto = LookupElement(to)) == NULL)
            {
            
            }
           break;
         case IMPORT:
           break;
         case TAPETYPE:
           break;
      }
    }

  moveok = CheckMove(pfrom, pto);

  switch (pto->type)
  {
    case TAPETYPE:
      SDX_DTE = pto->address;
      break;
    case STORAGE:
     SDX_STE = pto->address;
     break;
    case IMPORT:
     SDX_STE = pto->address;
     break;
  }

  switch (pfrom->type)
  {
    case TAPETYPE:
      SDX_DTE = pfrom->address;
      break;
    case STORAGE:
     SDX_STE = pfrom->address;
     break;
    case IMPORT:
     SDX_STE = pfrom->address;
     break;
  }

  if (SDX_DTE >= 0 && SDX_STE >= 0)
  {
    ret = SCSI_AlignElements(DeviceFD, SDX_MTE, SDX_DTE, SDX_STE);
    dbprintf(("##### SCSI_AlignElemnts ret = %d\n",ret));
    if (ret != 0 )
    {
       return(-1);
    }
  } else {
    dbprintf(("##### Error setting STE/DTE %d/%d\n", SDX_STE, SDX_DTE));
    return(-1);
  }

  /* If from is a tape we must check if it is loaded */
  /* and if yes we have to eject it                  */
  if (pfrom->type == TAPETYPE)
  {
    tapestat = Tape_Status(INDEX_TAPE);
    if ( tapestat & TAPE_ONLINE)
    {
      if (pDev[INDEX_TAPECTL].SCSI == 1)
      { 
        ret = eject_tape(pDev[INDEX_TAPECTL].dev,1);
      } else {
        ret = eject_tape(pDev[INDEX_TAPE].dev,2);
      }
    }
  }

  if ( ret == 0)
  {
    ret = SCSI_Move(DeviceFD, 0, from, to);
  } else {
    return(ret);
  }
  return(ret);
}

/* ======================================================= */
int GenericMove(int DeviceFD, int from, int to)
{
  ElementInfo_T *pfrom;
  ElementInfo_T *pto;
  int ret = 0;

  dbprintf(("##### START GenericMove\n"));

  dbprintf(("%-20s : from = %d, to = %d\n", "GenericMove", from, to));


  if ((pfrom = LookupElement(from)) == NULL)
    {
      dbprintf(("GenericMove : ElementInfo for %d not found\n", from));
    }
  
  if ((pto = LookupElement(to)) == NULL)
    {
      dbprintf(("GenericMove : ElementInfo for %d not found\n", to));
    }
  
  if (pfrom->status == 'E') 
    {
      dbprintf(("GenericMove : from %d is empty\n", from));
    }

  if (pto->status == 'F') 
    {
      dbprintf(("GenericMove : Destination Element %d Type %d is full\n",
                pto->address, pto->type));
      to = find_empty(DeviceFD, 0, 0);
      dbprintf(("GenericMove : Unload to %d\n", to));
      if ((pto = LookupElement(to)) == NULL)
        {
          
        }
    }
  
	if (CheckMove(pfrom, pto))
	{
	  ret = SCSI_Move(DeviceFD, 0, from, to);
	}
  return(ret);
}

/*
 * Check if a move based on the information we got from the Mode Sense command
 * is legal
 * Return Values:
 * 1 => OK
 * 0 => Not OK
 */

int CheckMove(ElementInfo_T *from, ElementInfo_T *to)
{
	int moveok = 0;

  if (pDeviceCapabilitiesPage != NULL )
    {
      dbprintf(("CheckMove : checking if move from %d to %d is legal\n", from->address, to->address));
      switch (from->type)
        {
        case CHANGER:
          dbprintf(("CheckMove : MT2"));
          switch (to->type)
            {
            case CHANGER:
              if (pDeviceCapabilitiesPage->MT2MT == 1)
                {
                  dbprintf(("MT\n"));
                  moveok = 1;
                }
              break;
            case STORAGE:
              if (pDeviceCapabilitiesPage->MT2ST == 1)
                {
                  dbprintf(("ST\n"));
                  moveok = 1;
                }
              break;
            case IMPORT:
              if (pDeviceCapabilitiesPage->MT2IE == 1)
                {
                  dbprintf(("IE\n"));
                  moveok = 1;
                }
              break;
            case TAPETYPE:
              if (pDeviceCapabilitiesPage->MT2DT == 1)
                {
                  dbprintf(("DT\n"));
                  moveok = 1;
                }
              break;
            default:
              break;
            }
          break;
        case STORAGE:
          dbprintf(("CheckMove : ST2"));
          switch (to->type)
            {
            case CHANGER:
              if (pDeviceCapabilitiesPage->ST2MT == 1)
                {
                  dbprintf(("MT\n"));
                  moveok = 1;
                }
              break;
            case STORAGE:
              if (pDeviceCapabilitiesPage->ST2ST == 1)
                {
                  dbprintf(("ST\n"));
                  moveok = 1;
                }
              break;
            case IMPORT:
              if (pDeviceCapabilitiesPage->ST2IE == 1)
                {
                  dbprintf(("IE\n"));
                  moveok = 1;
                }
              break;
            case TAPETYPE:
              if (pDeviceCapabilitiesPage->ST2DT == 1)
                {
                  dbprintf(("DT\n"));
                  moveok = 1;
                }
              break;
            default:
              break;
            }
          break;
        case IMPORT:
          dbprintf(("CheckMove : IE2"));
          switch (to->type)
            {
            case CHANGER:
              if (pDeviceCapabilitiesPage->IE2MT == 1)
                {
                  dbprintf(("MT\n"));
                  moveok = 1;
                }
              break;
            case STORAGE:
              if (pDeviceCapabilitiesPage->IE2ST == 1)
                {
                  dbprintf(("ST\n"));
                  moveok = 1;
                }
              break;
            case IMPORT:
              if (pDeviceCapabilitiesPage->IE2IE == 1)
                {
                  dbprintf(("IE\n"));
                  moveok = 1;
                }
              break;
            case TAPETYPE:
              if (pDeviceCapabilitiesPage->IE2DT == 1)
                {
                  dbprintf(("DT\n"));
                  moveok = 1;
                }
              break;
            default:
              break;
            }
          break;
        case TAPETYPE:
          dbprintf(("CheckMove : DT2"));
          switch (to->type)
            {
            case CHANGER:
              if (pDeviceCapabilitiesPage->DT2MT == 1)
                {
                  dbprintf(("MT\n"));
                  moveok = 1;
                }
              break;
            case STORAGE:
              if (pDeviceCapabilitiesPage->DT2ST == 1)
                {
                  dbprintf(("ST\n"));
                  moveok = 1;
                }
              break;
            case IMPORT:
              if (pDeviceCapabilitiesPage->DT2IE == 1)
                {
                  dbprintf(("IE\n"));
                  moveok = 1;
                }
              break;
            case TAPETYPE:
              if (pDeviceCapabilitiesPage->DT2DT == 1)
                {
                  dbprintf(("DT\n"));
                  moveok = 1;
                }
              break;
            default:
              break;
            }
          break;
        default:
          break;
        }
    } else {
	    dbprintf(("CheckMove : pDeviceCapabilitiesPage == NULL"));
	    ChgExit("CheckMove", "DeviceCapabilitiesPage == NULL", FATAL);
    }
    return(moveok);
}

/*
 */
 
int GetCurrentSlot(int fd, int drive)
{
  extern OpenFiles_T *pDev;
  int x;
  dbprintf(("##### START GetCurrentSlot\n"));

  if (pDev[0].SCSI == 0)
      {
          dbprintf(("GetCurrentSlot : can't send SCSI commands\n"));
          return(-1);
      }

  if (ElementStatusValid == 0)
    {
      if (pDev[0].functions->function_status(0, 1) != 0)
        {
          return(-1);
        }
    }   

  /* If the from address is the as the same as the tape address skip it */
  if (pDTE[drive].from >= 0 && pDTE[drive].from != pDTE[drive].address)
    {
      for (x = 0; x < STE;x++)
        {
          if (pSTE[x].address == pDTE[drive].from)
            return(x);
        }
      return(-1);
    }

  for (x = 0; x < STE;x++)
    {
      if (pSTE[x].status == 'E')
        return(x);
    }

  /* Ups nothing loaded */
  return(-1);
}

int DLT448ElementStatus(int DeviceFD, int InitStatus)
{
  unsigned char *DataBuffer = NULL;
  int DataBufferLength;
  ElementStatusData_T *ElementStatusData;
  ElementStatusPage_T *ElementStatusPage;
  MediumTransportElementDescriptor_T *MediumTransportElementDescriptor;
  StorageElementDescriptor_T *StorageElementDescriptor;
  DataTransferElementDescriptor_T *DataTransferElementDescriptor;
  ImportExportElementDescriptor_T *ImportExportElementDescriptor;

  int ESerror = 0;                /* Is set if ASC for an element is set */
  int x = 0;
  int offset = 0;
  int NoOfElements;

  dbprintf(("##### START DLT448ElementStatus\n"));

  if (pEAAPage == NULL)
    {
      if (pModePage == NULL)
        {
          if ((pModePage = malloc(0xff)) == NULL)
            {
              dbprintf(("DLT448ElementStatus : malloc failed\n"));
              return(-1);
            }
        }
      if (SCSI_ModeSense(DeviceFD, pModePage, 0xff, 0x8, 0x3f) == 0)
        DecodeModeSense(pModePage, 12, "DLT448ElementStatus :", 0, debug_file);
    }
  /* If the MODE_SENSE was successfull we use this Information to read the Elelement Info */
  if (pEAAPage != NULL)
    {
      /* First the Medim Transport*/
      if (V2(pEAAPage->NoMediumTransportElements)  > 0)
        {
          MTE = V2(pEAAPage->NoMediumTransportElements) ;
          if (pMTE == NULL)
            {
              if ((pMTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * MTE)) == NULL)
                {
                  dbprintf(("GenericElementStatus : malloc failed\n"));
                  return(-1);
                }
            }
          memset(pMTE, 0, sizeof(ElementInfo_T) * MTE);

          if (SCSI_ReadElementStatus(DeviceFD, 
                                     CHANGER, 
                                     0,
                                     BarCode(DeviceFD),
                                     V2(pEAAPage->MediumTransportElementAddress),
                                     MTE,
                                     (char **)&DataBuffer) != 0)
            {
              if (DataBuffer != 0)
              {
                free(DataBuffer);
              }
              ChgExit("genericElementStatus","Can't read MTE status", FATAL);
            }
          ElementStatusData = (ElementStatusData_T *)DataBuffer;
          offset = sizeof(ElementStatusData_T);
          
          ElementStatusPage = (ElementStatusPage_T *)&DataBuffer[offset];
          offset = offset + sizeof(ElementStatusPage_T);
          
          for (x = 0; x < MTE; x++)
            {
              MediumTransportElementDescriptor = (MediumTransportElementDescriptor_T *)&DataBuffer[offset];
              if (ElementStatusPage->pvoltag == 1)
                {
                  strncpy(pMTE[x].VolTag, 
                          MediumTransportElementDescriptor->res4,
                          TAG_SIZE);
                  TerminateString(pMTE[x].VolTag, TAG_SIZE+1);
                }
              pMTE[x].type = ElementStatusPage->type;
              pMTE[x].address = V2(MediumTransportElementDescriptor->address);
              pMTE[x].except = MediumTransportElementDescriptor->except;
              pMTE[x].ASC = MediumTransportElementDescriptor->asc;
              pMTE[x].ASCQ = MediumTransportElementDescriptor->ascq;
              pMTE[x].status = (MediumTransportElementDescriptor->full > 0) ? 'F':'E';
              pMTE[x].full = MediumTransportElementDescriptor->full;

              if (MediumTransportElementDescriptor->svalid == 1)
                {
                  pMTE[x].from = V2(MediumTransportElementDescriptor->source);
                } else {
                  pMTE[x].from = -1;
                }
              
              if (pMTE[x].ASC > 0)
                {
                  switch(SenseHandler(DeviceFD, 1, (char *)&pMTE[x]))
                    {
                    case SENSE_IES:
                      ESerror=1;
                      break;
                    case SENSE_ABORT:
                      return(-1);
                      break;
                    }
                }
              offset = offset + V2(ElementStatusPage->length); 
            }
        }
      /* Storage Elements */
      if ( V2(pEAAPage->NoStorageElements)  > 0)
        {
          STE = V2(pEAAPage->NoStorageElements);
          if (pSTE == NULL)
            {
              if ((pSTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * STE)) == NULL)
                {
                  dbprintf(("GenericElementStatus : malloc failed\n"));
                  return(-1);
                }
            }
          memset(pSTE, 0, sizeof(ElementInfo_T) * STE);
          
          if (SCSI_ReadElementStatus(DeviceFD, 
                                     STORAGE, 
                                     0,
                                     BarCode(DeviceFD),
                                     V2(pEAAPage->FirstStorageElementAddress),
                                     STE,
                                     (char **)&DataBuffer) != 0)
            {
              if (DataBuffer != 0)
              {
                free(DataBuffer);
              }
              ChgExit("GenericElementStatus", "Can't read STE status", FATAL);
            }
          
          ElementStatusData = (ElementStatusData_T *)DataBuffer;
          offset = sizeof(ElementStatusData_T);
          
          ElementStatusPage = (ElementStatusPage_T *)&DataBuffer[offset];
          offset = offset + sizeof(ElementStatusPage_T);
          
          for (x = 0; x < STE; x++)
            {
              StorageElementDescriptor = (StorageElementDescriptor_T *)&DataBuffer[offset];
              if (ElementStatusPage->pvoltag == 1)
                {
                  strncpy(pSTE[x].VolTag, 
                          StorageElementDescriptor->res4,
                          TAG_SIZE);
                  TerminateString(pSTE[x].VolTag, TAG_SIZE+1);
                }
              
              
              pSTE[x].type = ElementStatusPage->type;
              pSTE[x].address = V2(StorageElementDescriptor->address);
              pSTE[x].except = StorageElementDescriptor->except;
              pSTE[x].ASC = StorageElementDescriptor->asc;
              pSTE[x].ASCQ = StorageElementDescriptor->ascq;
              pSTE[x].status = (StorageElementDescriptor->full > 0) ? 'F':'E';
              pSTE[x].full = StorageElementDescriptor->full;
              
              if (StorageElementDescriptor->svalid == 1)
                {
                  pSTE[x].from = V2(StorageElementDescriptor->source);
                } else {
                  pSTE[x].from = -1;
                }
              
              if (pSTE[x].ASC > 0)
                {
                  switch(SenseHandler(DeviceFD, 1, (char *)&pSTE[x]))
                    {
                    case SENSE_IES:
                      ESerror=1;
                      break;
                    case SENSE_ABORT:
                      return(-1);
                      break;
                    }
                }
              
              offset = offset + V2(ElementStatusPage->length); 
            }
          
        }
      /* Import/Export Elements */
      if ( V2(pEAAPage->NoImportExportElements) > 0)
        {
          IEE = V2(pEAAPage->NoImportExportElements);
          if (pIEE == NULL)
            {
              if ((pIEE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * IEE)) == NULL)
                {
                  dbprintf(("GenericElementStatus : malloc failed\n"));
                  return(-1);
                }
            }
          memset(pIEE, 0, sizeof(ElementInfo_T) * IEE);
          if (SCSI_ReadElementStatus(DeviceFD, 
                                     IMPORT, 
                                     0,
                                     BarCode(DeviceFD),
                                     V2(pEAAPage->FirstImportExportElementAddress),
                                     IEE,
                                     (char **)&DataBuffer) != 0)
            {
              if (DataBuffer != 0)
              {
                free(DataBuffer);
              }
              ChgExit("GenericElementStatus", "Can't read IEE status", FATAL);
            }
          
          ElementStatusData = (ElementStatusData_T *)DataBuffer;
          offset = sizeof(ElementStatusData_T);
          
          ElementStatusPage = (ElementStatusPage_T *)&DataBuffer[offset];
          offset = offset + sizeof(ElementStatusPage_T);
          
          for (x = 0; x < IEE; x++)
            {
              ImportExportElementDescriptor = (ImportExportElementDescriptor_T *)&DataBuffer[offset];
              if (ElementStatusPage->pvoltag == 1)
                {
                  strncpy(pIEE[x].VolTag, 
                          ImportExportElementDescriptor->res4,
                          TAG_SIZE);
                  TerminateString(pIEE[x].VolTag, TAG_SIZE+1);
                }
              pIEE[x].type = ElementStatusPage->type;
              pIEE[x].address = V2(ImportExportElementDescriptor->address);
              pIEE[x].except = ImportExportElementDescriptor->except;
              pIEE[x].ASC = ImportExportElementDescriptor->asc;
              pIEE[x].ASCQ = ImportExportElementDescriptor->ascq;
              pIEE[x].status = (ImportExportElementDescriptor->full > 0) ? 'F':'E';
              pIEE[x].full = ImportExportElementDescriptor->full;
              
              if (ImportExportElementDescriptor->svalid == 1)
                {
                  pIEE[x].from = V2(ImportExportElementDescriptor->source);
                } else {
                  pIEE[x].from = -1;
                }
              
              if (pIEE[x].ASC > 0)
                {
                  switch(SenseHandler(DeviceFD, 1, (char *)&pIEE[x]))
                    {
                    case SENSE_IES:
                      ESerror=1;
                      break;
                    case SENSE_ABORT:
                      return(-1);
                      break;
                    }
                }
              
              offset = offset + V2(ElementStatusPage->length); 
            }
          
        }
      /* Data Transfer Elements*/
      if (V2(pEAAPage->NoDataTransferElements) >0)
        {
          DTE = V2(pEAAPage->NoDataTransferElements) ;
          if (pDTE == NULL)
            {
              if ((pDTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * DTE)) == NULL)
                {
                  dbprintf(("GenericElementStatus : malloc failed\n"));
                  return(-1);
                }
            }
          memset(pDTE, 0, sizeof(ElementInfo_T) * DTE);
          if (SCSI_ReadElementStatus(DeviceFD, 
                                     TAPETYPE, 
                                     0,
                                     BarCode(DeviceFD),
                                     V2(pEAAPage->FirstDataTransferElementAddress),
                                     DTE,
                                     (char **)&DataBuffer) != 0)
            {
              if (DataBuffer != 0)
              {
                free(DataBuffer);
              }
              ChgExit("GenericElementStatus", "Can't read DTE status", FATAL);
            }
          
          ElementStatusData = (ElementStatusData_T *)DataBuffer;
          offset = sizeof(ElementStatusData_T);
          
          ElementStatusPage = (ElementStatusPage_T *)&DataBuffer[offset];
          offset = offset + sizeof(ElementStatusPage_T);

          for (x = 0; x < DTE; x++)
            {
              DataTransferElementDescriptor = (DataTransferElementDescriptor_T *)&DataBuffer[offset];
              if (ElementStatusPage->pvoltag == 1)
                {
                  strncpy(pDTE[x].VolTag, 
                          DataTransferElementDescriptor->res4,
                          TAG_SIZE);
                  TerminateString(pDTE[x].VolTag, TAG_SIZE+1);
                }
              pDTE[x].type = ElementStatusPage->type;               pDTE[x].address = V2(DataTransferElementDescriptor->address);
              pDTE[x].except = DataTransferElementDescriptor->except;
              pDTE[x].ASC = DataTransferElementDescriptor->asc;
              pDTE[x].ASCQ = DataTransferElementDescriptor->ascq;
              pDTE[x].scsi = DataTransferElementDescriptor->scsi;
              pDTE[x].status = (DataTransferElementDescriptor->full > 0) ? 'F':'E';
              pDTE[x].full = DataTransferElementDescriptor->full;
              
              if (DataTransferElementDescriptor->svalid == 1)
                {
                  pDTE[x].from = V2(DataTransferElementDescriptor->source);
                } else {
                  pDTE[x].from = -1;
                }
              
              if (pDTE[x].ASC > 0)
                {
                  switch(SenseHandler(DeviceFD,  1, (char *)&pDTE[x]))
                    {
                    case SENSE_IES:
                      ESerror=1;
                      break;
                    case SENSE_ABORT:
                      return(-1);
                      break;
                    }
                }
              
              offset = offset + V2(ElementStatusPage->length); 
            }
        }
    } else {  
      if (SCSI_ReadElementStatus(DeviceFD, 
                                 0, 
                                 0,
                                 BarCode(DeviceFD),
                                 0,
                                 0xffff,
                                 (char **)&DataBuffer) != 0)
        {
              if (DataBuffer != 0)
              {
                free(DataBuffer);
              }
          ChgExit("GenericElementStatus","Can't get ElementStatus", FATAL);
        }
      
      ElementStatusData = (ElementStatusData_T *)DataBuffer;
      DataBufferLength = V3(ElementStatusData->count);
      
      offset = sizeof(ElementStatusData_T);
      
      if (DataBufferLength <= 0) 
        {
          dbprintf(("DataBufferLength %d\n",DataBufferLength));
          return(1);
        }
      
      while (offset < DataBufferLength) 
        {
          ElementStatusPage = (ElementStatusPage_T *)&DataBuffer[offset];
          NoOfElements = V3(ElementStatusPage->count) / V2(ElementStatusPage->length);
          offset = offset + sizeof(ElementStatusPage_T);
          
          switch (ElementStatusPage->type)
            {
            case 1:
              MTE = NoOfElements;
              if ((pMTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * MTE)) == NULL)
                {
                  dbprintf(("GenericElementStatus : malloc failed\n"));
                  return(-1);
                }
              memset(pMTE, 0, sizeof(ElementInfo_T) * MTE);
              for (x = 0; x < NoOfElements; x++)
                {
                  /*               dbprintf(("PVolTag %d, AVolTag %d\n", */
                  /*                         ElementStatusPage->pvoltag, */
                  /*                         ElementStatusPage->avoltag)); */
                  
                  MediumTransportElementDescriptor = (MediumTransportElementDescriptor_T *)&DataBuffer[offset];
                  if (ElementStatusPage->pvoltag == 1)
                    {
                      strncpy(pMTE[x].VolTag, 
                              MediumTransportElementDescriptor->res4,
                              TAG_SIZE);
                      TerminateString(pMTE[x].VolTag, TAG_SIZE+1);
                    }
                  pMTE[x].type = ElementStatusPage->type;
                  pMTE[x].address = V2(MediumTransportElementDescriptor->address);
                  pMTE[x].except = MediumTransportElementDescriptor->except;
                  pMTE[x].ASC = MediumTransportElementDescriptor->asc;
                  pMTE[x].ASCQ = MediumTransportElementDescriptor->ascq;
                  pMTE[x].status = (MediumTransportElementDescriptor->full > 0) ? 'F':'E';
                  pMTE[x].full = MediumTransportElementDescriptor->full;
                  
                  if (MediumTransportElementDescriptor->svalid == 1)
                    {
                      pMTE[x].from = V2(MediumTransportElementDescriptor->source);
                    } else {
                      pMTE[x].from = -1;
                    }
                  
                  if (pMTE[x].ASC > 0)
                    {
                      if (SenseHandler(DeviceFD,  1, (char *)&pMTE[x]) == SENSE_RETRY)
                        ESerror = 1;
                    }
                  offset = offset + V2(ElementStatusPage->length); 
                }
              break;
            case 2:
              STE = NoOfElements;
              if ((pSTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * STE)) == NULL)
                {
                  dbprintf(("GenericElementStatus : malloc failed\n"));
                  return(-1);
                }
              memset(pSTE, 0, sizeof(ElementInfo_T) * STE);
              for (x = 0; x < NoOfElements; x++)
                {
                  /*               dbprintf(("PVolTag %d, AVolTag %d\n", */
                  /*                         ElementStatusPage->pvoltag, */
                  /*                         ElementStatusPage->avoltag)); */
                  
                  StorageElementDescriptor = (StorageElementDescriptor_T *)&DataBuffer[offset];
                  if (ElementStatusPage->pvoltag == 1)
                    {
                      strncpy(pSTE[x].VolTag, 
                              StorageElementDescriptor->res4,
                              TAG_SIZE);
                      TerminateString(pSTE[x].VolTag, TAG_SIZE+1);
                    }
                  
                  pSTE[x].type = ElementStatusPage->type;
                  pSTE[x].address = V2(StorageElementDescriptor->address);
                  pSTE[x].except = StorageElementDescriptor->except;
                  pSTE[x].ASC = StorageElementDescriptor->asc;
                  pSTE[x].ASCQ = StorageElementDescriptor->ascq;
                  pSTE[x].status = (StorageElementDescriptor->full > 0) ? 'F':'E';
                  pSTE[x].full = StorageElementDescriptor->full;
                            
                  if (StorageElementDescriptor->svalid == 1)
                    {
                      pSTE[x].from = V2(StorageElementDescriptor->source);
                    } else {
                      pSTE[x].from = -1;
                    }
                            
                  if (pSTE[x].ASC > 0)
                    {
                      if (SenseHandler(DeviceFD,  1, (char *)&pSTE[x]) == SENSE_RETRY)
                        ESerror = 1;
                    }
                            
                  offset = offset + V2(ElementStatusPage->length); 
                }
              break;
            case 3:
              IEE = NoOfElements;
              if ((pIEE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * IEE)) == NULL)
                {
                  dbprintf(("DLT448ElementStatus : malloc failed\n"));
                  return(-1);
                }
              memset(pIEE, 0, sizeof(ElementInfo_T) * IEE);
              
              for (x = 0; x < NoOfElements; x++)
                {
                  ImportExportElementDescriptor = (ImportExportElementDescriptor_T *)&DataBuffer[offset];
                  if (ElementStatusPage->pvoltag == 1)
                    {
                      strncpy(pIEE[x].VolTag, 
                              ImportExportElementDescriptor->res4,
                              TAG_SIZE);
                      TerminateString(pIEE[x].VolTag, TAG_SIZE+1);
                    }
                  ImportExportElementDescriptor = (ImportExportElementDescriptor_T *)&DataBuffer[offset];
                  pIEE[x].type = ElementStatusPage->type;
                  pIEE[x].address = V2(ImportExportElementDescriptor->address);
                  pIEE[x].except = ImportExportElementDescriptor->except;
                  pIEE[x].ASC = ImportExportElementDescriptor->asc;
                  pIEE[x].ASCQ = ImportExportElementDescriptor->ascq;
                  pIEE[x].status = (ImportExportElementDescriptor->full > 0) ? 'F':'E';
                  pIEE[x].full = ImportExportElementDescriptor->full;
                  
                  if (ImportExportElementDescriptor->svalid == 1)
                    {
                      pIEE[x].from = V2(ImportExportElementDescriptor->source);
                    } else {
                      pIEE[x].from = -1;
                    }
                  
                  if (pIEE[x].ASC > 0)
                    {
                      if (SenseHandler(DeviceFD, 1, (char *)&pIEE[x]) == SENSE_RETRY)
                        ESerror = 1;
                    }
                  
                  offset = offset + V2(ElementStatusPage->length); 
                }
              break;
            case 4:
              DTE = NoOfElements;
              if ((pDTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * DTE)) == NULL)
                {
                  dbprintf(("GenericElementStatus : malloc failed\n"));
                  return(-1);
                }
              memset(pDTE, 0, sizeof(ElementInfo_T) * DTE);
                        
              for (x = 0; x < NoOfElements; x++)
                {
                  /*               dbprintf(("PVolTag %d, AVolTag %d\n", */
                  /*                         ElementStatusPage->pvoltag, */
                  /*                         ElementStatusPage->avoltag)); */
                            
                  DataTransferElementDescriptor = (DataTransferElementDescriptor_T *)&DataBuffer[offset];
                  if (ElementStatusPage->pvoltag == 1)
                    {
                      strncpy(pSTE[x].VolTag, 
                              DataTransferElementDescriptor->res4,
                              TAG_SIZE);
                      TerminateString(pSTE[x].VolTag, TAG_SIZE+1);
                    }
                  pDTE[x].type = ElementStatusPage->type;
                  pDTE[x].address = V2(DataTransferElementDescriptor->address);
                  pDTE[x].except = DataTransferElementDescriptor->except;
                  pDTE[x].ASC = DataTransferElementDescriptor->asc;
                  pDTE[x].ASCQ = DataTransferElementDescriptor->ascq;
                  pDTE[x].scsi = DataTransferElementDescriptor->scsi;
                  pDTE[x].status = (DataTransferElementDescriptor->full > 0) ? 'F':'E';
                  pDTE[x].full = DataTransferElementDescriptor->full;
                            
                  if (DataTransferElementDescriptor->svalid == 1)
                    {
                      pDTE[x].from = V2(DataTransferElementDescriptor->source);
                    } else {
                      pDTE[x].from = -1;
                    }
                            
                  if (pDTE[x].ASC > 0)
                    {
                      if (SenseHandler(DeviceFD, 1, (char *)&pDTE[x]) == SENSE_RETRY)
                        ESerror = 1;
                    }
                            
                  offset = offset + V2(ElementStatusPage->length); 
                }
              break;
            default:
              offset = offset + V2(ElementStatusPage->length); 
              dbprintf(("ReadElementStatus : UnGknown Type %d\n",ElementStatusPage->type));
              break;
            }
        }
    }

  dbprintf(("\n\n\tMedia Transport Elements (robot arms) :\n"));

  for ( x = 0; x < MTE; x++)
    dbprintf(("\t\tElement #%04d %c\n\t\t\tEXCEPT = %02x\n\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n",
              pMTE[x].address, pMTE[x].status, pMTE[x].except, pMTE[x].ASC,
              pMTE[x].ASCQ, pMTE[x].type, pMTE[x].from, pMTE[x].VolTag));

  dbprintf(("\n\n\tStorage Elements (Media slots) :\n"));

  for ( x = 0; x < STE; x++)
    dbprintf(("\t\tElement #%04d %c\n\t\t\tEXCEPT = %02X\n\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n",
              pSTE[x].address, pSTE[x].status, pSTE[x].except, pSTE[x].ASC,
              pSTE[x].ASCQ, pSTE[x].type, pSTE[x].from, pSTE[x].VolTag));

  dbprintf(("\n\n\tData Transfer Elements (tape drives) :\n"));
 
  for ( x = 0; x < DTE; x++)
    dbprintf(("\t\tElement #%04d %c\n\t\t\tEXCEPT = %02X\n\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n\t\t\tSCSI ADDRESS = %d\n",
              pDTE[x].address, pDTE[x].status, pDTE[x].except, pDTE[x].ASC,
              pDTE[x].ASCQ, pDTE[x].type, pDTE[x].from, pDTE[x].VolTag,pDTE[x].scsi));

  dbprintf(("\n\n\tImport/Export Elements  :\n"));

  for ( x = 0; x < IEE; x++)
    dbprintf(("\t\tElement #%04d %c\n\t\t\tEXCEPT = %02X\n\t\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n",
              pIEE[x].address, pIEE[x].status, pIEE[x].except, pIEE[x].ASC,
              pIEE[x].ASCQ, pIEE[x].type, pIEE[x].from, pIEE[x].VolTag));

  if (ESerror != 0 && InitStatus == 1)
    {
      if (GenericResetStatus(DeviceFD) != 0)
        {
          ElementStatusValid = 0;
          free(DataBuffer);
          return(-1);
        }
      if (GenericElementStatus(DeviceFD, 0) != 0)
        {
          ElementStatusValid = 0;
          free(DataBuffer);
          return(-1);
        }
    } 

  ElementStatusValid = 1;
  free(DataBuffer);
  return(0);
}

/* */
int SDXElementStatus(int DeviceFD, int InitStatus)
{
	int count;
	int SDXerror;
	int retry = 2;

	dbprintf(("##### START SDXElementStatus\n"));
	while (retry)
	{
		GenericElementStatus(DeviceFD, InitStatus);

		SDXerror=0;
		for (count=0; count < STE ; count++)
		{
			if (pSTE[count].except != 0)
			{
				SDXerror = 1;
			}
		}

		if (SDXerror == 1)
		{
			dbprintf(("##### GenericResetStatus\n"));
			GenericResetStatus(DeviceFD);
		}
		retry--;
	}

	if (SDXerror != 0)
	{
		dbprintf(("##### STOP SDXElementStatus (-1)\n"));
		return(-1);
	}
	TapeStatus();
	dbprintf(("##### STOP SDXElementStatus (0)\n"));
	return(0);
}

int GenericElementStatus(int DeviceFD, int InitStatus)
{
  unsigned char *DataBuffer = NULL;
  int DataBufferLength;
  ElementStatusData_T *ElementStatusData;
  ElementStatusPage_T *ElementStatusPage;
  MediumTransportElementDescriptor_T *MediumTransportElementDescriptor;
  StorageElementDescriptor_T *StorageElementDescriptor;
  DataTransferElementDescriptor_T *DataTransferElementDescriptor;
  ImportExportElementDescriptor_T *ImportExportElementDescriptor;

  int ESerror = 0;                /* Is set if ASC for an element is set */
  int x = 0;
  int offset = 0;
  int NoOfElements;

  dbprintf(("##### START GenericElementStatus\n"));

  if (pEAAPage == NULL)
    {
      if (pModePage == NULL)
        {
          if ((pModePage = malloc(0xff)) == NULL)
            {
              dbprintf(("GenericElementStatus : malloc failed\n"));
              return(-1);
            }
        }
      if (SCSI_ModeSense(DeviceFD, pModePage, 0xff, 0x8, 0x3f) == 0)
        DecodeModeSense(pModePage, 0, "GenericElementStatus :", 0, debug_file);
    }
  /* If the MODE_SENSE was successfull we use this Information to read the Elelement Info */
  if (pEAAPage != NULL)
    {
      /* First the Medim Transport*/
      if (V2(pEAAPage->NoMediumTransportElements)  > 0)
        {
          MTE = V2(pEAAPage->NoMediumTransportElements) ;
          if (pMTE == NULL)
            {
              if ((pMTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * MTE)) == NULL)
                {
                  dbprintf(("GenericElementStatus : malloc failed\n"));
                  return(-1);
                }
            }
          memset(pMTE, 0, sizeof(ElementInfo_T) * MTE);

          if (SCSI_ReadElementStatus(DeviceFD, 
                                     CHANGER, 
                                     0,
                                     BarCode(DeviceFD),
                                     V2(pEAAPage->MediumTransportElementAddress),
                                     MTE,
                                     (char **)&DataBuffer) != 0)
            {
              if (DataBuffer != 0)
              {
                free(DataBuffer);
              }
              ChgExit("genericElementStatus","Can't read MTE status", FATAL);
            }
          ElementStatusData = (ElementStatusData_T *)DataBuffer;
          offset = sizeof(ElementStatusData_T);
          
          ElementStatusPage = (ElementStatusPage_T *)&DataBuffer[offset];
          offset = offset + sizeof(ElementStatusPage_T);
          
          for (x = 0; x < MTE; x++)
            {
              MediumTransportElementDescriptor = (MediumTransportElementDescriptor_T *)&DataBuffer[offset];
              if (ElementStatusPage->pvoltag == 1)
                {
                  strncpy(pMTE[x].VolTag, 
                          MediumTransportElementDescriptor->res4,
                          TAG_SIZE);
                  TerminateString(pMTE[x].VolTag, TAG_SIZE+1);
                }
              pMTE[x].type = ElementStatusPage->type;
              pMTE[x].address = V2(MediumTransportElementDescriptor->address);
              pMTE[x].except = MediumTransportElementDescriptor->except;
              pMTE[x].ASC = MediumTransportElementDescriptor->asc;
              pMTE[x].ASCQ = MediumTransportElementDescriptor->ascq;
              pMTE[x].status = (MediumTransportElementDescriptor->full > 0) ? 'F':'E';
              pMTE[x].full = MediumTransportElementDescriptor->full;

              if (MediumTransportElementDescriptor->svalid == 1)
                {
                  pMTE[x].from = V2(MediumTransportElementDescriptor->source);
                } else {
                  pMTE[x].from = -1;
                }
              
              if (pMTE[x].ASC > 0)
                {
                  switch(SenseHandler(DeviceFD, 1, (char *)&pMTE[x]))
                    {
                    case SENSE_IES:
                      ESerror=1;
                      break;
                    case SENSE_ABORT:
                      return(-1);
                      break;
                    }
                }
              offset = offset + V2(ElementStatusPage->length); 
            }
        }
      /* Storage Elements */
      if ( V2(pEAAPage->NoStorageElements)  > 0)
        {
          STE = V2(pEAAPage->NoStorageElements);
          if (pSTE == NULL)
            {
              if ((pSTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * STE)) == NULL)
                {
                  dbprintf(("GenericElementStatus : malloc failed\n"));
                  return(-1);
                }
            }
          memset(pSTE, 0, sizeof(ElementInfo_T) * STE);
          
          if (SCSI_ReadElementStatus(DeviceFD, 
                                     STORAGE, 
                                     0,
                                     BarCode(DeviceFD),
                                     V2(pEAAPage->FirstStorageElementAddress),
                                     STE,
                                     (char **)&DataBuffer) != 0)
            {
              if (DataBuffer != 0)
              {
                free(DataBuffer);
              }
              ChgExit("GenericElementStatus", "Can't read STE status", FATAL);
            }
          
          ElementStatusData = (ElementStatusData_T *)DataBuffer;
          offset = sizeof(ElementStatusData_T);
          
          ElementStatusPage = (ElementStatusPage_T *)&DataBuffer[offset];
          offset = offset + sizeof(ElementStatusPage_T);
          
          for (x = 0; x < STE; x++)
            {
              StorageElementDescriptor = (StorageElementDescriptor_T *)&DataBuffer[offset];
              if (ElementStatusPage->pvoltag == 1)
                {
                  strncpy(pSTE[x].VolTag, 
                          StorageElementDescriptor->res4,
                          TAG_SIZE);
                  TerminateString(pSTE[x].VolTag, TAG_SIZE+1);
                }
              
              
              pSTE[x].type = ElementStatusPage->type;
              pSTE[x].address = V2(StorageElementDescriptor->address);
              pSTE[x].except = StorageElementDescriptor->except;
              pSTE[x].ASC = StorageElementDescriptor->asc;
              pSTE[x].ASCQ = StorageElementDescriptor->ascq;
              pSTE[x].status = (StorageElementDescriptor->full > 0) ? 'F':'E';
              pSTE[x].full = StorageElementDescriptor->full;
              
              if (StorageElementDescriptor->svalid == 1)
                {
                  pSTE[x].from = V2(StorageElementDescriptor->source);
                } else {
                  pSTE[x].from = -1;
                }
              
              if (pSTE[x].ASC > 0)
                {
                  switch(SenseHandler(DeviceFD, 1, (char *)&pSTE[x]))
                    {
                    case SENSE_IES:
                      ESerror=1;
                      break;
                    case SENSE_ABORT:
                      return(-1);
                      break;
                    }
                }
              
              offset = offset + V2(ElementStatusPage->length); 
            }
          
        }
      /* Import/Export Elements */
      if ( V2(pEAAPage->NoImportExportElements) > 0)
        {
          IEE = V2(pEAAPage->NoImportExportElements);
          if (pIEE == NULL)
            {
              if ((pIEE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * IEE)) == NULL)
                {
                  dbprintf(("GenericElementStatus : malloc failed\n"));
                  return(-1);
                }
            }
          memset(pIEE, 0, sizeof(ElementInfo_T) * IEE);
          if (SCSI_ReadElementStatus(DeviceFD, 
                                     IMPORT, 
                                     0,
                                     BarCode(DeviceFD),
                                     V2(pEAAPage->FirstImportExportElementAddress),
                                     IEE,
                                     (char **)&DataBuffer) != 0)
            {
              if (DataBuffer != 0)
              {
                free(DataBuffer);
              }
              ChgExit("GenericElementStatus", "Can't read IEE status", FATAL);
            }
          
          ElementStatusData = (ElementStatusData_T *)DataBuffer;
          offset = sizeof(ElementStatusData_T);
          
          ElementStatusPage = (ElementStatusPage_T *)&DataBuffer[offset];
          offset = offset + sizeof(ElementStatusPage_T);
          
          for (x = 0; x < IEE; x++)
            {
              ImportExportElementDescriptor = (ImportExportElementDescriptor_T *)&DataBuffer[offset];
              if (ElementStatusPage->pvoltag == 1)
                {
                  strncpy(pIEE[x].VolTag, 
                          ImportExportElementDescriptor->res4,
                          TAG_SIZE);
                  TerminateString(pIEE[x].VolTag, TAG_SIZE+1);
                }
              pIEE[x].type = ElementStatusPage->type;
              pIEE[x].address = V2(ImportExportElementDescriptor->address);
              pIEE[x].except = ImportExportElementDescriptor->except;
              pIEE[x].ASC = ImportExportElementDescriptor->asc;
              pIEE[x].ASCQ = ImportExportElementDescriptor->ascq;
              pIEE[x].status = (ImportExportElementDescriptor->full > 0) ? 'F':'E';
              pIEE[x].full = ImportExportElementDescriptor->full;
              
              if (ImportExportElementDescriptor->svalid == 1)
                {
                  pIEE[x].from = V2(ImportExportElementDescriptor->source);
                } else {
                  pIEE[x].from = -1;
                }
              
              if (pIEE[x].ASC > 0)
                {
                  switch(SenseHandler(DeviceFD, 1, (char *)&pIEE[x]))
                    {
                    case SENSE_IES:
                      ESerror=1;
                      break;
                    case SENSE_ABORT:
                      return(-1);
                      break;
                    }
                }
              
              offset = offset + V2(ElementStatusPage->length); 
            }
          
        }
      /* Data Transfer Elements*/
      if (V2(pEAAPage->NoDataTransferElements) >0)
        {
          DTE = V2(pEAAPage->NoDataTransferElements) ;
          if (pDTE == NULL)
            {
              if ((pDTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * DTE)) == NULL)
                {
                  dbprintf(("GenericElementStatus : malloc failed\n"));
                  return(-1);
                }
            }
          memset(pDTE, 0, sizeof(ElementInfo_T) * DTE);
          if (SCSI_ReadElementStatus(DeviceFD, 
                                     TAPETYPE, 
                                     0,
                                     BarCode(DeviceFD),
                                     V2(pEAAPage->FirstDataTransferElementAddress),
                                     DTE,
                                     (char **)&DataBuffer) != 0)
            {
              if (DataBuffer != 0)
              {
                free(DataBuffer);
              }
              ChgExit("GenericElementStatus", "Can't read DTE status", FATAL);
            }
          
          ElementStatusData = (ElementStatusData_T *)DataBuffer;
          offset = sizeof(ElementStatusData_T);
          
          ElementStatusPage = (ElementStatusPage_T *)&DataBuffer[offset];
          offset = offset + sizeof(ElementStatusPage_T);

          for (x = 0; x < DTE; x++)
            {
              DataTransferElementDescriptor = (DataTransferElementDescriptor_T *)&DataBuffer[offset];
              if (ElementStatusPage->pvoltag == 1)
                {
                  strncpy(pDTE[x].VolTag, 
                          DataTransferElementDescriptor->res4,
                          TAG_SIZE);
                  TerminateString(pDTE[x].VolTag, TAG_SIZE+1);
                }
              pDTE[x].type = ElementStatusPage->type;               pDTE[x].address = V2(DataTransferElementDescriptor->address);
              pDTE[x].except = DataTransferElementDescriptor->except;
              pDTE[x].ASC = DataTransferElementDescriptor->asc;
              pDTE[x].ASCQ = DataTransferElementDescriptor->ascq;
              pDTE[x].scsi = DataTransferElementDescriptor->scsi;
              pDTE[x].status = (DataTransferElementDescriptor->full > 0) ? 'F':'E';
              pDTE[x].full = DataTransferElementDescriptor->full;
              
              if (DataTransferElementDescriptor->svalid == 1)
                {
                  pDTE[x].from = V2(DataTransferElementDescriptor->source);
                } else {
                  pDTE[x].from = -1;
                }
              
              if (pDTE[x].ASC > 0)
                {
                  switch(SenseHandler(DeviceFD, 1, (char *)&pDTE[x]))
                    {
                    case SENSE_IES:
                      ESerror=1;
                      break;
                    case SENSE_ABORT:
                      return(-1);
                      break;
                    }
                }
              
              offset = offset + V2(ElementStatusPage->length); 
            }
        }
    } else {  
      if (SCSI_ReadElementStatus(DeviceFD, 
                                 0, 
                                 0,
                                 BarCode(DeviceFD),
                                 0,
                                 0xffff,
                                 (char **)&DataBuffer) != 0)
        {
              if (DataBuffer != 0)
              {
                free(DataBuffer);
              }
          ChgExit("GenericElementStatus","Can't get ElementStatus", FATAL);
        }
      
      ElementStatusData = (ElementStatusData_T *)DataBuffer;
      DataBufferLength = V3(ElementStatusData->count);
      
      offset = sizeof(ElementStatusData_T);
      
      if (DataBufferLength <= 0) 
        {
          dbprintf(("DataBufferLength %d\n",DataBufferLength));
          return(1);
        }
      
      while (offset < DataBufferLength) 
        {
          ElementStatusPage = (ElementStatusPage_T *)&DataBuffer[offset];
          NoOfElements = V3(ElementStatusPage->count) / V2(ElementStatusPage->length);
          offset = offset + sizeof(ElementStatusPage_T);
          
          switch (ElementStatusPage->type)
            {
            case 1:
              MTE = NoOfElements;
              if ((pMTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * MTE)) == NULL)
                {
                  dbprintf(("GenericElementStatus : malloc failed\n"));
                  return(-1);
                }
              memset(pMTE, 0, sizeof(ElementInfo_T) * MTE);
              for (x = 0; x < NoOfElements; x++)
                {
                  /*               dbprintf(("PVolTag %d, AVolTag %d\n", */
                  /*                         ElementStatusPage->pvoltag, */
                  /*                         ElementStatusPage->avoltag)); */
                  
                  MediumTransportElementDescriptor = (MediumTransportElementDescriptor_T *)&DataBuffer[offset];
                  if (ElementStatusPage->pvoltag == 1)
                    {
                      strncpy(pMTE[x].VolTag, 
                              MediumTransportElementDescriptor->res4,
                              TAG_SIZE);
                      TerminateString(pMTE[x].VolTag, TAG_SIZE+1);
                    }
                  pMTE[x].type = ElementStatusPage->type;
                  pMTE[x].address = V2(MediumTransportElementDescriptor->address);
                  pMTE[x].except = MediumTransportElementDescriptor->except;
                  pMTE[x].ASC = MediumTransportElementDescriptor->asc;
                  pMTE[x].ASCQ = MediumTransportElementDescriptor->ascq;
                  pMTE[x].status = (MediumTransportElementDescriptor->full > 0) ? 'F':'E';
                  pMTE[x].full = MediumTransportElementDescriptor->full;
                  
                  if (MediumTransportElementDescriptor->svalid == 1)
                    {
                      pMTE[x].from = V2(MediumTransportElementDescriptor->source);
                    } else {
                      pMTE[x].from = -1;
                    }
                  
                  if (pMTE[x].ASC > 0)
                    {
                      if (SenseHandler(DeviceFD, 1, (char *)&pMTE[x]) == SENSE_RETRY)
                        ESerror = 1;
                    }
                  offset = offset + V2(ElementStatusPage->length); 
                }
              break;
            case 2:
              STE = NoOfElements;
              if ((pSTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * STE)) == NULL)
                {
                  dbprintf(("GenericElementStatus : malloc failed\n"));
                  return(-1);
                }
              memset(pSTE, 0, sizeof(ElementInfo_T) * STE);
              for (x = 0; x < NoOfElements; x++)
                {
                  /*               dbprintf(("PVolTag %d, AVolTag %d\n", */
                  /*                         ElementStatusPage->pvoltag, */
                  /*                         ElementStatusPage->avoltag)); */
                  
                  StorageElementDescriptor = (StorageElementDescriptor_T *)&DataBuffer[offset];
                  if (ElementStatusPage->pvoltag == 1)
                    {
                      strncpy(pSTE[x].VolTag, 
                              StorageElementDescriptor->res4,
                              TAG_SIZE);
                      TerminateString(pSTE[x].VolTag, TAG_SIZE+1);
                    }
                  
                  pSTE[x].type = ElementStatusPage->type;
                  pSTE[x].address = V2(StorageElementDescriptor->address);
                  pSTE[x].except = StorageElementDescriptor->except;
                  pSTE[x].ASC = StorageElementDescriptor->asc;
                  pSTE[x].ASCQ = StorageElementDescriptor->ascq;
                  pSTE[x].status = (StorageElementDescriptor->full > 0) ? 'F':'E';
                  pSTE[x].full = StorageElementDescriptor->full;
                            
                  if (StorageElementDescriptor->svalid == 1)
                    {
                      pSTE[x].from = V2(StorageElementDescriptor->source);
                    } else {
                      pSTE[x].from = -1;
                    }
                            
                  if (pSTE[x].ASC > 0)
                    {
                      if (SenseHandler(DeviceFD, 1, (char *)&pSTE[x]) == SENSE_RETRY)
                        ESerror = 1;
                    }
                            
                  offset = offset + V2(ElementStatusPage->length); 
                }
              break;
            case 3:
              IEE = NoOfElements;
              if ((pIEE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * IEE)) == NULL)
                {
                  dbprintf(("GenericElementStatus : malloc failed\n"));
                  return(-1);
                }
              memset(pIEE, 0, sizeof(ElementInfo_T) * IEE);
              
              for (x = 0; x < NoOfElements; x++)
                {
                  ImportExportElementDescriptor = (ImportExportElementDescriptor_T *)&DataBuffer[offset];
                  if (ElementStatusPage->pvoltag == 1)
                    {
                      strncpy(pIEE[x].VolTag, 
                              ImportExportElementDescriptor->res4,
                              TAG_SIZE);
                      TerminateString(pIEE[x].VolTag, TAG_SIZE+1);
                    }
                  ImportExportElementDescriptor = (ImportExportElementDescriptor_T *)&DataBuffer[offset];
                  pIEE[x].type = ElementStatusPage->type;
                  pIEE[x].address = V2(ImportExportElementDescriptor->address);
                  pIEE[x].except = ImportExportElementDescriptor->except;
                  pIEE[x].ASC = ImportExportElementDescriptor->asc;
                  pIEE[x].ASCQ = ImportExportElementDescriptor->ascq;
                  pIEE[x].status = (ImportExportElementDescriptor->full > 0) ? 'F':'E';
                  pIEE[x].full = ImportExportElementDescriptor->full;
                  
                  if (ImportExportElementDescriptor->svalid == 1)
                    {
                      pIEE[x].from = V2(ImportExportElementDescriptor->source);
                    } else {
                      pIEE[x].from = -1;
                    }
                  
                  if (pIEE[x].ASC > 0)
                    {
                      if (SenseHandler(DeviceFD, 1, (char *)&pIEE[x]) == SENSE_RETRY)
                        ESerror = 1;
                    }
                  
                  offset = offset + V2(ElementStatusPage->length); 
                }
              break;
            case 4:
              DTE = NoOfElements;
              if ((pDTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * DTE)) == NULL)
                {
                  dbprintf(("GenericElementStatus : malloc failed\n"));
                  return(-1);
                }
              memset(pDTE, 0, sizeof(ElementInfo_T) * DTE);
                        
              for (x = 0; x < NoOfElements; x++)
                {
                  /*               dbprintf(("PVolTag %d, AVolTag %d\n", */
                  /*                         ElementStatusPage->pvoltag, */
                  /*                         ElementStatusPage->avoltag)); */
                            
                  DataTransferElementDescriptor = (DataTransferElementDescriptor_T *)&DataBuffer[offset];
                  if (ElementStatusPage->pvoltag == 1)
                    {
                      strncpy(pSTE[x].VolTag, 
                              DataTransferElementDescriptor->res4,
                              TAG_SIZE);
                      TerminateString(pSTE[x].VolTag, TAG_SIZE+1);
                    }
                  pDTE[x].type = ElementStatusPage->type;
                  pDTE[x].address = V2(DataTransferElementDescriptor->address);
                  pDTE[x].except = DataTransferElementDescriptor->except;
                  pDTE[x].ASC = DataTransferElementDescriptor->asc;
                  pDTE[x].ASCQ = DataTransferElementDescriptor->ascq;
                  pDTE[x].scsi = DataTransferElementDescriptor->scsi;
                  pDTE[x].status = (DataTransferElementDescriptor->full > 0) ? 'F':'E';
                  pDTE[x].full = DataTransferElementDescriptor->full;
                            
                  if (DataTransferElementDescriptor->svalid == 1)
                    {
                      pDTE[x].from = V2(DataTransferElementDescriptor->source);
                    } else {
                      pDTE[x].from = -1;
                    }
                            
                  if (pDTE[x].ASC > 0)
                    {
                      if (SenseHandler(DeviceFD, 1, (char *)&pDTE[x]) == SENSE_RETRY)
                        ESerror = 1;
                    }
                            
                  offset = offset + V2(ElementStatusPage->length); 
                }
              break;
            default:
              offset = offset + V2(ElementStatusPage->length); 
              dbprintf(("ReadElementStatus : UnGknown Type %d\n",ElementStatusPage->type));
              break;
            }
        }
    }

  dbprintf(("\n\n\tMedia Transport Elements (robot arms) :\n"));

  for ( x = 0; x < MTE; x++)
    dbprintf(("\t\tElement #%04d %c\n\t\t\tEXCEPT = %02x\n\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n",
              pMTE[x].address, pMTE[x].status, pMTE[x].except, pMTE[x].ASC,
              pMTE[x].ASCQ, pMTE[x].type, pMTE[x].from, pMTE[x].VolTag));

  dbprintf(("\n\n\tStorage Elements (Media slots) :\n"));

  for ( x = 0; x < STE; x++)
    dbprintf(("\t\tElement #%04d %c\n\t\t\tEXCEPT = %02X\n\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n",
              pSTE[x].address, pSTE[x].status, pSTE[x].except, pSTE[x].ASC,
              pSTE[x].ASCQ, pSTE[x].type, pSTE[x].from, pSTE[x].VolTag));

  dbprintf(("\n\n\tData Transfer Elements (tape drives) :\n"));
 
  for ( x = 0; x < DTE; x++)
    dbprintf(("\t\tElement #%04d %c\n\t\t\tEXCEPT = %02X\n\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n\t\t\tSCSI ADDRESS = %d\n",
              pDTE[x].address, pDTE[x].status, pDTE[x].except, pDTE[x].ASC,
              pDTE[x].ASCQ, pDTE[x].type, pDTE[x].from, pDTE[x].VolTag,pDTE[x].scsi));

  dbprintf(("\n\n\tImport/Export Elements  :\n"));

  for ( x = 0; x < IEE; x++)
    dbprintf(("\t\tElement #%04d %c\n\t\t\tEXCEPT = %02X\n\t\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n",
              pIEE[x].address, pIEE[x].status, pIEE[x].except, pIEE[x].ASC,
              pIEE[x].ASCQ, pIEE[x].type, pIEE[x].from, pIEE[x].VolTag));

  if (ESerror != 0 && InitStatus == 1)
    {
      if (GenericResetStatus(DeviceFD) != 0)
        {
          ElementStatusValid = 0;
          free(DataBuffer);
          return(-1);
        }
      if (GenericElementStatus(DeviceFD, 0) != 0)
        {
          ElementStatusValid = 0;
          free(DataBuffer);
          return(-1);
        }
    } 

  ElementStatusValid = 1;
  free(DataBuffer);
  return(0);
}


int RequestSense(int DeviceFD, ExtendedRequestSense_T *ExtendedRequestSense, int ClearErrorCounters )
{
  CDB_T CDB;
  RequestSense_T *pRequestSense;
  int ret;
  
  dbprintf(("##### START RequestSense\n"));

  if ((pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T))) == NULL)
      {
        dbprintf(("RequestSense : malloc failed\n"));
        return(-1);
      }

  CDB[0] = SC_COM_REQUEST_SENSE; /* REQUEST SENSE */                       
  CDB[1] = 0;                   /* Logical Unit Number = 0, Reserved */ 
  CDB[2] = 0;                   /* Reserved */              
  CDB[3] = 0;                   /* Reserved */
  CDB[4] = 0x1D;                /* Allocation Length */                    
  CDB[5] = (ClearErrorCounters << 7) & 0x80;                 /*  */
  
  memset(ExtendedRequestSense, 0, sizeof(ExtendedRequestSense_T));
  
  ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 6,                      
                            (char *) ExtendedRequestSense,
                            sizeof(ExtendedRequestSense_T),  
                            (char *) pRequestSense, sizeof(RequestSense_T));
  
  
  if (ret < 0)
    {
      return(ret);
    }
  
  if ( ret > 0)
    {
      DecodeExtSense(ExtendedRequestSense, "RequestSense : ",debug_file);
      return(pRequestSense->SenseKey);
    }
  return(0);
}


/*
 * Lookup function pointer for device ....
 */


ElementInfo_T *LookupElement(int address)
{
  int x;

  dbprintf(("##### START LookupElement\n"));

  if (DTE > 0)
    {
      for (x = 0; x < DTE; x++)
        {
          if (pDTE[x].address == address)
	  {
            dbprintf(("##### STOP LookupElement (DTE)\n"));
            return(&pDTE[x]);
	  }
        }
    }

  if (MTE > 0)
    {
      for (x = 0; x < MTE; x++)
        {
          if (pMTE[x].address == address)
	  {
            dbprintf(("##### STOP LookupElement (MTE)\n"));
            return(&pMTE[x]);
	  }
        }
    }

  if (STE > 0)
    {
      for (x = 0; x < STE; x++)
        {
          if (pSTE[x].address == address)
	  {
            dbprintf(("##### STOP LookupElement (STE)\n"));
            return(&pSTE[x]);
	  }
        }
    }

  if (IEE > 0)
    {
      for ( x = 0; x < IEE; x++)
        {
          if (pIEE[x].address == address)
	  {
            dbprintf(("##### STOP LookupElement (IEE)\n"));
            return(&pIEE[x]);
	  }
        }
    }
  return(NULL);
}
/*
 * Here comes everything what decode the log Pages
 */

int LogSense(DeviceFD)
{
  extern OpenFiles_T *pDev;
  CDB_T CDB;
  RequestSense_T *pRequestSense;
  LogSenseHeader_T *LogSenseHeader;
  LogParameter_T *LogParameter;
  struct LogPageDecode *p;
  int found;
  char *datestamp = NULL;
  char *label = NULL;
  char *result = NULL;
  extern char *tapestatfile;
  
  int i;
  int ParameterCode;
  unsigned int value;
  int length;
  int count;
  char *buffer;
  char *logpages;
  int nologpages;
  int size = 2048;

  dbprintf(("##### START LogSense\n"));

  if (tapestatfile != NULL && 
      (StatFile = fopen(tapestatfile,"a")) != NULL &&
      pDev[INDEX_TAPECTL].SCSI == 1) 
    {
      if ((pRequestSense  = (RequestSense_T *)malloc(sizeof(RequestSense_T))) == NULL)
        {
          dbprintf(("LogSense : malloc failed\n"));
              return(-1);
        }
      
      if (SCSI_TestUnitReady(INDEX_TAPECTL, pRequestSense) == 0)
        {
          DecodeSense(pRequestSense, "LogSense :", debug_file);
          dbprintf(("LogSense : Tape_Ready failed\n"));
          free(pRequestSense);
          return(0);
        }
      if (GenericRewind(2) < 0) 
        { 
          dbprintf(("LogSense : Rewind failed\n"));
          free(pRequestSense);
          return(0); 
        } 
      /*
       * Try to read the tape label
       */
      if (pDev[INDEX_TAPE].inqdone == 1)
        {
	  if (pDev[INDEX_TAPE].devopen == 0)
	    {
	      SCSI_OpenDevice(INDEX_TAPE);
	    }
	  if ((result = (char *)tapefd_rdlabel(pDev[INDEX_TAPE].fd, &datestamp, &label)) == NULL)
            {
              fprintf(StatFile, "==== %s ==== %s ====\n", datestamp, label);
            } else {
              fprintf(StatFile, "%s\n", result);
            }
	  SCSI_CloseDevice(INDEX_TAPE);
        }
      if ((buffer = (char *)malloc(size)) == NULL)
        {
          dbprintf(("LogSense : malloc failed\n"));
          return(-1);
        }
      memset(buffer, 0, size);
      /*
       * Get the known log pages 
       */
      
      CDB[0] = SC_COM_LOG_SENSE;
      CDB[1] = 0;
      CDB[2] = 0x40;    /* 0x40 for current values */
      CDB[3] = 0;
      CDB[4] = 0;
      CDB[5] = 0;
      CDB[6] = 00;
      MSB2(&CDB[7], size);
      CDB[9] = 0;
      
      
      dbprintf(("LogSense\n"));
      
      if (SCSI_ExecuteCommand(DeviceFD, Input, CDB, 10,
                              buffer,
                              size, 
                              (char *)pRequestSense,
                              sizeof(RequestSense_T)) != 0)
        {
          DecodeSense(pRequestSense, "LogSense : ",debug_file);
          free(buffer);
          free(pRequestSense);
          return(0);
        }
      
      LogSenseHeader = (LogSenseHeader_T *)buffer;
      nologpages = V2(LogSenseHeader->PageLength);
      if ((logpages = (char *)malloc(nologpages)) == NULL)
        {
          dbprintf(("LogSense : malloc failed\n"));
          return(-1);
        }
      
      memcpy(logpages, buffer + sizeof(LogSenseHeader_T), nologpages);
      
      for (count = 0; count < nologpages; count++) {
        if (logpages[count] != 0  ) {
          memset(buffer, 0, size);
          
          
          CDB[0] = SC_COM_LOG_SENSE;
          CDB[1] = 0;
          CDB[2] = 0x40 | logpages[count];    /* 0x40 for current values */
          CDB[3] = 0;
          CDB[4] = 0;
          CDB[5] = 0;
          CDB[6] = 00;
          MSB2(&CDB[7], size);
          CDB[9] = 0;
      
          if (SCSI_ExecuteCommand(DeviceFD, Input, CDB, 10,
                                  buffer,
                                  size, 
                                  (char *)pRequestSense,
                                  sizeof(RequestSense_T)) != 0)
            { 
              DecodeSense(pRequestSense, "LogSense : ",debug_file);
              free(buffer);
              free(pRequestSense);
              return(0);
            }
          LogSenseHeader = (LogSenseHeader_T *)buffer;
          length = V2(LogSenseHeader->PageLength);
          LogParameter = (LogParameter_T *)(buffer + sizeof(LogSenseHeader_T));
          /*
           * Decode the log pages
           */
          p = (struct LogPageDecode *)&DecodePages;
          found = 0;

          while(p->ident != NULL) {
            if ((strcmp(pDev[INDEX_TAPECTL].ident, p->ident) == 0 ||strcmp("*", p->ident) == 0)  && p->LogPage == logpages[count]) {
              p->decode(LogParameter, length);
              found = 1;
              fprintf(StatFile, "\n");
              break;
            }
            p++;
          }

          if (!found && 0 == 1) {
            fprintf(StatFile, "Logpage No %d = %x\n", count ,logpages[count]);
      
            while ((char *)LogParameter < (buffer + length)) {
              i = LogParameter->ParameterLength;
              ParameterCode = V2(LogParameter->ParameterCode);
              switch (i) {
              case 1:
                value = V1((char *)LogParameter + sizeof(LogParameter_T));
                fprintf(StatFile, "ParameterCode %02X = %u(%d)\n", ParameterCode, value, i);
                break;
              case 2:
                value = V2((char *)LogParameter + sizeof(LogParameter_T));
                fprintf(StatFile, "ParameterCode %02X = %u(%d)\n", ParameterCode, value, i);
                break;
              case 3:
                value = V3((char *)LogParameter + sizeof(LogParameter_T));
                fprintf(StatFile, "ParameterCode %02X = %u(%d)\n", ParameterCode, value, i);
                break;
              case 4:
                value = V4((char *)LogParameter + sizeof(LogParameter_T));
                fprintf(StatFile, "ParameterCode %02X = %u(%d)\n", ParameterCode, value, i);
                break;
              case 5:
                value = V5((char *)LogParameter + sizeof(LogParameter_T));
                fprintf(StatFile, "ParameterCode %02X = %u(%d)\n", ParameterCode, value, i);
                break;
              default:
                fprintf(StatFile, "ParameterCode %02X size %d\n", ParameterCode, i);
              }
              LogParameter = (LogParameter_T *)((char *)LogParameter +  sizeof(LogParameter_T) + i);
            }
            fprintf(StatFile, "\n");
          }
        }
      }
      free(pRequestSense);
      free(buffer);
    }
    return(0);
}

void WriteErrorCountersPage(LogParameter_T *buffer, int length)
{
  int i;
  int value;
  LogParameter_T *LogParameter;
  int ParameterCode;
  LogParameter = buffer;

  fprintf(StatFile, "\tWrite Error Counters Page\n");

  while ((char *)LogParameter < ((char *)buffer + length)) {
    i = LogParameter->ParameterLength;
    ParameterCode = V2(LogParameter->ParameterCode);

    if (Decode(LogParameter, &value) == 0) {
      switch (ParameterCode) {
      case 2:
        fprintf(StatFile, "%-30s = %u\n",
                "Total Rewrites",
                value);
        break;
      case 3:
        fprintf(StatFile, "%-30s = %u\n",
                "Total Errors Corrected",
                value);
        break;
      case 4:
        fprintf(StatFile, "%-30s = %u\n",
                "Total Times E. Processed",
                value);
        break;
      case 5:
        fprintf(StatFile, "%-30s = %u\n",
                "Total Bytes Processed",
                value);
        break;
      case 6:
        fprintf(StatFile, "%-30s = %u\n",
                "Total Unrecoverable Errors",
                value);
        break;
      default:
        fprintf(StatFile, "Unknown ParameterCode %02X = %u(%d)\n", 
                ParameterCode,
                value, i);
        break;
      }
    } else {
      fprintf(StatFile, "Error decoding Result\n");
    }
    LogParameter = (LogParameter_T *)((char *)LogParameter +  sizeof(LogParameter_T) + i);
  }
}

void ReadErrorCountersPage(LogParameter_T *buffer, int length)
{
  int i;
  int value;
  LogParameter_T *LogParameter;
  int ParameterCode;
  LogParameter = buffer;

  fprintf(StatFile, "\tRead Error Counters Page\n");

  while ((char *)LogParameter < ((char *)buffer + length)) {
    i = LogParameter->ParameterLength;
    ParameterCode = V2(LogParameter->ParameterCode);

    if (Decode(LogParameter, &value) == 0) {
      switch (ParameterCode) {
      case 2:
        fprintf(StatFile, "%-30s = %u\n",
                "Total Rereads",
                value);
        break;
      case 3:
        fprintf(StatFile, "%-30s = %u\n",
                "Total Errors Corrected",
                value);
        break;
      case 4:
        fprintf(StatFile, "%-30s = %u\n",
                "Total Times E. Processed",
                value);
        break;
      case 5:
        fprintf(StatFile, "%-30s = %u\n",
                "Total Bytes Processed",
                value);
        break;
      case 6:
        fprintf(StatFile, "%-30s = %u\n",
                "Total Unrecoverable Errors",
                value);
        break;
      default:
        fprintf(StatFile, "Unknown ParameterCode %02X = %u(%d)\n", 
                ParameterCode,
                value, i);
        break;
      }
    } else {
      fprintf(StatFile, "Error decoding Result\n");
    }
    LogParameter = (LogParameter_T *)((char *)LogParameter +  sizeof(LogParameter_T) + i);
  }
}

void C1553APage30(LogParameter_T *buffer, int length)
{
  int i;
  int value;
  LogParameter_T *LogParameter;
  int ParameterCode;
  LogParameter = buffer;

  fprintf(StatFile, "\tData compression transfer Page\n");

  while ((char *)LogParameter < ((char *)buffer + length)) {
    i = LogParameter->ParameterLength;
    ParameterCode = V2(LogParameter->ParameterCode);

    if (Decode(LogParameter, &value) == 0) {
      switch (ParameterCode) {
      default:
        fprintf(StatFile, "Unknown ParameterCode %02X = %u(%d)\n", 
                ParameterCode,
                value, i);
        break;
      }
    }
    LogParameter = (LogParameter_T *)((char *)LogParameter +  sizeof(LogParameter_T) + i);
  }
}

void C1553APage37(LogParameter_T *buffer, int length)
{
  int i;
  int value;
  LogParameter_T *LogParameter;
  int ParameterCode;
  LogParameter = buffer;

  fprintf(StatFile, "\tDrive Counters Page\n");

  while ((char *)LogParameter < ((char *)buffer + length)) {
    i = LogParameter->ParameterLength;
    ParameterCode = V2(LogParameter->ParameterCode);

    if (Decode(LogParameter, &value) == 0) {
      switch (ParameterCode) {
      case 1:
        fprintf(StatFile, "%-30s = %u\n",
                "Total loads",
                value);
        break;
      case 2:
        fprintf(StatFile, "%-30s = %u\n",
                "Total write drive errors",
                value);
        break;
      case 3:
        fprintf(StatFile, "%-30s = %u\n",
                "Total read drive errors",
                value);
        break;
      default:
        fprintf(StatFile, "Unknown ParameterCode %02X = %u(%d)\n", 
                ParameterCode,
                value, i);
        break;
      }
    }
    LogParameter = (LogParameter_T *)((char *)LogParameter +  sizeof(LogParameter_T) + i);
  }
}

void EXB85058HEPage39(LogParameter_T *buffer, int length)
{
  int i;
  int value;
  LogParameter_T *LogParameter;
  int ParameterCode;
  LogParameter = buffer;

  fprintf(StatFile, "\tData Compression Page\n");

  while ((char *)LogParameter < ((char *)buffer + length)) {
    i = LogParameter->ParameterLength;
    ParameterCode = V2(LogParameter->ParameterCode);

    if (Decode(LogParameter, &value) == 0) {
      switch (ParameterCode) {
      case 5:
        fprintf(StatFile, "%-30s = %u\n",
                "KB to Compressor",
                value);
        break;
      case 7:
        fprintf(StatFile, "%-30s = %u\n",
                "KB to tape",
                value);
        break;
      default:
        fprintf(StatFile, "Unknown ParameterCode %02X = %u(%d)\n", 
                ParameterCode,
                value, i);
        break;
      }
    }
    LogParameter = (LogParameter_T *)((char *)LogParameter +  sizeof(LogParameter_T) + i); 
  }
}

void EXB85058HEPage3c(LogParameter_T *buffer, int length)
{
  int i;
  int value;
  LogParameter_T *LogParameter;
  int ParameterCode;
  LogParameter = buffer;

  fprintf(StatFile, "\tDrive Usage Information Page\n");

  while ((char *)LogParameter < ((char *)buffer + length)) {
    i = LogParameter->ParameterLength;
    ParameterCode = V2(LogParameter->ParameterCode);

    if (Decode(LogParameter, &value) == 0) {
      switch (ParameterCode) {
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
        break;
      case 6:
        fprintf(StatFile, "%-30s = %u\n",
                "Total Load Count",
                value);
        break;
      case 7:
        fprintf(StatFile, "%-30s = %u\n",
                "MinutesSince Last Clean",
                value);
        break;
      case 8:
      case 9:
        break;
      case 0xa:
        fprintf(StatFile, "%-30s = %u\n",
                "Cleaning Count",
                value);
        break;
      case 0xb:
      case 0xc:
      case 0xd:
      case 0xe:
      case 0xf:
      case 0x10:
        break;
      case 0x11:
        fprintf(StatFile, "%-30s = %u\n",
                "Time to clean",
                value);
        break;
      case 0x12:
      case 0x13:
      case 0x14:
        break;
      default:
        fprintf(StatFile, "Unknown ParameterCode %02X = %u(%d)\n", 
                ParameterCode,
                value, i);
        break;
      }
    }
    LogParameter = (LogParameter_T *)((char *)LogParameter +  sizeof(LogParameter_T) + i); 
  }
}

int Decode(LogParameter_T *LogParameter, int *value)
{

  dbprintf(("##### START Decode\n"));

  switch (LogParameter->ParameterLength) {
  case 1:
    *value = V1((char *)LogParameter + sizeof(LogParameter_T));
    break;
  case 2:
    *value = V2((char *)LogParameter + sizeof(LogParameter_T));
    break;
  case 3:
    *value = V3((char *)LogParameter + sizeof(LogParameter_T));
    break;
  case 4:
    *value = V4((char *)LogParameter + sizeof(LogParameter_T));
    break;
  case 5:
    *value = V5((char *)LogParameter + sizeof(LogParameter_T));
    break;
  case 6:
    *value = V6((char *)LogParameter + sizeof(LogParameter_T));
    break;
  default:
    fprintf(StatFile, "Can't decode ParameterCode %02X size %d\n",
            V2(LogParameter->ParameterCode), LogParameter->ParameterLength);
    return(1);
  }
  return(0);
}

void DumpDev(OpenFiles_T *p, char *device)
{
	if (p != NULL)
	{
		printf("%s Devicefd   %d\n", device, p->fd);
		printf("%s Can SCSI   %d\n", device, p->SCSI);
		printf("%s Device     %s\n", device, p->dev);
		printf("%s ConfigName %s\n", device, p->ConfigName);
	} else {
		printf("%s Null Pointer ....\n", device);
	}
	printf("\n");
}

void ChangerReplay(char *option)
{
    char buffer[1024];
    FILE *ip;
    int x = 0, bufferx;
   
    if ((ip=fopen("/tmp/chg-scsi-trace", "r")) == NULL)
      {
	exit(1);
      }
    
    while (fscanf(ip, "%2x", &bufferx) != EOF)
      {
	buffer[x] = bufferx;
	x++;
      }

    DecodeModeSense(&buffer[0], 12, "DLT448ElementStatus :", 0, debug_file);
}

/*
 * Display all Information we can get about the library....
 */
void ChangerStatus(char *option, char * labelfile, int HasBarCode, char *changer_file, char *changer_dev, char *tape_device)
{
  extern OpenFiles_T *pDev;
  int x;
  FILE *out;
  char *label;
  ExtendedRequestSense_T ExtRequestSense;
  ChangerCMD_T *p = (ChangerCMD_T *)&ChangerIO;


  if ((pModePage = (char *)malloc(0xff)) == NULL)
    {
      printf("malloc failed \n");
      return;
    }

  if ((out = fdopen(1 , "w")) == NULL)
    {
      printf("Error fdopen stdout\n");
      return;
    }
  
  if (strcmp("types", option) == 0 || strcmp("all", option) == 0)
  {
    while(p->ident != NULL)
      {
         printf ("Ident = %s, type = %s\n",p->ident, p->type);
         p++;
      }
    DumpSense();
  }

  if (strcmp("robot", option) == 0 || strcmp("all", option) == 0)
      {
        if (ElementStatusValid == 0)
          {
            if (pDev[INDEX_CHANGER].functions->function_status(pDev[INDEX_CHANGER].fd, 1) != 0)
              {
                printf("Can not initialize changer status\n");
                return;
              }
          }
        /*      0123456789012345678901234567890123456789012 */
	if (HasBarCode)
	{
        	printf("Address Type Status From Barcode Label\n");
	} else {
        	printf("Address Type Status From\n");
	}
        printf("-------------------------------------------\n");
        
        
        for ( x = 0; x < MTE; x++)
	if (HasBarCode)
	{
          printf("%07d MTE  %s  %04d %s ",pMTE[x].address,
                 (pMTE[x].full ? "Full " :"Empty"),
                 pMTE[x].from, pMTE[x].VolTag);
          if ((label = (char *)MapBarCode(labelfile, "", pMTE[x].VolTag, BARCODE_BARCODE, 0, 0)) == NULL)
          { 
		printf("No mapping\n");
          } else {
                printf("%s \n",label);
          }
	} else {
          printf("%07d MTE  %s  %04d \n",pMTE[x].address,
                 (pMTE[x].full ? "Full " :"Empty"),
                 pMTE[x].from);
	}
        
        
        for ( x = 0; x < STE; x++)
	if (HasBarCode)
	{
          printf("%07d STE  %s  %04d %s ",pSTE[x].address,  
                 (pSTE[x].full ? "Full ":"Empty"),
                 pSTE[x].from, pSTE[x].VolTag);
	  if ((label = (char *)MapBarCode(labelfile, "", pSTE[x].VolTag,  BARCODE_BARCODE, 0, 0)) == NULL)
          {
                printf("No mapping\n");
          } else {
                printf("%s \n",label);
          }
	} else {
          printf("%07d STE  %s  %04d %s\n",pSTE[x].address,  
                 (pSTE[x].full ? "Full ":"Empty"),
                 pSTE[x].from, pSTE[x].VolTag);
	}
        
        
        for ( x = 0; x < DTE; x++)
	if (HasBarCode)
	{
          printf("%07d DTE  %s  %04d %s ",pDTE[x].address,  
                 (pDTE[x].full ? "Full " : "Empty"),
                 pDTE[x].from, pDTE[x].VolTag);
	  if (( label = (char *)MapBarCode(labelfile, "", pDTE[x].VolTag,  BARCODE_BARCODE, 0, 0)) == NULL)
          {
                printf("No mapping\n");
          } else {
                printf("%s \n",label);
          }
	} else {
          printf("%07d DTE  %s  %04d %s\n",pDTE[x].address,  
                 (pDTE[x].full ? "Full " : "Empty"),
                 pDTE[x].from, pDTE[x].VolTag);
	}
        
        for ( x = 0; x < IEE; x++)
	if (HasBarCode)
	{	
          printf("%07d IEE  %s  %04d %s ",pIEE[x].address,  
                 (pIEE[x].full ? "Full " : "Empty"),
                 pIEE[x].from, pIEE[x].VolTag);
	  if ((label = (char *)MapBarCode(labelfile, "", pIEE[x].VolTag,  BARCODE_BARCODE, 0, 0)) == NULL)
          {
                printf("No mapping\n");
          } else {
                printf("%s \n",label);
          }
	} else {
          printf("%07d IEE  %s  %04d %s\n",pIEE[x].address,  
                 (pIEE[x].full ? "Full " : "Empty"),
                 pIEE[x].from, pIEE[x].VolTag);
	}
        
      }

  if (strcmp("sense", option) == 0 || strcmp("all", option) == 0)
    {
      printf("\nSense Status from robot:\n");
      RequestSense(INDEX_CHANGER , &ExtRequestSense, 0);
      DecodeExtSense(&ExtRequestSense, "", out);
      
      if (pDev[2].SCSI == 1)
        {
          printf("\n");
          printf("Sense Status from tape:\n");
          RequestSense(2, &ExtRequestSense, 0); 
          DecodeExtSense(&ExtRequestSense, "", out);
        }
    }

    if (strcmp("ModeSenseRobot", option) == 0 || strcmp("all", option) == 0)
      {
        printf("\n");
        if (SCSI_ModeSense(INDEX_CHANGER, pModePage, 0xff, 0x08, 0x3f) == 0)
          {
            DecodeModeSense(pModePage, 0, "Changer :" , 0, out); 
          }
      }
  
    if (strcmp("ModeSenseTape", option) == 0 || strcmp("all", option) == 0)
      {
        if (pDev[INDEX_TAPECTL].SCSI == 1)
        {
          printf("\n");
          if (SCSI_ModeSense(INDEX_TAPECTL, pModePage, 0xff, 0x0, 0x3f) == 0)
            {
              DecodeModeSense(pModePage, 0, "Tape :" , 1, out); 
            }
        }
      }

    if (strcmp("fd", option) == 0 || strcmp("all", option) == 0)
    {
      printf("changer_dev  %s\n",changer_dev);
      printf("changer_file %s\n", changer_file);
      printf("tape_device  %s\n\n", tape_device);
      DumpDev(&pDev[INDEX_TAPE], "pTapeDev");
      DumpDev(&pDev[INDEX_TAPECTL], "pTapeDevCtl");
      DumpDev(&pDev[INDEX_CHANGER], "pChangerDev");
    }

  if (GenericClean("") == 1)
    printf("Tape needs cleaning\n");  
}

void dump_hex(char *p, int size)
{
    int row_count = 0;
    int x = 0;

    while (row_count < size)
    {
        dbprintf(("%02X ", (unsigned char)p[row_count]));
        if (((row_count + 1) % 16) == 0 )
          {
            dbprintf(("   "));
            for (x = 16; x>0;x--)
              {
            if (isalnum((unsigned char)p[row_count - x + 1 ]))
              dbprintf(("%c",(unsigned char)p[row_count - x + 1]));
            else
              dbprintf(("."));
              }
            dbprintf(("\n"));
          }
    	row_count++;
    }
    dbprintf(("\n"));
}

void TerminateString(char *string, int length)
{
  int x;
  
  for (x = length; x >= 0 && !isalnum((int)string[x]); x--)
    string[x] = '\0';
}

void ChgExit(char *where, char *reason, int level)
{
   dbprintf(("ChgExit in %s, reason %s\n", where, reason));
   fprintf(stderr,"%s\n",reason);
   exit(2); 
}

/* OK here starts a new set of functions.
   Every function is for one SCSI command.
   Prefix is SCSI_ and then the SCSI command name
*/

/*                                       */
/* This a vendor specific command !!!!!! */
/* First seen at AIT :-)                 */
/*                                       */
int SCSI_AlignElements(int DeviceFD, int AE_MTE, int AE_DTE, int AE_STE)
{
  RequestSense_T *pRequestSense;
  int retry = 1;
  CDB_T CDB;
  int ret;
  int i;

  dbprintf(("##### START SCSI_AlignElements\n"));

  if ((pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T))) == NULL)
    {
      dbprintf(("SCSI_AlignElements : malloc failed\n"));
      return(-1);
    }

  while (retry > 0 && retry < MAX_RETRIES)
    {
      CDB[0]  = 0xE5;
      CDB[1]  = 0;
      MSB2(&CDB[2],AE_MTE);	/* Which MTE to use, default 0 */
      MSB2(&CDB[4],AE_DTE);	/* Which DTE to use, no range check !! */
      MSB2(&CDB[6],AE_STE);	/* Which STE to use, no range check !! */
      CDB[8]  = 0;
      CDB[9]  = 0;
      CDB[10] = 0;
      CDB[11] = 0;
      
      ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 12,
                                NULL, 0, (char *)pRequestSense, sizeof(RequestSense_T)); 

      dbprintf(("SCSI_AlignElements : SCSI_ExecuteCommand = %d\n", ret));
      DecodeSense(pRequestSense, "SCSI_AlignElements :",debug_file);

      if (ret < 0)
        {
          dbprintf(("%s: Request Sense[Inquiry]: %02X", 
                    "chs", ((unsigned char *) &pRequestSense)[0])); 
          for (i = 1; i < sizeof(RequestSense_T); i++)                
            dbprintf((" %02X", ((unsigned char *) &pRequestSense)[i])); 
          dbprintf(("\n"));    
          return(ret);
        }
      if ( ret > 0)
        {
          switch(SenseHandler(DeviceFD, 0 ,(char *)pRequestSense))
            {
            case SENSE_IGNORE:
              dbprintf(("SCSI_AlignElements : SENSE_IGNORE\n"));
              return(0);
              break;
            case SENSE_RETRY:
              dbprintf(("SCSI_AlignElements : SENSE_RETRY no %d\n", retry));
              break;
            case SENSE_ABORT:
              dbprintf(("SCSI_AlignElements : SENSE_ABORT\n"));
              return(-1);
              break;
            case SENSE_TAPE_NOT_UNLOADED:
              dbprintf(("SCSI_AlignElements : Tape still loaded, eject failed\n"));
              return(-1);
              break;
            default:
              dbprintf(("SCSI_AlignElements : end %d\n", pRequestSense->SenseKey));
              return(pRequestSense->SenseKey);
              break;
            }
        }
      if (ret == 0)
        {
          dbprintf(("SCSI_AlignElements : end %d\n", ret));
          return(ret);
        } 
    }
  return(ret);
}


int SCSI_Move(int DeviceFD, unsigned char chm, int from, int to)
{
  RequestSense_T *pRequestSense;
  int retry = 1;
  CDB_T CDB;
  int ret;
  int i;

  dbprintf(("##### START SCSI_Move\n"));

  if ((pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T))) == NULL)
    {
      dbprintf(("SCSI_Move : malloc failed\n"));
      return(-1);
    }

  while (retry > 0 && retry < MAX_RETRIES)
    {
      CDB[0]  = SC_MOVE_MEDIUM;
      CDB[1]  = 0;
      CDB[2]  = 0;
      CDB[3]  = chm;     /* Address of CHM */
      MSB2(&CDB[4],from);
      MSB2(&CDB[6],to);
      CDB[8]  = 0;
      CDB[9]  = 0;
      CDB[10] = 0;
      CDB[11] = 0;
      
      ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 12,
                                NULL, 0, (char *)pRequestSense, sizeof(RequestSense_T)); 

      dbprintf(("SCSI_Move : SCSI_ExecuteCommand = %d\n", ret));
      DecodeSense(pRequestSense, "SCSI_Move :",debug_file);

      if (ret < 0)
        {
          dbprintf(("%s: Request Sense[Inquiry]: %02X", 
                    "chs", ((unsigned char *) &pRequestSense)[0])); 
          for (i = 1; i < sizeof(RequestSense_T); i++)                
            dbprintf((" %02X", ((unsigned char *) &pRequestSense)[i])); 
          dbprintf(("\n"));    
          return(ret);
        }
      if ( ret > 0)
        {
          switch(SenseHandler(DeviceFD,  0 ,(char *)pRequestSense))
            {
            case SENSE_IGNORE:
              dbprintf(("SCSI_Move : SENSE_IGNORE\n"));
              return(0);
              break;
            case SENSE_RETRY:
              dbprintf(("SCSI_Move : SENSE_RETRY no %d\n", retry));
              break;
            case SENSE_ABORT:
              dbprintf(("SCSI_Move : SENSE_ABORT\n"));
              return(-1);
              break;
            case SENSE_TAPE_NOT_UNLOADED:
              dbprintf(("SCSI_Move : Tape still loaded, eject failed\n"));
              return(-1);
              break;
            default:
              dbprintf(("SCSI_Move : end %d\n", pRequestSense->SenseKey));
              return(pRequestSense->SenseKey);
              break;
            }
        }
      if (ret == 0)
        {
          dbprintf(("SCSI_Move : end %d\n", ret));
          return(ret);
        } 
    }
  return(ret);
}

int SCSI_LoadUnload(int DeviceFD, RequestSense_T *pRequestSense, unsigned char byte1, unsigned char load)
{
  CDB_T CDB;
  int ret;

  dbprintf(("##### START SCSI_LoadUnload\n"));
  
  CDB[0] = SC_COM_UNLOAD;
  CDB[1] = byte1;             
  CDB[2] = 0;
  CDB[3] = 0;
  CDB[4] = load;
  CDB[5] = 0;
  
 
  ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 6,
                            NULL, 0, 
                            (char *) pRequestSense,
                            sizeof(RequestSense_T));
    
  if (ret < 0)
    {
      dbprintf(("SCSI_Unload : failed %d\n", ret));
      return(-1);
    } 

  return(ret);
}

int SCSI_TestUnitReady(int DeviceFD, RequestSense_T *pRequestSense)
{
  CDB_T CDB;

  dbprintf(("##### START SCSI_TestUnitReady\n"));

  CDB[0] = SC_COM_TEST_UNIT_READY;
  CDB[1] = 0;
  CDB[2] = 0;
  CDB[3] = 0;
  CDB[4] = 0;
  CDB[5] = 0;

  SCSI_ExecuteCommand(DeviceFD, Input, CDB, 6,
                      NULL, 0, 
                      (char *) pRequestSense,
                      sizeof(RequestSense_T));

  if (pRequestSense->ErrorCode == 0 && pRequestSense->SenseKey == 0)
    {
      return(1);
    }
  
  return(0);
}


int SCSI_ModeSelect(int DeviceFD, char *buffer, unsigned char length, unsigned char save, unsigned char mode, unsigned char lun)
{
  CDB_T CDB;
  RequestSense_T *pRequestSense;
  int ret;
  int retry = 1;
  char *sendbuf;
  
  dbprintf(("##### START SCSI_ModeSelect\n"));

  dbprintf(("SCSI_ModeSelect start length = %d:\n", length));
  if ((pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T))) == NULL)
      {
          dbprintf(("SCSI_ModeSelect : malloc failed\n"));
          return(-1);
      }

  
  if ((sendbuf = (char *)malloc(length + 4)) == NULL)
    {
      dbprintf(("SCSI_ModeSelect : malloc failed\n"));
      return(-1);
    }

  memset(sendbuf, 0 , length + 4);

  memcpy(&sendbuf[4], buffer, length);
  dump_hex(sendbuf, length+4);

  while (retry > 0 && retry < MAX_RETRIES)
    {
      memset(pRequestSense, 0, sizeof(RequestSense_T));
      
      CDB[0] = SC_COM_MODE_SELECT;
      CDB[1] = ((lun << 5) & 0xF0) | ((mode << 4) & 0x10) | (save & 1);
      CDB[2] = 0;
      CDB[3] = 0;
      CDB[4] = length + 4;
      CDB[5] = 0;
      ret = SCSI_ExecuteCommand(DeviceFD, Output, CDB, 6,                      
                                sendbuf,
                                length + 4, 
                                (char *) pRequestSense,
                                sizeof(RequestSense_T));
      if (ret < 0)
        {
          dbprintf(("SCSI_ModeSelect : ret %d\n", ret));
          return(ret);
        }
      
      if ( ret > 0)
        {
          switch(SenseHandler(DeviceFD, 0 ,(char *)pRequestSense))
            {
            case SENSE_IGNORE:
              dbprintf(("SCSI_ModeSelect : SENSE_IGNORE\n"));
              return(0);
              break;
            case SENSE_RETRY:
              dbprintf(("SCSI_ModeSelect : SENSE_RETRY no %d\n", retry));
              break;
            default:
              dbprintf(("SCSI_ModeSelect : end %d\n", pRequestSense->SenseKey));
              return(pRequestSense->SenseKey);
              break;
            }
        }
      retry++;
      if (ret == 0)
        {
          dbprintf(("SCSI_ModeSelect end: %d\n", ret));
          return(ret);
        }
    }
  dbprintf(("SCSI_ModeSelect end: %d\n", ret));
  return(ret);
}



int SCSI_ModeSense(int DeviceFD, char *buffer, u_char size, u_char byte1, u_char byte2)
{
  CDB_T CDB;
  RequestSense_T *pRequestSense;
  int ret;
  int retry = 1;

  dbprintf(("##### START SCSI_ModeSense\n"));

  dbprintf(("SCSI_ModeSense start length = %d:\n", size));
  if ((pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T))) == NULL)
      {
          dbprintf(("SCSI_ModeSense : malloc failed\n"));
          return(-1);
      }

  while (retry > 0 && retry < MAX_RETRIES)
    {
      memset(pRequestSense, 0, sizeof(RequestSense_T));
      memset(buffer, 0, size);
      
      CDB[0] = SC_COM_MODE_SENSE;
      CDB[1] = byte1;
      CDB[2] = byte2;
      CDB[3] = 0;
      CDB[4] = size;
      CDB[5] = 0;
      ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 6,                      
                                buffer,
                                size, 
                                (char *) pRequestSense,
                                sizeof(RequestSense_T));
      if (ret < 0)
        {
          return(ret);
        }
      
      if ( ret > 0)
        {
          switch(SenseHandler(DeviceFD, 0 ,(char *)pRequestSense))
            {
            case SENSE_IGNORE:
              dbprintf(("SCSI_ModeSense : SENSE_IGNORE\n"));
              return(0);
              break;
            case SENSE_RETRY:
              dbprintf(("SCSI_ModeSense : SENSE_RETRY no %d\n", retry));
              break;
            default:
              dbprintf(("SCSI_ModeSense : end %d\n", pRequestSense->SenseKey));
              return(pRequestSense->SenseKey);
              break;
            }
        }
      retry++;
      if (ret == 0)
        {
          dbprintf(("SCSI_ModeSense end: %d\n", ret));
          return(ret);
        }
    }
  dbprintf(("SCSI_ModeSense end: %d\n", ret));
  return(ret);
}

int SCSI_Inquiry(int DeviceFD, SCSIInquiry_T *buffer, u_char size)
{
  CDB_T CDB;
  RequestSense_T *pRequestSense;
  int i;
  int ret;
  int retry = 1;

  dbprintf(("##### START SCSI_Inquiry\n"));

  dbprintf(("SCSI_Inquiry start length = %d:\n", size));

  if ((pRequestSense = (RequestSense_T *)malloc(size)) == NULL)
      {
          dbprintf(("SCSI_Inquiry : malloc failed\n"));
          return(-1);
      }

  while (retry > 0 && retry < MAX_RETRIES)
    {
      memset(buffer, 0, size);
      CDB[0] = SC_COM_INQUIRY;
      CDB[1] = 0;
      CDB[2] = 0;
      CDB[3] = 0;
      CDB[4] = size;
      CDB[5] = 0;
      
      ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 6,                      
                                buffer,
                                size, 
                                (char *) pRequestSense,
                            sizeof(RequestSense_T));
      if (ret < 0)
        {
          dbprintf(("%s: Request Sense[Inquiry]: %02X",
                    "chs", ((unsigned char *) pRequestSense)[0]));
          for (i = 1; i < sizeof(RequestSense_T); i++)               
            dbprintf((" %02X", ((unsigned char *) pRequestSense)[i]));
          dbprintf(("\n"));   
      dbprintf(("Inquiry end: %d\n", ret));
      return(ret);
        }
      if ( ret > 0)
        {
          switch(SenseHandler(DeviceFD, 0 , (char *)pRequestSense))
            {
            case SENSE_IGNORE:
              dbprintf(("SCSI_Inquiry : SENSE_IGNORE\n"));
              return(0);
              break;
            case SENSE_RETRY:
              dbprintf(("SCSI_Inquiry : SENSE_RETRY no %d\n", retry));
              break;
            default:
              dbprintf(("SCSI_Inquiry : end %d\n", pRequestSense->SenseKey));
              return(pRequestSense->SenseKey);
              break;
            }
        }
      retry++;
      if (ret == 0)
        {
          dump_hex((char *)buffer, size);
          dbprintf(("SCSI_Inquiry : end %d\n", ret));
          return(ret);
        }
    }
  
  dbprintf(("SCSI_Inquiry end: %d\n", ret));
  return(ret);
}

int SCSI_ReadElementStatus(int DeviceFD, 
                           unsigned char type, 
                           unsigned char lun,
                           unsigned char VolTag,
                           int StartAddress,
                           int NoOfElements,
                           char **data)
{
  CDB_T CDB;
  int DataBufferLength;
  ElementStatusData_T *ElementStatusData;
  RequestSense_T *pRequestSense;
  int retry = 1;
  int ret; 
 
  dbprintf(("##### START SCSI_ReadElementStatus\n"));

  if ((pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T))) == NULL)
      {
          dbprintf(("SCSI_ReadElementStatus : malloc failed\n"));
          ChgExit("SCSI_ReadElementStatus","malloc failed", FATAL);
      }

  
  if (*data != NULL)
    {
      *data = realloc(*data, 8);
    } else {
      *data = malloc(8);
    }

  memset(*data, 0, 8);

  VolTag = (VolTag << 4) & 0x10;
  type = type & 0xf;
  lun = (lun << 5) & 0xe0;
 
  /* First try to get the allocation length for a second call
   */

  while (retry > 0 && retry < MAX_RETRIES)
    {
      memset(pRequestSense, 0, sizeof(RequestSense_T) );
      
      CDB[0] = SC_COM_RES;          /* READ ELEMENT STATUS */
      CDB[1] = VolTag | type | lun; /* Element Type Code , VolTag, LUN */
      MSB2(&CDB[2], StartAddress);  /* Starting Element Address */
      MSB2(&CDB[4], NoOfElements);  /* Number Of Element */
      CDB[6] = 0;                             /* Reserved */                          
      MSB3(&CDB[7],8);                   /* Allocation Length */    
      CDB[10] = 0;                           /* Reserved */                           
      CDB[11] = 0;                           /* Control */                             
      
      ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 12,                      
                                *data, 8, 
                                (char *)pRequestSense, sizeof(RequestSense_T));
      
      
      dbprintf(("SCSI_ReadElementStatus : (1) SCSI_ExecuteCommand %d\n", ret));
      if (ret < 0)
        {
          DecodeSense(pRequestSense, "SCSI_ReadElementStatus :",debug_file);
          return(ret);
        }
      if ( ret > 0)
        {
          switch(SenseHandler(DeviceFD, 0 ,(char *)pRequestSense))
            {
            case SENSE_IGNORE:
              dbprintf(("SCSI_ModeSense : SENSE_IGNORE\n"));
              retry = 0;
              break;
            case SENSE_RETRY:
              dbprintf(("SCSI_ModeSense : SENSE_RETRY no %d\n", retry));
              sleep(2);
              break;
            default:
              dbprintf(("SCSI_ModeSense : end %d\n", pRequestSense->SenseKey));
              return(pRequestSense->SenseKey);
              break;
            }
        }
      retry++;
      if (ret == 0)
        {
          retry=0;
        }
    }
  if (retry > 0)
    {
      return(ret);
    }
  
  ElementStatusData = (ElementStatusData_T *)*data;
  DataBufferLength = V3(ElementStatusData->count);
  DataBufferLength = DataBufferLength + 8;
  dbprintf(("SCSI_ReadElementStatus: DataBufferLength %X, ret %d\n",DataBufferLength, ret));

  dump_hex(*data, 8);

  *data = realloc(*data, DataBufferLength);
  memset(*data, 0, DataBufferLength);
  retry = 1;

  while (retry > 0 && retry < MAX_RETRIES)
    {
      memset(pRequestSense, 0, sizeof(RequestSense_T) );
      
      CDB[0] = SC_COM_RES;           /* READ ELEMENT STATUS */
      CDB[1] = VolTag | type | lun;  /* Element Type Code, VolTag, LUN */
      MSB2(&CDB[2], StartAddress);   /* Starting Element Address */
      MSB2(&CDB[4], NoOfElements);   /* Number Of Element */
      CDB[6] = 0;                              /* Reserved */                      
      MSB3(&CDB[7],DataBufferLength);  /* Allocation Length */    
      CDB[10] = 0;                                 /* Reserved */                      
      CDB[11] = 0;                                 /* Control */                       
      
      ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 12,                      
                                *data, DataBufferLength, 
                                (char *)pRequestSense, sizeof(RequestSense_T));
      
      
      dbprintf(("SCSI_ReadElementStatus : (2) SCSI_ExecuteCommand %d\n", ret));
      if (ret < 0)
        {
          DecodeSense(pRequestSense, "SCSI_ReadElementStatus :",debug_file);
          return(ret);
        }
      if ( ret > 0)
        {
          switch(SenseHandler(DeviceFD, 0 ,(char *)pRequestSense))
            {
            case SENSE_IGNORE:
              dbprintf(("SCSI_ModeSense : SENSE_IGNORE\n"));
              retry = 0;
              break;
            case SENSE_RETRY:
              dbprintf(("SCSI_ModeSense : SENSE_RETRY no %d\n", retry));
              sleep(2);
              break;
            default:
              dbprintf(("SCSI_ModeSense : end %d\n", pRequestSense->SenseKey));
              return(pRequestSense->SenseKey);
              break;
            }
        }
      retry++;
      if (ret == 0)
        {
          retry=0;
        }
    }

  if (retry > 0)
    {
      return(ret);
    }
  
  dump_hex(*data, DataBufferLength);
  
  return(ret);
}

