/*----------------------------------------------------------------------------
| File:
|   main.cpp
|
| Description:
|   Demo main for XCP on Ethernet (UDP) demo
|   Demo threads in C and C++ to emulate ECU tasks with measurement data acquisistion
|   Windows and Linux 32 and 64 Bit Version
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "main.h"

// Commandline Options amd Defaults
#ifdef _WIN
#ifdef XCPSIM_ENABLE_XLAPI_V3
unsigned char gOptionsSlaveMac[6] = XCPSIM_SLAVE_XL_MAC;
unsigned char gOptionsSlaveAddr[4] = XCPSIM_SLAVE_XL_IP;
int gOptionUseXLAPI = 0; 
char gOptionsXlSlaveNet[32] = XCPSIM_SLAVE_XL_NET;
char gOptionsXlSlaveSeg[32] = XCPSIM_SLAVE_XL_SEG;
#endif
#endif // _WIN
#ifdef _LINUX
unsigned char gOptionsSlaveMac[6] = { 0,0,0,0,0,0 };
unsigned char gOptionsSlaveAddr[4] = { 127,0,0,1 };
#endif // _LINUX


int gOptionA2L = 0;
char gOptionA2L_Path[MAX_PATH] = "./";
int gOptionJumbo = 0;
uint16_t gOptionsSlavePort = XCPSIM_SLAVE_PORT;
volatile unsigned int gDebugLevel = XCPSIM_DEBUG_LEVEL;


// Create A2L file
#ifdef XCPSIM_ENABLE_A2L_GEN
int createA2L( const char *path_name ) {

    if (!A2lInit(path_name)) {
        gOptionA2L = FALSE;
        return 0;
    }
    else {
        A2lHeader();
    }
    ecuCreateA2lDescription();
    ecuppCreateA2lDescription();
    A2lCreateParameterWithLimits(gDebugLevel, "Console output verbosity", "", 0, 4);
    A2lCreateParameterWithLimits(gFlushCycleMs, "DAQ flush cycle time, 0 = off", "", 0, 1000);
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
    
    for (;;) {
        Sleep(500);

        // Check if the XCP slave is healthy
        if (!gXcpSlaveCMDThreadRunning 
#ifndef XCPSIM_SINGLE_THREAD_SLAVE
            || !gXcpSlaveDAQThreadRunning
#endif
            ) {
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

#endif // _WIN

// help
static void usage(void) {

    printf(
        "\n"
        "Usage:\n"
#ifdef _WIN64
        "  XCPlite64 [options]\n"
#endif
#ifdef _WIN32
        "  XCPlite32 [options]\n"
#endif
#ifdef _LINUX
        "  XCPlite [options]\n"
#endif

        "\n"
        "  Options:\n"
        "    -tx              Set output verbosity to x (default: 1)\n"
        "    -port <portname> Slave port (default: 5555)\n"
        "    -a2l [path]      Generate A2L file\n"
        "    -jumbo           Enable Jumbo Frames\n"

#ifdef _WIN
        "    -v3              Use XL-API V3 (default is WINSOCK port 5555)\n"
        "    -net <netname>   V3 network (default: NET1)\n"
        "    -seg <segname>   V3 segment (default: SEG1)\n"
        "    -ip <ipaddr>     V3 socket IP address (default: 172.31.31.194)\n"

#endif // _WIN

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
        "\nXCPlite Demo - ECU Simulator with XCP on Ethernet (UDP)\n"
        "Vector Informatik GmbH 2021\n"
        "Build " __DATE__ " " __TIME__ "\n\n"
    );
     
    // Print activated application options
    printf("Options:\n");
#ifdef XCPSIM_SINGLE_THREAD_SLAVE
    printf("XCPSIM_SINGLE_THREAD_SLAVE,");
#endif
#ifdef XCPSIM_ENABLE_XLAPI_V3
    printf("XCPSIM_ENABLE_XLAPI_V3,");
#endif
#ifdef XCPSIM_SINGLE_THREAD_SLAVE
    printf("XCPSIM_SINGLE_THREAD_SLAVE,");
#endif 
#ifdef XCPSIM_ENABLE_A2L_GEN 
    printf("XCPSIM_ENABLE_A2L_GEN,");
#endif
#ifdef CLOCK_USE_APP_TIME_US 
    printf("CLOCK_USE_APP_TIME_US");
#endif
#ifdef CLOCK_USE_UTC_TIME_NS 
    printf("CLOCK_USE_UTC_TIME_NS");
#endif
    printf("\n");


    // Parse commandline
    for (int i = 1; i < argc; i++) {
        char c;
        if (strcmp(argv[i], "-h") == 0) {
            usage();
            exit(0);
        }
        #ifdef _LINUX
        else if (sscanf(argv[i], "-t%c", &c) == 1) {
        #else
        else if (sscanf_s(argv[i], "-t%c", &c, 1) == 1) {
            #endif
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
        else if (strcmp(argv[i], "-a2l") == 0) {
            gOptionA2L = TRUE;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                strcpy_s(gOptionA2L_Path, argv[++i]);
            }
            printf("Generate A2L file at %s\n", gOptionA2L_Path);
        }
        else if (strcmp(argv[i], "-jumbo") == 0) {
            printf("Enable Jumbo Frames\n");
            gOptionJumbo = TRUE;
        }

#ifdef XCPSIM_ENABLE_XLAPI_V3
        else if (strcmp(argv[i], "-v3") == 0) {
            gOptionUseXLAPI = TRUE;
            printf("Using XL-API V3\n");
        }
        else if (strcmp(argv[i], "-ip") == 0) {
            if (++i < argc) {
                if (inet_pton(AF_INET, argv[i], &gOptionsSlaveAddr)) {
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


    if (!clockInit()) return 1;

    // Initialize the XCP slave
#ifdef XCPSIM_ENABLE_XLAPI_V3
    if (!xcpSlaveInit(gOptionsSlaveMac, gOptionsSlaveAddr, gOptionsSlavePort, gOptionJumbo )) return 1;
#else
    if (!xcpSlaveInit(NULL, NULL, gOptionsSlavePort, gOptionJumbo)) return 1;
#endif

    // C demo
    // Initialize ECU demo task (C) 
    ecuInit();
        
    // C++ demo
    // Initialize ECU demo tasks (C++) 
    ecuppInit();
    

#ifdef _LINUX

    { // Always in Linux Version
        char* p;
        ApplXcpGetA2LFilename((vuint8**)&p, NULL, 1);
        createA2L(p);
    }

    // Demo threads
    pthread_t t3;
    int a3 = 0;
    pthread_create(&t3, NULL, ecuppTask, (void*)&a3);
    pthread_t t2;
    int a2 = 0;
    pthread_create(&t2, NULL, ecuTask, (void*)&a2);

    // XCP DAQ queue transmit thread
#ifndef XCPSIM_SINGLE_THREAD_SLAVE
    pthread_t t1;
    int a1 = 0;
    pthread_create(&t1, NULL, xcpSlaveDAQThread, (void*)&a1);
#endif

    // XCP server thread
    pthread_t t0;
    int a0 = 0;
    pthread_create(&t0, NULL, xcpSlaveCMDThread, (void*)&a0);

    // Exit
    pthread_join(t0, NULL); // wait here, xcpSlaveCMDThread may terminate on error
    
#ifndef XCPSIM_SINGLE_THREAD_SLAVE
    pthread_cancel(t1);
#endif
    pthread_cancel(t2);
    pthread_cancel(t3);

#endif // _LINUX
#ifdef _WIN

    // Generate A2L file
    if (gOptionA2L) {
        ApplXcpGetA2LFilename(NULL, NULL, 0);
    }
    printf("\n");


    // Demo threads
    std::thread t2([]() { ecuTask(0); });
    std::thread t3([]() { ecuppTask(0); });

    // XCP server thread
    Sleep(100); // give everything a chance to be up and running
#ifndef XCPSIM_SINGLE_THREAD_SLAVE
    std::thread t11([]() { xcpSlaveDAQThread(0); });
    Sleep(100); // give everything a chance to be up and running
#endif
    std::thread t1([]() { xcpSlaveCMDThread(0); });

    Sleep(100); // give everything a chance to be up and running
    printf("\nPress ESC to stop\n");
    std::thread t0([]() { mainTask(0); });
    t0.join(); // wait here, t0 may terminate on key ESC or when the XCP threads terminate

#ifndef XCPSIM_SINGLE_THREAD_SLAVE
    t11.detach();
#endif
    t1.detach();
    t2.detach();
    t3.detach();


#endif // _WIN
    
    printf("Shutdown\n");
    xcpSlaveShutdown();
        
    return 0;
}


