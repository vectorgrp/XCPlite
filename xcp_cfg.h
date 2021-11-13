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


// Enable debug print (printf)
#define XCP_ENABLE_TESTMODE 


 /*----------------------------------------------------------------------------*/
 /* Version */

// Driver version (GET_COMM_MODE_INFO)
#define XCP_DRIVER_VERSION 0x01

// Protocol layer version 
// #define XCP_PROTOCOL_LAYER_VERSION 0x0101 
// #define XCP_PROTOCOL_LAYER_VERSION 0x0103  // GET_DAQ_CLOCK_MULTICAST, GET_TIME_CORRELATION_PROPERTIES
#define XCP_PROTOCOL_LAYER_VERSION 0x0104  // PACKED_MODE, CC_START_STOP_SYNCH prepare


/*----------------------------------------------------------------------------*/
/* Protocol features */

#define XCP_ENABLE_INTERLEAVED
#define XCP_INTERLEAVED_QUEUE_SIZE 16

#define XCP_ENABLE_CHECKSUM // Enable checksum calculation command
#define XCP_ENABLE_CAL_PAGE // Enable cal page switch

#define XCP_ENABLE_FILE_UPLOAD // Enable GET_ID A2L content upload to host
#define XCP_ENABLE_A2L_NAME // Enable GET_ID A2L name upload to host


/*----------------------------------------------------------------------------*/
/* DAQ features and parameters */

// #define XCP_ENABLE_DAQ_EVENT_INFO // Enable XCP_GET_EVENT_INFO, if this is enabled, A2L file event information will be ignored

#define XCP_ENABLE_DAQ_EVENT_LIST // Enable event list
#define XCP_MAX_EVENT 256 // Maximum number of events, size of event table

// XCP V1.4
#define XCP_ENABLE_DAQ_CLOCK_MULTICAST // Enable GET_DAQ_CLOCK_MULTICAST
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
    // XCP default cluster id (multicast addr 239,255,0,1, group 127,0,1 (mac 01-00-5E-7F-00-01)
    #define XCP_MULTICAST_CLUSTER_ID 1
#endif


#define XCP_DAQ_MEM_SIZE (5*100000) // Amount of memory for DAQ tables, each ODT entry needs 5 bytes

#if 1  // Settings for 64 bit ns since 1.1.1970 TAI clock (CLOCK_USE_UTC_TIME_NS)

    // Slave clock (mandatory)
    // Specify ApplXcpGetClock and ApplXcpGetClock64 resolution for DAQ time stamps 
    #define XCP_DAQ_CLOCK_64BIT  // Use 64 Bit time stamps in GET_DAQ_CLOCK
    #define XCP_DAQ_CLOCK_EPOCH XCP_EPOCH_TAI
    #define XCP_TIMESTAMP_SIZE 4 // Use 32 Bit time stamps in DAQ DTO
    #define XCP_TIMESTAMP_UNIT DAQ_TIMESTAMP_UNIT_1NS // unit DAQ_TIMESTAMP_UNIT_xxx
    #define XCP_TIMESTAMP_TICKS 1  // ticks per unit

    // Grandmaster clock (optional, needs to use XcpSetGrandmasterClockInfo, ApplXcpGetClockInfo) 
    #define XCP_ENABLE_GRANDMASTER_CLOCK_INFO

#else // Setting for 32 bit us clock (CLOCK_USE_APP_TIME_US)

    #define XCP_DAQ_CLOCK_EPOCH XCP_EPOCH_ARB
    #define XCP_TIMESTAMP_SIZE 4 // Use 32 Bit time stamps in DAQ DTO
    #define XCP_TIMESTAMP_UNIT DAQ_TIMESTAMP_UNIT_1US // unit DAQ_TIMESTAMP_UNIT_xxx
    #define XCP_TIMESTAMP_TICKS 1  // ticks per unit

#endif

#define XCP_TIMESTAMP_TICKS_S CLOCK_TICKS_PER_S // ticks per s (for debug output)


#endif

