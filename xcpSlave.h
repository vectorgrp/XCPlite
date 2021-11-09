/* xcpSlave.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __XCPSLAVE_H_ 
#define __XCPSLAVE_H_

#ifdef __cplusplus
extern "C" {
#endif
	   
extern int XcpSlaveInit(uint16_t port, uint16_t mtu, uint16_t flushCycleMs);
extern int XcpSlaveShutdown();
extern int XcpSlaveStatus();

#ifdef _WIN
DWORD WINAPI XcpSlaveCMDThread(LPVOID lpParameter);
#else
extern void* XcpSlaveCMDThread(void* par);
#endif
#ifdef _WIN
DWORD WINAPI XcpSlaveDAQThread(LPVOID lpParameter);
#else
extern void* XcpSlaveDAQThread(void* par);
#endif


#ifdef __cplusplus
}
#endif



#endif
