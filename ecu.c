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

#include "platform.h"
#include "main_cfg.h"
#include "clock.h"
#include "util.h"
#ifdef APP_ENABLE_A2L_GEN
#include "A2L.h"
#endif
#include "xcpLite.h"
#include "ecu.h"


/**************************************************************************/
/* ECU Measurements */
/**************************************************************************/

// Events
uint16_t gXcpEvent_EcuCyclic = 0;

uint16_t ecuCounter = 0;
uint16_t ecuCycleCounter = 0;

double ecuTime = 0;
double channel1 = 0;
double channel2 = 0;
double channel3 = 0;

unsigned char byteArray1[1024];
unsigned char byteArray2[1024];
unsigned char byteArray3[1024];
unsigned char byteArray4[1024];
unsigned char byteArray5[1024];
unsigned char byteArray6[1024];
unsigned char byteArray7[1024];
unsigned char byteArray8[1024];
unsigned char byteArray9[1024];
unsigned char byteArray10[1024];
unsigned char byteArray11[1024];
unsigned char byteArray12[1024];
unsigned char byteArray13[1024];
unsigned char byteArray14[1024];
unsigned char byteArray15[1024];
unsigned char byteArray16[1024];

uint32_t longArray1[1024];
uint32_t longArray2[1024];
uint32_t longArray3[1024];
uint32_t longArray4[1024];
uint32_t longArray5[1024];
uint32_t longArray6[1024];
uint32_t longArray7[1024];
uint32_t longArray8[1024];
uint32_t longArray9[1024];
uint32_t longArray10[1024];
uint32_t longArray11[1024];
uint32_t longArray12[1024];
uint32_t longArray13[1024];
uint32_t longArray14[1024];
uint32_t longArray15[1024];
uint32_t longArray16[1024];

uint8_t  byteCounter = 0;
uint16_t wordCounter = 0;
uint32_t dwordCounter = 0;
int8_t  sbyteCounter = 0;
int16_t swordCounter = 0;
int32_t sdwordCounter = 0;

char testString[] = "TestString"; 



/**************************************************************************/
/* ECU Parameters */
/**************************************************************************/


volatile struct ecuPar ecuPar;

const struct ecuPar ecuRomPar = {
    
    (uint32_t)sizeof(struct ecuPar),

    2000, // Default cycle time in us

    3.0,

    0.0, 0.0, 0.0, 50.0, 50.0, 50.0,
    0, M_2PI / 3, 2 * M_2PI / 3, 0, M_2PI / 3, 2 * M_2PI / 3,
    400.0,300.0,200.0, 50.0,50.0,50.0,

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




/**************************************************************************/
/* ECU Demo Code */
/**************************************************************************/

static void ecuParInit() {

    memcpy((void*)&ecuPar,&ecuRomPar,sizeof(ecuPar));
}

// Init
void ecuInit() {

    // Initializes parameters
    ecuParInit();

    // Initialize measurements
    ecuCounter = 0;
    ecuCycleCounter = 0;
    channel1 = channel2 = channel3 = 0;
    byteCounter = 0;
    wordCounter = 0;
    dwordCounter = 0;
    sbyteCounter = 0;
    swordCounter = 0;
    sdwordCounter = 0;
    for (int i = 0; i < 1024; i++) {
        byteArray1[i] = (unsigned char)i;    byteArray2[i] = (unsigned char)i;    byteArray3[i] = (unsigned char)i;    byteArray4[i] = (unsigned char)i;
        byteArray5[i] = (unsigned char)i;    byteArray6[i] = (unsigned char)i;    byteArray7[i] = (unsigned char)i;    byteArray8[i] = (unsigned char)i;
        byteArray9[i] = (unsigned char)i;    byteArray10[i] = (unsigned char)i;   byteArray11[i] = (unsigned char)i;   byteArray12[i] = (unsigned char)i;
        byteArray13[i] = (unsigned char)i;   byteArray14[i] = (unsigned char)i;   byteArray15[i] = (unsigned char)i;   byteArray16[i] = (unsigned char)i;
    }
    for (unsigned int i = 0; i < 1024; i++) {
        longArray1[i] = i;     longArray2[i] = i;    longArray3[i] = i;     longArray4[i] = i;
        longArray5[i] = i;     longArray6[i] = i;    longArray7[i] = i;     longArray8[i] = i;
        longArray9[i] = i;     longArray10[i] = i;   longArray11[i] = i;    longArray12[i] = i;
        longArray13[i] = i;    longArray14[i] = i;   longArray15[i] = i;    longArray16[i] = i;
    }

    // Create XCP event
    // Events must be all defined before A2lHeader() is called, measurements and parameters have to be defined after all events have been defined !!
    // Character count should be <=8 to keep the A2L short names unique !
#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    gXcpEvent_EcuCyclic = XcpCreateEvent("ecuTask", 2000, 0, 0);                               // Standard event triggered in C ecuTask
#endif
}


// Create A2L File content
#ifdef APP_ENABLE_A2L_GEN
void ecuCreateA2lDescription() {
    
    A2lCreateParameter(ecuPar.CALRAM_SIZE, "", "ECU CALRAM size");

    A2lCreateParameterWithLimits(ecuPar.ampl1, "Amplitude", "V", 0, 800);
    A2lCreateParameterWithLimits(ecuPar.offset1, "RefOffset", "V", -200, +200);
    A2lCreateParameterWithLimits(ecuPar.phase1, "Phase", "", 0, M_2PI);
    A2lCreateParameterWithLimits(ecuPar.ampl2, "Amplitude", "V", 0, 800);
    A2lCreateParameterWithLimits(ecuPar.offset2, "RefOffset", "V", -200, +200);
    A2lCreateParameterWithLimits(ecuPar.phase2, "Phase", "", 0, M_2PI);
    A2lCreateParameterWithLimits(ecuPar.ampl3, "Amplitude", "V", 0, 800);
    A2lCreateParameterWithLimits(ecuPar.offset3, "RefOffset", "V", -200, +200);
    A2lCreateParameterWithLimits(ecuPar.phase3, "Phase", "", 0, M_2PI);
    A2lCreateParameterWithLimits(ecuPar.ampl4, "Amplitude", "A", 0, 800);
    A2lCreateParameterWithLimits(ecuPar.offset4, "RefOffset", "A", -200, +200);
    A2lCreateParameterWithLimits(ecuPar.phase4, "Phase", "", 0, M_2PI);
    A2lCreateParameterWithLimits(ecuPar.ampl5, "Amplitude", "A", 0, 800);
    A2lCreateParameterWithLimits(ecuPar.offset5, "RefOffset", "A", -200, +200);
    A2lCreateParameterWithLimits(ecuPar.phase5, "Phase", "", 0, M_2PI);
    A2lCreateParameterWithLimits(ecuPar.ampl6, "Amplitude", "A", 0, 800);
    A2lCreateParameterWithLimits(ecuPar.offset6, "RefOffset", "A", -200, +200);
    A2lCreateParameterWithLimits(ecuPar.phase6, "Phase", "", 0, M_2PI);

    A2lCreateParameterWithLimits(ecuPar.period, "Period in s (XCP slave time)", "s", 0, 10);

    A2lCreateMap(ecuPar.map1_8_8, 8, 8, "", "8*8 byte calibration array");
    A2lCreateCurve(ecuPar.curve1_32, 32, "", "32 byte calibration array");

    A2lCreateParameterWithLimits(ecuPar.cycleTime, "ECU task cycle time (task sleep duration) in us", "us", 50, 1000000);

    A2lParameterGroup("Parameters", 5 + 6 * 3,
        "ecuPar.CALRAM_SIZE",
        "ecuPar.cycleTime",
        "ecuPar.map1_8_8", "ecuPar.curve1_32",
        "ecuPar.period",
        "ecuPar.ampl1", "ecuPar.offset1", "ecuPar.phase1",
        "ecuPar.ampl2", "ecuPar.offset2", "ecuPar.phase2",
        "ecuPar.ampl3", "ecuPar.offset3", "ecuPar.phase3",
        "ecuPar.ampl4", "ecuPar.offset4", "ecuPar.phase4",
        "ecuPar.ampl5", "ecuPar.offset5", "ecuPar.phase5",
        "ecuPar.ampl6", "ecuPar.offset6", "ecuPar.phase6");
       
    A2lSetEvent(gXcpEvent_EcuCyclic); // Associate XCP event "EcuCyclic" to the variables created below
    
    A2lCreateMeasurement(ecuCounter, "16 bit counter incrementing exactly every 100us in XCP slave time");
    A2lCreateMeasurement(ecuCycleCounter, "16 bit counter incrementing every task cycle (event cyclic)");
    A2lCreateMeasurement(ecuTime, "XCP event time as double in s since 10.7.2021 15:14:03");

    A2lCreatePhysMeasurement(channel1, "Sinus signal 1 with period, ampl1, phase1", 1.0, 0.0, "");
    A2lCreatePhysMeasurement(channel2, "Sinus signal 2 with period, ampl2, phase2", 1.0, 0.0, "");
    A2lCreatePhysMeasurement(channel3, "Sinus signal 3 with period, ampl3, phase3", 1.0, 0.0, "");
    A2lCreateMeasurement(byteCounter, "");
    A2lCreateMeasurement(wordCounter, "");
    A2lCreateMeasurement(dwordCounter, "");
    A2lCreateMeasurement_s(sbyteCounter, "");
    A2lCreateMeasurement_s(swordCounter, "");
    A2lCreateMeasurement_s(sdwordCounter, "");

    A2lMeasurementGroup("EcuTaskSignals", 15, 
        "ecuCounter", "ecuCycleCounter", "ecuTime", "channel1", "channel2", "channel3", "channel4", "channel5", "channel6", "byteCounter", "wordCounter", "dwordCounter", "sbyteCounter", "swordCounter", "sdwordCounter");

    A2lCreateMeasurementArray(byteArray1);
    A2lCreateMeasurementArray(byteArray2);
    A2lCreateMeasurementArray(byteArray3);
    A2lCreateMeasurementArray(byteArray4);
    A2lCreateMeasurementArray(byteArray5);
    A2lCreateMeasurementArray(byteArray6);
    A2lCreateMeasurementArray(byteArray7);
    A2lCreateMeasurementArray(byteArray8);
    A2lCreateMeasurementArray(byteArray9);
    A2lCreateMeasurementArray(byteArray10);
    A2lCreateMeasurementArray(byteArray11);
    A2lCreateMeasurementArray(byteArray12);
    A2lCreateMeasurementArray(byteArray13);
    A2lCreateMeasurementArray(byteArray14);
    A2lCreateMeasurementArray(byteArray15);
    A2lCreateMeasurementArray(byteArray16);

    A2lCreateMeasurementArray(longArray1);
    A2lCreateMeasurementArray(longArray2);
    A2lCreateMeasurementArray(longArray3);
    A2lCreateMeasurementArray(longArray4);
    A2lCreateMeasurementArray(longArray5);
    A2lCreateMeasurementArray(longArray6);
    A2lCreateMeasurementArray(longArray7);
    A2lCreateMeasurementArray(longArray8);
    A2lCreateMeasurementArray(longArray9);
    A2lCreateMeasurementArray(longArray10);
    A2lCreateMeasurementArray(longArray11);
    A2lCreateMeasurementArray(longArray12);
    A2lCreateMeasurementArray(longArray13);
    A2lCreateMeasurementArray(longArray14);
    A2lCreateMeasurementArray(longArray15);
    A2lCreateMeasurementArray(longArray16);

    A2lParameterGroup("Arrays", 32,
        "byteArray1", "byteArray2", "byteArray3", "byteArray4", "byteArray5", "byteArray6", "byteArray7", "byteArray8", 
        "byteArray9", "byteArray10", "byteArray11", "byteArray12", "byteArray13", "byteArray14", "byteArray15", "byteArray16", 
        "longArray1", "longArray2", "longArray3", "longArray4", "longArray5", "longArray6", "longArray7", "longArray8", 
        "longArray9", "longArray10", "longArray11", "longArray12", "longArray13", "longArray14", "longArray15", "longArray16");
}
#endif


// Cyclic demo task (default 2ms cycle time)
void ecuCyclic( void )
{
    // Cycle counter
    ecuCycleCounter++;

    // Counters of different type
    sbyteCounter++;
    swordCounter++;
    sdwordCounter++;
    byteCounter++;
    wordCounter++;
    dwordCounter++;

    // Arrays
    unsigned int i = ecuCycleCounter % 1024;
    longArray1[i] ++;
    longArray2[i] ++;
    longArray3[i] ++;
    longArray4[i] ++;
    byteArray1[i] ++;
    byteArray2[i] ++;
    byteArray3[i] ++;
    byteArray4[i] ++;
    
    ecuCounter++;

    // channel 1-6 demo signals
    double x = M_2PI * ecuTime / ecuPar.period;
    channel1 = ecuPar.offset1 + ecuPar.ampl1 * sin(x + ecuPar.phase1);
    channel2 = ecuPar.offset2 + ecuPar.ampl2 * sin(x + ecuPar.phase2);
    channel3 = ecuPar.offset3 + ecuPar.ampl3 * sin(x + ecuPar.phase3);
    ecuTime += 0.002;

    XcpEvent(gXcpEvent_EcuCyclic); // Trigger measurement data aquisition event for ecuCyclic() task
}


// ECU cyclic (2ms default) demo task 
// Calls C ECU demo code
void* ecuTask(void* p) {

    printf("Start C demo task (cycle = %dus, event = %d)\n", ecuPar.cycleTime, gXcpEvent_EcuCyclic);
    for (;;) {
        sleepNs(ecuPar.cycleTime * 1000);
        ecuCyclic();
    }
    return 0;
}
