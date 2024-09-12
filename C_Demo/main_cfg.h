#pragma once
#define __MAIN_CFG_H__

// main_cfg.h
// C_Demo

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

// Application configuration:
// XCP configuration is in xcp_cfg.h (Protocol Layer) and xcptl_cfg.h (Transport Layer)

#define ON 1
#define OFF 0

// Set clock resolution (for clock function in platform.c)
//#define CLOCK_USE_APP_TIME_US
#define CLOCK_USE_UTC_TIME_NS

// Platform options
#define PLATFORM_ENABLE_GET_LOCAL_ADDR
#define PLATFORM_ENABLE_KEYBOARD

// Ethernet Server
// TCP or/and UDP server enabled
#define XCPTL_ENABLE_TCP
#define XCPTL_ENABLE_UDP
#define XCP_SERVER_FORCEFULL_TERMINATION // Otherwise use gracefull server thread termination in xcplib

// Ethernet Transport Layer
#define OPTION_USE_TCP                  OFF
#define OPTION_MTU                      1500            // Ethernet MTU
#define OPTION_SERVER_PORT              5555            // Default UDP port
#define OPTION_SERVER_ADDR              {127,0,0,1}     // IP addr to bind, 0.0.0.0 = ANY

// Enable demo how to create a calibration segment with page switching
#define OPTION_ENABLE_CAL_SEGMENT ON

// A2L generation
#define OPTION_ENABLE_A2L_GEN ON  // Enable A2L generation
#if OPTION_ENABLE_A2L_GEN
  #define OPTION_A2L_NAME                "C_Demo"     // A2L name 
  #define OPTION_A2L_FILE_NAME           "C_Demo.a2l" // A2L filename 
#endif

// Debug prints
#define OPTION_ENABLE_DBG_PRINTS        ON
#define OPTION_DEBUG_LEVEL              3 // 1 - Error, 2 - Warn, 3 - Info, 4 - Trace, 5 - Debug 
