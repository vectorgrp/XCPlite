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

#ifndef __MAIN_CFG_H__
#error "Include dependency error!"
#endif

#if defined(XCPTL_ENABLE_UDP) || defined(XCPTL_ENABLE_TCP)

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
    tXcpThread TransmitThreadHandle;
    volatile BOOL TransmitThreadRunning;
    tXcpThread ReceiveThreadHandle;
    volatile BOOL ReceiveThreadRunning;

} gXcpServer;


// Check XCP server status
BOOL XcpEthServerStatus() {
    return gXcpServer.isInit && gXcpServer.TransmitThreadRunning && gXcpServer.ReceiveThreadRunning;
}


// XCP server init
BOOL XcpEthServerInit(const uint8_t* addr, uint16_t port, BOOL useTCP)
{
    int r = 0;

    if (gXcpServer.isInit) return FALSE;
    DBG_PRINT3("\nStart XCP server\n");

    // Init network sockets
    if (!socketStartup()) return FALSE;
    
    gXcpServer.TransmitThreadRunning = FALSE;
    gXcpServer.ReceiveThreadRunning = FALSE;

    // Initialize XCP protocol layer if not already done
    XcpInit();

    // Initialize XCP transport layer
    r = XcpEthTlInit(addr, port, useTCP, TRUE /*blocking rx*/);
    if (!r) return 0;

    // Start XCP protocol layer
    XcpStart();

    // Create threads
    create_thread(&gXcpServer.TransmitThreadHandle, XcpServerTransmitThread);
    create_thread(&gXcpServer.ReceiveThreadHandle, XcpServerReceiveThread);

    gXcpServer.isInit = TRUE;
    return TRUE;
}

BOOL XcpEthServerShutdown() {

#ifdef XCP_SERVER_FORCEFULL_TERMINATION
    // Forcefull termination
    if (gXcpServer.isInit) {
        DBG_PRINT3("Disconnect, cancel threads and shutdown XCP!\n");
        XcpDisconnect();
        cancel_thread(gXcpServer.ReceiveThreadHandle);
        cancel_thread(gXcpServer.TransmitThreadHandle);
        XcpEthTlShutdown();
        gXcpServer.isInit = FALSE;
        socketCleanup();
        XcpReset();
    }
#else
    // Gracefull termination
    if (gXcpServer.isInit) {
        XcpDisconnect();
        gXcpServer.ReceiveThreadRunning = FALSE;
        gXcpServer.TransmitThreadRunning = FALSE;
        XcpEthTlShutdown();
        join_thread(gXcpServer.ReceiveThreadHandle);
        join_thread(gXcpServer.TransmitThreadHandle);
        gXcpServer.isInit = FALSE;
        socketCleanup();
        XcpReset();
    }
#endif
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
    gXcpServer.ReceiveThreadRunning = TRUE;
    while (gXcpServer.ReceiveThreadRunning) { 
      if (!XcpEthTlHandleCommands(XCPTL_TIMEOUT_INFINITE)) { // Timeout Blocking
        DBG_PRINT_ERROR("ERROR: XcpTlHandleCommands failed!\n");
        break; // error -> terminate thread
      }
    }
    gXcpServer.ReceiveThreadRunning = FALSE;

    DBG_PRINT3("XCP receive thread terminated!\n");
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
    gXcpServer.TransmitThreadRunning = TRUE;
    while (gXcpServer.TransmitThreadRunning) {

        // Wait for transmit data available, time out at least for required flush cycle
      if (!XcpTlWaitForTransmitData(XCPTL_QUEUE_FLUSH_CYCLE_MS)) XcpTlFlushTransmitBuffer(); // Flush after timerout to keep data visualization going

        // Transmit all completed UDP packets from the transmit queue
        n = XcpTlHandleTransmitQueue();
        if (n<0) {
          DBG_PRINT_ERROR("ERROR: XcpTlHandleTransmitQueue failed!\n");
          break; // error - terminate thread
        }

    } // for (;;)
    gXcpServer.TransmitThreadRunning = FALSE;

    DBG_PRINT3("XCP transmit thread terminated!\n");
    return 0;
}


#endif