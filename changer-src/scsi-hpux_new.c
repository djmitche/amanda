#ifndef lint
static char rcsid[] = "$Id: scsi-hpux_new.c,v 1.1 1998/11/07 08:49:22 oliva Exp $";
#endif
/*
 * Interface to execute SCSI commands on an HP-UX Workstation
 *
 * Copyright (c) 1998 T.Hepper
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef HAVE_HPUX_LIKE_SCSI

# ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
/*
#ifdef HAVE_STDIO_H
*/
#include <stdio.h>
/*
#endif
*/
#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#ifdef HAVE_SYS_SCSI_H
#include <sys/scsi.h>
#endif
#include <sys/mtio.h>
#include <scsi-defs.h>

/* ======================================================= */
int SCSI_OpenDevice(char *DeviceName)
{
  int DeviceFD = open(DeviceName, O_RDWR| O_NDELAY); 
/*   if (DeviceFD < 0)  */
/*     printf("cannot open SCSI device '%s'\n", DeviceName); */
  return DeviceFD; 
}

/* ======================================================= */
void SCSI_CloseDevice(char *DeviceName,
		      int DeviceFD)
{
  close(DeviceFD);
/*   if (close(DeviceFD) < 0) */
/*     printf("cannot close SCSI device '%s'\n", DeviceName); */
}

/* ======================================================= */
int SCSI_ExecuteCommand(int DeviceFD,
			Direction_T Direction,
			CDB_T CDB,
			int CDB_Length,
			void *DataBuffer,
			int DataBufferLength,
			char *RequestSense,
                        int RequestSenseLength)
{
  struct sctl_io sctl_io;
  extern int errno;
  int Retries = 3;
  int Zero = 0, Result;
  
  memset(&sctl_io, '\0', sizeof(struct sctl_io));

  sctl_io.flags = 0;  
  sctl_io.max_msecs = 240000;
  /* Set the cmd */
  memcpy(sctl_io.cdb, CDB, CDB_Length);
  sctl_io.cdb_length = CDB_Length;
  /* Data buffer for results */
  sctl_io.data = DataBuffer;
  sctl_io.data_length = (unsigned)DataBufferLength;

  switch (Direction) 
    {
    case Input:
      sctl_io.flags = sctl_io.flags | SCTL_READ;
      break;
    }
  while (--Retries > 0) {
    
    Result = ioctl(DeviceFD, SIOC_IO, &sctl_io);
    if (sctl_io.cdb_status == S_GOOD && Result >= 0)
      {
	/* Sense Buffer */
	memcpy(RequestSense, sctl_io.sense, RequestSenseLength);
	return(0);
      } else {
	  /* ????? */
      }
  }
  return(-1);
}

/* ======================================================= */
int Tape_Eject (int DeviceFD)
{
  struct mtop mtop;
  
  mtop.mt_op = MTOFFL;
  mtop.mt_count = 1;
  if (ioctl(DeviceFD, MTIOCTOP, &mtop) < 0) {
/*     fprintf(stderr,"Tape Eject failed on %d\n", DeviceFD); */
    return(-1);
  } else {
    return(0);
  }
}

/* ======================================================= */
int Tape_Ready(char *Device, int wait)
{
  struct mtget mtget;
  int true = 1;
  time_t StartTime, EndTime;
  int DeviceFD;

  if ((DeviceFD = SCSI_OpenDevice(Device)) < 0)
  {
     sleep(wait);
     return;
  }
  
  bzero(&mtget, sizeof(struct mtget));
  StartTime = time(0);
  while (true)
    {
      if(ioctl(DeviceFD,  MTIOCGET, &mtget) != -1)
	{
#ifdef GMT_ONLINE_MASK
	  if (mtget.mt_gstat & GMT_ONLINE_MASK)
#else
	    if (GMT_ONLINE(mtget.mt_gstat))
#endif
	      {
		EndTime = time(0);
/*
		fprintf(stderr,"BOT after %d sec\n", EndTime - StartTime);
*/
		true=0;
	      }
	}
    }
    SCSI_CloseDevice(Device, DeviceFD);
}

/* ======================================================= */
int SCSI_Scan()
{
}
#endif
