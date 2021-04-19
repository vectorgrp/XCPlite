/*----------------------------------------------------------------------------
| File:
|   ecu.c
|
| Description:
|   Test Measurement and Calibration variables for XCP demo
|   C language
|
 ----------------------------------------------------------------------------*/

#include "ecu.h"
#include "xcpLite.h"
#include "A2L.h"

#include <math.h>



/**************************************************************************/
/* ECU Measurements */
/**************************************************************************/

unsigned short ecuCounter = 0;
double timer;
double channel1;
double channel1p[100];

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


volatile double offset = 0.0;
volatile double period = 5.0;
volatile double ampl   = 50.0;

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

    timer = 0;
    channel1 = 0;
    for (int i = 0; i < 100; i++) channel1p[i] = i / 100.0; // Packed mode example, 100 samples

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
    }
}


// Create A2L File content
#ifdef XCP_ENABLE_A2L
void ecuCreateA2lDescription( void) {
      
    A2lSetEvent(gXcpEvent_EcuCyclic_packed); // Associate XCP event "EcuCyclic" to the variables created below
    A2lCreatePhysMeasurement(channel1p[0], "Demo triangle time series packet, 100 samples", 1.0, 0.0, "V");
    
    A2lSetEvent(gXcpEvent_EcuCyclic); // Associate XCP event "EcuCyclic" to the variables created below
    A2lCreateMeasurement(ecuCounter);
    A2lCreatePhysMeasurement(timer, "Time in s", 1.0, 0.0, "s");
    A2lCreatePhysMeasurement(channel1, "Demo sine wave signal", 1.0, 0.0, "V");
    A2lCreateParameterWithLimits(ampl, "Amplitude", "V", 0, 100);
    A2lCreateParameterWithLimits(offset, "Offset", "V", -50, +50);
    A2lCreateParameterWithLimits(period, "Period", "s", 0, 10);
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

    A2lParameterGroup("Arrays", 20, 
        "byteArray1", "byteArray2", "byteArray3", "byteArray4", "byteArray5", "byteArray6", "byteArray7", "byteArray8", "byteArray9", "byteArray10", 
        "byteArray11", "byteArray12", "byteArray13", "byteArray14", "byteArray15", "byteArray16", "longArray1", "longArray2", "longArray3", "longArray4");
}
#endif




// Cyclic demo task
void ecuCyclic( void )
{
  // Cycle counter
  ecuCounter++;
    
  // Sine wave
  if (period>0.01||period<-0.01) {
      channel1 = sin(6.283185307 * timer / period);
      channel1 = offset + ( ampl * channel1 );
  }
  timer = (timer + 0.001);
 
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


}



