#ifndef lint
static char rcsid[] = "$Id: scsi-solaris.c,v 1.11 2000/06/25 18:48:11 ant Exp $";
#endif
/*
 * Interface to execute SCSI commands on an Sun Workstation
 *
 * Copyright (c) Thomas Hepper th@ant.han.de
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
#include <sys/mtio.h>

OpenFiles_T * SCSI_OpenDevice(char *DeviceName)
{
  int DeviceFD;
  int i;
  OpenFiles_T *pwork;
  
  if ((DeviceFD = open(DeviceName, O_RDWR| O_NDELAY)) > 0)
    {
      pwork = (OpenFiles_T *)malloc(sizeof(OpenFiles_T));
      memset(pwork, 0, sizeof(OpenFiles_T));
      pwork->fd = DeviceFD;
      pwork->dev = strdup(DeviceName);
      pwork->SCSI = 0;
      pwork->inquiry = (SCSIInquiry_T *)malloc(INQUIRY_SIZE);

      if (SCSI_Inquiry(DeviceFD, pwork->inquiry, INQUIRY_SIZE) == 0)
          {
          if (pwork->inquiry->type == TYPE_TAPE || pwork->inquiry->type == TYPE_CHANGER)
            {
              for (i=0;i < 16;i++)
                pwork->ident[i] = pwork->inquiry->prod_ident[i];
              for (i=15; i >= 0 && !isalnum(pwork->ident[i]) ; i--)
                {
                  pwork->ident[i] = '\0';
                }
              pwork->SCSI = 1;
              PrintInquiry(pwork->inquiry);
              return(pwork);
            } else {
              close(DeviceFD);
              free(pwork->inquiry);
              free(pwork);
              return(NULL);
            }
          } else {
              free(pwork->inquiry);
              pwork->inquiry = NULL;
              return(pwork);
          }
      return(pwork);
    } else {
      dbprintf(("SCSI_OpenDevice %s failed\n", DeviceName));
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
  extern FILE * debug_file;
  int ret;
  int retries = 1;
  extern int errno;
  struct uscsi_cmd Command;
  ExtendedRequestSense_T pExtendedRequestSense;
  static int depth = 0;

  if (depth++ > 2)
  {
     --depth;
     return -1;
  }
  memset(&Command, 0, sizeof(struct uscsi_cmd));
  memset(pRequestSense, 0, RequestSenseLength);
  switch (Direction)
    {
    case Input:
      if (DataBufferLength > 0)
        memset(DataBuffer, 0, DataBufferLength);

      Command.uscsi_flags =  USCSI_READ | USCSI_RQENABLE;
      /*
      Command.uscsi_flags = USCSI_DIAGNOSE | USCSI_ISOLATE
        | USCSI_READ | USCSI_RQENABLE;
      */
      break;
    case Output:
      Command.uscsi_flags =  USCSI_WRITE | USCSI_RQENABLE;
      /*
      Command.uscsi_flags = USCSI_DIAGNOSE | USCSI_ISOLATE
        | USCSI_WRITE | USCSI_RQENABLE;
      */
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
  DecodeSCSI(CDB, "SCSI_ExecuteCommand : ");
  while (retries > 0)
  {
    if ((ret = ioctl(DeviceFD, USCSICMD, &Command)) >= 0)
    {
       break;
    }
    dbprintf(("ioctl on %d failed, errno %d, ret %d\n",DeviceFD, errno, ret));
    RequestSense(DeviceFD, &pExtendedRequestSense, 0);
    DecodeExtSense(&pExtendedRequestSense, "SCSI_ExecuteCommand:", debug_file);
    retries--;
  }
  --depth;
  return(ret);
}

int Tape_Eject ( int DeviceFD)
{
  struct mtop mtop;

  mtop.mt_op = MTOFFL;
  mtop.mt_count = 1;
  ioctl(DeviceFD, MTIOCTOP, &mtop);
  return;
}

int Tape_Status( int DeviceFD)
{
  return(-1); 
}

#endif
/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-file-style: gnu
 * End:
 */
