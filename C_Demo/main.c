/*----------------------------------------------------------------------------
| File:
|   main.c
|
| Description:
|   Demo main for XCP on Ethernet C_Demo
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "platform.h"
#include "dbg_print.h"
#include "options.h"
#include "xcpLite.h"
#include "xcpEthServer.h"
#if OPTION_ENABLE_A2L_GEN
#include "A2L.h"
#endif
#include "ecu.h" // Demo measurement task in C


// OPTIONs are defined in main_cfg.h


//-----------------------------------------------------------------------------------------------------
// Create A2L file

#if OPTION_ENABLE_A2L_GEN
static BOOL createA2L() {

    if (!A2lOpen(OPTION_A2L_FILE_NAME, OPTION_A2L_NAME )) return FALSE;
    ecuCreateA2lDescription(); // Create the measurements and calibration objects for the demo task
#if OPTION_ENABLE_DBG_PRINTS
#ifdef __cplusplus
    A2lCreateParameterWithLimits(gDebugLevel, "Console output verbosity", "", 0, 100);
#else
    A2lCreateParameterWithLimits(gDebugLevel, A2L_TYPE_UINT32, "Console output verbosity", "", 0, 100);
#endif
#endif
    A2lCreate_ETH_IF_DATA(gOptionUseTCP, gOptionBindAddr, gOptionPort);
    A2lClose();
    return TRUE;
}
#endif


//-----------------------------------------------------------------------------------------------------

static BOOL checkKeyboard() {
    if (_kbhit()) {
        switch (_getch()) {
        case 27:  XcpSendEvent(EVC_SESSION_TERMINATED, NULL, 0);  return FALSE; // Stop on ESC
#if OPTION_ENABLE_DBG_PRINTS
        case '+': if (gDebugLevel < 5) gDebugLevel++; printf("\nDebuglevel = %u\n", gDebugLevel); break;
        case '-': if (gDebugLevel > 0) gDebugLevel--; printf("\nDebuglevel = %u\n", gDebugLevel); break;
#endif
        }
    }
    return TRUE;
}


int main(int argc, char* argv[]) {

    printf("\nXCP on Ethernet C Demo\n");
    if (!cmdline_parser(argc, argv)) return 0;

    // Initialize high resolution clock for measurement event timestamping
    if (!clockInit()) return 0;

    // Initialize the XCP Server
    if (!socketStartup()) return 0; // Initialize sockets
    if (!XcpEthServerInit(gOptionBindAddr, gOptionPort, gOptionUseTCP, XCPTL_MAX_SEGMENT_SIZE)) return 0;

    // Initialize a demo measurement task thread (in ecu.c) and create an A2L for its measurement and calibration objects
    ecuInit();
#if OPTION_ENABLE_A2L_GEN
    createA2L();
#endif
    tXcpThread t2;
    create_thread(&t2, ecuTask); // create a cyclic task which produces demo measurement signal events

    // Loop and check status of the XCP server (no side effects)
    for (;;) {
        sleepMs(500);
        if (!XcpEthServerStatus()) { printf("\nXCP Server failed\n");  break;  } // Check if the XCP server is running
        if (!checkKeyboard()) break;
    }

    // Terminate task
    sleepMs(1000);
    cancel_thread(t2);
    
    // Stop the XCP server
    XcpEthServerShutdown();
    socketCleanup();

    printf("\nApplication terminated. Press any key to close\n");
    while (!_kbhit()) sleepMs(100);
    return 1;
}

