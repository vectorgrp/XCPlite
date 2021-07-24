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
#include "clock.h"


// Last clock values updated on all clock queries (getLocalClockXx())
// May be used as wall clock
volatile uint32_t gLocalClock32 = 0;
volatile uint64_t gLocalClock64 = 0;



#ifdef _LINUX // Linux

/* 
Linux clock type
  CLOCK_REALTIME  This clock is affected by incremental adjustments performed by NTP
  CLOCK_TAI       This clock does not experience discontinuities and backwards jumps caused by NTP inserting leap seconds as CLOCK_REALTIME does.
*/
#define CLOCK_TYPE CLOCK_TAI

static struct timespec gtr;
#ifdef CLOCK_USE_APP_TIME_US
  static struct timespec gts0;
#endif

char* clockGetString(char* s, unsigned int cs, uint64_t c) {

#ifdef CLOCK_USE_APP_TIME_US
    sprintf(s, "%gs", (double)c / CLOCK_TICKS_PER_S);
#else
    time_t t = (time_t)(c / CLOCK_TICKS_PER_S); // s since 1.1.1970
    struct tm tm;
    gmtime_r(&t, &tm);
    uint64_t fs = c % CLOCK_TICKS_PER_S;
    sprintf(s, "%u.%u.%u %02u:%02u:%02u +%luns", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, (tm.tm_hour + 2) % 24, tm.tm_min, tm.tm_sec, fs);
#endif
    return s;
}


int clockInit(int ptpEnable, uint8_t ptpDomain)
{    

    printf("Init clock\n");

#ifdef CLOCK_USE_UTC_TIME_US 
    #error "CLOCK_USE_UTC_TIME_US is deprecated!");
#endif
#ifdef CLOCK_USE_APP_TIME_US 
    printf(" CLOCK_USE_APP_TIME_US\n");
#endif
#ifdef CLOCK_USE_UTC_TIME_NS 
    printf(" CLOCK_USE_UTC_TIME_NS\n");
#endif

#if CLOCK_TYPE == CLOCK_TAI 
    printf(" CLOCK_TYPE_TAI\n");
#endif
#if CLOCK_TYPE == CLOCK_REALTIME
    printf(" CLOCK_TYPE_REALTIME\n");
#endif

    clock_getres(CLOCK_TYPE, &gtr);
    if (gtr.tv_sec != 0 || gtr.tv_nsec != 1) {
        printf("ERROR: Unexpected clock frequency %lds,%ldns!\n", gtr.tv_sec, gtr.tv_nsec);
        return 0;
    }

#ifdef CLOCK_USE_APP_TIME_US
    clock_gettime(CLOCK_TYPE, &gts0);
#endif
    getLocalClock64();

    if (gDebugLevel >= 1) {
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
        printf("  CLOCK_TAI=%us CLOCK_REALTIME=%us time=%u timeofday=%u\n", gts_TAI.tv_sec, gts_REALTIME.tv_sec, now, ptm.tv_sec);
        // Check 
        t1 = getLocalClock64();
        sleepMs(1);
        t2 = getLocalClock64();
        printf("  +0us:   %s\n", clockGetString(s, sizeof(s), t1));
        printf("  +100us: %s (%u)\n", clockGetString(s, sizeof(s), t2), (uint32_t)(t2-t1));
        printf("\n");
    }

    return 1;
}

// Free running clock with 1us tick
uint32_t getLocalClock32() {

    struct timespec ts; 
    clock_gettime(CLOCK_TYPE, &ts);
#ifdef CLOCK_USE_UTC_TIME_NS // ns since 1.1.1970
    gLocalClock64 = (((uint64_t)(ts.tv_sec) * 1000000000ULL) + (uint64_t)(ts.tv_nsec)); // ns
#else // us since init
    gLocalClock64 = (((uint64_t)(ts.tv_sec-gts0.tv_sec) * 1000000ULL) + (uint64_t)(ts.tv_nsec / 1000)); // us
#endif
    gLocalClock32 = (uint32_t)gLocalClock64;
    return gLocalClock32;  
}

uint64_t getLocalClock64() {

    getLocalClock32();
    return gLocalClock64;
}

void sleepNs(uint32_t ns) {
    struct timespec timeout, timerem;
    assert(ns < 1000000000UL);
    timeout.tv_sec = 0;
    timeout.tv_nsec = (int32_t)ns;
    nanosleep(&timeout, &timerem);
}

void sleepMs(uint32_t ms) {
    struct timespec timeout, timerem;
    timeout.tv_sec = (int32_t)ms/1000;
    timeout.tv_nsec = (int32_t)(ms%1000)*1000000;
    nanosleep(&timeout, &timerem);
}

#else // Windows

// Performance counter to clock conversion 
static uint64_t sFactor = 0; // ticks per us
static uint8_t sDivide = 0; // divide or multiply
static uint64_t sOffset = 0; // offset


char *clockGetString(char* s, unsigned int cs, uint64_t c) {

#ifdef CLOCK_USE_APP_TIME_US
    sprintf(s, "%gs", (double)c / CLOCK_TICKS_PER_S);
#else
    time_t t = c / CLOCK_TICKS_PER_S; // s
    struct tm tm;
    gmtime_s(&tm, &t);
    int64_t fs = c % CLOCK_TICKS_PER_S; 
    sprintf(s, "%u.%u.%u %02u:%02u:%02u +%lluns", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, (tm.tm_hour + 2) % 24, tm.tm_min, tm.tm_sec, fs);
#endif
    return s;
}

#include <sys/timeb.h>

int clockInit(int ptpEnable, uint8_t ptpDomain) {

    printf("Init clock\n");
#ifdef CLOCK_USE_UTC_TIME_NS 
    printf(" CLOCK_USE_UTC_TIME_NS\n");
#endif
#ifdef CLOCK_USE_APP_TIME_US 
    printf(" CLOCK_USE_APP_TIME_US\n");
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
#ifdef CLOCK_USE_APP_TIME_US
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
#ifndef CLOCK_USE_APP_TIME_US

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

    // Calculate factor and offset for getLocalClock64/32
    QueryPerformanceCounter(&tC);
    tp = (((int64_t)tC.u.HighPart) << 32) | (int64_t)tC.u.LowPart;
#ifdef CLOCK_USE_APP_TIME_US
    // Reset clock now
    sOffset = tp; 
#else
    // set initial offset from local clock value t
    // this is inaccurate up to 1 s, but irrelevant because system clock UTC offset is also not more accurate
    sOffset = (uint64_t)t * CLOCK_TICKS_PER_S  + t_ms * CLOCK_TICKS_PER_MS - tp * sFactor; 

    printf("  Current time = %I64uus + %ums\n", t, t_ms);
    printf("  Zone difference in minutes from UTC: %d\n", tstruct.timezone);
    printf("  Time zone: %s\n", _tzname[0]);
    printf("  Daylight saving: %s\n", tstruct.dstflag ? "YES" : "NO");
#endif

    getLocalClock64();

    // Test
    if (gDebugLevel >= 1) {
#ifndef CLOCK_USE_APP_TIME_US
        struct tm tm;
        _gmtime64_s(&tm, &t);
        printf("  UTC time = %llus since 1.1.1970 ", t);
        printf("  %u.%u.%u %u:%u:%u\n", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, (tm.tm_hour + 2) % 24, tm.tm_min, tm.tm_sec);
#endif
       uint64_t t1, t2;
       char s[64];
       t1 = getLocalClock64();
       sleepNs(100000);
       t2 = getLocalClock64();
       printf("  XCP clock resolution = %u Hz, system resolution = %u Hz, conversion %c%I64u-%I64u\n", CLOCK_TICKS_PER_S, tF.u.LowPart,  sDivide?'/':'*', sFactor, sOffset );
       printf("  +0us:   %I64u  %s\n", t1, clockGetString(s, sizeof(s), t1));
       printf("  +100us: %I64u  %s\n", t2, clockGetString(s, sizeof(s), t2));
       printf("\n");
    }

    return 1;
}



// Clock 64 Bit (UTC or ARB)
uint64_t getLocalClock64() {
   
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

    gLocalClock64 = t;
    gLocalClock32 = (uint32_t)t;
    return t;
}

// Clock 32 Bit
uint32_t getLocalClock32() {

    return (uint32_t)getLocalClock64();
}



void sleepNs(unsigned int ns) {

    vuint64 t1, t2;
    vuint32 us = ns / 1000;
    vuint32 ms = us / 1000;

    // Start sleeping at 1800us, shorter sleeps are more precise but need significant CPU time
    if (us >= 2000) {
        Sleep(ms - 1);
    }
    // Busy wait
    else {
        t1 = t2 = getLocalClock64();
        vuint64 te = t1 + us * CLOCK_TICKS_PER_US;
        for (;;) {
            t2 = getLocalClock64();
            if (t2 >= te) break;
            if (te - t2 > 0) Sleep(0);
        }
    }
}

void sleepMs(unsigned int ms) {

    Sleep(ms);
}


#endif


