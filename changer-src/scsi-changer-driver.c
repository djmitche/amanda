#ifndef lint
static char rcsid[] = "$Id: scsi-changer-driver.c,v 1.5 1998/12/14 07:55:23 oliva Exp $";
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

 

int Inquiry(int);
int PrintInquiry();
int Move(char *,int, int, int);
int Status(char *, int, ElementInfo_T **);
int Eject(char *, int);
int Clean(char *, int);
int ReadElementStatus(int DeviceFD, ElementInfo_T **ElementInfo);
int ResetStatus(int DeviceFD);
int DoNothing();
int GenericMove(int, int, int);
int GenericStatus();
int GenericFree();
int GenericEject();
int GenericClean(int);
int RequestSense(int, ExtendedRequestSense_T *, int  );

struct ChangerCMD ChangerIO[] = {
    {"C1553A",
     GenericMove,
     ReadElementStatus,
     DoNothing,
     GenericFree,
     GenericEject,
     GenericClean},
    {"EXB-10e",		/* Exabyte Robot */
     GenericMove,
     ReadElementStatus,
     ResetStatus,
     GenericFree,
     GenericEject,
     GenericClean},
    {"EXB-85058HE-0000",	/* Exabyte TapeDrive */
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
				   
SCSIInquiry_T *SCSIInquiry;
char SCSIIdent[16];
char *MediumChanger;
char *TapeDevice;
int MediumChangerFD;
int TapeDeviceFD;
int MaxSlots = 0;
int MinSlots = 99;
int TapeDrive = 0;
int NoTapeDrive = 0;
int NoRobot = 0;
char *SlotArgs = 0;
ElementInfo_T *pElementInfo = 0;
ElementInfo_T *pwElementInfo = 0;
 

int Inquiry(int DeviceFD)
{
    CDB_T CDB;
    RequestSense_T *pRequestSense;
    int i;
    int lun;
    int ret;

    dbprintf(("Inquiry start:\n"));
    pRequestSense = (RequestSense_T *)malloc(sizeof(RequestSense_T));

    SCSIInquiry = (SCSIInquiry_T *)malloc(sizeof(SCSIInquiry_T));
    bzero(SCSIInquiry,sizeof(SCSIInquiry_T));
    CDB[0] = 0x12;   /* Inquiry command */
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

    for (i=0;i < 16 && SCSIInquiry->prod_ident[i] != ' ';i++)
      SCSIIdent[i] = SCSIInquiry->prod_ident[i];
    SCSIIdent[i] = '\0';
    
    dbprintf(("Inquiry end: %d\n", ret));
    return(ret);
}

int DecodeSense(RequestSense_T *sense, char *pstring)
{
  dbprintf(("%sSense Keys\n", pstring));
  dbprintf(("\tErrorCode                     %02x\n", sense->ErrorCode));
  dbprintf(("\tValid                         %d\n", sense->Valid));
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

int PrintInquiry()
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

int GenericEject()
{
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
      /* 	fprintf(stderr, "%s: Request Sense[Inquiry]: %02X", */
      /* 		"chs", ((unsigned char *) &pRequestSense)[0]); */
      /* 	for (i = 1; i < sizeof(RequestSense_T); i++)                */
      /* 	  fprintf(stderr, " %02X", ((unsigned char *) &pRequestSense)[i]); */
      /* 	fprintf(stderr, "\n");    */
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
  if (Status((char *)&SCSIIdent, DeviceFD,&pElementInfo) > 0)
   {
     dbprintf(("GenericMove : ResetStatus\n"));
     ret = ResetStatus(DeviceFD);
     if (ret != 0)
       return(ret);
   }

  CDB[0]  = 0xA5;
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
/* ====================================================== */
int UnloadTape(ElementInfo_T *ElementInfo, int MediumChangerFD, int TapeDeviceFD)
{
  ElementInfo_T *pElementInfo;
  int CHMAvailable = 0;
  int CHMUsed = 0;
  int TapeUsed = 0;
  int SlotFree = 0;
  int from ;
  
  for (pElementInfo = ElementInfo; pElementInfo; pElementInfo = pElementInfo->next)
    {
      switch (pElementInfo->type)
	{
	case 1:
	  CHMAvailable = 1;
	  if (pElementInfo->status == 'F')
	    CHMUsed = pElementInfo->address;
	  break;
	case 2:
	  if (pElementInfo->status == 'E')
	    SlotFree = pElementInfo->address;
	  break;
	case 4:
	  if (pElementInfo->status == 'F')
	    TapeUsed = 1;
	  break;
	}
    }
  if (TapeUsed == 1 && SlotFree > 0)
    {
      if (CHMAvailable > 0)
	Tape_Eject(TapeDeviceFD);
      GenericMove(MediumChangerFD, TapeDrive, SlotFree);
    }
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
	  	dbprintf(("ReadElementStatus : Unknown Type %d\n",ElementStatusPage->type));
	  	break;
	}
    }
  dbprintf(("\tMinSlots %d, MaxSlots %d\n", MinSlots, MaxSlots));
  dbprintf(("\tTapeDrive %d, Changer %d\n", NoTapeDrive, NoRobot));
  dbprintf(("\tTapeDrive is Element %d\n", TapeDrive));
  for (pwElementInfo = pElementInfo; 
       pwElementInfo; 
       pwElementInfo = pwElementInfo->next)
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


int Move(char *ident, int fd, int from, int to)
{
  struct ChangerCMD *p = (struct ChangerCMD *)&ChangerIO;
  int ret;
  
  while(p->ident != NULL)
    {
	    if (strcmp(ident, p->ident) == 0)
	      {
		ret = p->move(fd, from, to);
		dbprintf(("Move for %s returned %d\n", ident, ret));
		return(ret);
	      }
	    p++;
    }
    /* Nothing matching found, try generic */
  p = (struct ChangerCMD *)&ChangerIO;
  while(p->ident != NULL)
    {
      if (strcmp("generic", p->ident) == 0)
	{
	  ret = p->move(fd, from, to);
	  dbprintf(("Move for %s returned %d\n", ident, ret));
	  return(ret);
	}
      p++;
    }
  
}

int Status(char *ident, int fd, ElementInfo_T ** ElementInfo_T)
{
  struct ChangerCMD *p = (struct ChangerCMD *)&ChangerIO;
  int ret;
  int retries = 10;
  
  while(p->ident != NULL)
    {
      if (strcmp(ident, p->ident) == 0)
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
  p = (struct ChangerCMD *)&ChangerIO;
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

int Eject(char *ident, int fd)
{
  struct ChangerCMD *p = (struct ChangerCMD *)&ChangerIO;
  
  while(p->ident != NULL)
    {
      if (strcmp(ident, p->ident) == 0)
	{
	  p->eject(fd);
	  return(0);
	}
      p++;
    }
  /* Nothing matching found, try generic */
  p = (struct ChangerCMD *)&ChangerIO;
  while(p->ident != NULL)
    {
      if (strcmp("generic", p->ident) == 0)
	{
	  p->eject(fd);
	  return(0);
	}
      p++;
    }
}


int Clean(char *ident, int tapedev)
{
  int ret;
  struct ChangerCMD *p = (struct ChangerCMD *)&ChangerIO;
  
  while(p->ident != NULL)
    {
      if (strcmp(ident, p->ident) == 0)
	{
	  ret=p->clean(tapedev);
	  return(ret);
	}
      p++;
    }
  /* Nothing matching found, try generic */
  p = (struct ChangerCMD *)&ChangerIO;
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
  Inquiry(fd);
  Status((char *)&SCSIIdent, fd, &pElementInfo);
  rslot = slot + MinSlots;
  
  for (pwElementInfo = pElementInfo; pwElementInfo; pwElementInfo = pwElementInfo->next) {
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

int get_clean_state(int changerfd, char * changerdev, char *tapedev)
{
  /* Return 1 it cleaning is needed */
  int fd;
  int ret;
  if (strcmp(changerdev,tapedev) == 0) {
    Inquiry(changerfd);
    ret=Clean((char *)&SCSIIdent, changerfd);
    return(ret);    
  }
  if ((fd = SCSI_OpenDevice(tapedev)) > 0)
    {
      Inquiry(fd);
      ret=Clean((char *)&SCSIIdent, fd);
      SCSI_CloseDevice(tapedev, fd);
      return(ret);
    }
  return(0);
}

void eject_tape(char *tape)
     /* This function ejects the tape from the drive */
{
  int fd;
  
  if ((fd = SCSI_OpenDevice(tape)) > 0)
    {
      Tape_Eject(fd);
      SCSI_CloseDevice(tape, fd);
    }
}


int find_empty(int fd)
{
  printf("find_empty %d\n", fd);
}

int drive_loaded(int fd, int drivenum)
{
  
  dbprintf(("drive_loaded : fd %d drivenum %d \n", fd, drivenum));
  Inquiry(fd);
  Status((char *)&SCSIIdent, fd, &pElementInfo);
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
  
  Inquiry(fd);
  Status((char *)&SCSIIdent, fd, &pElementInfo);    
  for (pwElementInfo = pElementInfo; pwElementInfo; pwElementInfo = pwElementInfo->next) {
    if (pwElementInfo->status == 'E' && pwElementInfo->type == STORAGE )
      empty=pwElementInfo->address;
  }
  if (empty == slot+MinSlots) {
    Move((char *)&SCSIIdent, fd, TapeDrive, slot+MinSlots);
  } else {
    Move((char *)&SCSIIdent, fd, TapeDrive, empty);
  }
  return(0);
}

int load(int fd, int drive, int slot)
{
  int ret;
  
  Inquiry(fd);
  Status((char *)&SCSIIdent, fd, &pElementInfo);
  ret = Move((char *)&SCSIIdent, fd, slot+MinSlots, TapeDrive);
  return(ret);
  /*
   * load the media from the specified element (slot) into the
   * specified data transfer unit (drive)
   */
}


int get_slot_count(int fd)
{
  Inquiry(fd);
  Status((char *)&SCSIIdent, fd, &pElementInfo);
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
  Inquiry(fd);
  Status((char *)&SCSIIdent, fd, &pElementInfo);
  
  for (pwElementInfo = pElementInfo; pwElementInfo; pwElementInfo = pwElementInfo->next) {
    if (pwElementInfo->type == TAPETYPE)
      {
	count++;
      }
  }
  
  return(count);
}
