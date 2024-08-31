/*----------------------------------------------------------------------------
| File:
|   xcpCanServer.c
|
| Description:
|   XCP on CAN Server
|   SHows how to integrate the XCP on CAN driver in an application
|   Creates threads for cmd handling and data transmission
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
-----------------------------------------------------------------------------*/

#include "main.h"
#include "platform.h"
#include "util.h"
#include "xcpLite.h"   
#include "xcpCanServer.h"


#if defined(_WIN) // Windows
static DWORD WINAPI XcpServerReceiveThread(LPVOID lpParameter);
#elif defined(_LINUX) // Linux
static void* XcpServerReceiveThread(void* par);
#endif
#if defined(_WIN) // Windows
static DWORD WINAPI XcpServerTransmitThread(LPVOID lpParameter);
#elif defined(_LINUX) // Linux
static void* XcpServerTransmitThread(void* par);
#endif


static struct {

    BOOL isInit; 

    // Threads
    tXcpThread TransmitThreadHandle;
    volatile BOOL TransmitThreadRunning;
    tXcpThread ReceiveThreadHandle;
    volatile BOOL ReceiveThreadRunning;

} gXcpServer;


// Check XCP server status
BOOL XcpCanServerStatus() {
    return gXcpServer.isInit && gXcpServer.TransmitThreadRunning && gXcpServer.ReceiveThreadRunning;
}


// XCP server init
BOOL XcpCanServerInit(BOOL useCANFD, uint16_t croId, uint16_t dtoId, uint32_t bitRate)
{
    int r = 0;

    if (gXcpServer.isInit) return FALSE;
    XCP_DBG_PRINT1("\nStart XCP server\n");

    gXcpServer.TransmitThreadRunning = 0;
    gXcpServer.ReceiveThreadRunning = 0;

    // Initialize XCP protocol layer
    XcpInit();

    // Initialize XCP transport layer
    r = XcpCanTlInit(useCANFD, croId, dtoId, bitRate);
    if (!r) return 0;

    // Start XCP protocol layer
    XcpStart();

    // Create threads
    create_thread(&gXcpServer.TransmitThreadHandle, XcpServerTransmitThread);
    create_thread(&gXcpServer.ReceiveThreadHandle, XcpServerReceiveThread);

    gXcpServer.isInit = TRUE;
    return TRUE;
}

BOOL XcpCanServerShutdown() {

    if (gXcpServer.isInit) {
        XcpDisconnect();

        // Shutdown XCP transport layer
        XcpTlShutdown();
        
        gXcpServer.TransmitThreadRunning = FALSE;
        gXcpServer.ReceiveThreadRunning = FALSE;
        join_thread(gXcpServer.TransmitThreadHandle);
        join_thread(gXcpServer.ReceiveThreadHandle);
    }
    return TRUE;
}


// XCP server command receive thread
#if defined(_WIN) // Windows
DWORD WINAPI XcpServerReceiveThread(LPVOID par)
#elif defined(_LINUX) // Linux
extern void* XcpServerReceiveThread(void* par)
#endif
{
    (void)par;
    XCP_DBG_PRINT3("Start XCP CMD thread\n");

    // Receive XCP command message loop
    gXcpServer.ReceiveThreadRunning = TRUE;
    while (gXcpServer.ReceiveThreadRunning) {
        // Blocking
        if (!XcpTlHandleCommands(XCPTL_TIMEOUT_INFINITE)) break; // Error -> terminate thread
    }
    gXcpServer.ReceiveThreadRunning = FALSE;

    XCP_DBG_PRINT_ERROR("ERROR: XcpTlHandleCommands failed!\n");
    XCP_DBG_PRINT_ERROR("ERROR: XcpServerReceiveThread terminated!\n");
    return 0;
}


// XCP server transmit thread
#if defined(_WIN) // Windows
DWORD WINAPI XcpServerTransmitThread(LPVOID par)
#elif defined(_LINUX) // Linux
extern void* XcpServerTransmitThread(void* par)
#endif
{
    (void)par;
    int32_t n;

    XCP_DBG_PRINT3("Start XCP DAQ thread\n");

    // Transmit loop
    gXcpServer.TransmitThreadRunning = TRUE;
    while (gXcpServer.TransmitThreadRunning) {

        // Wait for transmit data available
        XcpTlWaitForTransmitData(XCPTL_TIMEOUT_INFINITE);

        // Transmit all messages from the transmit queue
        n = XcpTlHandleTransmitQueue();
        if (n<0) {
          XCP_DBG_PRINT_ERROR("ERROR: XcpTlHandleTransmitQueue failed!\n");
          break; // error - terminate thread
        }

    } // for (;;)
    gXcpServer.TransmitThreadRunning = FALSE;

    XCP_DBG_PRINT_ERROR("XCP DAQ thread terminated!\n");
    return 0;
}


