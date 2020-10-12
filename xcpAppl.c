/*----------------------------------------------------------------------------
| File:
|   xcpAppl.cpp
|
| Description:
|   XCP protocol layer application callbacks
|   DAQ clock, all other callbacks are implemented as macros
|   Demo for XCP on Ethernet (UDP)
|   Linux (Raspberry Pi) Version
 ----------------------------------------------------------------------------*/

#include "xcpAppl.h"


// Wall clock updated at every AppXcpTimer
volatile vuint32 gTimer = 0;


/**************************************************************************/
// ApplXcpTimer()
// ApplXcpTimerInit()
// Platform and implementation specific functions for the XCP driver
// DAQ clock
/**************************************************************************/

/* Compile with:   -lrt */


void ApplXcpTimerInit( void )
{
    struct timespec clock_resolution;
    clock_getres(CLOCK_REALTIME, &clock_resolution);

#if defined ( XCP_ENABLE_TESTMODE )
    if (gXcpDebugLevel >= 1) {
        printf("clock resolution %lds,%ldns\n", clock_resolution.tv_sec, clock_resolution.tv_nsec);
        //XcpAssert(clock_resolution.tv_sec == 0);
        //XcpAssert(clock_resolution.tv_nsec == 1);
        
    }
#endif
    assert(sizeof(long long) == 8);
}

// Free runing clock with 10ns tick
// 1ns with overflow every 4s is critical for CANape measurement start time offset calculation
vuint32 ApplXcpTimer(void) {

    struct timespec ts; 
    
    clock_gettime(CLOCK_REALTIME, &ts);
    gTimer = (vuint32)((((unsigned long long)ts.tv_sec * 1000000000L) + (unsigned long long)(ts.tv_nsec))/1000);
    return gTimer;  
}



