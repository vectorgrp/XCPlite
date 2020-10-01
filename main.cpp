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

// Task cycles
volatile unsigned long gTaskCycleTimerECU = (1000000);
volatile unsigned long gTaskCycleTimerECUpp = (1000000);

volatile unsigned int gSocketPort = (5555); // UDP port
volatile unsigned int gSocketTimeout = (50000); // Receive timeout, determines the polling rate of transmit queue


}



// ECU simulation (C++ demo)
#include "ecupp.hpp"
ecu* gEcu = 0;

extern "C" {

    // Statics
    static volatile unsigned long gClock = 0;
    //static unsigned long gTaskTimerCMD = 0;
    static unsigned long gTaskTimerECU = 0;
    static unsigned long gTaskTimerECUpp = 0;

    // XCP command handler task
    void* xcpServer(void* par) {

        printf("Start XCP server\n");
        udpServerInit(gSocketPort,gSocketTimeout);

        // Server loop
        for (;;) {

            if (udpServerHandleXCPCommands() < 0) break;  // Handle XCP commands
            udpServerHandleTransmitQueue();

        } // for (;;)

        printf("XCP server shutdown\n");
        udpServerShutdown();
        return 0;
    }



    // ECU cyclic (1ms) demo task 
    // Calls C ECU demo code
    void* ecuTask(void* par) {

        printf("Start ecuTask\n");

        for (;;) {

            struct timespec timeout, timerem;
            timeout.tv_sec = 0;
            timeout.tv_nsec = 100000;
            nanosleep(&timeout, &timerem);

            gClock = ApplXcpGetTimestamp();

            /* 1ms C task */
            if (gClock - gTaskTimerECU > gTaskCycleTimerECU) {
                gTaskTimerECU = gClock;

                // C demo
                ecuCyclic();
            }

        }
        return 0;
    }
    
    // ECU cyclic (1ms) demo task 
    // Calls C++ ECU demo code
    void* ecuppTask(void* par) {

        printf("Start ecuppTask\n");

        for (;;) {

            struct timespec timeout, timerem;
            timeout.tv_sec = 0;
            timeout.tv_nsec = 100000;
            nanosleep(&timeout, &timerem);

            gClock = ApplXcpGetTimestamp();

            /* 1ms C++ task */
            if (gClock - gTaskTimerECUpp > gTaskCycleTimerECUpp) {
                gTaskTimerECUpp = gClock;

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
    int a1, a2, a3;
    pthread_t t1, t2, t3;
    
    printf(
        "\nRaspberryPi XCP on UDP Demo (Lite Version) \n"
        "V1.0\n"
        "Build " __DATE__ " " __TIME__ "\n"
        );

    // Initialize clock for DAQ event time stamps
    ApplXcpTimerInit();

    // Initialize XCP driver
    XcpInit();
    gDebugLevel = 1;

    // Initialize ECU demo (C)
    ecuInit();
    
    // Initialize ECU demo (C++)
    gEcu = new ecu();
            
    // Create the CMD and the ECU threads
    pthread_create(&t1, NULL, xcpServer, (void*)&a1);
    pthread_create(&t2, NULL, ecuTask, (void*)&a2); 
    //pthread_create(&t3, NULL, ecuppTask, (void*)&a3);

    pthread_join(t1, NULL); // xcpServer may fail, join here 
    pthread_cancel(t2); // and stop the other tasks
    //pthread_cancel(t3);

    return 0;
}


