#ifndef lint
static char rcsid[] = "$Id: scsi-linux.c,v 1.3 1998/11/17 20:20:10 martinea Exp $";
#endif
/*
 * Interface to execute SCSI commands on Linux
 *
 * Copyright (c) 1998 T.Hepper
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_LINUX_LIKE_SCSI

/*
#ifdef HAVE_STDIO_H
*/
#include <stdio.h>
/*
#endif
*/
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <time.h>

#include <scsi/scsi_ioctl.h>
#include <sys/mtio.h>
#include <scsi-defs.h>

extern int Move(char *,int, int, int);
extern int Status(char *, int);
extern int Eject(char *, int);
extern int Clean(char *, int);

extern SCSIInquiry_T *SCSIInquiry;
extern ElementInfo_T *pElementInfo;
extern ElementInfo_T *pwElementInfo;

extern int MinSlots;
extern int MaxSlots;
extern int TapeDrive;
extern char SCSIIdent;
extern int MoveElement(int, int,int);
extern int Inquiry(int);
extern int PrintInquiry();
extern struct ChangerCMD *ChangerIO;

static inline int min(int x, int y)
{
  return (x < y ? x : y);
}


static inline int max(int x, int y)
{
  return (x > y ? x : y);
}
 
int SCSI_OpenDevice(char *DeviceName)
{
  int DeviceFD = open(DeviceName, O_RDONLY);
  if (DeviceFD < 0)
    printf("cannot open SCSI device '%s' - %m\n", DeviceName);
  return DeviceFD;
}


void SCSI_CloseDevice(char *DeviceName,
			     int DeviceFD)
{
  if (close(DeviceFD) < 0)
    printf("cannot close SCSI device '%s' - %m\n", DeviceName);
}


int SCSI_ExecuteCommand(int DeviceFD,
			       Direction_T Direction,
			       CDB_T CDB,
			       int CDB_Length,
			       void *DataBuffer,
			       int DataBufferLength,
  			       char *RequestSense,
                               int RequestSenseLength)
{
  unsigned char *Command;
  int Zero = 0, Result;
  memset(RequestSense, 0, sizeof(RequestSense_T));
  switch (Direction)
    {
    case Input:
      Command = (unsigned char *)
	malloc(8 + max(DataBufferLength, RequestSenseLength));
      memcpy(&Command[0], &Zero, 4);
      memcpy(&Command[4], &DataBufferLength, 4);
      memcpy(&Command[8], CDB, CDB_Length);
      break;
    case Output:
      Command = (unsigned char *)
	malloc(8 + max(CDB_Length + DataBufferLength, RequestSenseLength));
      memcpy(&Command[0], &DataBufferLength, 4);
      memcpy(&Command[4], &Zero, 4);
      memcpy(&Command[8], CDB, CDB_Length);
      memcpy(&Command[8 + CDB_Length], DataBuffer, DataBufferLength);
      break;
    }
  Result = ioctl(DeviceFD, SCSI_IOCTL_SEND_COMMAND, Command);
  if (Result != 0)
    memcpy(RequestSense, &Command[8], RequestSenseLength);
  else if (Direction == Input)
    memcpy(DataBuffer, &Command[8], DataBufferLength);
  free(Command);
  return Result;
}

int Tape_Eject ( int DeviceFD)
{
  struct mtop mtop;
  
  mtop.mt_op = MTUNLOAD;
  mtop.mt_count = 1;
  ioctl(DeviceFD, MTIOCTOP, &mtop);
  return;
}

int Tape_Status( int DeviceFD)
{
	struct mtget mtget;
	int true = 1;
	bzero(&mtget, sizeof(struct mtget));

	if(ioctl(DeviceFD,  MTIOCGET, &mtget) != -1)
	{
	} else {
	}
}

int Tape_Ready(char *tapedev, char * changerdev, int changerfd, int wait)
{
  struct mtget mtget;
  int true = 1;
  int cnt;
  int DeviceFD;

  if (strcmp(tapedev, changerdev) == 0) 
    {
      DeviceFD = changerfd;
    } else {
      if ((DeviceFD = SCSI_OpenDevice(tapedev)) < 0)
	{
	  sleep(wait);
	  return;
	}
    }

  bzero(&mtget, sizeof(struct mtget));
  while (true && cnt < wait)
    {
      if(ioctl(DeviceFD,  MTIOCGET, &mtget) != -1)
        {
	  if (GMT_ONLINE(mtget.mt_gstat))
	    {
	      true=0;
	    }
        }
      cnt++;
    }

  if (strcmp(tapedev,changerdev) != 0)
    SCSI_CloseDevice(tapedev, DeviceFD);
}

int SCSI_Scan()
{
  printf("Not available \n");
}
#endif
