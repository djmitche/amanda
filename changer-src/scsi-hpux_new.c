#ifndef lint
static char rcsid[] = "$Id: scsi-hpux_new.c,v 1.1.2.4 1998/12/22 05:12:16 oliva Exp $";
#endif
/*
 * Interface to execute SCSI commands on an HP-UX Workstation
 *
 * Copyright (c) 1998 T.Hepper th@icem.de
 */
#include <amanda.h>

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

#include <scsi-defs.h>

OpenFiles_T * SCSI_OpenDevice(char *DeviceName)
{
  int DeviceFD;
  int i;
  OpenFiles_T *pwork;

  if ((DeviceFD = open(DeviceName, O_RDWR| O_NDELAY)) > 0)
    {
      pwork = (OpenFiles_T *)malloc(sizeof(OpenFiles_T));
      pwork->next = NULL;
      pwork->fd = DeviceFD;
      pwork->dev = strdup(DeviceName);
      pwork->inquiry = (SCSIInquiry_T *)malloc(sizeof(SCSIInquiry_T));
      Inquiry(DeviceFD, pwork->inquiry);
      for (i=0;i < 16 && pwork->inquiry->prod_ident[i] != ' ';i++)
        pwork->name[i] = pwork->inquiry->prod_ident[i];
      pwork->name[i] = '\0';
      pwork->SCSI = 1;
      return(pwork);
    }
  return(NULL); 
}

int SCSI_CloseDevice(int DeviceFD)
{
  int ret;

  ret = close(DeviceFD);
  return(ret);
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
    if (Result < 0)
      return(Result);
    
    memcpy(RequestSense, sctl_io.sense, RequestSenseLength);

    switch(sctl_io.cdb_status)
      {
      case S_GOOD:
      case S_CHECK_CONDITION:
        return(sctl_io.cdb_status);
        break;
      default:
        return(sctl_io.cdb_status);
      }
  }
  return(-1);
}

#endif
/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-file-style: gnu
 * End:
 */
