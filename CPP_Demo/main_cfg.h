#pragma once

// main_cfg.h.in
// CPP_Demo

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

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

// Default ip addr and port
#define OPTION_USE_TCP ON 
#define OPTION_SERVER_PORT 5555 // Default UDP port
#define OPTION_SERVER_ADDR {127,0,0,1} // Default IP addr, 0.0.0.0 = ANY, 255.255.255.255 = first adapter addr

// Calibration segment
#define OPTION_ENABLE_CAL_SEGMENT 




