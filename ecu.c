/*----------------------------------------------------------------------------
| File:
|   ecu.c
|
| Description:
|   Test Measurement and Calibration variables for XCP demo
|   C language
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "ecu.h"



/**************************************************************************/
/* ECU Measurements */
/**************************************************************************/

unsigned short ecuCounter = 0;

double channel1;
double channel2;
double channel3;
double channel4;
double channel5;
double channel6;
static double sTime; // clock as double in s


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

unsigned long longArray1[1024];
unsigned long longArray2[1024];
unsigned long longArray3[1024];
unsigned long longArray4[1024];
unsigned long longArray5[1024];
unsigned long longArray6[1024];
unsigned long longArray7[1024];
unsigned long longArray8[1024];
unsigned long longArray9[1024];
unsigned long longArray10[1024];
unsigned long longArray11[1024];
unsigned long longArray12[1024];
unsigned long longArray13[1024];
unsigned long longArray14[1024];
unsigned long longArray15[1024];
unsigned long longArray16[1024];

unsigned char byteCounter;
unsigned short wordCounter;
unsigned long dwordCounter;
signed char sbyteCounter;
signed short swordCounter;
signed int sdwordCounter;

char testString[] = "TestString"; 



/**************************************************************************/
/* ECU Parameters */
/**************************************************************************/

#define PI2 6.28318530718

volatile double period = 3.0;
volatile double offset1 = 0.0;
volatile double offset2 = 0.0;
volatile double offset3 = 0.0;
volatile double offset4 = 50.0;
volatile double offset5 = 50.0;
volatile double offset6 = 50.0;
volatile double phase1 = 0;
volatile double phase2 = PI2 / 3;
volatile double phase3 = 2 * PI2 / 3;
volatile double phase4 = 0;
volatile double phase5 = PI2 / 3;
volatile double phase6 = 2 * PI2 / 3;
volatile double ampl1 = 400.0;
volatile double ampl2 = 300.0;
volatile double ampl3 = 200.0;
volatile double ampl4 = 100.0;
volatile double ampl5 = 75.0;
volatile double ampl6 = 50.0;


volatile unsigned char map1_8_8[8][8] =
{ {0,0,0,0,0,0,1,2},
 {0,0,0,0,0,0,2,3},
 {0,0,0,0,1,1,2,3},
 {0,0,0,1,1,2,3,4},
 {0,1,1,2,3,4,5,7},
 {1,1,1,2,4,6,8,9},
 {1,1,2,4,5,8,9,10},
 {1,1,3,5,8,9,10,10}
};

volatile unsigned char curve1_32[32] =
{ 0,1,3,6,9,15,20,30,38,42,44,46,48,50,48,45,40,33,25,15,5,4,3,2,1,0,0,1,4,8,4,0};




/**************************************************************************/
/* ECU Demo Code */
/**************************************************************************/

// Init
void ecuInit(void) {

    ecuCounter = 0;

    channel1 = 0;
    channel2 = 0;
    channel3 = 0;
    channel4 = 0;
    channel5 = 0;
    channel6 = 0;

    byteCounter = 0;
    wordCounter = 0;
    dwordCounter = 0;
    sbyteCounter = 0;
    swordCounter = 0;
    sdwordCounter = 0;

    for (int i = 0; i < 1024; i++) {
        byteArray1[i] = (unsigned char)i;
        byteArray2[i] = (unsigned char)i;
        byteArray3[i] = (unsigned char)i;
        byteArray4[i] = (unsigned char)i;
        byteArray5[i] = (unsigned char)i;
        byteArray6[i] = (unsigned char)i;
        byteArray7[i] = (unsigned char)i;
        byteArray8[i] = (unsigned char)i;
        byteArray9[i] = (unsigned char)i;
        byteArray10[i] = (unsigned char)i;
        byteArray11[i] = (unsigned char)i;
        byteArray12[i] = (unsigned char)i;
        byteArray13[i] = (unsigned char)i;
        byteArray14[i] = (unsigned char)i;
        byteArray15[i] = (unsigned char)i;
        byteArray16[i] = (unsigned char)i;
    }

    for (unsigned int i = 0; i < 1024; i++) {
        longArray1[i] = i;
        longArray2[i] = i;
        longArray3[i] = i;
        longArray4[i] = i;
        longArray5[i] = i;
        longArray6[i] = i;
        longArray7[i] = i;
        longArray8[i] = i;
        longArray9[i] = i;
        longArray10[i] = i;
        longArray11[i] = i;
        longArray12[i] = i;
        longArray13[i] = i;
        longArray14[i] = i;
        longArray15[i] = i;
        longArray16[i] = i;
    }
}


// Create A2L File content
#ifdef XCPSIM_ENABLE_A2L_GEN
void ecuCreateA2lDescription(void) {


    A2lCreateParameterWithLimits(ampl1, "Amplitude of U1p", "V", 0, 800);
    A2lCreateParameterWithLimits(offset1, "Offset of U1p", "V", -200, +200);
    A2lCreateParameterWithLimits(phase1, "Phase of U1P", "", 0, PI2);
    A2lCreateParameterWithLimits(ampl2, "Amplitude of U2p", "V", 0, 800);
    A2lCreateParameterWithLimits(offset2, "Offset of U2p", "V", -200, +200);
    A2lCreateParameterWithLimits(phase2, "Phase of U2p", "", 0, PI2);
    A2lCreateParameterWithLimits(ampl3, "Amplitude of U3p", "V", 0, 800);
    A2lCreateParameterWithLimits(offset3, "Offset of U3p", "V", -200, +200);
    A2lCreateParameterWithLimits(phase3, "Phase of U3p", "", 0, PI2);
    A2lCreateParameterWithLimits(ampl4, "Amplitude of I1p", "A", 0, 800);
    A2lCreateParameterWithLimits(offset4, "Offset of I1p", "A", -200, +200);
    A2lCreateParameterWithLimits(phase4, "Phase of I1p", "", 0, PI2);
    A2lCreateParameterWithLimits(ampl5, "Amplitude of I2p", "A", 0, 800);
    A2lCreateParameterWithLimits(offset5, "Offset of I2p", "A", -200, +200);
    A2lCreateParameterWithLimits(phase5, "Phase of I2p", "", 0, PI2);
    A2lCreateParameterWithLimits(ampl6, "Amplitude of I3p", "A", 0, 800);
    A2lCreateParameterWithLimits(offset6, "Offset of I3p", "A", -200, +200);
    A2lCreateParameterWithLimits(phase6, "Phase of I3p", "", 0, PI2);
    A2lCreateParameterWithLimits(period, "Period", "s", 0, 10);
    A2lParameterGroup("eMobParameters", 6*3+1, "period", "ampl1", "offset1", "phase1", "ampl2", "offset2", "phase2", "ampl3", "offset3", "phase3", "ampl4", "offset4", "phase4", "ampl5", "offset5", "phase5", "ampl6", "offset6", "phase6");


    A2lCreateMap(map1_8_8, 8, 8, "", "8*8 byte calibration array");
    A2lCreateCurve(curve1_32, 32, "", "32 byte calibration array");
    A2lParameterGroup("EcuTaskParameters", 2, "map1_8_8", "curve1_32");


    A2lSetEvent(gXcpEvent_EcuCyclic); // Associate XCP event "EcuCyclic" to the variables created below
    A2lCreateMeasurement(ecuCounter);
    A2lCreatePhysMeasurement(channel1, "Demo signal 1", 1.0, 0.0, "");
    A2lCreatePhysMeasurement(channel2, "Demo signal 2", 1.0, 0.0, "");
    A2lCreatePhysMeasurement(channel3, "Demo signal 3", 1.0, 0.0, "");
    A2lCreatePhysMeasurement(channel4, "Demo signal 4", 1.0, 0.0, "");
    A2lCreatePhysMeasurement(channel5, "Demo signal 5", 1.0, 0.0, "");
    A2lCreatePhysMeasurement(channel6, "Demo signal 6", 1.0, 0.0, "");

    A2lCreateMeasurement(byteCounter);
    A2lCreateMeasurement(wordCounter);
    A2lCreateMeasurement(dwordCounter);
    A2lCreateMeasurement_s(sbyteCounter);
    A2lCreateMeasurement_s(swordCounter);
    A2lCreateMeasurement_s(sdwordCounter);

    A2lMeasurementGroup("EcuTaskSignals", 13, "ecuCounter", "channel1", "channel2", "channel3", "channel4", "channel5", "channel6", "byteCounter", "wordCounter", "dwordCounter", "sbyteCounter", "swordCounter", "sdwordCounter");

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
  ecuCounter++;

  // channel 1-6 demo signals
  channel1 = offset1 + ampl1 * sin(PI2 * sTime / period + phase1);
  channel2 = offset2 + ampl2 * sin(PI2 * sTime / period + phase2);
  channel3 = offset3 + ampl3 * sin(PI2 * sTime / period + phase3);
  channel4 = offset4 + ampl4 * sin(PI2 * sTime / period + phase4);
  channel5 = offset5 + ampl5 * sin(PI2 * sTime / period + phase5);
  channel6 = offset6 + ampl6 * sin(PI2 * sTime / period + phase6);

  sTime = sTime + 0.002; // time in s + 2ms


  // Arrays
  longArray1[0] ++;
  longArray2[0] ++;
  longArray3[0] ++;
  longArray4[0] ++; 
  byteArray1[0] ++; 
  byteArray2[0] ++; 
  byteArray3[0] ++; 
  byteArray4[0] ++; 
 
    
  // Counters of different type
  sbyteCounter++;
  swordCounter++;
  sdwordCounter++;
  byteCounter++;
  wordCounter++;
  dwordCounter++;

  XcpEvent(gXcpEvent_EcuCyclic); // Trigger measurement data aquisition event for ecuCyclic() task
}



