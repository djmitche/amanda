#ifndef lint
static char rcsid[] = "$Id: scsi-solaris.c,v 1.5 1998/11/18 07:03:23 oliva Exp $";
#endif
/*
 * Interface to execute SCSI commands on an Sun Workstation
 *
 * Copyright (c) 1998 T.Hepper th@icem.de
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
#include <time.h>

#include <sys/scsi/impl/uscsi.h>
#include <sys/mtio.h>
#include <scsi-defs.h>


int SCSI_OpenDevice(char *DeviceName)
{
  int DeviceFD = open(DeviceName, O_RDWR | O_NDELAY);
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
  struct uscsi_cmd Command;
  memset(&Command, 0, sizeof(struct uscsi_cmd));
  memset(RequestSense, 0, RequestSenseLength);
  switch (Direction)
    {
    case Input:
      if (DataBufferLength > 0)
	memset(DataBuffer, 0, DataBufferLength);
      Command.uscsi_flags = USCSI_DIAGNOSE | USCSI_ISOLATE
			    | USCSI_READ | USCSI_RQENABLE;
      break;
    case Output:
      Command.uscsi_flags = USCSI_DIAGNOSE | USCSI_ISOLATE
			    | USCSI_WRITE | USCSI_RQENABLE;
      break;
    }
  /* Set timeout to 5 minutes. */
  Command.uscsi_timeout = 300;
  Command.uscsi_cdb = (caddr_t) CDB;
  Command.uscsi_cdblen = CDB_Length;
  Command.uscsi_bufaddr = DataBuffer;
  Command.uscsi_buflen = DataBufferLength;
  Command.uscsi_rqbuf = (caddr_t) RequestSense;
  Command.uscsi_rqlen = RequestSenseLength;
  return ioctl(DeviceFD, USCSICMD, &Command);
}

int SCSI_Scan()
{
}

int Tape_Eject ( int DeviceFD)
{
  struct mtop mtop;
  
  mtop.mt_op = MTOFFL;
  mtop.mt_count = 1;
  ioctl(DeviceFD, MTIOCTOP, &mtop);
  return;
}

int Tape_Status( int DeviceFD)
{
	struct mtget mtget;
	int true = 1;
	bzero(&mtget, sizeof(struct mtget));

	if(ioctl(DeviceFD,  MTIOCGET, &mtget) != -1)
	{
	} else {
	}
}

int Tape_Ready(char *tapedev, char * changerdev, int changerfd, int wait)
{
  int cnt;
  FILE *out =NULL;
  
  if (strcmp(tapedev, changerdev) == 0) {
    sleep(wait);
    return(0);
  } else {
    while ((cnt<wait) && (NULL==(out=fopen(tapedev,"w+")))){
      cnt++;
      sleep(1);
    }
    if (out != NULL)
      fclose(out);
    return 0;
  }
}
#endif
