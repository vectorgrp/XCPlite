/*----------------------------------------------------------------------------
| File:
|   xcpSlave.c
|
| Description:
|   XCP on UDP Slave
|   SHows how to integrate the XCP driver in an application
|   Creates threads for cmd handling and data transmission
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
-----------------------------------------------------------------------------*/

#include "platform.h"
#include "util.h"
#include "clock.h"
#include "xcpTl.h"
#include "xcpLite.h"
#include "xcpSlave.h"


static struct {

    uint32_t FlushCycleMs; // 100ms, send a DTO packet at least every 200ms
    uint32_t FlushTimer;

    // Threads
    tXcpThread DAQThreadHandle;
    volatile int DAQThreadRunning;
    tXcpThread CMDThreadHandle;
    volatile int CMDThreadRunning;

} gXcpSlave;


// Check XCP slave status
int XcpSlaveStatus() {
    return !gXcpSlave.DAQThreadRunning || !gXcpSlave.CMDThreadRunning;
}


// XCP slave init
int XcpSlaveInit( uint8_t *addr, uint16_t port, uint16_t mtu, uint16_t flushCycleMs) {

    int r;

    gXcpSlave.DAQThreadRunning = gXcpSlave.CMDThreadRunning = 0;
    gXcpSlave.FlushTimer = 0;
    gXcpSlave.FlushCycleMs = flushCycleMs;

    // Initialize XCP protocoll driver
    printf("\nInit XCP protocol layer\n");
    printf("  (Version=%u.%u, MAXEV=%u, MAXCTO=%u, MAXDTO=%u, DAQMEM=%u, MAXDAQ=%u, MAXENTRY=%u, MAXENTRYSIZE=%u)\n", XCP_PROTOCOL_LAYER_VERSION >> 8, XCP_PROTOCOL_LAYER_VERSION & 0xFF, XCP_MAX_EVENT, XCPTL_CTO_SIZE, XCPTL_DTO_SIZE, XCP_DAQ_MEM_SIZE, (1 << sizeof(uint16_t) * 8) - 1, (1 << sizeof(uint16_t) * 8) - 1, (1 << (sizeof(uint8_t) * 8)) - 1);
    printf("  (");

    // Print activated XCP protocol options
#ifdef XCP_ENABLE_CDC // Enable CDC
    printf("CDC,");
#endif
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST // Enable GET_DAQ_CLOCK_MULTICAST
    printf("DAQ_CLK_MULTICAST,");
#endif
#ifdef XCP_DAQ_CLOCK_64BIT  // Use 64 Bit time stamps
    printf("DAQ_CLK_64BIT,");
#endif
#ifdef XCP_ENABLE_GRANDMASTER_CLOCK_INFO // Enable emulation of PTP grandmaster clock
    printf("GM_CLK_INFO,");
#endif
#ifdef XCP_ENABLE_A2L_NAME // Enable A2L name upload to host
    printf("A2L_NAME,");
#endif
#ifdef XCP_ENABLE_FILE_UPLOAD // Enable A2L upload to host
    printf("FILE_UPLOAD,");
#endif
#ifdef XCP_ENABLE_DAQ_EVENT_LIST // Enable A2L upload to host
    printf("DAQ_EVT_LIST,");
#endif
#ifdef XCP_ENABLE_DAQ_EVENT_INFO // Enable A2L upload to host
    printf("DAQ_EVT_INFO,");
#endif
#ifdef XCP_ENABLE_CHECKSUM
    printf("CHECKSUM,");
#endif
#ifdef XCP_ENABLE_INTERLEAVED
    printf("INTERLEAVED,");
#endif
    printf(")\n");

    // Initialize XCP protocol layer
    XcpInit();

    // Initialize XCP transport layer
    r = XcpTlInit(addr, port, mtu);
    if (!r) return 0;

    // Start XCP protocol layer
    printf("Start XCP protocol layer\n");
    XcpStart();

    create_thread(&gXcpSlave.DAQThreadHandle, XcpSlaveDAQThread);
    create_thread(&gXcpSlave.CMDThreadHandle, XcpSlaveCMDThread);
    return 1;
}

int XcpSlaveShutdown() {

    XcpDisconnect();
    cancel_thread(gXcpSlave.DAQThreadHandle);
    cancel_thread(gXcpSlave.CMDThreadHandle);
    XcpTlShutdown();
    return 0;
}



// XCP transport layer thread
// Handle commands
#ifdef _WIN
DWORD WINAPI XcpSlaveCMDThread(LPVOID lpParameter)
#else
extern void* XcpSlaveCMDThread(void* par)
#endif
{
    gXcpSlave.CMDThreadRunning = 1;
    printf("Start XCP CMD thread\n");

    // Server loop
    for (;;) {

        // Handle incoming XCP commands
        if (!XcpTlHandleCommands()) { // must be in nonblocking mode in single thread version, blocking mode with timeout in dual thread version
            printf("ERROR: XcpTlHandleCommands failed\n"); 
            break; // exit
        }

    } // for (;;)

    gXcpSlave.CMDThreadRunning = 0;
    printf("ERROR: XcpSlaveCMDThread terminated!\n");
    return 0;
}


// XCP DAQ queue thread
// Transmit DAQ data, flush DAQ data
// May terminate on error
#ifdef _WIN
DWORD WINAPI XcpSlaveDAQThread(LPVOID lpParameter)
#else
extern void* XcpSlaveDAQThread(void* par)
#endif
{
    gXcpSlave.DAQThreadRunning = 1;
    printf("Start XCP DAQ thread\n");

    // Server loop
    for (;;) {

        // If DAQ measurement is running
        if (XcpIsDaqRunning()) {

            // Wait for transmit data available, time out at least for required flush cycle
            XcpTlWaitForTransmitData(2000/*us*/);

            // Transmit all completed UDP packets from the transmit queue 
            if (!XcpTlHandleTransmitQueue()) { // Must be in blocking mode with timeout
                printf("ERROR: XcpTlHandleTransmitQueue failed!\n"); // Error
                break; // exit
            }

            // Every gFlushCycle in us time period
            // Cyclic flush of incomplete packets from transmit queue or transmit buffer to keep tool visualizations up to date
            // No priorisation of events implemented, no latency optimizations
            if (gXcpSlave.FlushCycleMs > 0 && clockGetLast32() - gXcpSlave.FlushTimer > gXcpSlave.FlushCycleMs*CLOCK_TICKS_PER_MS) {
                gXcpSlave.FlushTimer = clockGetLast32();
                XcpTlFlushTransmitQueue();
            }

        } // DAQ
        else {
            sleepMs(100);
        }

    } // for (;;)

    gXcpSlave.DAQThreadRunning = 0;
    printf("ERROR: XcpSlaveDAQThread terminated!\n");
    return 0;
}

