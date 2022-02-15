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
#include "main_cfg.h"
#include "platform.h"
#include "util.h"
#include "xcpTl.h"
#include "xcpLite.h"
#include "xcpServer.h"
#if OPTION_ENABLE_A2L_GEN
#include "A2L.h"
#endif
#include "ecu.h" // Demo measurement task in C

 
//-----------------------------------------------------------------------------------------------------
// Create A2L file

#if OPTION_ENABLE_A2L_GEN
static BOOL createA2L() {

    if (!A2lOpen(OPTION_A2L_FILE_NAME, OPTION_A2L_PROJECT_NAME )) return FALSE;
    ecuCreateA2lDescription();
    A2lCreateParameterWithLimits(gDebugLevel, A2L_TYPE_UINT32, "Console output verbosity", "", 0, 100);
    A2lCreate_IF_DATA(gOptionUseTCP, gOptionAddr, gOptionPort);
    A2lClose();
    return TRUE;
}
#endif


//-----------------------------------------------------------------------------------------------------


int main(int argc, char* argv[]) {

    printf("\nXCP on Ethernet C Demo\n");
    if (!cmdline_parser(argc, argv)) return 0;

    // Init network
    if (!socketStartup()) return 0;

    // Init clock
    if (!clockInit()) return 0;

    // Initialize the XCP Server
    if (!XcpServerInit(gOptionAddr, gOptionPort, gOptionUseTCP)) return 0;

    // Initialize measurement task thread
    ecuInit();
#if OPTION_ENABLE_A2L_GEN
    createA2L();
#endif
    tXcpThread t2;
    create_thread(&t2, ecuTask);

    // Loop   
    for (;;) {
        sleepMs(100);
        if (!XcpServerStatus()) { printf("\nXCP Server failed\n");  break;  } // Check if the XCP server is running
        if (_kbhit()) {
            if (_getch() == 27) { XcpSendEvent(EVC_SESSION_TERMINATED, NULL, 0);  break; } // Stop on ESC
        }
    }

    // Exit
    sleepMs(1000); // give everything a chance to be up and running
    printf("\nPress ESC to stop\n");
    cancel_thread(t2);
    
    XcpServerShutdown();
    socketCleanup();

    printf("\nApplication terminated. Press any key to close\n");
    while (!_kbhit()) sleepMs(100);
    return 1;
}
