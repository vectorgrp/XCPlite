/* util.h */

#ifndef __UTIL_H_ 
#define __UTIL_H_

#ifdef __cplusplus
extern "C" {
#endif
	
extern volatile uint64_t gClock64; // Win
extern volatile uint32_t gClock32;

extern int clockInit(void);
extern uint32_t getClock32(void);
extern uint64_t getClock64(void);

extern void sleepNs(unsigned int ns);


#define PI2 6.28318530718
#define PI (PI2/2)


#ifdef __cplusplus
}
#endif

#endif
