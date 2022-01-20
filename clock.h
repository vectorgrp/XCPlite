#pragma once

/* clock.h */
/*
| Code released into public domain, no attribution required
*/

#ifdef __cplusplus
extern "C" {
#endif

// Clock resolution
#define CLOCK_USE_UTC_TIME_NS // Use ns timestamps relative to 1.1.1970 (TAI monotonic - no backward jumps)
//#define CLOCK_USE_APP_TIME_US // Use arbitrary us timestamps relative to application start


#if defined(CLOCK_USE_UTC_TIME_NS)

#define CLOCK_TICKS_PER_M  (1000000000ULL*60)
#define CLOCK_TICKS_PER_S  1000000000
#define CLOCK_TICKS_PER_MS 1000000
#define CLOCK_TICKS_PER_US 1000
#define CLOCK_TICKS_PER_NS 1

#else

#define CLOCK_TICKS_PER_S  1000000
#define CLOCK_TICKS_PER_MS 1000
#define CLOCK_TICKS_PER_US 1

#endif

// Clock
extern BOOL clockInit();
extern char* clockGetString(char* s, unsigned int cs, uint64_t c);
extern uint64_t clockGet64();



#ifdef __cplusplus
}
#endif
