#pragma once

// main_cfg.h.in
// CPP_Demo

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#define APP_CPP_DEMO

#define APP_NAME "CPP_Demo"
#define APP_VERSION_MAJOR 5
#define APP_VERSION_MINOR 0

//-----------------------------------------------------------------------------------------------------
// Application configuration:
// XCP configuration is in xcp_cfg.h (Protocol Layer) and xcptl_cfg.h (Transport Layer)

#define ON 1
#define OFF 0

#define OPTION_DEBUG_LEVEL 1 

// A2L generation
#define OPTION_ENABLE_A2L_GEN ON // Enable A2L generation
#define OPTION_A2L_FILE_NAME APP_NAME ".a2l" // A2L full filename (with path)
#define OPTION_A2L_PROJECT_NAME "Demo" // A2L project name

// Default ip addr and port
#define OPTION_ENABLE_TCP ON // Enable TCP support and commandline option -tcp
#define OPTION_USE_TCP OFF // Enable TCP by default and commandline option -udp
#define OPTION_SERVER_PORT 5555 // Default UDP port
#define OPTION_SERVER_ADDR {0,0,0,0} // Default IP addr, 0.0.0.0 = ANY, 255.255.255.255 = first adapter found, overwritten by commandline option -bind x.x.x.x





