#ifndef lint
static char rcsid[] = "$Id: scsi-irix.c,v 1.1.2.3 1998/11/18 07:03:45 oliva Exp $";
#endif
/*
 * Interface to execute SCSI commands on an SGI Workstation
 *
 * Copyright (c) 1998 T.Hepper th@icem.de
 */
#include <amanda.h>

#ifdef HAVE_IRIX_LIKE_SCSI

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

#include <sys/scsi.h>
#include <sys/dsreq.h>
#include <sys/mtio.h>
#include <scsi-defs.h>


int SCSI_OpenDevice(char *DeviceName)
{
  int DeviceFD = open(DeviceName, O_RDONLY); 
  /*
    if (DeviceFD < 0) 
    printf("cannot open SCSI device '%s' - %m\n", DeviceName);
  */
  return DeviceFD; 
}

void SCSI_CloseDevice(char *DeviceName,
		      int DeviceFD)
{
  close(DeviceFD) ;
  /*
    if (close(DeviceFD) < 0)
    printf("cannot close SCSI device '%s' - %m\n", DeviceName);
  */
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
  struct dsreq ds;
  int Zero = 0, Result;
  int retries = 5;
  
  memset(&ds, 0, sizeof(struct dsreq));
  memset(RequestSense, 0, RequestSenseLength);
  
  ds.ds_flags = DSRQ_SENSE|DSRQ_ASYNXFR; 
  /* Timeout */
  ds.ds_time = 120000;
  /* Set the cmd */
  ds.ds_cmdbuf = (caddr_t)CDB;
  ds.ds_cmdlen = CDB_Length;
  /* Data buffer for results */
  ds.ds_databuf = (caddr_t)DataBuffer;
  ds.ds_datalen = DataBufferLength;
  /* Sense Buffer */
  ds.ds_sensebuf = (caddr_t)RequestSense;
  ds.ds_senselen = RequestSenseLength;
  
  switch (Direction) 
    {
    case Input:
      ds.ds_flags = ds.ds_flags | DSRQ_READ;
      break;
    case Output:
      ds.ds_flags = ds.ds_flags | DSRQ_WRITE;
      break;
    }

  while (--retries > 0) {
    Result = ioctl(DeviceFD, DS_ENTER, &ds);
    if (Result < 0)
      {
	RET(&ds) = DSRT_DEVSCSI;
	return (-1);
      }
    switch (STATUS(&ds))
      {
      case ST_BUSY:                /*  BUSY */
	break;
      case 0x18:                   /*  RESERV CONFLICT */
	if (retries > 0)
	  sleep(2);
	continue;
      case ST_GOOD:                /*  GOOD */
	switch (RET(&ds))
	  {
	  case DSRT_SHORT:
	    return(ST_GOOD);
	    break;
	  case DSRT_OK:
	  default:
	    return(STATUS(&ds));
	  }
      case ST_CHECK:               /*  CHECK CONDITION */ 
	return(ST_CHECK);
	break;
      case ST_COND_MET:            /*  INTERM/GOOD */
      default:
	continue;
      }
  }	
  return(STATUS(&ds));
}

int SCSI_Scan()
{
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

int Tape_Ready(char *tapedev, char *changerdev, int changerfd, int wait)
{

  struct mtget mtget;
  int true = 1;
  int cnt;
  int DeviceFD;
  
  if (strcmp(tapedev, changerdev) == 0) {
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
	  if (mtget.mt_dposn & MT_ONL)
	    {
	      true=0;
	    }
	}
      sleep(1);
      cnt++;
    }

  if (strcmp(tapedev, changerdev) != 0)
    SCSI_CloseDevice(tapedev, DeviceFD);
}
#endif
