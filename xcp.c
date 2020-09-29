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
// ApplXcpTimer()
// ApplXcpTimerInit()
// Platform and implementation specific functions for the XCP driver
// DAQ clock
/**************************************************************************/

/* Compile with:   -lrt */


int ApplXcpTimerInit( void )
{
    struct timespec clock_resolution;
    clock_getres(CLOCK_REALTIME, &clock_resolution);

#if defined ( XCP_ENABLE_TESTMODE )
    if (gDebugLevel >= 1) {
        printf("clock resolution %lds,%ldns\n", clock_resolution.tv_sec, clock_resolution.tv_nsec);
        //XcpAssert(clock_resolution.tv_sec == 0);
        //XcpAssert(clock_resolution.tv_nsec == 1);
        
    }
#endif
    return 0;
}

// Free runing clock with 1ns tick
unsigned long ApplXcpTimer(void) {

    struct timespec ts; 
    unsigned long long t;
    clock_gettime(CLOCK_REALTIME, &ts);
    t = ((unsigned long long)ts.tv_sec * 1000000000LL) + (unsigned long long)ts.tv_nsec;
    return (unsigned long)t;

}



/**************************************************************************/
// ApplXcpSend()
// ApplXcpSendFlush()
// Platform and implementation specific functions for the XCP driver
// Transmit XCP messages
/**************************************************************************/


// Transmit a CRM (Command Responce) message
void ApplXcpSendCrm(vuint8 len, MEMORY_ROM BYTEPTR msg) {

    udpServerSendPacket(len, msg);
    udpServerFlush();
}

// Get buffer space for a DTO message
vuint8 * ApplXcpGetDtoBuffer(vuint8 len) {

    return udpServerGetPacketBuffer(len);
}

// Commit a DTO message
void ApplXcpCommitDtoBuffer(vuint8 *buf) {

    udpServerCommitPacketBuffer(buf);
}



