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
 * $Id: scsi-solaris.c,v 1.20 2001/07/10 21:49:08 jrjackson Exp $
 *
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
              if (pDev[ip].inquiry->type == TYPE_TAPE || pDev[ip].inquiry->type == TYPE_CHANGER)
                {
                  for (i=0;i < 16;i++)
                    pDev[ip].ident[i] = pDev[ip].inquiry->prod_ident[i];
                  for (i=15; i >= 0 && !isalnum((int)pDev[ip].ident[i]) ; i--)
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
          dbprintf(("SCSI_OpenDevice %s failed\n", pDev[ip].dev));
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

  if (pDev[DeviceFD].avail == 0)
    {
      return(SCSI_ERROR);
    }
  
  if (depth++ > 2)
  {
     --depth;
     SCSI_CloseDevice(DeviceFD);
     return SCSI_ERROR;
  }
  memset(&Command, 0, sizeof(struct uscsi_cmd));
  memset(pRequestSense, 0, RequestSenseLength);
  switch (Direction)
    {
    case Input:
      if (DataBufferLength > 0)
        memset(DataBuffer, 0, DataBufferLength);

      /* Command.uscsi_flags =  USCSI_READ | USCSI_RQENABLE;    */
      Command.uscsi_flags = USCSI_DIAGNOSE | USCSI_ISOLATE
        | USCSI_READ | USCSI_RQENABLE;
      break;
    case Output:
      /* Command.uscsi_flags =  USCSI_WRITE | USCSI_RQENABLE;   */
      Command.uscsi_flags = USCSI_DIAGNOSE | USCSI_ISOLATE
        | USCSI_WRITE | USCSI_RQENABLE;
      break;
    }
  /* Set timeout to 5 minutes. */
  Command.uscsi_timeout = 300;
  Command.uscsi_cdb = (caddr_t) CDB;
  Command.uscsi_cdblen = CDB_Length;

  if (DataBufferLength > 0)
    {  
      Command.uscsi_bufaddr = DataBuffer;
      Command.uscsi_buflen = DataBufferLength;
    } else {
/*
 * If there is no data buffer force the direction to write, read with
 * a null buffer will fail (errno 22)
 */
      Command.uscsi_flags = USCSI_DIAGNOSE | USCSI_ISOLATE
        | USCSI_WRITE | USCSI_RQENABLE;
   }

  Command.uscsi_rqbuf = (caddr_t) pRequestSense;
  Command.uscsi_rqlen = RequestSenseLength;
  DecodeSCSI(CDB, "SCSI_ExecuteCommand : ");
  while (retries > 0)
  {
    if (pDev[DeviceFD].devopen == 0)
      SCSI_OpenDevice(DeviceFD);

    if ((ret = ioctl(pDev[DeviceFD].fd, USCSICMD, &Command)) >= 0)
    {
      ret = Command.uscsi_status;
      break;
    }
    dbprintf(("ioctl on %d failed, errno %d, ret %d\n",pDev[DeviceFD].fd, errno, ret));
    /*
     * RequestSense(DeviceFD, &pExtendedRequestSense, 0);
    */
    DecodeSense(RequestSense, "SCSI_ExecuteCommand:", debug_file);
    retries--;
  }
  --depth;
  SCSI_CloseDevice(DeviceFD);

  switch (ret)
    {
    default:
      DebugPrint(DEBUG_INFO, SECTION_SCSI,"ioctl ret (%d)\n",ret);
      return(SCSI_OK);
      break;
    }
}

/*
 * Send the command to the device with the
 * ioctl interface
 */
int Tape_Ioctl( int DeviceFD, int command)
{
  extern OpenFiles_T *pDev;
  struct mtop mtop;
  int ret = 0;

  if (pDev[DeviceFD].devopen == 0)
    {
      SCSI_OpenDevice(DeviceFD);
    }

  switch (command)
    {
    case IOCTL_EJECT:
      mtop.mt_op = MTOFFL;
      mtop.mt_count = 1;
      break;
     default:
      break;
    }

  if (ioctl(pDev[DeviceFD].fd , MTIOCTOP, &mtop) != 0)
    {
      dbprintf(("Tape_Ioctl error ioctl %d\n",errno));
      SCSI_CloseDevice(DeviceFD);
      return(-1);
    }

  SCSI_CloseDevice(DeviceFD);
  return(ret);  
}

int Tape_Status( int DeviceFD)
{
  extern OpenFiles_T *pDev;
  struct mtget mtget;
  int ret = -1;

  if (pDev[DeviceFD].devopen == 0)
    {
      SCSI_OpenDevice(DeviceFD);
    }
  
  if (ioctl(pDev[DeviceFD].fd , MTIOCGET, &mtget) != 0)
    {
      dbprintf(("Tape_Status error ioctl %d\n",errno));
      SCSI_CloseDevice(DeviceFD);
      return(-1);
    }

  /*
   * I have no idea what is the meaning of the bits in mt_erreg
   * I assume that nothing set is tape loaded
   * 0x2 is no tape online
   */
  if (mtget.mt_erreg == 0)
    {
      ret = ret | TAPE_ONLINE;
    }

  if (mtget.mt_erreg & 0x2)
    {
      ret = ret | TAPE_NOT_LOADED;
    }

  SCSI_CloseDevice(DeviceFD);

  return(ret); 
}

int ScanBus(int print)
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
