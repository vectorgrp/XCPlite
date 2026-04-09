#pragma once
#define __XCPLIB_CFG_H__

/*----------------------------------------------------------------------------
| File:
|   xcplib_cfg.h
|
| Description:
|   General configuration and build options for XCPlite / libxcplite
|
 ----------------------------------------------------------------------------*/

/*
  More specific XCP configuration is in xcp_cfg.h (Protocol Layer) and xcptl_cfg.h (Transport Layer))
  The values for XCP_xxx and XCPTL_xxx define constants (in xcp_cfg.h and xcptl_cfg.h) may depend on options
*/

// XCPlite version, currently V2.0.0
#define OPTION_VERSION_MAJOR 2
#define OPTION_VERSION_MINOR 0
#define OPTION_VERSION_PATCH 0

#ifdef XCPLIB_FOR_RUST // Set by the Rust build script

#include "xcplib_rust_cfg.h" // for Rust xcp-lite specific configuration

#else

//-------------------------------------------------------------------------------
// Logging

// Enable debug prints
#define OPTION_ENABLE_DBG_PRINTS
// Enable debug print errors and warnings go to stderr
#define OPTION_ENABLE_DBG_STDERR
// Default log level: 1 - Error, 2 - Warn, 3 - Info, 4 - Trace, 5 - Debug
// Use level 4 to print all XCP commands
#define OPTION_DEFAULT_DBG_LEVEL 3
// Optimize code size, higher levels optimized out
#define OPTION_MAX_DBG_LEVEL 5
// Optimize code size, fixed log level, not changeable at runtime
// #define OPTION_FIXED_DBG_LEVEL 3

//-------------------------------------------------------------------------------
// Clock

// Epoch options (only one must be defined)
#define OPTION_CLOCK_EPOCH_ARB // Arbitrary epoch -> uses CLOCK_MONOTONIC_RAW on Linux, CLOCK_MONOTONIC on QNX
// #define OPTION_CLOCK_EPOCH_PTP // Precision Time Protocol epoch (since 1.1.1970) -> uses CLOCK_REALTIME, which may be disciplined by NTP, PTP, ...

// Resolution 1ns or 1us, granularity depends on platform (only one must be defined)
#define OPTION_CLOCK_TICKS_1NS
// #define OPTION_CLOCK_TICKS_1US

//-------------------------------------------------------------------------------
// Enable atomic emulation for Windows
// Not designed for non x86 platforms, needs strong memory ordering
// Used for testing on Windows
#if defined(_WIN32) || defined(_WIN64)
#define OPTION_ATOMIC_EMULATION
#endif

//-------------------------------------------------------------------------------
// Socket options

// #define OPTION_SOCKET_HW_TIMESTAMPS // Enable hardware timestamps on UDP sockets if available (needed only for ptptool on Linux)

//-------------------------------------------------------------------------------
// XCP multi application mode

// Experimental, work in progress, not fully tested yet, may change or be removed without major version change, use with caution

// Enable multi application mode:
// All application processes have shared transmit queue, calibration RCU and XCP state
// One application is the XCP server, could be the first one running (XCP leader) or a dedicated application (XCP daemon)
// Requires a POSIX-compliant platform (Linux / macOS / QNX).  Not supported on Windows.
// #define OPTION_SHM_MODE

//-------------------------------------------------------------------------------
// XCP server options

#define OPTION_ENABLE_TCP
#define OPTION_ENABLE_UDP
#define OPTION_MTU 8000                     // Ethernet packet size (MTU), must be %8 - Jumbo frames supported
#define OPTION_SERVER_FORCEFULL_TERMINATION // Don't wait for the rx and tx thread to finish, just terminate them

//-------------------------------------------------------------------------------
// CAL setting

// Enable calibration segment management
// (otherwise the callbacks in xcpappl.c are used for calibration segment commands and memory read/write)
#define OPTION_CAL_SEGMENTS

// Maximum number of calibration segments
#define OPTION_CAL_SEGMENT_COUNT 32

// Total memory pool size for all calibration segments (header + 4 pages each)
// Must be large enough for all XcpCreateCalSeg() calls combined
#define OPTION_CAL_MEM_SIZE (1024 * 16) // 16 KB default

// Single page mode
// #define OPTION_CAL_SEGMENTS_SINGLE_PAGE

// Enable persistence, a binary (.BIN) file is used to store events and calibration segments
// This allows to safely build the A2L file only once per build, even if the creation order of events and segments changes
#define OPTION_ENABLE_PERSISTENCE

// Enable EPK calibration segment to check HEX/BIN file compatibility
// If the EPK is included in the HEX/BIN file, the version of the data structure can be checked using the EPK address specified in the A2L file
#define OPTION_CAL_SEGMENT_EPK

// Enable absolute addressing for calibration segments
// Default is segment relative addressing, uses address extension 0 for segment relative and 1 for absolute and encodes the segment number in the address high word
// As this is not compatible to most well known tools to update, modify and create A2L files, this option switches to absolute addressing on address extension 0
// Requirement is, that the address of all reference pages must be stable and in address range of 0x0000_0000 to 0xFFFF_FFFF
// #define OPTION_CAL_SEGMENTS_ABS

// Start on reference/default page instead of on working page
// #define OPTION_CAL_SEGMENTS_START_ON_REFERENCE_PAGE

// Automatically persist the working page on XCP disconnect
// #define OPTION_CAL_PERSIST_ON_DISCONNECT

//-------------------------------------------------------------------------------
// DAQ settings

#define OPTION_DAQ_MEM_SIZE (1024 * 8) // Memory bytes used for XCP DAQ tables - 6 bytes per measurement signal/block needed
#define OPTION_DAQ_EVENT_COUNT 64      // Maximum number of DAQ events (integer value, must be even)
// #define OPTION_DAQ_ASYNC_EVENT         // Create an asynchronous, cyclic DAQ event for asynchronous data acquisition

// Transport layer queue, vectored IO, lockless with variable queue entry size
// Default:
#define OPTION_QUEUE_64_VAR_SIZE

// Transport layer queue, vectored IO, lockless with fixed queue entry size
// For maximum performance with large DTO size, but less efficient memory usage with partially filled queue entries
// Entry size is XCPTL_MAX_DTO_SIZE  + XCPTL_TRANSPORT_LAYER_HEADER_SIZE (4) + 4
// Optimal overall queue size is required to be a multiple of the cache line size (so XCPTL_MAX_DTO_SIZE in xcptl_cfg.h currently set to 244)
// Tune XCPTL_MAX_DTO_SIZE for best compromise between memory efficiency and performance
// Larger DTO size may not payoff, rely on transport layer message accumulation
// #define OPTION_QUEUE_64_FIX_SIZE

// Transport layer queue, with variable queue entry size, 32 bit not lockless with mutex synchronization
// Mandatory for Windows and 32 bit platforms
// #define OPTION_QUEUE_32
#if defined(OPTION_ATOMIC_EMULATION) || defined(PLATFORM_32_BIT)
#undef OPTION_QUEUE_64_VAR_SIZE
#undef OPTION_QUEUE_64_FIX_SIZE
#define OPTION_QUEUE_32
#endif
//-------------------------------------------------------------------------------
// A2L generation settings

#define OPTION_ENABLE_A2L_GENERATOR // Enable A2L generator
#define OPTION_ENABLE_A2L_UPLOAD    // Enable A2L upload via XCP
#define OPTION_ENABLE_ELF_UPLOAD    // Enable ELF upload via XCP

// Enable socketGetLocalAddr for A2L file generation
// Used for convenience to get an existing ip address in A2L, when bound to ANY 0.0.0.0
// #define OPTION_ENABLE_GET_LOCAL_ADDR

//-------------------------------------------------------------------------------
// Tests

#if !defined(NDEBUG)

// #define TEST_CLOCK_GET_STATISTIC // Count number of calls to clockGet and clockGetLast, print results with clockPrintStatistic()
// #define TEST_ACQUIRE_LOCK_TIMING // Create a queue acquire time histogram, prints results on queue deinit, significant performance impact, for testing only !!!!!!!!!!
// #define TEST_ENABLE_DBG_METRICS // Enable debug metrics for XCP events and transport layer packets
// #define TEST_ENABLE_BUFFERCOUNT_HISTOGRAM // Enable histogram of the used buffer counts in the transport layer vectored io
// #define TEST_MUTABLE_ACCESS_OWNERSHIP // Enable tracking of mutable access thread ownership to detect overseen potential memory safety problems
#define TEST_ENABLE_DBG_CHECKS // Enable additional sanity checks in the XCP server

#endif // !defined(NDEBUG)

#endif // !XCPLIB_FOR_RUST
