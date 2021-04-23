/*----------------------------------------------------------------------------
| File:
|   main.cpp
|
| Description:
|   Demo main for XCP on Ethernet (UDP)
|   Demo threads in C and C++ to emulate ECU tasks with measurement data acquisistion
|   Linux (Raspberry Pi) or Windows (x64) Version
 ----------------------------------------------------------------------------*/

#include <typeinfo>
#include <thread>


// XCP driver
#include "xcpLite.h"
#include "xcpAppl.h"
#include "A2L.h"

// ECU simulation (C demo)
#include "ecu.h"

// ECU simulation (C++ demo)
#include "ecupp.hpp"
static EcuTask* gEcuTask1 = NULL; // C++ task instances
static EcuTask* gEcuTask2 = NULL;
static EcuTask* gActiveEcuTask = NULL;
volatile unsigned int gActiveEcuTaskId = 0; // Task id of the active C++ task


// Task delays
volatile vuint32 gTaskCycleTimerECU    = 2000; // 2ms Cycle time of the C task
volatile vuint32 gTaskCycleTimerECUpp  = 2000; // 2ms  Cycle time of the C++ task


// Measurement data acquisition event numbers
unsigned int gXcpEvent_EcuCyclic = 0;
unsigned int gXcpEvent_EcuCyclic_packed = 0;
unsigned int gXcpEvent_EcuTask1 = 0;
unsigned int gXcpEvent_EcuTask2 = 0;
unsigned int gXcpEvent_ActiveEcuTask = 0;

// Stresstest
#ifdef XCP_ENABLE_STRESSTEST // Enable measurement stress generator
#include "ecustress.h"  
volatile vuint32 gTaskCycleTimerStress = 2000; // 2ms  Cycle time of the C++ task
unsigned int gXcpEvent_EcuStress = 0;
#endif



extern "C" {

    // ECU cyclic (2ms default) demo task 
    // Calls C ECU demo code
    void* ecuTask(void* par) {

        printf("Start C demo task ( ecuCyclic() called  every %dus, event = %d )\n", gTaskCycleTimerECU, gXcpEvent_EcuCyclic);
        for (;;) {
            ApplXcpSleepNs(gTaskCycleTimerECU*1000);
            ecuCyclic();
            XcpEvent(gXcpEvent_EcuCyclic); // Trigger measurement data aquisition event for ecuCyclic() task
            XcpEvent(gXcpEvent_EcuCyclic_packed); // Trigger a packed mode measurement data aquisition event for ecuCyclic() task
        }
        return 0;
    }
    
    // ECU cyclic (2ms default) demo task 
    // Calls C++ ECU demo code
    void* ecuppTask(void* par) {

        printf("Start C++ demo task ( gActiveEcuTask->run() called every %dus, event = %d )\n", gTaskCycleTimerECUpp, gXcpEvent_ActiveEcuTask);
        for (;;) {
            ApplXcpSleepNs(gTaskCycleTimerECUpp*1000);
            // Run the currently active ecu task
            gActiveEcuTask = gActiveEcuTaskId==gXcpEvent_EcuTask1 ? gEcuTask1: gActiveEcuTaskId==gXcpEvent_EcuTask2 ? gEcuTask2 : NULL;
            if (gActiveEcuTask != NULL) {
                gActiveEcuTask->run();
                XcpEventExt(gXcpEvent_ActiveEcuTask, (BYTEPTR)gActiveEcuTask); // Trigger measurement date aquisition event for currently active task
            }
        }
        return 0;
    }

    // Stresstest
#ifdef XCP_ENABLE_STRESSTEST // Enable measurement stress generator

    // ECU cyclic (2ms default) stress generator task 
    void* ecuStressTask(void* par) {

        printf("Start stress task ( ecuStress() called  every %dus, event = %d )\n", gTaskCycleTimerStress, gXcpEvent_EcuStress);
        for (;;) {
            ApplXcpSleepNs(gTaskCycleTimerStress * 1000);
            ecuStressCyclic();
            XcpEvent(gXcpEvent_EcuStress);
        }
        return 0;
    }
#endif

} // C


// C++ main
int main(void)
{  
    printf(
        "\nXCPlite: XCP on UDP Demo\n"
        "Build " __DATE__ " " __TIME__ 
#ifndef _WIN
        " for Linux ARM32\n"
#else
        " for Windows x86\n"
#endif
#ifdef XCP_ENABLE_STRESS
        "  Option STRESS\n"
#endif
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
    if (!ApplXcpClockInit()) return 1;

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
        printf("&gXcpDebugLevel = 0x%llX\n", (vuint64)&gXcpDebugLevel);
    }
#endif


    // Create A2L events
    // Events must be all defined before A2lHeader(), measurements and parameters have to defined afterwards
#ifdef XCP_ENABLE_A2L
    A2lInit(kXcpA2LFilenameString);
    gXcpEvent_EcuCyclic = A2lCreateEvent("EcuCyclic",2000,0);   // Standard event triggered in C ecuTask
    gXcpEvent_EcuTask1 = A2lCreateEvent("EcuTask1", 2000, 0);     // Standard event triggered by C++ ecuTask1 instance
    gXcpEvent_EcuTask2 = A2lCreateEvent("EcuTask2", 2000, 0);     // Standard event triggered by C++ ecuTask2 instance
    gXcpEvent_ActiveEcuTask = A2lCreateEvent("activeEcuTask", 0, 0);     // Extended event (relative address objects) triggered by C++ main task
    gXcpEvent_EcuCyclic_packed = A2lCreateEvent("EcuCyclicP", 20, 100);   // Packed event triggered in C ecuTask 100 samples in 2ms = 20us rate
    gXcpEvent_EcuStress = A2lCreateEvent("EcuStress", 2000, 0);   
#endif

    // Create A2L header
#ifdef XCP_ENABLE_A2L
    A2lHeader();
#endif

    // Initialize the XCP server
    if (!xcpTransportLayerInit()) return 1;

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
    ecuCreateA2lDescription();
#endif

    // Create additional A2L parameters to control the demo  
#ifdef XCP_ENABLE_A2L
    A2lCreateParameterWithLimits(gActiveEcuTaskId,"Active ecu task object id","",2,3);
    A2lCreateParameterWithLimits(gTaskCycleTimerECU,"ECU task cycle time","us",50,1000000);
    A2lCreateParameterWithLimits(gTaskCycleTimerECUpp,"ECUpp task cycle time","us",50,1000000);
    A2lCreateParameterWithLimits(gXcpDebugLevel,"Console output verbosity","us",0,4);
    A2lParameterGroup("Demo_Parameters", 4, "gActiveEcuTaskId", "gTaskCycleTimerECU", "gTaskCycleTimerECUpp", "gXcpDebugLevel");
#endif

    // Stress
#ifdef XCP_ENABLE_STRESSTEST // Enable measurement stress generator
    A2lCreateParameterWithLimits(gTaskCycleTimerStress, "ECUstress task cycle time", "us", 50, 1000000);
    ecuStressInit();
    ecuStressCreateA2lDescription();
#endif

    // Finalize A2L
#ifdef XCP_ENABLE_A2L
    A2lClose();
#endif

#ifndef _WIN // Linux

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
    pthread_create(&t1, NULL, xcpTransportLayerThread, (void*)&a1);

    // Exit
    pthread_join(t1, NULL); // wait here, t1 may fail or terminate
    pthread_cancel(t2);
    pthread_cancel(t3);

#else

#ifdef XCP_ENABLE_STRESSTEST // Enable measurement stress generator
    std::thread t4([]() { ecuStressTask(0); });
#endif
    std::thread t2([]() { ecuTask(0); });
    std::thread t3([]() { ecuppTask(0); });
    std::thread t1([]() { xcpTransportLayerThread(0); });
    t1.join();
    
#endif

    return 0;
}


