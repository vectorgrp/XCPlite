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

// XCP 
extern "C" {
	#include "xcpLite.h"
    #include "A2L.h"
}




// Constructor 
ecu::ecu() {

	ecuppCounter = 0;
	
	byte = 1;
	word = 1;
	dword = 1;
	sword = -1;

#ifdef XCP_ENABLE_A2L

	//A2lBeginGroup("ECUPP");

	A2lCreateMeasurementType("ecu","class ecu");

	A2lSetEvent(2);
	A2lCreateMeasurement_abs("gEcu", ecuppCounter);

	A2lSetEvent(3);
	A2lCreateMeasurement_rel("ecu", ecuppCounter);
	A2lCreateMeasurement_rel("ecu", byte);
	A2lCreateMeasurement_rel("ecu", word);
	A2lCreateMeasurement_rel("ecu", dword);
	
	//A2lEndGroup("ECUPP");

#endif


}

// Cyclic task
void ecu::task() {

	ecuppCounter++;

	byte++;
	word++;
	dword++;

	XcpEventExt(3, (BYTEPTR)this); // Trigger measurement data aquisition event 2
	XcpEvent(2); 
	
}


