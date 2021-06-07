/*----------------------------------------------------------------------------
| File:
|   ecupp.cpp
|
| Description:
|   Test Measurement and Calibration variables for XCP demo
|   C++ language, Classes and Structures, dynamic memory
 ----------------------------------------------------------------------------*/


#include "main.h"
#include "ecupp.hpp"


#ifdef XCPSIM_ENABLE_A2L_GEN

// Create A2L description of this class 
void EcuTask::createA2lClassDefinition() {

#define offsetOf(x) (unsigned int)((vuint8*)&x - (vuint8*)this)

	// Create class typedef
	A2lTypedefBegin(EcuTask, "TYPEDEF for class EcuTask");
	A2lTypedefComponent(taskId, offsetOf(taskId));
	A2lTypedefComponent(counter, offsetOf(counter));
	A2lTypedefComponent(channel1, offsetOf(channel1));
	A2lTypedefComponent(byte, offsetOf(byte));
	A2lTypedefComponent(word, offsetOf(word));
	A2lTypedefComponent(dword, offsetOf(dword));
	A2lTypedefEnd();
}

// Create a dynamic instance (base addr = 0, event ext) of the class
void EcuTask::createA2lClassInstance(const char* instanceName, const char* comment) {
	A2lSetEvent(taskId);
	A2lCreateTypedefInstance(instanceName, "EcuTask", 0, comment);
}
#endif


// Constructor 
EcuTask::EcuTask( unsigned short id ) {

	taskId = id;

	counter = 0;
	timer = 0;
	squarewave = (id==2);
	channel1 = 0.0;
	offset = 0;
	period = 5.0;
	ampl = 50.0;

	byte = 0;
	word = 0;
	dword = 0;
	sbyte = 0;
	sword = 0;
	sdword = 0;
	float32 = 0.0;
	float64 = 0.0;
}

// Cyclic task
void EcuTask::run() {

	counter++;

	// Sine wave or square wave depending on instance
	if (period > 0.01 || period < -0.01) {
		channel1 = sin(6.283185307 * timer / period);
		if (squarewave) {
			channel1 = (channel1 >= 0.0);
		}
		channel1 = offset + (ampl * channel1);
	}
	timer = (timer + 0.001);

	byte++;
	sbyte++;
	word++;
	sword++;
	dword++;
	sdword++;
	float32 += 0.1f;
	float64 += 0.1;

	XcpEventExt(taskId, (vuint8*)this); // Trigger measurement data aquisition event for this task
}


