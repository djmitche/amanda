/*
 * Copyright (c) 1998 T.Hepper
 */
#ifndef WORDS_BIGENDIAN
#define LITTLE_ENDIAN_BITFIELDS
#endif

typedef enum { Input, Output } Direction_T; 
typedef unsigned char CDB_T[12];

#ifdef _AIX
typedef unsigned int PackedBit;
#else
typedef unsigned char PackedBit;
#endif

/* macros for building scsi msb array parameter lists */
#define B(s,i) ((unsigned char)((s) >> i))
#define B1(s)                           ((unsigned char)(s))
#define B2(s)                       B((s),8),   B1(s)
#define B3(s)            B((s),16), B((s),8),   B1(s)
#define B4(s) B((s),24), B((s),16), B((s),8),   B1(s)

/* macros for converting scsi msb array to binary */
#define V1(s)           (s)[0]
#define V2(s)         (((s)[0] << 8) | (s)[1])
#define V3(s)       (((((s)[0] << 8) | (s)[1]) << 8) | (s)[2])
#define V4(s)     (((((((s)[0] << 8) | (s)[1]) << 8) | (s)[2]) << 8) | (s)[3])
#define V5(s)   (((((((((s)[0] << 8) | (s)[1]) << 8) | (s)[2]) << 8) | (s)[3]) << 8) | (s)[4])
#define V6(s) (((((((((((s)[0] << 8) | (s)[1]) << 8) | (s)[2]) << 8) | (s)[3]) << 8) | (s)[4]) << 8) | (s)[5])

/* macros for converting binary into scsi msb array */
#define MSB1(s,v)                                               *(s)=B1(v)
#define MSB2(s,v)                                *(s)=B(v,8), (s)[1]=B1(v)
#define MSB3(s,v)               *(s)=B(v,16),  (s)[1]=B(v,8), (s)[2]=B1(v)
#define MSB4(s,v) *(s)=B(v,24),(s)[1]=B(v,16), (s)[2]=B(v,8), (s)[3]=B1(v)


/* ======================================================= */
/* RequestSense_T */
/* ======================================================= */
typedef struct  
{
#ifdef LITTLE_ENDIAN_BITFIELDS 
    PackedBit     ErrorCode:7;                    /* Byte 0 Bits 0-6 */
    PackedBit     Valid:1;                              /* Byte 0 Bit 7    */
#else 
    PackedBit     Valid:1;                              /* Byte 0 Bit 7    */
    PackedBit     ErrorCode:7;                    /* Byte 0 Bits 0-6 */
#endif
    unsigned char SegmentNumber;                  /* Byte 1 */ 
#ifdef LITTLE_ENDIAN_BITFIELDS        
    PackedBit     SenseKey:4;                     /* Byte 2 Bits 0-3 */
    PackedBit     :1;                             /* Byte 2 Bit 4    */
    PackedBit     RILI:1;                                /* Byte 2 Bit 5    */
    PackedBit     REOM:1;                                /* Byte 2 Bit 6    */
    PackedBit     Filemark:1;                           /* Byte 2 Bit 7    */
#else 
    PackedBit     Filemark:1;                           /* Byte 2 Bit 7    */
    PackedBit     REOM:1;                                /* Byte 2 Bit 6    */
    PackedBit     RILI:1;                                /* Byte 2 Bit 5    */
    PackedBit     :1;                             /* Byte 2 Bit 4    */ 
    PackedBit     SenseKey:4;                     /* Byte 2 Bits 0-3 */
#endif
    unsigned char Information[4];                 /* Bytes 3-6       */ 
    unsigned char AdditionalSenseLength;          /* Byte 7          */   
    unsigned char CommandSpecificInformation[4];  /* Bytes 8-11      */ 
    unsigned char AdditionalSenseCode;            /* Byte 12         */
    unsigned char AdditionalSenseCodeQualifier;   /* Byte 13         */ 
    unsigned char Byte14;                          /* Byte 14         */ 
    unsigned char Byte15;                           /* Byte 15         */ 
    
} RequestSense_T;     

/* ======================================================= */
/* ExtendedRequestSense_T */
/* ======================================================= */
typedef struct  
{
#ifdef LITTLE_ENDIAN_BITFIELDS 
    PackedBit     ErrorCode:7;                    /* Byte 0 Bits 0-6 */
    PackedBit     Valid:1;                        /* Byte 0 Bit 7    */
#else 
    PackedBit     Valid:1;                        /* Byte 0 Bit 7    */
    PackedBit     ErrorCode:7;                    /* Byte 0 Bits 0-6 */
#endif
    unsigned char SegmentNumber;                  /* Byte 1 */ 
#ifdef LITTLE_ENDIAN_BITFIELDS        
    PackedBit     SenseKey:4;                     /* Byte 2 Bits 0-3 */
    PackedBit     :1;                             /* Byte 2 Bit 4    */
    PackedBit     RILI:1;                         /* Byte 2 Bit 5    */
    PackedBit     REOM:1;                         /* Byte 2 Bit 6    */
    PackedBit     Filemark:1;                     /* Byte 2 Bit 7    */
#else 
    PackedBit     Filemark:1;                     /* Byte 2 Bit 7    */
    PackedBit     REOM:1;                         /* Byte 2 Bit 6    */
    PackedBit     RILI:1;                         /* Byte 2 Bit 5    */
    PackedBit     :1;                             /* Byte 2 Bit 4    */ 
    PackedBit     SenseKey:4;                     /* Byte 2 Bits 0-3 */
#endif
    unsigned char Information[4];                 /* Bytes 3-6       */ 
    unsigned char AdditionalSenseLength;          /* Byte 7          */   
    unsigned char LogParameterPageCode;           /* Bytes 8         */ 
    unsigned char LogParameterCode;               /* Bytes 9         */ 
    unsigned char Byte10;                         /* Bytes 10        */ 
    unsigned char UnderrunOverrunCounter;         /* Bytes 11        */ 
    unsigned char AdditionalSenseCode;            /* Byte 12         */
    unsigned char AdditionalSenseCodeQualifier;   /* Byte 13         */ 
    unsigned char Byte14;                         /* Byte 14         */ 
    unsigned char Byte15;                         /* Byte 15         */ 
    unsigned char ReadWriteDataErrorCounter[3];   /* Byte 16-18      */ 
#ifdef LITTLE_ENDIAN_BITFIELDS        
    PackedBit     LBOT:1;                         /* Byte 19 Bits 0 */
    PackedBit     TNP:1;                          /* Byte 19 Bits 1 */
    PackedBit     TME:1;                          /* Byte 19 Bits 2 */
    PackedBit     ECO:1;                          /* Byte 19 Bits 3 */
    PackedBit     ME:1;                           /* Byte 19 Bits 4 */
    PackedBit     FPE:1;                          /* Byte 19 Bits 5 */
    PackedBit     BPE:1;                          /* Byte 19 Bits 6 */
    PackedBit     PF:1;                           /* Byte 19 Bits 7 */
#else 
    PackedBit     PF:1;                           /* Byte 19 Bits 7 */
    PackedBit     BPE:1;                          /* Byte 19 Bits 6 */
    PackedBit     FPE:1;                          /* Byte 19 Bits 5 */
    PackedBit     ME:1;                           /* Byte 19 Bits 4 */
    PackedBit     ECO:1;                          /* Byte 19 Bits 3 */
    PackedBit     TME:1;                          /* Byte 19 Bits 2 */
    PackedBit     TNP:1;                          /* Byte 19 Bits 1 */
    PackedBit     LBOT:1;                         /* Byte 19 Bits 0 */
#endif
#ifdef LITTLE_ENDIAN_BITFIELDS        
    PackedBit     FE:1;                           /* Byte 20 Bits 0 */
    PackedBit     SSE:1;                          /* Byte 20 Bits 1 */
    PackedBit     WEI:1;                          /* Byte 20 Bits 2 */
    PackedBit     URE:1;                          /* Byte 20 Bits 3 */
    PackedBit     FMKE:1;                         /* Byte 20 Bits 4 */
    PackedBit     WP:1;                           /* Byte 20 Bits 5 */
    PackedBit     TMD:1;                          /* Byte 20 Bits 6 */
    PackedBit     :1;                             /* Byte 20 Bits 7 */
#else 
    PackedBit     :1;                             /* Byte 20 Bits 7 */
    PackedBit     TMD:1;                          /* Byte 20 Bits 6 */
    PackedBit     WP:1;                           /* Byte 20 Bits 5 */
    PackedBit     FMKE:1;                         /* Byte 20 Bits 4 */
    PackedBit     URE:1;                          /* Byte 20 Bits 3 */
    PackedBit     WEI:1;                          /* Byte 20 Bits 2 */
    PackedBit     SSE:1;                          /* Byte 20 Bits 1 */
    PackedBit     FE:1;                           /* Byte 20 Bits 0 */
#endif
#ifdef LITTLE_ENDIAN_BITFIELDS        
    PackedBit     WSEO:1;                         /* Byte 21 Bits 0 */
    PackedBit     WSEB:1;                         /* Byte 21 Bits 1 */
    PackedBit     PEOT:1;                         /* Byte 21 Bits 2 */
    PackedBit     CLN:1;                          /* Byte 21 Bits 3 */
    PackedBit     CLND:1;                         /* Byte 21 Bits 4 */
    PackedBit     RRR:1;                          /* Byte 21 Bits 5 */
    PackedBit     UCLN:1;                         /* Byte 21 Bits 6 */
    PackedBit     :1;                             /* Byte 21 Bits 7 */
#else 
    PackedBit     :1;                             /* Byte 21 Bits 7 */
    PackedBit     UCLN:1;                         /* Byte 21 Bits 6 */
    PackedBit     RRR:1;                          /* Byte 21 Bits 5 */
    PackedBit     CLND:1;                         /* Byte 21 Bits 4 */
    PackedBit     CLN:1;                          /* Byte 21 Bits 3 */
    PackedBit     PEOT:1;                         /* Byte 21 Bits 2 */
    PackedBit     WSEB:1;                         /* Byte 21 Bits 1 */
    PackedBit     WSEO:1;                         /* Byte 21 Bits 0 */
#endif
    unsigned char Byte21;                          /* Byte 22         */ 
    unsigned char RemainingTape[3];                /* Byte 23-25      */ 
    unsigned char TrackingRetryCounter;            /* Byte 26         */ 
    unsigned char ReadWriteRetryCounter;           /* Byte 27         */ 
    unsigned char FaultSymptomCode;                /* Byte 28         */ 
    
} ExtendedRequestSense_T;     

/* ======================================================= */
/*  ReadElementStatus_T */
/* ======================================================= */
typedef struct 
{
    unsigned char cmd;                           /* Byte 1 */
#ifdef LITTLE_ENDIAN_BITFIELDS        
    PackedBit     type : 4;
    PackedBit     voltag :1;
    PackedBit     lun :3;
#else
    PackedBit     lun :3;
    PackedBit     voltag :1;
    PackedBit     type : 4;
#endif
    unsigned char start[2];                    /* Byte 3-4 */
    unsigned char number[2];                   /* Byte 5-6 */
    unsigned char byte4;                       /* Byte 7 */
    unsigned char length[4];                   /* Byte 8-11 */
    unsigned char byte78[2];                   /* Byte 12-13 */
} ReadElementStatus_T;

/* ======================================================= */
/* ElementStatusPage_T */
/* ======================================================= */
typedef struct 
{
    unsigned char type;     /* Byte 1 = Element Type Code*/
#ifdef LITTLE_ENDIAN_BITFIELDS        
    PackedBit     bitres  : 6;
    PackedBit     avoltag : 1;
    PackedBit     pvoltag : 1;
#else
    PackedBit     pvoltag : 1;
    PackedBit     avoltag : 1;
    PackedBit     bitres  : 6;
#endif
    unsigned char length[2];    /* Byte 3-4  = Element Descriptor Length */
    unsigned char byte4;        /* Byte 5 */
    unsigned char count[3];     /* Byte 6-8 = Byte Count of Descriptor Available */
} ElementStatusPage_T;


/* ======================================================= */
/* ElementStatusData_T */
/* ======================================================= */
typedef struct 
{
    unsigned char first[2];    /* Byte 1-2 = First Element Adress Reported */
    unsigned char number[2];   /* Byte 3-4 = Number of Elements Available */
    unsigned char byte5;      /* Reserved */
    unsigned char count[3];     /* Byte 6-8 = Byte Count of Report Available */
} ElementStatusData_T;

/* ======================================================= */
/* MediumTransportElementDescriptor_T */
/* ======================================================= */
typedef struct 
{
    unsigned char address[2];   /* Byte 1 = Element Address */
#ifdef LITTLE_ENDIAN_BITFIELDS        
    PackedBit     full   : 1;
    PackedBit     rsvd   : 1;
    PackedBit     except : 1;
    PackedBit     res    : 5;
#else
    PackedBit     res    : 5;
    PackedBit     except : 1;
    PackedBit     rsvd   : 1;
    PackedBit     full   : 1;
#endif
    unsigned char byte4;
    unsigned char asc;
    unsigned char ascq;
    unsigned char byte79[3];
#ifdef LITTLE_ENDIAN_BITFIELDS        
    PackedBit     byte10res : 6;
    PackedBit     invert : 1;
    PackedBit     svalid : 1;
#else
    PackedBit     svalid : 1;
    PackedBit     invert : 1;
    PackedBit     byte10res : 6;
#endif
    unsigned char source[2];
    unsigned char byte1314[4];
} MediumTransportElementDescriptor_T;

/* ======================================================= */
/* StorageElementDescriptor_T */
/* ======================================================= */
typedef struct 
{
    unsigned char address[2];
#ifdef LITTLE_ENDIAN_BITFIELDS        
    PackedBit     full   : 1;
    PackedBit     rsvd   : 1;
    PackedBit     except : 1;
    PackedBit     access : 1;
    PackedBit     res    : 4;
#else
    PackedBit     res    : 4;
    PackedBit     access : 1;
    PackedBit     except : 1;
    PackedBit     rsvd   : 1;
    PackedBit     full   : 1;
#endif
    unsigned char res1;
    unsigned char asc;
    unsigned char ascq;
    unsigned char res2[3];
#ifdef LITTLE_ENDIAN_BITFIELDS        
    PackedBit     res3   : 6;
    PackedBit     invert : 1;
    PackedBit     svalid : 1;
#else
    PackedBit     svalid : 1;
    PackedBit     invert : 1;
    PackedBit     res3   : 6;
#endif
    unsigned char source[2];
    unsigned char res4[4];
} StorageElementDescriptor_T;

/* ======================================================= */
/* DataTransferElementDescriptor_T */
/* ======================================================= */
typedef struct 
{
    unsigned char address[2];
#ifdef LITTLE_ENDIAN_BITFIELDS        
    PackedBit     full    : 1;
    PackedBit     rsvd    : 1;
    PackedBit     except  : 1;
    PackedBit     access  : 1;
    PackedBit     res     : 4;
#else
    PackedBit     res     : 4;
    PackedBit     access  : 1;
    PackedBit     except  : 1;
    PackedBit     rsvd    : 1;
    PackedBit     full    : 1;
#endif
    unsigned char res1;
    unsigned char asc;
    unsigned char ascq;
#ifdef LITTLE_ENDIAN_BITFIELDS        
    PackedBit     lun     : 3;
    PackedBit     rsvd1   : 1;
    PackedBit     luvalid : 1;
    PackedBit     idvalid : 1;
    PackedBit     rsvd2   : 1;
    PackedBit     notbus  : 1;
#else
    PackedBit     notbus  : 1;
    PackedBit     rsvd2   : 1;
    PackedBit     idvalid : 1;
    PackedBit     luvalid : 1;
    PackedBit     rsvd1   : 1;
    PackedBit     lun     : 3;
#endif
    unsigned char scsi;
    unsigned char res2;
#ifdef LITTLE_ENDIAN_BITFIELDS        
    PackedBit     res3    : 6;
    PackedBit     invert  : 1;
    PackedBit     svalid  : 1;
#else
    PackedBit     svalid  : 1;
    PackedBit     invert  : 1;
    PackedBit     res3    : 6;
#endif
    unsigned char source[2];
    unsigned char res4[4];
} DataTransferElementDescriptor_T;

/* ======================================================= */
/* SCSIInquiry_T */
/* ======================================================= */
typedef struct
{
#ifdef LITTLE_ENDIAN_BITFIELDS
    PackedBit     data_format : 4;
    PackedBit     res3_54 : 2;
    PackedBit     termiop : 1;
    PackedBit     aenc : 1;

    PackedBit     ansi_version : 3;
    PackedBit     ecma_version : 3;
    PackedBit     iso_version : 2;

    PackedBit     type_modifier : 7;
    PackedBit     removable : 1;

    PackedBit     type : 5;
    PackedBit     qualifier : 3;
#else
    PackedBit     qualifier : 3;
    PackedBit     type : 5;
  
    PackedBit     removable : 1;
    PackedBit     type_modifier : 7;
  
    PackedBit     iso_version : 2;
    PackedBit     ecma_version : 3;
    PackedBit     ansi_version : 3;
  
    PackedBit     aenc : 1;
    PackedBit     termiop : 1;
    PackedBit     res3_54 : 2;
    PackedBit     data_format : 4;
#endif
  
    unsigned char add_len;
  
    unsigned char  res2;
    unsigned char res3;
  
#ifdef LITTLE_ENDIAN_BITFIELDS
    PackedBit     softreset : 1;
    PackedBit     cmdque : 1;
    PackedBit     res7_2 : 1;
    PackedBit     linked  : 1;
    PackedBit     sync : 1;
    PackedBit     wbus16 : 1;
    PackedBit     wbus32 : 1;
    PackedBit     reladr : 1;
#else
    PackedBit     reladr : 1;
    PackedBit     wbus32 : 1;
    PackedBit     wbus16 : 1;
    PackedBit     sync : 1;
    PackedBit     linked  : 1;
    PackedBit     res7_2 : 1;
    PackedBit     cmdque : 1;
    PackedBit     softreset : 1;
#endif
    char vendor_info[8];
    char prod_ident[16];
    char prod_version[4];
    char vendor_specific[20];
} SCSIInquiry_T;

/* ======================================================= */
/* ModeSenseHeader_T */
/* ======================================================= */
typedef struct
{
    unsigned char DataLength;
    unsigned char MediumType;
#ifdef LITTLE_ENDIAN_BITFIELDS
    PackedBit Speed:4;
    PackedBit BufferedMode:3;
    PackedBit WP:1;
#else
    PackedBit WP:1;
    PackedBit BufferedMode:3;
    PackedBit Speed:4;
#endif
    unsigned char BlockDescLength;
} ModeSenseHeadr_T;
/* ======================================================= */
/* ModeBlockDescriptor_T */
/* ======================================================= */
typedef struct 
{
    unsigned char DensityCode;
    unsigned char NumberOfBlocks[3];
    unsigned char Reserved;
    unsigned char BlockLength[3];
} ModeBlockDescriptor_T;
/* ======================================================= */
/* LogSenseHeader_T */
/* ======================================================= */
typedef struct 
{
#ifdef LITTLE_ENDIAN_BITFIELDS
    PackedBit PageCode:6;
    PackedBit Reserved:2;
#else
    PackedBit Reserved:2;
    PackedBit PageCode:6;
#endif
    unsigned char Reserved1;
    unsigned char PageLength[2];
} LogSenseHeader_T ;
/* ======================================================= */
/* LogParameters_T */
/* ======================================================= */
typedef struct
{
    unsigned char ParameterCode[2];
#ifdef LITTLE_ENDIAN_BITFIELDS
    PackedBit LP:1;
    PackedBit RSCD:1;
    PackedBit TMC:1;
    PackedBit ETC:1;
    PackedBit TSD:1;
    PackedBit DS:1;
    PackedBit DU:1;
#else
    PackedBit DU:1;
    PackedBit DS:1;
    PackedBit TSD:1;
    PackedBit ETC:1;
    PackedBit TMC:1;
    PackedBit RSCD:1;
    PackedBit LP:1;
#endif
    char ParameterLength;
} LogParameter_T;
/* ======================================================= */
/* ElementInfo_T */
/* ======================================================= */
typedef struct ElementInfo
{
    int type;       /* CHANGER - 1, STORAGE - 2, TAPE - 4 */
    int address;    /* Adress of this Element */
    int from;       /* From where did it come */
    char status;    /* F -> Full, E -> Empty */
    int ASC;        /* Additional Sense Code from read element status */
    int ASCQ;      /* */
    struct ElementInfo *next; /* Pointer to next ElementInfo */
} ElementInfo_T;



typedef struct {
    char *ident;                   /* Name of the device from inquiry */
    int (*function[7])();          /* New way to call the device dependend functions move/eject ... */
} ChangerCMD_T ;

typedef struct {
    unsigned char command;
    int length;
    char *name;
} SC_COM_T;

typedef struct OpenFiles {
    int fd;
    unsigned char SCSI;
    char *dev;
    char *ConfigName;
    char ident[17];
    SCSIInquiry_T *inquiry;
    struct OpenFiles *next;
} OpenFiles_T;

typedef struct LogPageDecode {
    int LogPage;
    char *ident;
    void (*decode)(LogParameter_T *, int);
} LogPageDecode_T;

/* ======================================================= */
/* Funktion-Declaration */
/* ======================================================= */
OpenFiles_T * SCSI_OpenDevice(char *DeviceName);
int OpenDevice(char *DeviceName, char *ConfigName);

int SCSI_CloseDevice(int DeviceFD); 
int CloseDevice(char *,int ); 

int SCSI_ExecuteCommand(int DeviceFD,
                        Direction_T Direction,
                        CDB_T CDB,
                        int CDB_Length,
                        void *DataBuffer,
                        int DataBufferLength,
                        char *RequestSense,
                        int RequestSenseLength);

#define TAPETYPE 4
#define STORAGE 2
#define CHANGER 1

#define NOT_READY 2
#define UNIT_ATTENTION 6

#define MAX_RETRIES 100

#define SCSI_OK 0
#define SCSI_SENSE 1

/*
 * Sense Key definitions
*/
#define SENSE_NOT_READY 0x2

/*
 *  SCSI Commands
*/
#define SC_COM_TEST_UNIT_READY 0
#define SC_COM_REWIND 0x1
#define SC_COM_REQUEST_SENSE 0x3
#define SC_COM_IES 0x7
#define SC_COM_INQUIRY 0x12
#define SC_COM_MODE_SELECT 0x15
#define SC_COM_ERASE 0x19
#define SC_COM_MODE_SENSE 0x1A
#define SC_COM_UNLOAD 0x1B
#define SC_COM_LOCATE 0x2B
#define SC_COM_LOG_SELECT 0x4C
#define SC_COM_LOG_SENSE 0x4d
#define SC_MOVE_MEDIUM 0xa5
#define SC_COM_RES 0xb8
/*
 * Define for LookupDevice
 */
#define LOOKUP_NAME 1
#define LOOKUP_FD 2
#define LOOKUP_TYPE 3
#define LOOKUP_CONFIG 4
/*
 * Defines for the function types in Changer_CMD_T
 */
#define CHG_MOVE 0
#define CHG_STATUS 1
#define CHG_RESET_STATUS 2
#define CHG_FREE 3
#define CHG_EJECT 4
#define CHG_CLEAN 5
#define CHG_REWIND 6
/*
 * Defines for the type field in the inquiry command
 */
#define TYPE_DISK 0
#define TYPE_TAPE 1
#define TYPE_PRINTER 2
#define TYPE_PROCESSOR 3
#define TYPE_WORM 4
#define TYPE_CDROM 5
#define TYPE_SCANNER 6
#define TYPE_OPTICAL 7
#define TYPE_CHANGER 8
#define TYPE_COMM 9
/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-file-style: gnu
 * End:
 */
