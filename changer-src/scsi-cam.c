#ifndef lint
static char rcsid[] = "$Id: scsi-cam.c,v 1.1 2000/07/17 18:55:47 ant Exp $";
#endif
/*
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

#include <scsi-defs.h>

extern OpenFiles_T *pChangerDev;
extern OpenFiles_T *pTapeDev;
extern OpenFiles_T *pTapeDevCtl;
extern FILE *debug_file;
/*
 * Check if the device is already open,
 * if no open it and save it in the list 
 * of open files.
 */
int SCSI_OpenDevice(OpenFiles_T *pwork, char *DeviceName)
{
  int DeviceFD;
  int i;
  static int fdfake = 1;

  pwork->fd = fdfake++;
  pwork->SCSI = 0;
  pwork->dev = strdup(DeviceName);
  pwork->inquiry = (SCSIInquiry_T *)malloc(INQUIRY_SIZE);

  if ((pwork->curdev = cam_open_pass(DeviceName, O_RDWR, NULL)) != NULL) {
	pwork->SCSI = 1;
	if (SCSI_Inquiry(pwork->fd, pwork->inquiry, INQUIRY_SIZE) == 0)
	{
          if (pwork->inquiry->type == TYPE_TAPE || pwork->inquiry->type == TYPE_CHANGER)
            {
              for (i=0;i < 16;i++)
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
              close(DeviceFD);
              return(0);
            }
        } else {
          pwork->SCSI = 0;
          free(pwork->inquiry);
          pwork->inquiry = NULL;
          return(1);
	}
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
  union ccb *ccb;
  int ret;
  u_int32_t ccb_flags;
  OpenFiles_T *pwork = NULL;

  if (pChangerDev->fd == DeviceFD)
	  pwork = pChangerDev;
  if (pTapeDev->fd == DeviceFD)
	  pwork = pTapeDev;
  if (pTapeDevCtl->fd == DeviceFD)
	  pwork = pTapeDevCtl;

  if (pwork == NULL)
	  return(-1);

  ccb = cam_getccb(pwork->curdev);

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
  
  if (( ret = cam_send_ccb(pwork->curdev, ccb)) == -1)
    {
      cam_freeccb(ccb);
      return(-1);
    }
  
  /* ToDo add error handling */
  if ((ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
    {
      dbprintf(("SCSI_ExecuteCommand return %d\n", (ccb->ccb_h.status & CAM_STATUS_MASK)));
      return(-1);
    }
  
  cam_freeccb(ccb);
  return(0);
}

int Tape_Eject ( int DeviceFD)
{
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
