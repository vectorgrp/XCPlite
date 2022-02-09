/*----------------------------------------------------------------------------
| File:
|   xcpServer.c
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
#include "main_cfg.h"
#include "platform.h"
#include "util.h"

#include "xcpTl.h"
#include "xcpLite.h"
#include "xcpServer.h"


#ifdef _WIN
static DWORD WINAPI XcpServerReveiveThread(LPVOID lpParameter);
#else
static void* XcpServerReveiveThread(void* par);
#endif
#ifdef _WIN
static DWORD WINAPI XcpServerTransmitThread(LPVOID lpParameter);
#else
static void* XcpServerTransmitThread(void* par);
#endif


static struct {

    BOOL isInit; 

    uint64_t FlushCycleNs; // Send a DTO packet at least every x ns (typical value 200ms, 0 = off)
    uint64_t FlushCycleTimer;

    // Threads
    tXcpThread DAQThreadHandle;
    volatile int TransmitThreadRunning;
    tXcpThread CMDThreadHandle;
    volatile int ReceiveThreadRunning;

} gXcpServer;


// Check XCP server status
BOOL XcpServerStatus() {
    return gXcpServer.isInit && gXcpServer.TransmitThreadRunning && gXcpServer.ReceiveThreadRunning;
}


// XCP server init
BOOL XcpServerInit( const uint8_t *addr, uint16_t port, BOOL useTCP) {

    int r;

    if (gXcpServer.isInit) return FALSE;

    gXcpServer.TransmitThreadRunning = gXcpServer.ReceiveThreadRunning = 0;
    gXcpServer.FlushCycleTimer = 0;
    gXcpServer.FlushCycleNs = 200*CLOCK_TICKS_PER_MS; // 200 ms flush cycle time

    // Initialize XCP protocol layer
    XcpInit();

    // Initialize XCP transport layer
    r = XcpTlInit(addr, port, useTCP);
    if (!r) return 0;

    // Start XCP protocol layer
    XcpStart();

    // Create threads
    create_thread(&gXcpServer.DAQThreadHandle, XcpServerTransmitThread);
    create_thread(&gXcpServer.CMDThreadHandle, XcpServerReveiveThread);
    
    gXcpServer.isInit = TRUE;
    return TRUE;
}

BOOL XcpServerShutdown() {
    if (gXcpServer.isInit) {
        XcpDisconnect();
        cancel_thread(gXcpServer.DAQThreadHandle);
        cancel_thread(gXcpServer.CMDThreadHandle);
        XcpTlShutdown();
    }
    return TRUE;
}


// XCP server unicast command receive thread
#ifdef _WIN
DWORD WINAPI XcpServerReveiveThread(LPVOID par)
#else
extern void* XcpServerReveiveThread(void* par)
#endif
{
    (void)par;
#ifdef XCP_ENABLE_DEBUG_PRINTS
    printf("Start XCP CMD thread\n");
#endif

    // Receive XCP unicast commands loop
    gXcpServer.ReceiveThreadRunning = 1;
    for (;;) {
        if (!XcpTlHandleCommands()) break; // error -> terminate thread
    }
    gXcpServer.ReceiveThreadRunning = 0;

#ifdef XCP_ENABLE_DEBUG_PRINTS
    printf("ERROR: XcpTlHandleCommands failed!\n");
    printf("ERROR: XcpServerReveiveThread terminated!\n");
#endif
    return 0;
}


// XCP server transmit thread
#ifdef _WIN
DWORD WINAPI XcpServerTransmitThread(LPVOID par)
#else
extern void* XcpServerTransmitThread(void* par)
#endif
{
    (void)par;
#ifdef XCP_ENABLE_DEBUG_PRINTS
    printf("Start XCP DAQ thread\n");
#endif

    // Transmit loop
    gXcpServer.TransmitThreadRunning = 1;
    for (;;) {

        // Wait for transmit data available, time out at least for required flush cycle
        XcpTlWaitForTransmitData(2/*ms*/);

        // Transmit all completed UDP packets from the transmit queue
        if (!XcpTlHandleTransmitQueue()) {
            break; // error - terminate thread
        }

        // Every gFlushCycle in us time period
        // Cyclic flush of incomplete packets from transmit queue or transmit buffer to keep tool visualizations up to date
        // No priorisation of events implemented, no latency optimizations
        uint64_t c = clockGet64();
        if (gXcpServer.FlushCycleNs > 0 &&  c - gXcpServer.FlushCycleTimer > gXcpServer.FlushCycleNs) {
            gXcpServer.FlushCycleTimer = c;
            XcpTlFlushTransmitQueue();
        }

    } // for (;;)
    gXcpServer.TransmitThreadRunning = 0;

#ifdef XCP_ENABLE_DEBUG_PRINTS
    printf("ERROR: XcpTlHandleTransmitQueue failed!\n"); // Error
    printf("ERROR: XcpServerTransmitThread terminated!\n");
#endif
    return 0;
}

