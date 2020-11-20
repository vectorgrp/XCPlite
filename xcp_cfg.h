/*----------------------------------------------------------------------------
| File:
|   XCP_CFG.H
|
| Description:
|   Konfiguration file for XCP lite protocol and transport layer
|   Linux (Raspberry Pi) Version
 ----------------------------------------------------------------------------*/

#ifndef __XCP_CFG_H_
#define __XCP_CFG_H_


// General includes
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>


#include <pthread.h> // link with -lpthread

#include <assert.h>

#include <errno.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------------------------*/
/* Platform specific definitions */

  /* 8-Bit  */
typedef unsigned char  vuint8;
typedef signed char    vsint8;

/* 16-Bit  */
typedef unsigned short vuint16;
typedef signed short   vsint16;

/* 32-Bit  */
typedef unsigned long  vuint32;
typedef signed long    vsint32;

/* Byte order */
//#define XCP_CPUTYPE_BIGENDIAN  /* Motorola */
#define XCP_CPUTYPE_LITTLEENDIAN /* Intel */

/* Memory qualifiers */
#define MEMORY_CONST const
#define MEMORY_ROM const

/* Pointers */
#define DAQBYTEPTR vuint8 *
#define MTABYTEPTR vuint8 *
#define BYTEPTR vuint8 *


/*----------------------------------------------------------------------------*/
/* XCP Driver Callbacks as macros */

extern int udpServerSendCrmPacket(const unsigned char* data, unsigned int n);
extern unsigned char* udpServerGetPacketBuffer(void** par, unsigned int size);
extern void udpServerCommitPacketBuffer(void* par);

// Convert a XCP (BYTE addrExt, DWORD addr from A2L) address to a C pointer to unsigned byte
// extern BYTEPTR ApplXcpGetPointer(vuint8 addr_ext, vuint32 addr)
#define ApplXcpGetPointer(e,a) ((BYTEPTR)((a)))

// Get and commit buffer space for a DTO message
#define ApplXcpGetDtoBuffer udpServerGetPacketBuffer
#define ApplXcpCommitDtoBuffer udpServerCommitPacketBuffer

// Send a CRM message
#define ApplXcpSendCrm udpServerSendCrmPacket


/*----------------------------------------------------------------------------*/
/* A2L creator */

#define XCP_ENABLE_A2L

#ifdef XCP_ENABLE_A2L

#endif


/*----------------------------------------------------------------------------*/
/* Test instrumentation */

/* Turn on screen logging, assertions and parameter checks */

#define XCP_ENABLE_TESTMODE
#ifdef XCP_ENABLE_TESTMODE
  #define XCP_DEBUG_LEVEL 1
  #define ApplXcpPrint printf
  #define XCP_ENABLE_PARAMETER_CHECK

  #include <wiringPi.h>
  #define PI_IO_1	17
  #define ApplXcpDbgPin(x) digitalWrite(PI_IO_1,x);

#else
  ApplXcpDbgPin(x)
#endif
	  
extern volatile vuint32 gTaskCycleTimerECU;
extern volatile vuint32 gTaskCycleTimerECUpp;

/*----------------------------------------------------------------------------*/
/* XCP protocol and transport layer parameters */

/* Transport layer */
#define XCP_UDP_MTU (1400)  // IPv4 1500 ETH - 28 IP - 8 UDP ???


/* XCP slave device and A2L file identification (optional) */
#define kXcpSlaveIdLength 5    /* Slave device identification length */
#define kXcpSlaveIdString "XCPpi"  /* Slave device identification */
extern vuint8 MEMORY_ROM gXcpSlaveId[];
#define kXcpA2LFilenameLength 9    /* Length of A2L filename */
#define kXcpA2LFilenameString "XCPpi.A2L"  /* A2L filename */
extern vuint8 MEMORY_ROM gXcpA2LFilename[];


/* XCP protocol message sizes */
#define kXcpMaxCTO     250      /* Maximum CTO and CRM Message Lenght */
#define kXcpMaxDTO     (XCP_UDP_MTU-4)      /* Maximum DTO Message Lenght UDP_MTU - Transport Layer Header */

/* Transmit queue (DAQ) */
#define DTO_SEND_QUEUE       /* Enable DTO packet queue, decouples xcpEvent from sendto, results in almost deterministic runtime of xcpEvent */
#define DTO_QUEUE_SIZE 32    /* Transmit queue size in DAQ UDP packets, should at least be able to hold all data produced by the largest event */
//#define DTO_SEND_RAW         /* Activate UDP on RAW socket for DAQ transmission */

/* DAQ table size */
#define kXcpDaqMemSize 60000u  // Memory space reserved for DAQ tables (XCP needs 5 bytes (addr+len) per memory region (odt entry)

/* Maximum ODT entry size */
  #define XCP_MAX_ODT_ENTRY_SIZE 248 // mod 4 = 0 to optimize DAQ copy granularity

/* DAQ timestamp settings */
#define kApplXcpDaqTimestampTicksPerMs 1000 
extern vuint32 ApplXcpTimer(void);
#define kXcpDaqTimestampUnit DAQ_TIMESTAMP_UNIT_1US
#define kXcpDaqTimestampTicksPerUnit 1  
#define ApplXcpGetTimestamp() ApplXcpTimer()


/*----------------------------------------------------------------------------*/
/* XCP protocol features */

#define XCP_ENABLE_CALIBRATION


#ifdef __cplusplus
}
#endif

#endif


