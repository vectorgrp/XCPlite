/* xcpSlave.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __XCPSLAVE_H_ 
#define __XCPSLAVE_H_

#ifdef __cplusplus
extern "C" {
#endif
	   
extern volatile int gXcpSlaveCMDThreadRunning;
extern volatile int gXcpSlaveDAQThreadRunning;

extern int xcpSlaveInit();
extern int xcpSlaveShutdown();

#ifdef _WIN
DWORD WINAPI xcpSlaveCMDThread(LPVOID lpParameter);
#else
extern void* xcpSlaveCMDThread(void* par);
#endif
#ifdef _WIN
DWORD WINAPI xcpSlaveDAQThread(LPVOID lpParameter);
#else
extern void* xcpSlaveDAQThread(void* par);
#endif


#ifdef __cplusplus
}
#endif



#endif
