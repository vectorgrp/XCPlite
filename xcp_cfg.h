/*----------------------------------------------------------------------------
| File:
|   XCP_CFG.H
|
| Description:
|   Konfiguration file for XCP lite protocol and transport layer parameters
|   Linux (Raspberry Pi) or Windows version
 ----------------------------------------------------------------------------*/

#ifndef __XCP_CFG_H_
#define __XCP_CFG_H_

#ifdef _WIN32
#define _WIN
#endif
#ifdef _WIN64
#error WIN64 not implemented yet
#endif

 /*----------------------------------------------------------------------------*/
 /* Protocol and debugging options */

#define XCP_ENABLE_A2L // Enable A2L creator and A2L upload to host

#ifdef _WIN // Windows
   #define XCP_ENABLE_XLAPI // Enable Vector XLAPI instead of Winsock Network Adapter
#endif
#ifndef _WIN // Linus
// #define XCP_ENABLE_SO // Enable measurement and calibration of shared objects
// #define XCP_ENABLE_PTP // Enable PTP synchronized DAQ time stamps
#endif

#define XCP_ENABLE_TESTMODE // Enable debug console prints
#define XCP_DEBUG_LEVEL 1

#ifndef _WIN // Linux

#define _LINUX
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>
#include <pthread.h> // link with -lpthread
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
//#include <linux/arp.h>
#include <arpa/inet.h>

#else // Windows

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#ifdef XCP_ENABLE_XLAPI

// Need to link with vxlapi.lib
#pragma comment(lib, "vxlapi.lib")
#include "vxlapi.h"

#else

// Need to link with Ws2_32.lib
#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>

#endif
#endif


#ifdef __cplusplus
extern "C" {
#endif




/*----------------------------------------------------------------------------*/
/* Important Protocol settings and parameters */

#ifndef _WIN // Linux
  #define XCP_SLAVE_IP_S "172.31.31.194" // Only for A2L generator
  #define XCP_SLAVE_PORT 5555
  #define XCP_SLAVE_PORT_S "5555"
#else // Windows
#ifdef XCP_ENABLE_XLAPI // Enable Vector XLAPI as Network Adapter
  #define XCP_SLAVE_NET "NET1" // V3 Network
  #define XCP_SLAVE_SEG "SEG1" // V3 Segment
  #define XCP_SLAVE_NAME "XCPlite" // V3 Application Name
  #define XCP_SLAVE_IP_S "172.31.31.194" // V3 Ethernet Adapter IP
  #define XCP_SLAVE_IP {172,31,31,194} // V3 Ethernet Adapter IP
  #define XCP_SLAVE_MAC {0xdc,0xa6,0x32,0x7e,0x66,0xdc} // V3 Ethernet Adapter MAC
  #define XCP_SLAVE_UUID {0xdc,0xa6,0x32,0xFF,0xFE,0x7e,0x66,0xdc} // V3 PTP UUID
#define XCP_SLAVE_PORT 5555
#define XCP_SLAVE_PORT_S "5555"
#else
  #define XCP_SLAVE_IP_S "127.0.0.1" // Localhost default for winsock
  #define XCP_SLAVE_IP {127,0,0,1} 
#define XCP_SLAVE_PORT 5555
#define XCP_SLAVE_PORT_S "5555"
#endif
#endif

#define XCP_SLAVE_PORT 5555
#define XCP_SLAVE_PORT_S "5555"


// XCP slave device and A2L file identification (optional)
#define kXcpSlaveIdLength 7    /* Slave device identification length */
#define kXcpSlaveIdString "XCPlite"  /* Slave device identification */
#define kXcpA2LFilenameLength 11    /* Length of A2L filename */
#define kXcpA2LFilenameString "XCPlite.A2L"  /* A2L filename */

// The following parameters are dependant on the amount of measurement signals supported, they have significant impact on memory consumption
#ifndef _WIN // Windows needs much larger buffers to achieve more performance
  #define XCP_DAQ_QUEUE_SIZE 64   // Transmit queue size in DAQ UDP packets (MTU=1400), should at least be able to hold all data produced by the largest event
  #define XCP_DAQ_MEM_SIZE (32*1024) // Amount of memory for DAQ tables, each ODT entry needs 5 bytes
#else
#define XCP_DAQ_QUEUE_SIZE (4*1024)   // Transmit queue size in DAQ UDP packets (MTU=1400), should at least be able to hold all data produced by the largest event
#define XCP_DAQ_MEM_SIZE (64*1024-1) // Amount of memory for DAQ tables, each ODT entry needs 5 bytes
#endif

// Transmit mode
#define DTO_SEND_QUEUE       /* Enable DTO packet queue, decouples xcpEvent from sendto, results in almost deterministic runtime of xcpEvent */


/*----------------------------------------------------------------------------*/
/* Test instrumentation */

/* Turn on screen logging, assertions and parameter checks */
#ifdef XCP_ENABLE_TESTMODE
  #define ApplXcpPrint printf
  #define XCP_ENABLE_PARAMETER_CHECK
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
typedef unsigned int   vuint32;
typedef signed int     vsint32;

/* 64-Bit  */
typedef unsigned long long  vuint64;
typedef signed long long    vsint64;


/* Byte order */
//#define XCP_CPUTYPE_BIGENDIAN  /* Motorola */
#define XCP_CPUTYPE_LITTLEENDIAN /* Intel */


/* Pointers */
#define DAQBYTEPTR vuint8 *
#define MTABYTEPTR vuint8 *
#define BYTEPTR vuint8 *


/*----------------------------------------------------------------------------*/
/* XCP Driver Callbacks for pointer/address conversions */

#ifdef XCP_ENABLE_SO

#define XCP_MAX_MODULE 1

// Convert a XCP (BYTE addrExt, DWORD addr from A2L) address to a C pointer to unsigned byte
extern BYTEPTR ApplXcpGetPointer(vuint8 addr_ext, vuint32 addr);

// Convert a pointer to XCP (DWORD address from A2L) address
extern vuint32 ApplXcpGetAddr(BYTEPTR addr);

// Init module base address list
extern void ApplXcpInitBaseAddressList();

#else

#define ApplXcpGetPointer(e,a) ((BYTEPTR)((a)))
#define ApplXcpGetAddr(p) ((unsigned int)((p)))

#endif


/*----------------------------------------------------------------------------*/
/* XCP Driver Transport Layer Callbacks as macros */

// Get and commit buffer space for a DTO message
extern unsigned char* udpServerGetPacketBuffer(void** par, unsigned int size);
extern void udpServerCommitPacketBuffer(void* par);
#define ApplXcpGetDtoBuffer udpServerGetPacketBuffer
#define ApplXcpCommitDtoBuffer udpServerCommitPacketBuffer

// Send a CRM message
extern int udpServerSendCrmPacket(const unsigned char* data, unsigned int n);
#define ApplXcpSendCrm udpServerSendCrmPacket


/*----------------------------------------------------------------------------*/
/* XCP protocol and transport layer parameters */

/* Transport layer */
#define kXcpMaxMTU (1400)  // IPv4 1500 ETH - 28 IP - 8 UDP ???

/* XCP protocol message sizes */
#define kXcpMaxCTO     250      /* Maximum CTO and CRM Message Lenght */
#define kXcpMaxDTO     (kXcpMaxMTU-4)      /* Maximum DTO Message Lenght UDP_MTU - Transport Layer Header */

/* Maximum ODT entry size */
#define kXcpMaxOdtEntrySize 248 // mod 4 = 0 to optimize DAQ copy granularity

/* DAQ time stamping */
extern vuint32 ApplXcpGetClock(void);

#ifdef XCP_ENABLE_PTP // Experimental -  UUIDs hardcoded
   #define TIMESTAMP_DLONG
	//dca632.fffe.79a251
	#define MASTER_UUID {0xdc,0xa6,0x32,0xFF,0xFE,0x79,0xa2,0x51}
    extern vuint64 ApplXcpGetClock64(void);
#endif

#define kApplXcpDaqTimestampTicksPerMs 1000 
#define kXcpDaqTimestampUnit DAQ_TIMESTAMP_UNIT_1US
#define kXcpDaqTimestampTicksPerUnit 1  

	
#ifdef __cplusplus
}
#endif

#endif


