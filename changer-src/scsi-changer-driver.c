#ifndef lint
static char rcsid[] = "$Id: scsi-changer-driver.c,v 1.1.2.8 1999/01/26 11:23:05 th Exp $";
#endif
/*
 * Interface to control a tape robot/library connected to the SCSI bus
 *
 * Copyright (c) 1998 T.Hepper th@icem.de
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

int Inquiry(int, SCSIInquiry_T *);
int TestUnitReady(int, RequestSense_T *);
int PrintInquiry(SCSIInquiry_T *);
int GenericElementStatus(int DeviceFD);
int EXB120ElementStatus(int DeviceFD);
ElementInfo_T *LookupElement(int addr);
int ResetStatus(int DeviceFD);
int RequestSense(int, ExtendedRequestSense_T *, int  );

int DoNothing();
int GenericMove(int, int, int);
int GenericRewind(int);
int GenericStatus();
int GenericFree();
int GenericEject(char *Device, int type);
int DLTEject(char *Device, int type);
int GenericClean(char *Device);
ChangerCMD_T *LookupFunction(int fd, char *dev);

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

int SCSI_ReadElementStatus(int DeviceFD, unsigned char type, char **data);

FILE *StatFile;

SC_COM_T SCSICommand[] = {
  {0x00,
   5,
   "TEST UNIT READY"},
  {0x01,
   5,
   "REWIND"},
  {0x03,
   5,
   "REQUEST SENSE"},
  {0x07,
   5,
   "INITIALIZE ELEMENT STATUS"},
  {0x12,
   5,
   "INQUIRY"},
  {0x13,
   5,
   "ERASE"},
  {0x1B,
   5,
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
    GenericRewind}},
  {"EXB-10e",                 /* Exabyte Robot */
   {GenericMove,
    GenericElementStatus,
    ResetStatus,
    GenericFree,
    GenericEject,
    GenericClean,
    GenericRewind}},
  {"EXB-120",                 /* Exabyte Robot */
   {GenericMove,
    EXB120ElementStatus,
    ResetStatus,
    GenericFree,
    GenericEject,
    GenericClean,
    GenericRewind}},
  {"EXB-85058HE-0000",        /* Exabyte TapeDrive */
   {DoNothing,
    DoNothing,
    DoNothing,
    DoNothing,
    GenericEject,
    GenericClean,
    GenericRewind}},
  {"DLT7000",        /* DLT7000 TapeDrive */
   {DoNothing,
    DoNothing,
    DoNothing,
    DoNothing,
    DLTEject,
    GenericClean,
    GenericRewind}},
  {"generic",
   {GenericMove,
    GenericElementStatus,
    ResetStatus,
    GenericFree,
    GenericEject,
    GenericClean,
    GenericRewind}},
  {NULL, {NULL, NULL, NULL, NULL, NULL, NULL, NULL}}
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

OpenFiles_T *pOpenFiles = NULL;

int ElementStatusValid = 0;
char *SlotArgs = 0;

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
  int rslot;
  ChangerCMD_T *pwork;

  if ((pwork = LookupFunction(fd, "")) == NULL)
    {
      return(-1);
    }

  if (ElementStatusValid == 0)
    {
      if ( pwork->function[CHG_STATUS](fd) != 0)
        {
          return(-1);
        }
    }

  if (pSTE[slot].status == 'E')
      return(1);
  return(0);
}

int get_clean_state(char *tapedev)
{
  /* Return 1 it cleaning is needed */
  int ret;
  ChangerCMD_T *pwork;

  if ((pwork = LookupFunction(0, tapedev)) == NULL )
      {
          return(-1);
      }

  ret=pwork->function[CHG_CLEAN](tapedev);
  return(ret);    
}

int eject_tape(char *tapedev, int type)
  /* This function ejects the tape from the drive */
{
  int ret = 0;
  ChangerCMD_T *pwork;

  if ((pwork = LookupFunction(0, tapedev)) == NULL )
      {
          return(-1);
      }

  ret=pwork->function[CHG_EJECT](tapedev, type);
  return(ret);    
}


int find_empty(int fd)
{
  dbprintf(("find_empty %d\n", fd));
  return(0);
}

int drive_loaded(int fd, int drivenum)
{
  ChangerCMD_T *pwork;

  dbprintf(("\ndrive_loaded : fd %d drivenum %d \n", fd, drivenum));

  if ((pwork = LookupFunction(fd, "")) == NULL)
    {
      return(-1);
    }

  if (ElementStatusValid == 0)
      {
          if (pwork->function[CHG_STATUS](fd) != 0)
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
  int empty = 0;
  ChangerCMD_T *pwork;

  if ((pwork = LookupFunction(fd, "")) == NULL)
    {
      return(-1);
    }

  if (ElementStatusValid == 0)
      {
          if (pwork->function[CHG_STATUS](fd) != 0)
              {
                  return(-1);
              }
      }

  dbprintf(("unload : unload drive %d[%d] slot %d[%d]\n",drive,
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
      return(-1);
    }
  
  pwork->function[CHG_MOVE](fd, pDTE[drive].address, pSTE[slot].address);
  /*
   * Update the Status
   */
  if (pwork->function[CHG_STATUS](fd) != 0)
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
  ChangerCMD_T *pwork;

  if ((pwork = LookupFunction(fd, "")) == NULL)
    {
      return(-1);
    }
  
  if (ElementStatusValid == 0)
      {
          if (pwork->function[CHG_STATUS](fd) != 0)
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

  ret = pwork->function[CHG_MOVE](fd, pSTE[slot].address, pDTE[drive].address);
  
  /*
   * Update the Status
   */
  if (pwork->function[CHG_STATUS](fd) != 0)
      {
          return(-1);
      }
  
  return(ret);
}


int get_slot_count(int fd)
{
  ChangerCMD_T *pwork;

  if (ElementStatusValid == 0)
    {
      if ((pwork = LookupFunction(fd, "")) == NULL)
        {
          return(-1);
        }

      if (ElementStatusValid == 0)
          {
              pwork->function[CHG_STATUS](fd);
          }
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
  int count = 0;
  ChangerCMD_T *pwork;

  if ((pwork = LookupFunction(fd, "")) == NULL)
    {
      return(-1);
    }

  if (ElementStatusValid == 0)
      {
          if ( pwork->function[CHG_STATUS](fd) != 0)
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

int OpenDevice(char *DeviceName, char *ConfigName)
{
  int i;
  OpenFiles_T *pwork, *pprev;
  
  dbprintf(("OpenDevice : %s\n", DeviceName));

  for (pwork = pOpenFiles; pwork != NULL; pwork = pwork->next)
    {
      if (strcmp(DeviceName,pwork->dev) == 0)
        {
          return(pwork->fd);
        }
      pprev = pwork;
    }

  if ((pwork = SCSI_OpenDevice(DeviceName)) != NULL )
    {
      if (pOpenFiles == NULL)
        {
          pOpenFiles = pwork;
        } else {
          pprev->next = pwork;
        }
      if (strlen(ConfigName) > 0)
        {
          pwork->ConfigName = strdup(ConfigName);
        } else {
          pwork->ConfigName = NULL;
        }
      return(pwork->fd);
    }
  
  return(-1); 
}

OpenFiles_T * LookupDevice(char *DeviceName, int DeviceFD, int what)
{
  OpenFiles_T *pwork, *pprev;
  
  switch (what)
    {
    case LOOKUP_NAME:
      for (pwork = pOpenFiles; pwork != NULL; pwork = pwork->next)
        {
          if (strcmp(DeviceName, pwork->dev) == 0)
            {
              return(pwork);
            }
          pprev = pwork;
        }
      break;
    case LOOKUP_CONFIG:
      for (pwork = pOpenFiles; pwork != NULL; pwork = pwork->next)
        {
          if (strcmp(DeviceName, pwork->ConfigName) == 0)
            {
              return(pwork);
            }
          pprev = pwork;
        }
      break;
    case LOOKUP_FD:
      for (pwork = pOpenFiles; pwork != NULL; pwork = pwork->next)
        {
          if (DeviceFD == pwork->fd)
            {
              return(pwork);
            }
          pprev = pwork;
        }
      break;
    case LOOKUP_TYPE:
      for (pwork = pOpenFiles; pwork != NULL; pwork = pwork->next)
        {
          if (pwork->SCSI == 1 && DeviceFD == pwork->inquiry->data_format)
            {
              return(pwork);
            }
          pprev = pwork;
        }
      break;
    }
  /*
   * Nothing matching found, try to open 
   */
  if (strlen(DeviceName) > 0)
    {
      if ((pwork = SCSI_OpenDevice(DeviceName)) != NULL)
        {
          pprev->next = pwork;
          return(pwork);
        }
    }
  
  return(NULL);
}

int CloseDevice(char *DeviceName, int DeviceFD)
{
  OpenFiles_T *pwork, *pprev;
  int found = 0;
  pprev = NULL;

  if (strlen(DeviceName) == 0 && DeviceFD == 0)
    {
      for (pwork = pOpenFiles; pwork != NULL; pwork = pwork->next)
        {
          dbprintf(("CloseDevice : %d %s\n", pwork->fd, pwork->dev));
          SCSI_CloseDevice(pwork->fd);
        }
      return(0);
    }
  
  if (strlen(DeviceName) == 0)
    {
      for (pwork = pOpenFiles; pwork != NULL; pwork = pwork->next)
        {
          if (pwork->fd == DeviceFD)
            {
              found = 1;
              break;
            }
          pprev = pwork;
        }
    } else {
      for (pwork = pOpenFiles; pwork != NULL; pwork = pwork->next)
        {
          if (strcmp(DeviceName,pwork->dev) == 0)
            {
              found = 1;
              break;
            }
          pprev = pwork;
        }
    }
  
  if (pwork != NULL && found == 1)
    {
      if(SCSI_CloseDevice(pwork->fd) < 0)
        {
          dbprintf(("SCSI_CloseDevice %s failed\n", pwork->ident));
          return(0);
        }
      if (pwork == pOpenFiles && pwork->next == NULL)
        {
          free(pOpenFiles);
          pOpenFiles == NULL;
          return(0);
        }
      if (pwork == pOpenFiles)
        {
          pOpenFiles = pwork->next;
          free(pwork);
          return(0);
        }
      if (pwork->next == NULL)
        {
          pprev->next = NULL;
          free(pwork);
          return(0);
        }
      pprev->next = pwork->next;
      free(pwork);
      return(0);
    }
  return(0);
}


int Tape_Ready(char *tapedev, int wait)
{
  int true = 1;
  int cnt = 0;
  RequestSense_T *pRequestSense;
  OpenFiles_T *pwork;

  if ((pwork = LookupDevice(tapedev, 0, LOOKUP_NAME)) == NULL)
    {
      dbprintf(("Tape_Ready : LookupDevice %s failed \n", tapedev));
      sleep(wait);
      return(0);
    }

  if (pwork->SCSI == 0)
      {
          dbprintf(("Tape_Ready : can#t send SCSI commands\n"));
          sleep(wait);
          return(0);
      }

  pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T));

  while (true && cnt < wait)
    {
      if (TestUnitReady(pwork->fd, pRequestSense ))
        {
          true = 0;
        } 
        if ((pRequestSense->SenseKey == SENSE_NOT_READY) &&
            (pRequestSense->AdditionalSenseCode == 0x3A))
          {
           true=0;
           break;
        } else {
          sleep(1);
        }
      cnt++;
    }

  free(pRequestSense);

  dbprintf(("Tape_Ready after %d sec\n", cnt));
  return(0);
}

int TestUnitReady(int DeviceFD, RequestSense_T *pRequestSense)
{
  CDB_T CDB;

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

  DecodeSense(pRequestSense, "TestUnitReady : ");

  if (pRequestSense->ErrorCode == 0 && pRequestSense->SenseKey == 0)
    {
      return(1);
    }
  
  return(0);
}

int Inquiry(int DeviceFD, SCSIInquiry_T *SCSIInquiry)
{
  CDB_T CDB;
  RequestSense_T *pRequestSense;
  int i;
  int lun;
  int ret;

  dbprintf(("Inquiry start:\n"));
  pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T));

  bzero(SCSIInquiry,sizeof(SCSIInquiry_T));
  CDB[0] = SC_COM_INQUIRY;
  CDB[1] = 0;
  CDB[2] = 0;
  CDB[3] = 0;
  CDB[4] = (unsigned char)sizeof(SCSIInquiry_T);
  CDB[5] = 0;

  ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 6,                      
                            SCSIInquiry,
                            sizeof(SCSIInquiry_T), 
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
      DecodeSense(pRequestSense, "Inquiry : ");
      return(pRequestSense->SenseKey);
    }

    
  dbprintf(("Inquiry end: %d\n", ret));
  return(ret);
}

int DecodeSCSI(CDB_T CDB, char *string)
{
  SC_COM_T *pSCSICommand;
  int x;

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

int DecodeSense(RequestSense_T *sense, char *pstring)
{
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
  dbprintf(("%-15s %.20s\n", "vendor_specific", SCSIInquiry->vendor_specific));
  return(0);
}


int DoNothing()
{
  return(0);
}

int GenericFree()
{
  return(0);
}

int DLTEject(char *Device, int type)
{
  CDB_T CDB;
  RequestSense_T *pRequestSense;
  int ret;
  int cnt = 0;
  int true = 1;
  OpenFiles_T *pwork;
  OpenFiles_T *ptape;

  if ((ptape = LookupDevice(Device, 0, LOOKUP_NAME)) == NULL) 
    {
      dbprintf(("DLTEject : LookupDevice %s failed\n", Device));
      return(-1);
    }

  pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T));

  LogSense(ptape->fd);
 
  if ((pwork = LookupDevice("tape_device", 0, LOOKUP_CONFIG)) == NULL) 
    {
      dbprintf(("DLTEject : LookupDevice tape_device failed\n"));
      return(-1);
    }

  if ( type > 1)
  {
     dbprintf(("DLTEject : use mtio ioctl for eject on %s\n", pwork->dev));
     return(Tape_Eject(pwork->fd));
  }

  if (ptape->fd != pwork->fd) {
    dbprintf(("DLTEject : Close %s \n", pwork->dev));
    CloseDevice("", pwork->fd);
  }

  if (ptape->SCSI == 0)
  {
     dbprintf(("DLTEject : Device %s not able to receive SCSI commands\n", pwork->dev));
     return(Tape_Eject(ptape->fd));
  }


  dbprintf(("DLTEject : SCSI eject on %s = %s\n", ptape->dev, ptape->ConfigName));

  CDB[0] = SC_COM_UNLOAD;
  CDB[1] = 1;             /* Don't wait for success */
  CDB[2] = 0;
  CDB[3] = 0;
  CDB[4] = 0;
  CDB[5] = 0;

  while (true)
    {
      ret = SCSI_ExecuteCommand(ptape->fd, Input, CDB, 6,
                                0, 0, 
                                (char *) pRequestSense,
                                sizeof(RequestSense_T));
      
      DecodeSense(pRequestSense, "GenericEject : ");
      
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
          dbprintf(("DLTEject : failed %d\n", ret));
          true = 0;
        }
    }
  true = 1;

  while (true && cnt < 300)
    {
      if (TestUnitReady(ptape->fd, pRequestSense))
        {
          true = 0;
          break;
        }
      if ((pRequestSense->SenseKey == SENSE_NOT_READY) &&
          (pRequestSense->AdditionalSenseCode == 0x4) &&
          (pRequestSense->AdditionalSenseCodeQualifier == 0x2))
        {
          true=0;
          break;
        } else {
          cnt++;
          sleep(2);
        }
    }

  free(pRequestSense);

  dbprintf(("DLTEject : Ready after %d sec, true = %d\n", cnt * 2, true));
  return(0);

}

int GenericEject(char *Device, int type)
{
  CDB_T CDB;
  RequestSense_T *pRequestSense;
  int ret;
  int cnt = 0;
  int true = 1;
  OpenFiles_T *pwork;
  OpenFiles_T *ptape;

  if ((ptape = LookupDevice(Device, 0, LOOKUP_NAME)) == NULL) 
    {
      dbprintf(("GenericEject : LookupDevice %s failed\n", Device));
      return(-1);
    }
  pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T));

  LogSense(ptape->fd);

  if ((pwork = LookupDevice("tape_device", 0, LOOKUP_CONFIG)) == NULL) 
    {
      dbprintf(("GenericEject : LookupDevice tape_device failed\n"));
      return(-1);
    }

  if ( type > 1)
  {
     dbprintf(("GenericEject : use mtio ioctl for eject on %s\n", pwork->dev));
     return(Tape_Eject(pwork->fd));
  }

  if (ptape->fd != pwork->fd) {
    dbprintf(("GenericEject : Close %s \n", pwork->dev));
    CloseDevice("", pwork->fd);
  }

  if (ptape->SCSI == 0)
  {
     dbprintf(("GenericEject : Device %s not able to receive SCSI commands\n", pwork->dev));
     return(Tape_Eject(ptape->fd));
  }


  dbprintf(("GenericEject : SCSI eject on %s = %s\n", ptape->dev, ptape->ConfigName));

  CDB[0] = SC_COM_UNLOAD;
  CDB[1] = 1;             /* Don't wait for success */
  CDB[2] = 0;
  CDB[3] = 0;
  CDB[4] = 0;
  CDB[5] = 0;

  while (true)
    {
      ret = SCSI_ExecuteCommand(ptape->fd, Input, CDB, 6,
                                0, 0, 
                                (char *) pRequestSense,
                                sizeof(RequestSense_T));
      
      DecodeSense(pRequestSense, "GenericEject : ");
      
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
          dbprintf(("GenericEject : failed %d\n", ret));
          true = 0;
        }
    }
  true = 1;

  while (true && cnt < 300)
    {
      if (TestUnitReady(ptape->fd, pRequestSense))
        {
          true = 0;
          break;
        }
      if ((pRequestSense->SenseKey == SENSE_NOT_READY) &&
          (pRequestSense->AdditionalSenseCode == 0x3A))
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

  pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T));

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
      if (TestUnitReady(DeviceFD, pRequestSense))
        {
          true = 0;
          break;
        }
      if ((pRequestSense->SenseKey == SENSE_NOT_READY) &&
          (pRequestSense->AdditionalSenseCode == 0x3A))
        {
          true=0;
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

  OpenFiles_T *pwork;

  if ((pwork = LookupDevice(Device, 0, LOOKUP_NAME)) == NULL) 
    {
      return(-1);
    }

  if (pwork->SCSI == 0)
      {
          dbprintf(("GenericClean : can't send SCSI commands\n"));
          return(0);
      }

  RequestSense(pwork->fd, &ExtRequestSense, 0);
  dbprintf(("GenericClean :\n"));
  DecodeExtSense(&ExtRequestSense, "GenericClean :");
  if(ExtRequestSense.CLN) {
    return(1);
  } else {
    return(0);
  }
}

int ResetStatus(int DeviceFD)
{
  CDB_T CDB;
  RequestSense_T *pRequestSense;
  int i;
  int ret;
  int retry = 1;
  
  pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T));

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
      if ( ret > 0)
        {
          dbprintf(("ResetStatus : ret = %d\n",ret));
          DecodeSense(pRequestSense, "ResetStatus : ");
          if (pRequestSense->SenseKey)
            {
              if (pRequestSense->SenseKey == UNIT_ATTENTION || pRequestSense->SenseKey == NOT_READY)
                {
                  if ( retry < MAX_RETRIES )
                    { 
                      dbprintf(("ResetStatus : retry %d\n", retry));
                      retry++;
                      sleep(2);
                    } else {
                      return(pRequestSense->SenseKey);
                    }
                } else {
                  return(pRequestSense->SenseKey);
                }
            }
        }
      if (ret == 0)
        retry = 0;
    }
  
  return(ret);
}


/* ======================================================= */
int GenericMove(int DeviceFD, int from, int to)
{
  CDB_T CDB;
  RequestSense_T pRequestSense;
  OpenFiles_T *pwork;
  int ret;
  int i;

  dbprintf(("GenericMove: from = %d, to = %d\n", from, to));

  if ((pwork = LookupDevice("", TYPE_TAPE, LOOKUP_TYPE)) != NULL)
    {
      LogSense(pwork->fd);
    }

  if (pwork->SCSI == 0)
      {
          dbprintf(("GenericMove : can't send SCSI commands\n"));
          return(-1);
      }

  CDB[0]  = SC_MOVE_MEDIUM;
  CDB[1]  = 0;
  CDB[2]  = 0;
  CDB[3]  = 0;     /* Address of CHM */
  MSB2(&CDB[4],from);
  MSB2(&CDB[6],to);
  CDB[8]  = 0;
  CDB[9]  = 0;
  CDB[10] = 0;
  CDB[11] = 0;
 
  ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 12,
                            0, 0, (char *) &pRequestSense, sizeof(RequestSense_T)); 

  dbprintf(("GenericMove SCSI_ExecuteCommand = %d\n", ret));

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
      dbprintf(("GenericMove: ret = %d\n",ret));
      DecodeSense(&pRequestSense, "GenericMove : ");
      return(pRequestSense.SenseKey);
    }
  
  return(ret);
}

int GetCurrentSlot(int fd, int drive)
{
  OpenFiles_T *pfile;
  ChangerCMD_T *pwork;
  int x;
  
  if ((pfile = LookupDevice("changer_dev", 0, LOOKUP_CONFIG)) == NULL)
    {
      dbprintf(("GetCurrentSlot: LookupDevice changer_dev failed\n"));
      return(-1);
    }
  
  if (pfile->SCSI == 0)
      {
          dbprintf(("GetCurrentSlot : can't send SCSI commands\n"));
          return(-1);
      }

  if ((pwork = LookupFunction(pfile->fd, "")) == NULL)
    {
      dbprintf(("GetCurrentSlot:  LookupFunctio failed\n"));
      return(-1);
    }
  
  if (ElementStatusValid == 0)
    {
      if (pwork->function[CHG_STATUS](pfile->fd) != 0)
        {
          return(-1);
        }
    }   

    
  if (pDTE[drive].from >= 0)
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
  return(-1);
}

int GenericElementStatus(int DeviceFD)
{
  unsigned char *DataBuffer = NULL;
  int DataBufferLength;
  ElementStatusData_T *ElementStatusData;
  ElementStatusPage_T *ElementStatusPage;
  MediumTransportElementDescriptor_T *MediumTransportElementDescriptor;
  StorageElementDescriptor_T *StorageElementDescriptor;
  DataTransferElementDescriptor_T *DataTransferElementDescriptor;
  ElementInfo_T *pwork;

  int error = 0;                /* Is set if ASC for an element is set */
  int i;
  int x = 0;
  int offset = 0;
  int NoOfElements;
  int retry = 1;
  int ret; 

  if (SCSI_ReadElementStatus(DeviceFD, 0, &DataBuffer) != 0)
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
          pMTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * MTE);
          memset(pMTE, 0, sizeof(ElementInfo_T) * MTE);
          for (x = 0; x < NoOfElements; x++)
            {
/*               dbprintf(("PVolTag %d, AVolTag %d\n", */
/*                         ElementStatusPage->pvoltag, */
/*                         ElementStatusPage->avoltag)); */

              MediumTransportElementDescriptor = (MediumTransportElementDescriptor_T *)&DataBuffer[offset];
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
                error = 1;
              offset = offset + V2(ElementStatusPage->length); 
            }
          break;
        case 2:
          STE = NoOfElements;
          pSTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * STE);
          memset(pSTE, 0, sizeof(ElementInfo_T) * STE);
          for (x = 0; x < NoOfElements; x++)
            {
/*               dbprintf(("PVolTag %d, AVolTag %d\n", */
/*                         ElementStatusPage->pvoltag, */
/*                         ElementStatusPage->avoltag)); */
              
              StorageElementDescriptor = (StorageElementDescriptor_T *)&DataBuffer[offset];
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
                error = 1;

              offset = offset + V2(ElementStatusPage->length); 
            }
          break;
        case 4:
          DTE = NoOfElements;
          pDTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * DTE);
          memset(pDTE, 0, sizeof(ElementInfo_T) * DTE);
          
          for (x = 0; x < NoOfElements; x++)
            {
/*               dbprintf(("PVolTag %d, AVolTag %d\n", */
/*                         ElementStatusPage->pvoltag, */
/*                         ElementStatusPage->avoltag)); */
              
              DataTransferElementDescriptor = (DataTransferElementDescriptor_T *)&DataBuffer[offset];
              pDTE[x].type = ElementStatusPage->type;
              pDTE[x].address = V2((char *)DataTransferElementDescriptor->address);
              pDTE[x].ASC = DataTransferElementDescriptor->asc;
              pDTE[x].ASCQ = DataTransferElementDescriptor->ascq;
              pDTE[x].status = (DataTransferElementDescriptor->full > 0) ? 'F':'E';
               
              if (DataTransferElementDescriptor->svalid == 1)
                {
                  pDTE[x].from = V2(DataTransferElementDescriptor->source);
                } else {
                  pDTE[x].from = -1;
                }
              
              if (pDTE[x].ASC > 0)
                error = 1;
              
              offset = offset + V2(ElementStatusPage->length); 
            }
          break;
        default:
          offset = offset + V2(ElementStatusPage->length); 
          dbprintf(("ReadElementStatus : UnGknown Type %d\n",ElementStatusPage->type));
          break;
        }
    }

  dbprintf(("\tMedia Transport Elements (robot arms) :\n"));
  for ( x = 0; x < MTE; x++)
    dbprintf(("\t\tElement #%04d %c ASC = %02X ASCQ = %02X Type %d From = %04d\n",
              pMTE[x].address, pMTE[x].status, pMTE[x].ASC,
              pMTE[x].ASCQ, pMTE[x].type, pMTE[x].from));

  dbprintf(("\tStorage Elements (Media slots) :\n"));
  for ( x = 0; x < STE; x++)
    dbprintf(("\t\tElement #%04d %c ASC = %02X ASCQ = %02X Type %d From = %04d\n",
              pSTE[x].address, pSTE[x].status, pSTE[x].ASC,
              pSTE[x].ASCQ, pSTE[x].type, pSTE[x].from));

  dbprintf(("\tData Transfer Elements (tape drives) :\n"));
  for ( x = 0; x < DTE; x++)
    dbprintf(("\t\tElement #%04d %c ASC = %02X ASCQ = %02X Type %d From = %04d\n",
              pDTE[x].address, pDTE[x].status, pDTE[x].ASC,
              pDTE[x].ASCQ, pDTE[x].type, pDTE[x].from));


  if (error != 0)
      {
          if (ResetStatus(DeviceFD) != 0)
              {
                  ElementStatusValid = 0;
                  free(DataBuffer);
                  return(-1);
              }
          if (GenericElementStatus(DeviceFD) != 0)
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


int EXB120ElementStatus(int DeviceFD)
{
  unsigned char *DataBuffer = NULL;
  int DataBufferLength;
  ElementStatusData_T *ElementStatusData;
  ElementStatusPage_T *ElementStatusPage;
  MediumTransportElementDescriptor_T *MediumTransportElementDescriptor;
  StorageElementDescriptor_T *StorageElementDescriptor;
  DataTransferElementDescriptor_T *DataTransferElementDescriptor;
  ElementInfo_T *pwork;

  int error = 0;                /* Is set if ASC for an element is set */
  int i;
  int x = 0;
  int offset = 0;
  int NoOfElements;
  int retry = 1;
  int ret; 

  if (SCSI_ReadElementStatus(DeviceFD, 0, &DataBuffer) != 0)
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
          pMTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * MTE);
          memset(pMTE, 0, sizeof(ElementInfo_T) * MTE);
          for (x = 0; x < NoOfElements; x++)
            {
/*               dbprintf(("PVolTag %d, AVolTag %d\n", */
/*                         ElementStatusPage->pvoltag, */
/*                         ElementStatusPage->avoltag)); */

              MediumTransportElementDescriptor = (MediumTransportElementDescriptor_T *)&DataBuffer[offset];
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

              if (pMTE[x].ASC ==0x83 && (pMTE[x].ASCQ == 00 || pMTE[x].ASCQ == 03))
                error = 1;
              offset = offset + V2(ElementStatusPage->length); 
            }
          break;
        case 2:
          STE = NoOfElements;
          pSTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * STE);
          memset(pSTE, 0, sizeof(ElementInfo_T) * STE);
          for (x = 0; x < NoOfElements; x++)
            {
/*               dbprintf(("PVolTag %d, AVolTag %d\n", */
/*                         ElementStatusPage->pvoltag, */
/*                         ElementStatusPage->avoltag)); */
              
              StorageElementDescriptor = (StorageElementDescriptor_T *)&DataBuffer[offset];
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
              
              if (pSTE[x].ASC ==0x83 && (pSTE[x].ASCQ == 00 || pSTE[x].ASCQ == 03))
                error = 1;

              offset = offset + V2(ElementStatusPage->length); 
            }
          break;
        case 4:
          DTE = NoOfElements;
          pDTE = (ElementInfo_T *)malloc(sizeof(ElementInfo_T) * DTE);
          memset(pDTE, 0, sizeof(ElementInfo_T) * DTE);
          
          for (x = 0; x < NoOfElements; x++)
            {
/*               dbprintf(("PVolTag %d, AVolTag %d\n", */
/*                         ElementStatusPage->pvoltag, */
/*                         ElementStatusPage->avoltag)); */
              
              DataTransferElementDescriptor = (DataTransferElementDescriptor_T *)&DataBuffer[offset];
              pDTE[x].type = ElementStatusPage->type;
              pDTE[x].address = V2((char *)DataTransferElementDescriptor->address);
              pDTE[x].ASC = DataTransferElementDescriptor->asc;
              pDTE[x].ASCQ = DataTransferElementDescriptor->ascq;
              pDTE[x].status = (DataTransferElementDescriptor->full > 0) ? 'F':'E';
               
              if (DataTransferElementDescriptor->svalid == 1)
                {
                  pDTE[x].from = V2(DataTransferElementDescriptor->source);
                } else {
                  pDTE[x].from = -1;
                }
              
              if (pDTE[x].ASC ==0x83 && (pDTE[x].ASCQ == 00 || pDTE[x].ASCQ == 03))
                 error = 1;
              
              offset = offset + V2(ElementStatusPage->length); 
            }
          break;
        default:
          offset = offset + V2(ElementStatusPage->length); 
          dbprintf(("ReadElementStatus : UnGknown Type %d\n",ElementStatusPage->type));
          break;
        }
    }

  dbprintf(("\tMedia Transport Elements (robot arms) :\n"));
  for ( x = 0; x < MTE; x++)
    dbprintf(("\t\tElement #%04d %c ASC = %02X ASCQ = %02X Type %d From = %04d\n",
              pMTE[x].address, pMTE[x].status, pMTE[x].ASC,
              pMTE[x].ASCQ, pMTE[x].type, pMTE[x].from));

  dbprintf(("\tStorage Elements (Media slots) :\n"));
  for ( x = 0; x < STE; x++)
    dbprintf(("\t\tElement #%04d %c ASC = %02X ASCQ = %02X Type %d From = %04d\n",
              pSTE[x].address, pSTE[x].status, pSTE[x].ASC,
              pSTE[x].ASCQ, pSTE[x].type, pSTE[x].from));

  dbprintf(("\tData Transfer Elements (tape drives) :\n"));
  for ( x = 0; x < DTE; x++)
    dbprintf(("\t\tElement #%04d %c ASC = %02X ASCQ = %02X Type %d From = %04d\n",
              pDTE[x].address, pDTE[x].status, pDTE[x].ASC,
              pDTE[x].ASCQ, pDTE[x].type, pDTE[x].from));


  if (error != 0)
      {
          if (ResetStatus(DeviceFD) != 0)
              {
                  ElementStatusValid = 0;
                  free(DataBuffer);
                  return(-1);
              }
          if (EXB120ElementStatus(DeviceFD) != 0)
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
  int error = 0;                /* Is set if ASC for an element is set */
  int i;
  int x = 0;
  int offset = 0;
  int NoOfElements;
  int ret;
  
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

ChangerCMD_T *LookupFunction(int fd, char *dev)
{
  OpenFiles_T *pwork =NULL ;
  ChangerCMD_T *p = (ChangerCMD_T *)&ChangerIO;

  if (fd > 0 && strlen(dev) == 0) 
    {
      if ((pwork = LookupDevice("", fd, LOOKUP_FD)) == NULL)
        {
          return(NULL);
        }
    }
  
  if (fd == 0 && strlen(dev) > 0)
    {
      if ((pwork = LookupDevice(dev, 0, LOOKUP_NAME)) == NULL)
        {
          return(NULL);
        }
      
    }

  if (pwork == NULL)
    {
      return(NULL);
    }

  while(p->ident != NULL)
    {
      if (strcmp(pwork->ident, p->ident) == 0)
        {
          return(p);
        }
      p++;
    }
  /* Nothing matching found, try generic */
  p = (ChangerCMD_T *)&ChangerIO;
  while(p->ident != NULL)
    {
      if (strcmp("generic", p->ident) == 0)
        {
          return(p);
        }
      p++;
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
  extern OpenFiles_T *pOpenFiles;
  OpenFiles_T *pwork;
  LogSenseHeader_T *LogSenseHeader;
  LogParameter_T *LogParameter;
  struct LogPageDecode *p;
  int found;
  int fd;
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

  if (tapestatfile != NULL && (StatFile = fopen(tapestatfile,"a")) != NULL) 
    {
      pRequestSense  = (RequestSense_T *)malloc(sizeof(RequestSense_T));

      if (TestUnitReady(DeviceFD, pRequestSense) == 0)
        {
          dbprintf(("LogSense : Tape_Ready failed\n"));
          return(0);
        }
       if (GenericRewind(DeviceFD) < 0) 
         { 
           dbprintf(("LogSense : Rewind failed\n")); 
           return(0); 
         } 
      /*
       * Try to read the tape label
       */
      if ((pwork = LookupDevice("tape_device", 0, LOOKUP_CONFIG)) != NULL)
        {
          if ((result = (char *)tapefd_rdlabel(pwork->fd, &datestamp, &label)) == NULL)
            {
              fprintf(StatFile, "==== %s ==== %s ====\n", datestamp, label);
            } else {
              fprintf(StatFile, "%s\n", result);
            }
        }
      buffer = (char *)malloc(size);
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
          free(pRequestSense);
          return(0);
        }

      for (pwork = pOpenFiles; pwork != NULL; pwork = pwork->next)
        {
          if (pwork->fd == DeviceFD)
            {
              break;
            }
        }

      if (pwork == NULL)
        {
          free(pRequestSense);
          return(-1);
        }

      LogSenseHeader = (LogSenseHeader_T *)buffer;
      nologpages = V2(LogSenseHeader->PageLength);
      logpages = (char *)malloc(nologpages);
  
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
            if ((strcmp(pwork->ident, p->ident) == 0 ||strcmp("*", p->ident) == 0)  && p->LogPage == logpages[count]) {
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
    }
    free(pRequestSense);
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

int
dump_hex(char *p, int size)
{
    int row_count = 0;
    int x = 0;

    while (row_count <= size)
    {
        dbprintf(("%02X ", (unsigned char)p[row_count]));
        row_count++;
        if ((row_count % 16) == 0)
          {
            dbprintf(("   "));
            for (x = 16; x>=0;x--)
              {
            if (isalnum((unsigned char)p[row_count - x]))
              dbprintf(("%c",(unsigned char)p[row_count - x]));
            else
              dbprintf(("."));
              }
            dbprintf(("\n"));
          }
    }
    dbprintf(("\n"));
}

/* OK here starts a new set of functions.
   Every function is for one SCSI command.
   Prefix is SCSI_ and the the SCSI command name
   SCSI_ReadElementStatus
*/


int SCSI_ReadElementStatus(int DeviceFD, unsigned char type, char **data)
{
  CDB_T CDB;
  int DataBufferLength;
  ElementStatusData_T *ElementStatusData;
  RequestSense_T RequestSenseBuf;
 
  int retry = 1;
  int ret; 

  if (*data != NULL)
    {
      *data = realloc(data, 8);
    } else {
      *data = malloc(8);
    }
  
  if (retry > 0)
    {
      bzero(&RequestSenseBuf, sizeof(RequestSense_T) );
      
      CDB[0] = SC_COM_RES;          /* READ ELEMENT STATUS */
      CDB[1] = type & 0xf;          /* Element Type Code = 0, VolTag = 0 */
      CDB[2] = 0;                   /* Starting Element Address MSB */   
      CDB[3] = 0;                   /* Starting Element Address LSB */   
      CDB[4] = 0;                   /* Number Of Elements MSB */             
      CDB[5] = 255;                 /* Number Of Elements LSB */                   
      CDB[6] = 0;                   /* Reserved */                                 
      CDB[7] = 0;                   /* Allocation Length MSB */                    
      MSB2(&CDB[8],8); /* Allocation Length */    
      CDB[10] = 0;                  /* Reserved */                                
      CDB[11] = 0;                  /* Control */                                  
      
      ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 12,                      
                                *data, 8, 
                                (char *) &RequestSenseBuf, sizeof(RequestSense_T));
      
      
      dbprintf(("SCSI_ReadElementStatus : (1) SCSI_ExecuteCommand %d\n", ret));
      if (ret < 0)
        {
          DecodeSense(&RequestSenseBuf, "SCSI_ReadElementStatus :");
          free(data);
          return(ret);
        }
      if ( ret > 0)
        {
          DecodeSense(&RequestSenseBuf, "SCSI_ReadElementStatus : ");
          if (RequestSenseBuf.SenseKey)
            {
              if (RequestSenseBuf.SenseKey == UNIT_ATTENTION || RequestSenseBuf.SenseKey == NOT_READY)
                {
                  if ( retry < MAX_RETRIES )
                    { 
                      dbprintf(("SCSI_ReadElementStatus : retry %d\n", retry));
                      retry++;
                      sleep(2);
                    } else {
                      free(data);
                      return(RequestSenseBuf.SenseKey);
                    }
                } else {
                  free(data);
                  return(RequestSenseBuf.SenseKey);
                }
            }
        }
      if (ret == 0)
        {
          retry=0;
        }
    }
  
  ElementStatusData = (ElementStatusData_T *)*data;
  DataBufferLength = V3(ElementStatusData->count);
  DataBufferLength = DataBufferLength + 8;
  dbprintf(("SCSI_ReadElementStatus: DataBufferLength %d, ret %d\n",DataBufferLength, ret));

  *data = realloc(*data, DataBufferLength);
  retry = 1;

  if (retry > 0)
    {
      bzero(&RequestSenseBuf, sizeof(RequestSense_T) );
      
      CDB[0] = SC_COM_RES;          /* READ ELEMENT STATUS */
      CDB[1] = 0;                   /* Element Type Code = 0, VolTag = 0 */
      CDB[2] = 0;                   /* Starting Element Address MSB */   
      CDB[3] = 0;                   /* Starting Element Address LSB */   
      CDB[4] = 0;                   /* Number Of Elements MSB */             
      CDB[5] = 255;                 /* Number Of Elements LSB */                   
      CDB[6] = 0;                   /* Reserved */                                 
      CDB[7] = 0;                   /* Allocation Length MSB */                    
      MSB2(&CDB[8],DataBufferLength); /* Allocation Length */    
      CDB[10] = 0;                  /* Reserved */                                
      CDB[11] = 0;                  /* Control */                                  
      
      ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 12,                      
                                *data, DataBufferLength, 
                                (char *) &RequestSenseBuf, sizeof(RequestSense_T));
      
      
      dbprintf(("SCSI_ReadElementStatus : (2) SCSI_ExecuteCommand %d\n", ret));
      if (ret < 0)
        {
          DecodeSense(&RequestSenseBuf, "SCSI_ReadElementStatus :");
          free(data);
          return(ret);
        }
      if ( ret > 0)
        {
          DecodeSense(&RequestSenseBuf, "SCSI_ReadElementStatus : ");
          if (RequestSenseBuf.SenseKey)
            {
              if (RequestSenseBuf.SenseKey == UNIT_ATTENTION || RequestSenseBuf.SenseKey == NOT_READY)
                {
                  if ( retry < MAX_RETRIES )
                    { 
                      dbprintf(("SCSI_ReadElementStatus : retry %d\n", retry));
                      retry++;
                      sleep(2);
                    } else {
                      free(data);
                      return(RequestSenseBuf.SenseKey);
                    }
                } else {
                  free(data);
                  return(RequestSenseBuf.SenseKey);
                }
            }
        }
      if (ret == 0)
        {
          retry=0;
        }
    }

  return(ret);
}

/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-file-style: gnu
 * End:
 */
