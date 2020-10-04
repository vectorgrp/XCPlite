// xcp.h
// V1.0 23.9.2020

#if !defined ( __XCP_H_ )
#define __XCP_H_

#include "xcpLite.h"

extern volatile vuint32 gTimer;

extern void ApplXcpTimerInit(void);
extern vuint32 ApplXcpTimer(void);

#endif

