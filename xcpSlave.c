/*----------------------------------------------------------------------------
| File:
|   xcpSlave.c
|
| Description:
|   XCP on UDP Slave
|
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
-----------------------------------------------------------------------------*/


#include "main.h"
#include "xcpSlave.h"

 
// Parameters
volatile unsigned int gFlushCycleMs = 200; // 100ms, send a DTO packet at least every 200ms
static unsigned int gFlushTimer = 0;


#ifndef XCPSIM_SINGLE_THREAD_SLAVE
tXcpThread gDAQThreadHandle;
volatile int gXcpSlaveDAQThreadRunning = 0;
#endif



tXcpThread gCMDThreadHandle;
volatile int gXcpSlaveCMDThreadRunning = 0;




// XCP slave init
int xcpSlaveInit(unsigned char *slaveAddr, uint16_t slavePort, unsigned int jumbo ) {

    int r;

    // Initialize XCP protocoll driver
    printf("\nInit XCP protocol layer\n");
    printf("  (Version=%u.%u, MAXEV=%u, MAXCTO=%u, MAXDTO=%u, DAQMEM=%u, MAXDAQ=%u, MAXENTRY=%u, MAXENTRYSIZE=%u)\n", XCP_PROTOCOL_LAYER_VERSION >> 8, XCP_PROTOCOL_LAYER_VERSION & 0xFF, XCP_MAX_EVENT, XCPTL_CTO_SIZE, XCPTL_DTO_SIZE, XCP_DAQ_MEM_SIZE, (1 << sizeof(vuint16) * 8) - 1, (1 << sizeof(vuint16) * 8) - 1, (1 << (sizeof(vuint8) * 8)) - 1);
    printf("  (");

    // Print activated protocol options

#ifdef XCP_ENABLE_MULTICAST // Enable GET_DAQ_CLOCK_MULTICAST
    printf("MULTICAST,");
#endif
#ifdef XCP_DAQ_CLOCK_64BIT  // Use 64 Bit time stamps
    printf("DAQ_CLOCK_64BIT,");
#endif
#ifdef XCP_ENABLE_PTP_GRANDMASTER // Enable emulation of PTP grandmaster clock
printf("PTP_GRANDMASTER,");
#endif
#ifdef XCP_ENABLE_PACKED_MODE
    printf("PACKED_MODE,");
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
    uint16_t mtu = jumbo ? XCPTL_SOCKET_JUMBO_MTU_SIZE : XCPTL_SOCKET_MTU_SIZE;
    r = udpTlInit(slaveAddr,slavePort,mtu);
    if (!r) return 0;

     // Create threads
#ifndef XCPSIM_SINGLE_THREAD_SLAVE
    create_thread(&gDAQThreadHandle,xcpSlaveDAQThread);
#endif
    create_thread(&gCMDThreadHandle, xcpSlaveCMDThread);
    sleepMs(200UL); 

    return 1;
}

int xcpSlaveShutdown() {

    XcpDisconnect();
#ifndef XCPSIM_SINGLE_THREAD_SLAVE
    cancel_thread(gDAQThreadHandle);
#endif
    cancel_thread(gCMDThreadHandle);

    udpTlShutdown();
    return 0;
}



// Restart the XCP transport layer
int xcpSlaveRestart() {
    xcpSlaveShutdown();
    return xcpSlaveInit((unsigned char*)&gXcpTl.SlaveAddr.addr.sin_addr, gXcpTl.SlaveAddr.addr.sin_port, gXcpTl.SlaveMTU);
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

#ifndef XCPSIM_SINGLE_THREAD_SLAVE // Multi thread mode

#ifdef XCPSIM_ENABLE_XLAPI_V3
        // XL-API V3 has no blocking mode, always use the registered event for receive
        if (gOptionUseXLAPI) udpTlWaitForReceiveEvent(10000/*us*/);
#endif

        // Check the DAQ queue thread is running, break and fail if not, should not happen
        if (!gXcpSlaveDAQThreadRunning) {
            break; // exit
        }

#endif

        // Handle incoming XCP commands
        if (!udpTlHandleCommands()) { // must be in nonblocking mode in single thread version, blocking mode with timeout in dual thread version
            printf("ERROR: udpTlHandleCommands failed\n"); 
            break; // exit
        }

#ifdef XCPSIM_SINGLE_THREAD_SLAVE // Single thread mode, handle DAQ queue here

        // Handle DAQ queue
        if (!udpTlHandleTransmitQueue()) { // must be in non blocking mode with timeout
            printf("ERROR: udpTlHandleTransmitQueue failed, DAQ stopped, XCP disconnect!\n"); // Error
            XcpDisconnect();
        }

        // Every gFlushCycle in us time period
        // Cyclic flush of incomplete packets from transmit queue or transmit buffer to keep tool visualizations up to date
        // No priorisation of events implemented, no latency optimizations
        // If DAQ measurement is running
        if (XcpIsDaqRunning()) {
            if (gFlushCycleMs > 0 && gLocalClock32 - gFlushTimer > gFlushCycleMs* XCP_TIMESTAMP_TICKS_MS) {
                gFlushTimer = gLocalClock32;
                udpTlFlushTransmitQueue();
            }
        }

        udpTlWaitForReceiveEvent(10000/*us*/);

#endif

    } // for (;;)

    gXcpSlaveCMDThreadRunning = 0;
    printf("ERROR: xcpSlaveCMDThread terminated!\n");
    return 0;
}


#ifndef XCPSIM_SINGLE_THREAD_SLAVE

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
            udpTlWaitForTransmitData(1000/*us*/);

            // Transmit all completed UDP packets from the transmit queue 
            if (!udpTlHandleTransmitQueue()) { // Must be in blocking mode with timeout
                printf("ERROR: udpTlHandleTransmitQueue failed, DAQ stopped, XCP disconnect!\n"); // Error
                XcpDisconnect();
            }

            // Every gFlushCycle in us time period
            // Cyclic flush of incomplete packets from transmit queue or transmit buffer to keep tool visualizations up to date
            // No priorisation of events implemented, no latency optimizations
            if (gFlushCycleMs > 0 && gLocalClock32 - gFlushTimer > gFlushCycleMs* CLOCK_TICKS_PER_MS) {
                gFlushTimer = gLocalClock32;
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

#endif


