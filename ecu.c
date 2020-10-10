/*----------------------------------------------------------------------------
| File:
|   ecu.c
|   V1.0 23.9.2020
|
| Description:
|   Measurement and Calibration variables for XCP demo
|   C language
|
 ----------------------------------------------------------------------------*/

#include "ecu.h"
#include "xcpLite.h"

#include <math.h>

/**************************************************************************/
/* ECU Measurement RAM */
/**************************************************************************/

//make sure all of these variables go into RAM

unsigned short counter = 0;


float timer;
float channel1;
float channel2;
float channel3;
float channel4;

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
signed long sdwordCounter;


signed char sbyteTriangleSlope;
signed char sbyteTriangle;
unsigned char bytePWM;
unsigned char bytePWMFiltered;

unsigned char testbyte1;
unsigned char testbyte2;
unsigned char testbyte3;
unsigned char testbyte4;
unsigned char testbyte5;
unsigned char testbyte6;
unsigned char testbyte7;
unsigned char testbyte8;
unsigned char testbyte9;
unsigned char testbyte0;

unsigned short vin;
unsigned short vdiff;
unsigned short v;

char testString[] = "TestString"; 


/**************************************************************************/
/* ECU Calibration RAM */
/**************************************************************************/


unsigned short CALRAM_START = 0xAAAA;

unsigned char flashSignatur[32] = "Default";

volatile float period = 5;	 
volatile float ampl   = 6;
volatile float limit  = 100;
volatile float offset = 0;
volatile float filter = 0;

volatile unsigned short  a = 1;
volatile unsigned short  b = 5;
volatile unsigned short  c = 6;
volatile signed char sbytePWMLevel = 0;
volatile unsigned char	bytePWMFilter = 50;

volatile unsigned char	byte1  = 1;
volatile unsigned char  byte2  = 2;
volatile signed char  byte3  = 3;
volatile unsigned char	byte4  = 4;

volatile unsigned short word1  = 1;
volatile unsigned short word2  = 1;
volatile unsigned short word3  = 1;
volatile unsigned short word4  = 1;

volatile unsigned long dword1 = 1;
volatile unsigned long dword2 = 1;
volatile unsigned long dword3 = 1;
volatile unsigned long dword4 = 1;

volatile unsigned char map1Counter = 25;


volatile unsigned char map1_8_8_uc[8][8] =
  {{0,0,0,0,0,0,1,2},
   {0,0,0,0,0,0,2,3},
   {0,0,0,0,1,1,2,3},
   {0,0,0,1,1,2,3,4},
   {0,1,1,2,3,4,5,7},
   {1,1,1,2,4,6,8,9},
   {1,1,2,4,5,8,9,10},
   {1,1,3,5,8,9,10,10}
};

volatile unsigned char map2_8_8_uc[8][8] = {
  { 1, 2, 3, 4, 5, 6, 7, 8},
  {11,12,13,14,15,16,17,18},
  {21,22,23,24,25,26,27,28},
  {31,32,33,34,35,36,37,38},
  {41,42,43,44,45,46,47,48},
  {51,52,53,54,55,56,57,58},
  {61,62,63,64,65,66,67,68},
  {71,72,73,74,75,76,77,78}
};


volatile unsigned char map4_80_uc[80] =
  {
   0,  1,  2,  3,  4,  5,  6,  7,    /* X Coordinates */
   100,101,102,103,104,105,106,107,  /* Y Coordinates */
   1,2,3,4,5,6,7,8,		     /* Values		  */
   1,2,3,4,5,6,7,8,		     /* Values		  */
   1,2,3,4,5,6,7,8,		     /* Values		  */
   1,2,3,4,5,6,7,8,		     /* Values		  */
   1,2,3,4,5,6,7,8,		     /* Values		  */
   1,2,3,4,5,6,7,8,		     /* Values		  */
   1,2,3,4,5,6,7,8,		     /* Values		  */
   1,2,3,4,5,6,7,8		     /* Values		  */
};


volatile unsigned char map5_82_uc[82] =
  {
   8, 0,1,2,3,4,5,6,7,  /* X-coordinates */
   8, 0,1,2,3,4,5,6,7,  /* Y-coordinates */
   0,0,0,0,0,0,1,2,
   0,0,0,0,0,0,2,3,
   0,0,0,0,1,1,2,3,
   0,0,0,1,1,2,3,4,
   0,1,1,2,3,4,5,7,
   1,1,1,2,4,6,8,9,
   1,1,2,4,5,8,9,10,
   1,1,3,5,8,9,10,10
};


unsigned long CALRAM_SIGN = 0x0055AAFF;
unsigned short CALRAM_LAST = 0xAAAA;



/**************************************************************************/
/* ECU Demo */
/**************************************************************************/



void ecuInit( void ) {

    counter = 0;

    timer  = 0;
    channel1 = 0;
    channel2 = 0;
    channel3 = 0;
    channel4 = 0;

    
    byteCounter  = 0;
    wordCounter = 0;
    dwordCounter = 0;
    sbyteCounter	= 0;
    swordCounter = 0;
    sdwordCounter = 0;
    
    sbyteTriangleSlope = 1;
    sbyteTriangle = 0;
    bytePWM = 0;
    bytePWMFiltered = 0;
    testbyte1 = 101;
    testbyte2 = 102;
    testbyte3 = 103;
    testbyte4 = 104;
    testbyte5 = 105;
    testbyte6 = 106;
    testbyte7 = 107;
    testbyte8 = 108;
    testbyte9 = 109;
    testbyte0 = 100;
    vin = vdiff = 0;
    v = 0;
    
  
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
}




/* 10ms Raster */
void ecuCyclic( void )
{
    counter++;

  /* Floatingpoint sine signals */
  if (period>0.01||period<-0.01) {
	channel1  = (float)(offset+sin(6.283185307*timer/period*1)*ampl);
	if (channel1>+limit) channel1 = +limit;
	if (channel1<-limit) channel1 = -limit;
	//channel2  = (float)(sin(6.283185307*timer/period*2)*ampl);
	//channel3  = (float)(sin(6.283185307*timer/period*3)*ampl);
	//channel4  = (float)(sin(6.283185307*timer/period*4)*ampl);
  }
  timer = (float)(timer+0.01);
 
  // Measurement Arrays 
  longArray1[0] ++;
  longArray2[0] ++;
  longArray3[0] ++;
  longArray4[0] ++; 
  
  byteArray1[1] ++; byteArray1[0] = 1;
  byteArray2[1] ++; byteArray2[0] = 2;
  byteArray3[1] ++; byteArray3[0] = 3;
  byteArray4[1] ++; byteArray4[0] = 4;
  byteArray5[1] ++; byteArray5[0] = 5;
  byteArray6[1] ++; byteArray6[0] = 6;
  byteArray7[1] ++; byteArray7[0] = 7;
  byteArray8[1] ++; byteArray8[0] = 8;
  byteArray9[1] ++; byteArray9[0] = 9;
  byteArray10[1] ++; byteArray10[0] = 10;
  byteArray11[1] ++; byteArray12[0] = 11;
  byteArray12[1] ++; byteArray12[0] = 12;
  byteArray13[1] ++; byteArray13[0] = 13;
  byteArray14[1] ++; byteArray14[0] = 14;
  byteArray15[1] ++; byteArray15[0] = 15;
  byteArray16[1] ++; byteArray16[0] = 16;


  
  /* PWM Example */
  sbyteTriangle += sbyteTriangleSlope;
  if (sbyteTriangle>=50) sbyteTriangleSlope = -1;
  if (sbyteTriangle<=-50) sbyteTriangleSlope = 1;
  if (sbyteTriangle>sbytePWMLevel) {
    bytePWM = 100;
  } else {
    bytePWM = 0;
  }
  bytePWMFiltered = (bytePWMFilter*bytePWMFiltered+(100-bytePWMFilter)*bytePWM)/100;

  /* Counters */
  byteCounter++;
  wordCounter++;
  dwordCounter++;
  sbyteCounter++;
  swordCounter++;
  sdwordCounter++;

  

  // Filter example
    if (c==0) {
      v = 0;
    } else {
      v = (unsigned short)((a*vin + b*v)/c);
    }

  
    XcpEvent(1); // Trigger measurement date aquisition event 1

}



