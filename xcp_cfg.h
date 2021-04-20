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

#ifndef _WIN // Linux

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
#include <arpa/inet.h>

#else // Windows

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <assert.h>

// Need to link with Ws2_32.lib
#pragma comment(lib, "ws2_32.lib")

#endif


#ifdef __cplusplus
extern "C" {
#endif


/*----------------------------------------------------------------------------*/
/* Protocol and debugging options */

//#define XCP_ENABLE_64 // Enable 64 bit address space
	
#define XCP_ENABLE_A2L // Enable A2L creator and A2L upload to host

// #define XCP_ENABLE_SO // Enable measurement and calibration of shared objects

// #define XCP_ENABLE_PTP // Enable PTP synchronized DAQ time stamps

#define XCP_ENABLE_TESTMODE // Enable debug console prints
#define XCP_DEBUG_LEVEL 1

// #define XCP_ENABLE_WIRINGPI // Enable digital io 


/*----------------------------------------------------------------------------*/
/* Important Protocol settings and parameters */
	
// XCP slave IP address
#define XCP_SLAVE_IP "172.31.31.194"  // IP is hardcode yet, should be improved in future versions

// XCP slave device and A2L file identification (optional)
#define kXcpSlaveIdLength 7    /* Slave device identification length */
#define kXcpSlaveIdString "XCPlite"  /* Slave device identification */
#define kXcpA2LFilenameLength 11    /* Length of A2L filename */
#define kXcpA2LFilenameString "XCPlite.A2L"  /* A2L filename */

// The following parameters are dependant on the amount of measurement signals supported, they have significant impact on memory consumption
#ifndef _WIN
  #define XCP_DAQ_QUEUE_SIZE 64   // Transmit queue size in DAQ UDP packets (MTU=1400), should at least be able to hold all data produced by the largest event
#else
#define XCP_DAQ_QUEUE_SIZE 256   // Transmit queue size in DAQ UDP packets (MTU=1400), should at least be able to hold all data produced by the largest event
#endif
#define XCP_DAQ_MEM_SIZE 60000u // Amount of memory for DAQ tables, each ODT entries needs 5 bytes

// Transmit mode
#define DTO_SEND_QUEUE       /* Enable DTO packet queue, decouples xcpEvent from sendto, results in almost deterministic runtime of xcpEvent */
//#define DTO_SEND_RAW       /* Activate build in UDP stack on RAW sockets for DAQ transmission */


/*----------------------------------------------------------------------------*/
/* Test instrumentation */

/* Turn on screen logging, assertions and parameter checks */

#ifdef XCP_ENABLE_TESTMODE
#define ApplXcpPrint printf
#define XCP_ENABLE_PARAMETER_CHECK

#endif

#ifdef XCP_ENABLE_WIRINGPI
#include <wiringPi.h>
#define PI_IO_1	17
#define ApplXcpDbgPin(x) digitalWrite(PI_IO_1,x);
#else
#define ApplXcpDbgPin(x)
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

	#define XCP_SLAVE2

	#ifdef XCP_SLAVE2
	#define SLAVE_IP "172.31.31.195"
	#define SLAVE_MAC {0xdc,0xa6,0x32,0x2e,0x7d,0xe0}
	#define SLAVE_UUID {0xdc,0xa6,0x32,0xFF,0xFE,0x2e,0x7d,0xe0}
	#else
	#define SLAVE_IP "172.31.31.194"
	#define SLAVE_MAC {0xdc,0xa6,0x32,0x7e,0x66,0xdc}
	#define SLAVE_UUID {0xdc,0xa6,0x32,0xFF,0xFE,0x7e,0x66,0xdc}
	#endif

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


