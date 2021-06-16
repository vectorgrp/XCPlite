/* xcpSlave.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __XCPSLAVE_H_ 
#define __XCPSLAVE_H_

#ifdef __cplusplus
extern "C" {
#endif
	   
extern volatile unsigned int gFlushCycleMs;
extern volatile int gXcpSlaveCMDThreadRunning;
extern volatile int gXcpSlaveDAQThreadRunning;

extern int xcpSlaveInit(unsigned char *slaveMac, unsigned char *slaveAddr, uint16_t slavePort, unsigned int jumbo);
extern int xcpSlaveRestart(void);
extern void* xcpSlaveCMDThread(void* par);
extern void* xcpSlaveDAQThread(void* par);
extern int xcpSlaveShutdown(void);

#ifdef __cplusplus
}
#endif



#endif
