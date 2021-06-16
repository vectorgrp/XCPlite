/*----------------------------------------------------------------------------
| File:
|   xcp_cfg.h
|
| Description:
|   Konfiguration file for XCP protocol layer parameters
 ----------------------------------------------------------------------------*/

 /* Copyright(c) Vector Informatik GmbH.All rights reserved.
    Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __XCP_CFG_H_
#define __XCP_CFG_H_


 /*----------------------------------------------------------------------------*/
 /* Platform specific type definitions for xcpLite.c */

#include <stdint.h>

 /* 8-Bit  */
typedef uint8_t  vuint8;
typedef int8_t    vsint8;

/* 16-Bit  */
typedef uint16_t vuint16;
typedef int16_t   vsint16;

/* 32-Bit  */
typedef uint32_t   vuint32;
typedef int32_t     vsint32;

/* 64-Bit  */
typedef uint64_t  vuint64;
typedef int64_t    vsint64;

typedef uint8_t  vbool;
#define TRUE 1
#define FALSE 0


// Enable debug print (ApplXcpPrint)
#define XCP_ENABLE_TESTMODE 


 /*----------------------------------------------------------------------------*/
 /* Version */

// Driver version (GET_COMM_MODE_INFO)
#define XCP_DRIVER_VERSION 0x01

// Protocol layer version 
// #define XCP_PROTOCOL_LAYER_VERSION 0x0130  // GET_DAQ_CLOCK_MULTICAST, GET_TIME_CORRELATION_PROPERTIES
#define XCP_PROTOCOL_LAYER_VERSION 0x0140  // PACKED_MODE, CC_START_STOP_SYNCH prepare
// #define XCP_PROTOCOL_LAYER_VERSION 0x0160  // Experimental, changes marked with @@@@ V1.6


/*----------------------------------------------------------------------------*/
 /* Protocol features */

// #define XCP_ENABLE_DAQ_EVENT_INFO // Enable XCP_GET_EVENT_INFO, if this is enabled, A2L file event information will be ignored

#define XCP_ENABLE_DAQ_EVENT_LIST // Enable event list
#define XCP_MAX_EVENT 256 // Maximum number of events, size of event table

#define XCP_ENABLE_CHECKSUM // Enable checksum calculation command
#define XCP_ENABLE_CAL_PAGE // Enable cal page switch

#define XCP_ENABLE_A2L_UPLOAD // Enable GET_ID A2L content upload to host
#define XCP_ENABLE_A2L_NAME // Enable GET_ID A2L name upload to host

// XCP V1.4
//#define XCP_ENABLE_MULTICAST // Enable GET_DAQ_CLOCK_MULTICAST
//#define XCP_DAQ_CLOCK_64BIT  // Use 64 Bit time stamps
//#define XCP_ENABLE_PTP // Enable emulation of PTP synchronized slave DAQ time stamps
//#define XCP_ENABLE_PACKED_MODE // Enable packed mode emulation

// XCP V1.6


/*----------------------------------------------------------------------------*/
/* Settings and parameters */

#define XCP_DAQ_MEM_SIZE (5*100000) // Amount of memory for DAQ tables, each ODT entry needs 5 bytes

// Specify ApplXcpGetClock and ApplXcpGetClock64 resolution for DAQ time stamps
#define XCP_TIMESTAMP_UNIT DAQ_TIMESTAMP_UNIT_1US // unit DAQ_TIMESTAMP_UNIT_xxx
#define XCP_TIMESTAMP_TICKS 1  // ticks per unit
#define XCP_TIMESTAMP_TICKS_MS 1000 // ticks per millisecond


#endif



