// main.h 

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __MAIN_H_
#define __MAIN_H_

// Windows or Linux ?
#if defined(_WIN32) || defined(_WIN64)
  #define _WIN
  #if defined(_WIN32) && defined(_WIN64)
    #undef _WIN32
  #endif
#else
  #if defined (_ix64_) || defined (__x86_64__) || defined (__aarch64__)
    #define _LINUX64
  #else
    #define _LINUX32
  #endif
  #define _LINUX
#endif


#ifndef _WIN // Linux

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#define M_2PI (2*M_PI)
#define M_PI (M_2PI/2)
#include <math.h>
#include <assert.h>
#include <errno.h>

#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>
#include <pthread.h> 

#include <ifaddrs.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define MAX_PATH 256

#else // Windows

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <time.h>
#include <conio.h>
#define M_2PI 6.28318530718
#define M_PI (M_2PI/2)
#include <math.h>
#ifdef __cplusplus
#include <thread>
#endif

// Need to link with Ws2_32.lib
#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>

#endif


//-----------------------------------------------------------------------------------------------------
// Application configuration:
// XCP configuration is in xcp_cfg.h and xcptl_cfg.h


#define APP_XCPLITE
#define APP_NAME "XCPlite"
#define APP_NAME_LEN 7
#define APP_VERSION "3.2"

#define APP_DEFAULT_DEBUGLEVEL 1

#define APP_ENABLE_A2L_GEN // Enable A2L generation

// #define APP_ENABLE_CAL_SEGMENT // Enable calibration memory segment 

//#define CLOCK_USE_UTC_TIME_NS // Use ns timestamps relative to 1.1.1970 (TAI monotonic - no backward jumps) 
#define CLOCK_USE_APP_TIME_US // Use unsynchronized us timestamps from local relative to application start

#define APP_DEFAULT_JUMBO 0 // Disable jumbo frames

#define APP_DEFAULT_SLAVE_PORT 5555 // Default UDP port, overwritten by commandline option 
#define APP_DEFAULT_SLAVE_IP {172,31,31,194} // Default Ethernet Adapter IP, overwritten by commandline option
#define APP_DEFAULT_SLAVE_MAC {0xdc,0xa6,0x32,0x7e,0x66,0xdc} // XL_API option Ethernet Adapter MAC
#define APP_DEFAULT_SLAVE_UUID {0xdc,0xa6,0x32,0xFF,0xFE,0x7e,0x66,0xdc} // Slave clock UUID 

// Multicast (GET_DAQ_CLOCK_MULTICAST)
// Use multicast time synchronisation to improve synchronisation of multiple XCP slaves
// This is standard in XCP V1.3, but it needs to create an additional thread and socket for multicast reception
// Adjust CANape setting in device/protocol/event/TIME_CORRELATION_GETDAQCLOCK from "multicast" to "extended response" if this is not desired 
//#define APP_ENABLE_MULTICAST 
#ifdef APP_ENABLE_MULTICAST
    // XCP default cluster id (multicast addr 239,255,0,1, group 127,0,1 (mac 01-00-5E-7F-00-01)
    // #define XCP_MULTICAST_CLUSTER_ID 1
    // #define XCP_MULTICAST_PORT 5557
    #define XCP_DEFAULT_MULTICAST_MAC {0x01,0x00,0x5E,0x7F,0x00,0x01}
#endif

// XCP slave name 
#define APP_SLAVE_ID_LEN      APP_NAME_LEN  /* Slave device identification length */
#define APP_SLAVE_ID          APP_NAME  /* Slave device identification */


#ifdef _LINUX // Linux

    #define APP_DEFAULT_A2L_PATH "./"

#else // Windows

    #define APP_DEFAULT_A2L_PATH ".\\"

    // Enable XL-API ethernet adapter and UDP stack for Windows PC with Vector Automotive Ethernet Adapter Hardware
    // Use case is emulation of an ECU with automotive Ethernet 1000-Base-t1 or 100-Base-t1 (BroadrReach)
    //#define APP_ENABLE_XLAPI_V3 
    #ifdef APP_ENABLE_XLAPI_V3

        // XL-API V3 UDP stack parameters
        #define APP_DEFAULT_SLAVE_XL_NET "NET1" // XL_API Network name, overwritten by commandline option
        #define APP_DEFAULT_SLAVE_XL_SEG "SEG1" // XL_API Segment name, overwritten by commandline option

        // Need to link with vxlapi.lib
        #ifdef _WIN64
            #pragma comment(lib, "vxlapi64.lib")
        #else
        #ifdef _WIN32
            #pragma comment(lib, "vxlapi.lib")
        #endif
        #endif
        #include "vxlapi.h"


    #endif 

#endif 


//-----------------------------------------------------------------------------------------------------

#include "util.h" 
#include "clock.h" 

#include "xcpLite.h"

#ifdef _WIN 
#include "udp.h" // UDP stack for Vector XL-API V3 
#endif

#include "xcpTl.h" // XCP on UDP transport layer
#include "xcpSlave.h" // XCP slave

#ifdef APP_ENABLE_A2L_GEN // Enable A2L generator
#include "A2L.h"
#endif

#include "ecu.h" // Demo measurement task C
#include "ecupp.hpp" // Demo measurement task C++

//-----------------------------------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif


// Options
extern volatile unsigned int gDebugLevel;
extern int gOptionJumbo;
extern uint16_t gOptionSlavePort;
extern unsigned char gOptionSlaveAddr[4];
extern char gOptionA2L_Path[MAX_PATH];
extern int gOptionUseXLAPI;

#ifdef APP_ENABLE_XLAPI_V3
    extern char gOptionXlSlaveNet[32];
    extern char gOptionXlSlaveSeg[32];
#endif

// Create the A2L file
#ifdef APP_ENABLE_A2L_GEN // Enable A2L generator
    extern int createA2L(const char* a2l_path_name, const char* mdi_path_name);
    extern char* getA2lSlaveIP(); // Info needed by createA2L()
    extern uint16_t getA2lSlavePort();
#endif

#ifdef __cplusplus
}
#endif

#endif
