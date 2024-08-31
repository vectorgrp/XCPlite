/*----------------------------------------------------------------------------
| File:
|   main.c
|
| Description:
|   Demo main for XCPite
|   Very basic demo to demonstrate integration into user application  
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "platform.h"
#include "xcpLite.h"
#include "xcpEthServer.h"
#if OPTION_ENABLE_A2L_GEN
#include "A2L.h"
#endif


// OPTIONs defined in main_cfg.h


//-------------------------------------------------------------------------------------------------

// Demo measurement event
uint16_t event = 0;

// Demo measurement signal
double channel1 = 0.0;
uint16_t counter = 0;

// Demo parameters
double ampl = 400.0;
double period = 3.0;
uint32_t cycleTime = 10000; // us


//-------------------------------------------------------------------------------------------------

static BOOL checkKeyboard(void) { if (_kbhit()) { if (_getch()==27) {  return FALSE; } } return TRUE; }

int main() {

    printf("\nXCPlite - Simple Demo\n");

    // Initialize high resolution clock for measurement event timestamping
    if (!clockInit()) return 0;

    // Init network
    if (!socketStartup()) return 0;
    
    // Initialize the XCP Server
    uint8_t ipAddr[] = OPTION_SERVER_ADDR;
    if (!XcpEthServerInit(ipAddr, OPTION_SERVER_PORT, OPTION_USE_TCP, XCPTL_MAX_SEGMENT_SIZE)) return 0;

    // Test address conversion functions
    // uint8_t* ApplXcpGetPointer(uint8_t addr_ext, uint32_t addr);
    // uint32_t ApplXcpGetAddr(uint8_t * p);
    uint32_t a = ApplXcpGetAddr((uint8_t*)&ampl);
    uint8_t* p = ApplXcpGetPointer(0 /*addr_ext*/, a);
    double val = *(double*)p; // read
    assert(ampl == val); 
    *(double*)p = 100.0; // write
    assert(ampl == 100.0);

    // Create ASAM A2L description file for measurement signals, calibration variables, events and communication parameters 
#if OPTION_ENABLE_A2L_GEN
    if (!A2lOpen(OPTION_A2L_FILE_NAME, OPTION_A2L_NAME)) return 0;
    event = XcpCreateEvent("mainLoop", 0, 0, 0, 0);
#ifdef __cplusplus // In C++, A2L objects datatype is detected at run time
    A2lCreateParameterWithLimits(ampl, "Amplitude of sinus signal in V", "V", 0, 800);
    A2lCreateParameterWithLimits(period, "Period of sinus signal in s", "s", 0, 10);
    A2lCreateParameterWithLimits(cycleTime, "Cycle time of demo event loop in us", "us", 0, 1000000);
    A2lSetFixedEvent(event); // Associate to the variables created below
    A2lCreatePhysMeasurement(channel1, "Sinus demo signal", 1.0, 0.0, "V");
    A2lCreateMeasurement(counter, "Event counter");
#else
    A2lCreateParameterWithLimits(ampl, A2L_TYPE_DOUBLE, "Amplitude of sinus signal in V", "V", 0, 800);
    A2lCreateParameterWithLimits(period, A2L_TYPE_DOUBLE, "Period of sinus signal in s", "s", 0, 10);
    A2lCreateParameterWithLimits(cycleTime, A2L_TYPE_UINT32, "Cycle time of demo event loop in us", "us", 0, 1000000);
    A2lSetFixedEvent(event); // Associate to the variables created below
    A2lCreatePhysMeasurement(channel1, A2L_TYPE_DOUBLE, "Sinus demo signal", 1.0, 0.0, "V");
    A2lCreateMeasurement(counter, A2L_TYPE_UINT32, "Event counter");
#endif
    A2lCreate_ETH_IF_DATA(OPTION_USE_TCP, ipAddr, OPTION_SERVER_PORT);
    A2lClose();
#endif    


    // Mainloop 
    for (;;) {

        // XCP Measurement Demo
        counter++;
        channel1 = ampl * sin(M_2PI * ((double)clockGet() / CLOCK_TICKS_PER_S) / period);
        XcpEvent(event); // Trigger XCP measurement data aquisition event 

        sleepNs(cycleTime*1000);

        if (!XcpEthServerStatus()) { printf("\nXCP Server failed\n");  break; } // Check if the XCP server is running

        if (!checkKeyboard()) {
          XcpSendEvent(EVC_SESSION_TERMINATED, NULL, 0);
          break;
        }
    }
            
    // Stop XCP
    XcpDisconnect();

    // Stop the XCP server
    XcpEthServerShutdown();
    socketCleanup();

    printf("\nXCPlite terminated. Press any key to close\n");
    while (!_kbhit()) sleepMs(100);
    return 1;
}


