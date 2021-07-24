/* clock.h */
/*
| Code released into public domain, no attribution required
*/

#ifndef __CLOCK_H_ 
#define __CLOCK_H_

#ifdef __cplusplus
extern "C" {
#endif


#if defined(CLOCK_USE_UTC_TIME_NS) // ns since 1.1.1970

#define CLOCK_TICKS_PER_S  1000000000 
#define CLOCK_TICKS_PER_MS 1000000 
#define CLOCK_TICKS_PER_US 1000
#define CLOCK_TICKS_PER_NS 1 

#else // us since init

#define CLOCK_TICKS_PER_S  1000000 
#define CLOCK_TICKS_PER_MS 1000 
#define CLOCK_TICKS_PER_US 1

#endif

extern volatile uint64_t gLocalClock64;
extern volatile uint32_t gLocalClock32;

extern int clockInit(int ptpEnable, uint8_t ptpDomain);
extern char* clockGetString(char* s, unsigned int cs, uint64_t c);
extern void clockShutdown();

extern uint32_t getLocalClock32();
extern uint64_t getLocalClock64();

extern void sleepNs(uint32_t ns);
extern void sleepMs(uint32_t ms);


#ifdef __cplusplus
}
#endif

#endif
