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
#include "udpraw.h"

// ECU simulation (C demo)
#include "ecu.h"



// Parameters
volatile unsigned short gSocketPort = 5555; // UDP port
volatile unsigned int gSocketTimeout = 0; // General socket timeout

// Task delays
volatile vuint32 gTaskCycleTimerECU = 1000000; // ns
volatile vuint32 gTaskCycleTimerECUpp = 1000000; // ns
volatile vuint32 gTaskCycleTimerServer = 10000; // ns

// Cycles times
volatile vuint32 gFlushCycle = 100 * kApplXcpDaqTimestampTicksPerMs; // 100ms send a DTO packet at least every 100ms
volatile vuint32 gCmdCycle = 10 * kApplXcpDaqTimestampTicksPerMs; 
volatile vuint32 gTaskCycle = 1 * kApplXcpDaqTimestampTicksPerMs; 

static vuint32 gFlushTimer = 0;
static vuint32 gCmdTimer = 0;
static vuint32 gTaskTimer = 0;

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

        printf("Start XCP server\n");
        udpServerInit(gSocketPort,gSocketTimeout);

        // Server loop
        for (;;) {

            ApplXcpTimer();
            sleepns(gTaskCycleTimerServer);

            if (gTimer - gCmdTimer > gCmdCycle) {
                gCmdTimer = gTimer;
                if (udpServerHandleXCPCommands() < 0) break;  // Handle XCP commands
            }

            if (gTimer - gTaskTimer > gTaskCycle) {
                gTaskTimer = gTimer;
                ecuCyclic();
            }

            if (gXcp.SessionStatus & SS_DAQ) {

#ifdef DTO_SEND_QUEUE                
                // Transmit completed UDP packets from the transmit queue
                udpServerHandleTransmitQueue();
#endif
            
                // Cyclic flush of incomlete packets from transmit queue or transmit buffer to keep tool visualizations up to date
                // No priorisation of events implemented, no latency optimizations
                if (gTimer - gFlushTimer > gFlushCycle && gFlushTimer>0) {
                    gFlushTimer = gTimer;
#ifdef DTO_SEND_QUEUE  
                    udpServerFlushTransmitQueue();
#else
                    udpServerFlushPacketBuffer();
#endif
                } // Flush

            } // DAQ

        } // for (;;)

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


extern "C" {



}




// C++ main
int main(void)
{  
    printf(
        "\nRaspberryPi XCP on UDP Demo (Lite Version) \n"
        "V1.0\n"
        "Build " __DATE__ " " __TIME__ "\n"
        );
      
    // Initialize clock for DAQ event time stamps
    ApplXcpTimerInit();

    // Initialize XCP driver
    XcpInit();
#if defined ( XCP_ENABLE_TESTMODE )
    if (gXcpDebugLevel >= 0) {
        printf("gXcpDebugLevel = %u\n", gXcpDebugLevel);
    }
#endif

    // Initialize ECU demo (C)
    ecuInit();
    
    // Initialize ECU demo (C++)
    gEcu = new ecu();
      
    
    //pthread_t t2;
    //int a2;
    //pthread_create(&t2, NULL, ecuTask, (void*)&a2);

    //pthread_t t3;
    //int a3;
    //pthread_create(&t3, NULL, ecuppTask, (void*)&a3);

    // Create the CMD and the ECU threads
    //pthread_t t1;
    //int a1
    //pthread_create(&t1, NULL, xcpServer, (void*)&a1);
    //pthread_join(t1, NULL); // t1 may fail
    xcpServer(0); // Run the XCP server here

    //pthread_cancel(t2);
    //pthread_cancel(t3);

    return 0;
}


