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

#include "xcpEthServer.h"

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIu64
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t
#include <stdio.h>    // for printf

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#include "main_cfg.h"  // for OPTION_xxx
#include "platform.h"  // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex
#include "xcp.h"       // for CRC_XXX
#include "xcpEthTl.h"  // for tXcpCtoMessage, xcpTlXxxx, xcpEthTlxxx
#include "xcpLite.h"   // for tXcpDaqLists, XcpXxx, ApplXcpXxx, ...
#include "xcpQueue.h"
#include "xcptl_cfg.h" // for XCPTL_xxx

#if !defined(_WIN) && !defined(_LINUX) && !defined(_MACOS)
#error "Please define platform _WIN, _MACOS or _LINUX"
#endif

#if defined(_WIN) // Windows
static DWORD WINAPI XcpServerReceiveThread(LPVOID lpParameter);
#elif defined(_LINUX) // Linux
static void *XcpServerReceiveThread(void *par);
#endif
#if defined(_WIN) // Windows
static DWORD WINAPI XcpServerTransmitThread(LPVOID lpParameter);
#elif defined(_LINUX) // Linux
static void *XcpServerTransmitThread(void *par);
#endif

static struct {

    bool isInit;

    // Threads
    THREAD TransmitThreadHandle;
    volatile bool TransmitThreadRunning;
    THREAD ReceiveThreadHandle;
    volatile bool ReceiveThreadRunning;

    // Queue
    tQueueHandle TransmitQueue;

} gXcpServer;

// XCP server status
bool XcpEthServerStatus(void) { return gXcpServer.isInit && gXcpServer.TransmitThreadRunning && gXcpServer.ReceiveThreadRunning; }

// XCP server information
void XcpEthServerGetInfo(bool *isTcp, uint8_t *mac, uint8_t *addr, uint16_t *port) { XcpEthTlGetInfo(isTcp, mac, addr, port); }

// XCP server init
bool XcpEthServerInit(const uint8_t *addr, uint16_t port, bool useTCP, uint32_t queueSize) {

    // Check that the XCP singleton has been explicitly initialized
    if (!XcpIsInitialized()) {
        DBG_PRINT_ERROR("XCP not initialized!\n");
        return false;
    }

    // Check if already initialized and running
    if (gXcpServer.isInit) {
        DBG_PRINT_WARNING("XCP server already running!\n");
        return false;
    }

    DBG_PRINT3("Start XCP server\n");
    DBG_PRINTF3("  Queue size = %u\n", queueSize);

    gXcpServer.TransmitThreadRunning = false;
    gXcpServer.ReceiveThreadRunning = false;

    // Init network sockets
    if (!socketStartup())
        return false;

    // Create queue
    assert(queueSize > 0);

    gXcpServer.TransmitQueue = QueueInit(queueSize);
    if (gXcpServer.TransmitQueue == NULL)
        return false;

    // Initialize XCP transport layer
    if (!XcpEthTlInit(addr, port, useTCP, true /*blocking rx*/, gXcpServer.TransmitQueue))
        return false;

    // Start XCP protocol layer
    XcpStart(gXcpServer.TransmitQueue, false);

    // Create threads
    create_thread(&gXcpServer.TransmitThreadHandle, XcpServerTransmitThread);
    create_thread(&gXcpServer.ReceiveThreadHandle, XcpServerReceiveThread);

    gXcpServer.isInit = true;
    return true;
}

// XCP server shutdown
bool XcpEthServerShutdown(void) {

#ifdef OPTION_SERVER_FORCEFULL_TERMINATION
    // Forcefull termination
    if (gXcpServer.isInit) {
        DBG_PRINT3("Disconnect, cancel threads and shutdown XCP!\n");
        XcpDisconnect();
        cancel_thread(gXcpServer.ReceiveThreadHandle);
        cancel_thread(gXcpServer.TransmitThreadHandle);
        XcpEthTlShutdown();
        gXcpServer.isInit = false;
        socketCleanup();
        XcpReset();
    }
#else
    // Gracefull termination
    if (gXcpServer.isInit) {
        XcpDisconnect();
        gXcpServer.ReceiveThreadRunning = false;
        gXcpServer.TransmitThreadRunning = false;
        XcpEthTlShutdown();
        join_thread(gXcpServer.ReceiveThreadHandle);
        join_thread(gXcpServer.TransmitThreadHandle);
        gXcpServer.isInit = false;
        socketCleanup();
        XcpReset();
    }
#endif

    QueueDeinit(gXcpServer.TransmitQueue);

    return true;
}

// XCP server unicast command receive thread
#if defined(_WIN) // Windows
DWORD WINAPI XcpServerReceiveThread(LPVOID par)
#elif defined(_LINUX) // Linux
extern void *XcpServerReceiveThread(void *par)
#endif
{
    (void)par;
    DBG_PRINT3("Start XCP CMD thread\n");

    // Receive XCP unicast commands loop
    gXcpServer.ReceiveThreadRunning = true;
    while (gXcpServer.ReceiveThreadRunning) {
        if (!XcpEthTlHandleCommands(XCPTL_TIMEOUT_INFINITE)) { // Timeout Blocking
            DBG_PRINT_ERROR("XcpEthTlHandleCommands failed!\n");
            break; // error -> terminate thread
        }

        XcpBackgroundTasks(); // Handle background tasks, e.g. pending calibration updates
    }
    gXcpServer.ReceiveThreadRunning = false;

    DBG_PRINT3("XCP receive thread terminated!\n");
    return 0;
}

// XCP server transmit thread
#if defined(_WIN) // Windows
DWORD WINAPI XcpServerTransmitThread(LPVOID par)
#elif defined(_LINUX) // Linux
extern void *XcpServerTransmitThread(void *par)
#endif
{
    (void)par;
    int32_t n;

    DBG_PRINT3("Start XCP DAQ thread\n");

    // Transmit loop
    gXcpServer.TransmitThreadRunning = true;
    while (gXcpServer.TransmitThreadRunning) {

        // Transmit all committed messages from the transmit queue
        n = XcpTlHandleTransmitQueue();
        if (n < 0) {
            DBG_PRINT_ERROR("XcpTlHandleTransmitQueue failed!\n");
            break; // error - terminate thread
        }

    } // for (;;)
    gXcpServer.TransmitThreadRunning = false;

    DBG_PRINT3("XCP transmit thread terminated!\n");
    return 0;
}
