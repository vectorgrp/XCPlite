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

volatile int gXcpSlaveCMDThreadRunning = 0;
#ifndef XCPSIM_SINGLE_THREAD_SLAVE
volatile int gXcpSlaveDAQThreadRunning = 0;
#endif

// XCP transport layer init
int xcpSlaveInit(unsigned char *slaveMac, unsigned char *slaveAddr, uint16_t slavePort, unsigned int jumbo ) {

    // Initialize XCP protocoll driver
    printf("Init XCP protocol layer\n");
    printf("  (Version=%u.%u, MAXEV=%u, MAXCTO=%u, MAXDTO=%u, DAQMEM=%u, MAXDAQ=%u, MAXENTRY=%u, MAXENTRYSIZE=%u)\n", XCP_PROTOCOL_LAYER_VERSION >> 8, (XCP_PROTOCOL_LAYER_VERSION >> 4) & 0xF, XCP_MAX_EVENT, XCPTL_CTO_SIZE, XCPTL_DTO_SIZE, XCP_DAQ_MEM_SIZE, (1 << sizeof(vuint16) * 8) - 1, (1 << sizeof(vuint16) * 8) - 1, (1 << (sizeof(vuint8) * 8)) - 1);
    printf("  (");

    // Print activated protocol options
#ifdef XCP_ENABLE_CHECKSUM
    printf("ENABLE_CHECKSUM,");
#endif
#ifdef XCP_ENABLE_MULTICAST // Enable GET_DAQ_CLOCK_MULTICAST
    printf("ENABLE_MULTICAST,");
#endif
#ifdef XCP_DAQ_CLOCK_64BIT  // Use 64 Bit time stamps
    printf("DAQ_CLOCK_64BIT,");
#endif
#ifdef XCP_ENABLE_PTP // Enable emulation of PTP synchronized slave DAQ time stamps
    printf("ENABLE_PTP,");
#endif
#ifdef XCP_ENABLE_PACKED_MODE
    printf("ENABLE_PACKED_MODE,");
#endif
#ifdef XCP_ENABLE_A2L_NAME // Enable A2L name upload to host
    printf("XCP_ENABLE_A2L_NAME,");
#endif
#ifdef XCP_ENABLE_A2L_UPLOAD // Enable A2L upload to host
    printf("XCP_ENABLE_A2L_UPLOAD,");
#endif
#ifdef XCP_ENABLE_DAQ_EVENT_LIST // Enable A2L upload to host
    printf("XCP_ENABLE_DAQ_EVENT_LIST,");
#endif
#ifdef XCP_ENABLE_DAQ_EVENT_INFO // Enable A2L upload to host
    printf("XCP_ENABLE_DAQ_EVENT_INFO,");
#endif
    printf(")\n");
    
    XcpInit();

    // Initialize XCP transport layer
    unsigned int mtu = jumbo ? XCPTL_SOCKET_JUMBO_MTU_SIZE : XCPTL_SOCKET_MTU_SIZE;
    return udpTlInit(slaveMac,slaveAddr,slavePort,mtu);
}

// Restart the XCP transport layer
int xcpSlaveRestart(void) {
#ifdef _LINUX
    return xcpSlaveInit(NULL, (unsigned char*)&gXcpTl.SlaveAddr.addr.sin_addr.s_addr, gXcpTl.SlaveAddr.addr.sin_port, 0);
#else
#ifdef XCPSIM_ENABLE_XLAPI_V3
    return xcpSlaveInit(gXcpTl.SlaveAddr.addrXl.sin_mac, (unsigned char*)&gXcpTl.SlaveAddr.addr.sin_addr, gXcpTl.SlaveAddr.addr.sin_port, gXcpTl.SlaveMTU);
#else
    return xcpSlaveInit(NULL, (unsigned char*)&gXcpTl.SlaveAddr.addr.sin_addr, gXcpTl.SlaveAddr.addr.sin_port, gXcpTl.SlaveMTU);
#endif
#endif
}


// XCP transport layer thread
// Handle commands
void* xcpSlaveCMDThread(void* par) {

    gXcpSlaveCMDThreadRunning = 1;
    printf("Start XCP server thread\n");

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
        if (!udpTlHandleXCPCommands()) { // must be in nonblocking mode in single thread version, blocking mode with timeout in dual thread version
            printf("ERROR: udpTlHandleXCPCommands failed, shutdown and restart\n"); // Error
            xcpSlaveShutdown();
            xcpSlaveRestart();
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
            if (gFlushCycleMs > 0 && gClock32 - gFlushTimer > gFlushCycleMs* XCP_TIMESTAMP_TICKS_MS) {
                gFlushTimer = gClock32;
                udpTlFlushTransmitQueue();
            }
        }

        udpTlWaitForReceiveEvent(10000/*us*/);

#endif

    } // for (;;)

    gXcpSlaveCMDThreadRunning = 0;
    printf("Error: xcpSlaveCMDThread terminated!\n");
    return 0;
}


#ifndef XCPSIM_SINGLE_THREAD_SLAVE

// XCP DAQ queue thread
// Transmit DAQ data, flush DAQ data
// May terminate on error
void* xcpSlaveDAQThread(void* par) {

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
            if (gFlushCycleMs > 0 && gClock32 - gFlushTimer > gFlushCycleMs * CLOCK_TICKS_PER_MS) {
                gFlushTimer = gClock32;
                udpTlFlushTransmitQueue();
            }

        } // DAQ
        else {
            sleepNs(100000000/*100ms*/);
        }

    } // for (;;)

    gXcpSlaveDAQThreadRunning = 0;
    printf("Error: xcpSlaveDAQThread terminated!\n");
    return 0;
}

#endif

int xcpSlaveShutdown(void) {

    XcpDisconnect();
    udpTlShutdown();
    return 0;
}

