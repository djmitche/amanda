#ifndef lint
static char rcsid[] = "$Id: scsi-linux.c,v 1.1.2.7 1999/01/10 17:14:44 th Exp $";
#endif
/*
 * Interface to execute SCSI commands on Linux
 *
 * Copyright (c) 1998 T.Hepper th@icem.de
 */
#include <amanda.h>

#ifdef HAVE_LINUX_LIKE_SCSI

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

#include <scsi/scsi_ioctl.h>
#include <scsi/sg.h>

#include <scsi-defs.h>


OpenFiles_T * SCSI_OpenDevice(char *DeviceName)
{
  int DeviceFD;
  int i;
  OpenFiles_T *pwork;

  if ((DeviceFD = open(DeviceName, O_RDWR)) > 0)
    {
      pwork = (OpenFiles_T *)malloc(sizeof(OpenFiles_T));
      pwork->next = NULL;
      pwork->fd = DeviceFD;
      pwork->SCSI = 0;
      pwork->dev = strdup(DeviceName);
      if (strncmp("/dev/sg", DeviceName, 7) == 0)
        {
          pwork->inquiry = (SCSIInquiry_T *)malloc(sizeof(SCSIInquiry_T));
          Inquiry(DeviceFD, pwork->inquiry);
          for (i=0;i < 16 && pwork->inquiry->prod_ident[i] != ' ';i++)
             pwork->ident[i] = pwork->inquiry->prod_ident[i];
          pwork->ident[i] = '\0';
          pwork->SCSI = 1;
        }
      return(pwork); 
    }
  return(NULL);
}

int SCSI_CloseDevice(int DeviceFD)
{
    int ret;
    
    ret = close(DeviceFD);
    return(ret);
}
#ifdef LINUX_CHG
#define SCSI_OFF sizeof(struct sg_header)
int SCSI_ExecuteCommand(int DeviceFD,
                        Direction_T Direction,
                        CDB_T CDB,
                        int CDB_Length,
                        void *DataBuffer,
                        int DataBufferLength,
                        char *pRequestSense,
                        int RequestSenseLength)
{
  unsigned char *Command;
  struct sg_header *psg_header;
  char *buffer;
  int osize;
  int status;

  if (SCSI_OFF + CDB_Length + DataBufferLength > 4096) 
    {
       return(-1);
    }

  buffer = (char *)malloc(SCSI_OFF + CDB_Length + DataBufferLength);
  memset(buffer, 0, SCSI_OFF + CDB_Length + DataBufferLength);
  memcpy(buffer + SCSI_OFF, CDB, CDB_Length);

  psg_header = (struct sg_header *)buffer;
  if (CDB_Length >= 12)
    {
      psg_header->twelve_byte = 1;
    } else {
      psg_header->twelve_byte = 0;
    }
  psg_header->result = 0;
  psg_header->reply_len = SCSI_OFF + DataBufferLength;

  switch (Direction)
  {
    case Input:
      osize = 0;
      break;
    case Output:
      osize = DataBufferLength;
      break;
  }

  status = write(DeviceFD, buffer, SCSI_OFF + CDB_Length + osize);
  if ( status < 0 || status != SCSI_OFF + CDB_Length + osize ||
    psg_header->result ) 
    {
      dbprintf(("SCSI_ExecuteCommand error send \n"));
      return(-1);
    }

  memset(buffer, 0, SCSI_OFF + DataBufferLength);
  status = read(DeviceFD, buffer, SCSI_OFF + DataBufferLength);
  memset(pRequestSense, 0, RequestSenseLength);
  memcpy(psg_header->sense_buffer, pRequestSense, 16);

  if ( status < 0 || status != SCSI_OFF + DataBufferLength || 
    psg_header->result ) 
    { 
      dbprintf(("SCSI_ExecuteCommand error read \n"));
      dbprintf(("Status %d (%d) %2X\n", status, SCSI_OFF + DataBufferLength, psg_header->result ));
      return(-1);
    }

  if (DataBufferLength)
    {
       memcpy(DataBuffer, buffer + SCSI_OFF, DataBufferLength);
    }

  free(buffer);

  return(0);
}

#else

static inline int min(int x, int y)
{
  return (x < y ? x : y);
}


static inline int max(int x, int y)
{
  return (x > y ? x : y);
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
    unsigned char *Command;
    int Zero = 0, Result;
    memset(pRequestSense, 0, RequestSenseLength);
    switch (Direction)
        {
        case Input:
            Command = (unsigned char *)
                malloc(8 + max(DataBufferLength, RequestSenseLength));
            memcpy(&Command[0], &Zero, 4);
            memcpy(&Command[4], &DataBufferLength, 4);
            memcpy(&Command[8], CDB, CDB_Length);
            break;
        case Output:
            Command = (unsigned char *)
                malloc(8 + max(CDB_Length + DataBufferLength, RequestSenseLength));
            memcpy(&Command[0], &DataBufferLength, 4);
            memcpy(&Command[4], &Zero, 4);
            memcpy(&Command[8], CDB, CDB_Length);
            memcpy(&Command[8 + CDB_Length], DataBuffer, DataBufferLength);
            break;
        }
    Result = ioctl(DeviceFD, SCSI_IOCTL_SEND_COMMAND, Command);
    if (Result != 0)
        memcpy(pRequestSense, &Command[8], RequestSenseLength);
    else if (Direction == Input)
        memcpy(DataBuffer, &Command[8], DataBufferLength);
    free(Command);
    return Result;
}
#endif
#endif
/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-file-style: gnu
 * End:
 */
