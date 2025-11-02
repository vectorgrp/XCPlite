#pragma once
#define __MAIN_CFG_H__

/*----------------------------------------------------------------------------
| File:
|   main_cfg.h
|
| Description:
|   General configuration and build options for XCPlite / xcplib
|
 ----------------------------------------------------------------------------*/

/*
  More specific XCP configuration is in xcp_cfg.h (Protocol Layer) and xcptl_cfg.h (Transport Layer))
  The values for XCP_xxx and XCPTL_xxx define constants (in xcp_cfg.h and xcptl_cfg.h) may depend on options
*/

// Version
#define OPTION_VERSION_MAJOR 0
#define OPTION_VERSION_MINOR 9
#define OPTION_VERSION_PATCH 3

//-------------------------------------------------------------------------------
// Logging

// Enable debug prints
#define OPTION_ENABLE_DBG_PRINTS
// Enable debug print errors and warnings go to stderr
#define OPTION_ENABLE_DBG_STDERR
// Default log level: 1 - Error, 2 - Warn, 3 - Info, 4 - Trace, 5 - Debug
// Use level 4 print all XCP commands
#define OPTION_DEFAULT_DBG_LEVEL 3
// Optimize code size, only errors and warnings enabled, other levels optimized out
// #define OPTION_FIXED_DBG_LEVEL 2
// Enable debug metrics
#define OPTION_ENABLE_DBG_METRICS

//-------------------------------------------------------------------------------
// Clock

// Epoch options (only one must be defined)
#define OPTION_CLOCK_EPOCH_ARB // Arbitrary epoch -> uses CLOCK_MONOTONIC_RAW
// #define OPTION_CLOCK_EPOCH_PTP // Precision Time Protocol epoch -> uses CLOCK_REALTIME

// Resolution 1ns or 1us, granularity depends on platform (only one must be defined)
#define OPTION_CLOCK_TICKS_1NS
// #define OPTION_CLOCK_TICKS_1US

//-------------------------------------------------------------------------------
// XCP server options

#define OPTION_ENABLE_TCP
#define OPTION_ENABLE_UDP
#define OPTION_MTU 8000                     // Ethernet packet size (MTU), must be %8 - Jumbo frames supported
#define OPTION_SERVER_FORCEFULL_TERMINATION // Don't wait for the rx and tx thread to finish, just terminate them

//-------------------------------------------------------------------------------
// CAL setting

#ifndef XCPLIB_FOR_RUST // Disable for Rust, the Rust wrapper for XCPlite currently has its own calibration segment management and uses the callbacks in xcpAppl.c

// Enable calibration segment management
// (otherwise the callbacks in xcpAppl.c are used for calibration segment commands and memory read/write)
#define OPTION_CAL_SEGMENTS

// Maximum number of calibration segments
#define OPTION_CAL_SEGMENT_COUNT 8

// Enable calibration segment persistence, a binary (.BIN) file is used to store calibration segments
// This allows to safely build the A2L file only once per build, even if the creation order of events and segments changes
#define OPTION_CAL_PERSISTENCE

// Enable EPK calibration segment to check HEX/BIN file compatibility
// If the EPK is included in the HEX/BIN file, the version of the data structure can be checked using the EPK address specified in the A2L file
#define OPTION_CAL_SEGMENT_EPK

// Enable absolute addressing for calibration segments
// Default is segment relative addressing, uses address extension 0 for segment relative and 1 for absolute and encodes the segment number in the address high word
// As this is not compatible to most well known tools to update, modify and create A2L files, this option switches to absolute addressing on address extension 0
// Requirement is, that the address of all reference pages must be stable and in address range of 0x0000_0000 to 0xFFFF_FFFF
// #define OPTION_CAL_SEGMENTS_ABS

// Enable persistency (freeze) to reference page or to working page on next application restart
// This decides which pages (reference/FLASH or working/RAM) are stored to the calibration segment binary file (.BIN) on XCP freeze request
// #define OPTION_CAL_REFERENCE_PAGE_PERSISTENCY

// Start on reference/default page or on working page
#define OPTION_CAL_SEGMENT_START_ON_REFERENCE_PAGE

// Automatically persist the working page on XCP disconnect
// #define OPTION_CAL_PERSIST_ON_DISCONNECT

#endif

//-------------------------------------------------------------------------------
// DAQ settings

#define OPTION_DAQ_MEM_SIZE (1024 * 8) // Memory bytes used for XCP DAQ tables - 6 bytes per measurement signal/block needed
#define OPTION_DAQ_EVENT_COUNT 256     // Maximum number of DAQ events (integer value, must be even)
#define OPTION_DAQ_ASYNC_EVENT         // Create an asynchronous, cyclic DAQ event for asynchronous data acquisition

//-------------------------------------------------------------------------------
// A2L generation settings

#define OPTION_ENABLE_A2L_GENERATOR // Enable A2L generator
#define OPTION_ENABLE_A2L_UPLOAD    // Enable A2L upload via XCP

// Enable socketGetLocalAddr and XcpEthTlGetInfo
// Used for convenience to get an existing ip address in A2L, when bound to ANY 0.0.0.0
#define OPTION_ENABLE_GET_LOCAL_ADDR

//-------------------------------------------------------------------------------
// Miscellaneous options

// Enable atomic emulation for platforms without stdatomic.h
// This is used on Windows and automatically set in platform.h in this case
// Switches to 32 bit transmit queue implementation
// Not designed for non x86 platforms, needs strong memory ordering
// Used for testing on Windows
// #define OPTION_ATOMIC_EMULATION
