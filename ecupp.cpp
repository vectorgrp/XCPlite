/*----------------------------------------------------------------------------
| File:
|   ecupp.cpp
|   V1.0 23.9.2020
|
| Description:
|   Measurement and Calibration variables for XCP demo
|   C++ language
|
 ----------------------------------------------------------------------------*/


#include <iostream>
using namespace std;


#include "ecupp.hpp"


extern "C" {

	// XCP driver - XcpEventExt
	#include "xcpLite.h"
}




// Constructor 
ecu::ecu() {

	counter = 0;
	pCounter = &counter;
	byte = 0;
	word = 0;
	dword = 0;

	cout << "ecu() constructor\n";	
	
}

// Cyclic task
void ecu::task() {

	counter++;

	byte++;
	word++;
	dword++;

	XcpEventExt(2, (BYTEPTR)this); // Trigger measurement data aquisition event 2
}


