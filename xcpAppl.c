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

#include <linux/ptp_clock.h>


// Wall clock updated at every AppXcpTimer
volatile vuint32 gClock = 0; 
volatile vuint64 gClock64 = 0;


/**************************************************************************/
// ApplXcpClock()
// ApplXcpTimerInit()
// Platform and implementation specific functions for the XCP driver
// DAQ clock
/**************************************************************************/

/* Compile with:   -lrt */

static struct timespec gts0;
static struct timespec gtr;

void ApplXcpClockInit( void )
{    
    assert(sizeof(long long) == 8);
    clock_getres(CLOCK_REALTIME, &gtr);
    assert(gtr.tv_sec == 0);
    assert(gtr.tv_nsec == 1);
    clock_gettime(CLOCK_REALTIME, &gts0);

    ApplXcpGetClock64();

#if defined ( XCP_ENABLE_TESTMODE )
    if (gXcpDebugLevel >= 1) {
        printf("clock resolution = %lds,%ldns\n", gtr.tv_sec, gtr.tv_nsec);
        printf("clock now = %lds+%ldns\n", gts0.tv_sec, gts0.tv_nsec);
        printf("clock year = %u\n", 1970 + gts0.tv_sec / 3600 / 24 / 365 );
        printf("gClock64 = %lluus %llxh, gClock = %xh\n", gts0.tv_sec, gts0.tv_nsec, gClock64, gClock64, gClock);
    }
#endif

}

// Free runing clock with 10ns tick
// 1ns with overflow every 4s is critical for CANape measurement start time offset calculation
vuint32 ApplXcpGetClock(void) {

    struct timespec ts; 
    
    clock_gettime(CLOCK_REALTIME, &ts);
    gClock64 = ( ( (vuint64)(ts.tv_sec/*-gts0.tv_sec*/) * 1000000ULL ) + (vuint64)(ts.tv_nsec / 1000) ); // us
    gClock = (vuint32)gClock64;
    return gClock;  
}

vuint64 ApplXcpGetClock64(void) {
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    gClock64 = (((vuint64)(ts.tv_sec/*-gts0.tv_sec*/) * 1000000ULL) + (vuint64)(ts.tv_nsec / 1000)); // us
    gClock = (vuint32)gClock64;
    return gClock64;
}


