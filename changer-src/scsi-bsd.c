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
 * $Id: scsi-bsd.c,v 1.8 2000/11/26 15:55:45 martinea Exp $
 *
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
int SCSI_OpenDevice(OpenFiles_T *pwork, char *DeviceName)
{
  int DeviceFD;
  int i;
  
  if ((DeviceFD = open(DeviceName, O_RDWR)) > 0)
    {
      pwork->fd = DeviceFD;
      pwork->SCSI = 0;
      pwork->dev = strdup(DeviceName);
      pwork->inquiry = (SCSIInquiry_T *)malloc(INQUIRY_SIZE);

      if (SCSI_Inquiry(DeviceFD, pwork->inquiry, INQUIRY_SIZE) == 0)
        {
          if (pwork->inquiry->type == TYPE_TAPE || pwork->inquiry->type == TYPE_CHANGER)
            {
              for (i=0;i < 16 ;i++)
                pwork->ident[i] = pwork->inquiry->prod_ident[i];
              for (i=15; i >= 0 && !isalnum(pwork->ident[i]); i--)
                {
                  pwork->ident[i] = '\0';
                }
              pwork->SCSI = 1;
              PrintInquiry(pwork->inquiry);
              return(1);
            } else {
              free(pwork->inquiry);
              return(0);
            }
        } else {
          free(pwork->inquiry);
          pwork->inquiry = NULL;
          return(1);
        }
      return(1);
    } 
  return(0); 
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

int Tape_Status( int DeviceFD)
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
