/* xcpAppl.h */


#ifndef __XCP_H_ 
#define __XCP_H_

#include "xcpLite.h"

#ifdef __cplusplus
extern "C" {
#endif

extern volatile vuint32 gTimer;

extern void ApplXcpTimerInit(void);
extern vuint32 ApplXcpTimer(void);

#ifdef __cplusplus
}
#endif

#endif
