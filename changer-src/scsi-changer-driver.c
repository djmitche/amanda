#ifndef lint
static char rcsid[] = "$Id: scsi-changer-driver.c,v 1.1.2.14 1999/03/04 20:47:41 th Exp $";
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

#include <scsi-defs.h>

#include "tapeio.h"

extern int TapeEject(int DeviceFD);

extern OpenFiles_T *pChangerDev;
extern OpenFiles_T *pTapeDev;
extern OpenFiles_T *pTapeDevCtl;


int PrintInquiry(SCSIInquiry_T *);
int GenericElementStatus(int DeviceFD, int InitStatus);
ElementInfo_T *LookupElement(int addr);
int GenericResetStatus(int DeviceFD);
int RequestSense(int, ExtendedRequestSense_T *, int  );
void dump_hex(char *, int);
void TerminateString(char *string, int length);
int BarCode(int fd);
int LogSense(int fd);
int SenseHandler(int fd, int flag, char *buffer);

int DoNothing();
int GenericMove(int, int, int);
int GenericRewind(int);
int GenericStatus();
int GenericFree();
int GenericEject(char *Device, int type);
int GenericClean(char *Device);                 /* Does the tape need a clean */
int GenericBarCode(int DeviceFD);               /* Do we have Barcode reader support */
int NoBarCode(int DeviceFD);

int GenericSearch();

int EXB120BarCode(int DeviceFD);
int EXB120SenseHandler(int DeviceFD, int, char *);
int GenericSenseHandler(int DeviceFD, int, char *);
int EXB10eSenseHandler(int DeviceFD, int, char *);
int EXB85058SenseHandler(int DeviceFD, int, char *);
int DLTSenseHandler(int DeviceFD, int, char *);
int TDS1420SenseHandler(int DeviceFD, int, char *);

ElementInfo_T *LookupElement(int address);

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
int DecodeModeSense(char *buffer, char *pstring);

int SCSI_Move(int DeviceFD, unsigned char chm, int from, int to);
int SCSI_LoadUnload(int DeviceFD, RequestSense_T *pRequestSense, unsigned char byte1, unsigned char load);
int SCSI_TestUnitReady(int, RequestSense_T *);
int SCSI_Inquiry(int, char *, unsigned char);
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
  {0xA5,
   12,
   "MOVE MEDIUM"},
  {0xB8,
   12,
   "READ ELEMENT STATUS"},
  {0, 0, 0}
};

ChangerCMD_T ChangerIO[] = {
  {"C1553A",
   {GenericMove,
    GenericElementStatus,
    DoNothing,
    GenericFree,
    GenericEject,
    GenericClean,
    GenericRewind,
    NoBarCode,
    GenericSearch,
    GenericSenseHandler}},
  {"EXB-10e",                 /* Exabyte Robot */
   {GenericMove,
    GenericElementStatus,
    GenericResetStatus,
    GenericFree,
    GenericEject,
    GenericClean,
    GenericRewind,
    GenericBarCode,
    GenericSearch,
    EXB10eSenseHandler}},
  {"EXB-120",                 /* Exabyte Robot */
   {GenericMove,
    GenericElementStatus,
    GenericResetStatus,
    GenericFree,
    GenericEject,
    GenericClean,
    GenericRewind,
    EXB120BarCode,
    GenericSearch,
    EXB120SenseHandler}},
  {"TDS 1420",                 /* Tandberg Robot */
   {GenericMove,
    GenericElementStatus,
    GenericResetStatus,
    GenericFree,
    GenericEject,
    GenericClean,
    GenericRewind,
    GenericBarCode,
    GenericSearch,
    TDS1420SenseHandler}},
  {"EXB-85058HE-0000",        /* Exabyte TapeDrive */
   {DoNothing,
    DoNothing,
    DoNothing,
    DoNothing,
    GenericEject,
    GenericClean,
    GenericRewind,
    GenericBarCode,
    GenericSearch,
    EXB85058SenseHandler}},
  {"DLT7000",        /* DLT7000 TapeDrive */
   {DoNothing,
    DoNothing,
    DoNothing,
    DoNothing,
    GenericEject,
    GenericClean,
    GenericRewind,
    GenericBarCode,
    GenericSearch,
    DLTSenseHandler}},
  {"generic",
   {GenericMove,
    GenericElementStatus,
    GenericResetStatus,
    GenericFree,
    GenericEject,
    GenericClean,
    GenericRewind,
    NoBarCode,
    GenericSearch,
    GenericSenseHandler}},
  {NULL, {NULL,NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL}}
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
int isempty(int fd, int slot)
{
  dbprintf(("##### START isempty\n"));
  if (pChangerDev == NULL)
    {
      dbprintf(("##### STOP isempty [-1]\n"));
      return(-1);
    }
  if (ElementStatusValid == 0)
    {
      if ( pChangerDev->functions->function[CHG_STATUS](fd, 1) != 0)
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
  /* Return 1 it cleaning is needed */
  int ret;
  
  dbprintf(("##### START get_clean_state\n"));

  if (pTapeDevCtl == NULL)
    {
      dbprintf(("##### STOP get_clean_state [-1]\n"));
      return(-1);
    }
  ret=pTapeDevCtl->functions->function[CHG_CLEAN](tapedev);
  dbprintf(("##### STOP get_clean_state [%d]\n", ret));
  return(ret);    
}

int eject_tape(char *tapedev, int type)
  /* This function ejects the tape from the drive */
{
  int ret = 0;

  dbprintf(("##### START eject_tape\n"));

  if (pTapeDevCtl != NULL)
    {
      ret=pTapeDevCtl->functions->function[CHG_EJECT](tapedev, type);
      dbprintf(("##### STOP eject_tape [%d]\n", ret));
      return(ret);
    }
  
  if (pTapeDev != NULL)
    {
      ret=pTapeDev->functions->function[CHG_EJECT](tapedev, type);
      dbprintf(("##### STOP eject_tape [%d]\n", ret));
      return(ret);      
    }
  dbprintf(("##### STOP eject_tape [-1]\n"));
  return(-1);
}


int find_empty(int fd)
{
  int x;
  dbprintf(("###### START find_empty\n"));

  if (pChangerDev == NULL)
    {
      dbprintf(("%-20s : pChangerCtl == NULL\n","find_empty"));
      return(-1);
    }

  if (ElementStatusValid == 0)
    {
      if ( pChangerDev->functions->function[CHG_STATUS](fd, 1) != 0)
        {
          dbprintf(("###### END find_empty [%d]\n", -1));
          return(-1);
        }
    }

  for (x = 0; x < STE; x++)
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

int drive_loaded(int fd, int drivenum)
{

  dbprintf(("###### START drive_loaded\n"));
  dbprintf(("%-20s : fd %d drivenum %d \n", "drive_loaded", fd, drivenum));

  if (pChangerDev == NULL)
    {
      dbprintf(("%-20s : pChangerCtl == NULL\n", "drive_loaded"));
      return(-1);
    }

  if (ElementStatusValid == 0)
      {
          if (pChangerDev->functions->function[CHG_STATUS](fd, 1) != 0)
              {
                  return(-1);
              }
      }

  if (pDTE[drivenum].status == 'E')
      return(0);
  return(1);
}



int unload(int fd, int drive, int slot)
{
  /*
   * unload the specified drive into the specified slot
   * (storage element)
   */
  dbprintf(("###### START unload\n"));
  dbprintf(("%-20s : fd %d, slot %d, drive %d \n", "unload", fd, slot, drive));

  if (pChangerDev == NULL)
    {
      dbprintf(("%-20s : pChangerCtl == NULL\n", "unload"));
      return(-1);
    }

  if (ElementStatusValid == 0)
      {
          if (pChangerDev->functions->function[CHG_STATUS](fd, 1) != 0)
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
      slot = find_empty(fd);
      dbprintf(("unload : try to unload to slot %d\n", slot));
    }
  
  pChangerDev->functions->function[CHG_MOVE](fd, pDTE[drive].address, pSTE[slot].address);
  /*
   * Update the Status
   */
  if (pChangerDev->functions->function[CHG_STATUS](fd, 1) != 0)
      {
          return(-1);
      }

  return(0);
}

/*
 * load the media from the specified element (slot) into the
 * specified data transfer unit (drive)
 */
int load(int fd, int drive, int slot)
{
  int ret;

  dbprintf(("###### START load\n"));
  dbprintf(("%-20s : fd %d, drive %d, slot %d \n", "load", fd, drive, slot));

  if (pChangerDev == NULL)
    {
      dbprintf(("%-20s : pChangerCtl == NULL\n", "load"));
      return(-1);
    }
  
  if (ElementStatusValid == 0)
      {
          if (pChangerDev->functions->function[CHG_STATUS](fd, 1) != 0)
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

  ret = pChangerDev->functions->function[CHG_MOVE](fd, pSTE[slot].address, pDTE[drive].address);
  
  /*
   * Update the Status
   */
  if (pChangerDev->functions->function[CHG_STATUS](fd, 1) != 0)
      {
          return(-1);
      }
  
  return(ret);
}


int get_slot_count(int fd)
{
  dbprintf(("###### START get_slot_count\n"));
  dbprintf(("%-20s : fd %d\n", "get_slot_count", fd));

  if (pChangerDev == NULL)
    {
      dbprintf(("%-20s : pChangerCtl == NULL\n", "get_slot_count"));
      return(-1);
    }

  if (ElementStatusValid == 0)
    {
      pChangerDev->functions->function[CHG_STATUS](fd, 1);
    }

  return(STE);
  /*
   * return the number of slots in the robot
   * to the caller
   */
  
}


/*
 * retreive the number of data-transfer devices
 */ 
int get_drive_count(int fd)
{
  dbprintf(("###### START get_drive_count\n"));
  dbprintf(("%-20s : fd %d\n", "get_drive_count", fd));

  if (pChangerDev == NULL)
    {
      dbprintf(("%-20s : pChangerCtl == NULL\n", "get_drive_count"));
      return(-1);
    }


  if (ElementStatusValid == 0)
      {
          if ( pChangerDev->functions->function[CHG_STATUS](fd, 1) != 0)
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

OpenFiles_T *OpenDevice(char *DeviceName, char *ConfigName)
{
  OpenFiles_T *pwork;
  ChangerCMD_T *p = (ChangerCMD_T *)&ChangerIO;
  
  dbprintf(("##### START OpenDevice\n"));
  dbprintf(("OpenDevice : %s\n", DeviceName));
  
  if ((pwork = SCSI_OpenDevice(DeviceName)) != NULL )
    {
      
      pwork->ConfigName = strdup(ConfigName);
      while(p->ident != NULL)
        {
          if (strcmp(pwork->ident, p->ident) == 0)
            {
              pwork->functions = p;
              return(pwork);
            }
          p++;
        }
      /* Nothing matching found, try generic */
      p = (ChangerCMD_T *)&ChangerIO;
      while(p->ident != NULL)
        {
          if (strcmp("generic", p->ident) == 0)
            {
              pwork->functions = p;
              return(pwork);
            }
          p++;
        }  
    }
  return(NULL); 
}



int BarCode(int fd)
{
  int ret;
  dbprintf(("##### START BarCode\n"));
  dbprintf(("%-20s : fd %d\n", "BarCode", fd));

  if (pChangerDev == NULL)
    {
      dbprintf(("%-20s : pChangerCtl == NULL\n", "BarCode"));
      return(-1);
    }

  ret = pChangerDev->functions->function[CHG_BARCODE](fd);
  return(ret);
}

int Tape_Ready(char *tapedev, int wait)
{
  int true = 1;
  int cnt = 0;
  OpenFiles_T *pwork;

  RequestSense_T *pRequestSense;
  dbprintf(("##### START Tape_Ready\n"));
  if (pTapeDev != NULL)
    {
      if (pTapeDev->SCSI == 1)
        pwork = pTapeDev;
    }
  
  if (pTapeDev != NULL)
    {
      if (pTapeDevCtl->SCSI == 1)
        pwork = pTapeDevCtl;
    }
   
  
  if (pwork != NULL && pwork->SCSI == 0)
      {
          dbprintf(("Tape_Ready : can#t send SCSI commands\n"));
          sleep(wait);
          return(0);
      }

  if ((pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T))) == NULL)
      {
        dbprintf(("Tape_Ready : malloc failed\n"));
        return(-1);
      }

  GenericRewind(pwork->fd);

  while (true && cnt < wait)
    {
      if (SCSI_TestUnitReady(pwork->fd, pRequestSense ))
        {
          true = 0;
        } else { 
          switch (SenseHandler(pwork->fd, 0, (char *)pRequestSense))
            {
            case SENSE_NO_TAPE_ONLINE:
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
          break;
        }
      pSCSICommand++;
    }
  return(0);
}

int DecodeModeSense(char *buffer, char *pstring)
{
  int length = (unsigned char)*buffer - 4;
  dump_hex(buffer, 255);

  dbprintf(("##### START DecodeModeSense\n"));
  /* Jump over the Parameter List header */
  buffer = buffer + 4;
  
  while (length > 0)
    {
      switch (*buffer & 0x3f)
        {
        case 0:
            pVendorUnique = buffer;
            break;
        case 0x1d:
          pEAAPage = (EAAPage_T *)buffer;
          dbprintf(("DecodeModeSense : Medium Transport Element Address %d\n", 
                    V2(pEAAPage->MediumTransportElementAddress)));
          dbprintf(("DecodeModeSense : Number of Medium Transport Elements %d\n", 
                    V2(pEAAPage->NoMediumTransportElements)));
          dbprintf(("DecodeModeSense : First Storage Element Address %d\n", 
                    V2(pEAAPage->FirstStorageElementAddress)));
          dbprintf(("DecodeModeSense : Number of  Storage Elements %d\n", 
                    V2(pEAAPage->NoStorageElements)));
          dbprintf(("DecodeModeSense : First Import/Export Element Address %d\n", 
                    V2(pEAAPage->FirstImportExportElementAddress)));
          dbprintf(("DecodeModeSense : Number of  ImportExport Elements %d\n", 
                    V2(pEAAPage->NoImportExportElements)));
          dbprintf(("DecodeModeSense : First Data Transfer Element Address %d\n", 
                    V2(pEAAPage->FirstDataTransferElementAddress)));
          dbprintf(("DecodeModeSense : Number of  Data Transfer Elements %d\n", 
                    V2(pEAAPage->NoDataTransferElements)));
          buffer++;
          break;
        case 0x1f:
          pDeviceCapabilitiesPage = (DeviceCapabilitiesPage_T *)buffer;
          dbprintf(("DecodeModeSense : MT can store data cartridges %d\n",
                    pDeviceCapabilitiesPage->MT));
          dbprintf(("DecodeModeSense : ST can store data cartridges %d\n",
                    pDeviceCapabilitiesPage->ST));
          dbprintf(("DecodeModeSense : IE can store data cartridges %d\n",
                    pDeviceCapabilitiesPage->IE));
          dbprintf(("DecodeModeSense : DT can store data cartridges %d\n",
                    pDeviceCapabilitiesPage->DT));
          dbprintf(("DecodeModeSense : MT to MT %d\n",
                    pDeviceCapabilitiesPage->MT2MT));
          dbprintf(("DecodeModeSense : MT to ST %d\n",
                    pDeviceCapabilitiesPage->MT2ST));
          dbprintf(("DecodeModeSense : MT to IE %d\n",
                    pDeviceCapabilitiesPage->MT2IE));
          dbprintf(("DecodeModeSense : MT to DT %d\n",
                    pDeviceCapabilitiesPage->MT2DT));
          dbprintf(("DecodeModeSense : ST to MT %d\n",
                    pDeviceCapabilitiesPage->ST2ST));
          dbprintf(("DecodeModeSense : ST to MT %d\n",
                    pDeviceCapabilitiesPage->ST2ST));
          dbprintf(("DecodeModeSense : ST to DT %d\n",
                    pDeviceCapabilitiesPage->ST2DT));
          dbprintf(("DecodeModeSense : IE to MT %d\n",
                    pDeviceCapabilitiesPage->IE2MT));
          dbprintf(("DecodeModeSense : IE to ST %d\n",
                    pDeviceCapabilitiesPage->IE2IE));
          dbprintf(("DecodeModeSense : IE to ST %d\n",
                    pDeviceCapabilitiesPage->IE2DT));
          dbprintf(("DecodeModeSense : IE to ST %d\n",
                    pDeviceCapabilitiesPage->IE2DT));
          dbprintf(("DecodeModeSense : DT to MT %d\n",
                    pDeviceCapabilitiesPage->DT2MT));
          dbprintf(("DecodeModeSense : DT to ST %d\n",
                    pDeviceCapabilitiesPage->DT2ST));
          dbprintf(("DecodeModeSense : DT to IE %d\n",
                    pDeviceCapabilitiesPage->DT2IE));
          dbprintf(("DecodeModeSense : DT to DT %d\n",
                    pDeviceCapabilitiesPage->DT2DT));
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

int DecodeSense(RequestSense_T *sense, char *pstring)
{
  dbprintf(("##### START DecodeSense\n"));
  dbprintf(("%sSense Keys\n", pstring));
  dbprintf(("\tErrorCode                     %02x\n", sense->ErrorCode));
  dbprintf(("\tValid                         %d\n", sense->Valid));
  dbprintf(("\tASC                           %02X\n", sense->AdditionalSenseCode));
  dbprintf(("\tASCQ                          %02X\n", sense->AdditionalSenseCodeQualifier));
  dbprintf(("\tSense key                     %02X\n", sense->SenseKey));
  switch (sense->SenseKey)
    {
    case 0:
      dbprintf(("\t\tNo Sense\n"));
      break;
    case 1:
      dbprintf(("\t\tRecoverd Error\n"));
      break;
    case 2:
      dbprintf(("\t\tNot Ready\n"));
      break;
    case 3:
      dbprintf(("\t\tMedium Error\n"));
      break;
    case 4:
      dbprintf(("\t\tHardware Error\n"));
      break;
    case 5:
      dbprintf(("\t\tIllegal Request\n"));
      break;
    case 6:
      dbprintf(("\t\tUnit Attention\n"));
      break;
    case 7:
      dbprintf(("\t\tData Protect\n"));
      break;
    case 8:
      dbprintf(("\t\tBlank Check\n"));
      break;
    case 9:
      dbprintf(("\t\tVendor uniq\n"));
      break;
    case 0xa:
      dbprintf(("\t\tCopy Aborted\n"));
      break;
    case 0xb:
      dbprintf(("\t\tAborted Command\n"));
      break;
    case 0xc:
      dbprintf(("\t\tEqual\n"));
      break;
    case 0xd:
      dbprintf(("\t\tVolume Overflow\n"));
      break;
    case 0xe:
      dbprintf(("\t\tMiscompare\n"));
      break;
    case 0xf:
      dbprintf(("\t\tReserved\n"));
      break;
    }
  return(0);      
}

int DecodeExtSense(ExtendedRequestSense_T *sense, char *pstring)
{
  ExtendedRequestSense_T *p;

  dbprintf(("##### START DecodeExtSense\n"));
  p = sense;

  dbprintf(("%sExtended Sense\n", pstring));
  DecodeSense((RequestSense_T *)p, pstring);
  dbprintf(("\tLog Parameter Page Code         %02X\n", sense->LogParameterPageCode));
  dbprintf(("\tLog Parameter Code              %02X\n", sense->LogParameterCode));
  dbprintf(("\tUnderrun/Overrun Counter        %02X\n", sense->UnderrunOverrunCounter));
  dbprintf(("\tRead/Write Error Counter        %d\n", V3((char *)sense->ReadWriteDataErrorCounter))); 
  if (sense->PF)
    dbprintf(("\tPower Fail\n"));
  if (sense->BPE)
    dbprintf(("\tSCSI Bus Parity Error\n"));
  if (sense->FPE)
    dbprintf(("\tFormatted Buffer parity Error\n"));
  if (sense->ME)
    dbprintf(("\tMedia Error\n"));
  if (sense->ECO)
    dbprintf(("\tError Counter Overflow\n"));
  if (sense->TME)
    dbprintf(("\tTapeMotion Error\n"));
  if (sense->TNP)
    dbprintf(("\tTape Not Present\n"));
  if (sense->LBOT)
    dbprintf(("\tLogical Beginning of tape\n"));
  if (sense->TMD)
    dbprintf(("\tTape Mark Detect Error\n"));
  if (sense->WP)
    dbprintf(("\tWrite Protect\n"));
  if (sense->FMKE)
    dbprintf(("\tFilemark Error\n"));
  if (sense->URE)
    dbprintf(("\tUnder Run Error\n"));
  if (sense->WEI)
    dbprintf(("\tWrite Error 1\n"));
  if (sense->SSE)
    dbprintf(("\tServo System Error\n"));
  if (sense->FE)
    dbprintf(("\tFormatter Error\n"));
  if (sense->UCLN)
    dbprintf(("\tCleaning Cartridge is empty\n"));
  if (sense->RRR)
    dbprintf(("\tReverse Retries Required\n"));
  if (sense->CLND)
    dbprintf(("\tTape Drive has been cleaned\n"));
  if (sense->CLN)
    dbprintf(("\tTape Drive needs to be cleaned\n"));
  if (sense->PEOT)
    dbprintf(("\tPhysical End of Tape\n"));
  if (sense->WSEB)
    dbprintf(("\tWrite Splice Error\n"));
  if (sense->WSEO)
    dbprintf(("\tWrite Splice Error\n"));
  dbprintf(("\tRemaing 1024 byte tape blocks   %d\n", V3((char *)sense->RemainingTape)));
  dbprintf(("\tTracking Retry Counter          %02X\n", sense->TrackingRetryCounter));
  dbprintf(("\tRead/Write Retry Counter        %02X\n", sense->ReadWriteRetryCounter));
  dbprintf(("\tFault Sympton Code              %02X\n", sense->FaultSymptomCode));
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

int EXB120BarCode(int DeviceFD)
{
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
      DecodeModeSense(pModePage, "EXB120BarCode :");
      
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
  
  dump_hex((char *)pChangerDev->inquiry, INQUIRY_SIZE);
  dbprintf(("EXB120BarCode : vendor_specific[19] %x\n",
            pChangerDev->inquiry->vendor_specific[19]));
  return(1);
}

int NoBarCode(int DeviceFD)
{
  dbprintf(("##### START NoBarCode\n"));
  return(0);
}

int GenericBarCode(int DeviceFD)
{

  dbprintf(("##### START GenericBarCode\n"));
  dump_hex((char *)pChangerDev->inquiry, INQUIRY_SIZE);
  dbprintf(("GenericBarCode : vendor_specific[19] %x\n",
            pChangerDev->inquiry->vendor_specific[19]));
  if ((pChangerDev->inquiry->vendor_specific[19] & 1) == 1)
    {
      return(1);
    } else {
      return(0);
    }
  return(0);
}

int SenseHandler(int DeviceFD, int flag, char *buffer)
{
  int ret;
  dbprintf(("##### START SenseHandler\n"));
  if (pChangerDev != NULL && pChangerDev->fd == DeviceFD)
    {
      ret = pChangerDev->functions->function[CHG_ERROR](DeviceFD, flag, buffer);
      return(ret);
    }

  if (pTapeDev != NULL && pTapeDev->fd == DeviceFD)
    {
      ret = pTapeDev->functions->function[CHG_ERROR](DeviceFD, flag, buffer);
      return(ret);
    }
  
  if (pTapeDevCtl != NULL && pTapeDevCtl->fd == DeviceFD)
    {
      ret = pTapeDevCtl->functions->function[CHG_ERROR](DeviceFD, flag, buffer);
      return(ret);
    }

}

int GenericEject(char *Device, int type)
{
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
  
  if (pTapeDevCtl != NULL && pTapeDevCtl->SCSI == 1)
    LogSense(pTapeDev->fd);
  
  if ( type > 1)
    {
      dbprintf(("GenericEject : use mtio ioctl for eject on %s\n", pTapeDev->dev));
      return(Tape_Eject(pTapeDev->fd));
    }
  
  if (pTapeDev->fd != pTapeDevCtl->fd && pTapeDevCtl->SCSI == 1) {
    dbprintf(("GenericEject : Close %s \n", pTapeDev->dev));
    close(pTapeDev->fd);
  }
  
  
  if (pTapeDevCtl->SCSI == 0)
    {
      dbprintf(("GenericEject : Device %s not able to receive SCSI commands\n", pTapeDev->dev));
      return(Tape_Eject(pTapeDev->fd));
    }
  
  
  dbprintf(("GenericEject : SCSI eject on %s = %s\n", pTapeDevCtl->dev, pTapeDevCtl->ConfigName));
  
  /* Unload the tape, 1 == don't wait for success
   * 0 == unload 
   */
  ret = SCSI_LoadUnload(pTapeDevCtl->fd, pRequestSense, 1, 0);
  
  /* < 0 == fatal */
  if (ret < 0)
    return(-1);
  
  if ( ret > 0)
    {
    }
  
  true = 1;
  
  
  while (true && cnt < 300)
    {
      if (SCSI_TestUnitReady(pTapeDevCtl->fd, pRequestSense))
        {
          true = 0;
          break;
        }
      if (SenseHandler(pTapeDevCtl->fd, 0, (char *)pRequestSense) == SENSE_NO_TAPE_ONLINE)
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
                                0, 0, 
                                (char *) pRequestSense,
                                sizeof(RequestSense_T));
      
      DecodeSense(pRequestSense, "GenericRewind : ");
      
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
      if (SenseHandler(DeviceFD, 0, (char *)pRequestSense) == SENSE_NO_TAPE_ONLINE)
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
  ExtendedRequestSense_T ExtRequestSense;

  dbprintf(("##### START GenericClean\n"));
  if (pTapeDevCtl == NULL || pTapeDevCtl->SCSI == 0)
      {
          dbprintf(("GenericClean : can't send SCSI commands\n"));
          return(0);
      }

  RequestSense(pTapeDevCtl->fd, &ExtRequestSense, 0);
  dbprintf(("GenericClean :\n"));
  DecodeExtSense(&ExtRequestSense, "GenericClean : ");
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
                                0, 0,
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

int GenericSenseHandler(int DeviceFD, int flag, char *buffer)
{ 
  RequestSense_T *pRequestSense = (RequestSense_T *)buffer;
  int ret = 0;
  InErrorHandler = 1;

  dbprintf(("##### START GenericSenseHandler\n"));

  if (flag == 1)
    {
    } else {
      DecodeSense(pRequestSense, "GenericSenseHandler : ");
      switch (pRequestSense->SenseKey)
        {
        case NOT_READY:
          dbprintf(("GenericSenseHandler : NOT_READY ASC = %x ASCQ = %x\n",
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          switch (pRequestSense->AdditionalSenseCode)
            {
            default:
              ret = SENSE_RETRY;
              break;
            }
          break;
        case UNIT_ATTENTION:
          dbprintf(("GenericSenseHandler : UNIT_ATTENTION ASC = %x ASCQ = %x\n",
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          switch (pRequestSense->AdditionalSenseCode)
            {
            default:
              ret = SENSE_RETRY;
              break;
            }
          break;
        case ILLEGAL_REQUEST:
          dbprintf(("GenericSenseHandler : ILLEGAL_REQUEST  ASC = %x ASCQ = %x\n",
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          switch (pRequestSense->AdditionalSenseCode)
            {
            default:
              ret = SENSE_ABORT;
              break;
            }
          break;
        default:
          dbprintf(("GenericSenseHandler : Unknow %x ASC = %x ASCQ = %x\n",
                    pRequestSense->SenseKey,
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          ret = SENSE_ABORT;
          break;
        }
    }
  InErrorHandler=0;
  return(ret);
}

/**/

int TDS1420SenseHandler(int DeviceFD, int flag, char *buffer)
{ 
  RequestSense_T *pRequestSense = (RequestSense_T *)buffer;
  ElementInfo_T *pElementInfo;
  int ret = 0;
  InErrorHandler = 1;

  dbprintf(("##### START TDS1420SenseHandler\n"));

  if (flag == 1)
    {
      pElementInfo = (ElementInfo_T *)buffer;
      switch(pElementInfo->ASC)
        {
        case 0x83:
          switch(pElementInfo->ASCQ)
            {
            case 00:
              ret=SENSE_IES;
              break;
            case 01:
              ret=SENSE_IES;
              break;
            case 04:
              ret=SENSE_IGNORE;
              break;
            default:
              break;
            }
          break;
        default:
          break;
        }
    } else {
      DecodeSense(pRequestSense, "TDS1420SenseHandler : ");
      switch (pRequestSense->SenseKey)
        {
        case NOT_READY:
          dbprintf(("TDS1420SenseHandler : NOT_READY ASC = %x ASCQ = %x\n",
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          switch (pRequestSense->AdditionalSenseCode)
            {
            default:
              ret = SENSE_RETRY;
              break;
            }
          break;
        case UNIT_ATTENTION:
          dbprintf(("TDS1420SenseHandler : UNIT_ATTENTION ASC = %x ASCQ = %x\n",
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          switch (pRequestSense->AdditionalSenseCode)
            {
            default:
              ret = SENSE_RETRY;
              break;
            }
          break;
        case ILLEGAL_REQUEST:
          dbprintf(("TDS1420SenseHandler : ILLEGAL_REQUEST  ASC = %x ASCQ = %x\n",
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          switch (pRequestSense->AdditionalSenseCode)
            {
            default:
              ret = SENSE_ABORT;
              break;
            }
          break;
        default:
          dbprintf(("TDS1420SenseHandler : Unknow %x ASC = %x ASCQ = %x\n",
                    pRequestSense->SenseKey,
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          ret = SENSE_ABORT;
          break;
        }
    }
  InErrorHandler=0;
  return(ret);
}



int DLTSenseHandler(int DeviceFD, int flag, char *buffer)
{ 
  RequestSense_T *pRequestSense = (RequestSense_T *)buffer;
  int ret = 0;
  InErrorHandler = 1;

  dbprintf(("##### START DLTSenseHandler\n"));

  if (flag == 1)
    {
    } else {
      DecodeSense(pRequestSense, "DLTSenseHandler : ");
      switch (pRequestSense->SenseKey)
        {
        case NOT_READY:
          dbprintf(("DLTSenseHandler : NOT_READY ASC = %x ASCQ = %x\n",
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          switch (pRequestSense->AdditionalSenseCode)
            {
            case 0x4:
              if (pRequestSense->AdditionalSenseCodeQualifier == 00)
                {
                  dbprintf(("DLTSenseHandler : Logical Unit not ready, No additional sense\n"));
                  ret = SENSE_RETRY;
                }
              if (pRequestSense->AdditionalSenseCodeQualifier == 02)
                {
                  dbprintf(("DLTSenseHandler : Logical Unit not ready, in progress becoming ready\n"));
                  ret = SENSE_NO_TAPE_ONLINE;
                }
              break;
            case 0x30:
              if (pRequestSense->AdditionalSenseCodeQualifier == 0x3)
                {
                  dbprintf(("DLTSenseHandler : The tape drive is being cleaned\n"));
                } else {
                  dbprintf(("DLTSenseHandler : Unknown ASCQ %x\n",
                            pRequestSense->AdditionalSenseCodeQualifier));
                }
              ret = SENSE_RETRY;
              break;
            default:
              ret = SENSE_RETRY;
              break;
            }
          break;
        case UNIT_ATTENTION:
          dbprintf(("DLTSenseHandler : UNIT_ATTENTION ASC = %x ASCQ = %x\n",
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          switch (pRequestSense->AdditionalSenseCode)
            {
            default:
              ret = SENSE_RETRY;
              break;
            }
          break;
        case ILLEGAL_REQUEST:
          dbprintf(("DLTSenseHandler : ILLEGAL_REQUEST  ASC = %x ASCQ = %x\n",
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          switch (pRequestSense->AdditionalSenseCode)
            {
            default:
              ret = SENSE_ABORT;
              break;
            }
          break;
        default:
          dbprintf(("DLTSenseHandler : Unknow %x ASC = %x ASCQ = %x\n",
                    pRequestSense->SenseKey,
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          ret = SENSE_ABORT;
          break;
        }
    }
  InErrorHandler=0;
  return(ret);
}

int EXB85058SenseHandler(int DeviceFD, int flag, char *buffer)
{ 
  RequestSense_T *pRequestSense = (RequestSense_T *)buffer;
  int ret = 0;
  InErrorHandler = 1;

  dbprintf(("##### START EXB85058SenseHandler\n"));
    
  if (flag == 1)
    {
    } else {
      DecodeSense(pRequestSense, "EXB85058SenseHandler : ");
      switch (pRequestSense->SenseKey)
        {
        case NOT_READY:
          dbprintf(("EXB85058SenseHandler : NOT_READY ASC = %x ASCQ = %x\n",
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          switch (pRequestSense->AdditionalSenseCode)
            {
            case 0x4:
              if (pRequestSense->AdditionalSenseCodeQualifier == 00)
                {
                  dbprintf(("EXB85058SenseHandler : Logical Unit not ready, No additional sense\n"));
                }
              if (pRequestSense->AdditionalSenseCodeQualifier == 01)
                {
                  dbprintf(("EXB85058SenseHandler : Logical Unit not ready, in progress becoming ready\n"));
                }
              ret = SENSE_RETRY;
              break;
            case 0x30:
              if (pRequestSense->AdditionalSenseCodeQualifier == 0x3)
                {
                  dbprintf(("EXB85058SenseHandler : The tape drive is being cleaned\n"));
                } else {
                  dbprintf(("EXB85058SenseHandler : Unknown ASCQ %x\n",
                            pRequestSense->AdditionalSenseCodeQualifier));
                }
              ret = SENSE_RETRY;
              break;
            case 0x3A:
              dbprintf(("EXB85058SenseHandler : No tape online/loaded\n"));
              ret = SENSE_NO_TAPE_ONLINE;
              break;
            default:
              ret = SENSE_RETRY;
              break;
            }
          break;
        case UNIT_ATTENTION:
          dbprintf(("EXB85058SenseHandler : UNIT_ATTENTION ASC = %x ASCQ = %x\n",
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          switch (pRequestSense->AdditionalSenseCode)
            {
            default:
              ret = SENSE_RETRY;
              break;
            }
          break;
        case ILLEGAL_REQUEST:
          dbprintf(("EXB85058SenseHandler : ILLEGAL_REQUEST  ASC = %x ASCQ = %x\n",
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          switch (pRequestSense->AdditionalSenseCode)
            {
            default:
              ret = SENSE_ABORT;
              break;
            }
          break;
        default:
          dbprintf(("EXB85058SenseHandler : Unknow %x ASC = %x ASCQ = %x\n",
                    pRequestSense->SenseKey,
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          ret = SENSE_ABORT;
          break;
    }
    }
  InErrorHandler=0;
  return(ret);
}

int EXB10eSenseHandler(int DeviceFD, int flag, char *buffer)
{
  RequestSense_T *pRequestSense;
  ElementInfo_T *pElementInfo;
  int ret = 0;
  int to;
  InErrorHandler = 1;

  dbprintf(("##### START EXB10eSenseHandler\n"));
    
  if (flag == 1)
    {
      pElementInfo = (ElementInfo_T *)buffer;
      switch (pElementInfo->ASC)
        {
        case 0x90:
          switch (pElementInfo->ASCQ)
            {
            case 02:
              ret = SENSE_ABORT;
              break;
            case 03:
              ret = SENSE_IES;
              break;
            }
          break;
        default:
          ret = SENSE_RETRY;
          break;
        }
    } else {
      pRequestSense = (RequestSense_T *)buffer;
      DecodeSense(pRequestSense, "EXB10eSenseHandler : ");
      switch (pRequestSense->SenseKey)
        {
        case NOT_READY:
          switch (pRequestSense->AdditionalSenseCode)
            {
            case 0x4:
              switch (pRequestSense->AdditionalSenseCodeQualifier)
                {
                case 0x85:   
                  dbprintf(("EXB10eSenseHandler : Library door is open\n"));
                  ret=SENSE_ABORT;
                  break;
                case 0x86:   
                  dbprintf(("EXB10eSenseHandler : The data cartridge magazine is missing\n"));
                  ret=SENSE_ABORT;
                  break;
                case 0x89:
                  dbprintf(("EXB10eSenseHandler : The library is in CHS Monitor mode\n"));
                  ret=SENSE_ABORT;
                  break;
                case 0x8c:
                  dbprintf(("EXB10eSenseHandler : The library is performing a power-on self test\n"));
                  sleep(10);
                  ret=SENSE_RETRY;
                  break;
                case 0x8d:
                  dbprintf(("EXB10eSenseHandler : The library is in LCD mode\n"));
                  ret=SENSE_ABORT;
                  break;
                case 0x8e:
                  dbprintf(("EXB10eSenseHandler : The library is in Sequential mode\n"));
                  ret=SENSE_ABORT;
                  break;
                default:
                  dbprintf(("EXB10eSenseHandler : unknown %x ASC %x\n", 
                            pRequestSense->SenseKey,
                            pRequestSense->AdditionalSenseCode));
                  ret=SENSE_ABORT;
                  break;
                }
              break;
            default:
              dbprintf(("EXB10eSenseHandler : unknown %x ASC %x ASCQ %x\n", 
                        pRequestSense->SenseKey,
                        pRequestSense->AdditionalSenseCode,
                        pRequestSense->AdditionalSenseCodeQualifier));
              ret=SENSE_ABORT;
              break;
            }
          break;
        case UNIT_ATTENTION:
          dbprintf(("EXB10eSenseHandler : SenseKey UNIT_ATTENTION\n"));
          ret=SENSE_RETRY;
          break;
        case ILLEGAL_REQUEST:
          dbprintf(("EXB10eSenseHandler : SenseKey ILLEGAL_REQUEST\n"));
          switch(pRequestSense->AdditionalSenseCode)
            {
            case 0x91:
              dbprintf(("EXB10eSenseHandler : CHM full during reset, try to unload\n"));
              
              if (pChangerDev == NULL)
                {
                  return(SENSE_ABORT);
                }
              
              for (to = 0; to < DTE ; to++)
                {
                  if (pChangerDev->functions->function[CHG_MOVE](DeviceFD, 11, pDTE[to].address) == 0)
                    {
                      InErrorHandler = 0;
                      return(SENSE_RETRY);
                    }
                }
              for (to = 0; to < STE ; to++)
                {
                  if (pChangerDev->functions->function[CHG_MOVE](DeviceFD, 11, pSTE[to].address) == 0)
                    {
                      InErrorHandler = 0;
                      return(SENSE_RETRY);
                    }
                }              
              break;
            default:
              ret=SENSE_ABORT;
              break;
            }
        }
    }
  InErrorHandler=0;
  return(ret);
}

int EXB120SenseHandler(int DeviceFD, int flag, char *buffer)
{ 
  RequestSense_T *pRequestSense = (RequestSense_T *)buffer;
  ElementInfo_T *pElementInfo;
  int ret = 0;
  InErrorHandler = 1;

  dbprintf(("##### START EXB120SenseHandler\n"));

  if (flag == 1)
    {
      pElementInfo = (ElementInfo_T *)buffer;
      switch (pElementInfo->ASC)
        {
        case 0x83:
          switch (pElementInfo->ASCQ)
            {
            case 00:
              dbprintf(("EXB120SenseHandler : Label Invalid\n"));
              ret = SENSE_IES;
              break;
            case 01:
              dbprintf(("EXB120SenseHandler : Cannot read label\n"));
              ret = SENSE_IGNORE;
              break;
            case 02:
              dbprintf(("EXB120SenseHandler : Magazine not present\n"));
              ret = SENSE_ABORT;
              break;
            case 03:
              dbprintf(("EXB120SenseHandler : Label and full status unknown\n"));
              ret = SENSE_IES;
              break;
            case 04:
              dbprintf(("EXB120SenseHandler : Drive not installed\n"));
              ret = SENSE_IGNORE;
              break;
            default:
              dbprintf(("EXB120SenseHandler : unkown ASC/ASCQ %x %x\n",
                        pElementInfo->ASC,
                        pElementInfo->ASCQ));    
              ret = SENSE_IGNORE;
              break;
            }
          break;
        default:
          ret = SENSE_RETRY;
          break;
        }
      
    } else {
      DecodeSense(pRequestSense, "EXB120SenseHandler : ");
      switch (pRequestSense->SenseKey)
        {
        case NOT_READY:
          dbprintf(("EXB120SenseHandler : NOT_READY ASC = %x ASCQ = %x\n",
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          switch (pRequestSense->AdditionalSenseCode)
            {
            default:
              ret = SENSE_RETRY;
              break;
            }
          break;
        case UNIT_ATTENTION:
          dbprintf(("EXB120SenseHandler : UNIT_ATTENTION ASC = %x ASCQ = %x\n",
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          switch (pRequestSense->AdditionalSenseCode)
            {
            default:
              ret = SENSE_RETRY;
              break;
            }
          break;
        case ILLEGAL_REQUEST:
          dbprintf(("EXB120SenseHandler : ILLEGAL_REQUEST  ASC = %x ASCQ = %x\n",
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          switch (pRequestSense->AdditionalSenseCode)
            {
            case 0x3b:
                switch (pRequestSense->AdditionalSenseCodeQualifier)
                    {
                    case 0x83:
                      dbprintf(("EXB120SenseHandler : Drive indicates handle is not ready to operate\n"));
                      ret = SENSE_RETRY;
                      break;
                    default:
                      ret = SENSE_ABORT;
                      break;
                    }
                break;
            default:
              ret = SENSE_ABORT;
              break;
            }
          break;
        default:
          dbprintf(("EXB120SenseHandler : Unknow %x ASC = %x ASCQ = %x\n",
                    pRequestSense->SenseKey,
                    pRequestSense->AdditionalSenseCode,
                    pRequestSense->AdditionalSenseCodeQualifier));
          ret = SENSE_ABORT;
          break;
        }
    }
  InErrorHandler=0;
  return(ret);
}

/* ======================================================= */
int GenericMove(int DeviceFD, int from, int to)
{
  ElementInfo_T *pfrom;
  ElementInfo_T *pto;
  int ret;
  int moveok = 0;

  dbprintf(("##### START GenericMove\n"));

  dbprintf(("%-20s : from = %d, to = %d\n", "GenericMove", from, to));

  if (pChangerDev == NULL)
    {
      dbprintf(("%20s : can't send SCSI commands\n", "GenericMove"));
      return(-1);
    }

  if (pTapeDevCtl == NULL || pTapeDevCtl->SCSI == 0)
    {
      dbprintf(("GenericMove : can't send SCSI commands to tape, no log pages read\n"));
    } else {
      LogSense(pTapeDev->fd);
    }

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
      to = find_empty(DeviceFD);
      dbprintf(("GenericMove : Unload to %d\n", to));
      if ((pto = LookupElement(to)) == NULL)
        {
          
        }
    }
  
  if (pDeviceCapabilitiesPage != NULL )
    {
      dbprintf(("GenericMove : checking if move from %d to %d is legal\n", from, to));
      switch (pfrom->type)
        {
        case CHANGER:
          dbprintf(("GenericMove : MT2"));
          switch (pto->type)
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
          dbprintf(("GenericMove : ST2"));
          switch (pto->type)
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
          dbprintf(("GenericMove : IE2"));
          switch (pto->type)
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
          dbprintf(("GenericMove : DT2"));
          switch (pto->type)
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
    }

  if (moveok == 0)
    { 
      dbprintf(("?? failed\n"));
      return(-1);
    }

  ret = SCSI_Move(DeviceFD, 0, from, to);
  return(ret);
}

int GetCurrentSlot(int fd, int drive)
{
  int x;
  dbprintf(("##### START GetCurrentSlot\n"));
  if (pChangerDev == NULL)
    {
      return(-1);
    }

  if (pChangerDev->SCSI == 0)
      {
          dbprintf(("GetCurrentSlot : can't send SCSI commands\n"));
          return(-1);
      }

  if (ElementStatusValid == 0)
    {
      if (pChangerDev->functions->function[CHG_STATUS](pChangerDev->fd, 1) != 0)
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

  int error = 0;                /* Is set if ASC for an element is set */
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
        DecodeModeSense(pModePage, "GenericElementStatus :");
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
              return(-1);
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
              pMTE[x].ASC = MediumTransportElementDescriptor->asc;
              pMTE[x].ASCQ = MediumTransportElementDescriptor->ascq;
              pMTE[x].status = (MediumTransportElementDescriptor->full > 0) ? 'F':'E';
              
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
                      error=1;
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
              return(-1);
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
              pSTE[x].ASC = StorageElementDescriptor->asc;
              pSTE[x].ASCQ = StorageElementDescriptor->ascq;
              pSTE[x].status = (StorageElementDescriptor->full > 0) ? 'F':'E';
              
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
                      error=1;
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
              return(-1);
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
              pIEE[x].ASC = ImportExportElementDescriptor->asc;
              pIEE[x].ASCQ = ImportExportElementDescriptor->ascq;
              pIEE[x].status = (ImportExportElementDescriptor->full > 0) ? 'F':'E';
              
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
                      error=1;
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
              return(-1);
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
              pDTE[x].ASC = DataTransferElementDescriptor->asc;
              pDTE[x].ASCQ = DataTransferElementDescriptor->ascq;
              pDTE[x].scsi = DataTransferElementDescriptor->scsi;
              pDTE[x].status = (DataTransferElementDescriptor->full > 0) ? 'F':'E';
              
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
                      error=1;
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
          return(-1);
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
                  pMTE[x].ASC = MediumTransportElementDescriptor->asc;
                  pMTE[x].ASCQ = MediumTransportElementDescriptor->ascq;
                  pMTE[x].status = (MediumTransportElementDescriptor->full > 0) ? 'F':'E';
                  
                  if (MediumTransportElementDescriptor->svalid == 1)
                    {
                      pMTE[x].from = V2(MediumTransportElementDescriptor->source);
                    } else {
                      pMTE[x].from = -1;
                    }
                  
                  if (pMTE[x].ASC > 0)
                    {
                      if (SenseHandler(DeviceFD, 1, (char *)&pMTE[x]) == SENSE_RETRY)
                        error = 1;
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
                  pSTE[x].ASC = StorageElementDescriptor->asc;
                  pSTE[x].ASCQ = StorageElementDescriptor->ascq;
                  pSTE[x].status = (StorageElementDescriptor->full > 0) ? 'F':'E';
                            
                  if (StorageElementDescriptor->svalid == 1)
                    {
                      pSTE[x].from = V2(StorageElementDescriptor->source);
                    } else {
                      pSTE[x].from = -1;
                    }
                            
                  if (pSTE[x].ASC > 0)
                    {
                      if (SenseHandler(DeviceFD, 1, (char *)&pSTE[x]) == SENSE_RETRY)
                        error = 1;
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
                  pIEE[x].ASC = ImportExportElementDescriptor->asc;
                  pIEE[x].ASCQ = ImportExportElementDescriptor->ascq;
                  pIEE[x].status = (ImportExportElementDescriptor->full > 0) ? 'F':'E';
                  
                  if (ImportExportElementDescriptor->svalid == 1)
                    {
                      pIEE[x].from = V2(ImportExportElementDescriptor->source);
                    } else {
                      pIEE[x].from = -1;
                    }
                  
                  if (pIEE[x].ASC > 0)
                    {
                      if (SenseHandler(DeviceFD, 1, (char *)&pIEE[x]) == SENSE_RETRY)
                        error = 1;
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
                  pDTE[x].ASC = DataTransferElementDescriptor->asc;
                  pDTE[x].ASCQ = DataTransferElementDescriptor->ascq;
                  pDTE[x].scsi = DataTransferElementDescriptor->scsi;
                  pDTE[x].status = (DataTransferElementDescriptor->full > 0) ? 'F':'E';
                            
                  if (DataTransferElementDescriptor->svalid == 1)
                    {
                      pDTE[x].from = V2(DataTransferElementDescriptor->source);
                    } else {
                      pDTE[x].from = -1;
                    }
                            
                  if (pDTE[x].ASC > 0)
                    {
                      if (SenseHandler(DeviceFD, 1, (char *)&pDTE[x]) == SENSE_RETRY)
                        error = 1;
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
    dbprintf(("\t\tElement #%04d %c\n\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n",
              pMTE[x].address, pMTE[x].status, pMTE[x].ASC,
              pMTE[x].ASCQ, pMTE[x].type, pMTE[x].from, pMTE[x].VolTag));

  dbprintf(("\n\n\tStorage Elements (Media slots) :\n"));

  for ( x = 0; x < STE; x++)
    dbprintf(("\t\tElement #%04d %c\n\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n",
              pSTE[x].address, pSTE[x].status, pSTE[x].ASC,
              pSTE[x].ASCQ, pSTE[x].type, pSTE[x].from, pSTE[x].VolTag));

  dbprintf(("\n\n\tData Transfer Elements (tape drives) :\n"));
 
  for ( x = 0; x < DTE; x++)
    dbprintf(("\t\tElement #%04d %c\n\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n\t\t\tSCSI ADDRESS = %d\n",
              pDTE[x].address, pDTE[x].status, pDTE[x].ASC,
              pDTE[x].ASCQ, pDTE[x].type, pDTE[x].from, pDTE[x].VolTag,pDTE[x].scsi));

  dbprintf(("\n\n\tImport/Export Elements  :\n"));

  for ( x = 0; x < IEE; x++)
    dbprintf(("\t\tElement #%04d %c\n\t\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n",
              pIEE[x].address, pIEE[x].status, pIEE[x].ASC,
              pIEE[x].ASCQ, pIEE[x].type, pIEE[x].from, pIEE[x].VolTag));

  if (error != 0 && InitStatus == 1)
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
  RequestSense_T RequestSense;
  int ret;
  
  dbprintf(("##### START RequestSense\n"));

  CDB[0] = SC_COM_REQUEST_SENSE; /* REQUEST SENSE */                       
  CDB[1] = 0;                   /* Logical Unit Number = 0, Reserved */ 
  CDB[2] = 0;                   /* Reserved */              
  CDB[3] = 0;                   /* Reserved */
  CDB[4] = 0x1D;                /* Allocation Length */                    
  CDB[5] = (ClearErrorCounters << 7) & 0x80;                 /*  */
  
  bzero(ExtendedRequestSense, sizeof(ExtendedRequestSense_T));
  
  ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 6,                      
                            (char *) ExtendedRequestSense,
                            sizeof(ExtendedRequestSense_T),  
                            (char *) &RequestSense, sizeof(RequestSense_T));
  
  
  if (ret < 0)
    {
      return(ret);
    }
  
  if ( ret > 0)
    {
      DecodeExtSense(ExtendedRequestSense, "RequestSense : ");
      return(RequestSense.SenseKey);
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
            return(&pDTE[x]);
        }
    }

  if (MTE > 0)
    {
      for (x = 0; x < MTE; x++)
        {
          if (pMTE[x].address == address)
            return(&pMTE[x]);
        }
    }

  if (STE > 0)
    {
      for (x = 0; x < STE; x++)
        {
          if (pSTE[x].address == address)
            return(&pSTE[x]);
        }
    }

  if (IEE > 0)
    {
      for ( x = 0; x < IEE; x++)
        {
          if (pIEE[x].address == address)
            return(&pIEE[x]);
        }
    }
  return(NULL);
}
/*
 * Here comes everything what decode the log Pages
 */

int LogSense(DeviceFD)
{
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
      pTapeDevCtl->SCSI == 1) 
    {
      if ((pRequestSense  = (RequestSense_T *)malloc(sizeof(RequestSense_T))) == NULL)
        {
          dbprintf(("LogSense : malloc failed\n"));
              return(-1);
        }
      
      if (SCSI_TestUnitReady(pTapeDevCtl->fd, pRequestSense) == 0)
        {
          DecodeSense(pRequestSense, "LogSense :");
          dbprintf(("LogSense : Tape_Ready failed\n"));
          free(pRequestSense);
          return(0);
        }
      if (GenericRewind(pTapeDevCtl->fd) < 0) 
        { 
          dbprintf(("LogSense : Rewind failed\n"));
          free(pRequestSense);
          return(0); 
        } 
      /*
       * Try to read the tape label
       */
      if (pTapeDev != NULL)
        {
          if ((result = (char *)tapefd_rdlabel(pTapeDev->fd, &datestamp, &label)) == NULL)
            {
              fprintf(StatFile, "==== %s ==== %s ====\n", datestamp, label);
            } else {
              fprintf(StatFile, "%s\n", result);
            }
        }
      if ((buffer = (char *)malloc(size)) == NULL)
        {
          dbprintf(("LogSense : malloc failed\n"));
          return(-1);
        }
      bzero(buffer, size);
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
          DecodeSense(pRequestSense, "LogSense : ");
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
          bzero(buffer, size);
          
          
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
              DecodeSense(pRequestSense, "LogSense : ");
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
            if ((strcmp(pTapeDevCtl->ident, p->ident) == 0 ||strcmp("*", p->ident) == 0)  && p->LogPage == logpages[count]) {
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


void ChangerStatus(char *option)
{
  int x;

  if (pChangerDev == NULL)
    {
      printf(("Changer dev not opened"));
      return;
    }
  
  if (ElementStatusValid == 0)
    {
      if (pChangerDev->functions->function[CHG_STATUS](pChangerDev->fd, 1) != 0)
        {
          printf("Can not initialize changer status\n");
          return;
        }
    }
  printf("\n\n\tMedia Transport Elements (robot arms) :\n");

  for ( x = 0; x < MTE; x++)
    printf("\t\tElement #%04d %c\n\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n",
              pMTE[x].address, pMTE[x].status, pMTE[x].ASC,
              pMTE[x].ASCQ, pMTE[x].type, pMTE[x].from, pMTE[x].VolTag);
  
  printf("\n\n\tStorage Elements (Media slots) :\n");
  
  for ( x = 0; x < STE; x++)
    printf("\t\tElement #%04d %c\n\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n",
              pSTE[x].address, pSTE[x].status, pSTE[x].ASC,
              pSTE[x].ASCQ, pSTE[x].type, pSTE[x].from, pSTE[x].VolTag);
  
  printf("\n\n\tData Transfer Elements (tape drives) :\n");
  
  for ( x = 0; x < DTE; x++)
    printf("\t\tElement #%04d %c\n\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n\t\t\tSCSI ADDRESS = %d\n",
              pDTE[x].address, pDTE[x].status, pDTE[x].ASC,
              pDTE[x].ASCQ, pDTE[x].type, pDTE[x].from, pDTE[x].VolTag,pDTE[x].scsi);
  
  printf("\n\n\tImport/Export Elements  :\n");
  
  for ( x = 0; x < IEE; x++)
    printf("\t\tElement #%04d %c\n\t\t\t\tASC = %02X ASCQ = %02X\n\t\t\tType %d From = %04d\n\t\t\tTAG = %s\n",
              pIEE[x].address, pIEE[x].status, pIEE[x].ASC,
              pIEE[x].ASCQ, pIEE[x].type, pIEE[x].from, pIEE[x].VolTag);

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
  
  for (x = length; x >= 0 && !isalnum(string[x]); x--)
    string[x] = '\0';
}

/* OK here starts a new set of functions.
   Every function is for one SCSI command.
   Prefix is SCSI_ and then the SCSI command name
*/

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
                                0, 0, (char *)pRequestSense, sizeof(RequestSense_T)); 

      dbprintf(("SCSI_Move : SCSI_ExecuteCommand = %d\n", ret));

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
                            0, 0, 
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
                      0, 0, 
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
      bzero(pRequestSense, sizeof(RequestSense_T));
      
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
      bzero(pRequestSense, sizeof(RequestSense_T));
      bzero(buffer, size);
      
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

int SCSI_Inquiry(int DeviceFD, char *buffer, u_char size)
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
      bzero(buffer, size);
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
          dump_hex(buffer, size);
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
          return(-1);
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
      bzero(pRequestSense, sizeof(RequestSense_T) );
      
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
          DecodeSense(pRequestSense, "SCSI_ReadElementStatus :");
          free(data);
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
      free(*data);
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
      bzero(pRequestSense, sizeof(RequestSense_T) );
      
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
          DecodeSense(pRequestSense, "SCSI_ReadElementStatus :");
          free(data);
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
      free(*data);
      return(ret);
    }
  
  dump_hex(*data, DataBufferLength);
  
  return(ret);
}

/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-file-style: gnu
 * End:
 */
