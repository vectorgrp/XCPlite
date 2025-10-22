#pragma once
#define __MAIN_CFG_H__

/*----------------------------------------------------------------------------
| File:
|   main_cfg.h
|
| Description:
|   General configuration and build options for XCPlite / xcplib
||
 ----------------------------------------------------------------------------*/

// Version
#define OPTION_VERSION_MAJOR 0
#define OPTION_VERSION_MINOR 9
#define OPTION_VERSION_PATCH 3

/*

  A #define OPTION_xxx may be a higher abstraction level configuration set
  More specific XCP configuration is in xcp_cfg.h (Protocol Layer) and xcptl_cfg.h (Transport Layer))
  The default values for XCP_xxx and XCPTL_xxx defines (in xcp_cfg.h and xcptl_cfg.h) may depend on options


  XCP library build options:

  // Logging
  #define OPTION_ENABLE_DBG_PRINTS            Enable debug prints
  #define OPTION_ENABLE_DBG_STDERR            Enable debug print errors and warnings go to stderr
  #define OPTION_DEFAULT_DBG_LEVEL x          Default log level: 1 - Error, 2 - Warn, 3 - Info, 4 - Trace, 5 - Debug
  #define OPTION_FIXED_DBG_LEVEL x            Fixed log level to optimize code size

  // Clock
  #define OPTION_CLOCK_EPOCH_ARB              Arbitrary epoch or since 1.1.1970
  #define OPTION_CLOCK_EPOCH_PTP
  #define OPTION_CLOCK_TICKS_1NS              Resolution 1ns or 1us, granularity depends on platform
  #define OPTION_CLOCK_TICKS_1US

  // XCP server settings
  #define OPTION_ENABLE_TCP
  #define OPTION_ENABLE_UDP
  #define OPTION_MTU x                        Ethernet MTU, must be %8
  #define OPTION_SERVER_FORCEFULL_TERMINATION Terminate the server threads instead of waiting for the tasks to finish

  // DAQ settings
  #define OPTION_DAQ_MEM_SIZE x               Size of memory for DAQ setup in bytes (integer value, 6 bytes per signal needed)
  #define OPTION_DAQ_EVENT_COUNT x            Maximum number of DAQ events (integer value, must be even)
  #define OPTION_DAQ_ASYNC_EVENT              Create a global, cyclic DAQ event for asynchronous data acquisition

  // CAL settings
  #define OPTION_CAL_SEGMENTS                 Enable calibration segment management (otherwise callbacks are used for calibration segment commands)
  #define OPTION_CAL_SEGMENT_COUNT x          Maximum number of calibration segments
  #define OPTION_CAL_PERSISTENCE              Enable calibration segment persistence, BIN file is used to store calibration segments, A2L maybe generated only once per build
  #define OPTION_CAL_SEGMENT_EPK              Enable EPK calibration segment to check HEX file compatibility
  #define OPTION_CAL_SEGMENTS_ABS             Enable absolute addressing for calibration segments, calibration parameters and calibration segments may be updated by linker mapfile,

  // A2L generation settings
  #define OPTION_ENABLE_A2L_GENERATOR         Enable A2L generator
  #define OPTION_ENABLE_A2L_UPLOAD            Enable A2L upload through XCP
  #define OPTION_ENABLE_GET_LOCAL_ADDR        Determine an existing IP address for A2L file, if bound to ANY

*/

// Logging
#define OPTION_ENABLE_DBG_PRINTS
#define OPTION_ENABLE_DBG_STDERR
#define OPTION_DEFAULT_DBG_LEVEL 3 // User adjustable log level, default 3
// #define OPTION_FIXED_DBG_LEVEL 2 // Optimize code size, only errors and warnings enabled, other levels optimized out

// Clock
#define OPTION_CLOCK_EPOCH_ARB // -> use CLOCK_MONOTONIC_RAW
// #define OPTION_CLOCK_EPOCH_PTP // -> use CLOCK_REALTIME
// #define OPTION_CLOCK_TICKS_1US
#define OPTION_CLOCK_TICKS_1NS

// XCP server options
#define OPTION_ENABLE_TCP
#define OPTION_ENABLE_UDP
#define OPTION_MTU 8000                     // Ethernet packet size (MTU) - Jumbo frames supported
#define OPTION_SERVER_FORCEFULL_TERMINATION // Don't wait for the rx and tx thread to finish, just terminate them

// CAL
#ifndef XCPLIB_FOR_RUST // Set by the rust build script, Rust xcp-lite currently has its own calibration segment management

#define OPTION_CAL_SEGMENTS        // Enable calibration segment management
#define OPTION_CAL_SEGMENT_COUNT 4 // Maximum number of calibration segments
#define OPTION_CAL_PERSISTENCE     // Enable calibration segment persistence, BIN file is used to store calibration segments, A2L maybe generated only once per build
#define OPTION_CAL_SEGMENT_EPK     // Enable EPK calibration segment to check HEX file compatibility
// #define OPTION_CAL_SEGMENTS_ABS    // Enable absolute addressing for calibration segments

#endif

// DAQ
#ifndef XCPLIB_FOR_RUST // Set by the rust build script, Rust xcp-lite needs larger DAQ lists for testing

#define OPTION_DAQ_MEM_SIZE (1000 * 6) // Memory bytes used for XCP DAQ tables - max 6 bytes per measurement signal needed
#define OPTION_DAQ_EVENT_COUNT 32      // Maximum number of DAQ events (integer value, must be even)
#define OPTION_DAQ_ASYNC_EVENT         // Create an asynchronous, cyclic DAQ event for asynchronous data acquisition

#else

#define OPTION_DAQ_MEM_SIZE (10000 * 6) // Memory bytes used for XCP DAQ tables - max 6 bytes per measurement signal needed
#define OPTION_DAQ_EVENT_COUNT 256      // Maximum number of DAQ events (integer value, must be even)

#endif

// A2L
#define OPTION_ENABLE_A2L_GENERATOR // Enable A2L generator
#define OPTION_ENABLE_A2L_UPLOAD    // Enable A2L upload via XCP

// Enable socketGetLocalAddr and XcpEthTlGetInfo
// Used for convenience to get an existing ip address in A2L, when bound to ANY 0.0.0.0
#define OPTION_ENABLE_GET_LOCAL_ADDR

//-------------------------------------------------------------------------------

// Enable atomic emulation for platforms without stdatomic.h
// This is used on Windows and automatically set in platform.hin this case
// Switches to 32 bit transmit queue implementation
// Not designed for non x86 platforms, needs strong memory ordering
// Explicit define used for testing only
// #define OPTION_ATOMIC_EMULATION
