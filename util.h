/* util.h */

#ifndef __UTIL_H_ 
#define __UTIL_H_

#ifdef __cplusplus
extern "C" {
#endif

extern volatile vuint64 gClock64;
extern volatile vuint32 gClock32;

extern int clockInit(void);
extern vuint32 getClock32(void);
extern vuint64 getClock64(void);

extern void sleepNs(unsigned int ns);

extern void seed16(unsigned int seed);
extern unsigned int random16();


#ifdef __cplusplus
}
#endif

#endif
