#pragma once
#define __XCP_CFG_H__

/*----------------------------------------------------------------------------
| File:
|   xcp_cfg.h
|
| Description:
|   User configuration file for XCP protocol layer parameters
|
| Code released into public domain, no attribution required
|
 ----------------------------------------------------------------------------*/



/*----------------------------------------------------------------------------*/
/* Version */

// Driver version (GET_COMM_MODE_INFO)
#define XCP_DRIVER_VERSION 0x01

// Protocol layer version
// #define XCP_PROTOCOL_LAYER_VERSION 0x0101
#define XCP_PROTOCOL_LAYER_VERSION 0x0103  // GET_DAQ_CLOCK_MULTICAST, GET_TIME_CORRELATION_PROPERTIES
// #define XCP_PROTOCOL_LAYER_VERSION 0x0104  // PACKED_MODE, CC_START_STOP_SYNCH prepare


/*----------------------------------------------------------------------------*/
/* Adress, address extension coding */

// Use addr_ext XCP_ADDR_EXT_ABS to indicate absulute addr format (ApplXcpGetBaseAddr()+(uint32_t)addr) 
#define XCP_ENABLE_ABS_ADDRESSING
#define XCP_ADDR_EXT_ABS 0x01 // Absolute address format 

// Use addr_ext XCP_ADDR_EXT_DYN to indicate relative addr format (event<<16)|offset 
#if OPTION_ENABLE_XCP_CLASS
  #define XCP_ENABLE_DYN_ADDRESSING
  #define XCP_ADDR_EXT_DYN 0x02 // Relative address format
#endif

// Use addr_ext XCP_ADDR_EXT_APP to indicate application specific addr format and use ApplXcpReadMemory and ApplXcpWriteMemory
// #define XCP_ENABLE_APP_ADDRESSING
// #define XCP_ADDR_EXT_APP 0x00 // Address format handled by application

// Internally used address extensions
// Use addr_ext XCP_ADDR_EXT_A2L to indicate A2L upload memory space
#define XCP_ADDR_EXT_A2L 0xFD
// Use addr_ext XCP_ADDR_EXT_PTR to indicate gXcp.MtaPtr is valid 
#define XCP_ADDR_EXT_PTR 0xFE

// Undefined address extension
#define XCP_ADDR_EXT_UNDEFINED 0xFF // Undefined address extension

/*----------------------------------------------------------------------------*/
/* Protocol features */

//#define XCP_ENABLE_INTERLEAVED
//#define XCP_INTERLEAVED_QUEUE_SIZE 16

#define XCP_ENABLE_CHECKSUM // Enable checksum calculation command

#define XCP_ENABLE_CAL_PAGE // Enable cal page switch


/*----------------------------------------------------------------------------*/
/* GET_ID command */

#if OPTION_ENABLE_A2L_GEN
#define XCP_ENABLE_IDT_A2L_UPLOAD // Upload A2L via XCP UPLOAD
#endif

/*----------------------------------------------------------------------------*/
/* DAQ features and parameters */

// #define XCP_ENABLE_DAQ_EVENT_INFO // Enable XCP_GET_EVENT_INFO, if this is enabled, A2L file event information will be ignored
#define XCP_ENABLE_DAQ_EVENT_LIST // Enable event list
#define XCP_MAX_EVENT 256 // Maximum number of events, size of event table
#define XCP_ENABLE_MULTITHREAD_EVENTS // Make XcpEvent thread safe also for same event from different thread
// #define XCP_ENABLE_PACKED_MODE // Enable packed mode emulation

#define XCP_DAQ_MEM_SIZE (5*100) // Amount of memory for DAQ tables, each ODT entry (e.g. measurement variable) needs 5 bytes

// CLOCK_USE_UTC_TIME_NS
// Settings for 64 bit ns since 1.1.1970 TAI clock (CLOCK_USE_UTC_TIME_NS)

#define XCP_DAQ_CLOCK_64BIT  // Use 64 Bit time stamps in GET_DAQ_CLOCK
#define XCP_DAQ_CLOCK_UIID { 0xdc,0xa6,0x32,0xFF,0xFE,0x7e,0x66,0xdc }

// Server DAQ clock info (mandatory)
#define XCP_TIMESTAMP_UNIT DAQ_TIMESTAMP_UNIT_1NS // unit DAQ_TIMESTAMP_UNIT_xxx
#define XCP_TIMESTAMP_TICKS 1  // ticks per unit
#define XCP_TIMESTAMP_EPOCH XCP_EPOCH_TAI

// Grandmaster clock (optional, use XcpSetGrandmasterClockInfo, implement ApplXcpGetClockInfoGrandmaster)
// #define XCP_ENABLE_PTP


//#define XCP_ENABLE_DAQ_CLOCK_MULTICAST // Enable GET_DAQ_CLOCK_MULTICAST
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
    // XCP default cluster id (multicast addr 239,255,0,1, group 127,0,1 (mac 01-00-5E-7F-00-01)
#define XCP_MULTICAST_CLUSTER_ID 1
#endif

#define XCP_TIMESTAMP_TICKS_S CLOCK_TICKS_PER_S // ticks per s (for debug output)


//-------------------------------------------------------------------------------
// Debug 

// Debug console prints
#if !defined(OPTION_ENABLE_DBG_PRINTS) || !defined(OPTION_DEBUG_LEVEL)
  #warning "Please define OPTION_ENABLE_DBG_PRINTS and OPTION_DEBUG_LEVEL in main_cfg.h to ON or OFF"
#else
#if OPTION_ENABLE_DBG_PRINTS
  #define XCP_ENABLE_DEBUG_PRINTS
  #define XCP_DBG_LEVEL OPTION_DEBUG_LEVEL
#else
  #undef XCP_ENABLE_DEBUG_PRINTS
  #undef XCP_DBG_LEVEL OPTION_DEBUG_LEVEL
#endif
#endif

// Enable extended error checks, performance penalty !!!
#define XCP_ENABLE_TEST_CHECKS

