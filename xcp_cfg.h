/*----------------------------------------------------------------------------
| File:
|   xcp_cfg.h
|
| Description:
|   User configuration file for XCP protocol layer parameters
 ----------------------------------------------------------------------------*/

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
// #define XCP_PROTOCOL_LAYER_VERSION 0x0101 
// #define XCP_PROTOCOL_LAYER_VERSION 0x0103  // GET_DAQ_CLOCK_MULTICAST, GET_TIME_CORRELATION_PROPERTIES
#define XCP_PROTOCOL_LAYER_VERSION 0x0104  // PACKED_MODE, CC_START_STOP_SYNCH prepare
// #define XCP_PROTOCOL_LAYER_VERSION 0x0106  // Experimental, changes marked with @@@@ V1.6


/*----------------------------------------------------------------------------*/
 /* Protocol features */

// #define XCP_ENABLE_DAQ_EVENT_INFO // Enable XCP_GET_EVENT_INFO, if this is enabled, A2L file event information will be ignored

#define XCP_ENABLE_DAQ_EVENT_LIST // Enable event list
#define XCP_MAX_EVENT 256 // Maximum number of events, size of event table

#ifdef APP_ENABLE_CAL_SEGMENT
  #define XCP_ENABLE_CHECKSUM // Enable checksum calculation command
  #define XCP_ENABLE_CAL_PAGE // Enable cal page switching co
#endif

#define XCP_ENABLE_FILE_UPLOAD // Enable GET_ID A2L content upload to host
#define XCP_ENABLE_A2L_NAME // Enable GET_ID A2L name upload to host

// XCP V1.3
#ifdef APP_ENABLE_MULTICAST
  #define XCP_ENABLE_DAQ_CLOCK_MULTICAST // Enable GET_DAQ_CLOCK_MULTICAST
#endif


/*----------------------------------------------------------------------------*/
/* Settings and parameters */

#define XCP_DAQ_MEM_SIZE (5*10000) // Amount of memory for DAQ tables, each ODT entry needs 5 bytes


#ifdef CLOCK_USE_UTC_TIME_NS  // Clock type defined in main.h

    // Slave clock (mandatory)
    // Specify ApplXcpGetClock and ApplXcpGetClock64 resolution for DAQ time stamps 
    #define XCP_DAQ_CLOCK_64BIT  // Use 64 Bit time stamps in GET_DAQ_CLOCK
    #define XCP_DAQ_CLOCK_EPOCH XCP_EPOCH_TAI
    #define XCP_TIMESTAMP_SIZE 4 // Use 32 Bit time stamps in DAQ DTO
    #define XCP_TIMESTAMP_UNIT DAQ_TIMESTAMP_UNIT_1NS // unit DAQ_TIMESTAMP_UNIT_xxx
    #define XCP_TIMESTAMP_TICKS CLOCK_TICKS_PER_NS  // ticks per unit

    // Grandmaster clock (optional, needs to use XcpSetGrandmasterClockInfo, ApplXcpGetClockInfo) 
    #define XCP_ENABLE_GRANDMASTER_CLOCK_INFO

#else

    #define XCP_DAQ_CLOCK_EPOCH XCP_EPOCH_ARB
    #define XCP_TIMESTAMP_SIZE 4 // Use 32 Bit time stamps in DAQ DTO
    #define XCP_TIMESTAMP_UNIT DAQ_TIMESTAMP_UNIT_1US // unit DAQ_TIMESTAMP_UNIT_xxx
    #define XCP_TIMESTAMP_TICKS CLOCK_TICKS_PER_US  // ticks per unit

#endif


#define XCP_TIMESTAMP_TICKS_S CLOCK_TICKS_PER_S // ticks per s (for debug output)




#endif



