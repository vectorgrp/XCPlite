#pragma once

/*----------------------------------------------------------------------------
| File:
|   xcplib_shm_cfg.h
|
| Description:
|   XCPlite configuration OVERRIDES for multi application mode (SHM)
|   Applied AFTER the defaults in xcplib_cfg.h via:
|     cmake: target_compile_definitions(xcplite PRIVATE "XCPLIB_CFG_OVERRIDE=\"xcplib_shm_cfg.h\"")
|
|   Key differences in overrides from the defaults in xcplib_cfg.h:
|     OPTION_SHM_MODE
|   Optional
|.    OPTION_ENABLE_PTP
|   Addressing scheme:
|     Default: Relative memory addressing for calibration segment and blocks
|   Platform requirements:
|    Default: Filesystem required, 64 bit platform
|   Examples:
|    hello_xcp, hello_xcp_cpp, silkit_demo
|   Tools:
|     shmtool, helper scripts shm_xxxx.sh
|   Tests:
|     -
 ----------------------------------------------------------------------------*/

//-------------------------------------------------------------------------------
// XCP multi application mode
// Multiple application processes may have shared transmit queue, calibration RCU and XCP state
// One application is the XCP server, could be the first one running (XCP leader) or a dedicated application (XCP daemon)
// Requires a POSIX-compliant platform (Linux / macOS / QNX).  Not supported on Windows.

// Experimental, work in progress, demonstrated in silkit_demo
#define OPTION_SHM_MODE

// Linker section based event and calibration segment registration not supported
#undef OPTION_SECTION_REGISTRATION
