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

// XCPlite version, currently V2.1.0
#define OPTION_VERSION_MAJOR 2
#define OPTION_VERSION_MINOR 1
#define OPTION_VERSION_PATCH 0

// CANape version compatibility
// Disable workarounds for CANape versions < 24SP2
// (Disable COPY_CAL_PAGE bug workaround and enable .this references to shared calibration axis)
#define OPTION_CANAPE_24

//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------
// Specific build settings of libxcplite

#if defined(XCPLIB_FOR_RUST) // Set by the Rust build script

#include "xcplib_rust_cfg.h" // for Rust xcp-lite specific configuration

#else

//-------------------------------------------------------------------------------
//-------------------------------------------------------------------------------
// Default settings
// For 64 bit targets with on-target A2L generation and file system support

//-------------------------------------------------------------------------------
// Logging

// Enable debug prints
#define OPTION_ENABLE_DBG_PRINTS
// Enable debug print errors and warnings go to stderr
#define OPTION_ENABLE_DBG_STDERR
// Default log level: 1 - Error, 2 - Warn, 3 - Info, 4 - Trace (used to print all XCP commands), 5 - Debug, 6 - Very verbose
#define OPTION_DEFAULT_DBG_LEVEL 3
// Optimize code size, higher levels than OPTION_MAX_DBG_LEVEL are optimized out
#define OPTION_MAX_DBG_LEVEL 4
// Optimize code size, fixed log level, not changeable at runtime
// #define OPTION_FIXED_DBG_LEVEL 4

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
// Used for testing on Windows
#if defined(_WIN32) || defined(_WIN64)
#define OPTION_ATOMIC_EMULATION
#endif

//-------------------------------------------------------------------------------
// XCP multi application mode
// Multiple application processes may have shared transmit queue, calibration RCU and XCP state
// One application is the XCP server, could be the first one running (XCP leader) or a dedicated application (XCP daemon)
// Requires a POSIX-compliant platform (Linux / macOS / QNX).  Not supported on Windows.

// Experimental, work in progress, not fully tested yet, may change or be removed without major version change, use with caution

// #define OPTION_SHM_MODE

//-------------------------------------------------------------------------------
// XCP server options

#define OPTION_ENABLE_TCP
#define OPTION_ENABLE_UDP
#define OPTION_MTU 8000                     // Ethernet packet size (MTU), must be %8 - Jumbo frames supported
#define OPTION_SERVER_FORCEFULL_TERMINATION // Don't wait for the rx and tx thread to finish, just terminate them

//-------------------------------------------------------------------------------
// Calibration segments management

// Enable calibration segments management and API for thread-safe calibration
#define OPTION_CAL_SEGMENTS
#ifdef OPTION_CAL_SEGMENTS

// Maximum number of calibration segments
#define OPTION_CAL_SEGMENT_COUNT 8

// Total memory pool size for all calibration segments (header + 4 pages each)
// Must be large enough for all XcpCreateCalSeg() calls combined
#define OPTION_CAL_MEM_SIZE (1024 * 5) // 5 KB default

// Single page mode
// #define OPTION_CAL_SEGMENTS_SINGLE_PAGE

// Enable persistence, a binary (.BIN) file is used to store events and calibration segments
// This allows to safely build the A2L file only once per build, even if the creation order of events and segments changes
#define OPTION_ENABLE_PERSISTENCE

// Enable EPK calibration segment to check HEX/BIN file compatibility
// If the EPK is included in the HEX/BIN file, the version of the data structure can be checked using the EPK address specified in the A2L file
#define OPTION_CAL_SEGMENT_EPK

// Enable absolute addressing for calibration segments and calibration blocks
// Default is segment relative addressing, uses address extension 0 for segment relative and 1 for absolute and encodes the segment number in the address high word
// As this is not compatible to most well known tools to update, modify and create A2L files, this option switches to absolute addressing on address extension 0
// Requirement is, that the address of all default/reference pages must be stable and in address range of 0x0000_0000 to 0xFFFF_FFFF
// #define OPTION_CAL_SEGMENTS_ABS

// Start on reference/default page instead of on working page
// #define OPTION_CAL_SEGMENTS_START_ON_REFERENCE_PAGE

// Automatically persist the working page on XCP disconnect
// #define OPTION_CAL_PERSIST_ON_DISCONNECT

#endif // OPTION_CAL_SEGMENTS

//-------------------------------------------------------------------------------
// DAQ settings

#define OPTION_DAQ_MEM_SIZE (512 * 6) // Memory bytes used for XCP DAQ tables - 6 bytes per measurement signal/block needed
#define OPTION_DAQ_EVENT_COUNT 32     // Maximum number of DAQ events (integer value, must be even)
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
// Optional benchmark switch for queue64f.c:
// If undefined, slot reuse is published by a release-store to the fixed entry_header and tail is relaxed.
// If defined, slot reuse is published by a release update to tail and producers acquire-load tail.
// #define OPTION_QUEUE_64_FIX_SIZE_SYNC_TAIL

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
// #define TEST_ACQUIRE_SPIN_COUNT // Get max spin count of the queue acquire operations
// #define TEST_ACQUIRE_LOCK_TIMING // Create a queue acquire time histogram, prints results on queue deinit, significant performance impact, for testing only !!!!!!!!!!
// #define TEST_ENABLE_DBG_METRICS // Enable debug metrics for XCP events and transport layer packets
// #define TEST_ENABLE_BUFFERCOUNT_HISTOGRAM // Enable histogram of the used buffer counts in the transport layer vectored io
// #define TEST_MUTABLE_ACCESS_OWNERSHIP // Enable tracking of mutable access thread ownership to detect overseen potential memory safety problems
// #define TEST_ENABLE_DBG_CHECKS // Enable timing checks in the XCP server
// #define TEST_STACK_SIZE // Enable stack size measurement for the transmit and receive thread

#endif // !defined(NDEBUG)

// Optional application-specific override — patches any of the above defaults.
// Pass the filename of your override header via:
//   cmake: target_compile_definitions(xcplite PRIVATE "XCPLIB_CFG_OVERRIDE=\"my_xcplib_overrides.h\"")
// See xcplib_rtos_cfg.h and xcplib_no_a2l_cfg.h for example override files.
#ifdef XCPLIB_CFG_OVERRIDE
#include XCPLIB_CFG_OVERRIDE
#endif

#endif
