/*----------------------------------------------------------------------------
| File:
|   util.c
|
| Description:
|   Some helper functions
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "util.h"


/**************************************************************************/
// clock
/**************************************************************************/

/* Compile with:   -lrt */

// Last clock values updated on queries
volatile uint32_t gClock32 = 0;
volatile uint64_t gClock64 = 0;

#ifndef _WIN // Linux

static struct timespec gts0;
static struct timespec gtr;

int clockInit( void )
{    
    clock_getres(CLOCK_REALTIME, &gtr);
    assert(gtr.tv_sec == 0);
    assert(gtr.tv_nsec == 1);
    clock_gettime(CLOCK_REALTIME, &gts0);

    getClock64();

#ifdef XCP_ENABLE_TESTMODE
    if (gDebugLevel >= 2) {
        printf("Init clock \n");
        printf("  System realtime clock resolution = %lds,%ldns\n", gtr.tv_sec, gtr.tv_nsec);
        //printf("clock now = %lds+%ldns\n", gts0.tv_sec, gts0.tv_nsec);
        //printf("clock year = %u\n", 1970 + gts0.tv_sec / 3600 / 24 / 365 );
        //printf("gClock64 = %lluus %llxh, gClock32 = %xh\n", gts0.tv_sec, gts0.tv_nsec, gClock64, gClock64, gClock32);
    }
#endif

    return 1;
}

// Free running clock with 1us tick
uint32_t getClock32(void) {

    struct timespec ts; 
    
    clock_gettime(CLOCK_REALTIME, &ts);
    gClock64 = ( ( (uint64_t)(ts.tv_sec/*-gts0.tv_sec*/) * 1000000ULL ) + (uint64_t)(ts.tv_nsec / 1000) ); // us
    gClock32 = (uint32_t)gClock64;
    return gClock32;  
}

uint64_t getClock64(void) {

    getClock32();
    return gClock64;
}

void sleepNs(unsigned int ns) {
    struct timespec timeout, timerem;
    timeout.tv_sec = 0;
    timeout.tv_nsec = (int32_t)ns;
    nanosleep(&timeout, &timerem);
}

#else

static uint64_t sFactor = 1;
static uint64_t sOffset = 0;

int clockInit(void) {

    LARGE_INTEGER tF, tC;

    if (QueryPerformanceFrequency(&tF)) {
        if (tF.u.HighPart) {
            printf("ERROR: Unexpected Performance Counter frequency\n");
            return 0;
        }
        sFactor = tF.u.LowPart; // Ticks pro s
        QueryPerformanceCounter(&tC);
        sOffset = (((__int64)tC.u.HighPart) << 32) | (__int64)tC.u.LowPart;
        getClock64();
#ifdef XCP_ENABLE_TESTMODE
        if (gDebugLevel >= 3) {
            printf("system realtime clock resolution = %I64u ticks per s\n", sFactor);
            printf("gClock64 = %I64u, gClock32 = %u\n", gClock64, gClock32);
        }
#endif

    }
    else {
        printf("ERROR: Performance Counter not available\n");
        return 0;
    }
    return 1;
}

// Free running clock with 1us tick
uint32_t getClock32(void) {
   
    LARGE_INTEGER t;
    uint64_t td;

    QueryPerformanceCounter(&t);
    td = (((uint64_t)t.u.HighPart) << 32) | (uint64_t)t.u.LowPart;
    if (sFactor == 1) {
        gClock64 = (uint64_t)(((td - sOffset) * 1000000UL));
    }
    else {
      gClock64 = (uint64_t)(((td - sOffset) * 1000000UL) / sFactor);
    }
    gClock32 = (uint32_t)gClock64;
    return gClock32;
}

// Free running clock with 1us in int64 which never overflows :-)
uint64_t getClock64(void) {

    getClock32();
    return gClock64;
}


void sleepNs(unsigned int ns) {

    vuint64 t1, t2;
    t1 = t2 = getClock64();

    vuint32 us = ns / 1000;
    vuint32 ms = us / 1000;

    // Start sleeping at 1800us, shorter sleeps are more precise but need significant CPU time
    if (us >= 2000) { 
        Sleep(ms-1);  
    }
    // Busy wait
    else {
        vuint64 te = t1 + us;
        for (;;) {
            t2 = getClock64();
            if (t2 >= te) break;
            if (te - t2 > 0) Sleep(0);  
        }
    }
}


#endif



