#pragma once

/*----------------------------------------------------------------------------
| File:
|   xcplib_ptp_cfg.h
|
| Description:
|   XCPlite configuration OVERRIDES for building the library with PTP support.
|   Applied AFTER the defaults in xcplib_cfg.h via:
|     cmake: target_compile_definitions(xcplite PRIVATE "XCPLIB_CFG_OVERRIDE=\"xcplib_ptp_cfg.h\"")
|

|   Key differences in overrides from the defaults in xcplib_cfg.h:
|     OPTION_SOCKET_HW_TIMESTAMPS
|     OPTION_ENABLE_PTP

|   Addressing scheme:
|     Default:
|   Platform requirements:
|     Linux with hardware timestamping capable network interface
|     (e.g. Intel i210 or i350, Raspberry Pi 5) and kernel support for socket hardware timestamps (e.g. Linux 5.11 or later)
|   Examples:
|    ptp_demo
|   Tools:
|    ptptool
|   Tests:
|     clock_test, ptp_test
 ----------------------------------------------------------------------------*/

#define OPTION_SOCKET_HW_TIMESTAMPS // Enable hardware timestamps on UDP sockets if available (needed only for ptptool on Linux)

// Contexts and spans are supported in the default configuration only
#undef OPTION_DAQ_CONTEXT_LIST
