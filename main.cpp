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

// Cycles times
volatile vuint32 gTaskCycleTimerECU = 1* kApplXcpDaqTimestampTicksPerMs; // 1ms
volatile vuint32 gTaskCycleTimerECUpp = 1 * kApplXcpDaqTimestampTicksPerMs; // 1ms
volatile vuint32 gFlushCycle = 100 * kApplXcpDaqTimestampTicksPerMs; // 100ms send a DTO packet at least every 100ms

volatile unsigned short gSocketPort = 5555; // UDP port
volatile unsigned int gSocketTimeout = 0; // Receive timeout, determines the polling rate of transmit queue


}



// ECU simulation (C++ demo)
#include "ecupp.hpp"
ecu* gEcu = 0;

extern "C" {

       
    static void sleepns(unsigned int ns) {
        struct timespec timeout, timerem;
        timeout.tv_sec = 0;
        timeout.tv_nsec = ns;
        nanosleep(&timeout, &timerem);
    }
    

    // XCP command handler task
    void* xcpServer(void* par) {

        static vuint32 gFlushTimer = 0;

        printf("Start XCP server\n");
        udpServerInit(gSocketPort,gSocketTimeout);

        // Server loop
        for (;;) {

            if (udpServerHandleXCPCommands() < 0) break;  // Handle XCP commands
            udpServerHandleTransmitQueue();
            sleepns(10000);

            // Cyclic flush of the transmit queue to keep tool visualizations up to date
            ApplXcpTimer();
            if (gTimer - gFlushTimer > gFlushCycle) {
              gFlushTimer = gTimer;
              udpServerFlushTransmitQueue();
            }

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

            sleepns(gTaskCycleTimerECU);
            
            // C demo
                ecuCyclic();
            
        }
        return 0;
    }
    
    // ECU cyclic (1ms) demo task 
    // Calls C++ ECU demo code
    void* ecuppTask(void* par) {

        printf("Start ecuppTask\n");

        for (;;) {

            sleepns(gTaskCycleTimerECUpp);
            
            // C++ demo
            gEcu->task();
            
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


