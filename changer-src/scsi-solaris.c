#ifndef lint
static char rcsid[] = "$Id: scsi-solaris.c,v 1.13 2000/07/31 18:55:13 ant Exp $";
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

int SCSI_OpenDevice(int ip)
{
  int DeviceFD;
  int i;
  extern OpenFiles_T *pDev;

  if (pDev[ip].inqdone == 0)
    {
      pDev[ip].inqdone = 1;
      if ((DeviceFD = open(pDev[ip].dev, O_RDWR| O_NDELAY)) > 0)
        {
          pDev[ip].avail = 1;
          pDev[ip].fd = DeviceFD;
          pDev[ip].SCSI = 0;
          pDev[ip].devopen = 1;
          pDev[ip].inquiry = (SCSIInquiry_T *)malloc(INQUIRY_SIZE);
          
          if (SCSI_Inquiry(ip, pDev[ip].inquiry, INQUIRY_SIZE) == 0)
            {
              if (pDev[ip].inquiry->type == TYPE_TAPE || pDev[ip]->inquiry->type == TYPE_CHANGER)
                {
                  for (i=0;i < 16;i++)
                    pDev[ip].ident[i] = pDev[ip].inquiry->prod_ident[i];
                  for (i=15; i >= 0 && !isalnum(pDev[ip].ident[i]) ; i--)
                    {
                      pDev[ip].ident[i] = '\0';
                    }
                  pDev[ip].SCSI = 1;
                  PrintInquiry(pDev[ip].inquiry);
                  return(1);
                } else {
                  close(DeviceFD);
                  free(pDev[ip].inquiry);
                  return(0);
                }
            } else {
              free(pDev[ip].inquiry);
              pDev[ip].inquiry = NULL;
              return(1);
            }
          return(1);
        } else {
          dbprintf(("SCSI_OpenDevice %s failed\n", DeviceName));
          return(0);
        }
    } else {
      if ((DeviceFD = open(pDev[ip].dev, O_RDWR| O_NDELAY)) > 0)
        {
          pDev[ip].fd = DeviceFD;
          pDev[ip].devopen = 1;
          return(1);
        }
    }
  return(0); 
}


int SCSI_CloseDevice(int DeviceFD)
{
  int ret;
  extern OpenFiles_T *pDev;

  ret = close(pDev[DeviceFD].fd);
  pDev[DeviceFD].devopen = 0;
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
  extern OpenFiles_T *pDev;
  extern FILE * debug_file;
  int ret;
  int retries = 1;
  extern int errno;
  struct uscsi_cmd Command;
  ExtendedRequestSense_T pExtendedRequestSense;
  static int depth = 0;

  if (pDev[DeviceFD].devopen == 0)
    SCSI_OpenDevice(DeviceFD);

  if (depth++ > 2)
  {
     --depth;
     SCSI_CloseDevice(DeviceFD);
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
  SCSI_CloseDevice(DeviceFD);
  return(ret);
}

int Tape_Eject ( int DeviceFD)
{
  struct mtop mtop;
  extern OpenFiles_T pDev;

  if (pDev[DeviceFD].devopen == 0)
    SCSI_OpenDevice(DeviceFD);

  mtop.mt_op = MTOFFL;
  mtop.mt_count = 1;
  ioctl(DeviceFD, MTIOCTOP, &mtop);

  SCSI_CloseDevice(DeviceFD);
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
