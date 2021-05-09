/*----------------------------------------------------------------------------
| File:
|   ecu.c
|
| Description:
|   Test Measurement and Calibration variables for XCP demo
|   C language
|
 ----------------------------------------------------------------------------*/

#include "xcpLite.h"
#include "xcpAppl.h"
#include "A2L.h"

#include <math.h>
#include "ecu.h"



/**************************************************************************/
/* ECU Measurements */
/**************************************************************************/

unsigned short ecuCounter = 0;

double time1; // clock as double in s
double channel1;
double channel2;
double channel3;

unsigned char byteArray1[1400];
unsigned char byteArray2[1400];
unsigned char byteArray3[1400];
unsigned char byteArray4[1400];
unsigned char byteArray5[1400];
unsigned char byteArray6[1400];
unsigned char byteArray7[1400];
unsigned char byteArray8[1400];
unsigned char byteArray9[1400];
unsigned char byteArray10[1400];
unsigned char byteArray11[1400];
unsigned char byteArray12[1400];
unsigned char byteArray13[1400];
unsigned char byteArray14[1400];
unsigned char byteArray15[1400];
unsigned char byteArray16[1400];

unsigned long longArray1[1024];
unsigned long longArray2[1024];
unsigned long longArray3[1024];
unsigned long longArray4[1024];

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

volatile double period = 4.0;
volatile double offset = 0.0;
volatile double phase = PI2 / 2;
volatile double threshold = 0;
volatile double ampl = 400.0;

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
    time1 = 0; 
    
    byteCounter = 0;
    wordCounter = 0;
    dwordCounter = 0;
    sbyteCounter = 0;
    swordCounter = 0;
    sdwordCounter = 0;

    for (int i = 0; i < 1400; i++) {
        byteArray1[i] = (unsigned char)i;
        byteArray2[i] = (unsigned char)i;
        byteArray3[i] = (unsigned char)i;
        byteArray4[i] = (unsigned char)i;
    }

    for (unsigned int i = 0; i < 1024; i++) {
        longArray1[i] = i;
        longArray2[i] = i;
        longArray3[i] = i;
        longArray4[i] = i;
    }
}


// Create A2L File content
#ifdef XCP_ENABLE_A2L
void ecuCreateA2lDescription( void) {
      
    A2lCreateParameterWithLimits(ampl, "Amplitude", "V", 0, 800);
    A2lCreateParameterWithLimits(offset, "Offset of channel 1", "V", -200, +200);
    A2lCreateParameterWithLimits(phase, "Phase of channel 1", "", 0, PI2);
    A2lCreateParameterWithLimits(threshold, "Threshold for channel 3", "", -400, 400);
    A2lCreateParameterWithLimits(period, "Period", "", 0.01, 10);
    A2lParameterGroup("Parameters", 5, "period", "ampl", "offset", "phase", "threshold");


    A2lSetEvent(gXcpEvent_EcuCyclic); // Associate XCP event "EcuCyclic" to the variables created below
    A2lCreateMeasurement(ecuCounter);
    A2lCreatePhysMeasurement(channel1, "Demo signal 1", 1.0, 0.0, "");
    A2lCreatePhysMeasurement(channel2, "Demo signal 2", 1.0, 0.0, "");
    A2lCreatePhysMeasurement(channel3, "Demo signal 3", 1.0, 0.0, "");

    A2lCreateMap(map1_8_8, 8, 8, "", "8*8 byte calibration array");
    A2lCreateCurve(curve1_32, 32, "", "32 byte calibration array");
    
    A2lCreateMeasurement(byteCounter);
    A2lCreateMeasurement(wordCounter);
    A2lCreateMeasurement(dwordCounter);
    A2lCreateMeasurement_s(sbyteCounter);
    A2lCreateMeasurement_s(swordCounter);
    A2lCreateMeasurement_s(sdwordCounter);

    A2lCreateMeasurementArray(byteArray1);
    A2lCreateMeasurementArray(byteArray2);
    A2lCreateMeasurementArray(byteArray3);
    A2lCreateMeasurementArray(byteArray4);
    
    A2lCreateMeasurementArray(longArray1);
    A2lCreateMeasurementArray(longArray2);
    A2lCreateMeasurementArray(longArray3);
    A2lCreateMeasurementArray(longArray4);

    A2lParameterGroup("Arrays", 8, "byteArray1", "byteArray2", "byteArray3", "byteArray4", "longArray1", "longArray2", "longArray3", "longArray4");
}
#endif





// Cyclic demo task (default 2ms cycle time)
void ecuCyclic( void )
{
  // Cycle counter
  ecuCounter++;

  // channel 1-6 demo signals
  channel1 = offset + ampl * sin(PI2 * time1 / period + phase);
  channel2 = ampl * sin(PI2 * time1 / period );
  channel3 = (channel1 > threshold) * ampl;
  time1 = time1 + 0.001; 

  // Arrays
  longArray1[0] ++;
  longArray2[0] ++;
  longArray3[0] ++;
  longArray4[0] ++; 
  byteArray1[0] ++; 
  byteArray2[0] ++; 
  byteArray3[0] ++; 
  byteArray4[0] ++; 
  byteArray5[0] ++; 
  byteArray6[0] ++; 
  byteArray7[0] ++; 
  byteArray8[0] ++; 
  byteArray9[0] ++; 
  byteArray10[0] ++;
  byteArray11[0] ++;
  byteArray12[0] ++;
  byteArray13[0] ++;
  byteArray14[0] ++;
  byteArray15[0] ++;
  byteArray16[0] ++;
    
  // Counters of different type
  sbyteCounter++;
  swordCounter++;
  sdwordCounter++;
  byteCounter++;
  wordCounter++;
  dwordCounter++;

  XcpEvent(gXcpEvent_EcuCyclic); // Trigger measurement data aquisition event for ecuCyclic() task
}



