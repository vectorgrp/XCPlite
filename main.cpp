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

// Task measurement data acquisition event numbers
extern "C" {
    unsigned int gXcpEvent_EcuCyclic = 0;
    unsigned int gXcpEvent_EcuTask1 = 0;
    unsigned int gXcpEvent_EcuTask2 = 0;
}

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
EcuTask* gEcuTask1 = 0;
EcuTask* gEcuTask2 = 0;

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

            ApplXcpGetClock();
            if (gClock - gCmdTimer > gCmdCycle) {
                gCmdTimer = gClock;
                if (udpServerHandleXCPCommands() < 0) break;  // Handle XCP commands
            }

#ifndef THREAD_ECU
            if (gClock - gTaskTimer > gTaskCycle) {
                gTaskTimer = gClock;
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
                if (gClock - gFlushTimer > gFlushCycle && gFlushCycle>0) {
                    gFlushTimer = gClock;
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

        printf("Start EcuTask1/EcuTask2\n");

        for (;;) {

            if (gExit) break; // Terminate application
            sleepns(gTaskCycleTimerECUpp);
            
            // C++ demo
            gEcuTask1->run();
            gEcuTask2->run(); // Task 2 runs twice per cycle to be able to see diffences between the 2 instances
            gEcuTask2->run();

        }
        return 0;
    }

}

#include <typeinfo>



// C++ main
int main(void)
{  
    printf(
        "\nRaspberryPi XCPlite: XCP on UDP Demo\n"
        "Build " __DATE__ " " __TIME__ "\n"
#ifdef XCP_ENABLE_A2L
        "  Option A2L\n"
#endif
#ifdef XCP_ENABLE_SO
        "  Option SO\n"
#endif
#ifdef XCP_ENABLE_PTP
        "  Option PTP\n"
#endif
#ifdef XCP_ENABLE_64
        "  Option 64\n"
#endif
#ifdef DTO_SEND_QUEUE
        "  Option SEND_QUEUE\n"
#endif
#ifdef DTO_SEND_RAW
        "  Option SEND_RAW\n"
#endif
#ifdef XCP_ENABLE_TESTMODE
        "  Option TEST\n"
#endif
#ifdef XCP_ENABLE_WIRINGPI
        "  Option WIRINGPI\n"
#endif
    );
     
    // Initialize clock for DAQ event time stamps
    ApplXcpClockInit();

    // Initialize module load addresses
#ifdef XCP_ENABLE_SO
    ApplXcpInitBaseAddressList();
#endif

    // Initialize digital io
#ifdef XCP_ENABLE_WIRINGPI
    wiringPiSetupSys();
    pinMode(PI_IO_1, OUTPUT);
#endif

    // Initialize XCP driver
    XcpInit();
#if defined ( XCP_ENABLE_TESTMODE )
    if (gXcpDebugLevel >= 1) {
        printf("gXcpDebugLevel = %u\n", gXcpDebugLevel);
        //printf("&gXcpDebugLevel = %0Xh\n", &gXcpDebugLevel);
    }
#endif

    //----------------------------------------------------------------------------------
    // Create A2L header and events
#ifdef XCP_ENABLE_A2L
    A2lInit(kXcpA2LFilenameString);
    gXcpEvent_EcuCyclic = A2lCreateEvent("EcuCyclic");   // Standard event triggered in C ecuTask
    gXcpEvent_EcuTask1 = A2lCreateEvent("EcuTask1");     // Extended event (relative address objects) triggered by C++ ecuTask1 instance
    gXcpEvent_EcuTask2 = A2lCreateEvent("EcuTask2");     // Extended event (relative address objects) triggered by C++ ecuTask2 instance
    // Events must be all defined before A2lHeader(), measurements and parameters have to defined afterwards
    A2lHeader();
#endif


    //----------------------------------------------------------------------------------
    // C++ demo

    // Initialize ECU demo runnables (C++)
    // Instances are associated to events
    gEcuTask1 = new EcuTask(gXcpEvent_EcuTask1);
    gEcuTask2 = new EcuTask(gXcpEvent_EcuTask2);

    // Create A2L measurement variables for all known EcuTask instances and generic for the class itself
    unsigned int eventList[] = { gXcpEvent_EcuTask1,gXcpEvent_EcuTask2 };
    gEcuTask1->CreateA2lClassDescription(2,eventList);
    


    //----------------------------------------------------------------------------------
    // C demo

    // Initialize ECU demo task (C) 
    ecuInit();
    ecuCreateA2lDescription();


    //----------------------------------------------------------------------------------
    // Create additional A2L parameters to control the demo
#ifdef XCP_ENABLE_A2L
    A2lCreateParameter(gCmdCycle, "us", "Command handler cycle time");
    A2lCreateParameter(gFlushCycle, "us", "Flush cycle time");
    A2lCreateParameter(gTaskCycleTimerECU, "ns", "ECU cycle time (ns delay)");
    A2lCreateParameter(gTaskCycleTimerECUpp, "ns", "ECU cycle time (ns delay)");
    A2lCreateParameter(gTaskCycleTimerServer, "ns", "Server loop cycle time (ns delay)");
    A2lCreateParameter(gXcpDebugLevel, "", "Debug verbosity");
    A2lCreateParameter(gExit, "", "Quit application");
    A2lCreateGroup("Test_Parameters", 7, "gCmdCycle", "gFlushCycle", "gTaskCycleTimerECU", "gTaskCycleTimerECUpp", "gTaskCycleTimerServer", "gXcpDebugLevel", "gExit");
#endif

    // Finish A2L
#ifdef XCP_ENABLE_A2L
    A2lClose();
#endif

    //----------------------------------------------------------------------------------
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

    //----------------------------------------------------------------------------------
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


