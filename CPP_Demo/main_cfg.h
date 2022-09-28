#pragma once

// main_cfg.h
// CPP_Demo

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#define APP_CPP_Demo

#define APP_NAME "CPP_Demo"
#define APP_VERSION_MAJOR 6
#define APP_VERSION_MINOR 0

//-----------------------------------------------------------------------------------------------------
// Application configuration:
// XCP configuration is in xcp_cfg.h (Protocol Layer) and xcptl_cfg.h (Transport Layer)

#define ON 1
#define OFF 0

#define OPTION_DEBUG_LEVEL 1 

// A2L generation
#define OPTION_ENABLE_A2L_GEN          ON             // Enable A2L generation
#if OPTION_ENABLE_A2L_GEN
#define OPTION_ENABLE_A2L_SYMBOL_LINKS ON             // Enable generation of symbol links (required for CANape integrated linker map update)
#define OPTION_A2L_NAME                "CPP_Demo"     // A2L name 
#define OPTION_A2L_FILE_NAME           "CPP_Demo.a2l" // A2L filename 
#define OPTION_A2L_PROJECT_NAME        "CPP_Demo"     // A2L project name
#endif

// Default communication parameters
#define OPTION_ENABLE_TCP             ON // Enable TCP support
#define OPTION_USE_TCP                ON // Default is UDP
#define OPTION_MTU                    1500
#define OPTION_SERVER_PORT            5555 // Default UDP port, overwritten by commandline option
#define OPTION_SERVER_ADDR            {0,0,0,0} // Default IP addr, 0.0.0.0 = ANY, overwritten by commandline option
