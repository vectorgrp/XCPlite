/*----------------------------------------------------------------------------
| File:
|   main.cpp
|   V1.0 23.9.2020
|
| Project:
|   Demo for XCP on Ethernet (UDP)
|   Linux (Raspberry Pi) Version
 ----------------------------------------------------------------------------*/



extern "C" {

// XCP driver
#include "xcpLite.h"

// XCP handler
#include "xcp.h"

// UDP server
#include "udpserver.h"

// ECU simulation (C demo)
#include "ecu.h"

// Parameters
volatile unsigned long gTaskCycleTimerCMD = 10000000; // 1ms default task cycle time in ns
volatile unsigned long gTaskCycleTimerDAQ = 1000000;
    
}


// ECU simulation (C++ demo)
#include "ecupp.hpp"
ecu* gEcu = 0;

extern "C" {

    // Statics
    static volatile unsigned long gClock = 0;
    static unsigned long gTaskTimerCMD = 0;
    static unsigned long gTaskTimerDAQ = 0;

    // XCP command handler task
    void* xcpServer(void* par) {

        printf("Start XCP server\n");
        udpServerInit(5555);

        // 10ms task */
        for (;;) {

            gClock = ApplXcpDaqGetTimestamp();
            if (gClock - gTaskTimerCMD > gTaskCycleTimerCMD) {
                gTaskTimerCMD = gClock;
                if (udpServerHandleXCPCommands() < 0) break;  // Handle XCP commands


            }

        } // for (;;)

        printf("XCP server shutdown\n");
        udpServerShutdown();
        return 0;
    }



    // ECU cyclic (1ms) demo task 
    // Calls C and C++ ECU demo code
    void* ecuTask(void* par) {

        printf("Start ecu task\n");

        for (;;) {

            struct timespec timeout, timerem;
            timeout.tv_sec = 0;
            timeout.tv_nsec = 10000;
            nanosleep(&timeout, &timerem);

            gClock = ApplXcpDaqGetTimestamp();

            /* 1ms task */
            if (gClock - gTaskTimerDAQ > gTaskCycleTimerDAQ) {
                gTaskTimerDAQ = gClock;

                // C demo
                ecuCyclic();


                // C++ demo
                gEcu->task();
            }

        }
        return 0;
    }

}



// C++ main
int main(void)
{

    int a1, a2;
    pthread_t t1, t2;
    pthread_mutexattr_t a;

    printf(
        "\nRaspberryPi XCP on UDP Demo (Lite Version) \n"
        "V1.0\n"
        "Build " __DATE__ " " __TIME__ "\n"
        );

    // Initialize clock for DAQ event time stamps
    ApplXcpTimerInit();

    // Initialize XCP driver
    XcpInit();

    // Initialize ECU demo (C)
    ecuInit();
    
    // Initialize ECU demo (C++)
    gEcu = new ecu();
        
    // Create a mutex for XCP driver, must be recursive
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&gXcpMutex, &a);

    // Create the CMD and the DAQ thread
    pthread_create(&t1, NULL, xcpServer, (void*)&a1);
    pthread_create(&t2, NULL, ecuTask, (void*)&a2);

    pthread_join(t1, NULL); // xcpServer may fail, join here 
    pthread_cancel(t2); // and stop XcpTask

    return 0;
}