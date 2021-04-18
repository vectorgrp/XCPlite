/* xcpAppl.h */


#ifndef __XCP_H_ 
#define __XCP_H_

#include "xcpLite.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int xcpServerInit(void);
extern void* xcpServerThread(void* par);

extern volatile vuint32 gClock;
extern volatile vuint64 gClock64;

extern int ApplXcpClockInit(void);
extern vuint32 ApplXcpGetClock(void);
extern vuint64 ApplXcpGetClock64(void);
extern void ApplXcpSleepNs(unsigned int ns);

#ifdef XCP_ENABLE_A2L
extern int ApplXcpReadA2LFile(char** p, unsigned int* n);
#endif

#ifdef __cplusplus
}
#endif



#endif
