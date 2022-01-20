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
#include "platform.h"
#include "clock.h"
#include "xcpLite.h"
#ifdef APP_ENABLE_A2L_GEN
#include "A2L.h"
#endif
#include "ecu.h"


/**************************************************************************/
/* ECU Measurements */
/**************************************************************************/

// Event
uint16_t gXcpEvent_EcuCyclic = 0; // EVNO

// Global measurement variables
uint16_t ecuCounter = 0;
double ecuTime = 0;
double channel1 = 0;
double channel2 = 0;
double channel3 = 0;
unsigned char byteArray1[1024];
uint32_t longArray1[1024];
uint8_t  byteCounter = 0;
uint16_t wordCounter = 0;
uint32_t dwordCounter = 0;
int8_t  sbyteCounter = 0;
int16_t swordCounter = 0;
int32_t sdwordCounter = 0;
char testString[] = "TestString";
char* ptr_string = testString;
double* ptr_double = &channel1;


/**************************************************************************/
/* ECU Parameters */
/**************************************************************************/

struct ecuPar ecuRomPar = {
    (uint32_t)sizeof(struct ecuPar),
    2000, // Default cycle time in us
    3.0,
    0.0, 0.0, 0.0,
    0, M_2PI / 3, 2 * M_2PI / 3,
    400.0,300.0,200.0,
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

#ifdef XCP_ENABLE_CAL_PAGE

// Calibration page handling
// page 0 is RAM, page 1 is ROM


void ecuParInit() {

    memcpy((void*)&ecuRamPar, (void*)&ecuRomPar, sizeof(struct ecuPar));
}

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

    // Initialize calibration
#ifdef XCP_ENABLE_CAL_PAGE
    ecuParInit(); // Initializes parameters in calibration RAM
    ecuParSetCalPage(0); // Switch to calibration RAM
#endif

    // Initialize measurement
    ecuCounter = 0;
    channel1 = channel2 = channel3 = 0;
    byteCounter = 0;
    wordCounter = 0;
    dwordCounter = 0;
    sbyteCounter = 0;
    swordCounter = 0;
    sdwordCounter = 0;
    for (int i = 0; i < 1024; i++) byteArray1[i] = (unsigned char)i;
    for (unsigned int i = 0; i < 1024; i++) longArray1[i] = i;

    // Create XCP event
    // Events must be all defined before A2lHeader() is called, measurements and parameters have to be defined after all events have been defined !!
    // Character count should be <=8 to keep the A2L short names unique !
#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    gXcpEvent_EcuCyclic = XcpCreateEvent("ecuTask", 2000, 0, 0, 0);  // Declare a standard XCP measurement event
#endif
}


// Create A2L File content
#ifdef APP_ENABLE_A2L_GEN
void ecuCreateA2lDescription() {

    // Parameters
    A2lCreateParameter(ecuRamPar.CALRAM_SIZE, "", "ECU CALRAM size");
    A2lCreateParameterWithLimits(ecuRamPar.ampl1, "Amplitude", "V", 0, 800);
    A2lCreateParameterWithLimits(ecuRamPar.offset1, "RefOffset", "V", -200, +200);
    A2lCreateParameterWithLimits(ecuRamPar.phase1, "Phase", "", 0, M_2PI);
    A2lCreateParameterWithLimits(ecuRamPar.ampl2, "Amplitude", "V", 0, 800);
    A2lCreateParameterWithLimits(ecuRamPar.offset2, "RefOffset", "V", -200, +200);
    A2lCreateParameterWithLimits(ecuRamPar.phase2, "Phase", "", 0, M_2PI);
    A2lCreateParameterWithLimits(ecuRamPar.ampl3, "Amplitude", "V", 0, 800);
    A2lCreateParameterWithLimits(ecuRamPar.offset3, "RefOffset", "V", -200, +200);
    A2lCreateParameterWithLimits(ecuRamPar.phase3, "Phase", "", 0, M_2PI);
    A2lCreateParameterWithLimits(ecuRamPar.period, "Period in s (XCP server time)", "s", 0, 10);
    A2lCreateMap(ecuRamPar.map1_8_8, 8, 8, "", "8*8 byte calibration array");
    A2lCreateCurve(ecuRamPar.curve1_32, 32, "", "32 byte calibration array");
    A2lCreateParameterWithLimits(ecuRamPar.cycleTime, "ECU task cycle time (task sleep duration) in us", "us", 50, 1000000);

    A2lParameterGroup("Parameters", 5 + 3 * 3,
        "ecuRamPar.CALRAM_SIZE",
        "ecuRamPar.cycleTime",
        "ecuRamPar.map1_8_8", "ecuRamPar.curve1_32",
        "ecuRamPar.period",
        "ecuRamPar.ampl1", "ecuRamPar.offset1", "ecuRamPar.phase1",
        "ecuRamPar.ampl2", "ecuRamPar.offset2", "ecuRamPar.phase2",
        "ecuRamPar.ampl3", "ecuRamPar.offset3", "ecuRamPar.phase3");

    // Measurements
    A2lSetEvent(gXcpEvent_EcuCyclic); // Associate XCP event "EcuCyclic" to the variables created below
    A2lCreateMeasurement(ecuCounter, "16 bit counter incrementing every ECU task cycle");
    A2lCreateMeasurement(byteCounter, "");
    A2lCreateMeasurement(wordCounter, "");
    A2lCreateMeasurement(dwordCounter, "");
    A2lCreateMeasurement_s(sbyteCounter, "");
    A2lCreateMeasurement_s(swordCounter, "");
    A2lCreateMeasurement_s(sdwordCounter, "");
    A2lCreatePhysMeasurement(channel1, "Sinus signal 1 with period, ampl1, phase1", 1.0, 0.0, "");
    A2lCreatePhysMeasurement(channel2, "Sinus signal 2 with period, ampl2, phase2", 1.0, 0.0, "");
    A2lCreatePhysMeasurement(channel3, "Sinus signal 3 with period, ampl3, phase3", 1.0, 0.0, "");


    A2lCreateMeasurementArray(byteArray1);
    A2lCreateMeasurementArray(longArray1);
    A2lMeasurementGroup("EcuTaskSignals", 12,
        "ecuCounter", "channel1", "channel2", "channel3", "byteCounter", "wordCounter", "dwordCounter", "sbyteCounter", "swordCounter", "sdwordCounter", "byteArray1", "longArray1");

}
#endif


// Cyclic demo task (default 2ms cycle time)
void ecuCyclic( void )
{
    // Cycle counter
    ecuCounter++;

    // Counters of different type
    sbyteCounter++;
    swordCounter++;
    sdwordCounter++;
    byteCounter++;
    wordCounter++;
    dwordCounter++;

    // Arrays
    unsigned int i = ecuCounter % 1024;
    longArray1[i] ++;
    byteArray1[i] ++;


    // channel 1-3 demo signals
    double x = M_2PI * ecuTime / ecuPar->period;
    channel1 = ecuPar->offset1 + ecuPar->ampl1 * sin(x + ecuPar->phase1);
    channel2 = ecuPar->offset2 + ecuPar->ampl2 * sin(x + ecuPar->phase2);
    channel3 = ecuPar->offset3 + ecuPar->ampl3 * sin(x + ecuPar->phase3);
    ecuTime += 0.002;

    XcpEvent(gXcpEvent_EcuCyclic); // Trigger measurement data aquisition event for ecuCyclic() task

}


// ECU cyclic (2ms default) demo task
// Calls C ECU demo code
void* ecuTask(void* p) {

    (void)p;
    printf("Start C demo task (cycle = %dus, XCP event = %d)\n", ecuPar->cycleTime, gXcpEvent_EcuCyclic);
    for (;;) {
        sleepNs(ecuPar->cycleTime * 1000);
        ecuCyclic();
    }
}
