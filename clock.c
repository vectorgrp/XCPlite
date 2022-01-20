/*----------------------------------------------------------------------------
| File:
|   clock.c
|
| Description:
|   High resolution clock and time functions for Linux and Window
|   Support relative and absolute (UTC/TAI epoch) time stamps
|   Compile with:   -lrt
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "platform.h"
#include "clock.h"


#ifdef _LINUX // Linux

/*
Linux clock type
  CLOCK_REALTIME  This clock is affected by incremental adjustments performed by NTP
  CLOCK_TAI       This clock does not experience discontinuities and backwards jumps caused by NTP inserting leap seconds as CLOCK_REALTIME does.
                  Not available on WSL
*/
#define CLOCK_TYPE CLOCK_REALTIME
/// #define CLOCK_TYPE CLOCK_TAI

static struct timespec gtr;
#ifndef CLOCK_USE_UTC_TIME_NS
static struct timespec gts0;
#endif

char* clockGetString(char* s, unsigned int cs, uint64_t c) {
    (void)cs;
#ifndef CLOCK_USE_UTC_TIME_NS
    sprintf(s, "%gs", (double)c / CLOCK_TICKS_PER_S);
#else
    time_t t = (time_t)(c / CLOCK_TICKS_PER_S); // s since 1.1.1970
    struct tm tm;
    gmtime_r(&t, &tm);
    uint64_t fns = c % CLOCK_TICKS_PER_S;
    uint32_t tai_s = (uint32_t)((c % CLOCK_TICKS_PER_M) / CLOCK_TICKS_PER_S);
    sprintf(s, "%u.%u.%u %02u:%02u:%02u/%02u +%luns", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, (tm.tm_hour + 2) % 24, tm.tm_min, tm.tm_sec, tai_s, fns);
#endif
    return s;
}


int clockInit()
{

    printf("\nInit clock\n  (");
#ifdef CLOCK_USE_UTC_TIME_US
    #error "CLOCK_USE_UTC_TIME_US is deprecated!");
#endif
#ifdef CLOCK_USE_UTC_TIME_NS
    printf("CLOCK_USE_UTC_TIME_NS,");
#endif
#ifdef CLOCK_USE_APP_TIME_US
    printf("CLOCK_USE_APP_TIME_US,");
#endif

#if CLOCK_TYPE == CLOCK_TAI
    printf("CLOCK_TYPE_TAI,");
#endif
#if CLOCK_TYPE == CLOCK_REALTIME
    printf("CLOCK_TYPE_REALTIME,");
#endif
    printf(")\n");

    clock_getres(CLOCK_TYPE, &gtr);
    printf("Clock resolution is %lds,%ldns!\n", gtr.tv_sec, gtr.tv_nsec);

#ifndef CLOCK_USE_UTC_TIME_NS
    clock_gettime(CLOCK_TYPE, &gts0);
#endif
    clockGet64();

    if (gDebugLevel >= 2) {
        uint64_t t1, t2;
        char s[128];
        struct timespec gts_TAI;
        struct timespec gts_REALTIME;
        struct timeval ptm;
        // Print different clocks
        time_t now = time(NULL);
        gettimeofday(&ptm, NULL);
        clock_gettime(CLOCK_TAI, &gts_TAI);
        clock_gettime(CLOCK_REALTIME, &gts_REALTIME);
        printf("  CLOCK_TAI=%lus CLOCK_REALTIME=%lus time=%lu timeofday=%lu\n", gts_TAI.tv_sec, gts_REALTIME.tv_sec, now, ptm.tv_sec);
        // Check
        t1 = clockGet64();
        sleepNs(100000);
        t2 = clockGet64();
        printf("  +0us:   %s\n", clockGetString(s, sizeof(s), t1));
        printf("  +100us: %s (%u)\n", clockGetString(s, sizeof(s), t2), (uint32_t)(t2-t1));
        printf("\n");
    }

    return 1;
}


// Free running clock with 1us tick
uint64_t clockGet64() {

    struct timespec ts;
    clock_gettime(CLOCK_TYPE, &ts);
#ifdef CLOCK_USE_UTC_TIME_NS // ns since 1.1.1970
    return (((uint64_t)(ts.tv_sec) * 1000000000ULL) + (uint64_t)(ts.tv_nsec)); // ns
#else // us since init
    return (((uint64_t)(ts.tv_sec-gts0.tv_sec) * 1000000ULL) + (uint64_t)(ts.tv_nsec / 1000)); // us
#endif
}

#else // Windows

// Performance counter to clock conversion
static uint64_t sFactor = 0; // ticks per us
static uint8_t sDivide = 0; // divide or multiply
static uint64_t sOffset = 0; // offset


char *clockGetString(char* s, unsigned int cs, uint64_t c) {
    (void)cs;
#ifndef CLOCK_USE_UTC_TIME_NS
    sprintf(s, "%gs", (double)c / CLOCK_TICKS_PER_S);
#else
    time_t t = (time_t)(c / CLOCK_TICKS_PER_S); // s
    struct tm tm;
    gmtime_s(&tm, &t);
    uint64_t fns = c % CLOCK_TICKS_PER_S;
    uint32_t tai_s = (uint32_t)((c % CLOCK_TICKS_PER_M) / CLOCK_TICKS_PER_S);
    sprintf(s, "%u.%u.%u %02u:%02u:%02u/%02u +%" PRIu64 "s", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, (tm.tm_hour + 2) % 24, tm.tm_min, tm.tm_sec, tai_s, fns);
#endif
    return s;
}

#include <sys/timeb.h>

int clockInit() {

    printf("\nInit clock\n");
#ifdef CLOCK_USE_UTC_TIME_NS
    printf("  CLOCK_USE_UTC_TIME_NS\n");
#endif

    // Get current performance counter frequency
    // Determine conversion to CLOCK_TICKS_PER_S -> sDivide/sFactor
    LARGE_INTEGER tF, tC;
    uint64_t tp;
    if (!QueryPerformanceFrequency(&tF)) {
        printf("ERROR: Performance counter not available on this system!\n");
        return 0;
    }
    if (tF.u.HighPart) {
         printf("ERROR: Unexpected performance counter frequency!\n");
         return 0;
    }
#ifndef CLOCK_USE_UTC_TIME_NS
    sFactor = tF.u.LowPart / CLOCK_TICKS_PER_S;
    sDivide = 1;
#else
    if (CLOCK_TICKS_PER_S > tF.u.LowPart) {
        sFactor = CLOCK_TICKS_PER_S / tF.u.LowPart;
        sDivide = 0;
    }
    else {
        sFactor = tF.u.LowPart / CLOCK_TICKS_PER_S;
        sDivide = 1;
    }
#endif

    // Get current performance counter to absolute time relation
#ifdef CLOCK_USE_UTC_TIME_NS

    // Set time zone from TZ environment variable. If TZ is not set, the operating system is queried
    _tzset();

    // Get current UTC time in ms since 1.1.1970
    struct _timeb tstruct;
    uint64_t t;
    uint32_t t_ms;
    _ftime(&tstruct);
    t_ms = tstruct.millitm;
    t = tstruct.time;
    //_time64(&t); // s since 1.1.1970
#endif

    // Calculate factor and offset for clockGet64/32
    QueryPerformanceCounter(&tC);
    tp = (((int64_t)tC.u.HighPart) << 32) | (int64_t)tC.u.LowPart;
#ifndef CLOCK_USE_UTC_TIME_NS
    // Reset clock now
    sOffset = tp;
#else
    // set  offset from local clock UTC value t
    // this is inaccurate up to 1 s, but irrelevant because system clock UTC offset is also not accurate
    sOffset = t * CLOCK_TICKS_PER_S  + (uint64_t)t_ms * CLOCK_TICKS_PER_MS - tp * sFactor; 
#endif

    clockGet64();

    return 1;
}


// Clock 64 Bit (UTC or ARB) 
uint64_t clockGet64() {

    LARGE_INTEGER tp;
    uint64_t t;

    QueryPerformanceCounter(&tp);
    t = (((uint64_t)tp.u.HighPart) << 32) | (uint64_t)tp.u.LowPart;
    if (sDivide) {
        t = t / sFactor + sOffset;
    }
    else {
        t = t * sFactor + sOffset;
    }

    return t;
}


#endif // Windows

