#ifndef lint
static char rcsid[] = "$Id: scsi-irix.c,v 1.5 1998/12/22 05:11:40 oliva Exp $";
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

#include <sys/scsi.h>
#include <sys/dsreq.h>

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
  
  if ((DeviceFD = open(DeviceName, O_RDONLY)) > 0)
    {
      pwork= (OpenFiles_T *)malloc(sizeof(OpenFiles_T));
      pwork->next = NULL;
      pwork->fd = DeviceFD;
      pwork->inquiry = (SCSIInquiry_T *)malloc(sizeof(SCSIInquiry_T));
      Inquiry(DeviceFD, pwork->inquiry);
      for (i=0;i < 16 && pwork->inquiry->prod_ident[i] != ' ';i++)
          pwork->name[i] = pwork->inquiry->prod_ident[i];
      pwork->name[i] = '\0';
      pwork->SCSI = 1;
      pwork->dev = strdup(DeviceName);
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
  struct dsreq ds;
  int Zero = 0, Result;
  int retries = 5;
  
  memset(&ds, 0, sizeof(struct dsreq));
  memset(pRequestSense, 0, RequestSenseLength);
  memset(&ExtendedRequestSense, 0 , sizeof(ExtendedRequestSense_T)); 
  
  ds.ds_flags = DSRQ_SENSE; 
  /* Timeout */
  ds.ds_time = 120000;
  /* Set the cmd */
  ds.ds_cmdbuf = (caddr_t)CDB;
  ds.ds_cmdlen = CDB_Length;
  /* Data buffer for results */
  ds.ds_databuf = (caddr_t)DataBuffer;
  ds.ds_datalen = DataBufferLength;
  /* Sense Buffer */
  ds.ds_sensebuf = (caddr_t)pRequestSense;
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
    DecodeSCSI(CDB, "SCSI_ExecuteCommand : ");
    dbprintf(("\t\t\tSTATUS(%02X) RET(%02X)\n", STATUS(&ds), RET(&ds)));
    switch (STATUS(&ds))
      {
      case ST_BUSY:                /*  BUSY */
        break;
      case STA_RESERV:             /*  RESERV CONFLICT */
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

#endif
/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-file-style: gnu
 * End:
 */
