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
#include "platform.h"
#include "clock.h"
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


    uint64_t FlushCycleNs; // Send a DTO packet at least every x ns (typical value 200ms, 0 = off)
    uint64_t FlushCycleTimer;

    // Threads
    tXcpThread DAQThreadHandle;
    volatile int TransmitThreadRunning;
    tXcpThread CMDThreadHandle;
    volatile int ReceiveThreadRunning;

} sXcpServer;


// Check XCP server status
int XcpServerStatus() {
    return !sXcpServer.TransmitThreadRunning || !sXcpServer.ReceiveThreadRunning;
}


// XCP  init
int XcpServerInit( const uint8_t *addr, uint16_t port, BOOL useTCP) {

    int r;

    sXcpServer.TransmitThreadRunning = sXcpServer.ReceiveThreadRunning = 0;
    sXcpServer.FlushCycleTimer = 0;
    sXcpServer.FlushCycleNs = 200*CLOCK_TICKS_PER_MS; // 200 ms flush cycle time

    // Initialize XCP protocol layer
    XcpInit();

    // Initialize XCP transport layer
    r = XcpTlInit(addr, port, useTCP);
    if (!r) return 0;

    // Start XCP protocol layer
    XcpStart();

    // Create threads
    create_thread(&sXcpServer.DAQThreadHandle, XcpServerTransmitThread);
    create_thread(&sXcpServer.CMDThreadHandle, XcpServerReveiveThread);
    return 1;
}

int XcpServerShutdown() {

    XcpDisconnect();
    cancel_thread(sXcpServer.DAQThreadHandle);
    cancel_thread(sXcpServer.CMDThreadHandle);
    XcpTlShutdown();
    return 0;
}


// XCP unicast command receive thread
#ifdef _WIN
DWORD WINAPI XcpServerReveiveThread(LPVOID par)
#else
extern void* XcpServerReveiveThread(void* par)
#endif
{
    (void)par;
#ifdef XCP_ENABLE_TESTMODE
    printf("Start XCP CMD thread\n");
#endif

    // Receive XCP unicast commands loop
    sXcpServer.ReceiveThreadRunning = 1;
    for (;;) {
        if (!XcpTlHandleCommands()) break; // error -> terminate thread
    }
    sXcpServer.ReceiveThreadRunning = 0;

#ifdef XCP_ENABLE_TESTMODE
    printf("ERROR: XcpTlHandleCommands failed!\n");
    printf("ERROR: XcpServerReveiveThread terminated!\n");
#endif
    return 0;
}


// XCP transmit thread
#ifdef _WIN
DWORD WINAPI XcpServerTransmitThread(LPVOID par)
#else
extern void* XcpServerTransmitThread(void* par)
#endif
{
    (void)par;
#ifdef XCP_ENABLE_TESTMODE
    printf("Start XCP DAQ thread\n");
#endif

    // Transmit loop
    sXcpServer.TransmitThreadRunning = 1;
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
        if (sXcpServer.FlushCycleNs > 0 &&  c - sXcpServer.FlushCycleTimer > sXcpServer.FlushCycleNs) {
            sXcpServer.FlushCycleTimer = c;
            XcpTlFlushTransmitQueue();
        }

    } // for (;;)
    sXcpServer.TransmitThreadRunning = 0;

#ifdef XCP_ENABLE_TESTMODE
    printf("ERROR: XcpTlHandleTransmitQueue failed!\n"); // Error
    printf("ERROR: XcpServerTransmitThread terminated!\n");
#endif
    return 0;
}


