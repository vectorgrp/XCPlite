/*----------------------------------------------------------------------------
| File:
|   ecu.c
|
| Description:
|   Test Measurement and Calibration variables for XCP demo
|   C language
 ----------------------------------------------------------------------------*/
 /*
 | Code released into public domain, no attribution required
 */

#include "main.h"
#include "main_cfg.h"
#include "..\src\platform.h"
#include "..\src\xcpLite.h"
#include "..\src\A2L.h"
#include "ecu.h"


/**************************************************************************/
/* ECU Measurements */
/**************************************************************************/

// Event
uint16_t gXcpEvent_EcuCyclic = 0; // XCP event number

// Global measurement variables
double ecuTime = 0;
double channel1 = 0;
double channel2 = 0;
double channel3 = 0;
uint8_t byteArray1[1024];
uint32_t longArray1[1024];
uint8_t  byteCounter = 0;
uint16_t wordCounter = 0;
uint32_t dwordCounter = 0;
int8_t  sbyteCounter = 0;
int16_t swordCounter = 0;
int32_t sdwordCounter = 0;


/**************************************************************************/
/* ECU Parameters */
/**************************************************************************/


struct ecuPar {
    uint32_t cycleTimeUs;
    double period;
    double offset;
    double phase;
    double ampl;
    uint8_t map1_8_8[8][8];
    uint8_t curve1_32[32];
};


struct ecuPar ecuRomPar = {
    2000, // Default cycle time in us
    3.0,
    0.0, 
    0, 
    400.0,
    { {0,0,0,0,0,0,1,2},
     {0,0,0,0,0,0,2,3},
     {0,0,0,0,1,1,2,3},
     {0,0,0,1,1,2,3,4},
     {0,1,1,2,3,4,5,7},
     {1,1,1,2,4,6,8,9},
     {1,1,2,4,5,8,9,10},
     {1,1,3,5,8,9,10,10}
    },
    { 0,1,3,6,9,15,20,30,38,42,44,46,48,50,48,45,40,33,25,15,5,4,3,2,1,0,0,1,4,8,4,0}
};


volatile struct ecuPar ecuRamPar;
volatile struct ecuPar *ecuPar = &ecuRomPar;


/**************************************************************************/
/* ECU Demo Code */
/**************************************************************************/

void ecuParInit() {

    memcpy((void*)&ecuRamPar, (void*)&ecuRomPar, sizeof(struct ecuPar));
    ecuPar = (struct ecuPar*) & ecuRamPar;
}

#if OPTION_ENABLE_CAL_SEGMENT

// Calibration page handling
// page 0 is RAM, page 1 is ROM

void ecuParSetCalPage(uint8_t page) {

    ecuPar = (page == 0) ? &ecuRamPar : (struct ecuPar *)&ecuRomPar;
}

uint8_t ecuParGetCalPage() {

    return (ecuPar == &ecuRamPar) ? 0 : 1;
}

uint8_t *ecuParAddrMapping( uint8_t *a ) {

    if (a >= (uint8_t*)&ecuRamPar && a < (uint8_t*)&ecuRamPar + sizeof(struct ecuPar)) {
      if (ecuPar == &ecuRamPar) return a; // RAM
      return (uint8_t*)&ecuRomPar + (a - (uint8_t*)&ecuRamPar); // ROM
    }
    return a;
}

#endif

// Init
void ecuInit() {

    // Initialize calibration parameters
    ecuParInit(); // Initializes parameters in calibration RAM
#ifdef OPTION_ENABLE_CAL_SEGMENT
    ecuParSetCalPage(0); // Switch to calibration RAM
#endif

    // Initialize measurement variables
    channel1 = channel2 = channel3 = 0;
    byteCounter = 0;
    wordCounter = 0;
    dwordCounter = 0;
    sbyteCounter = 0;
    swordCounter = 0;
    sdwordCounter = 0;
    for (int i = 0; i < 1024; i++) byteArray1[i] = (unsigned char)i;
    for (unsigned int i = 0; i < 1024; i++) longArray1[i] = i;

    gXcpEvent_EcuCyclic = XcpCreateEvent("ecuTask", 2000, 0, 0, 0);  // Created a XCP measurement event
}


// Create A2L File 
void ecuCreateA2lDescription() {

    // Calibration Memory Segment
#ifdef OPTION_ENABLE_CAL_SEGMENT  
    A2lCreate_MOD_PAR(ApplXcpGetAddr((uint8_t*)&ecuRamPar), sizeof(ecuRamPar));
#endif

    // Parameters
    A2lCreateParameterWithLimits(ecuRamPar.ampl, A2L_TYPE_DOUBLE, "Amplitude", "V", 0, 800);
    A2lCreateParameterWithLimits(ecuRamPar.offset, A2L_TYPE_DOUBLE, "RefOffset", "V", -200, +200);
    A2lCreateParameterWithLimits(ecuRamPar.phase, A2L_TYPE_DOUBLE, "Phase", "", 0, M_2PI);
    A2lCreateParameterWithLimits(ecuRamPar.period, A2L_TYPE_DOUBLE, "Period in s", "s", 0, 10);
    A2lCreateParameterWithLimits(ecuRamPar.cycleTimeUs, A2L_TYPE_UINT32, "ECU task cycle time in us", "us", 50, 1000000);
    A2lCreateMap(ecuRamPar.map1_8_8, A2L_TYPE_UINT8, 8, 8, "8*8 byte calibration array", "");
    A2lCreateCurve(ecuRamPar.curve1_32, A2L_TYPE_UINT8, 32, "32 byte calibration array", "");

    A2lParameterGroup("Parameters", 7,        
        "ecuRamPar.cycleTimeUs", 
        "ecuRamPar.map1_8_8", "ecuRamPar.curve1_32",
        "ecuRamPar.period", "ecuRamPar.ampl", "ecuRamPar.phase", "ecuRamPar.offset" );

    // Measurements
    A2lSetEvent(gXcpEvent_EcuCyclic); // Associate XCP event "EcuCyclic" to the variables created below
    A2lCreateMeasurement(byteCounter, A2L_TYPE_UINT8, "");
    A2lCreateMeasurement(wordCounter, A2L_TYPE_UINT16, "");
    A2lCreateMeasurement(dwordCounter, A2L_TYPE_UINT32, "");
    A2lCreateMeasurement(sbyteCounter, A2L_TYPE_INT8, "");
    A2lCreateMeasurement(swordCounter, A2L_TYPE_INT16, "");
    A2lCreateMeasurement(sdwordCounter, A2L_TYPE_INT32, "");
    A2lCreatePhysMeasurement(channel1, A2L_TYPE_DOUBLE, "Sinus signal 1", 1.0, 0.0, "");
    A2lCreatePhysMeasurement(channel2, A2L_TYPE_DOUBLE, "Sinus signal 2", 1.0, 0.0, "");
    A2lCreatePhysMeasurement(channel3, A2L_TYPE_DOUBLE, "Sinus signal 3", 1.0, 0.0, "");
    A2lCreateMeasurementArray(byteArray1, A2L_TYPE_UINT8);
    A2lCreateMeasurementArray(longArray1, A2L_TYPE_UINT32);

    A2lMeasurementGroup("EcuTaskSignals", 11,
        "channel1", "channel2", "channel3", "byteCounter", "wordCounter", "dwordCounter", "sbyteCounter", "swordCounter", "sdwordCounter" ,"longArray1","byteArray1");
}


// Cyclic demo task 
void ecuCyclic( void )
{
    // Counters of different type
    sbyteCounter++;
    swordCounter++;
    sdwordCounter++;
    byteCounter++;
    wordCounter++;
    dwordCounter++;

    // Arrays
    unsigned int i = dwordCounter % 1024;
    longArray1[i] ++;
    byteArray1[i] ++;

    // Floating point signals
    double x = M_2PI * ecuTime / ecuPar->period;
    channel1 = ecuPar->offset + ecuPar->ampl * sin(x);
    channel2 = ecuPar->offset + ecuPar->ampl * sin(x + M_PI * 1 / 3);
    channel3 = ecuPar->offset + ecuPar->ampl * sin(x + M_PI * 2 / 3);
    ecuTime += 0.002;

    XcpEvent(gXcpEvent_EcuCyclic); // Trigger measurement data aquisition event for ecuCyclic() task
}


// ECU cyclic (2ms default) demo task
// Calls C ECU demo code
#ifdef _WIN
DWORD WINAPI ecuTask(LPVOID p)
#else
void* ecuTask(void* p)
#endif
{
    (void)p;
    printf("Start C task (cycle = %dus, XCP event = %d)\n", ecuPar->cycleTimeUs, gXcpEvent_EcuCyclic);
    for (;;) {
        sleepNs(ecuPar->cycleTimeUs * 1000);
        ecuCyclic();
    }
}
