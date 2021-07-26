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


// Commandline Options amd Defaults
volatile unsigned int gDebugLevel = APP_DEFAULT_DEBUGLEVEL;
char gOptionA2L_Path[MAX_PATH] = APP_DEFAULT_A2L_PATH;
int gOptionJumbo = APP_DEFAULT_JUMBO;
unsigned char gOptionSlaveAddr[4] = { 127,0,0,1 };
uint16_t gOptionSlavePort = APP_DEFAULT_SLAVE_PORT;
int gOptionUseXLAPI = FALSE;

#ifdef _WIN 
#ifdef APP_ENABLE_XLAPI_V3
char gOptionXlSlaveNet[32] = APP_DEFAULT_SLAVE_XL_NET;
char gOptionXlSlaveSeg[32] = APP_DEFAULT_SLAVE_XL_SEG;
#endif
#endif 


// Infos needed by createA2L()
char* getA2lSlaveIP() {
    static char tmp[32];
    inet_ntop(AF_INET, &gXcpTl.SlaveAddr.addr.sin_addr, tmp, sizeof(tmp));
    return tmp;
}

uint16_t getA2lSlavePort() {
    return htons(gXcpTl.SlaveAddr.addr.sin_port);
}

// Create A2L file
// Create MDF header for all measurements
#ifdef APP_ENABLE_A2L_GEN
int createA2L(const char* a2l_path_name) {

    if (!A2lInit(a2l_path_name)) return 0;
    A2lHeader();
    ecuCreateA2lDescription();
    ecuppCreateA2lDescription();
    A2lCreateParameterWithLimits(gDebugLevel, "Console output verbosity", "", 0, 100);
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
        
        sleepMs(1000);

        // Check if the XCP threads are is running
        if (!gXcpSlaveCMDThreadRunning || !gXcpSlaveDAQThreadRunning) {
            printf("\nXCP slave failed (CMD=%u, DAQ=%u)\n",gXcpSlaveCMDThreadRunning, gXcpSlaveDAQThreadRunning);
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
        "    -ip <ipaddr>     IP address of prefered adapter or for XL-API V3 socket (default: 172.31.31.194)\n"
        "    -port <portname> Slave port (default: 5555)\n"
#if APP_DEFAULT_JUMBO==1
        "    -jumbo           Disable Jumbo Frames\n"
#endif
        "    -a2l [path]      Generate A2L file\n"
#ifdef APP_ENABLE_XLAPI_V3
        "    -v3              Use XL-API V3 (default is WINSOCK port 5555)\n"
        "    -net <netname>   V3 network (default: NET1)\n"
        "    -seg <segname>   V3 segment (default: SEG1)\n"
        "    -pcap <file>     V3 log to PCAP file\n"
#endif 

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
            gOptionJumbo = FALSE;
        }
        else if (strcmp(argv[i], "-ip") == 0) {
            if (++i < argc) {
                if (inet_pton(AF_INET, argv[i], &gOptionSlaveAddr)) {
                    printf("Set ip addr to %s\n", argv[i]);
                }
            }
        }
#ifdef APP_ENABLE_XLAPI_V3
        else if (strcmp(argv[i], "-v3") == 0) {
            uint8_t a[4] = APP_DEFAULT_SLAVE_IP;
            memcpy(gOptionSlaveAddr, a, 4);
            gOptionUseXLAPI = TRUE;
        }
        else if (strcmp(argv[i], "-net") == 0) {
            gOptionUseXLAPI = TRUE;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                strcpy(gOptionXlSlaveNet, argv[++i]);
                printf("Set net to %s\n", argv[i]);
            }
        }
#endif 
    }
    if (gDebugLevel) printf("Set screen output verbosity to %u\n", gDebugLevel);
    if (gOptionJumbo) printf("Using Jumbo Frames\n");
#ifdef APP_ENABLE_XLAPI_V3
    if (gOptionUseXLAPI) {
        printf("Using XL-API V3\n");
        if (gOptionJumbo) {
            printf("WARNING: XL-API does not support jumbo frames! Disabled!\n");
            gOptionJumbo = FALSE;
        }
    }
#endif
    printf("\n");

    // Init network
    if (networkInit()) {

        // Init clock 
        if (clockInit()) {

            // Initialize the XCP slave
            if (xcpSlaveInit()) {

                // Initialize ECU demo task (C) 
                ecuInit();

                // Initialize ECU demo tasks (C++) 
                ecuppInit();

    #ifdef APP_ENABLE_A2L_GEN
                // Create A2L/MDI names and generate A2L file
                printf("\n");
                char* filepath; // Full path + name +extension
                ApplXcpGetA2LFilename(&filepath, NULL, TRUE);
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
                Sleep(100); // give everything a chance to be up and running
                printf("\nPress ESC to stop\n");
                std::thread t0([]() { mainTask(0); });

                // Exit
                t0.join(); // wait here, main loop terminates on key ESC or when the XCP threads terminate
                t2.detach();
                t3.detach();

#endif // _WIN
            }
        }
    }

    printf("Shutdown\n");
    xcpSlaveShutdown();
      
    networkShutdown();
    printf("\nPress any key to close\n");
    while (!_kbhit()) sleepMs(100);
    return 0;
}


