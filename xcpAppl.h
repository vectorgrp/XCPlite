/* xcpAppl.h */


#ifndef __XCP_H_ 
#define __XCP_H_

#include "xcpLite.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void xcpServerInit(void);
extern void* xcpServerThread(void* __par);

extern volatile vuint32 gClock;
extern volatile vuint64 gClock64;

extern void ApplXcpClockInit(void);
extern vuint32 ApplXcpGetClock(void);
extern vuint64 ApplXcpGetClock64(void);
extern void ApplXcpSleepNs(unsigned int ns);


#ifdef __cplusplus
}
#endif



#endif
