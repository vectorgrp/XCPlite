/*----------------------------------------------------------------------------
| File:
|   xcpEthServer.c
|
| Description:
|   XCP on UDP Server
|   SHows how to integrate the XCP driver in an application
|   Creates threads for cmd handling and data transmission
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
-----------------------------------------------------------------------------*/

#include "main.h"
#include "platform.h"
#include "dbg_print.h"
#include "xcpLite.h"   
#include "xcpEthServer.h"


#if defined(_WIN) // Windows
static DWORD WINAPI XcpServerReceiveThread(LPVOID lpParameter);
#elif defined(_LINUX) // Linux
static void* XcpServerReceiveThread(void* par);
#endif
#if defined(_WIN) // Windows
static DWORD WINAPI XcpServerTransmitThread(LPVOID lpParameter);
#elif defined(_LINUX) // Linux
static void* XcpServerTransmitThread(void* par);
#endif


static struct {

    BOOL isInit; 

    // Threads
    tXcpThread DAQThreadHandle;
    volatile int TransmitThreadRunning;
    tXcpThread CMDThreadHandle;
    volatile int ReceiveThreadRunning;

} gXcpServer;


// Check XCP server status
BOOL XcpEthServerStatus() {
    return gXcpServer.isInit && gXcpServer.TransmitThreadRunning && gXcpServer.ReceiveThreadRunning;
}


// XCP server init
BOOL XcpEthServerInit(const uint8_t* addr, uint16_t port, BOOL useTCP, uint16_t segmentSize)
{
    int r = 0;

    if (gXcpServer.isInit) return FALSE;
    DBG_PRINT1("\nStart XCP server\n");

    gXcpServer.TransmitThreadRunning = 0;
    gXcpServer.ReceiveThreadRunning = 0;

    // Initialize XCP protocol layer
    XcpInit();

    // Initialize XCP transport layer
    r = XcpEthTlInit(addr, port, useTCP, segmentSize, TRUE /*blocking rx*/);
    if (!r) return 0;

    // Start XCP protocol layer
    XcpStart();

    // Create threads
    create_thread(&gXcpServer.DAQThreadHandle, XcpServerTransmitThread);
    create_thread(&gXcpServer.CMDThreadHandle, XcpServerReceiveThread);

    gXcpServer.isInit = TRUE;
    return TRUE;
}

BOOL XcpEthServerShutdown() {

    if (gXcpServer.isInit) {

        XcpDisconnect();
        cancel_thread(gXcpServer.DAQThreadHandle);
        cancel_thread(gXcpServer.CMDThreadHandle);

        // Shutdown XCP transport layer
        XcpTlShutdown();
    }
    return TRUE;
}


// XCP server unicast command receive thread
#if defined(_WIN) // Windows
DWORD WINAPI XcpServerReceiveThread(LPVOID par)
#elif defined(_LINUX) // Linux
extern void* XcpServerReceiveThread(void* par)
#endif
{
    (void)par;
    DBG_PRINT3("Start XCP CMD thread\n");

    // Receive XCP unicast commands loop
    gXcpServer.ReceiveThreadRunning = 1;
    for (;;) { 
      if (!XcpTlHandleCommands(XCPTL_TIMEOUT_INFINITE)) { // Timeout Blocking
        break; // error -> terminate thread
      }
    }
    gXcpServer.ReceiveThreadRunning = 0;

    DBG_PRINT_ERROR("ERROR: XcpTlHandleCommands failed!\n");
    DBG_PRINT_ERROR("ERROR: XcpServerReceiveThread terminated!\n");
    return 0;
}


// XCP server transmit thread
#if defined(_WIN) // Windows
DWORD WINAPI XcpServerTransmitThread(LPVOID par)
#elif defined(_LINUX) // Linux
extern void* XcpServerTransmitThread(void* par)
#endif
{
    (void)par;
    int32_t n;

    DBG_PRINT3("Start XCP DAQ thread\n");

    // Transmit loop
    gXcpServer.TransmitThreadRunning = 1;
    for (;;) {

        // Wait for transmit data available, time out at least for required flush cycle
      if (!XcpTlWaitForTransmitData(XCPTL_QUEUE_FLUSH_CYCLE_MS)) XcpTlFlushTransmitBuffer(); // Flush after timerout to keep data visualization going

        // Transmit all completed UDP packets from the transmit queue
        n = XcpTlHandleTransmitQueue();
        if (n<0) {
          DBG_PRINT_ERROR("ERROR: XcpTlHandleTransmitQueue failed!\n");
          break; // error - terminate thread
        }

    } // for (;;)
    gXcpServer.TransmitThreadRunning = 0;

    DBG_PRINT_ERROR("XCP DAQ thread terminated!\n");
    return 0;
}

