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

// Maximum length of EPK string (excluding null terminator), must be odd
#define XCP_EPK_MAX_LENGTH 31

// Maximum length of the project name (excluding null terminator), must be odd
#define XCP_PROJECT_NAME_MAX_LENGTH 31

/*----------------------------------------------------------------------------*/
// Enable calibration segment list management
#ifdef OPTION_CAL_SEGMENTS
#define XCP_ENABLE_CALSEG_LIST
#if OPTION_CAL_SEGMENT_COUNT > 0
#define XCP_MAX_CALSEG_COUNT OPTION_CAL_SEGMENT_COUNT
#endif
#endif // OPTION_CAL_SEGMENTS

/*----------------------------------------------------------------------------*/
/* Address, address extension coding */

/*
Address extensions and addressing modes:

XCPlite absolute addressing: XCP_LITE_ACSDD (default)
0x00        - Calibration segment relative addressing mode (XCP_ADDR_EXT_SEG with u16 offset)
0x01        - Absolute addressing mode (XCP_ADDR_EXT_ABS)
0x02        - Stackframe relative (Event based relative addressing mode with asynchronous access and i16 offset)
0x03-0x04   - Pointer relative (Event based relative addressing mode with asynchronous access and i16 offset)
0x05-0xFC   - Reserved
0xFD        - A2L upload memory space (XCP_ADDR_EXT_A2L)
0xFE        - MTA pointer address space (XCP_ADDR_EXT_PTR)
0xFF        - Undefined address extension (XCP_UNDEFINED_ADDR_EXT)

XCPlite relative addressing: XCP_LITE_CASDD:
0x00        - Absolute addressing mode (XCP_ADDR_EXT_ABS)
0x01        - Calibration segment relative addressing mode (XCP_ADDR_EXT_SEG)

xcp-lite for Rust XCP_LITE_CADR:
0x00        - Absolute addressing mode (XCP_ADDR_EXT_ABS)
0x01        - Calibration segment relative addressing mode (XCP_ADDR_EXT_SEG with u16 offset)
0x02        - Pointer relative (Event based relative addressing mode with asynchronous access and i16 offset)
0x03        - Stackframe relative (Not event based, with i32 offset)

...
*/

// --- Event based addressing mode without asynchronous access
#ifdef XCPLIB_FOR_RUST // XCPLIB_FOR_RUST is set by the Rust build script

// Rust xcp-lite uses only relative addressing (0x03) and dynamic addressing (0x02) and application specific addressing (0x00) for calibration via callbacks
// Calibration segments are currently handled by the application
#define XCP_ADDRESS_MODE_XCP_LITE_CADR
#define XCP_ADDRESS_MODE "XCP_LITE_P_DR"
#define XCP_ENABLE_APP_ADDRESSING
#define XCP_ADDR_EXT_APP 0x00
#define XCP_ENABLE_REL_ADDRESSING
#define XCP_ADDR_EXT_REL 0x03
#define XCP_ENABLE_DYN_ADDRESSING
#define XCP_ADDR_EXT_DYN 0x02
#define XCP_ADDR_EXT_DYN_MAX 0x02

#else

// C/C++ XCPlite uses absolute, dynamic and calibration segment relative addressing
#define XCP_ENABLE_ABS_ADDRESSING
#define XCP_ENABLE_SEG_ADDRESSING
#if !defined(XCP_ENABLE_CALSEG_LIST) || defined(OPTION_CAL_SEGMENTS_ABS)
#define XCP_ADDRESS_MODE_XCP_LITE_ACSDD
#define XCP_ADDRESS_MODE "XCP_LITE_ACSDD"
#define XCP_ADDR_EXT_ABS 0x00
#define XCP_ADDR_EXT_SEG 0x01
#else
#define XCP_ADDRESS_MODE_XCP_LITE_CASDD
#define XCP_ADDRESS_MODE "XCP_LITE_CASDD"
#define XCP_ADDR_EXT_ABS 0x01
#define XCP_ADDR_EXT_SEG 0x00
#endif
#define XCP_ENABLE_DYN_ADDRESSING
#define XCP_ADDR_EXT_DYN 0x02
#define XCP_ADDR_EXT_DYN_MAX 0x04

#endif

#ifdef XCP_ENABLE_REL_ADDRESSING

// Use addr_ext XCP_ADDR_EXT_REL to indicate relative addr format (rel_base + (offset as int32_t))
// Used for stack frame relative addressing
#define XcpAddrIsRel(addr_ext) ((addr_ext) == XCP_ADDR_EXT_REL)
#define XcpAddrEncodeRel(signed_int32_offset) ((uint32_t)(signed_int32_offset & 0xFFFFFFFF))
#define XcpAddrDecodeRelOffset(addr) (int32_t)(addr) // signed address offset

#endif // XCP_ENABLE_REL_ADDRESSING

// --- Event based addressing modes with asynchronous access
#ifdef XCP_ENABLE_DYN_ADDRESSING

// Relative addr format (dyn_base + (((event as uint16_t) <<16) | offset as int16_t))
#define XcpAddrIsDyn(addr_ext) (((addr_ext) >= XCP_ADDR_EXT_DYN && (addr_ext) <= XCP_ADDR_EXT_DYN_MAX))
#define XcpAddrEncodeDyn(signed_int16_offset, event) (((uint32_t)(event) << 16) | ((signed_int16_offset) & 0xFFFF))
#define XcpAddrDecodeDynEvent(addr) (uint16_t)((addr) >> 16)    // event
#define XcpAddrDecodeDynOffset(addr) (int16_t)((addr) & 0xFFFF) // signed address offset

#endif // XCP_ENABLE_DYN_ADDRESSING

// --- Asynchronous absolute addressing mode (not thread safe)
#ifdef XCP_ENABLE_ABS_ADDRESSING

// Absolute addr format (xcp_get_base_addr() + (addr as uint32_t))
// Used for global data
#define XcpAddrIsAbs(addr_ext) ((addr_ext) == XCP_ADDR_EXT_ABS)
#define XcpAddrEncodeAbs(p) ApplXcpGetAddr(p) // Calculate absolute address encoding from a pointer, application specific function
#define XcpAddrDecodeAbsOffset(addr) (uint32_t)(addr)

#endif // XCP_ENABLE_ABS_ADDRESSING

// --- Calibration segment relative addressing mode
#ifdef XCP_ENABLE_SEG_ADDRESSING

#ifndef XCP_ENABLE_CALSEG_LIST
#error "XCP_ENABLE_SEG_ADDRESSING requires XCP_ENABLE_CALSEG_LIST"
#endif

#define XcpAddrIsSeg(addr_ext) ((addr_ext) == XCP_ADDR_EXT_SEG)

// Enable the EPK calibration segment to detect HEX file incompatibility
#ifdef OPTION_CAL_SEGMENT_EPK
#define XCP_ENABLE_EPK_CALSEG
#endif

#if defined(XCP_ENABLE_EPK_CALSEG) && XCP_ADDR_EXT_SEG == 0

#define XCP_ADDR_EPK 0x80000000 // Segment relative EPK address
#define XcpAddrEncodeSegIndex(seg_index, offset) (0x80000000 + ((uint32_t)((seg_index)) << 16) + (offset))
// Assuming the EPK calibration segment has the lowest segment index (0)

#else

#define XCP_ADDR_EPK 0xFFFFFF00 // Absolute EPK address
#define XcpAddrEncodeSegIndex(seg_index, offset) (0x80000000 + (((uint32_t)(seg_index)) << 16) + (offset))

#endif

#define XcpAddrEncodeSegNumber(seg_number, offset) (0x80000000 + (((uint32_t)((seg_number))) << 16) + (offset))
#define XcpAddrDecodeSegNumber(addr) (uint16_t)(((addr) >> 16) & 0x7FFF)
#define XcpAddrDecodeSegOffset(addr) (uint16_t)((addr) & 0xFFFF)

#else

#define XCP_ADDR_EPK 0xFFFFFF00

#endif // XCP_ENABLE_SEG_ADDRESSING

// --- Application specific addressing mode for external calibration segment management and memory access
// If built-in calibration segment management is disabled
#ifdef XCP_ENABLE_APP_ADDRESSING

// Use addr_ext XCP_ADDR_EXT_APP/SEG to indicate application specific addr format or segment relative address format
// Application specific address format
// Memory access and calibration segments are handled by the application, calls ApplXcpReadMemory and ApplXcpWriteMemory
#define XCP_ADDR_EXT_APP 0x00
#define XcpAddrIsApp(addr_ext) ((addr_ext) == XCP_ADDR_EXT_APP)

#endif // XCP_ENABLE_APP_ADDRESSING

// --- Internally used address extensions
// Use addr_ext XCP_ADDR_EXT_EPK to indicate EPK upload memory space
// A2L specification does not allow to specify the address extension for the EPK address, we use a virtual calibration segment (number 0, address ext 0)
#define XCP_ADDR_EXT_EPK 0x00 // must be 0
// Use addr_ext XCP_ADDR_EXT_A2L to indicate A2L upload memory space
#define XCP_ADDR_EXT_A2L 0xFD
#define XCP_ADDR_A2l 0x00000000
// Use addr_ext XCP_ADDR_EXT_PTR to indicate gXcp.MtaPtr is valid
#define XCP_ADDR_EXT_PTR 0xFE

// Undefined address extension
#define XCP_UNDEFINED_ADDR_EXT 0xFF // Undefined address extension

/*----------------------------------------------------------------------------*/
/* Protocol features and commands */

#define XCP_ENABLE_CAL_PAGE // Enable calibration page commands (GET/SET_CAL_PAGE)
#ifdef XCP_ENABLE_CAL_PAGE

// Enable calibration page initialization (COPY_CAL_PAGE, FLASH->RAM copy only)
#define XCP_ENABLE_COPY_CAL_PAGE
// Activate workaround for CANape issue with COPY_CAL_PAGE command
// COPY_CAL_PAGE always copies all segments from default to working, this is not compliant to the XCP specification
#define XCP_ENABLE_COPY_CAL_PAGE_WORKAROUND

// Enable working page freeze request
// There are 2 modes:
//   1. A persisted working page will become the new reference page (this requires segment relative addressing mode)
//   2. Just persist the working page
#ifdef OPTION_CAL_PERSISTENCE

// Enable the FREEZE_CAL_PAGE command
// Required for calibration segment persistency
#define XCP_ENABLE_FREEZE_CAL_PAGE

// Enable persistency of reference (default) page, instead of working page
// This requires segment relative addressing mode !
#ifdef OPTION_CAL_REFERENCE_PAGE_PERSISTENCY
#define XCP_ENABLE_REFERENCE_PAGE_PERSISTENCY
#endif

#endif

#endif // XCP_ENABLE_CAL_PAGE

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

// Enable individual address extensions for each ODT entry, otherwise address extension must be unique for each DAQ list
#define XCP_ENABLE_DAQ_ADDREXT

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
#define XCP_DAQ_MEM_SIZE (1024 * 6) // Amount of memory for DAQ tables, each ODT entry (e.g. measurement variable or memory block) needs 5 bytes
#endif

// Enable DAQ resume mode, requires XCP_ENABLE_DAQ_EVENT_LIST
// #define XCP_ENABLE_DAQ_RESUME

// Enable prescaler for DAQ events, requires XCP_ENABLE_DAQ_EVENT_LIST
#define XCP_ENABLE_DAQ_PRESCALER

// Overrun indication via PID
// Not needed for Ethernet, client detects data loss via transport layer counter gaps
// #define XCP_ENABLE_OVERRUN_INDICATION_PID

/*----------------------------------------------------------------------------*/
/* DAQ event management */

// Enable event list
#ifndef XCPLIB_FOR_RUST // // Set by the Rust build script, not needed for Rust xcp-lite, currently has its own event management
#define XCP_ENABLE_DAQ_EVENT_LIST
#endif

#ifdef XCP_ENABLE_DAQ_EVENT_LIST

#if defined(OPTION_DAQ_EVENT_COUNT) && (OPTION_DAQ_EVENT_COUNT > 0)
#define XCP_MAX_EVENT_COUNT OPTION_DAQ_EVENT_COUNT
#else
#error "Please define OPTION_DAQ_EVENT_COUNT"
#endif

// Enable XCP_GET_EVENT_INFO, if this is enabled, event information can be queried by the XCP client tool
#define XCP_ENABLE_DAQ_EVENT_INFO

// Maximum length of event name without the trailing 0
#define XCP_MAX_EVENT_NAME 15

#else                           // XCP_ENABLE_DAQ_EVENT_LIST

// If XCP_MAX_EVENT_COUNT is defined and DAQ event management is not used, DAQ list to event association lookup will be optimized
// Set the maximum number of DAQ events (the highest DAQ event number used), XCP_MAX_EVENT_COUNT must be even
// Requires XCP_MAX_EVENT_COUNT * 2 bytes of memory
#define XCP_MAX_EVENT_COUNT 256 // For available event numbers from 0 to 255

#endif // !XCP_ENABLE_DAQ_EVENT_LIST

/*----------------------------------------------------------------------------*/
/* Calibration segment management */

#ifdef XCP_ENABLE_CALSEG_LIST

#define XCP_MAX_CALSEG_NAME 15 // Maximum length of calibration segment name

// Enable lazy write mode for calibration segments
// RCU updates of calibration segments are done in a cyclic manner in the background
// Calibration write speed is then independent from the lock rate, but single calibration latencies are higher
// Without this, calibration updates are always delayed by one lock cycle and only one single direct or one atomic calibration change is possible per lock
// If the latency of a single, sporadic calibration change is extremely important, this can be disabled
#define XCP_ENABLE_CALSEG_LAZY_WRITE

// Timeout for acquiring a free calibration segment page
#define XCP_CALSEG_AQUIRE_FREE_PAGE_TIMEOUT 500 // 500 ms timeout

#endif // XCP_ENABLE_CALSEG_LIST

//-------------------------------------------------------------------------------
/* Clock */

// Clock resolution
#define XCP_DAQ_CLOCK_64BIT                       // Use 64 Bit time stamps for XCP V1.3
#if CLOCK_TICKS_PER_S == 1000000                  // us
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
