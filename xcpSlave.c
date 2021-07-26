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

#include "main.h"
#include "xcpSlave.h"

 
// Parameters
static const uint32_t gFlushCycleMs = 200; // 100ms, send a DTO packet at least every 200ms
static uint32_t gFlushTimer = 0;

// Threads
tXcpThread gDAQThreadHandle;
volatile int gXcpSlaveDAQThreadRunning = 0;
tXcpThread gCMDThreadHandle;
volatile int gXcpSlaveCMDThreadRunning = 0;

// XCP slave init
int xcpSlaveInit() {

    int r;

    // Initialize XCP protocoll driver
    printf("\nInit XCP protocol layer\n");
    printf("  (Version=%u.%u, MAXEV=%u, MAXCTO=%u, MAXDTO=%u, DAQMEM=%u, MAXDAQ=%u, MAXENTRY=%u, MAXENTRYSIZE=%u)\n", XCP_PROTOCOL_LAYER_VERSION >> 8, XCP_PROTOCOL_LAYER_VERSION & 0xFF, XCP_MAX_EVENT, XCPTL_CTO_SIZE, XCPTL_DTO_SIZE, XCP_DAQ_MEM_SIZE, (1 << sizeof(vuint16) * 8) - 1, (1 << sizeof(vuint16) * 8) - 1, (1 << (sizeof(vuint8) * 8)) - 1);
    printf("  (");

    // Print activated XCP protocol options
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST // Enable GET_DAQ_CLOCK_MULTICAST
    printf("DAQ_CLOCK_MULTICAST,");
#endif
#ifdef XCP_DAQ_CLOCK_64BIT  // Use 64 Bit time stamps
    printf("DAQ_CLOCK_64BIT,");
#endif
#ifdef XCP_ENABLE_GRANDMASTER_CLOCK_INFO // Enable emulation of PTP grandmaster clock
    printf("GRANDMASTER_CLOCK_INFO,");
#endif
#ifdef XCP_ENABLE_A2L_NAME // Enable A2L name upload to host
    printf("A2L_NAME,");
#endif
#ifdef XCP_ENABLE_FILE_UPLOAD // Enable A2L upload to host
    printf("FILE_UPLOAD,");
#endif
#ifdef XCP_ENABLE_DAQ_EVENT_LIST // Enable A2L upload to host
    printf("DAQ_EVENT_LIST,");
#endif
#ifdef XCP_ENABLE_DAQ_EVENT_INFO // Enable A2L upload to host
    printf("DAQ_EVENT_INFO,");
#endif
#ifdef XCP_ENABLE_CHECKSUM
    printf("CHECKSUM,");
#endif
    printf(")\n");

    // Initialize XCP protocol layer
    XcpInit();

    // Initialize XCP transport layer
    uint16_t mtu = gOptionJumbo ? XCPTL_SOCKET_JUMBO_MTU_SIZE : XCPTL_SOCKET_MTU_SIZE;
    r = udpTlInit(gOptionSlaveAddr, gOptionSlavePort, mtu);
    if (!r) return 0;

    // Create threads
    create_thread(&gDAQThreadHandle,xcpSlaveDAQThread);
    create_thread(&gCMDThreadHandle, xcpSlaveCMDThread);
    sleepMs(200UL); 

    return 1;
}

int xcpSlaveShutdown() {

    XcpDisconnect();
    cancel_thread(gDAQThreadHandle);
    cancel_thread(gCMDThreadHandle);
    udpTlShutdown();
    return 0;
}



// XCP transport layer thread
// Handle commands
#ifdef _WIN
DWORD WINAPI xcpSlaveCMDThread(LPVOID lpParameter)
#else
extern void* xcpSlaveCMDThread(void* par)
#endif
{
    gXcpSlaveCMDThreadRunning = 1;
    printf("Start XCP CMD thread\n");

    // Server loop
    for (;;) {

        // Handle incoming XCP commands
        if (!udpTlHandleCommands()) { // must be in nonblocking mode in single thread version, blocking mode with timeout in dual thread version
            printf("ERROR: udpTlHandleCommands failed\n"); 
            break; // exit
        }

    } // for (;;)

    gXcpSlaveCMDThreadRunning = 0;
    printf("ERROR: xcpSlaveCMDThread terminated!\n");
    return 0;
}


// XCP DAQ queue thread
// Transmit DAQ data, flush DAQ data
// May terminate on error
#ifdef _WIN
DWORD WINAPI xcpSlaveDAQThread(LPVOID lpParameter)
#else
extern void* xcpSlaveDAQThread(void* par)
#endif
{
    gXcpSlaveDAQThreadRunning = 1;
    printf("Start XCP DAQ thread\n");

    // Server loop
    for (;;) {

        // If DAQ measurement is running
        if (XcpIsDaqRunning()) {

            // Wait for transmit data available, time out at least for required flush cycle
            udpTlWaitForTransmitData(2000/*us*/);

            // Transmit all completed UDP packets from the transmit queue 
            if (!udpTlHandleTransmitQueue()) { // Must be in blocking mode with timeout
                printf("ERROR: udpTlHandleTransmitQueue failed!\n"); // Error
                break; // exit
            }

            // Every gFlushCycle in us time period
            // Cyclic flush of incomplete packets from transmit queue or transmit buffer to keep tool visualizations up to date
            // No priorisation of events implemented, no latency optimizations
            if (gFlushCycleMs > 0 && gClock32 - gFlushTimer > gFlushCycleMs*CLOCK_TICKS_PER_MS) {
                gFlushTimer = gClock32;
                udpTlFlushTransmitQueue();
            }

        } // DAQ
        else {
            sleepMs(100);
        }

    } // for (;;)

    gXcpSlaveDAQThreadRunning = 0;
    printf("ERROR: xcpSlaveDAQThread terminated!\n");
    return 0;
}




