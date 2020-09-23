// xcp.h
// V1.0 23.9.2020

#include "xcpLite.h"
extern vuint8 gDebugLevel;

#include <pthread.h>
extern pthread_mutex_t gXcpMutex;  // Mutex for multithreaded DAQ

extern volatile unsigned long gTaskCycleTimerCMD;
extern volatile unsigned long gTaskCycleTimerDAQ;


extern void* xcpServer(void* par);
extern void* ecuTask(void* par);
extern int xcpMain(void);
