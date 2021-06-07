/*----------------------------------------------------------------------------
| File:
|   xcpSlave.c
|
| Description:
|   XCP on UDP Slave
 ----------------------------------------------------------------------------*/


#include "main.h"
#include "xcpSlave.h"

 
// Parameters
volatile unsigned int gFlushCycleMs = 100; // 100ms, send a DTO packet at least every 100ms
static unsigned int gFlushTimer = 0;

volatile int gXcpSlaveCMDThreadRunning = 0;
volatile int gXcpSlaveDAQThreadRunning = 0;


// XCP transport layer init
int xcpSlaveInit(unsigned char slaveMac[6], unsigned char slaveAddr[4], unsigned short slavePort, unsigned int MTU ) {

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
    printf(")\n");
    
    XcpInit();

    // Initialize XCP transport layer
    printf("Init UDP transport layer\n  (MTU = %u, DTO_QUEUE_SIZE = %u )\n", MTU, XCPTL_DTO_QUEUE_SIZE);
    return udpTlInit(slaveMac,slaveAddr,slavePort, MTU);
}

// Restart the XCP transport layer
int xcpSlaveRestart(void) {
#ifdef _LINUX
    return xcpSlaveInit(NULL, (unsigned char*)&gXcpTl.SlaveAddr.addr.sin_addr.s_addr, gXcpTl.SlaveAddr.addr.sin_port, 0);
#else
    return xcpSlaveInit(gXcpTl.SlaveAddr.addrXl.sin_mac, &gXcpTl.SlaveAddr.addr.sin_addr.S_un, gXcpTl.SlaveAddr.addr.sin_port, gXcpTl.SlaveMTU);
#endif
}


// XCP transport layer thread
// Handle commands
void* xcpSlaveCMDThread(void* par) {

    gXcpSlaveCMDThreadRunning = 1;
    printf("Start XCP server\n");

    // Server loop
    for (;;) {

        // Handle XCP commands
        udpTlWaitForReceiveEvent(1000/*us*/);
        if (!udpTlHandleXCPCommands()) {
            printf("ERROR: udpTlHandleXCPCommands failed, shutdown and restart\n"); // Error
            xcpSlaveShutdown();
            xcpSlaveRestart();
        }

        // Handle DAQ queue
#ifdef _WIN
        if (!udpTlHandleTransmitQueue()) {
            printf("ERROR: udpTlHandleTransmitQueue failed, DAQ stopped, XCP disconnect!\n"); // Error
            XcpDisconnect();
        }
#endif

        // Check DAQ thread is ok
        if (!gXcpSlaveDAQThreadRunning) {
            break; // exit
        }

    } // for (;;)

    gXcpSlaveCMDThreadRunning = 0;
    printf("Error: xcpSlaveCMDThread terminated!\n");
    return 0;
}

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

            // Transmit all completed UDP packets from the transmit queue 
#ifdef _LINUX
            int result = udpTlHandleTransmitQueue();
            if (result == 0) break; // Error
            udpTlWaitForTransmitData(10000/*us*/);

            // Every gFlushCycle in us time period
            // Cyclic flush of incomplete packets from transmit queue or transmit buffer to keep tool visualizations up to date
            // No priorisation of events implemented, no latency optimizations
            vuint32 clock = getClock32();
            if (gFlushCycleMs > 0 && clock - gFlushTimer > gFlushCycleMs* XCP_TIMESTAMP_TICKS_MS) {
                gFlushTimer = clock;
                udpTlFlushTransmitQueue();
            }
#endif
#ifdef _WIN
            Sleep(100);
#endif


        } // DAQ
        else {
            sleepNs(10000000/*10ms*/);
        }

    } // for (;;)

    gXcpSlaveDAQThreadRunning = 0;
    printf("Error: xcpSlaveDAQThread terminated!\n");
    return 0;
}


int xcpSlaveShutdown(void) {

    XcpDisconnect();
    udpTlShutdown();
    return 0;
}

