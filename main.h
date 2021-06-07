// main.h 

#ifndef __MAIN_H_
#define __MAIN_H_


#ifdef _WIN64
  #define _WIN
#else
  #ifdef _WIN32
    #define _WIN
  #else
    #define _LINUX
    #define LINUX
  #endif
#endif

#ifndef _WIN // Linux

#define _LINUX
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/stat.h>
#include <pthread.h> // link with -lpthread
#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/ip.h>
#include <linux/udp.h>
//#include <linux/arp.h>
#include <arpa/inet.h>

#define strncpy_s(a,b,c,d) strncpy(a,c,d)
#define strcpy_s strcpy
#define sscanf_s sscanf
#define sprintf_s sprintf
#define MAX_PATH 256

#else // Windows

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <malloc.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <conio.h>

#ifdef __cplusplus
#include <iostream>
#include <chrono>
#include <thread>
#include <typeinfo>
#endif

// Need to link with vxlapi.lib
#ifdef _WIN64
#pragma comment(lib, "vxlapi64.lib")
#else
#ifdef _WIN32
#pragma comment(lib, "vxlapi.lib")
#endif
#endif
#include "vxlapi.h"

// Need to link with Ws2_32.lib
#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>

#endif


//-----------------------------------------------------------------------------------------------------
// Application configuration:
// XCP configuration is in xcp_cfg.h and xcptl_cfg.h

#ifdef _LINUX // Linux

// Config 
#define XCPSIM_DEBUG_LEVEL 1
#define XCPSIM_ENABLE_A2L_GEN // Enable A2L generation

#define XCPSIM_SLAVE_UUID {0xdc,0xa6,0x32,0xFF,0xFE,0x7e,0x66,0xdc} // Default slave clock UUID
#define XCPSIM_SLAVE_PORT 5555 // Default UDP port

#define getA2lSlavePort() XCPSIM_SLAVE_PORT // for A2L generation
#define getA2lSlaveIP() ("172.31.31.194")

#endif
#ifdef _WIN // Windows

// Config 
#define XCPSIM_DEBUG_LEVEL 1

#define XCPSIM_ENABLE_A2L_GEN // Enable A2L generation


// XL-API UDP stack
#define XCPSIM_SLAVE_XL_NET "NET1" // Default V3 Network name
#define XCPSIM_SLAVE_XL_SEG "SEG1" // Default V3 Segment name
#define XCPSIM_SLAVE_XL_MAC {0xdc,0xa6,0x32,0x7e,0x66,0xdc} // Default Ethernet Adapter MAC
#define XCPSIM_SLAVE_XL_IP_S "172.31.31.194" // Default V3 Ethernet Adapter IP as string
#define XCPSIM_SLAVE_XL_IP {172,31,31,194} // Default V3 Ethernet Adapter IP

#define XCPSIM_SLAVE_UUID {0xdc,0xa6,0x32,0xFF,0xFE,0x7e,0x66,0xdc} // Default slave clock UUID
#define XCPSIM_SLAVE_PORT 5555 // Default UDP port

#define getA2lSlavePort() XCPSIM_SLAVE_PORT // for A2L generation
#define getA2lSlaveIP() ((gOptionUseXLAPI)?XCPSIM_SLAVE_XL_IP_S:"127.0.0.1")

#endif

// Slave name
#define XCPSIM_SLAVE_ID_LEN       7    /* Slave device identification length */
#define XCPSIM_SLAVE_ID          "XCPlite"  /* Slave device identification */

#include "xcpLite.h"
#include "util.h" 

#ifdef _WIN 
#include "udp.h" // UDP stack for Vector XL-API V3 
#endif
#include "udpserver.h" // XCP on UDP server
#include "xcpSlave.h" // XCP on UDP transport layer

#ifdef XCPSIM_ENABLE_A2L_GEN // Enable A2L generator
#include "A2L.h"
#endif

#include "ecu.h" // Demo measurement task C
#include "ecupp.hpp" // Demo measurement task C++

#ifdef __cplusplus
extern "C" {
#endif


// Options
extern volatile unsigned int gDebugLevel;

extern int gOptionUseXLAPI;
extern char gOptionsXlSlaveNet[32];
extern char gOptionsXlSlaveSeg[32];

extern int gOptionA2L;
extern char gOptionA2L_Path[MAX_PATH];


// Externals
extern int createA2L(const char* path_name);


#ifdef __cplusplus
}
#endif

#endif
