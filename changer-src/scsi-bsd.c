#ifndef lint
static char rcsid[] = "$Id: scsi-bsd.c,v 1.1.2.6 1999/02/26 19:42:01 th Exp $";
#endif
/*
 * Interface to execute SCSI commands on an BSD System (FreeBSD)
 *
 * Copyright (c) Thomes Hepper th@ant.han.de
 */
#include <amanda.h>

#ifdef HAVE_BSD_LIKE_SCSI

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

#include <sys/scsiio.h>
#include <sys/mtio.h>

#include <scsi-defs.h>

/*
 * Check if the device is already open,
 * if no open it and save it in the list 
 * of open files.
 */
OpenFiles_T * SCSI_OpenDevice(char *DeviceName)
{
  int DeviceFD;
  int i;
  OpenFiles_T *pwork;
  
  if ((DeviceFD = open(DeviceName, O_RDWR)) > 0)
    {
      pwork = (OpenFiles_T *)malloc(sizeof(OpenFiles_T));
     pwork->fd = DeviceFD;
      pwork->SCSI = 0;
      pwork->dev = strdup(DeviceName);
      pwork->inquiry = (SCSIInquiry_T *)malloc(INQUIRY_SIZE);

      if (SCSI_Inquiry(DeviceFD, pwork->inquiry, INQUIRY_SIZE) == 0)
          {
              if (pwork->inquiry->type == TYPE_TAPE || pwork->inquiry->type == TYPE_CHANGER)
                {
                  for (i=0;i < 16 && pwork->inquiry->prod_ident[i] != ' ';i++)
                    pwork->ident[i] = pwork->inquiry->prod_ident[i];
                  for (i=15; i >= 0 && pwork->inquiry->prod_ident[i] == ' ' ; i--)
                    {
                      pwork->inquiry->prod_ident[i] = '\0';
                    }
                  pwork->SCSI = 1;
                  PrintInquiry(pwork->inquiry);
                  return(pwork);
                } else {
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
    }
  
  return(NULL); 
}

int SCSI_CloseDevice(int DeviceFD)
{
  int ret;
    
  ret = close(DeviceFD) ;
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
  ExtendedRequestSense_T ExtendedRequestSense;
  scsireq_t ds;
  int Zero = 0, Result;
  int retries = 5;
  extern int errno;
  
  memset(&ds, 0, sizeof(scsireq_t));
  memset(pRequestSense, 0, RequestSenseLength);
  memset(&ExtendedRequestSense, 0 , sizeof(ExtendedRequestSense_T)); 
  
  ds.flags = SCCMD_ESCAPE; 
  /* Timeout */
  ds.timeout = 120000;
  /* Set the cmd */
  memcpy(ds.cmd, CDB, CDB_Length);
  ds.cmdlen = CDB_Length;
  /* Data buffer for results */
  ds.databuf = (caddr_t)DataBuffer;
  ds.datalen = DataBufferLength;
  /* Sense Buffer */
  /*
    ds.sense = (u_char)pRequestSense;
  */
  ds.senselen = RequestSenseLength;
    
  switch (Direction) 
    {
    case Input:
      ds.flags = ds.flags | SCCMD_READ;
      break;
    case Output:
      ds.flags = ds.flags | SCCMD_WRITE;
      break;
    }
    
  while (--retries > 0) {
    Result = ioctl(DeviceFD, SCIOCCOMMAND, &ds);
    memcpy(pRequestSense, ds.sense, RequestSenseLength);
    if (Result < 0)
      {
        dbprintf(("errno : %d\n",errno));
        return (-1);
      }
    dbprintf(("SCSI_ExecuteCommand(BSD) %02X STATUS(%02X) \n", CDB[0], ds.retsts));
    switch (ds.retsts)
      {
      case SCCMD_BUSY:                /*  BUSY */
        break;
      case SCCMD_OK:                /*  GOOD */
        return(SCCMD_OK);
        break;
      case SCCMD_SENSE:               /*  CHECK CONDITION */ 
        return(SCCMD_SENSE);
        break;
      default:
        continue;
      }
  }   
  return(ds.retsts);
}

int Tape_Eject ( int DeviceFD)
{
    struct mtop mtop;

    mtop.mt_op = MTOFFL;
    mtop.mt_count = 1;
    ioctl(DeviceFD, MTIOCTOP, &mtop);

    return(0);
}

#endif
/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-file-style: gnu
 * End:
 */
