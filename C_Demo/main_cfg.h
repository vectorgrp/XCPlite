#pragma once

// main_cfg.h.in
// C_Demo

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */


#define APP_NAME "C_Demo"
#define APP_VERSION_MAJOR 5
#define APP_VERSION_MINOR 0


//-----------------------------------------------------------------------------------------------------
// Application configuration:
// XCP configuration is in xcp_cfg.h (Protocol Layer) and xcptl_cfg.h (Transport Layer)

#define ON 1
#define OFF 0

#define OPTION_DEBUG_LEVEL 2 

// A2L generation
#define OPTION_ENABLE_A2L_GEN ON // Enable A2L generation
#define OPTION_A2L_FILE_NAME APP_NAME ".a2l" // A2L full filename (with path)
#define OPTION_A2L_PROJECT_NAME "Demo" // A2L project name

// Default ip addr and port
#define OPTION_ENABLE_TCP ON // Enable TCP support and commandline option -tcp
#define OPTION_USE_TCP OFF // Enable TCP by default and commandline option -udp
#define OPTION_SERVER_PORT 5555 // Default UDP port, overwritten by commandline option -port
#define OPTION_SERVER_ADDR {0,0,0,0} // Default IP addr, 0.0.0.0 = ANY, 255.255.255.255 = first adapter found, overwritten by commandline option -bind x.x.x.x

// Calibration segment
#define OPTION_ENABLE_CAL_SEGMENT ON

#ifdef _WIN
#define OPTION_ENABLE_XLAPI_V3 OFF 
#if OPTION_ENABLE_XLAPI_V3

#define OPTION_USE_XLAPI_V3  // Use XL-API by default
#define OPTION_SERVER_XL_ADDR {192,168,0,200} // XL_API Ethernet Adapter IP
#define OPTION_SERVER_XL_MAC {0xdc,0xa6,0x32,0x7e,0x66,0xdc} // XL_API Ethernet Adapter MAC
#define OPTION_SERVER_XL_NET "NET1" // XL_API Network name
#define OPTION_SERVER_XL_SEG "SEG1" // XL_API Segment name

#endif
#endif

