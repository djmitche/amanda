#ifndef lint
static char rcsid[] = "$Id: scsi-aix.c,v 1.1 1998/11/07 08:49:16 oliva Exp $";
#endif
/*
 * Interface to execute SCSI commands on an AIX Workstation
 *
 * Copyright (c) 1998 T.Hepper
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
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
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#include <sys/scarray.h>
#include <sys/tape.h>
#include <scsi-defs.h>


int SCSI_OpenDevice(char *DeviceName)
{
  extern int errno;
  int DeviceFD = openx(DeviceName, O_RDONLY ,0 ,SC_DIAGNOSTIC);
  /*
  int DeviceFD = open(DeviceName, O_RDWR);
  */
  if (DeviceFD < 0) 
    printf("cannot open SCSI device '%s' %d\n", DeviceName, errno);
  return(DeviceFD); 
}

void SCSI_CloseDevice(char *DeviceName,
		      int DeviceFD)
{
  if (close(DeviceFD) < 0)
    printf("cannot close SCSI device '%s' - %m\n", DeviceName);
}

int SCSI_ExecuteCommand(int DeviceFD,
			Direction_T Direction,
			CDB_T CDB,
			int CDB_Length,
			void *DataBuffer,
			int DataBufferLength,
			char *RequestSense,
			 int RequestSenseLength)
{
  struct sc_iocmd ds;
  int Result;
  int isbusy = 0;
  int target = 3;
  /*  
  if (ioctl(DeviceFD, SCIOSTART, IDLUN(target, 0)) == -1) {
    if (errno == EINVAL) {
      printf("is in use\n");
      isbusy = 1;
    } else
      return(1);
  } else
    isbusy = 0;
  */
  bzero(&ds, sizeof(struct sc_iocmd));
  bzero(RequestSense, RequestSenseLength);
  
  ds.flags = SC_ASYNC; 
  /* Timeout */
  ds.timeout_value = 60;
  bcopy(CDB, ds.scsi_cdb, CDB_Length);
  dump_hex(ds.scsi_cdb, 12);
  ds.command_length = CDB_Length;
  /* Data buffer for results */
  ds.buffer = DataBuffer;
  ds.data_length = DataBufferLength;
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
  Result = ioctl(DeviceFD, STIOCMD, &ds);
  fprintf(stderr,"ioctl on %d return %d\n", DeviceFD, Result);
  printf("ret: %d errno: %d (%s)\n", Result, errno, "");
  printf("data_length:     %d\n", ds.data_length);
  printf("buffer:          0x%X\n", ds.buffer);
  printf("timeout_value:   %d\n", ds.timeout_value);
  printf("status_validity: %d\n", ds.status_validity);
  printf("scsi_bus_status: 0x%X\n", ds.scsi_bus_status);
  printf("adapter_status:  0x%X\n", ds.adapter_status);
  printf("adap_q_status:   0x%X\n", ds.adap_q_status);
  printf("q_tag_msg:       0x%X\n", ds.q_tag_msg);
  printf("flags:           0X%X\n", ds.flags);
   
  /*  
  if (!isbusy && ioctl(DeviceFD, SCIOSTOP, IDLUN(target, 0)) == -1)
    return(1);
  */
  return(Result);
}

int Tape_Eject ( int DeviceFD)
{
/*
 Not yet ....
*/
  return(-1);
}
int Tape_Ready ( int DeviceFD)
{
/*
 Not yet ....
*/
  return(-1);
}

int SCSI_Scan()
{
	       int fd;
		extern int errno;
        struct sc_inquiry si;
        u_char buf[255];
        int target;
        int isbusy;
        char bus[] = "/dev/scsi0";

        if ((fd = open(bus, O_RDWR)) == -1)
                return(1);
        for (target = 0; target < 7; target++) {
                printf("Target %d:\n", target);
                if (ioctl(fd, SCIOSTART, IDLUN(target, 0)) == -1) {
                        if (errno == EINVAL) {
                                printf("is in use\n");
                                isbusy = 1;
                        } else
                                return(1);
                } else
                        isbusy = 0;
                bzero(&si, sizeof(si));
               si.scsi_id = target;
                si.lun_id = 0;
                si.inquiry_len = 255;
                si.inquiry_ptr = buf;
                if (ioctl(fd, SCIOINQU, &si) == -1)
                        printf("SCIOINQU: %s\n", strerror(errno));
                else {
                        dump_hex(si.inquiry_ptr, si.inquiry_len);
                }
                if (!isbusy && ioctl(fd, SCIOSTOP, IDLUN(target, 0)) == -1)
                        return(1);
        }
}
#endif
