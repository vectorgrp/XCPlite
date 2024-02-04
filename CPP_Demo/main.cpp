/*----------------------------------------------------------------------------
| File:
|   main.cpp
|
| Description:
|   CPP Demo for XCPlite
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "main_cfg.h"
#include "platform.h"
#include "options.h"
#include "util.h"
#include "xcp_cfg.h"
#include "xcptl_cfg.h"
#include "xcp.hpp"
#include "A2Lpp.hpp"


 //-----------------------------------------------------------------------------------------------------

 // Demo measurement event
uint16_t event = 0;

// Demo measurement signal (global)
double channel1 = 0.0;
uint16_t counter = 0;

// Demo Parameters (global)
double ampl = 400.0;
double period = 3.0;
uint32_t cycleTime = 10000; // us




#if OPTION_ENABLE_DYNAMIC_DEMO

//-----------------------------------------------------------------------------------------------------
// Demo 
// Measure and calibrate parameters in multiple instances of a class


/*
An instance of class SigGen creates a sine signal in its variable value, depending on property ampl, phase, offset and period
value is declared as a measurement signal of each instance of SigGen
amp, phase, offset and period are calibration parameters
*/
class SigGen : public XcpObject {

protected:

    // Create A2L desription of this instance
#if OPTION_ENABLE_A2L_GEN
    virtual void xcpCreateA2lTypedefComponents(A2L* a2l) {
        a2l->createTypedefMeasurementComponent(value);
        a2l->createTypedefParameterComponent(par_ampl);
        a2l->createTypedefParameterComponent(par_phase);
        a2l->createTypedefParameterComponent(par_period);
        a2l->createTypedefParameterComponent(par_offset);
    }
#endif

public:

    // Parameters
    double par_ampl;
    double par_phase;
    double par_offset;
    double par_period;

    // Measurements
    double value;

    // Task
    std::thread *t;

    SigGen(const char* instanceName, double _par_ampl, double _par_offset, double _par_phase, double _par_period) : XcpObject(instanceName,"SigGen",sizeof(SigGen)) {

        // Init parameters
        par_ampl = _par_ampl;
        par_offset = _par_offset;
        par_phase = _par_phase;
        par_period = _par_period;

        // Init signals
        value = 0; 

        // Start ECU task thread
        t = new std::thread([this]() { task(); });
    }
         
    void task() {

        printf("ECU task (name=%s id=%u) running\n", xcpInstanceName, xcpInstanceId);
        for (;;) {

            sleepNs(cycleTime * 1000);

            value = par_offset + par_ampl * sin( (double)clockGet() * M_2PI / (CLOCK_TICKS_PER_S * par_period) + par_phase);

            // Trigger the XCP instance event
            xcpEvent();
        }
    }

    ~SigGen() {
        delete t;
    }

};

#endif // OPTION_ENABLE_DYNAMIC_DEMO



int main(int argc, char* argv[]) {

    printf("\nXCP on Ethernet C++ Demo\n");
    if (!cmdline_parser(argc, argv)) return 0;

    // XCP singleton
    Xcp* xcp = Xcp::getInstance();
    if (!xcp->init(gOptionBindAddr, gOptionPort, gOptionUseTCP, FALSE, XCPTL_MAX_SEGMENT_SIZE)) return -1;

    // Create a measurement event
    event = xcp->createEvent("mainLoop");

    // A2L generation (using the A2L factory from Xcp)
#if OPTION_ENABLE_A2L_GEN
    A2L* a2l = xcp->createA2L("CPP_DEMO");

    // Declare the test calibration parameters in global address space
    a2l->createParameterWithLimits(ampl, "Amplitude of sinus signal in V", "V", 0, 800);
    a2l->createParameterWithLimits(period, "Period of sinus signal in s", "s", 0, 10);
    a2l->createParameterWithLimits(cycleTime, "Cycle time of demo event loop in us", "us", 0, 1000000);

    // Declare the test measurement variables in global address space
    A2lSetFixedEvent(event); // Associate this event to the variables created below
    a2l->createPhysMeasurement(channel1, "Sinus demo signal", 1.0, 0.0, "V");
    a2l->createMeasurement(counter, "Event counter");

    a2l->createParameter(gDebugLevel, "", ""); // Create a calibration parameter to control the debug output verbosity
#endif

#if OPTION_ENABLE_DYNAMIC_DEMO

    // Create 10 different SigGen sine signal generator task instances with calibration parameters and dynamic addressing
    // The constructor of SigGen will create an instance and an associated XCP event for each
    SigGen* sigGen[10];
    for (int i = 0; i <= 9; i++) {
      std::string* s = new std::string("SigGen"); s->append(std::to_string(i+1));
        sigGen[i] = new SigGen((char*)s->c_str(), 100.0-(double)i*5, 0.0, (double)i*M_PI/15.0, 2.0);
    }
    
    // Create A2L description for class SigGen, use any instance to do this, function can't be static
#if OPTION_ENABLE_A2L_GEN
    sigGen[0]->xcpCreateA2lTypedef();
#endif

#endif // OPTION_ENABLE_DYNAMIC_DEMO

    // Finalize and close A2l
#if OPTION_ENABLE_A2L_GEN
    xcp->closeA2L();
#endif

    // Main loop (XCP health status (no side effects) and keyboard check)
    printf("\nPress ESC to stop\n");
    for (;;) {

        // XCP Measurement Demo
        counter++;
        channel1 = ampl * sin(M_2PI * ((double)clockGet() / CLOCK_TICKS_PER_S) / period);
        xcp->event(event); // Trigger XCP measurement data aquisition event 

        sleepNs(cycleTime * 1000);

        if (!xcp->status()) { printf("\nXCP server failed\n");  break;  } // Check if the XCP server is running
        if (_kbhit()) {  if (_getch() == 27) break;  } // Stop on ESC
    }

    // XCP shutdown
    xcp->shutdown();

    printf("\nApplication terminated. Press any key to close\n");
    while (!_kbhit()) sleepMs(100);
    return 0;
}

