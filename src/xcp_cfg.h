#pragma once
#define __XCP_CFG_H__

/*----------------------------------------------------------------------------
| File:
|   xcp_cfg.h
|
| Description:
|   Parameter configuration for XCP protocol layer parameters
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "main_cfg.h" // for OPTION_xxx

// Clock resolution defined in platform.h
#include "platform.h" // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex

/*----------------------------------------------------------------------------*/
/* Version */

// Driver version (GET_COMM_MODE_INFO)
#define XCP_DRIVER_VERSION 0x01

// Enable Ethernet specific protocol layer commands
#define XCP_ENABLE_PROTOCOL_LAYER_ETH

// Protocol layer version
// #define XCP_PROTOCOL_LAYER_VERSION 0x0101
// #define XCP_PROTOCOL_LAYER_VERSION 0x0103  // GET_DAQ_CLOCK_MULTICAST, GET_TIME_CORRELATION_PROPERTIES
#define XCP_PROTOCOL_LAYER_VERSION 0x0104 // PACKED_MODE, CC_START_STOP_SYNCH prepare

/*----------------------------------------------------------------------------*/
/* Address, address extension coding */

// Enable individual address extensions for each ODT entry, otherwise address extension must be unique for each DAQ list
#define XCP_ENABLE_DAQ_ADDREXT

// Use addr_ext XCP_ADDR_EXT_REL to indicate relative addr format offset as uint64
#define XCP_ENABLE_REL_ADDRESSING
#define XCP_ADDR_EXT_REL 0x03 // Event relative address format

// Use addr_ext XCP_ADDR_EXT_DYN to indicate relative addr format (event as uint16_t <<16)| offset as int16_t
#define XCP_ENABLE_DYN_ADDRESSING
#define XCP_ADDR_EXT_DYN 0x02 // Relative address format

// Use addr_ext XCP_ADDR_EXT_ABS to indicate absulute addr format (ApplXcpGetBaseAddr()+ addr as uint64)
#define XCP_ENABLE_ABS_ADDRESSING
#define XCP_ADDR_EXT_ABS 0x01 // Absolute address format

// Use addr_ext XCP_ADDR_EXT_SEG to indicate application specific addr format
// Use ApplXcpReadMemory and ApplXcpWriteMemory or the XCP lite calibration segments (#ifdef XCP_ENABLE_CALSEG_LIST) to access memory
#define XCP_ENABLE_APP_ADDRESSING // Segment relative memory access handled by application
#define XCP_ADDR_EXT_APP 0x00     // Segment relative address format, must be 0, CANape does not support memory segment address extensions

// Internally used address extensions
// Use addr_ext XCP_ADDR_EXT_EPK to indicate EPK upload memory space
#define XCP_ADDR_EXT_EPK 0x00
#define XCP_ADDR_EPK 0x80000000
// Use addr_ext XCP_ADDR_EXT_A2L to indicate A2L upload memory space
#define XCP_ADDR_EXT_A2L 0xFD
#define XCP_ADDR_A2l 0x00000000
// Use addr_ext XCP_ADDR_EXT_PTR to indicate gXcp.MtaPtr is valid
#define XCP_ADDR_EXT_PTR 0xFE

// Undefined address extension
#define XCP_UNDEFINED_ADDR_EXT 0xFF // Undefined address extension

/*----------------------------------------------------------------------------*/
/* Protocol features */

#define XCP_ENABLE_CAL_PAGE // Enable calibration page switching commands
#ifdef XCP_ENABLE_CAL_PAGE
#define XCP_ENABLE_COPY_CAL_PAGE   // // Enable calibration page initialization (FLASH->RAM copy)
#define XCP_ENABLE_FREEZE_CAL_PAGE // Enable calibration freeze command
#endif

#define XCP_ENABLE_CHECKSUM // Enable checksum calculation command

// #define XCP_ENABLE_SEED_KEY // Enable seed/key command

#define XCP_ENABLE_SERV_TEXT // Enable SERV_TEXT events

/*----------------------------------------------------------------------------*/
/* GET_ID command */

#ifdef OPTION_ENABLE_A2L_UPLOAD
#define XCP_ENABLE_IDT_A2L_UPLOAD // Enable upload A2L via XCP
#endif

/*----------------------------------------------------------------------------*/
/* User defined command */

// Used for begin and end atomic calibration operation
#define XCP_ENABLE_USER_COMMAND

/*----------------------------------------------------------------------------*/
/* DAQ features and parameters */

// Maximum number of DAQ events
// If XCP_MAX_EVENT_COUNT is defined, DAQ list to event association lookup will be optimized
// Requires XCP_MAX_EVENT_COUNT * 2 bytes of memory
// XCP_MAX_EVENT_COUNT must be even
#define XCP_MAX_EVENT_COUNT 256 // For available event numbers from 0 to 255

// Maximum number of DAQ lists
// Must be <= 0xFFFE
// Numbers smaller than 256 will switch to 2 byte transport layer header DAQ_HDR_ODT_DAQB
#define XCP_MAX_DAQ_COUNT 1024

// Static allocated memory for DAQ tables
// Amount of memory for DAQ tables, each ODT entry (e.g. measurement variable) needs 5 bytes, each DAQ list 12 bytes and
// each ODT 8 bytes
#ifdef OPTION_DAQ_MEM_SIZE
#define XCP_DAQ_MEM_SIZE OPTION_DAQ_MEM_SIZE
#else
#define XCP_DAQ_MEM_SIZE (1024 * 5) // Amount of memory for DAQ tables, each ODT entry (e.g. measurement variable or memory block) needs 5 bytes
#endif

// Enable DAQ resume mode
#define XCP_ENABLE_DAQ_RESUME

// Enable event list
// Not needed for Rust xcp-lite, has its own event management
#define XCP_ENABLE_DAQ_EVENT_LIST
#ifdef XCP_ENABLE_DAQ_EVENT_LIST

// Enable XCP_GET_EVENT_INFO, if this is enabled, A2L file event information will be ignored
// #define XCP_ENABLE_DAQ_EVENT_INFO

#define XCP_MAX_EVENT_NAME 15

#endif

// Enable calibration segment list
// Not needed for Rust xcp-lite, has its own calibration segment management
#define XCP_ENABLE_CALSEG_LIST
#ifdef XCP_ENABLE_CALSEG_LIST

#define XCP_MAX_CALSEG_COUNT 4
#define XCP_MAX_CALSEG_NAME 15

#define XCP_ADDR_EXT_SEG 0x00 // Segment relative address format, must be 0, CANape does not support memory segment address extensions

// Enable lazy write mode for calibration segments
// RCU updates of calibration segments are done in a cyclic manner in the background
// Calibration write speed is then independent from the lock rate, but single calibration latencies are higher
// Without this, calibration updates are always delayed by one lock cycle and only one single direct or one atomic calibration change is possible per lock
// If the latency of a single, sporadic calibration change is extremely important, this can be disabled
#define XCP_ENABLE_CALSEG_LAZY_WRITE

// Timeout for acquiring a free calibration segment page
#define XCP_CALSEG_AQUIRE_FREE_PAGE_TIMEOUT 500 // 500 ms timeout

#endif

// Overrun indication via PID
// Not needed for Ethernet, client detects data loss via transport layer counters
// #define XCP_ENABLE_OVERRUN_INDICATION_PID

// Clock resolution
// #define XCP_DAQ_CLOCK_32BIT  // Use 32 Bit time stamps
#define XCP_DAQ_CLOCK_64BIT                       // Use 64 Bit time stamps
#if CLOCK_TICKS_PER_S == 1000000                  //  us
#define XCP_TIMESTAMP_UNIT DAQ_TIMESTAMP_UNIT_1US // unit
#define XCP_TIMESTAMP_TICKS 1                     // ticks per unit
#elif CLOCK_TICKS_PER_S == 1000000000             // ns
#define XCP_TIMESTAMP_UNIT DAQ_TIMESTAMP_UNIT_1NS // unit
#define XCP_TIMESTAMP_TICKS 1                     // ticks per unit
#else
#error "Please define clock resolution"
#endif

// Grandmaster clock (optional, use XcpSetGrandmasterClockInfo, implement ApplXcpGetClockInfoGrandmaster)
#define XCP_ENABLE_PTP
#define XCP_DAQ_CLOCK_UIID {0xdc, 0xa6, 0x32, 0xFF, 0xFE, 0x7e, 0x66, 0xdc}

// Enable GET_DAQ_CLOCK_MULTICAST
// Not recommended
// #define XCP_ENABLE_DAQ_CLOCK_MULTICAST
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
// XCP default cluster id (multicast addr 239,255,0,1, group 127,0,1 (mac 01-00-5E-7F-00-01)
#define XCP_MULTICAST_CLUSTER_ID 1
#endif

//-------------------------------------------------------------------------------
// Debug

// Enable extended error checks, performance penalty !!!
#define XCP_ENABLE_TEST_CHECKS
