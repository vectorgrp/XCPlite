/*----------------------------------------------------------------------------
| File:
|   main.cpp
|
| Description:
|   CPP Demo for XCPlite
|   Shows how to use the C++ wrapper class for XCPlite
|   Shows how to measure and calibrate global variables, global structs and dynamic instances of a C++ class (optional)
|   Optional runtime A2L generation
|   Can be used either with A2L upload or with CANape linker map updater
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "platform.h"
#include "options.h"
#include "xcp.hpp"
#include "A2Lpp.hpp"


 // OPTIONs are defined in main_cfg.h

#if OPTION_ENABLE_DBG_PRINTS
unsigned int gDebugLevel = OPTION_DEBUG_LEVEL;
#endif

 //-----------------------------------------------------------------------------------------------------

 // Demo measurement event
uint16_t gMainloopEvent = 0;

// Demo measurement signals
double gChannel1 = 0.0;
uint16_t gCounter = 0;

// Demo calibration parameters
struct sSignalParameters {
  double ampl;
  double offset;
  double phase;
};
struct sSignalParameters gSignalParameters = { 400.0, 0.0, 0.0 };
double gPeriod = 5.0;;
uint32_t gCycleTime = 10000; // us




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
        a2l->createDynTypedefMeasurementComponent(value);
        a2l->createDynTypedefParameterComponent(par_ampl);
        a2l->createDynTypedefParameterComponent(par_phase);
        a2l->createDynTypedefParameterComponent(par_offset);
    }
#endif

public:

    // Parameters
    double par_ampl;
    double par_phase;
    double par_offset;

    // Measurements
    double value;

    // Task
    std::thread *t;

    SigGen(const char* instanceName, double _par_ampl, double _par_offset, double _par_phase) : XcpObject(instanceName,"SigGen",sizeof(SigGen)) {

        // Init parameters
        par_ampl = _par_ampl;
        par_offset = _par_offset;
        par_phase = _par_phase;
        
        // Init signals
        value = 0; 

        // Start ECU task thread
        t = new std::thread([this]() { task(); });
    }
         
    void task() {

        printf("ECU task (name=%s id=%u) running\n", xcpInstanceName, xcpInstanceId);
        for (;;) {

            sleepNs(gCycleTime * 1000);

            value = par_offset + par_ampl * sin( (double)clockGet() * M_2PI / (CLOCK_TICKS_PER_S * gPeriod) + par_phase);

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
    // Must be defined before A2L generation
    gMainloopEvent = xcp->createEvent("mainLoop");

    // A2L generation (optional)
    // Declare all measurement variable and calibration parameters
#if OPTION_ENABLE_A2L_GEN
    A2L* a2l = xcp->createA2L("CPP_DEMO");

    // Declare calibration parameters in global address space
    a2l->createTypedefBegin(gSignalParameters, "This is the global signal parameters structure type"); // global struct signal_parameters signal_parameters
    a2l->createTypedefParameterComponent(gSignalParameters,ampl);
    a2l->createTypedefParameterComponent(gSignalParameters,offset);
    a2l->createTypedefParameterComponent(gSignalParameters,phase);
    a2l->createTypedefEnd();
    a2l->createTypedefInstance(gSignalParameters, sSignalParameters, "This is the global signal parameters structure instance"); // global instance signal_parameter of struct signal_parameters
    // a2l->createParameterWithLimits(gSignalParameters.phase, "Phase of sinus signal in s", "s", 0, 10); // global signal_parameters.phase, this is an alternative way to define components of global structs
    a2l->createParameterWithLimits(gPeriod, "Period of sinus signal in s", "s", 0, 10); // global double period
    a2l->createParameterWithLimits(gCycleTime, "Cycle time of demo event loop in us", "us", 0, 1000000); // global uint32_t cycleTime
#if OPTION_ENABLE_DBG_PRINTS
    a2l->createParameter(gDebugLevel, "Console output verbosity level", ""); // Create a calibration parameter to control the debug output verbosity
#endif
    // Declare measurement variables in global address space
    A2lSetFixedEvent(gMainloopEvent); // Associate this event to the measurement variables created below
    a2l->createPhysMeasurement(gChannel1, "Sinus signal as double with physical conversion rule", 1.0, 0.0, "V");
    a2l->createMeasurement(gCounter, "Event counter as uint32");
#endif // OPTION_ENABLE_A2L_GEN

    //Demo: Measure and calibrate multiple instances of a class (optional)
#if OPTION_ENABLE_DYNAMIC_DEMO

    // Create 10 different SigGen sine signal generator task instances with calibration parameters and dynamic addressing
    // The constructor of SigGen will create an instance and an associated XCP event for each
    SigGen* sigGen[10];
    for (int i = 0; i <= 9; i++) {
      std::string* s = new std::string("SigGen"); s->append(std::to_string(i+1));
        sigGen[i] = new SigGen((char*)s->c_str(), 100.0-(double)i*5, 0.0, (double)i*M_PI/15.0);
    }
    
    // Create A2L description for class SigGen, use any instance to do this, function can't be static
#if OPTION_ENABLE_A2L_GEN
    sigGen[0]->xcpCreateA2lTypedef();
#endif

#endif // OPTION_ENABLE_DYNAMIC_DEMO

    // Optional: Finalize and close A2l, this would be done automatic on XCP connect, to make the A2L file immediately available, its done explicitly here
#if OPTION_ENABLE_A2L_GEN
    xcp->closeA2L();
#endif // OPTION_ENABLE_A2L_GEN


    // Main loop
    printf("\nPress ESC to stop\n");
    for (;;) {

        // XCP Measurement Demo
        gCounter++;
        gChannel1 = gSignalParameters.offset + gSignalParameters.ampl * sin((double)clockGet() * M_2PI / (CLOCK_TICKS_PER_S * gPeriod) + gSignalParameters.phase);
        xcp->event(gMainloopEvent); // Trigger a XCP measurement data aquisition event 

        sleepNs(gCycleTime * 1000);

        if (!xcp->status()) { printf("\nXCP server failed\n");  break;  } // XCP health status (no side effects)
        if (_kbhit()) {  if (_getch() == 27) break;  } // Stop on ESC
    }

    // XCP shutdown
    xcp->shutdown();

    printf("\nApplication terminated. Press any key to close\n");
    while (!_kbhit()) sleepMs(100);
    return 0;
}

