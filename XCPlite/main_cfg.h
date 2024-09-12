#pragma once

// main_cfg.h
// XCPlite

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */


// When static library is used, consider the following options which are compiled into it
/*

  xcpAppl.c:
  OPTION_A2L_NAME
  OPTION_A2L_FILE_NAME

  xcptl_cfg.h:
  XCPTL_QUEUE_SIZE
  XCPTL_MAX_SEGMENT_SIZE (usually is (OPTION_MTU-20-8))
   
  xcp_cfg.h:
  XCP_MAX_EVENT 
  XCP_DAQ_MEM_SIZE 
  CLOCK_TICKS_PER_S

  xcpLite.c
  XCP_ENABLE_CAL_PAGE // Enable cal page switch, would require callbacks to the application code !
  XCP_ENABLE_TEST_CHECKS
  XCP_ENABLE_DYN_ADDRESSING   MTA==0x01
  XCP_ENABLE_IDT_A2L_UPLOAD   MTA==0xFF

*/


// Application configuration:
// XCP configuration is in xcp_cfg.h (Protocol Layer) and xcptl_cfg.h (Transport Layer)

#define ON 1
#define OFF 0

// Set clock resolution (for clock function in platform.c)
#define CLOCK_USE_APP_TIME_US
//#define CLOCK_USE_UTC_TIME_NS

// A2L generation
#define OPTION_ENABLE_A2L_GEN           ON // Enable A2L generation
#if OPTION_ENABLE_A2L_GEN
#define OPTION_A2L_NAME                 "XCPlite"     // A2L name 
#define OPTION_A2L_FILE_NAME            "XCPlite.a2l" // A2L filename 
#endif

// Ethernet Transport Layer
#define OPTION_USE_TCP                  OFF
#define OPTION_MTU                      1500            // Ethernet MTU
#define OPTION_SERVER_PORT              5555            // Default UDP port
#define OPTION_SERVER_ADDR              {127,0,0,1}     // IP addr to bind, 0.0.0.0 = ANY

// Shutdown options for the XCP server
//#define OPTION_SERVER_FORCEFULL_TERMINATION ON

// Debug prints
#define OPTION_ENABLE_DBG_PRINTS        ON
#define OPTION_DEBUG_LEVEL              2

