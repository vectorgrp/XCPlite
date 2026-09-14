#pragma once

/*----------------------------------------------------------------------------
| File:
|   xcplib_app_cfg.h
|
| Description:
|   Application specific xcplite configuration OVERRIDES for fetchcontent_example
|   Applied AFTER the defaults in xcplib_cfg.h. Selected in CMakeLists.txt via
|     set(XCPLITE_CFG_OVERRIDE "${CMAKE_CURRENT_SOURCE_DIR}/config/xcplib_app_cfg.h")
|   before FetchContent_MakeAvailable(xcplite), which makes xcplite define
|   XCPLIB_CFG_OVERRIDE="xcplib_app_cfg.h" for the library and for this application.
|
|   Only #undef / #define OPTION_* here. All tunables are documented in docs/xcplib_cfg.md.
|   To build on a shipped configuration instead of the defaults, include it first, e.g.
|     #include "xcplib_no_a2l_cfg.h"
|
|   Main purpose of this example is to demonstrate fetchcontent
|
|   This example has a minimal user feature configuration
|   - No thread-safe calibration segment management and RCU
|   - No calibration data persistence
|   - UDP only with ethernet MTU = 1500
|   - No online A2L generation
|   - No dynamic event creation
|   - 32 bit queue with mutex
|   - Reduced DAQ table memory
|   - Log level limited to 3
|
 ----------------------------------------------------------------------------*/

//-------------------------------------------------------------------------------
// Logging: less verbose library default (the application may still raise it with XcpSetLogLevel)
#undef OPTION_DEFAULT_DBG_LEVEL
#define OPTION_DEFAULT_DBG_LEVEL 3
#undef OPTION_MAX_DBG_LEVEL
#define OPTION_MAX_DBG_LEVEL 3 // Save program space, level>3 not compiled

//-------------------------------------------------------------------------------
// Calibration:

// Disable calibration segment management and RCU completely
#undef OPTION_CAL_SEGMENTS

#ifdef OPTION_CAL_SEGMENTS

// Absolute addressing mode for calibration segments (address extension 0 is absolute addressing)
#define OPTION_CAL_SEGMENTS_ABS

// Disable the EPK calibration segment
// There is still an EPK, but it is not inside a calibration segment
#undef OPTION_CAL_SEGMENT_EPK

// Memory for calibration segments (if enabled)
#undef OPTION_CAL_SEGMENT_COUNT
#define OPTION_CAL_SEGMENT_COUNT 2
#undef OPTION_CAL_MEM_SIZE
#define OPTION_CAL_MEM_SIZE (1024 * 1)

#endif

//-------------------------------------------------------------------------------
// Runtime A2L generation

#undef OPTION_ENABLE_A2L_GENERATOR
#undef OPTION_ENABLE_A2L_UPLOAD
#undef OPTION_ENABLE_ELF_UPLOAD

//-------------------------------------------------------------------------------
// Events

// No runtime DAQ event management (dynamic event creation at runtime)
// Disables tXcpEvent, XcpCreateIndexedEvent, XcpCreateEvent, XcpCreateEventInstance, XcpGetEventCount, XcpFindEvent, XcpGetEventName, XcpGetEventIndex, XcpGetEvent
#undef OPTION_DAQ_EVENT_LIST

//-------------------------------------------------------------------------------
// Persistence

// No persistence for (dynamic events and calibration segments and calibration data)
#undef OPTION_ENABLE_PERSISTENCE

//-------------------------------------------------------------------------------
// DAQ

// Memory for DAQ lists
#undef OPTION_DAQ_MEM_SIZE
#define OPTION_DAQ_MEM_SIZE (512 * 16)

//-------------------------------------------------------------------------------
// XCP server transport

// UDP only
#undef OPTION_ENABLE_TCP

// Optional: Use buildin XCPlite UDP stack, provide an ethernet HAL implementation
// #undef OPTION_ENABLE_UDP
// #define OPTION_ENABLE_UDP_RAW
// #define OPTION_UDP_RAW_ZERO_COPY // Supported by OPTION_QUEUE_32

// Ethernet MTU, no jumbo frames
// Raise this, if your network path allows it. The default XCPlite configuration will error on fragmentation.
// # Test MTU 1501 — just above standard Ethernet MTU: ping -D -c 3 -s 1473 192.168.0.206
#undef OPTION_MTU
#define OPTION_MTU 1500

//-------------------------------------------------------------------------------
// Transmit queue

#undef OPTION_QUEUE_64_VAR_SIZE
#undef OPTION_QUEUE_64_FIX_SIZE
#define OPTION_QUEUE_32
