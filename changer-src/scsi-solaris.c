#ifndef lint
static char rcsid[] = "$Id: scsi-solaris.c,v 1.1.2.8 1999/01/10 17:14:46 th Exp $";
#endif
/*
 * Interface to execute SCSI commands on an Sun Workstation
 *
 * Copyright (c) 1998 T.Hepper th@icem.de
 */
#include <amanda.h>

#ifdef HAVE_SOLARIS_LIKE_SCSI
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

#include <sys/scsi/impl/uscsi.h>

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
      pwork->SCSI = 0;
      pwork->inquiry = (SCSIInquiry_T *)malloc(sizeof(SCSIInquiry_T));

      if (Inquiry(DeviceFD, pwork->inquiry) == 0)
          {
              for (i=0;i < 16 && pwork->inquiry->prod_ident[i] != ' ';i++)
                  pwork->ident[i] = pwork->inquiry->prod_ident[i];
              pwork->ident[i] = '\0';
              pwork->SCSI = 1;
              return(pwork);
          } else {
              free(pwork->inquiry);
              pwork->inquiry = NULL;
              return(pwork);
          }
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
                        char *pRequestSense,
                        int RequestSenseLength)
{
  struct uscsi_cmd Command;
  memset(&Command, 0, sizeof(struct uscsi_cmd));
  memset(pRequestSense, 0, RequestSenseLength);
  switch (Direction)
    {
    case Input:
      if (DataBufferLength > 0)
        memset(DataBuffer, 0, DataBufferLength);
      Command.uscsi_flags = USCSI_DIAGNOSE | USCSI_ISOLATE
        | USCSI_READ | USCSI_RQENABLE;
      break;
    case Output:
      Command.uscsi_flags = USCSI_DIAGNOSE | USCSI_ISOLATE
        | USCSI_WRITE | USCSI_RQENABLE;
      break;
    }
  /* Set timeout to 5 minutes. */
  Command.uscsi_timeout = 300;
  Command.uscsi_cdb = (caddr_t) CDB;
  Command.uscsi_cdblen = CDB_Length;
  Command.uscsi_bufaddr = DataBuffer;
  Command.uscsi_buflen = DataBufferLength;
  Command.uscsi_rqbuf = (caddr_t) pRequestSense;
  Command.uscsi_rqlen = RequestSenseLength;
  return ioctl(DeviceFD, USCSICMD, &Command);
}

#endif
/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-file-style: gnu
 * End:
 */
