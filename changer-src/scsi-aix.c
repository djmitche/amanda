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
 * $Id: scsi-aix.c,v 1.17 2001/07/10 21:49:08 jrjackson Exp $
 *
 * Interface to execute SCSI commands on an AIX System
 *
 * Copyright (c) Thomas Hepper th@ant.han.de
 */
#include <amanda.h>

#ifdef HAVE_AIX_LIKE_SCSI

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include <sys/scarray.h>
#include <sys/tape.h>

#include <scsi-defs.h>
#include <gscdds.h>

int SCSI_OpenDevice(int ip)
{
  int DeviceFD;
  int i;
  extern OpenFiles_T *pDev;
  
  if (pDev[ip].inqdone == 0)
    {
      pDev[ip].inqdone = 1;
      /*
       * Check if it is an gsc (generic SCSI device)
       */
      if (strncmp("/dev/gsc", pDev[ip].dev, 8) == 0)
        {
          pDev[ip].dev = AIX_USE_GSC;
          DeviceFD = open(pDev[ip].dev, 0);
        } else {
          DeviceFD = openx(pDev[ip].dev, O_RDWR, 0, SC_DIAGNOSTIC);
        }
      if (DeviceFD > 0)
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
	 dbprintf(("SCSI_OpenDevice %s failed\n", pDev[ip].dev));
         return(0);
       }
    } else {
      if ((DeviceFD = openx(pDev[ip].dev, O_RDWR, 0, SC_DIAGNOSTIC)) > 0)
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
                        char *RequestSenseBuf,
                        int RequestSenseLength)
{
  extern OpenFiles_T *pDev;
  extern FILE * debug_file;
  CDB_T CDBSENSE;
  CDB_T SINQ;
  ExtendedRequestSense_T ExtendedRequestSense;
  struct sc_iocmd ds;
  scmd_t scmd;
  char sbyte;
  int Result;
  int isbusy = 0;
  int target = 3;


  if (pDev[DeviceFD].avail == 0)
    {
      return(SCSI_ERROR);
    }


  if (pDev[DeviceFD].flags == AIX_USE_GSC)
    {
      scmd.cdb = CDB;
      scmd.cdblen = CDB_Length;
      scmd.data_buf = DataBuffer;
      scmd.datalen = DataBufferLength;
      scmd.sense_buf = RequestSenseBuf;
      scmd.senselen = RequestSenseLength;
      scmd.statusp = &sbyte;
      scmd.timeval = 60;
      switch (Direction) 
        {
        case Input:
          scmd.rw = 0;
          break;
        case Output:
          scmd.rw = 1;
          break;
        }

      if (ioctl(pDev[DeviceFD].fd, GSC_CMD, (caddr_t) &scmd) < 0) {
        return(SCSI_ERROR);
      }
      return(SCSI_OK);

    } else {
      bzero(&ds, sizeof(struct sc_iocmd));
      bzero(RequestSenseBuf, RequestSenseLength);
      bzero(&ExtendedRequestSense, sizeof(ExtendedRequestSense_T));
      
      ds.flags = SC_ASYNC; 
      /* Timeout */
      ds.timeout_value = 60;
      bcopy(CDB, ds.scsi_cdb, CDB_Length);
      ds.command_length = CDB_Length;
      /* 
       *  Data buffer for results 
       * If the size of the buffer is 0
       * then keep this fields untouched           
       */
      if (DataBufferLength > 0)
        {
          ds.buffer = DataBuffer;
          ds.data_length = DataBufferLength;
        }
      
      /* Sense Buffer is not available on AIX ?*/
      /*
        ds.req_sense_length = 255;
        ds.request_sense_ptr = (char *)RequestSense;
      */
      switch (Direction) 
        {
        case Input:
          ds.flags = ds.flags | B_READ;
          break;
        case Output:
          ds.flags = ds.flags | B_WRITE;
          break;
        }
      DecodeSCSI(CDB, "SCSI_ExecuteCommand : ");
      
      if (pDev[DeviceFD].devopen == 0)
        SCSI_OpenDevice(DeviceFD);
      Result = ioctl(pDev[DeviceFD].fd, STIOCMD, &ds);
      SCSI_CloseDevice(DeviceFD);
      
      if ( Result < 0)
        {
          switch (ds.scsi_bus_status)
            {
            case SC_GOOD_STATUS:
              SINQ[0] = SC_COM_REQUEST_SENSE;
              SINQ[1] = 0; 
              SINQ[2] = 0;
              SINQ[3] = 0;
              SINQ[4] = 0x1D;
              SINQ[5] = 0x80;
              bcopy(SINQ, ds.scsi_cdb, 6);
              ds.command_length = 6;
              ds.buffer = RequestSenseBuf;
              ds.data_length = RequestSenseLength;
              
              if (pDev[DeviceFD].devopen == 0)
                SCSI_OpenDevice(DeviceFD);
              Result = ioctl(pDev[DeviceFD].fd, STIOCMD, &ds);
              SCSI_CloseDevice(DeviceFD);
              return(SCSI_OK);
              break;
            case SC_BUSY_STATUS:
              return(SCSI_BUSY);
              break;
            case SC_CHECK_CONDITION:
              SINQ[0] = SC_COM_REQUEST_SENSE;
              SINQ[1] = 0; 
              SINQ[2] = 0;
              SINQ[3] = 0;
              SINQ[4] = 0x1D;
              SINQ[5] = 0x80;
              bcopy(SINQ, ds.scsi_cdb, 6);
              ds.command_length = 6;
              ds.buffer = RequestSenseBuf;
              ds.data_length = RequestSenseLength;

              if (pDev[DeviceFD].devopen == 0)
                SCSI_OpenDevice(DeviceFD);
              Result = ioctl(pDev[DeviceFD].fd, STIOCMD, &ds);
              SCSI_CloseDevice(DeviceFD);
              return(SCSI_CHECK);
              break;
            default:
              /*
               * Makes no sense yet, may result in an endless loop
               *
               RequestSense(DeviceFD, &ExtendedRequestSense, 0);
               DecodeExtSense(&ExtendedRequestSense, "SCSI_ExecuteCommand:", debug_file);
               bcopy(&ExtendedRequestSense, RequestSenseBuf, RequestSenseLength);
              */
              dbprintf(("ioctl on %d return %d\n", pDev[DeviceFD].fd, Result));
              dbprintf(("ret: %d errno: %d (%s)\n", Result, errno, ""));
              dbprintf(("data_length:     %d\n", ds.data_length));
              dbprintf(("buffer:          0x%X\n", ds.buffer));
              dbprintf(("timeout_value:   %d\n", ds.timeout_value));
              dbprintf(("status_validity: %d\n", ds.status_validity));
              dbprintf(("scsi_bus_status: 0x%X\n", ds.scsi_bus_status));
              dbprintf(("adapter_status:  0x%X\n", ds.adapter_status));
              dbprintf(("adap_q_status:   0x%X\n", ds.adap_q_status));
              dbprintf(("q_tag_msg:       0x%X\n", ds.q_tag_msg));
              dbprintf(("flags:           0X%X\n", ds.flags));
              return(SCSI_ERROR);
            }
        }
      return(SCSI_OK);
    }
}

int SCSI_Scan()
{
  int fd;
  extern int errno;
  struct sc_inquiry si;
  u_char buf[255];
  int target;
  int lun;
  int isbusy;
  char type;
  char bus[] = "/dev/scsi0";
  
  if ((fd = open(bus, O_RDWR)) == -1)
    return(1);

  for (target = 0; target < 7; target++) {
    printf("Target %d:\n", target);
    if (ioctl(fd, SCIOSTART, IDLUN(target, 0)) == -1) {
      if (errno == EINVAL) {
        printf("is in use\n");
        isbusy = 1;
      } else {
        return(1);
      }
    } else {
      isbusy = 0;
    }

    for (lun = 0; lun < 7; lun++)
      {
        bzero(&si, sizeof(si));
        si.scsi_id = target;
        si.lun_id = lun;
        si.inquiry_len = 255;
        si.inquiry_ptr = &buf;
        if (ioctl(fd, SCIOINQU, &si) == -1)
          {
            printf("SCIOINQU: %s\n", strerror(errno));
          } else {
            type = buf[0] & 0x1f;
            buf[8+28] = 0;
            printf(stdout,"%-28s|Device Type %d\n",&buf[8], type);
          }
      }
    if (!isbusy && ioctl(fd, SCIOSTOP, IDLUN(target, 0)) == -1)
      return(1);
  }
}

int Tape_Ioctl ( int DeviceFD)
{
/*
 Not yet ....
*/
  return(-1);
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
  SCSI_Scan();
  return(-1);
}

#endif
/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-file-style: gnu
 * End:
 */
