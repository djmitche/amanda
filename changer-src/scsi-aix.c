#ifndef lint
static char rcsid[] = "$Id: scsi-aix.c,v 1.4 1998/11/27 04:25:29 oliva Exp $";
#endif
/*
 * Interface to execute SCSI commands on an AIX Workstation
 *
 * Copyright (c) 1998 T.Hepper th@icem.de
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
  return(DeviceFD); 
}

void SCSI_CloseDevice(char *DeviceName,
		      int DeviceFD)
{
  close(DeviceFD);
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
  CDB_T CDBSENSE;
  ExtendedRequestSense_T ExtendedRequestSense;
  struct sc_iocmd ds;
  int Result;
  int isbusy = 0;
  int target = 3;
  bzero(&ds, sizeof(struct sc_iocmd));
  bzero(RequestSenseBuf, RequestSenseLength);
  bzero(&ExtendedRequestSense, sizeof(ExtendedRequestSense_T));

  ds.flags = SC_ASYNC; 
  /* Timeout */
  ds.timeout_value = 60;
  bcopy(CDB, ds.scsi_cdb, CDB_Length);
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
  if ( Result < 0)
    {
      switch (ds.scsi_bus_status)
	{
	case SC_GOOD_STATUS:
	  return(SC_GOOD_STATUS);
	  break;
	case SC_CHECK_CONDITION:
	  RequestSense(DeviceFD, &ExtendedRequestSense, 0);
	  DecodeExtSense(&ExtendedRequestSense);
	  bcopy(&ExtendedRequestSense, RequestSenseBuf, RequestSenseLength); 
	  return(SC_CHECK_CONDITION);
	  break;
	default:
	  RequestSense(DeviceFD, &ExtendedRequestSense, 0);
	  DecodeExtSense(&ExtendedRequestSense);
	  bcopy(&ExtendedRequestSense, RequestSenseBuf, RequestSenseLength); 
	  dbprintf(("ioctl on %d return %d\n", DeviceFD, Result));
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
	  return(ds.scsi_bus_status);
	}
    }
  return(Result);
}

int Tape_Eject ( int DeviceFD)
{
/*
 Not yet ....
*/
  return(-1);
}

/* This function should ask the drive if it is ready */
int Tape_Ready ( char *tapedev , char * changerdev, int changerfd, int wait)
{
  FILE *out=NULL;
  int cnt=0;
  
  if (strcmp(tapedev,changerdev) == 0)
    {
      sleep(wait);
      return(0);
    }
  
  while ((cnt < wait) && (NULL==(out=fopen(tapedev,"w+")))){
    cnt++;
    sleep(1);
  }
  if (out != NULL)
    fclose(out);
  return 0;
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
    }
    if (!isbusy && ioctl(fd, SCIOSTOP, IDLUN(target, 0)) == -1)
      return(1);
  }
}
#endif
