#ifndef lint
static char rcsid[] = "$Id: scsi-changer-driver.c,v 1.1.2.6 1998/12/22 05:12:08 oliva Exp $";
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
#ifdef HAVE_MTIO_H
#include <sys/mtio.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#

#include <scsi-defs.h>
#define MaxReadElementStatusData 1024

 

int Inquiry(int, SCSIInquiry_T *);
int TestUnitReady(int, RequestSense_T *);
int PrintInquiry(SCSIInquiry_T *);
int Move(int, int, int);
int Status(int, ElementInfo_T **);
int Eject(char *);
int Clean(char *);
int ReadElementStatus(int DeviceFD, ElementInfo_T **ElementInfo);
int ResetStatus(int DeviceFD);
int DoNothing();
int GenericMove(int, int, int);
int GenericStatus();
int GenericFree();
int GenericEject(int);
int GenericClean(int);
int RequestSense(int, ExtendedRequestSense_T *, int  );
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
  {0xB8,
   12,
   "READ ELEMENT STATUS"},
  {0, 0, 0}
};

ChangerCMD_T ChangerIO[] = {
  {"C1553A",
   GenericMove,
   ReadElementStatus,
   DoNothing,
   GenericFree,
   GenericEject,
   GenericClean},
  {"EXB-10e",                 /* Exabyte Robot */
   GenericMove,
   ReadElementStatus,
   ResetStatus,
   GenericFree,
   GenericEject,
   GenericClean},
  {"EXB-85058HE-0000",        /* Exabyte TapeDrive */
   DoNothing,
   DoNothing,
   DoNothing,
   DoNothing,
   GenericEject,
   GenericClean},
  {"generic",
   GenericMove,
   ReadElementStatus,
   DoNothing,
   GenericFree,
   GenericEject,
   GenericClean},
  {NULL, NULL, NULL, NULL, NULL}
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

int MaxSlots = 0;
int MinSlots = 99;
int TapeDrive = 0;
int NoTapeDrive = 0;
int NoRobot = 0;
char *SlotArgs = 0;
ElementInfo_T *pElementInfo = NULL;
ElementInfo_T *pwElementInfo = NULL;

/*
 * Open the device and placeit in the list of open files
 * The OS has to decide if it is an SCSI Commands capable device
 */

int OpenDevice(char *DeviceName)
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
      return(pwork->fd);
    }
  
  return(-1); 
}

OpenFiles_T * LookupDevice(char *DeviceName, int DeviceFD)
{
  OpenFiles_T *pwork, *pprev;

  for (pwork = pOpenFiles; pwork != NULL; pwork = pwork->next)
    {
      if (pwork->fd == DeviceFD || strcmp(DeviceName, pwork->dev) == 0)
        {
          return(pwork);
        }
        pprev = pwork;
    }

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
  pprev = NULL;

  if (DeviceName == NULL)
    {
      for (pwork = pOpenFiles; pwork != NULL; pwork = pwork->next)
        {
          dbprintf(("SCSI_CloseDevice %s %s\n", pwork->dev, pwork->name));
          close(pwork->fd);
        }
      return;
    }

  for (pwork = pOpenFiles; pwork != NULL; pwork = pwork->next)
    {
      if (strcmp(DeviceName,pwork->dev) == 0)
        {
          if(SCSI_CloseDevice(pwork->fd) < 0)
            {
              dbprintf(("SCSI_CloseDevice %s failed\n", pwork->name));
              return;
            }
          if (pwork == pOpenFiles && pwork->next == NULL)
            {
              free(pOpenFiles);
              pOpenFiles == NULL;
              return;
            }
          if (pwork == pOpenFiles)
            {
              pOpenFiles = pwork->next;
              free(pwork);
              return;
            }
          if (pwork->next == NULL)
            {
              pprev->next = NULL;
              free(pwork);
              return;
            }
          pprev->next = pwork->next;
          free(pwork);
          return;
        }
      pprev = pwork;
    }
}

int Tape_Ready(char *tapedev, char *changerdev, int changerfd, int wait)
{
  int true = 1;
  int cnt;
  int DeviceFD;
  RequestSense_T *pRequestSense;

  if (tapedev == NULL || strcmp(tapedev, changerdev) == 0) 
    {
      DeviceFD = changerfd;
    } else {
      if ((DeviceFD = OpenDevice(tapedev)) < 0)
        {
          sleep(wait);
          return;
        }
    }

  pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T));

  while (true && cnt < wait)
    {
      if (TestUnitReady(DeviceFD, pRequestSense ))
        {
          true = 0;
        } else {
          sleep(1);
        }
      cnt++;
    }

  free(pRequestSense);

  dbprintf(("Tape_Ready after %d sec\n", cnt));
  return;
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
          for (x=0; x <= pSCSICommand->length; x++)
            {
              dbprintf((" %02X", CDB[x]));
            }
          dbprintf(("\n"));
          break;
        }
      pSCSICommand++;
    }
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
    dbprintf(("\tTape Motion Error\n"));
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
}


int DoNothing()
{
}

int GenericFree()
{
}

int GenericEject(int DeviceFD)
{
  CDB_T CDB;
  RequestSense_T *pRequestSense;
  int ret;
  int cnt = 0;
  int true = 1;

  pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T));

  LogSense(DeviceFD);

  CDB[0] = SC_COM_UNLOAD;
  CDB[1] = 1;             /* Don't wait for success */
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

  dbprintf(("GenericEject : Ready after %d sec, true = %d\n", cnt * 2, true));
  return(0);

}

int GenericClean(int TapeFd)
{
  ExtendedRequestSense_T ExtRequestSense;
  RequestSense(TapeFd, &ExtRequestSense, 0);      
  if(ExtRequestSense.CLN) {
    return(1);
  } else {
    return(0);
  }
}

/* Function Prototypes */
/* =================== */
void NewElement(ElementInfo_T **ElementInfo);

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
      CDB[0] = 0x7;   /* */
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
  int ret;
  int i;

  dbprintf(("GenericMove: from = %d, to = %d\n", from, to));

  if (Status(DeviceFD,&pElementInfo) > 0)
    {
      dbprintf(("GenericMove : ResetStatus\n"));
      ret = ResetStatus(DeviceFD);
      if (ret != 0)
        return(ret);
    }

  LogSense(DeviceFD);

  CDB[0]  = SC_MOVE_MEDIUM;
  CDB[1]  = 0;
  CDB[2]  = 0;
  CDB[3]  = 0;     /* Address of CHM */
  MSB2(&CDB[4],from);
  MSB2(&CDB[6], to);
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
/* ======================================================= */
int ReadElementStatus(int DeviceFD, ElementInfo_T **ElementInfo)
{
  CDB_T CDB;
  unsigned char DataBuffer[MaxReadElementStatusData];
  int DataBufferLength;
  RequestSense_T RequestSenseBuf;
  ElementStatusData_T *ElementStatusData;
  ElementStatusPage_T *ElementStatusPage;
  MediumTransportElementDescriptor_T *MediumTransportElementDescriptor;
  StorageElementDescriptor_T *StorageElementDescriptor;
  DataTransferElementDescriptor_T *DataTransferElementDescriptor;
  int error = 0;                /* Is set if ASC for an element is set */
  int i;
  int x = 0;
  int offset = 0;
  int NoOfElements;
  int ret; 
  int retry = 1;

  MinSlots = 99;
  MaxSlots = 0;
  NoTapeDrive = 0;
  NoRobot = 0;

  while (retry > 0)
    {
      bzero(&RequestSenseBuf, sizeof(RequestSense_T) );

      CDB[0] = 0xB8;                /* READ ELEMENT STATUS */
      CDB[1] = 0;                   /* Element Type Code = 0, VolTag = 0 */
      CDB[2] = 0;                   /* Starting Element Address MSB */   
      CDB[3] = 0;                   /* Starting Element Address LSB */   
      CDB[4] = 0;                   /* Number Of Elements MSB */             
      CDB[5] = 255;                 /* Number Of Elements LSB */                   
      CDB[6] = 0;                   /* Reserved */                                 
      CDB[7] = 0;                   /* Allocation Length MSB */                    
      MSB2(&CDB[8], MaxReadElementStatusData); /* Allocation Length */    
      CDB[10] = 0;                  /* Reserved */                                
      CDB[11] = 0;                  /* Control */                                  



      ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 12,                      
                                DataBuffer, sizeof(DataBuffer), 
                                (char *) &RequestSenseBuf, sizeof(RequestSense_T));


      dbprintf(("ReadElementStatus : SCSI_ExecuteCommand %d\n", ret));
      if (ret < 0)
        {
          dbprintf(("%s: Request Sense[Inquiry]: %02X",
                    "chs", ((unsigned char *) &RequestSenseBuf)[0]));
          for (i = 1; i < sizeof(RequestSense_T); i++)               
            dbprintf((" %02X", ((unsigned char *) &RequestSenseBuf)[i]));
          dbprintf(("\n"));   
          return(ret);
        }
      if ( ret > 0)
        {
          DecodeSense(&RequestSenseBuf, "ReadElementStatus : ");
          if (RequestSenseBuf.SenseKey)
            {
              if (RequestSenseBuf.SenseKey == UNIT_ATTENTION || RequestSenseBuf.SenseKey == NOT_READY)
                {
                  if ( retry < MAX_RETRIES )
                    { 
                      dbprintf(("ReadElementStatus : retry %d\n", retry));
                      retry++;
                      sleep(2);
                    } else {
                      return(RequestSenseBuf.SenseKey);
                    }
                } else {
                  return(RequestSenseBuf.SenseKey);
                }
            }
        }
      if ( ret == 0)
        retry = 0;
    }
    
  ElementStatusData = (ElementStatusData_T *)&DataBuffer[offset];
  DataBufferLength = V3(ElementStatusData->count);
  dbprintf(("DataBufferLength %d, ret %d\n",DataBufferLength, ret));
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
          for (x = 0; x < NoOfElements; x++)
            {
              NewElement(ElementInfo);
              MediumTransportElementDescriptor = (MediumTransportElementDescriptor_T *)&DataBuffer[offset];
              (*ElementInfo)->type = ElementStatusPage->type;
              /*
                (*ElementInfo)->address = V2((char *)MediumTransportElementDescriptor->address);
              */
              (*ElementInfo)->address = V2(MediumTransportElementDescriptor->address);
              (*ElementInfo)->ASC = MediumTransportElementDescriptor->asc;
              (*ElementInfo)->ASCQ = MediumTransportElementDescriptor->ascq;
              (*ElementInfo)->status = (MediumTransportElementDescriptor->full > 0) ? 'F':'E';
              if ((*ElementInfo)->ASC > 0)
                error = 1;
              ElementInfo = &((*ElementInfo)->next);
              offset = offset + V2(ElementStatusPage->length); 
              NoRobot++;
            }
          break;
        case 2:
          for (x = 0; x < NoOfElements; x++)
            {
              
              StorageElementDescriptor = (StorageElementDescriptor_T *)&DataBuffer[offset];
              NewElement(ElementInfo);
              (*ElementInfo)->type = ElementStatusPage->type;
              (*ElementInfo)->address = V2(StorageElementDescriptor->address);
              (*ElementInfo)->ASC = StorageElementDescriptor->asc;
              (*ElementInfo)->ASCQ = StorageElementDescriptor->ascq;
              (*ElementInfo)->status = (StorageElementDescriptor->full > 0) ? 'F':'E';
              if ((*ElementInfo)->ASC > 0)
                error = 1;
              if ((*ElementInfo)->address > MaxSlots)
                MaxSlots = (*ElementInfo)->address;
              if ((*ElementInfo)->address < MinSlots)
                MinSlots = (*ElementInfo)->address;
              ElementInfo = &((*ElementInfo)->next);
              offset = offset + V2(ElementStatusPage->length); 
            }
          break;
        case 4:
          for (x = 0; x < NoOfElements; x++)
            {
              DataTransferElementDescriptor = (DataTransferElementDescriptor_T *)&DataBuffer[offset];
              NewElement(ElementInfo);
              (*ElementInfo)->type = ElementStatusPage->type;
              (*ElementInfo)->address = V2((char *)DataTransferElementDescriptor->address);
              (*ElementInfo)->ASC = DataTransferElementDescriptor->asc;
              (*ElementInfo)->ASCQ = DataTransferElementDescriptor->ascq;
              (*ElementInfo)->status = (DataTransferElementDescriptor->full > 0) ? 'F':'E';
              if ((*ElementInfo)->ASC > 0)
                error = 1;
              TapeDrive = (*ElementInfo)->address;
              NoTapeDrive++;
              ElementInfo = &((*ElementInfo)->next);
              offset = offset + V2(ElementStatusPage->length); 
            }
          break;
        default:
          dbprintf(("ReadElementStatus : UnGknown Type %d\n",ElementStatusPage->type));
          break;
        }
    }
  dbprintf(("\tMinSlots %d, MaxSlots %d\n", MinSlots, MaxSlots));
  dbprintf(("\tTapeDrive %d, Changer %d\n", NoTapeDrive, NoRobot));
  dbprintf(("\tTapeDrive is Element %d\n", TapeDrive));
  for (pwElementInfo = pElementInfo; 
       pwElementInfo; 
       pwElementInfo = (ElementInfo_T *)pwElementInfo->next)
    dbprintf(("\t\tElement #%d %c ASC = %02X ASCQ = %02X Type %d\n",
              pwElementInfo->address, pwElementInfo->status,
              pwElementInfo->ASC, pwElementInfo->ASCQ, pwElementInfo->type ));
  
  return(error);
}

/* ======================================================= */
void NewElement(ElementInfo_T **ElementInfo)
{
  if (*ElementInfo == 0)
    {
      *ElementInfo = (ElementInfo_T *)malloc(sizeof(ElementInfo_T));
      (*ElementInfo)->next = 0;
    }
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
  
  CDB[0] = 0x03;                /* REQUEST SENSE */                       
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


int Move(int fd, int from, int to)
{
  ChangerCMD_T *p = (ChangerCMD_T *)&ChangerIO;
  int ret;
  OpenFiles_T *pwork ;

  if ((pwork = LookupDevice("", fd)) == NULL)
    {
      return(-1);
    }

  if (pwork->SCSI == 0)
    {
      dbprintf(("Move : Device %s not SCSI capable\n", pwork->dev));
      return(-1);
    }

  while(p->ident != NULL)
    {
      if (strcmp(pwork->name, p->ident) == 0)
        {
          ret = p->move(fd, from, to);
          dbprintf(("Move for %s returned %d\n", pwork->name, ret));
          return(ret);
        }
      p++;
    }
  /* Nothing matching found, try generic */
  p = (ChangerCMD_T *)&ChangerIO;
  while(p->ident != NULL)
    {
      if (strcmp("generic", p->ident) == 0)
        {
          ret = p->move(fd, from, to);
          dbprintf(("Move for %s returned %d\n", pwork->name, ret));
          return(ret);
        }
      p++;
    }
  
}

int Status(int fd, ElementInfo_T ** ElementInfo_T)
{
  ChangerCMD_T *p = (ChangerCMD_T *)&ChangerIO;
  int ret;
  int retries = 10;
  OpenFiles_T *pwork ;

  if ((pwork = LookupDevice("", fd)) == NULL)
    {
      return(-1);
    }

  if (pwork->SCSI == 0)
    {
      dbprintf(("Status : Device %s not SCSI capable\n", pwork->dev));
      return(-1);
    }

  dbprintf(("TYPE %s, fd %d, fdin %d\n", pwork->name, pwork->fd, fd));
  while(p->ident != NULL)
    {
      if (strcmp(pwork->name, p->ident) == 0)
        {
          if ((ret = p->status(fd, ElementInfo_T)) != 0 )
            {
              ret = p->resetstatus(fd);
              return(ret);
            }
          return(0);
        }
      p++;
    }
  /* Nothing matching found, try generic */
  p = (ChangerCMD_T *)&ChangerIO;
  while(p->ident != NULL)
    {
      if (strcmp("generic", p->ident) == 0)
        {
          if ((ret = p->status(fd, ElementInfo_T)) != 0 ) 
            {
              ret=p->resetstatus(fd);
              return(ret);
            } 
          return(0);
        }
      p++;
    }     
}

int Eject(char *tapedev)
{
  ChangerCMD_T *p = (ChangerCMD_T *)&ChangerIO;
  OpenFiles_T *pwork ;

  if ((pwork = LookupDevice(tapedev, 0)) == NULL)
    {
      return(-1);
    }

  if (pwork->SCSI == 0)
    {
      dbprintf(("Eject : Device %s not SCSI capable\n", pwork->dev));
      return(-1);
    }

  while(p->ident != NULL)
    {
      if (strcmp(pwork->name, p->ident) == 0)
        {
          p->eject(pwork->fd);
          return(0);
        }
      p++;
    }
  /* Nothing matching found, try generic */
  p = (ChangerCMD_T *)&ChangerIO;
  while(p->ident != NULL)
    {
      if (strcmp("generic", p->ident) == 0)
        {
          p->eject(pwork->fd);
          return(0);
        }
      p++;
    }
}


int Clean(char *tapedev)
{
  int ret;
  ChangerCMD_T *p = (ChangerCMD_T *)&ChangerIO;
  OpenFiles_T *pwork ;

  if ((pwork = LookupDevice(tapedev, 0)) == NULL)
    {
      return(-1);
    }

  if (pwork->SCSI == 0)
    {
      dbprintf(("Clean : Device %s not SCSI capable\n", pwork->dev));
      return(-1);
    }

  while(p->ident != NULL)
    {
      if (strcmp(pwork->name, p->ident) == 0)
        {
          ret=p->clean(tapedev);
          return(ret);
        }
      p++;
    }
  /* Nothing matching found, try generic */
  p = (ChangerCMD_T *)&ChangerIO;
  while(p->ident != NULL)
    {
      if (strcmp("generic", p->ident) == 0)
        {
          ret=p->clean(tapedev);
          return(ret);
        }
      p++;
    }   
}

int isempty(int fd, int slot)
{
  int rslot;
  Status(fd, &pElementInfo);
  rslot = slot + MinSlots;
  
  for (pwElementInfo = pElementInfo; pwElementInfo != NULL ; pwElementInfo = pwElementInfo->next) {
    if (pwElementInfo->address == rslot)
      {
        /*
          fprintf(stderr, "Element #%d %c ASC = %02X ASCQ = %02X Type %d\n",
          pwElementInfo->address, pwElementInfo->status,
          pwElementInfo->ASC, pwElementInfo->ASCQ, pwElementInfo->type );
        */
        if(pwElementInfo->status == 'E')
          return(1);
        return(0);
      }
  }
}

int get_clean_state(char *tapedev)
{
  /* Return 1 it cleaning is needed */
  int ret;
  ret=Clean(tapedev);
  return(ret);    
}

int eject_tape(char *tapedev)
  /* This function ejects the tape from the drive */
{
  int ret = 0;

  ret=Eject(tapedev);
  return(ret);    
}


int find_empty(int fd)
{
  dbprintf(("find_empty %d\n", fd));
}

int drive_loaded(int fd, int drivenum)
{
  
  dbprintf(("\ndrive_loaded : fd %d drivenum %d \n", fd, drivenum));
  Status(fd, &pElementInfo);
  for (pwElementInfo = pElementInfo; pwElementInfo; pwElementInfo = pwElementInfo->next) {
    if (pwElementInfo->address == TapeDrive)
      {
        dbprintf(("drive_loaded : Element #%d %c ASC = %02X ASCQ = %02X Type %d\n",
                  pwElementInfo->address, pwElementInfo->status,
                  pwElementInfo->ASC, pwElementInfo->ASCQ, pwElementInfo->type));
        if(pwElementInfo->status == 'E')
          {
            return(0);
          } else {
            return(1);
          }
      }
  }
  /*
   * check the status of the transport (tape drive).
   *
   * return 1 if the drive is occupied, 0 otherwise.
   */
  return(0);
}



int unload(int fd, int drive, int slot)
{
  /*
   * unload the specified drive into the specified slot
   * (storage element)
   */
  int empty = 0;
  
  Status(fd, &pElementInfo);    
  for (pwElementInfo = pElementInfo; pwElementInfo; pwElementInfo = pwElementInfo->next) {
    if (pwElementInfo->status == 'E' && pwElementInfo->type == STORAGE )
      empty=pwElementInfo->address;
  }
  if (empty == slot+MinSlots) {
    Move(fd, TapeDrive, slot+MinSlots);
  } else {
    Move(fd, TapeDrive, empty);
  }
  return(0);
}

int load(int fd, int drive, int slot)
{
  int ret;
  
  Status(fd, &pElementInfo);
  ret = Move(fd, slot+MinSlots, TapeDrive);
  return(ret);
  /*
   * load the media from the specified element (slot) into the
   * specified data transfer unit (drive)
   */
}


int get_slot_count(int fd)
{
  Status(fd, &pElementInfo);
  return(MaxSlots-MinSlots);
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
  Status(fd, &pElementInfo);
  
  for (pwElementInfo = pElementInfo; pwElementInfo; pwElementInfo = pwElementInfo->next) {
    if (pwElementInfo->type == TAPETYPE)
      {
        count++;
      }
  }
  
  return(count);
}


int LogSense(DeviceFD)
{
  CDB_T CDB;
  RequestSense_T RequestSense;
  extern OpenFiles_T *pOpenFiles;
  OpenFiles_T *pwork;
  LogSenseHeader_T *LogSenseHeader;
  LogParameter_T *LogParameter;
  struct LogPageDecode *p;
  int found;
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
      buffer = (char *)malloc(size);
      bzero(buffer, size);
      /*
       * Get the known log pages 
       */

      CDB[0] = 0x4d;
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
                              buffer, size, 
                              (char *) &RequestSense,
                              sizeof(RequestSense_T)) != 0)
        {
          DecodeSense(&RequestSense, "LogSense : ");
          return;
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
          return(-1);
        }

      LogSenseHeader = (LogSenseHeader_T *)buffer;
      nologpages = V2(LogSenseHeader->PageLength);
      logpages = (char *)malloc(nologpages);
  
      memcpy(logpages, buffer + sizeof(LogSenseHeader_T), nologpages);
  
      for (count = 0; count < nologpages; count++) {
        if (logpages[count] != 0  ) {
          bzero(buffer, size);
      
      
          CDB[0] = 0x4d;
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
                                  (char *) &RequestSense, sizeof(RequestSense_T)) != 0)
            { 
              DecodeSense(&RequestSense, "LogSense : ");
              return;
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
            if ((strcmp(pwork->name, p->ident) == 0 ||strcmp("*", p->ident) == 0)  && p->LogPage == logpages[count]) {
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

/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-file-style: gnu
 * End:
 */
