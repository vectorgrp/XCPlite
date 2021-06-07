/*----------------------------------------------------------------------------
| File:
|   main.cpp
|
| Description:
|   Demo main for XCP on Ethernet (UDP) XCPlite demo
|   Demo threads in C and C++ to emulate ECU tasks with measurement data acquisistion
|   Linux, Windows 32 and 64 Bit Version for Winsock or XL-API
 ----------------------------------------------------------------------------*/

#include "main.h"

extern "C" {
    extern tXcpData gXcp;
}

// Commandline Options Default
#ifdef _WIN
unsigned char gOptionsXlSlaveMac[6] = XCPSIM_SLAVE_XL_MAC;
unsigned char gOptionsXlSlaveAddr[4] = XCPSIM_SLAVE_XL_IP;
char gOptionsXlSlaveNet[32] = XCPSIM_SLAVE_XL_NET;
char gOptionsXlSlaveSeg[32] = XCPSIM_SLAVE_XL_SEG;
char gOptionA2L_Path[MAX_PATH] = "./";
int gOptionA2L = FALSE;
int gOptionUseXLAPI = FALSE; // Enable Vector XLAPI (1) or WINSOCK (0) as Network Adapter
#else
#define gOptionsXlSlaveMac NULL
#define gOptionsXlSlaveAddr NULL
int gOptionA2L = TRUE;
#endif
unsigned short gOptionsSlavePort = XCPSIM_SLAVE_PORT;
volatile unsigned int gDebugLevel = XCPSIM_DEBUG_LEVEL;


// Demo tasks, cycle times and measurement data acquisition event numbers
volatile vuint32 gTaskCycleTimerECU = 2000; // 2ms Cycle time of the C task
volatile vuint32 gTaskCycleTimerECUpp = 2000; // 2ms  Cycle time of the C++ task
static EcuTask* gEcuTask1 = NULL;
static EcuTask* gEcuTask2 = NULL;
static EcuTask* gActiveEcuTask = NULL;
volatile unsigned int gActiveEcuTaskId = 1; // Task id of the active C++ task

// Events
unsigned short gXcpEvent_EcuCyclic = 0;
unsigned short gXcpEvent_EcuTask1 = 1;
unsigned short gXcpEvent_EcuTask2 = 2;
unsigned short gXcpEvent_ActiveEcuTask = 3;



extern "C" {

    // ECU cyclic (2ms default) demo task 
    // Calls C ECU demo code
    void* ecuTask(void *p) {

        printf("Start C demo task (cycle = %dus, event = %d)\n", gTaskCycleTimerECU, gXcpEvent_EcuCyclic);
        for (;;) {
            sleepNs(gTaskCycleTimerECU*1000);
            ecuCyclic();
        }
        return 0;
    }
    
    // ECU cyclic (2ms default) demo task 
    // Calls C++ ECU demo code
    void* ecuppTask(void *p) {

        printf("Start C++ demo task (cycle = %uus, ext event = %d, size = %u )\n", gTaskCycleTimerECUpp, gXcpEvent_ActiveEcuTask, (vuint32)sizeof(class EcuTask));
        for (;;) {
            sleepNs(gTaskCycleTimerECUpp*1000);
            // Run the currently active ecu task
            gActiveEcuTask = gActiveEcuTaskId==gXcpEvent_EcuTask1 ? gEcuTask1: gActiveEcuTaskId==gXcpEvent_EcuTask2 ? gEcuTask2 : NULL;
            if (gActiveEcuTask != NULL) {
                gActiveEcuTask->run();
                XcpEventExt(gXcpEvent_ActiveEcuTask, (vuint8*)gActiveEcuTask); // Trigger measurement date aquisition event for currently active task
            }
        }
        return 0;
    }



} // C


// Create A2L file
#ifdef XCPSIM_ENABLE_A2L_GEN
int createA2L( const char *path_name ) {

    if (!A2lInit(path_name)) {
        printf("ERROR: Could not create A2L file %s!\n", path_name);
        gOptionA2L = FALSE;
        return 0;
    }
    else {
        A2lHeader();
    }

    assert(gEcuTask1!=NULL); 
    assert(gEcuTask2 != NULL);
    gEcuTask1->createA2lClassDefinition(); // use any instance of a class to create its typedef
    gEcuTask1->createA2lClassInstance("ecuTask1", "ecu task 1");
    gEcuTask2->createA2lClassInstance("ecuTask2", "ecu task 2");
    A2lSetEvent(gXcpEvent_ActiveEcuTask);
    A2lCreateDynamicTypedefInstance("activeEcuTask", "EcuTask", "");

    ecuCreateA2lDescription();

    // Create additional A2L parameters for statistics and control
    A2lCreateParameterWithLimits(gTaskCycleTimerECU, "ECU task cycle time", "us", 50, 1000000);
    A2lCreateParameterWithLimits(gTaskCycleTimerECUpp, "ECUpp task cycle time", "us", 50, 1000000);
    A2lCreateParameterWithLimits(gActiveEcuTaskId, "Active ecu task object id", "", 1, 2);
    A2lParameterGroup("Demo_Params", 3, "gActiveEcuTaskId", "gTaskCycleTimerECU", "gTaskCycleTimerECUpp");

    // Test measurement and parameters
    A2lSetEvent(gXcpEvent_EcuCyclic);
    A2lCreateParameterWithLimits(gDebugLevel, "Console output verbosity", "", 0, 4);
    A2lCreateParameterWithLimits(gFlushCycleMs, "DAQ flush cycle time, 0 = off", "", 0, 1000);
    A2lParameterGroup("Test_Params", 2, "gDebugLevel", "gFlushCycleMs");

    // Finalize A2L description
    A2lClose();

    return 1;
}
#endif


#ifdef _WIN // Windows

// Key handler
static int handleKey(int key) {

    switch (key) {

        // ESC
    case 27:
        return 1;
        break;

    default:
        break;
    }

    return 0;
}

// Main task
void mainTask(void *p) {
    static unsigned long long b0 = 0;
    static unsigned long long bl = 0;
    static unsigned long long t0 = 0;
    static unsigned long long tl = 0;
    for (;;) {
        Sleep(500);

        // Measurement statistics
        if (gDebugLevel <= 1) { 
            if (XcpIsDaqRunning()) {
                unsigned long long dt = gClock64-tl;
                unsigned int db = (unsigned int)(gXcpTl.dto_bytes_written - bl);
                unsigned long long t = (gClock64 - t0) / (1000 * XCP_TIMESTAMP_TICKS_MS);
                unsigned int  n = (unsigned int)((gXcpTl.dto_bytes_written - b0) / 1024) / 1024; // Mbyte
                double r = ((double)db / (1024.0 * 1024.0)) / ((double)dt / 1000000.0); // MByte/s
                unsigned int o = gXcp.DaqOverflowCount;
                printf(" %llus:  %u MByte, %g MByte/s, %u            \r", t, n, r, o);
                bl = gXcpTl.dto_bytes_written;
                tl = gClock64;
            }
            else {
                t0 = tl = gClock64;
                b0 = bl = gXcpTl.dto_bytes_written;
            }
        }

        // Check if the XCP slave is healthy
        if (!gXcpSlaveCMDThreadRunning || !gXcpSlaveDAQThreadRunning) {
            printf("\nXCP slave failed. Exit\n");
            break;
        }

        // Check keyboard
        if (_kbhit()) {
            int c = _getch();
            if (handleKey(c)) {
                XcpSendEvent(EVC_SESSION_TERMINATED, NULL, 0);
                break;
            }
        }
    }
}

#endif

// help
static void usage(void) {

    printf(
        "\n"
        "Usage:\n"
        "\n"
        "  Options:\n"
        "    -tx              Set output verbosity to x (default: 1)\n"
        "    -port <portname> Slave port (default: 5555)\n"
#ifdef _WIN
        "    -v3              Use XL-API V3 (default is WINSOCK port 5555)\n"
        "    -net <netname>   V3 network (default: NET1)\n"
        "    -seg <segname>   V3 segment (default: SEG1)\n"
        "    -ip <ipaddr>     V3 socket IP address (default: 172.31.31.194)\n"
        "    -a2l [path]      Generate A2L file\n"
#endif
        "\n"
        "  Keyboard Commands:\n"
        "    ESC      Exit\n"
        "\n"
    );


}


// C++ main
int main(int argc, char* argv[])
{  
    printf(
#ifndef _WIN
        "\nXCPlite demo for with XCP on Ethernet (Linux 32 Bit Version)\n"
#else
#ifdef _WIN64
        "\nXCPlite64 demo for XCP on Ethernet (64 Bit Version)\n"
#else
        "\nXCPlite32 demo for XCP on Ethernet (32 Bit Version)\n"
#endif
#endif
        "Vector Informatik GmbH 2021\n"
        "Build " __DATE__ " " __TIME__ "\n\n"
        "WARNING: Sorry, create CANape device from A2L is not Plug&Play yet!\n"
        "         Expert Settings Event List: Disable Multicast\n"
        "         Expert Settings Transport Layer: Exclude Command Response!\n\n"
    );
     

    // Parse commandline
    for (int i = 1; i < argc; i++) {
        char c;
        if (strcmp(argv[i], "-h") == 0) {
            usage();
            exit(0);
        }
        else if (sscanf_s(argv[i], "-t%c", &c, 1) == 1) {
            gDebugLevel = c - '0';
            printf("Set screen output verbosity to %u\n", gDebugLevel);
        }
        else if (strcmp(argv[i], "-port") == 0) {
            if (++i < argc) {
                if (sscanf_s(argv[i], "%hu", &gOptionsSlavePort) == 1) {
                    printf("Set port to %u\n", gOptionsSlavePort);
                }
            }
        }

#ifdef _WIN
        else if (strcmp(argv[i], "-a2l") == 0) {
            gOptionA2L = TRUE;
            if (i+1 < argc && argv[i+1][0]!='-') {
                strcpy_s(gOptionA2L_Path, argv[++i]);
            }
            printf("Generate A2L file at %s\n",gOptionA2L_Path);
        }
        else if (strcmp(argv[i], "-v3") == 0) {
            gOptionUseXLAPI = TRUE;
            printf("Using XL-API V3\n");
        }
        else if (strcmp(argv[i], "-ip") == 0) {
            if (++i < argc) {
                if (inet_pton(AF_INET, argv[i], &gOptionsXlSlaveAddr)) {
                    printf("Set ip addr to %s\n", argv[i]);
                }
            }
        }
        else if (strcmp(argv[i], "-net") == 0) {
            gOptionUseXLAPI = TRUE;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                strcpy_s(gOptionsXlSlaveNet, argv[++i]);
                printf("Set net to %s\n", argv[i]);
            }
        }


#endif
        else {
            usage();
            exit(0);
        }

    }
    printf("\n");


    // Initialize clock for DAQ event time stamps
    if (!clockInit()) return 1;

    // Initialize the XCP slave
    if (!xcpSlaveInit(gOptionsXlSlaveMac, gOptionsXlSlaveAddr, gOptionsSlavePort, 0 )) return 1;
    
    // Create XCP events
    // Events must be all defined before A2lHeader() is called, measurements and parameters have to be defined after all events have been defined !!
    // Character count should be <=8 to keep the A2L short names unique !
#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    printf("\n");
    gXcpEvent_EcuCyclic = XcpCreateEvent("Cyclic", 2000, 0, 0);                               // Standard event triggered in C ecuTask
    gXcpEvent_EcuTask1 = XcpCreateEvent("Task1", 2000, 0, sizeof(EcuTask));                   // Extended event triggered by C++ ecuTask1 instance
    gXcpEvent_EcuTask2 = XcpCreateEvent("Task2", 2000, 0, sizeof(EcuTask));                   // Extended event triggered by C++ ecuTask2 instance
    gXcpEvent_ActiveEcuTask = XcpCreateEvent("TaskAct", 0, 0, sizeof(class EcuTask));      // Extended event triggered by C++ main task for a pointer to an EcuTask instance

#endif

    // C++ demo
    // Initialize ECU demo runnables (C++)
    // Instances are associated to events
    gEcuTask1 = new EcuTask(gXcpEvent_EcuTask1);
    gEcuTask2 = new EcuTask(gXcpEvent_EcuTask2);
    gActiveEcuTaskId = gXcpEvent_EcuTask1;

    // C demo
    // Initialize ECU demo task (C) 
    ecuInit();


// Generate A2L file
#ifdef XCPSIM_ENABLE_A2L_GEN 
#ifdef _WIN
    if (gOptionA2L) { 
        ApplXcpGetA2LFilename(NULL, NULL,0);
    }
#else
    {
        char *p;
        ApplXcpGetA2LFilename((vuint8**)&p, NULL, 1);
        createA2L(p);
    }
#endif
#endif

#ifndef _WIN // Linux

    // Demo threads
    pthread_t t3;
    int a3 = 0;
    pthread_create(&t3, NULL, ecuppTask, (void*)&a3);
    pthread_t t2;
    int a2 = 0;
    pthread_create(&t2, NULL, ecuTask, (void*)&a2);

    // XCP DAQ queue transmit thread
    pthread_t t1;
    int a1 = 0;
    pthread_create(&t1, NULL, xcpSlaveDAQThread, (void*)&a1);

    // XCP server thread
    pthread_t t0;
    int a0 = 0;
    pthread_create(&t0, NULL, xcpSlaveCMDThread, (void*)&a0);

    // Exit
    pthread_join(t0, NULL); // wait here, xcpSlaveCMDThread may terminate on error
    
    pthread_cancel(t1);
    pthread_cancel(t2);
    pthread_cancel(t2);
    pthread_cancel(t3);

#else

    printf("\n");

    // Demo threads
    std::thread t2([]() { ecuTask(0); });
    std::thread t3([]() { ecuppTask(0); });

     // XCP server thread
    std::thread t11([]() { xcpSlaveDAQThread(0); });
    std::thread t1([]() { xcpSlaveCMDThread(0); });

    Sleep(100); // give everything a chance to be up and running
    printf("\nPress ESC to stop\n");
    std::thread t0([]() { mainTask(0); });
    t0.join(); // wait here, t0 may terminate on key ESC or when the XCP threads terminate

    t11.detach();
    t1.detach();
    t2.detach();
    t3.detach();

#endif
    
    printf("Shutdown\n");
    xcpSlaveShutdown();
        
    return 0;
}


