/*----------------------------------------------------------------------------
| File:
|   xcp.cpp
|   XCP callbacks
|
| Project:
|   Demo for XCP on Ethernet (UDP)
|   Linux (Raspberry Pi) Version
 ----------------------------------------------------------------------------*/

// XCP driver
#include "xcpLite.h"

// XCP handler
#include "xcp.h"

// UDP server
#include "udpserver.h"


/**************************************************************************/
// 
// Platform and implementation specific globals for the XCP driver
// 
/**************************************************************************/


vuint8 MEMORY_ROM kXcpStationId[] = "XCPpi"; // Name of the A2L file for auto detection

#ifdef XCP_ENABLE_TESTMODE
  vuint8 gDebugLevel = 1; // Debug output verbosity level 
#endif

pthread_mutex_t gXcpMutex;  // Mutex for multithreaded DAQ



/**************************************************************************/
// ApplXcpTimer()
// ApplXcpTimerInit()
// Platform and implementation specific functions for the XCP driver
// DAQ clock
/**************************************************************************/

/* Compile with:   -lrt */


int ApplXcpTimerInit( void )
{
    struct timespec clock_resolution;
    

    if (gDebugLevel >= 1) {

        clock_getres(CLOCK_REALTIME, &clock_resolution);
        printf("clock resolution %lds,%ldns\n", clock_resolution.tv_sec, clock_resolution.tv_nsec);
        //XcpAssert(clock_resolution.tv_sec == 0);
        //XcpAssert(clock_resolution.tv_nsec == 1);
        
    }
    return 0;
}

// Free runing clock with 1ns tick
unsigned long ApplXcpTimer(void) {

    struct timespec ts; 
    unsigned long long t;
    clock_gettime(CLOCK_REALTIME, &ts);
    t = ((unsigned long long)ts.tv_sec * 1000000000L) + (unsigned long long)ts.tv_nsec;
    return (unsigned long)t;

}



/**************************************************************************/
// ApplXcpSend()
// ApplXcpSendFlush()
// Platform and implementation specific functions for the XCP driver
// Transmit XCP messages
/**************************************************************************/


// Transmit a message
void ApplXcpSend(vuint8 len, MEMORY_ROM BYTEPTR msg) {

    udpServerSendPacket(len, msg);
     
}

// Flush the tranmit buffer
void ApplXcpSendFlush(void) {

    udpServerFlush();
}

