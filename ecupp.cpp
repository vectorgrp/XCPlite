/*----------------------------------------------------------------------------
| File:
|   ecupp.cpp
|
| Description:
|   Test Measurement and Calibration variables for XCP demo
|   C++ language, Classes and Structures, dynamic memory
|   Experimental for new XCP features
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
	
	byte = 0;
	word = 0;
	dword = 0;
}

// Cyclic task
void ecu::task() {

	counter++;

	byte++;
	word++;
	dword++;

	XcpEventExt(2, (BYTEPTR)this); // Trigger measurement data aquisition event 2
}


