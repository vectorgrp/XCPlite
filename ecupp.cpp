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
void EcuTask::createA2lClassDescription() {

#define offsetOf(x) ((int)&x - (int)this)

	// Create class typedef
	A2lTypedefBegin(EcuTask, "TYPEDEF for class EcuTask");
	A2lTypedefComponent(taskId, offsetOf(taskId));
	A2lTypedefComponent(counter, offsetOf(counter));
	A2lTypedefComponent(byte, offsetOf(byte));
	A2lTypedefComponent(word, offsetOf(word));
	A2lTypedefComponent(dword, offsetOf(dword));
	A2lTypedefEnd();
}


void EcuTask::createA2lClassInstance(const char* instanceName, const char* comment) {

	A2lCreateTypedefInstance(instanceName, "EcuTask", (unsigned long)this, comment);
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

	XcpEvent(taskId); // Trigger measurement data aquisition event for this task
}


