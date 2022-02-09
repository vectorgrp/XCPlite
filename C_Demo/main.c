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
#include "..\src\platform.h"
#include "../src/util.h"
#include "..\src\xcpTl.h"
#include "..\src\xcpLite.h"
#include "..\src\xcpServer.h"
#if OPTION_ENABLE_A2L_GEN
#include "..\src\A2L.h"
#endif
#include "ecu.h" // Demo measurement task in C

 
//-----------------------------------------------------------------------------------------------------
// Create A2L file

#if OPTION_ENABLE_A2L_GEN
static BOOL createA2L(const char* a2l_path_name) {

    if (!A2lOpen(a2l_path_name, ApplXcpGetName() )) return FALSE;

    A2lCreateParameterWithLimits(gDebugLevel, A2L_TYPE_UINT32, "Console output verbosity", "", 0, 100);
    ecuCreateA2lDescription();
    A2lCreate_IF_DATA(gOptionUseTCP, gOptionAddr, gOptionPort);
    A2lClose();
    return TRUE;
}
#endif


//-----------------------------------------------------------------------------------------------------


int main(int argc, char* argv[]) {

    printf("\n%s - XCP on Ethernet C Demo\n", ApplXcpGetName());
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
    createA2L(ApplXcpGetA2lFileName());
#endif
    tXcpThread t2;
    create_thread(&t2, ecuTask);

    // Loop   
    for (;;) {
        Sleep(100);
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
