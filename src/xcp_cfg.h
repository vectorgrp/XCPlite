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
#include "platform.h" // for CLOCK_TICKS_PER_xx

/*----------------------------------------------------------------------------*/
/* Version */

// Driver version (GET_COMM_MODE_INFO, BYTE)
#define XCP_DRIVER_VERSION (((OPTION_VERSION_MAJOR << 4) & 0xF0) | (OPTION_VERSION_MINOR & 0x0F))

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

// Event based addressing modes without asynchronous access
#define XCP_ENABLE_REL_ADDRESSING
#ifdef XCP_ENABLE_REL_ADDRESSING

// Use addr_ext XCP_ADDR_EXT_REL to indicate relative addr format (rel_base + (offset as int32_t))
// Used for stack frame relative addressing
#define XCP_ADDR_EXT_REL 0x03 // Event relative address format

#endif // XCP_ENABLE_REL_ADDRESSING

// Event based addressing modes with asynchronous access
#define XCP_ENABLE_DYN_ADDRESSING
#ifdef XCP_ENABLE_DYN_ADDRESSING

// Use addr_ext DYN to indicate relative addr format (dyn_base + (((event as uint16_t) <<16) | offset as int16_t))
#define XCP_ADDR_EXT_DYN 0x02 // Relative address format

#endif // XCP_ENABLE_DYN_ADDRESSING

// Asynchronous absolute addressing mode (not thread safe)
#define XCP_ENABLE_ABS_ADDRESSING
#ifdef XCP_ENABLE_ABS_ADDRESSING

// Use addr_ext XCP_ADDR_EXT_ABS to indicate absolute addr format (ApplXcpGetBaseAddr() + (addr as uint32_t))
// Used for global data
#define XCP_ADDR_EXT_ABS 0x01 // Absolute address format

#endif // XCP_ENABLE_ABS_ADDRESSING

// Segment or application specific addressing mode
#define XCP_ENABLE_APP_ADDRESSING
#ifdef XCP_ENABLE_APP_ADDRESSING

// Use addr_ext XCP_ADDR_EXT_APP/SEG to indicate application specific addr format or segment relative address format
// Application specific address format
#define XCP_ADDR_EXT_APP 0x00 // Memory access handled by application, calls ApplXcpReadMemory and ApplXcpWriteMemory
// If calibration segments are enabled (#ifdef XCP_ENABLE_CALSEG_LIST)
#define XCP_ADDR_EXT_SEG 0x00 // Segment relative address format, must be 0, CANape does not support memory segment address extensions

#endif // XCP_ENABLE_APP_ADDRESSING

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
/* Protocol features and commands */

#define XCP_ENABLE_CAL_PAGE // Enable calibration page commands
#ifdef XCP_ENABLE_CAL_PAGE

// Enable calibration page initialization (FLASH->RAM copy)
#define XCP_ENABLE_COPY_CAL_PAGE

// Enable calibration page freeze request
#ifdef OPTION_CAL_PERSISTENCE
#define XCP_ENABLE_FREEZE_CAL_PAGE
#endif

#endif

// Enable checksum calculation command
#define XCP_ENABLE_CHECKSUM
#define XCP_CHECKSUM_TYPE XCP_CHECKSUM_TYPE_CRC16CCITT
// #define XCP_CHECKSUM_TYPE XCP_CHECKSUM_TYPE_ADD44

// Enable seed/key command
// #define XCP_ENABLE_SEED_KEY

#define XCP_ENABLE_SERV_TEXT // Enable SERV_TEXT events

// Enable GET_ID command support for A2L upload
#ifdef OPTION_ENABLE_A2L_UPLOAD
#define XCP_ENABLE_IDT_A2L_UPLOAD
#endif

// Enable user defined command
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

/*----------------------------------------------------------------------------*/
/* DAQ event management */

// Enable event list
#ifndef XCPLIB_FOR_RUST // Not needed for Rust xcp-lite, has its own event management

#define XCP_ENABLE_DAQ_EVENT_LIST

#if defined(OPTION_DAQ_EVENT_COUNT) && (OPTION_DAQ_EVENT_COUNT > 0)
#undef XCP_MAX_EVENT_COUNT
#define XCP_MAX_EVENT_COUNT OPTION_DAQ_EVENT_COUNT
#endif

#endif

#ifdef XCP_ENABLE_DAQ_EVENT_LIST

// Enable XCP_GET_EVENT_INFO, if this is enabled, event information can be queried by the XCP client tool
// #define XCP_ENABLE_DAQ_EVENT_INFO

#define XCP_MAX_EVENT_NAME 15

#endif

/*----------------------------------------------------------------------------*/
/* Calibration segment management */

// Enable calibration segment list management
#ifndef XCPLIB_FOR_RUST // Not needed for Rust xcp-lite, has its own calibration segment management and uses the callbacks
#ifdef OPTION_CAL_SEGMENTS
#define XCP_ENABLE_CALSEG_LIST
#if OPTION_CAL_SEGMENT_COUNT > 0
#define XCP_MAX_CALSEG_COUNT OPTION_CAL_SEGMENT_COUNT
#endif
#endif // OPTION_CAL_SEGMENTS
#endif

#ifdef XCP_ENABLE_CALSEG_LIST

#define XCP_MAX_CALSEG_NAME 15 // Maximum length of calibration segment name

#define XCP_ADDR_EXT_SEG 0x00 // Segment relative address format, must be 0x00, CANape does not support memory segment address extensions

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
