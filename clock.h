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

#elif defined(CLOCK_USE_UTC_TIME_US) // us since 1.1.1970

#define CLOCK_TICKS_PER_S  1000000 
#define CLOCK_TICKS_PER_MS 1000 
#define CLOCK_TICKS_PER_US 1

#else // us since init

#define CLOCK_TICKS_PER_S  1000000 
#define CLOCK_TICKS_PER_MS 1000 
#define CLOCK_TICKS_PER_US 1

#endif


extern volatile uint64_t gClock64;
extern volatile uint32_t gClock32;

extern int clockInit(void);
extern uint32_t getClock32(void);
extern uint64_t getClock64(void);
extern char* clockGetString(char* s, unsigned int cs, int64_t c);
extern void sleepNs(unsigned int ns);

#ifdef __cplusplus
}
#endif

#endif
