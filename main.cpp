/*----------------------------------------------------------------------------
| File:
|   main.cpp
|
| Description:
|   Demo main for XCP on Ethernet (UDP)
|   Demo threads in C and C++ to emulate ECU tasks with measurement data acquisistion
|   Linux (Raspberry Pi) Version
 ----------------------------------------------------------------------------*/

#include <typeinfo>

// XCP driver
#include "xcpLite.h"
#include "xcpAppl.h"
#include "A2L.h"

// ECU simulation (C demo)
#include "ecu.h"

// Calibration Parameters

// Task delays
volatile vuint32 gTaskCycleTimerECU    = 1000000; // ns  Cycle time of the C task
volatile vuint32 gTaskCycleTimerECUpp  = 1000000; // ns  Cycle time of the C++ task

// Active task
volatile unsigned int gActiveEcuTaskId = 0; // Task id of the active C++ task


// Measurement data acquisition event numbers

    static unsigned int gXcpEvent_EcuCyclic = 0;
    static unsigned int gXcpEvent_EcuTask1 = 0;
    static unsigned int gXcpEvent_EcuTask2 = 0;
    static unsigned int gXcpEvent_ActiveEcuTask = 0;


// C++ task instances
#include "ecupp.hpp"
static EcuTask* gEcuTask1 = NULL;
static EcuTask* gEcuTask2 = NULL;
static EcuTask* gActiveEcuTask = NULL;



extern "C" {

    // ECU cyclic (1ms) demo task 
    // Calls C ECU demo code
    void* ecuTask(void* par) {

        printf("Start C demo task ( ecuCyclic() called  every %dns, event = %d )\n", gTaskCycleTimerECU, gXcpEvent_EcuCyclic);
        for (;;) {
            ApplXcpSleepNs(gTaskCycleTimerECU);
            ecuCyclic();

#if defined ( XCP_ENABLE_WIRINGPI )
            digitalWrite(PI_IO_1, HIGH);
#endif

            XcpEvent(gXcpEvent_EcuCyclic); // Trigger measurement date aquisition event for ecuCyclic() task

#if defined ( XCP_ENABLE_WIRINGPI )
            digitalWrite(PI_IO_1, LOW);
#endif
        }
        return 0;
    }
    
    // ECU cyclic (1ms) demo task 
    // Calls C++ ECU demo code
    void* ecuppTask(void* par) {

        printf("Start C++ demo task ( gActiveEcuTask->run() called every %dns, event = %d )\n", gTaskCycleTimerECUpp, gXcpEvent_ActiveEcuTask);
        for (;;) {
            ApplXcpSleepNs(gTaskCycleTimerECUpp);
            // Run the currently active ecu task
            gActiveEcuTask = gActiveEcuTaskId==gXcpEvent_EcuTask1 ? gEcuTask1: gActiveEcuTaskId==gXcpEvent_EcuTask2 ? gEcuTask2 : NULL;
            if (gActiveEcuTask != NULL) {
                gActiveEcuTask->run();

                XcpEventExt(gXcpEvent_ActiveEcuTask, (BYTEPTR)gActiveEcuTask); // Trigger measurement date aquisition event for currently active task
            }
        }
        return 0;
    }
}


// C++ main
int main(void)
{  
    printf(
        "\nXCPlite: XCP on UDP Demo\n"
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
        printf("debug level = %u\n", gXcpDebugLevel);
    }
#endif


    // Create A2L header and events
#ifdef XCP_ENABLE_A2L
    A2lInit(kXcpA2LFilenameString);
    gXcpEvent_EcuCyclic = A2lCreateEvent("EcuCyclic");   // Standard event triggered in C ecuTask
    gXcpEvent_EcuTask1 = A2lCreateEvent("EcuTask1");     // Standard event triggered by C++ ecuTask1 instance
    gXcpEvent_EcuTask2 = A2lCreateEvent("EcuTask2");     // Standard event triggered by C++ ecuTask2 instance
    gXcpEvent_ActiveEcuTask = A2lCreateEvent("activeEcuTask");     // Extended event (relative address objects) triggered by C++ main task
    // Events must be all defined before A2lHeader(), measurements and parameters have to defined afterwards
    A2lHeader();
#endif

    // Initialize the XCP server
    xcpServerInit();


    // C++ demo
    // Initialize ECU demo runnables (C++)
    // Instances are associated to events
    gEcuTask1 = new EcuTask(gXcpEvent_EcuTask1);
    gEcuTask2 = new EcuTask(gXcpEvent_EcuTask2);

    // Create A2L typedef for the class and example static and dynamic instances
#ifdef XCP_ENABLE_A2L
    gEcuTask1->createA2lClassDefinition(); // use any instance of a class to create its typedef
    gEcuTask1->createA2lStaticClassInstance("ecuTask1", "");
    gEcuTask2->createA2lStaticClassInstance("ecuTask2","");
    A2lSetEvent(gXcpEvent_ActiveEcuTask);
    A2lCreateDynamicTypedefInstance("activeEcuTask", "EcuTask", "");
#endif
    gActiveEcuTaskId = gXcpEvent_EcuTask1;


    // C demo
    // Initialize ECU demo task (C) 
    ecuInit();
#ifdef XCP_ENABLE_A2L
    A2lSetEvent(gXcpEvent_EcuCyclic); // Associate XCP event "EcuCyclic" to the variables created below
    ecuCreateA2lDescription();
#endif

    // Create additional A2L parameters to control the demo tasks 
#ifdef XCP_ENABLE_A2L
    A2lCreateParameter(gActiveEcuTaskId, "", "Active ecu task id control");
    A2lCreateParameter(gTaskCycleTimerECU, "ns", "ECU cycle time (ns delay)");
    A2lCreateParameter(gTaskCycleTimerECUpp, "ns", "ECU cycle time (ns delay)");
    A2lParameterGroup("Demo_Parameters", 3, "gActiveEcuTaskId", "gTaskCycleTimerECU", "gTaskCycleTimerECUpp");
#endif

    // Finalize A2L
#ifdef XCP_ENABLE_A2L
    A2lClose();
#endif

#ifndef XCP_WI

    //----------------------------------------------------------------------------------
    // Create the ECU threads
    pthread_t t2;
    int a2 = 0;
    pthread_create(&t2, NULL, ecuTask, (void*)&a2);
    pthread_t t3;
    int a3 = 0;
    pthread_create(&t3, NULL, ecuppTask, (void*)&a3);

    //----------------------------------------------------------------------------------
    // Create the XCP server thread
    pthread_t t1;
    int a1 = 0;
    pthread_create(&t1, NULL, xcpServerThread, (void*)&a1);

    // Exit
    pthread_join(t1, NULL); // wait here, t1 may fail or terminate
    pthread_cancel(t2);
    pthread_cancel(t3);

#endif

    return 0;
}


