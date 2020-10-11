/*----------------------------------------------------------------------------
| File:
|   XCP_CFG.H
|   V1.0 23.9.2020
|
| Project:
|   Konfiguration file for XCP basic driver
|   Linux XCP on UDP demo (Tested on RaspberryPi4)
 ----------------------------------------------------------------------------*/

#if !defined ( __XCP_CFG_H__ )
#define __XCP_CFG_H__


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

#include <pthread.h> // link with -lpthread

#include <assert.h>

#include <errno.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <arpa/inet.h>


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
/* Test instrumentation */

/* Turn on screen logging, assertions and parameter checks */

#define XCP_ENABLE_TESTMODE
#ifdef XCP_ENABLE_TESTMODE
  #define ApplXcpPrint printf
  #define XCP_ENABLE_PARAMETER_CHECK
#endif


/*----------------------------------------------------------------------------*/
/* XCP protocol and transport layer parameters */

/* Transport layer */
#define XCP_UDP_MTU (1400)  // IPv4 1500 ETH - 28 IP - 8 UDP ???
#define XCP_TRANSPORT_LAYER_VERSION 0x0100

/* XCP slave device identification (optional) */
#define kXcpStationIdLength 5    /* Slave device identification length */
#define kXcpStationIdString "xcpPi"  /* Slave device identification */
extern vuint8 MEMORY_ROM gXcpStationId[];

/* XCP protocol message sizes */
#define kXcpMaxCTO     250      /* Maximum CTO and CRM Message Lenght */
#define kXcpMaxDTO     (XCP_UDP_MTU-4)      /* Maximum DTO Message Lenght UDP_MTU - Transport Layer Header */

/* Transmit queue (DAQ) */
#define DTO_SEND_QUEUE
#define DTO_QUEUE_SIZE 32
//#define DTO_SEND_RAW

/* DAQ table size */
#define kXcpDaqMemSize 60000u  // Memory space reserved for DAQ tables (XCP needs 5 bytes (addr+len) per memory region (odt entry)

/* DAQ timestamp settings */
#define kApplXcpDaqTimestampTicksPerMs 1000 
extern vuint32 ApplXcpTimer(void);
#define kXcpDaqTimestampUnit DAQ_TIMESTAMP_UNIT_1US
#define kXcpDaqTimestampTicksPerUnit 1  
#define ApplXcpGetTimestamp() ApplXcpTimer()


/*----------------------------------------------------------------------------*/
/* XCP protocol features */

#define XCP_ENABLE_CALIBRATION



#endif


