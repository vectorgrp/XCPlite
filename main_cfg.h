#pragma once

#ifndef MAIN_CFG_H // Include guard needed for manual control
#define MAIN_CFG_H

// main_cfg.h

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */


#define APP_XCPLITE
#define APP_NAME "XCPlite"
#define APP_VERSION 5.00


//-----------------------------------------------------------------------------------------------------
// Application configuration:
// XCP configuration is in xcp_cfg.h (Protocol Layer) and xcptl_cfg.h (Transport Layer)

// Debug print verbosity
#define APP_DEFAULT_DEBUGLEVEL 1

// A2L generation
#define APP_ENABLE_A2L_GEN // Enable A2L generation

// Default ip addr and port
#define APP_DEFAULT_SERVER_PORT 5555 // Default UDP port, overwritten by commandline option
#define APP_DEFAULT_SERVER_ADDR {0,0,0,0} // Default IP addr, 0.0.0.0 = ANY, 255.255.255.255 = first adapter addr, overwritten by commandline option




#ifdef __cplusplus
extern "C" {
#endif

extern unsigned int gDebugLevel;
extern int gOptionTCP;

#ifdef __cplusplus
}
#endif

#endif
