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

#include "platform.h"
#include "main_cfg.h"
#include "util.h"
#include "clock.h"
#ifdef APP_ENABLE_A2L_GEN
#include "A2L.h"
#endif
#include "xcpTl.h"
#include "xcpLite.h"
#include "xcpSlave.h"
#include "ecu.h" // Demo measurement task C
#include "ecupp.hpp" // Demo measurement task C++

//-----------------------------------------------------------------------------------------------------

// Commandline Options amd Defaults

volatile unsigned int gDebugLevel = APP_DEFAULT_DEBUGLEVEL;
char gOptionA2L_Path[MAX_PATH] = APP_DEFAULT_A2L_PATH;
int gOptionJumbo = APP_DEFAULT_JUMBO;
uint16_t gOptionSlavePort = APP_DEFAULT_SLAVE_PORT; 
unsigned char gOptionSlaveAddr[4] = APP_DEFAULT_SLAVE_ADDR;

static uint16_t gXcpEvent_EcuTest = 0;

// Create A2L file
#ifdef APP_ENABLE_A2L_GEN

int createA2L(const char* a2l_path_name) {

    if (!A2lInit(a2l_path_name)) return 0;

    A2lHeader();

    ecuCreateA2lDescription();
    ecuppCreateA2lDescription();

    A2lCreateParameterWithLimits(gDebugLevel, "Console output verbosity", "", 0, 100);

    // Finalize A2L description
    A2lClose();
    return 1;
}
#endif


// Main task
#ifdef _WIN
DWORD WINAPI mainTask(LPVOID lpParameter)
#else
extern void* mainTask(void* par)
#endif
{

    for (;;) {

        XcpEvent(gXcpEvent_EcuTest);
        sleepMs(10);

        // Check if the XCP slave is running
        int err = XcpSlaveStatus();
        if (err) {
            printf("\nXCP slave failed (err==%u)\n",err);
            break;
        }

        // Check keyboard
        if (_kbhit()) {
            int c = _getch();
            if (c == 27) {
                XcpSendEvent(EVC_SESSION_TERMINATED, NULL, 0);
                break;
            }
        }
    }

    return 0;
}


// help
static void usage() {
    printf(
        "\n"
        "Usage:\n"
        "  " APP_NAME " [options]\n"
        "\n"
        "  Options:\n"
        "    -tx              Set output verbosity to x (default: 1)\n"
        "    -addr <ipaddr>   IP address (default: ANY)\n"
        "    -port <portname> Slave port (default: 5555)\n"
        "    -jumbo           Disable Jumbo Frames\n"
        "    -a2l [path]      Generate A2L file\n"
        "\n"
    );
}


// C++ main
int main(int argc, char* argv[]) {

    printf("\n" APP_NAME " - ECU simulator with XCP on Ethernet\n"
#if defined(_WIN64) || defined(_LINUX64)
        "64 Bit Version\n"
#endif
        "Version %s  Build " __DATE__ " " __TIME__ "\n\n", APP_VERSION);

    // Parse commandline
    for (int i = 1; i < argc; i++) {
        char c;
        if (strcmp(argv[i], "-h") == 0) {
            usage();
            exit(0);
        }
        else if (sscanf(argv[i], "-t%c", &c) == 1) {
            gDebugLevel = c - '0';
        }
        else if (strcmp(argv[i], "-addr") == 0) {
            if (++i < argc) {
                if (inet_pton(AF_INET, argv[i], &gOptionSlaveAddr)) {
                    printf("Set ip addr to %s\n", argv[i]);
                }
            }
        }
        else if (strcmp(argv[i], "-port") == 0) {
            if (++i < argc) {
                if (sscanf(argv[i], "%hu", &gOptionSlavePort) == 1) {
                    printf("Set XCP port to %u\n", gOptionSlavePort);
                }
            }
        }
        else if (strcmp(argv[i], "-a2l") == 0) {
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                strcpy(gOptionA2L_Path, argv[++i]);
                printf("Generate A2L/MDI file at %s\n", gOptionA2L_Path);
            }
        }
        else if (strcmp(argv[i], "-jumbo") == 0) {
            gOptionJumbo = !APP_DEFAULT_JUMBO;
        }

    }

    if (gDebugLevel) printf("Set screen output verbosity to %u\n", gDebugLevel);
    if (gOptionJumbo) printf("Using Jumbo Frames\n");
    printf("\n");

    // Init network
    if (socketStartup()) {

        // Init clock 
        if (clockInit()) {

            // Initialize the XCP slave
            if (XcpSlaveInit(gOptionSlaveAddr, gOptionSlavePort, gOptionJumbo ? XCPTL_SOCKET_JUMBO_MTU_SIZE : XCPTL_SOCKET_MTU_SIZE, 200)) {
                
                sleepMs(200UL); // Not needed, just to get the debug printing complete
                printf("\n");

                // Initialize ECU demo task (C) 
                ecuInit();

                // Initialize ECU demo tasks (C++) 
                ecuppInit();

                gXcpEvent_EcuTest = XcpCreateEvent("Test", 10000, 0, 0);

#ifdef APP_ENABLE_A2L_GEN
                // Create A2L/MDI names and generate A2L file
                printf("\n");
                char* filepath; // Full path + name +extension
                ApplXcpGetA2LFilename(&filepath, NULL, 1);
                createA2L(filepath);
#endif
                printf("\n");

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

                XcpSlaveShutdown();

            }
        }

        socketShutdown();
    }

    printf("\nPress any key to close\n");
    while (!_kbhit()) sleepMs(100);
    return 0;
}


