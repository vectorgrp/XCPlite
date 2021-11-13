// main_cfg.h 

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __MAIN_H_
#define __MAIN_H_

//-----------------------------------------------------------------------------------------------------
// Application configuration:
// XCP configuration is in xcp_cfg.h (Protocol Layer) and xcptl_cfg.h (Transport Layer)

// Name and version
#define APP_XCPLITE
#define APP_NAME "XCPlite"
#define APP_NAME_LEN 7
#define APP_VERSION "4.0"

// Debug print verbosity 
#define APP_DEFAULT_DEBUGLEVEL 1

// A2L generation
#define APP_ENABLE_A2L_GEN // Enable A2L generation
#ifdef _LINUX // Linux
#define APP_DEFAULT_A2L_PATH "./"
#else // Windows
#define APP_DEFAULT_A2L_PATH ".\\"
#endif

// Clock resolution and UUID
#define APP_DEFAULT_CLOCK_UUID {0xdc,0xa6,0x32,0xFF,0xFE,0x7e,0x66,0xdc} // Clock UUID

// Default ip addr and port 
#define APP_DEFAULT_SLAVE_PORT 5555 // Default UDP port, overwritten by commandline option 
#define APP_DEFAULT_SLAVE_ADDR {0,0,0,0} // Default IP addr, 0.0.0.0 = ANY, 255.255.255.255 = first adapter addr, overwritten by commandline option 

// Jumbo frames
#define APP_DEFAULT_JUMBO 1

#ifdef __cplusplus
extern "C" {
#endif

extern volatile unsigned int gDebugLevel;

#ifdef __cplusplus
}
#endif

#endif
