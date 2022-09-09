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


#define OPTION_CALC_MINMAX 0 // Calculate minimum and maximun in each cycle and trigger events wenn changed


//-----------------------------------------------------------------------------------------------------
// Demo code 
// Measure and calibrate parameters in multiple instances of a class


#if OPTION_CALC_MINMAX
// Min/Max
static class SigGen* pMaxSigGen = NULL;
static class SigGen* pMinSigGen = NULL;
static class XcpObject* minSigGenEvent = NULL;
static class XcpObject* maxSigGenEvent = NULL;
#endif

class SigGen : public XcpObject {

protected:

    // Create A2L desription of this instance
    virtual void xcpCreateA2lTypedefComponents(A2L* a2l) {
        a2l->createTypedefMeasurementComponent(value);
        a2l->createTypedefParameterComponent(par_cycleTimeUs);
        a2l->createTypedefParameterComponent(par_ampl);
        a2l->createTypedefParameterComponent(par_phase);
        a2l->createTypedefParameterComponent(par_period);
        a2l->createTypedefParameterComponent(par_offset);
    }

public:

    // Parameters
    uint32_t par_cycleTimeUs;
    double par_ampl;
    double par_phase;
    double par_offset;
    double par_period;

    // Measurements
    double value;

    // Task
    thread *t;

    SigGen(const char* instanceName, uint32_t cycleTimeUs, double ampl, double offset, double phase, double period) : XcpObject(instanceName,"SigGen",sizeof(SigGen)) {

        // Init parameters
        par_cycleTimeUs = cycleTimeUs;
        par_ampl = ampl;
        par_offset = offset;
        par_phase = phase;
        par_period = period;

        // Init signals
        value = 0; 

        // Start ECU task thread
        t = new thread([this]() { task(); });
    }
         
#if OPTION_CALC_MINMAX
    void calcMinMax(double v) { // track the task with current minimum and maximum value
        
        if (pMaxSigGen==NULL || (pMaxSigGen != this && pMaxSigGen->value < v)) {
            pMaxSigGen = this;
        }
        if (pMinSigGen==NULL || (pMinSigGen != this && pMinSigGen->value > v)) {
            pMinSigGen = this;
        }
        maxSigGenEvent->xcpEvent((uint8_t*)pMaxSigGen);
        minSigGenEvent->xcpEvent((uint8_t*)pMinSigGen);
        
    }
#endif

    void task() {

        printf("ECU task (name=%s id=%u) running\n", xcpInstanceName, xcpInstanceId);
        for (;;) {

            sleepNs(par_cycleTimeUs * 1000); 
            value = par_offset + par_ampl * sin( (double)clockGet() * M_2PI / (CLOCK_TICKS_PER_S * par_period) + par_phase);
#if OPTION_CALC_MINMAX
            calcMinMax(value); // track the task with current minimum and maximum value
#endif
            // Trigger the XCP instance event
            xcpEvent();
        }
        // printf("\nECU task %s stopped\n", instanceName);
    }

    ~SigGen() {
        delete t;
    }

};



//-----------------------------------------------------------------------------------------------------

// Demo Parameters (global)
uint8_t par_uint8 = 8;
uint16_t par_uint16 = 16;
uint32_t par_uint32 = 32;
uint64_t par_uint64 = 64;
int8_t par_int8 = -8;
int16_t par_int16 = -16;
int32_t par_int32 = -32;
int64_t par_int64 = -64;
float par_float = 0.32f;
double par_double = 0.64;


int main(int argc, char* argv[]) {

    printf("\nXCP on Ethernet C++ Demo\n");
    if (!cmdline_parser(argc, argv)) return 0;

    // XCP singleton and A2L init (using the A2L factory from Xcp)
    Xcp* xcp = Xcp::getInstance();
    if (!xcp->init(gOptionBindAddr, gOptionPort, gOptionUseTCP, FALSE, XCPTL_MAX_SEGMENT_SIZE)) return -1;
    A2L* a2l = xcp->createA2L("CPP_DEMO");

    // Create a calibration parameter to control the debug output verbosity
    // Create some test calibration parameters in global address space
    a2l->createParameterGroup("TestParameters");
    a2l->createParameter(gDebugLevel, "", "");
    a2l->createParameter(par_uint8, "", "");
    a2l->createParameter(par_uint16, "", "");
    a2l->createParameter(par_uint32, "", "");
    a2l->createParameter(par_uint64, "", "");
    a2l->createParameter(par_int8, "", "");
    a2l->createParameter(par_int16, "", "");
    a2l->createParameter(par_int32, "", "");
    a2l->createParameter(par_int64, "", "");
    a2l->createParameter(par_float, "", "");
    a2l->createParameter(par_double, "", "");
    a2l->closeParameterGroup();
    
    // Create 10 different SigGen signal generator task instances with calibration parameters and dynamic addressing
    // The constructor of SigGen will create an instance amd an associated XCP event
    SigGen* sigGen[10];
    for (int i = 0; i <= 9; i++) {
        string* s = new string("SigGen"); s->append(to_string(i+1));
        sigGen[i] = new SigGen((char*)s->c_str(), 16000, 100.0-i*5, 0.0, i*M_PI/15.0, 2.0);
    }
    
    // Create A2L description for class SigGen, use any instance to do this, function cant be static
    sigGen[0]->xcpCreateA2lTypedef();
    
#if OPTION_CALC_MINMAX
    // Create virtual instances of pMinSigGen and pMaxSigGen 
    minSigGenEvent = new XcpDynObject("pMinSigGen", SigGen);
    maxSigGenEvent = new XcpDynObject("pMaxSigGen", SigGen);
#endif

    // Finalize and close A2l (otherwise it will be closed later on connect)
    xcp->closeA2L();

    // Main loop (health status and keyboard check)
    printf("\nPress ESC to stop\n");
    for (;;) {
        sleepMs(100);
        if (!xcp->status()) { printf("\nXCP server failed\n");  break;  } // Check if the XCP server is running
        if (_kbhit()) {  if (_getch() == 27) break;  } // Stop on ESC
    }

    // XCP shutdown
    xcp->shutdown();

    printf("\nApplication terminated. Press any key to close\n");
    while (!_kbhit()) sleepMs(100);
    return 0;
}

