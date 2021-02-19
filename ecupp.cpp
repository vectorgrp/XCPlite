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

// XCP 
extern "C" {
	#include "xcpLite.h"
    #include "A2L.h"
}
#include "ecupp.hpp"


#ifdef XCP_ENABLE_A2L

// Static method to create A2L description of this class (instanceName==NULL) or this instance
void EcuTask::CreateA2lClassDescription( int eventCount, const unsigned int eventList[] ) {

	// Create class variables and a typedef
	
		 // Todo: Create a typedef
		// A2lCreateMeasurementType("ecu", "structure typedef for class ecu"); // Create a A2L typedef for class ecu

	// Todo: EventList
		
        #define offsetOf(x) ((int)&x - (int)this)
		
		A2lCreateMeasurement_rel("EcuTask", "counter", counter, offsetOf(counter)); // Create measurement signal ecuppCounter with relative adressing on any given dynamic instance of class ecu
		A2lCreateMeasurement_rel("EcuTask", "byte", byte, offsetOf(byte));
		A2lCreateMeasurement_rel("EcuTask", "word", word, offsetOf(word));
		A2lCreateMeasurement_rel("EcuTask", "dword", dword, offsetOf(dword));
		A2lCreateMeasurement_rel("EcuTask", "taskId", taskId, offsetOf(taskId));

	
}
#endif


// Constructor 
EcuTask::EcuTask( unsigned int id ) {

	taskId = id;

	counter = 0;
	
	byte = 0;
	word = 0;
	dword = 0;
	sword = 0;
}

// Cyclic task
void EcuTask::run() {

	counter++;

	byte++;
	word++;
	dword++;
	sword++;

	XcpEventExt(taskId, (BYTEPTR)this); // Trigger measurement data aquisition event for this task, relative addressing to this
}


