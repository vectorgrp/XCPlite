/* xcpSlave.h */

#ifndef __XCPSLAVE_H_ 
#define __XCPSLAVE_H_

#ifdef __cplusplus
extern "C" {
#endif
	   
extern volatile unsigned int gFlushCycleMs;
extern volatile int gXcpSlaveCMDThreadRunning;
extern volatile int gXcpSlaveDAQThreadRunning;

extern int xcpSlaveInit(unsigned char *slaveAddr, uint16_t slavePort, unsigned int jumbo);
extern int xcpSlaveShutdown();
extern int xcpSlaveRestart();

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
