/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-2000 University of Maryland at College Park
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of U.M. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  U.M. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * U.M. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL U.M.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors: the Amanda Development Team.  Its members are listed in a
 * file named AUTHORS, in the root directory of this distribution.
 */
/*
 * $Id: scsi-irix.c,v 1.15 2001/04/15 12:05:24 ant Exp $
 *
 * Interface to execute SCSI commands on an SGI Workstation
 *
 * Copyright (c) Thomas Hepper th@ant.han.de
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
#include <sys/mtio.h>

#include <scsi-defs.h>

/*
 */
int SCSI_OpenDevice(int ip)
{
  extern OpenFiles_T *pDev;
  int DeviceFD;
  int i;
  
  if (pDev[ip].inqdone == 0)
    {
      if ((DeviceFD = open(DeviceName, O_RDONLY)) > 0)
        {
          pDev[ip].SCSI = 0;
          pDev[ip].inquiry = (SCSIInquiry_T *)malloc(INQUIRY_SIZE);
          if (SCSI_Inquiry(DeviceFD, pDev[ip].inquiry, INQUIRY_SIZE) == 0)
            {
              if (pDev[ip].inquiry->type == TYPE_TAPE || pDev[ip].inquiry->type == TYPE_CHANGER)
                {
                  for (i=0;i < 16 ;i++)
                    pDev[ip].ident[i] = pDev[ip].inquiry->prod_ident[i];
                  for (i=15; i >= 0 && !isalnum(pDev[ip].ident[i]) ; i--)
                    {
                      pDev[ip].ident[i] = '\0';
                    }
                  pDev[ip].SCSI = 1;
                  close(DeviceFD);
                  PrintInquiry(pDev[ip].inquiry);
                  return(1);
                } else {
                  close(DeviceFD);
                  free(pDev[ip].inquiry);
                  return(0);
                }
            } else {
              close(DeviceFD);
              free(pwork->inquiry);
              pwork->inquiry = NULL;
              return(1);
            }
          close(DeviceFD);
          return(1); /* Open OK, but no SCSI control */
        }
    } else {
      if ((DeviceFD = open(DeviceName, O_RDONLY)) > 0)
        {
          pDev[ip].fd = DeviceFD;
          pDev[ip].devopen = 1;
          return(1);
        } else {
          pDev[ip].devopen = 0;
          return(0);
        }
    }
  return(0); 
}

int SCSI_CloseDevice(int DeviceFD)
{
  extern OpenFiles_T *pDev;
  int ret;
    
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
  ExtendedRequestSense_T ExtendedRequestSense;
  struct dsreq ds;
  int Zero = 0, Result;
  int retries = 5;
  
  if (pDev[DeviceFD].avail == 0)
    {
      return(SCSI_ERROR);
    }
  
  memset(&ds, 0, sizeof(struct dsreq));
  memset(pRequestSense, 0, RequestSenseLength);
  memset(&ExtendedRequestSense, 0 , sizeof(ExtendedRequestSense_T)); 
  
  ds.ds_flags = DSRQ_SENSE|DSRQ_TRACE|DSRQ_PRINT; 
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
    if (pDev[DeviceFD].devopen == 0)
      {
        SCSI_OpenDevice(DeviceFD);
      }
    Result = ioctl(pDev[DeviceFD].fd, DS_ENTER, &ds);
    SCSI_CloseDevice(DeviceFD);

    if (Result < 0)
      {
        RET(&ds) = DSRT_DEVSCSI;
        SCSI_CloseDevice(DeviceFD);
        return (SCSI_ERROR);
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
            return(SCSI_OK);
            break;
          case DSRT_OK:
          default:
            return(SCSI_OK);
          }
      case ST_CHECK:               /*  CHECK CONDITION */ 
        return(SCSI_CHECK);
        break;
      case ST_COND_MET:            /*  INTERM/GOOD */
      default:
        continue;
      }
  }     
  return(SCSI_ERROR);
}

int Tape_Eject ( int DeviceFD)
{
  extern OpenFiles_T *pDev;
  struct mtop mtop;

  if (pDev[DeviceFD].devopen == 0)
    {
      SCSI_OpenDevice(DeviceFD);
    }

  mtop.mt_op = MTUNLOAD;
  mtop.mt_count = 1;
  ioctl(pDev[DeviceFD].fd, MTIOCTOP, &mtop);
  SCSI_CloseDevice(DeviceFD);
  return(0);
}

int Tape_Status( int DeviceFD)
{
/*
  Not yet
*/
  return(-1);
}

int ScanBus(int print)
{
/*
  Not yet
*/
  return(-1);
}

#endif
/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-file-style: gnu
 * End:
 */
