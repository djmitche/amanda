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
	unsigned char type;		/* Byte 1 = Element Type Code*/
#ifdef LITTLE_ENDIAN_BITFIELDS        
	PackedBit     bitres  : 6;
	PackedBit     avoltag : 1;
	PackedBit     pvoltag : 1;
#else
	PackedBit     pvoltag : 1;
	PackedBit     avoltag : 1;
	PackedBit     bitres  : 6;
#endif
	unsigned char length[2];	/* Byte 3-4  = Element Descriptor Length */
	unsigned char byte4;		/* Byte 5 */
	unsigned char count[3];		/* Byte 6-8 = Byte Count of Descriptor Available */
} ElementStatusPage_T;


/* ======================================================= */
/* ElementStatusData_T */
/* ======================================================= */
typedef struct 
{
   unsigned char first[2];    /* Byte 1-2 = First Element Adress Reported */
    unsigned char number[2];   /* Byte 3-4 = Number of Elements Available */
    unsigned char byte5;      /* Reserved */
    unsigned char count[3];		/* Byte 6-8 = Byte Count of Report Available */
} ElementStatusData_T;

/* ======================================================= */
/* MediumTransportElementDescriptor_T */
/* ======================================================= */
typedef struct 
{
	unsigned char address[2];	/* Byte 1 = Element Address */
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
#else
	PackedBit     svalid : 1;
	PackedBit     invert : 1;
	PackedBit     byte10res : 6;
#endif
	unsigned char ssea[2];
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
/* ElementInfo_T */
/* ======================================================= */
typedef struct ElementInfo_T
{
	int type;		/* CHANGER - 1, STORAGE - 2, TAPE - 4 */
	int address;	/* Adress of this Element */
	char status;	/* F -> Full, E -> Empty */
	int ASC;		/* Additional Sense Code from read element status */
	int ASCQ;      /* */
	struct ElementInfo_T *next; /* Pointer to next ElementInfo */
} ElementInfo_T;


/* ======================================================= */
/* Funktion-Declaration */
/* ======================================================= */
int SCSI_OpenDevice(char *DeviceName);

void SCSI_CloseDevice(char *DeviceName, 
		      int DeviceFD);

int SCSI_ExecuteCommand(int DeviceFD,
			Direction_T Direction,
			CDB_T CDB,
			int CDB_Length,
			void *DataBuffer,
			int DataBufferLength,
			char *RequestSense,
                        int RequestSenseLength);

struct ChangerCMD {
  char *ident; /* Length of prod_ident from inquiry */
  int (*move)(int, int, int);     /* Move Element */
  int (*status)(int, ElementInfo_T **);   /* Read Element Status */
  int(*resetstatus)(int);                        /* Reset Element inventory */
  int (*free)();     /* Is slot free */
  int (*eject)();    /* Eject tape from drive */
  int (*clean)();    /* Cleaning Flag set, Vendor specific */
};

#define TAPETYPE 4
#define STORAGE 2
#define CHANGER 1

#define NOT_READY 2
#define UNIT_ATTENTION 6

#define MAX_RETRIES 100
