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
#define TAPETYPE 4
#define IMPORT 3
#define STORAGE 2
#define CHANGER 1

#define TAG_SIZE 36

#define NOT_READY 2
#define HARDWARE_ERROR 4
#define ILLEGAL_REQUEST 5
#define UNIT_ATTENTION 6

#define MAX_RETRIES 100

#define INQUIRY_SIZE sizeof(SCSIInquiry_T)

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
 * Define for the return codes from SenseHandler
 */
#define SENSE_ABORT -1
#define SENSE_IGNORE 0
#define SENSE_NO_TAPE_ONLINE 1
#define SENSE_RETRY 2
#define SENSE_IES 3
#define SENSE_TAPE_NOT_UNLOADED 4
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
#define CHG_BARCODE 7
#define CHG_SEARCH 8
#define CHG_ERROR 9
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

#define LABEL_DB_VERSION 1
#define BARCODE_PUT 1
#define BARCODE_VOL 2
#define BARCODE_BARCODE 3

typedef struct {
  char voltag[128];
  char barcode[TAG_SIZE];
  unsigned char valid;
} LabelV1_T;

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
    unsigned char res4[4];
} MediumTransportElementDescriptor_T;

/* ======================================================= */
/* ImportExportElementDescriptor_T */
/* ======================================================= */
typedef struct 
{
  unsigned char address[2];   /* Byte 1 = Element Address */
#ifdef LITTLE_ENDIAN_BITFIELDS        
  PackedBit     full   : 1;
  PackedBit     impexp : 1;
  PackedBit     except : 1;
  PackedBit     access : 1;
  PackedBit     exenab : 1;
  PackedBit     inenab : 1;
  PackedBit     res    : 2;
#else
  PackedBit     res    : 2;
  PackedBit     inenab : 1;
  PackedBit     exenab : 1;
  PackedBit     access : 1;
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
    unsigned char res4[4];
} ImportExportElementDescriptor_T;

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
    PackedBit     type : 5;
    PackedBit     qualifier : 3;

    PackedBit     type_modifier : 7;
    PackedBit     removable : 1;

    PackedBit     ansi_version : 3;
    PackedBit     ecma_version : 3;
    PackedBit     iso_version : 2;

    PackedBit     data_format : 4;
    PackedBit     res3_54 : 2;
    PackedBit     termiop : 1;
    PackedBit     aenc : 1;
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
} ModeSenseHeader_T;
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
/*
 * Pages returned by the MODE_SENSE command
 */
typedef struct {
    unsigned char SenseDataLength;
    char res[3];
} ParameterListHeader_T;

typedef struct 
{
#ifdef LITTLE_ENDIAN_BITFIELDS
    PackedBit PageCode : 6;
    PackedBit RSVD     : 1;
    PackedBit PS       : 1;
#else
    PackedBit PS       : 1;
    PackedBit RSVD     : 1;
    PackedBit PageCode : 6;
#endif
    unsigned char ParameterListLength;
    unsigned char MediumTransportElementAddress[2];
    unsigned char NoMediumTransportElements[2];
    unsigned char FirstStorageElementAddress[2];
    unsigned char NoStorageElements[2];
    unsigned char FirstImportExportElementAddress[2];
    unsigned char NoImportExportElements[2];
    unsigned char FirstDataTransferElementAddress[2];
    unsigned char NoDataTransferElements[2];
    unsigned char res[2];
} EAAPage_T;    

typedef struct {
#ifdef LITTLE_ENDIAN_BITFIELDS
    PackedBit PageCode : 6;
    PackedBit RSVD     : 1;
    PackedBit PS       : 1;
#else
    PackedBit PS       : 1;
    PackedBit RSVD     : 1;
    PackedBit PageCode : 6;
#endif
    unsigned char ParameterListLength;
#ifdef LITTLE_ENDIAN_BITFIELDS
    PackedBit Rotate    : 1;
    PackedBit res       : 7;
#else
    PackedBit res       : 7;
    PackedBit Rotate    : 1;
#endif
    unsigned char MemberNumber;
} TransportGeometryDescriptorPage_T;  

typedef struct
{
#ifdef LITTLE_ENDIAN_BITFIELDS
    PackedBit PageCode : 6;
    PackedBit RSVD     : 1;
    PackedBit PS       : 1;
#else
    PackedBit PS       : 1;
    PackedBit RSVD     : 1;
    PackedBit PageCode : 6;
#endif
    unsigned char ParameterLength;
#ifdef LITTLE_ENDIAN_BITFIELDS
    PackedBit MT        : 1;
    PackedBit ST        : 1;
    PackedBit IE        : 1;
    PackedBit DT        : 1;
    PackedBit res1      : 4;
#else
    PackedBit res1      : 4;
    PackedBit DT        : 1;
    PackedBit IE        : 1;
    PackedBit ST        : 1;
    PackedBit MT        : 1;
#endif
    unsigned char res;
#ifdef LITTLE_ENDIAN_BITFIELDS
    PackedBit MT2MT     : 1;
    PackedBit MT2ST     : 1;
    PackedBit MT2IE     : 1;
    PackedBit MT2DT     : 1;
    PackedBit res2      : 4;
#else
    PackedBit res2      : 4;
    PackedBit MT2DT     : 1;
    PackedBit MT2IE     : 1;
    PackedBit MT2ST     : 1;
    PackedBit MT2MT     : 1;
#endif

#ifdef LITTLE_ENDIAN_BITFIELDS
    PackedBit ST2MT     : 1;
    PackedBit ST2ST     : 1;
    PackedBit ST2IE     : 1;
    PackedBit ST2DT     : 1;
    PackedBit res3      : 4;
#else
    PackedBit res3      : 4;
    PackedBit ST2DT     : 1;
    PackedBit ST2IE     : 1;
    PackedBit ST2ST     : 1;
    PackedBit ST2MT     : 1;
#endif

#ifdef LITTLE_ENDIAN_BITFIELDS
    PackedBit IE2MT     : 1;
    PackedBit IE2ST     : 1;
    PackedBit IE2IE     : 1;
    PackedBit IE2DT     : 1;
    PackedBit res4      : 4;
#else
    PackedBit res4      : 4;
    PackedBit IE2DT     : 1;
    PackedBit IE2IE     : 1;
    PackedBit IE2ST     : 1;
    PackedBit IE2MT     : 1;
#endif

#ifdef LITTLE_ENDIAN_BITFIELDS
    PackedBit DT2MT     : 1;
    PackedBit DT2ST     : 1;
    PackedBit DT2IE     : 1;
    PackedBit DT2DT     : 1;
    PackedBit res5      : 4;
#else
    PackedBit res5      : 4;
    PackedBit DT2DT     : 1;
    PackedBit DT2IE     : 1;
    PackedBit DT2ST     : 1;
    PackedBit DT2MT     : 1;
#endif
    unsigned char res0819[12];
} DeviceCapabilitiesPage_T;  

typedef struct ModePageEXB10hLCD
{
  unsigned char PageCode;
  unsigned char ParameterListLength;

#ifdef LITTLE_ENDIAN_BITFIELDS
  PackedBit WriteLine4 : 1;
  PackedBit WriteLine3 : 1;
  PackedBit WriteLine2 : 1;
  PackedBit WriteLine1 : 1;
  PackedBit res        : 4;
#else
  PackedBit res        : 4;
  PackedBit WriteLine1 : 1;
  PackedBit WriteLine2 : 1;
  PackedBit WriteLine3 : 1;
  PackedBit WriteLine4 : 1;
#endif
  unsigned char reserved;
  unsigned char line1[20];
  unsigned char line2[20];
  unsigned char line3[20];
  unsigned char line4[20];
} ModePageEXB10hLCD_T;

typedef struct ModePageEXBBaudRatePage
{
  unsigned char PageCode;
  unsigned char ParameterListLength;
  unsigned char BaudRate[2];
} ModePageEXBBaudRatePage_T;

typedef struct ModePageEXB120VendorUnique
{
#ifdef  LITTLE_ENDIAN_BITFIELDS
    PackedBit PageCode : 6;
    PackedBit RSVD0    : 1;
    PackedBit PS       : 1;
#else
    PackedBit PS       : 1;
    PackedBit RSVD0    : 1;
    PackedBit PageCode : 6;
#endif
    unsigned char ParameterListLength;
#ifdef LITTLE_ENDIAN_BITFIELDS
    PackedBit MDC  : 2;
    PackedBit NRDC : 1;
    PackedBit RSVD : 1;
    PackedBit NBL  : 1;
    PackedBit PRTY : 1;
    PackedBit UINT : 1;
    PackedBit AINT : 1;
#else
    PackedBit AINT : 1;
    PackedBit UINT : 1;
    PackedBit PRTY : 1;
    PackedBit NBL  : 1;
    PackedBit RSVD : 1;
    PackedBit NRDC : 1;
    PackedBit MDC  : 2;
#endif
    unsigned char MaxParityRetries;
    unsigned char DisplayMessage[60];
} ModePageEXB120VendorUnique_T;
/* ======================================================= */
/* ElementInfo_T */
/* ======================================================= */
typedef struct ElementInfo
{
    int type;       /* CHANGER - 1, STORAGE - 2, TAPE - 4 */
    int address;    /* Adress of this Element */
    int from;       /* From where did it come */
    char status;    /* F -> Full, E -> Empty */
    char VolTag[TAG_SIZE]; /* Label Info if Barcode reader exsist */
    int ASC;        /* Additional Sense Code from read element status */
    int ASCQ;      /* */
    unsigned char scsi; /* if DTE, which scsi address */

  PackedBit svalid : 1;
  PackedBit invert : 1;
  PackedBit full   : 1;
  PackedBit impexp : 1;
  PackedBit except : 1;
  PackedBit access : 1;
  PackedBit inenab : 1;
  PackedBit exenab : 1;

} ElementInfo_T;



typedef struct {
    char *ident;                  /* Name of the device from inquiry */
    int (*function[10])();        /* New way to call the device dependend functions move/eject ... */
} ChangerCMD_T ;

typedef struct {
    unsigned char command;        /* The SCSI command byte */
    int length;                   /* How long */
    char *name;                   /* Name of the command */
} SC_COM_T;

typedef struct OpenFiles {
    int fd;                       /* The foledescriptor */
    unsigned char SCSI;           /* Can we send SCSI commands */
    char *dev;                    /* The device which is used */
    char *ConfigName;             /* The name in the config */
    char ident[17];               /* The identifier from the inquiry command */
    ChangerCMD_T *functions;      /* Pointer to the function array for this device */
    SCSIInquiry_T *inquiry;       /* The result from the Inquiry */
} OpenFiles_T;

typedef struct LogPageDecode {
    int LogPage;
    char *ident;
    void (*decode)(LogParameter_T *, int);
} LogPageDecode_T;

/* ======================================================= */
/* Funktion-Declaration */
/* ======================================================= */
OpenFiles_T *SCSI_OpenDevice(char *DeviceName);
OpenFiles_T *OpenDevice(char *DeviceName, char *ConfigName);

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

/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-file-style: gnu
 * End:
 */
