#ifndef lint
static char rcsid[] = "$Id: scsi-aix.c,v 1.2 1998/11/11 23:59:09 oliva Exp $";
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
			char *RequestSense,
			 int RequestSenseLength)
{
  CDB_T CDBSENSE;
  struct sc_iocmd ds;
  int Result;
  int isbusy = 0;
  int target = 3;
  
  bzero(&ds, sizeof(struct sc_iocmd));
  bzero(RequestSense, RequestSenseLength);
  
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
/* 	  CDBSENSE[0] = 0x03; */
/* 	  CDBSENSE[1] = 0; */
/* 	  CDBSENSE[2] = 0; */
/* 	  CDBSENSE[3] = 0; */
/* 	  CDBSENSE[4] = 0x1d; */
/* 	  CDBSENSE[5] = 0; */
/* 	  ds.timeout_value = 60; */
/* 	  bcopy(CDBSENSE, ds.scsi_cdb, CDB_Length); */
/* 	  ds.command_length = CDB_Length; */
	  /* Data buffer for results */
/* 	  ds.buffer = RequestSense; */
/* 	  ds.data_length = RequestSenseLength; */
/* 	  ioctl(DeviceFD, STIOCMD, &ds); */
	  return(SC_CHECK_CONDITION);
	  break;
	default:
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
