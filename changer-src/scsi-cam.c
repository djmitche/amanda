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
 * $Id: scsi-cam.c,v 1.4 2000/11/26 15:55:45 martinea Exp $
 *
 * Interface to execute SCSI commands on an system with cam support
 * Current support is for FreeBSD 4.x
 *
 * Copyright (c) Thomes Hepper th@ant.han.de
 */
#include <amanda.h>

#ifdef HAVE_CAM_LIKE_SCSI

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

#ifdef HAVE_CAMLIB_H
# include <camlib.h>
#endif

#include <cam/scsi/scsi_message.h>

#ifdef HAVE_SYS_MTIO_H
#include <sys/mtio.h>
#endif

#include <scsi-defs.h>

extern OpenFiles_T *pChangerDev;
extern OpenFiles_T *pTapeDev;
extern OpenFiles_T *pTapeDevCtl;
extern FILE *debug_file;
/*
 * Check if the device is already open,
 * if no open it and save it in the list 
 * of open files.
 * DeviceName can be an device name, /dev/nrsa0 for example
 * or an bus:target:lun path, 0:4:0 for bus 0 target 4 lun 0
 */
int SCSI_OpenDevice(int ip)
{
  extern OpenFiles_T *pDev;
  char *DeviceName;
  int DeviceFD;
  int i;
  char *p;
  path_id_t path;
  target_id_t target;
  lun_id_t lun;

  DeviceName = strdup(pDev[ip].dev);

  if (pDev[ip].inqdone == 0)
    {
      pDev[ip].inqdone = 1;
      pDev[ip].SCSI = 0;
      pDev[ip].inquiry = (SCSIInquiry_T *)malloc(INQUIRY_SIZE);
      
      if (strstr(DeviceName, ":") != NULL)
        {  
          
          p = strtok(DeviceName, ":");
          sscanf(p,"%d", &path);
          
          if ((p = strtok(NULL,":")) == NULL)
            {
              free(DeviceName);
              ChgExit("SCSI_OpenDevice", "target in Device Name not found", FATAL);
            }
          sscanf(p,"%d", &target);
          if ((p = strtok(NULL,":")) == NULL)
            {
              free(DeviceName);
              ChgExit("SCSI_OpenDevice", "lun in Device Name not found", FATAL);
            }
          sscanf(p,"%d", &lun);
          
          if ((pDev[ip].curdev = cam_open_btl(path, target, lun, O_RDWR, NULL)) != NULL) {
            pDev[ip].SCSI = 1;
            pDev[ip].devopen = 1;
            if (SCSI_Inquiry(ip, pDev[ip].inquiry, INQUIRY_SIZE) == 0)
              {
                if (pDev[ip].inquiry->type == TYPE_TAPE || pDev[ip].inquiry->type == TYPE_CHANGER)
                  {
                    for (i=0;i < 16;i++)
                      pDev[ip].ident[i] = pDev[ip].inquiry->prod_ident[i];
                    for (i=15; i >= 0 && !isalnum(pDev[ip].ident[i]); i--)
                      {
                        pDev[ip].ident[i] = '\0';
                      }
                    pDev[ip].SCSI = 1;
                    free(DeviceName);
                    PrintInquiry(pDev[ip].inquiry);
                    return(1);
                  } else {
                    free(DeviceName);
                    free(pDev[ip].inquiry);
                    return(0);
                  }
              } else {
                pDev[ip].SCSI = 0;
                free(DeviceName);
                free(pDev[ip].inquiry);
                pDev[ip].inquiry = NULL;
                return(1);
              }
          }
        } else {
          if ((DeviceFD = open(DeviceName, O_RDWR)) > 0)
            {
              pDev[ip].devopen = 0;
              pDev[ip].SCSI=0;
              close(DeviceFD);
              free(DeviceName);
              return(1);
            }
        }
    } else {
      if (strstr(DeviceName, ":") != NULL)
        {  
          
          p = strtok(DeviceName, ":");
          sscanf(p,"%d", &path);
          
          if ((p = strtok(NULL,":")) == NULL)
            {
              free(DeviceName);
              ChgExit("SCSI_OpenDevice", "target in Device Name not found", FATAL);
            }
          sscanf(p,"%d", &target);
          if ((p = strtok(NULL,":")) == NULL)
            {
              free(DeviceName);
              ChgExit("SCSI_OpenDevice", "lun in Device Name not found", FATAL);
            }
          sscanf(p,"%d", &lun);
          
          if ((pDev[ip].curdev = cam_open_btl(path, target, lun, O_RDWR, NULL)) != NULL) {
            pDev[ip].devopen = 1;
            free(DeviceName);
            return(0);
          }
        } else {
          free(DeviceName);
          if ((DeviceFD = open(pDev[ip].dev, O_RDWR)) > 0)
            {
              pDev[ip].fd = DeviceFD;
              pDev[ip].devopen = 1;
              return(1);
            } else {
              return(0);
            }
          
        }
    }
  return(0); 
}

int SCSI_CloseDevice(int DeviceFD)
{
  int ret;
  extern OpenFiles_T *pDev;

  if (pDev[DeviceFD].SCSI == 1)
    {
      cam_close_device(pDev[DeviceFD].curdev);
      pDev[DeviceFD].devopen = 0;
    } else {
      close(pDev[DeviceFD].fd);
    }
  return(0);
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
  extern OpenFiles_T *pDev;
  union ccb *ccb;
  int ret;
  u_int32_t ccb_flags;
  OpenFiles_T *pwork = NULL;

  /* 
   * CLear the SENSE buffer
   */
  bzero(pRequestSense, RequestSenseLength);

  if (pDev[DeviceFD].devopen == 0)
    {
      SCSI_OpenDevice(DeviceFD);
    }

  DecodeSCSI(CDB, "SCSI_ExecuteCommand : ");

  ccb = cam_getccb(pDev[DeviceFD].curdev);

  /* Build the CCB */
  bzero(&(&ccb->ccb_h)[1], sizeof(struct ccb_scsiio));
  bcopy(&CDB[0], &ccb->csio.cdb_io.cdb_bytes, CDB_Length);

  switch (Direction)
    {
    case Input:
      if (DataBufferLength == 0)
        {
          ccb_flags = CAM_DIR_NONE;
        } else {
          ccb_flags = CAM_DIR_IN;
        }
      break;
    case Output:
      if (DataBufferLength == 0)
        {
          ccb_flags = CAM_DIR_NONE;
        } else {     
          ccb_flags = CAM_DIR_OUT;
        }
      break;
    default:
      ccb_flags = CAM_DIR_NONE;
      break;
    }
  
  cam_fill_csio(&ccb->csio,
                /* retires */ 1,
                /* cbfncp */ NULL,
                /* flags */ ccb_flags,
                /* tag_action */ MSG_SIMPLE_Q_TAG,
                /* data_ptr */ (u_int8_t*)DataBuffer,
                /* dxfer_len */ DataBufferLength,
                /* sense_len */ SSD_FULL_SIZE,
                /* cdb_len */ CDB_Length,
                /* timeout */ 600 * 1000);
  
  if (( ret = cam_send_ccb(pDev[DeviceFD].curdev, ccb)) == -1)
    {
      SCSI_CloseDevice(DeviceFD);
      cam_freeccb(ccb);
      return(-1);
    }
  
  /* 
   * copy the SENSE data to the Sense Buffer !!
   */
  memcpy(pRequestSense, &ccb->csio.sense_data, RequestSenseLength);
  
  /* ToDo add error handling */
  if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
    {
      SCSI_CloseDevice(DeviceFD);
      dbprintf(("SCSI_ExecuteCommand return %d\n", (ccb->ccb_h.status & CAM_STATUS_MASK)));
      return(-1);
    }

  SCSI_CloseDevice(DeviceFD);
  cam_freeccb(ccb);
  return(0);
}

int Tape_Eject ( int DeviceFD)
{  
  extern OpenFiles_T *pDev;
  struct mtop mtop;
  
  if (pDev[DeviceFD].devopen == 0)
    SCSI_OpenDevice(DeviceFD);

  mtop.mt_op = MTOFFL;
  mtop.mt_count = 1;
  ioctl(pDev[DeviceFD].fd, MTIOCTOP, &mtop);

  SCSI_CloseDevice(DeviceFD);

  return(-1);
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
