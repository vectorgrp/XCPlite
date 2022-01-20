/*----------------------------------------------------------------------------
| File:
|   ecupp.cpp
|
| Description:
|   Test Measurement and Calibration variables for XCP demo
|   C++ language, Classes and Structures, dynamic memory
 ----------------------------------------------------------------------------*/
 /*
 | Code released into public domain, no attribution required
 */

#include "main.h"
#include "platform.h"
#include "clock.h"
#include "xcpLite.h"
#ifdef APP_ENABLE_A2L_GEN
#include "A2L.h"
#endif
#include "ecu.h"
#include "ecupp.hpp"

#ifdef APP_ENABLE_A2L_GEN

// Create A2L description of this class
void EcuTask::createA2lClassDefinition() {

#define offsetOf(x) (unsigned int)((uint8_t*)&x - (uint8_t*)this)

	// Create class typedef
	A2lTypedefBegin(EcuTask, "TYPEDEF for class EcuTask");
	A2lTypedefComponent(taskId, offsetOf(taskId));
	A2lTypedefComponent(counter, offsetOf(counter));
	A2lTypedefComponent(channel1, offsetOf(channel1));
	A2lTypedefComponent(time, offsetOf(time));
	A2lTypedefComponent(byte, offsetOf(byte));
	A2lTypedefComponent(word, offsetOf(word));
	A2lTypedefComponent(dword, offsetOf(dword));
	A2lTypedefComponent_s(sbyte, offsetOf(sbyte));
	A2lTypedefComponent_s(sword, offsetOf(sword));
	A2lTypedefComponent_s(sdword, offsetOf(sdword));
	A2lTypedefComponent(float64, offsetOf(float64));
	A2lTypedefEnd();
}

// Create a dynamic instance (base addr = 0, event ext) of the class
void EcuTask::createA2lClassInstance(const char* instanceName, const char* comment) {
	A2lSetEvent(taskId);
	A2lCreateTypedefInstance(instanceName, "EcuTask", 0, comment);
}

#endif


// Constructor
EcuTask::EcuTask( uint16_t id ) {

	taskId = id;

	counter = 0;
	time = 0;
	channel1 = 0.0;
	offset = 0;
	ampl = 50.0;

	byte = 0;
	word = 0;
	dword = 0;
	sbyte = 0;
	sword = 0;
	sdword = 0;
	float64 = 0.0;
}

// Cyclic task
void EcuTask::run() {

	counter++;

	// Sine wave or square wave depending on instance
	switch (taskId) {
	case 2:
		offset = ecuPar->offset2; ampl = ecuPar->ampl2; break;
	default:
	case 1:
		offset = ecuPar->offset1; ampl = ecuPar->ampl1; break;
	}
    channel1 = offset + (ampl * sin(6.283185307 * time / ecuPar->period));
	time += 0.002;

	byte++;
	sbyte++;
	word++;
	sword++;
	dword++;
	sdword++;
	float64 += 0.1;

	XcpEventExt(taskId, (uint8_t*)this); // Trigger measurement data aquisition event for this task
}


extern "C" {

	// Demo tasks, cycle times and measurement data acquisition event numbers
	volatile uint32_t gTaskCycleTimerECUpp = 2000; // 4ms  Cycle time of the C++ task
	EcuTask* gEcuTask1 = NULL;
	EcuTask* gEcuTask2 = NULL;
	EcuTask* gActiveEcuTask = NULL;
	volatile unsigned int gActiveEcuTaskId = 1; // Task id of the active C++ task

	// Events
	uint16_t gXcpEvent_EcuTask1 = 1; // EVNO
	uint16_t gXcpEvent_EcuTask2 = 2; // EVNO
	uint16_t gXcpEvent_ActiveEcuTask = 3; // EVNO


	void ecuppInit() {

		// Create XCP events
        // Events must be all defined before A2lHeader() is called, measurements and parameters have to be defined after all events have been defined !!
        // Character count should be <=8 to keep the A2L short names unique !
#ifdef XCP_ENABLE_DAQ_EVENT_LIST
		gXcpEvent_EcuTask1 = XcpCreateEvent("ecuTask1", 2000, 0, 0, sizeof(EcuTask));              // Extended event triggered by C++ ecuTask1 instance
		gXcpEvent_EcuTask2 = XcpCreateEvent("ecuTask2", 2000, 0, 0, sizeof(EcuTask));              // Extended event triggered by C++ ecuTask2 instance
		gXcpEvent_ActiveEcuTask = XcpCreateEvent("ecuTaskA", 0, 0, 0, sizeof(class EcuTask));      // Extended event triggered by C++ main task for a pointer to an EcuTask instance
#endif

		// C++ demo
    	// Initialize ECU demo runnables (C++)
	    // Instances are associated to events
		gEcuTask1 = new EcuTask(gXcpEvent_EcuTask1);
		gEcuTask2 = new EcuTask(gXcpEvent_EcuTask2);
		gActiveEcuTaskId = gXcpEvent_EcuTask1;
	}

#ifdef APP_ENABLE_A2L_GEN
	void ecuppCreateA2lDescription() {
		assert(gEcuTask1 != NULL);
		assert(gEcuTask2 != NULL);
		gEcuTask1->createA2lClassDefinition(); // use any instance of a class to create its typedef
		gEcuTask1->createA2lClassInstance("ecuTask1", "ecupp task number 1");
		gEcuTask2->createA2lClassInstance("ecuTask2", "ecu task number 2");
		A2lSetEvent(gXcpEvent_ActiveEcuTask);
		A2lCreateDynamicTypedefInstance("activeEcuTask" /* instanceName */, "EcuTask" /* typeName*/, "pointer to active ecu task");
		A2lCreateParameterWithLimits(gActiveEcuTaskId, "select active ecu task (object id)", "", 1, 2);
	}
#endif

	// ECU cyclic (2ms default) demo task
	// Calls C++ ECU demo code
	void* ecuppTask(void* p) {

		(void)p;
		printf("Start C++ demo task (cycle = %uus, XCP event = %d (ext), size = %u )\n", gTaskCycleTimerECUpp, gXcpEvent_ActiveEcuTask, (uint32_t)sizeof(class EcuTask));
		for (;;) {
			sleepNs(gTaskCycleTimerECUpp * 1000);
			// Run the currently active ecu task
			gActiveEcuTask = gActiveEcuTaskId == gXcpEvent_EcuTask1 ? gEcuTask1 : gActiveEcuTaskId == gXcpEvent_EcuTask2 ? gEcuTask2 : NULL;
			if (gActiveEcuTask != NULL) {
				gActiveEcuTask->run();
				XcpEventExt(gXcpEvent_ActiveEcuTask, (uint8_t*)gActiveEcuTask); // Trigger measurement date aquisition event for currently active task
			}
		}
	}

} // C
