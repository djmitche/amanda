#ifndef lint
static char rcsid[] = "$Id: scsi-changer-driver.c,v 1.1.2.3 1998/11/18 07:03:34 oliva Exp $";
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

#ifdef __linux
#define LITTLE_ENDIAN_BITFIELDS
#endif
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
int ExabyteClean(int);
 
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
    RequestSense_T RequestSense;
    int i;
    int lun;
    int ret;

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
			      (char *) &RequestSense,
			      sizeof(RequestSense_T));
    if (ret < 0)
      {
	dbprintf(("%s: Request Sense[Inquiry]: %02X",
		"chs", ((unsigned char *) &RequestSense)[0]));
	for (i = 1; i < sizeof(RequestSense_T); i++)               
	  dbprintf((" %02X", ((unsigned char *) &RequestSense)[i]));
	dbprintf(("\n"));   
	return(ret);
      }
    if ( ret > 0)
      return(RequestSense.SenseKey);

    for (i=0;i < 16 && SCSIInquiry->prod_ident[i] != ' ';i++)
      SCSIIdent[i] = SCSIInquiry->prod_ident[i];
    SCSIIdent[i] = '\0';
    
    return(ret);
}

int PrintInquiry()
{
    dbprintf((stderr,"%-15s %x\n", "qualifier", SCSIInquiry->qualifier));
    dbprintf((stderr,"%-15s %x\n", "type", SCSIInquiry->type));
    dbprintf((stderr,"%-15s %x\n", "data_format", SCSIInquiry->data_format));
    dbprintf((stderr,"%-15s %X\n", "ansi_version", SCSIInquiry->ansi_version));
    dbprintf((stderr,"%-15s %X\n", "ecma_version", SCSIInquiry->ecma_version));
    dbprintf((stderr,"%-15s %X\n", "iso_version", SCSIInquiry->iso_version));
    dbprintf((stderr,"%-15s %X\n", "type_modifier", SCSIInquiry->type_modifier));
    dbprintf((stderr,"%-15s %x\n", "removable", SCSIInquiry->removable));
    dbprintf((stderr,"%-15s %.8s\n", "vendor_info", SCSIInquiry->vendor_info));
    dbprintf((stderr,"%-15s %.16s\n", "prod_ident", SCSIInquiry->prod_ident));
    dbprintf((stderr,"%-15s %.4s\n", "prod_version", SCSIInquiry->prod_version));
    dbprintf((stderr,"%-15s %.20s\n", "vendor_specific", SCSIInquiry->vendor_specific));
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
int ExabyteClean(int TapeFd)
{
	ExtendedRequestSense_T ExtRequestSense;
	RequestSense(TapeFd, &ExtRequestSense, 0);	
	if(ExtRequestSense.CLN) {
		return(1);
	} else {
		return(0);
	}
}


static int Endian16(unsigned char *BigEndianData)
{
  return (BigEndianData[0] << 8) + BigEndianData[1];
}

static int Endian24(unsigned char *BigEndianData)
{
  return (BigEndianData[0] << 16) + (BigEndianData[1] << 8) + BigEndianData[2];
}




/* Function Prototypes */
/* =================== */
void NewElement(ElementInfo_T **ElementInfo);

int ResetStatus(int DeviceFD)
{
  CDB_T CDB;
  RequestSense_T RequestSense;
  int i;
  int ret;

  CDB[0] = 0x7;   /* */
  CDB[1] = 0;
  CDB[2] = 0;
  CDB[3] = 0;
  CDB[4] = 0;
  CDB[5] = 0;


  ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 6,
			    0, 0,
			    (char *) &RequestSense,
			    sizeof(RequestSense_T));

  if (ret < 0)
    {
      /* 	fprintf(stderr, "%s: Request Sense[Inquiry]: %02X", */
      /* 		"chs", ((unsigned char *) &RequestSense)[0]); */
      /* 	for (i = 1; i < sizeof(RequestSense_T); i++)                */
      /* 	  fprintf(stderr, " %02X", ((unsigned char *) &RequestSense)[i]); */
      /* 	fprintf(stderr, "\n");    */
      return(ret);
    }
  if ( ret > 0)
    return(RequestSense.SenseKey);
  
  return(ret);
}

int EXBMove(int DeviceFD, int from, int to)
{
    CDB_T CDB;
    RequestSense_T RequestSense;
    int Result;
    int i;

    if (Status((char *)&SCSIIdent, DeviceFD,&pElementInfo) > 0)
	{
	   ResetStatus(DeviceFD);
	}
}

/* ======================================================= */
int GenericMove(int DeviceFD, int from, int to)
{
  CDB_T CDB;
  RequestSense_T RequestSense;
  int ret;
  int i;

  dbprintf(("GenericMove from = %d, to = %d\n", from, to));

  CDB[0]  = 0xA5;
  CDB[1]  = 0;
  CDB[2]  = 0;
  CDB[3]  = 0;     /* Address of CHM */
  CDB[4]  = (from >> 8) & 0xFF;
  CDB[5]  = from & 0xFF;
  CDB[6]  = (to >> 8) & 0xFF;
  CDB[7]  = to & 0xFF;
  CDB[8]  = 0;
  CDB[9]  = 0;
  CDB[10] = 0;
  CDB[11] = 0;
 
  ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 12,
	0, 0, (char *) &RequestSense, sizeof(RequestSense_T)); 

  dbprintf(("GenericMove SCSI_ExecuteCommand = %d\n", ret));

  if (ret < 0)
    {
      dbprintf(("%s: Request Sense[Inquiry]: %02X", 
	      "chs", ((unsigned char *) &RequestSense)[0])); 
      for (i = 1; i < sizeof(RequestSense_T); i++)                
	dbprintf((stderr, " %02X", ((unsigned char *) &RequestSense)[i])); 
      dbprintf(("\n"));    
      return(ret);
    }
  if ( ret > 0)
    return(RequestSense.SenseKey);
  
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
  RequestSense_T RequestSense;
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

  MinSlots = 99;
  MaxSlots = 0;
  NoTapeDrive = 0;
  NoRobot = 0;
 
  dbprintf(("ReadElementStatus ....\n"));

  CDB[0] = 0xB8;                /* READ ELEMENT STATUS */
  CDB[1] = 0;                   /* Element Type Code = 0, VolTag = 0 */
  CDB[2] = 0;                   /* Starting Element Address MSB */   
  CDB[3] = 0;                   /* Starting Element Address LSB */   
  CDB[4] = 0;                   /* Number Of Elements MSB */             
  CDB[5] = 255;                 /* Number Of Elements LSB */                   
  CDB[6] = 0;                   /* Reserved */                                 
  CDB[7] = 0;                   /* Allocation Length MSB */                    
  CDB[8] = (MaxReadElementStatusData >> 8) & 0xFF; /* Allocation Length */    
  CDB[9] = MaxReadElementStatusData & 0xFF; /* Allocation Length LSB */      
  CDB[10] = 0;                  /* Reserved */                                
  CDB[11] = 0;                  /* Control */                                  



  ret = SCSI_ExecuteCommand(DeviceFD, Input, CDB, 12,                      
                          DataBuffer, sizeof(DataBuffer), 
                          (char *) &RequestSense, sizeof(RequestSense_T));


  if (ret < 0)
    {
      dbprintf(("%s: Request Sense[Inquiry]: %02X",
	      "chs", ((unsigned char *) &RequestSense)[0]));
      for (i = 1; i < sizeof(RequestSense_T); i++)               
	dbprintf((" %02X", ((unsigned char *) &RequestSense)[i]));
      dbprintf(("\n"));   
      return(ret);
    }
  if ( ret > 0)
    return(RequestSense.SenseKey);
  
  
  ElementStatusData = (ElementStatusData_T *)&DataBuffer[offset];
  DataBufferLength = Endian24(ElementStatusData->count);
  offset = sizeof(ElementStatusData_T);
  while (offset < DataBufferLength) 
    {
      ElementStatusPage = (ElementStatusPage_T *)&DataBuffer[offset];
      NoOfElements = Endian24(ElementStatusPage->count) / sizeof(StorageElementDescriptor_T);
     offset = offset + sizeof(ElementStatusPage_T);
      switch (ElementStatusPage->type)
	{
	case 1:
	  for (x = 0; x < NoOfElements; x++)
	    {
	      NewElement(ElementInfo);
	      MediumTransportElementDescriptor = (MediumTransportElementDescriptor_T *)&DataBuffer[offset];
	      (*ElementInfo)->type = ElementStatusPage->type;
	      (*ElementInfo)->address = Endian16(MediumTransportElementDescriptor->address);
	      (*ElementInfo)->ASC = MediumTransportElementDescriptor->asc;
	      (*ElementInfo)->ASCQ = MediumTransportElementDescriptor->ascq;
	      (*ElementInfo)->status = (MediumTransportElementDescriptor->full > 0) ? 'F':'E';
	      if ((*ElementInfo)->ASC > 0)
		error = 1;
	      ElementInfo = &((*ElementInfo)->next);
	      offset = offset + sizeof(MediumTransportElementDescriptor_T);
          NoRobot++;
	    }
	  break;
	case 2:
	  for (x = 0; x < NoOfElements; x++)
	    {
	      
	      StorageElementDescriptor = (StorageElementDescriptor_T *)&DataBuffer[offset];
	      NewElement(ElementInfo);
	      (*ElementInfo)->type = ElementStatusPage->type;
	      (*ElementInfo)->address = Endian16(StorageElementDescriptor->address);
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
	      offset = offset + sizeof(StorageElementDescriptor_T);
	    }
	  break;
	case 4:
	  for (x = 0; x < NoOfElements; x++)
	    {
	      DataTransferElementDescriptor = (DataTransferElementDescriptor_T *)&DataBuffer[offset];
	      NewElement(ElementInfo);
	      (*ElementInfo)->type = ElementStatusPage->type;
	      (*ElementInfo)->address = Endian16(DataTransferElementDescriptor->address);
	      (*ElementInfo)->ASC = DataTransferElementDescriptor->asc;
	      (*ElementInfo)->ASCQ = DataTransferElementDescriptor->ascq;
	      (*ElementInfo)->status = (DataTransferElementDescriptor->full > 0) ? 'F':'E';
	      if ((*ElementInfo)->ASC > 0)
		error = 1;
	      TapeDrive = (*ElementInfo)->address;
          NoTapeDrive++;
	      ElementInfo = &((*ElementInfo)->next);
	      offset = offset + sizeof(DataTransferElementDescriptor_T);
	    }
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


/* ======================================================= */
/* Added 11.05.98 Wolfgang Bressgott */
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
      dbprintf(("%s: Request Sense[Inquiry]: %02X",
       		"chs", ((unsigned char *) &RequestSense)[0]));
       	for (i = 1; i < sizeof(RequestSense_T); i++)               
       	  dbprintf((" %02X", ((unsigned char *) &RequestSense)[i]));
       	dbprintf(("\n"));   
      return(ret);
    }
  if ( ret > 0)
    return(RequestSense.SenseKey);
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
  
  while(p->ident != NULL)
    {
      if (strcmp(ident, p->ident) == 0)
	{
	  if(p->status(fd, ElementInfo_T) != 0) {
	    p->resetstatus(fd);
	    if(p->status(fd, ElementInfo_T) != 0) 
	      return(1);
	    return(0);
	  }
	}
      p++;
    }
  /* Nothing matching found, try generic */
  p = (struct ChangerCMD *)&ChangerIO;
  while(p->ident != NULL)
    {
      if (strcmp("generic", p->ident) == 0)
	{
	  if(p->status(fd, ElementInfo_T) != 0) {
	    p->resetstatus(fd);
	    if(p->status(fd, ElementInfo_T) != 0) {
	      return(1);
	    }
	  }
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
    
    Inquiry(fd);
    Status((char *)&SCSIIdent, fd, &pElementInfo);
    for (pwElementInfo = pElementInfo; pwElementInfo; pwElementInfo = pwElementInfo->next) {
	if (pwElementInfo->address == TapeDrive)
	    {
		/*
		fprintf(stderr, "Element #%d %c ASC = %02X ASCQ = %02X Type %d\n",
			pwElementInfo->address, pwElementInfo->status,
			pwElementInfo->ASC, pwElementInfo->ASCQ, pwElementInfo->type );
		*/
		if(pwElementInfo->status == 'E')
		    return(0);
		return(1);
	    }
    }
   /*
     * check the status of the transport (tape drive).
     *
     * return 1 if the drive is occupied, 0 otherwise.
     */
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
    /*
     * retreive the number of data-transfer devices
     */
}
