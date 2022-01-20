/*----------------------------------------------------------------------------
| File:
|   main.cpp
|
| Description:
|   Demo main for XCP on Ethernet (UDP)
|   Demo threads in C and C++ to emulate ECU tasks with measurement data acquisistion
|   Windows 32 and 64 Bit Version
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/


#include "main.h"
#include "platform.h"
#include "clock.h"
#include "xcpTl.h"
#include "xcpLite.h"
#include "xcpServer.h"
#ifdef APP_ENABLE_A2L_GEN
#include "A2L.h"
#endif


//------------------------------
// Demo

#include "ecu.h" // Demo measurement task C
#include "ecupp.hpp" // Demo measurement task C++


//-----------------------------------------------------------------------------------------------------
// Test

unsigned int gDebugLevel = APP_DEFAULT_DEBUGLEVEL; // Debug print verbosity level

//-----------------------------------------------------------------------------------------------------
// Options

// Commandline Options amd Defaults
BOOL gOptionTCP = FALSE;
uint16_t gOptionServerPort = APP_DEFAULT_SERVER_PORT;
uint8_t gOptionServerAddr[4] = APP_DEFAULT_SERVER_ADDR;

// A2L name and path
#ifdef APP_ENABLE_A2L_GEN
char gOptionA2L_Path[MAX_PATH] = "";
#endif



//-----------------------------------------------------------------------------------------------------

// Create A2L file
#ifdef APP_ENABLE_A2L_GEN

// Callback for address conversion
extern "C" {
    static uint32_t getAddr(uint8_t* p) {
        return ApplXcpGetAddr(p);
    }
}

// Create A2L file
static int createA2L(const char* a2l_path_name) {

    if (!A2lInit(a2l_path_name, getAddr)) return 0;
    A2lHeader(ApplXcpGetName(), gOptionTCP, gOptionServerAddr, gOptionServerPort);
    ecuCreateA2lDescription();
    ecuppCreateA2lDescription();

    A2lCreateParameterWithLimits(gDebugLevel, "Console output verbosity", "", 0, 100);
    A2lClose();
    return 1;
}
#endif

//-----------------------------------------------------------------------------------------------------

// Main task
#ifdef _WIN
DWORD WINAPI mainTask(LPVOID par)
#else
extern void* mainTask(void* par)
#endif
{
    (void)par;
    for (;;) {

        sleepMs(100);

        // Check if the XCP server is running
        BOOL err = XcpServerStatus();
        if (err) {
            printf("\nXCP Server failed (err==%u)\n", err);
            break;
        }

        // Check keyboard
        if (_kbhit()) {
            int c = _getch();
            if (c == 27) {
                //XcpSendEvent(EVC_SESSION_TERMINATED, NULL, 0);
                break;
            }
        }
    }

    return 0;
}


//-----------------------------------------------------------------------------------------------------
// main


// help
static void usage( const char *appName) {
    printf(
        "\n"
        "Usage:\n"
        "  %s [options]\n"
        "\n"
        "  Options:\n"
        "    -dx              Set output verbosity to x (default: 1)\n"
        "    -bind <ipaddr>   IP address for socket bind (default: ANY)\n"
        "    -port <portname> Server port (default: 5555)\n"
        "    -tcp             Use TCP\n"
#ifdef APP_ENABLE_A2L_GEN
        "    -a2l [path]      Generate .a2l file at path\n"
#endif
        "\n", appName
    );
}


int main(int argc, char* argv[]) {

    printf("\n%s - ECU simulator with XCP on Ethernet\n"
#if defined(_WIN64) || defined(_LINUX64)
        "64 Bit Version\n"
#endif
        "Build " __DATE__ " " __TIME__ "\n\n", ApplXcpGetName());

    // Parse commandline
    for (int i = 1; i < argc; i++) {
        char c;
        if (strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            exit(0);
        }
        else if (sscanf(argv[i], "-d%c", &c) == 1) {
            gDebugLevel = c - '0';
        }
        else if (strcmp(argv[i], "-bind") == 0) {
            if (++i < argc) {
                if (inet_pton(AF_INET, argv[i], &gOptionServerAddr)) {
                    printf("Set ip addr for bind to %s\n", argv[i]);
                }
            }
        }
        else if (strcmp(argv[i], "-port") == 0) {
            if (++i < argc) {
                if (sscanf(argv[i], "%hu", &gOptionServerPort) == 1) {
                    printf("Set XCP port to %u\n", gOptionServerPort);
                }
            }
        }
#ifdef APP_ENABLE_A2L_GEN
        else if (strcmp(argv[i], "-a2l") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                strcpy(gOptionA2L_Path, argv[++i]);
                printf("Generate A2L file at %s\n", gOptionA2L_Path);
            }
        }
#endif
#ifdef XCPTL_ENABLE_TCP
        else if (strcmp(argv[i], "-tcp") == 0) {
            gOptionTCP = TRUE;
        }
#endif
        else {
            printf("Unknown command line option %s\n", argv[i]);
            return 0;
        }
    }

    if (gDebugLevel) printf("Set screen output verbosity to %u\n", gDebugLevel);

#ifdef XCPTL_ENABLE_TCP
    if (gOptionTCP) printf("Using TCP socket\n");
#endif

    // Init network
    if (!socketStartup()) return 0;

    // Init clock
    if (!clockInit()) return 0;

    // Initialize the XCP Server
    if (!XcpServerInit((const uint8_t*)gOptionServerAddr, gOptionServerPort, gOptionTCP)) return 0;

    sleepMs(200UL); // Not needed, just to get the debug printing complete
    printf("\n");

    // Initialize ECU demo task (C)
    ecuInit();

    // Initialize ECU demo tasks (C++)
    ecuppInit();

#ifdef APP_ENABLE_A2L_GEN // Enable A2L generation
    createA2L(ApplXcpGetA2lFileName());
    printf("\n");
#endif

#ifdef _LINUX

    // Demo threads
    pthread_t t3;
    int a3 = 0;
    pthread_create(&t3, NULL, ecuppTask, (void*)&a3);
    pthread_t t2;
    int a2 = 0;
    pthread_create(&t2, NULL, ecuTask, (void*)&a2);

    // Main loop
    pthread_t t1;
    int a1 = 0;
    pthread_create(&t1, NULL, mainTask, (void*)&a1);

    // Exit
    sleepMs(1000); // give everything a chance to be up and running
    printf("\nPress ESC to stop\n");
    pthread_join(t1, NULL); // wait here, main loop terminates on key ESC or when the XCP threads terminate
    pthread_cancel(t2);
    pthread_cancel(t3);

#endif // _LINUX

#ifdef _WIN

    // Demo threads
    std::thread t2([]() { ecuTask(0); });
    std::thread t3([]() { ecuppTask(0); });

    // Main loop
    sleepMs(100); // give everything a chance to be up and running
    printf("\nPress ESC to stop\n");
    std::thread t0([]() { mainTask(0); });

    // Exit
    t0.join(); // wait here, main loop terminates on key ESC or when the XCP threads terminate
    t2.detach();
    t3.detach();

#endif // _WIN

    XcpServerShutdown();
            
    socketCleanup();

    printf("\nApplication terminated. Press any key to close\n");
    while (!_kbhit()) sleepMs(100);
    return 1;
}
