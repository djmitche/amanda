#ifndef lint
static char rcsid[] = "$Id: scsi-bsd.c,v 1.1 1998/12/14 07:55:21 oliva Exp $";
#endif
/*
 * Interface to execute SCSI commands on an SGI Workstation
 *
 * Copyright (c) 1998 T.Hepper th@icem.de
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
#include <time.h>

#include <sys/scsiio.h>
#include <sys/mtio.h>
#include <scsi-defs.h>


int SCSI_OpenDevice(char *DeviceName)
{
    int DeviceFD = open(DeviceName, O_RDWR); 
    /*
      if (DeviceFD < 0) 
      printf("cannot open SCSI device '%s' - %m\n", DeviceName);
    */
    return DeviceFD; 
}

void SCSI_CloseDevice(char *DeviceName,
		      int DeviceFD)
{
    close(DeviceFD) ;
    /*
      if (close(DeviceFD) < 0)
      printf("cannot close SCSI device '%s' - %m\n", DeviceName);
    */
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

int SCSI_Scan()
{
	return(0);
}

int Tape_Eject ( int DeviceFD)
{
    struct mtop mtop;
    
    mtop.mt_op = MTOFFL;
    mtop.mt_count = 1;
    ioctl(DeviceFD, MTIOCTOP, &mtop);
      
    return(0);
}


int Tape_Ready(char *tapedev, char *changerdev, int changerfd, int wait)
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

#endif
/*
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
