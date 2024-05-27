/*----------------------------------------------------------------------------
| File:
|   ecu.c
|
| Description:
|   Measurement and calibration demo for XCP demo
|   C language
 ----------------------------------------------------------------------------*/
 /*
 | Code released into public domain, no attribution required
 */

#include "main.h"
#include "platform.h"
#include "xcpLite.h"
#if OPTION_ENABLE_A2L_GEN
#include "A2L.h"
#endif
#include "ecu.h"


/**************************************************************************/
/* ECU Measurements */
/**************************************************************************/

// Default cycle time of ecuTask() in us
// Realized with simple Sleep(), note that Windows can not sleep less than 15ms
#ifdef _LINUX
#define ECU_TASK_CYCLE_TIME_US 2000 
#else
#define ECU_TASK_CYCLE_TIME_US 15000 
#endif


static uint16_t gXcpEvent_EcuCyclic = 0; // XCP event number
static uint64_t ecuStartTime = 0; // Time at ECU startup


/**************************************************************************/
/* ECU Measurement Signals */
/**************************************************************************/

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
/* ECU Calibration Parameters */
/**************************************************************************/

struct ecuPar {
    char epk[32];
    uint32_t cycleTimeUs;
    double period;
    double offset;
    double phase;
    double ampl;
    uint8_t map1_8_8[8][8];
    uint8_t curve1_32[32];
};


#if OPTION_ENABLE_CAL_SEGMENT
const
#endif
struct ecuPar ecuPar = {
    __DATE__ " " __TIME__, // EPK
    ECU_TASK_CYCLE_TIME_US, // Default cycle time in us
    1.0,    // period
    0.0,    // offset
    0,      // phase
    400.0,  // ampl
    { {0,0,0,0,0,0,1,2}, // map1_8_8
     {0,0,0,0,0,0,2,3},
     {0,0,0,0,1,1,2,3},
     {0,0,0,1,1,2,3,4},
     {0,1,1,2,3,4,5,7},
     {1,1,1,2,4,6,8,9},
     {1,1,2,4,5,8,9,10},
     {1,1,3,5,8,9,10,10}
    },
    { 0,1,3,6,9,15,20,30,38,42,44,46,48,50,48,45,40,33,25,15,5,4,3,2,1,0,0,1,4,8,4,0} // curve1_32
};


#if OPTION_ENABLE_CAL_SEGMENT
/*
Calibration parameters are collected in a C struct
This is used to create a RAM and a FLASH version (const) of the parameter set
The FLASH parameter set is treated as readonly
Demo code accesses parameters through a pointer
The calibration tool can switch between the parameters sets or reinitialized the RAM patameter set from FLASH
*/
volatile struct ecuPar* ecuCalPage = (struct ecuPar*)&ecuPar; // Calibration page pointer
volatile struct ecuPar ecuRamPar;
#define ECU_PAR(x) (ecuCalPage->x)
#else
#define ECU_PAR(x) (ecuPar.x)
#endif

/**************************************************************************/
/* ECU Demo Code */
/**************************************************************************/

char* ecuGetEPK() {
    return (char*)ecuPar.epk;
}

#if OPTION_ENABLE_CAL_SEGMENT

// Calibration page handling
// page 0 is RAM, page 1 is FLASH
#define RAM 0
#define FLASH 1
// A2L file contains the FLASH addresses

void ecuParInit() { // Initialize RAM calibration page from FLASH and switch to RAM

    memcpy((void*)&ecuRamPar, (void*)&ecuPar, sizeof(struct ecuPar)); 
    ecuParSetCalPage(RAM);
}

void ecuParSetCalPage(uint8_t page) { // Set the current calibration page

    ecuCalPage = (page == RAM) ? &ecuRamPar : (struct ecuPar *)&ecuPar;
}

uint8_t ecuParGetCalPage() { // Get the current calibration page

    return (ecuCalPage == &ecuRamPar) ? RAM : FLASH;
}

uint8_t *ecuParAddrMapping( uint8_t *a ) {

    if (a >= (uint8_t*)&ecuPar && a < (uint8_t*)&ecuPar + sizeof(struct ecuPar)) {
      if (ecuCalPage == &ecuPar) return a; // RAM
      assert(ecuCalPage == &ecuRamPar);
      return (uint8_t*)&ecuRamPar + (a - (uint8_t*)&ecuPar); // ROM -> RAM
    }
    return a;
}

#endif // OPTION_ENABLE_CAL_SEGMENT


// Init demo parameters and measurements 
void ecuInit() {

    ecuStartTime = clockGet();

    // Initialize calibration parameters
#if OPTION_ENABLE_CAL_SEGMENT
    ecuParInit(); // Initializes parameters in RAM calibration segment
    ecuParSetCalPage(RAM); // Switch to RAM calibration segment 
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

    // Create an XCP event for the cyclic task
    gXcpEvent_EcuCyclic = XcpCreateEvent("ecuTask", ECU_TASK_CYCLE_TIME_US*1000, 0, 0, 0);
}


#if OPTION_ENABLE_A2L_GEN

// Create demo A2L file 
void ecuCreateA2lDescription() {

    // Calibration Memory Segment
#if OPTION_ENABLE_CAL_SEGMENT  
    A2lCreate_MOD_PAR(ApplXcpGetAddr((uint8_t*)&ecuPar), sizeof(struct ecuPar), (char*)ecuPar.epk);
#endif

    // Parameters
#ifdef __cplusplus
    A2lCreateParameterWithLimits(ecuPar.ampl, "Amplitude", "V", 0, 800);
    A2lCreateParameterWithLimits(ecuPar.offset, "RefOffset", "V", -200, +200);
    A2lCreateParameterWithLimits(ecuPar.phase, "Phase", "", 0, M_2PI);
    A2lCreateParameterWithLimits(ecuPar.period, "Period in s", "s", 0, 10);
    A2lCreateParameterWithLimits(ecuPar.cycleTimeUs, "ECU task cycle time in us", "us", 50, 1000000);
    A2lCreateMap(ecuPar.map1_8_8, 8, 8, "8*8 byte calibration array", "");
    A2lCreateCurve(ecuPar.curve1_32, 32, "32 byte calibration array", "");
#else
    A2lCreateParameterWithLimits(ecuPar.ampl, A2L_TYPE_DOUBLE, "Amplitude", "V", 0, 800);
    A2lCreateParameterWithLimits(ecuPar.offset, A2L_TYPE_DOUBLE, "RefOffset", "V", -200, +200);
    A2lCreateParameterWithLimits(ecuPar.phase, A2L_TYPE_DOUBLE, "Phase", "", 0, M_2PI);
    A2lCreateParameterWithLimits(ecuPar.period, A2L_TYPE_DOUBLE, "Period in s", "s", 0, 10);
    A2lCreateParameterWithLimits(ecuPar.cycleTimeUs, A2L_TYPE_UINT32, "ECU task cycle time in us", "us", 50, 1000000);
    A2lCreateMap(ecuPar.map1_8_8, A2L_TYPE_UINT8, 8, 8, "8*8 byte calibration array", "");
    A2lCreateCurve(ecuPar.curve1_32, A2L_TYPE_UINT8, 32, "32 byte calibration array", "");
#endif
    // Create a group for the parameters (optional)
    A2lParameterGroup("Parameters", 7,        
        "ecuPar.cycleTimeUs", 
        "ecuPar.map1_8_8", "ecuPar.curve1_32",
        "ecuPar.period", "ecuPar.ampl", "ecuPar.phase", "ecuPar.offset" );

    // Measurements
    A2lSetFixedEvent(gXcpEvent_EcuCyclic); // Associate XCP event "EcuCyclic" to the variables created below
#ifdef __cplusplus
    A2lCreateMeasurement(byteCounter, "");
    A2lCreateMeasurement(wordCounter, "");
    A2lCreateMeasurement(dwordCounter, "");
    A2lCreateMeasurement(sbyteCounter, "");
    A2lCreateMeasurement(swordCounter, "");
    A2lCreateMeasurement(sdwordCounter, "");
    A2lCreatePhysMeasurement(channel1, "Sinus signal 1", 1.0, 0.0, "");
    A2lCreatePhysMeasurement(channel2, "Sinus signal 2", 1.0, 0.0, "");
    A2lCreatePhysMeasurement(channel3, "Sinus signal 3", 1.0, 0.0, "");
    A2lCreateMeasurementArray(byteArray1);
    A2lCreateMeasurementArray(longArray1);
#else
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
#endif 

    // Create groups (optional)
    A2lMeasurementGroup("EcuTaskSignals", 9,
      "channel1", "channel2", "channel3", "byteCounter", "wordCounter", "dwordCounter", "sbyteCounter", "swordCounter", "sdwordCounter");
    A2lParameterGroup("EcuTaskArrays", 2,
      "byteArray1", "longArray1");
}

#endif


// Cyclic demo task 
void ecuCyclic( void )
{
    ecuTime = (double)(clockGet()-ecuStartTime)/CLOCK_TICKS_PER_S;

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
    double x = M_2PI * ecuTime / ECU_PAR(period);
    channel1 = ECU_PAR(offset) + ECU_PAR(ampl) * sin(x);
    channel2 = ECU_PAR(offset) + ECU_PAR(ampl) * sin(x + M_PI * 1 / 3);
    channel3 = ECU_PAR(offset) + ECU_PAR(ampl) * sin(x + M_PI * 2 / 3);

    XcpEvent(gXcpEvent_EcuCyclic); // Trigger XCP measurement data aquisition event 
}


// ECU cyclic (2ms default) demo task
#ifdef _WIN
DWORD WINAPI ecuTask(LPVOID p)
#else
void* ecuTask(void* p)
#endif
{
    (void)p;
    printf("Start C task (cycle = %dus, XCP event = %d)\n", ECU_PAR(cycleTimeUs), gXcpEvent_EcuCyclic);
    for (;;) {
        sleepNs(ECU_PAR(cycleTimeUs) * 1000); // cycletime is a calibration parameter
        ecuCyclic();
    }
}
