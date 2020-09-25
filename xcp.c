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



// Globals
V_MEMROM0 vuint8 MEMORY_ROM kXcpStationId[] = "XCPpi"; // Name of the A2L file for auto detection
pthread_mutex_t gXcpMutex;  // Mutex for multithreaded DAQ
vuint8 gDebugLevel = 1; // Debug output verbosity level 



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
// ApplXcpGetPointer() 
// ApplXcpGetSeed()
// ApplXcpUnlock()
// Platform and implementation specific functions for the XCP driver
/**************************************************************************/


// Transmit a message
void ApplXcpSend(vuint8 len, MEMORY_ROM BYTEPTR msg) {

    udpServerSendPacket(len, msg);
     
}




