/*----------------------------------------------------------------------------
| File:
|   clock.c
|
| Description:
|   High resolution clock and time functions for Linux and Window
|   Support relative and absolute (UTC like) time stamps
|   Compile with:   -lrt
|
| Code released into public domain, no attribution required
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "clock.h"



// Last clock values updated on all clock queries (getClockXx())
// May be used as wall clock
volatile uint32_t gClock32 = 0;
volatile uint64_t gClock64 = 0;

#ifdef _LINUX // Linux

static struct timespec gts0;
static struct timespec gtr;

char* clockGetString(char* s, unsigned int cs, uint64_t c) {

#ifdef CLOCK_USE_APP_TIME_US
    sprintf_s(s, (size_t)cs, "%gs", (double)c / CLOCK_TICKS_PER_S);
#else
    time_t t = (time_t)(c / CLOCK_TICKS_PER_S); // s since 1.1.1970
    struct tm tm;
    gmtime_r(&t, &tm);
    uint64_t fs = c % CLOCK_TICKS_PER_S;
    sprintf(s, "%u.%u.%u %02u:%02u:%02u +%lu", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, (tm.tm_hour + 2) % 24, tm.tm_min, tm.tm_sec, fs);
#endif
    return s;
}


int clockInit( void )
{    
    clock_getres(CLOCK_REALTIME, &gtr);
    if (gtr.tv_sec != 0 || gtr.tv_nsec != 1) {
      printf("ERROR: Unexpected real time clock frequency!\n");
      return 0;
    }
    clock_gettime(CLOCK_REALTIME, &gts0);
    getClock64();

    if (gDebugLevel >= 1) {
        uint64_t t1, t2;
        char s[128];
        printf("Init clock \n");
        printf("  System realtime clock resolution = %lds,%ldns\n", gtr.tv_sec, gtr.tv_nsec);
        t1 = getClock64();
        sleepNs(100000);
        t2 = getClock64();
        printf("  +0us:   %s\n", clockGetString(s, sizeof(s), t1));
        printf("  +100us: %s\n", clockGetString(s, sizeof(s), t2));
        printf("\n");
    }

    return 1;
}

// Free running clock with 1us tick
uint32_t getClock32(void) {

    struct timespec ts; 
    clock_gettime(CLOCK_REALTIME, &ts);
#if defined(CLOCK_USE_UTC_TIME_NS) // ns since 1.1.1970
    gClock64 = (((uint64_t)(ts.tv_sec) * 1000000000ULL) + (uint64_t)(ts.tv_nsec)); // ns
#elif defined(CLOCK_USE_UTC_TIME_US) // us since 1.1.1970
    gClock64 = (((uint64_t)(ts.tv_sec) * 1000000ULL) + (uint64_t)(ts.tv_nsec / 1000)); // us
#else // us since init
    gClock64 = (((uint64_t)(ts.tv_sec-gts0.tv_sec) * 1000000ULL) + (uint64_t)(ts.tv_nsec / 1000)); // us
#endif
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

#else // Windows


static uint64_t sFactor = 0; // ticks per us
static uint8_t sDivide = 0; // divide or multiply
static uint64_t sOffset = 0; // offset


char *clockGetString(char* s, unsigned int cs, uint64_t c) {

#ifdef CLOCK_USE_APP_TIME_US
    sprintf_s(s, (size_t)cs, "%gs", (double)c / CLOCK_TICKS_PER_S);
#else
    time_t t = c / CLOCK_TICKS_PER_S; // s
    struct tm tm;
    gmtime_s(&tm, &t);
    int64_t fs = c % CLOCK_TICKS_PER_S; 
    sprintf_s(s, (size_t)cs, "%u.%u.%u %02u:%02u:%02u +%llu", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, (tm.tm_hour + 2) % 24, tm.tm_min, tm.tm_sec, fs);
#endif
    return s;
}


int clockInit(void) {

#ifndef CLOCK_USE_APP_TIME_US
    _tzset();

    // Get current UTC time in ns since 1.1.1970
    time_t t;
    _time64(&t); // s since 1.1.1970
#endif

    // Get current performance counter
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

    // Calculate factor and offset for getClock64/32
    QueryPerformanceCounter(&tC);
    tp = (((int64_t)tC.u.HighPart) << 32) | (int64_t)tC.u.LowPart;
#ifdef CLOCK_USE_APP_TIME_US
    sOffset = tp;
    sDivide = 1;
    sFactor = tF.u.LowPart / CLOCK_TICKS_PER_S;
#else
    if (CLOCK_TICKS_PER_S > tF.u.LowPart) {
        sFactor = CLOCK_TICKS_PER_S / tF.u.LowPart;
        sDivide = 0;
        sOffset = tp - (uint64_t)t * CLOCK_TICKS_PER_S / sFactor; // This conversion is inaccurat, but irrelevant because system UTC offset is also not accurate
    }
    else {
        sFactor = tF.u.LowPart / CLOCK_TICKS_PER_S;
        sDivide = 1;
        sOffset = tp - (uint64_t)t * CLOCK_TICKS_PER_S * sFactor; // This conversion is inaccurat, but irrelevant because system UTC offset is also not accurate
    }
#endif

    getClock64();

    if (gDebugLevel >= 1) {
        struct tm tm;
        _gmtime64_s(&tm, &t);
        printf("Init clock \n");
        printf("  UTC time = %llus since 1.1.1970 ", t);
        printf("  %u.%u.%u %u:%u:%u\n", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, (tm.tm_hour + 2) % 24, tm.tm_min, tm.tm_sec);

       uint64_t t1, t2;
       char s[64];
       t1 = getClock64();
       sleepNs(100000);
       t2 = getClock64();
       printf("  XCP clock resolution = %u Hz, system resolution = %u Hz, conversion %c= %I64u\n", CLOCK_TICKS_PER_S, tF.u.LowPart, sDivide?'/':'*', sFactor);
       printf("  +0us:   %I64u  %s\n", t1, clockGetString(s, sizeof(s), t1));
       printf("  +100us: %I64u  %s\n", t2, clockGetString(s, sizeof(s), t2));
       printf("\n");
    }

    return 1;
}


// Clock with 1us ticks, 32 Bit with wrap around at 0xFFFFFFFF
uint32_t getClock32(void) {
   
    LARGE_INTEGER t;
    uint64_t td;

    QueryPerformanceCounter(&t);
    td = (((uint64_t)t.u.HighPart) << 32) | (uint64_t)t.u.LowPart;
    if (sDivide) {
        gClock64 = (uint64_t)(td - sOffset) / sFactor;
    }
    else {
        gClock64 = (uint64_t)(td - sOffset) * sFactor;
    }
    gClock32 = (uint32_t)gClock64;
    return gClock32;
}

// Clock with 1us tick, 64 Bit
uint64_t getClock64(void) {

    getClock32();
    return gClock64;
}


void sleepNs(unsigned int ns) {

    vuint64 t1, t2;
    vuint32 us = ns / 1000;
    vuint32 ms = us / 1000;

    // Start sleeping at 1800us, shorter sleeps are more precise but need significant CPU time
    if (us >= 2000) { 
        Sleep(ms-1);  
    }
    // Busy wait
    else {
        t1 = t2 = getClock64();
        vuint64 te = t1 + us*CLOCK_TICKS_PER_US;
        for (;;) {
            t2 = getClock64();
            if (t2 >= te) break;
            if (te - t2 > 0) Sleep(0);  
        }
    }
}


#endif


