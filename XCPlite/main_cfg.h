#pragma once

// main_cfg.h
// XCPlite

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

// Application configuration:
// XCP configuration is in xcp_cfg.h (Protocol Layer) and xcptl_cfg.h (Transport Layer)

#define APP_NAME                "XCPlite"
#define APP_VERSION_MAJOR       6
#define APP_VERSION_MINOR       0



#define ON 1
#define OFF 0


// Debug prints
#define OPTION_ENABLE_DBG_PRINTS        ON
#define OPTION_DEBUG_LEVEL              2 
#define OPTION_UDP_DEBUG_LEVEL          0  


// A2L generation
#define OPTION_ENABLE_A2L_GEN           ON            // Enable A2L generation
#if OPTION_ENABLE_A2L_GEN
#define OPTION_ENABLE_A2L_SYMBOL_LINKS  ON            // Enable generation of symbol links (required for CANape integrated linker map update)
#define OPTION_A2L_NAME                 "XCPlite"     // A2L name 
#define OPTION_A2L_FILE_NAME            "XCPlite.a2l" // A2L filename 
#define OPTION_A2L_PROJECT_NAME         "XCPlite"     // A2L project name
#endif


// Set clock resolution (for clock function in platform.c)
//#define CLOCK_USE_APP_TIME_US
#define CLOCK_USE_UTC_TIME_NS


// Ethernet Transport Layer
#define OPTION_ENABLE_ETH_TRANSPORT      ON 
#define OPTION_USE_TCP                   OFF // Default is UDP
#define OPTION_MTU                       1500 // MTU Standard Frame
#define OPTION_SERVER_PORT               5555 // Default UDP port, overwritten by commandline option
#define OPTION_SERVER_ADDR               {127,0,0,1} // Default IP addr, 0.0.0.0 = ANY, overwritten by commandline option


