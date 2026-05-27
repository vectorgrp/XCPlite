#pragma once

/*----------------------------------------------------------------------------
| File:
|   xcplib_ptptool_cfg.h
|
| Description:
|   XCPlite configuration OVERRIDES for building the ptptool
|   Applied AFTER the defaults in xcplib_cfg.h via:
|     cmake: target_compile_definitions(xcplite PRIVATE "XCPLIB_CFG_OVERRIDE=\"xcplib_ptptool_cfg.h\"")
|
|   Only settings that DIFFER from the POSIX defaults are listed here.
|   Key differences:
|     - #define OPTION_SOCKET_HW_TIMESTAMPS
|
 ----------------------------------------------------------------------------*/

#define OPTION_SOCKET_HW_TIMESTAMPS // Enable hardware timestamps on UDP sockets if available (needed only for ptptool on Linux)
