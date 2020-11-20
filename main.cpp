/*----------------------------------------------------------------------------
| File:
|   main.cpp
|
| Description:
|   Demo main for XCP on Ethernet (UDP)
|   XCP server thread
|   Demo threads in C and C++ to emulate ECU tasks with measurement data acquisistion
|   Linux (Raspberry Pi) Version
 ----------------------------------------------------------------------------*/

// XCP driver
#include "xcpLite.h"

// XCP handler
#include "xcpAppl.h"

// UDP server
#include "udpserver.h"
#include "udpraw.h"

#include "A2L.h"

// ECU simulation (C demo)
#include "ecu.h"


#define THREAD_ECU
#define THREAD_ECUPP
#define THREAD_SERVER

// Parameters
volatile unsigned short gSocketPort = 5555; // UDP port
volatile unsigned int gSocketTimeout = 0; // General socket timeout

// Task delays
volatile vuint32 gTaskCycleTimerECU    = 1000000; // ns
volatile vuint32 gTaskCycleTimerECUpp  = 1000000; // ns
volatile vuint32 gTaskCycleTimerServer =   10000; // ns

// Cycles times
volatile vuint32 gFlushCycle = 100 * kApplXcpDaqTimestampTicksPerMs; // send a DTO packet at least every 100ms
volatile vuint32 gCmdCycle =    10 * kApplXcpDaqTimestampTicksPerMs; // check for commands every 10ms

static vuint32 gFlushTimer = 0;
static vuint32 gCmdTimer = 0;
#ifndef THREAD_ECU
static vuint32 gTaskTimer = 0;
#endif

// Quit application
volatile vuint8 gExit = 0;


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
    void* xcpServer(void* __par) {

        printf("Start XCP server\n");
        udpServerInit(gSocketPort,gSocketTimeout);

        // Server loop
        for (;;) {

            if (gExit) break; // Terminate application
            sleepns(gTaskCycleTimerServer);

            ApplXcpTimer();
            if (gTimer - gCmdTimer > gCmdCycle) {
                gCmdTimer = gTimer;
                if (udpServerHandleXCPCommands() < 0) break;  // Handle XCP commands
            }

#ifndef THREAD_ECU
            if (gTimer - gTaskTimer > gTaskCycle) {
                gTaskTimer = gTimer;
                ecuCyclic();
            }
#endif

            if (gXcp.SessionStatus & SS_DAQ) {

#ifdef DTO_SEND_QUEUE                
                // Transmit completed UDP packets from the transmit queue
                udpServerHandleTransmitQueue();
#endif
            
                // Cyclic flush of incomlete packets from transmit queue or transmit buffer to keep tool visualizations up to date
                // No priorisation of events implemented, no latency optimizations
                if (gTimer - gFlushTimer > gFlushCycle && gFlushCycle>0) {
                    gFlushTimer = gTimer;
#ifdef DTO_SEND_QUEUE  
                    udpServerFlushTransmitQueue();
#else
                    udpServerFlushPacketBuffer();
#endif
                } // Flush

            } // DAQ

        } // for (;;)

        sleepns(100000000);
        udpServerShutdown();
        return 0;
    }



    // ECU cyclic (1ms) demo task 
    // Calls C ECU demo code
    void* ecuTask(void* par) {

        printf("Start ecuTask\n");

        for (;;) {

            if (gExit) break; // Terminate application
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

            if (gExit) break; // Terminate application
            sleepns(gTaskCycleTimerECUpp);
            
            // C++ demo
            gEcu->task();
            
        }
        return 0;
    }

}

#include <typeinfo>



// C++ main
int main(void)
{  
    printf(
        "\nRaspberryPi XCP on UDP Demo (Lite Version with A2L generation) \n"
        "Build " __DATE__ " " __TIME__ "\n"
        );
     
    // Initialize clock for DAQ event time stamps
    ApplXcpTimerInit();

    ApplXcpInitBaseAddressList();

    // Initialize digital io
#if defined ( TEST_WIRINING_PI )
    wiringPiSetupSys();
    pinMode(PI_IO_1, OUTPUT);
#endif

    // Initialize XCP driver
    XcpInit();
#if defined ( XCP_ENABLE_TESTMODE )
    if (gXcpDebugLevel > 0) {
        printf("gXcpDebugLevel = %u\n", gXcpDebugLevel);
    }
#endif

    // Create A2L
#ifdef XCP_ENABLE_A2L

    // Create A2L file header
    A2lInit(kXcpA2LFilenameString);
    A2lCreateEvent("ECU");     // 1: Standard event triggered in ecuTask
    A2lCreateEvent("ECUPP");   // 2: Standard event triggered in ecuppTask
    A2lCreateEvent("ECUPPEXT");// 3: Extended event (relative address objects) triggered in ecuppTask
    A2lHeader();

    // Test parameters
    A2lCreateParameter(gCmdCycle, "us", "Command handler cycle time");
    A2lCreateParameter(gFlushCycle, "us", "Flush cycle time");
    A2lCreateParameter(gTaskCycleTimerECU, "ns", "ECU cycle time (ns delay)");
    A2lCreateParameter(gTaskCycleTimerECUpp, "ns", "ECU cycle time (ns delay)");
    A2lCreateParameter(gTaskCycleTimerServer, "ns", "Server loop cycle time (ns delay)");
    A2lCreateParameter(gXcpDebugLevel, "", "Debug verbosity");
    A2lCreateParameter(gExit, "", "Quit application");
    A2lCreateGroup("Test_Parameters", 7, "gCmdCycle", "gFlushCycle", "gTaskCycleTimerECU", "gTaskCycleTimerECUpp", "gTaskCycleTimerServer", "gXcpDebugLevel", "gExit");

#endif

    // Initialize ECU demo (C) variables and add them to A2L
    ecuInit();

    // Initialize ECU demo (C++) variables and add them to A2L 
    gEcu = new ecu();

    // Finish A2L
#ifdef XCP_ENABLE_A2L
    A2lClose();
#endif

    // Create the ECU threads
#ifdef THREAD_ECU
    pthread_t t2;
    int a2 = 0;
    pthread_create(&t2, NULL, ecuTask, (void*)&a2);
#endif
#ifdef THREAD_ECUPP
    pthread_t t3;
    int a3 = 0;
    pthread_create(&t3, NULL, ecuppTask, (void*)&a3);
#endif

    // Create the XCP server thread
#ifdef THREAD_SERVER
    pthread_t t1;
    int a1 = 0;
    pthread_create(&t1, NULL, xcpServer, (void*)&a1);
    pthread_join(t1, NULL); // t1 may fail ot terminate
#else
    xcpServer(NULL); // Run the XCP server here
#endif


#ifdef THREAD_ECU
    pthread_cancel(t2);
#endif
#ifdef THREAD_ECU
    pthread_cancel(t3);
#endif
    
    return 0;
}


