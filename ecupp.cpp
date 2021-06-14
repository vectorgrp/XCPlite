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
EcuTask::EcuTask( uint16_t id ) {

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


extern "C" {

	// Demo tasks, cycle times and measurement data acquisition event numbers
	volatile vuint32 gTaskCycleTimerECUpp = 2000; // 2ms  Cycle time of the C++ task
	EcuTask* gEcuTask1 = NULL;
	EcuTask* gEcuTask2 = NULL;
	EcuTask* gActiveEcuTask = NULL;
	volatile unsigned int gActiveEcuTaskId = 1; // Task id of the active C++ task
	
	// Events
	vuint16 gXcpEvent_EcuTask1 = 1;
	vuint16 gXcpEvent_EcuTask2 = 2;
	vuint16 gXcpEvent_ActiveEcuTask = 3;


	void ecuppInit(void) {

		// Create XCP events
        // Events must be all defined before A2lHeader() is called, measurements and parameters have to be defined after all events have been defined !!
        // Character count should be <=8 to keep the A2L short names unique !
#ifdef XCP_ENABLE_DAQ_EVENT_LIST
		gXcpEvent_EcuTask1 = XcpCreateEvent("Task1", 2000, 0, sizeof(EcuTask));                   // Extended event triggered by C++ ecuTask1 instance
		gXcpEvent_EcuTask2 = XcpCreateEvent("Task2", 2000, 0, sizeof(EcuTask));                   // Extended event triggered by C++ ecuTask2 instance
		gXcpEvent_ActiveEcuTask = XcpCreateEvent("TaskAct", 0, 0, sizeof(class EcuTask));      // Extended event triggered by C++ main task for a pointer to an EcuTask instance
#endif

		// C++ demo
    	// Initialize ECU demo runnables (C++)
	    // Instances are associated to events
		gEcuTask1 = new EcuTask(gXcpEvent_EcuTask1);
		gEcuTask2 = new EcuTask(gXcpEvent_EcuTask2);
		gActiveEcuTaskId = gXcpEvent_EcuTask1;
	}

	void ecuppCreateA2lDescription(void) {
		assert(gEcuTask1 != NULL);
		assert(gEcuTask2 != NULL);
		gEcuTask1->createA2lClassDefinition(); // use any instance of a class to create its typedef
		gEcuTask1->createA2lClassInstance("ecuTask1", "ecu task 1");
		gEcuTask2->createA2lClassInstance("ecuTask2", "ecu task 2");
		A2lSetEvent(gXcpEvent_ActiveEcuTask);
		A2lCreateDynamicTypedefInstance("activeEcuTask", "EcuTask", "");
		A2lCreateParameterWithLimits(gActiveEcuTaskId, "Active ecu task object id", "", 1, 2);
	}

	// ECU cyclic (2ms default) demo task 
	// Calls C++ ECU demo code
	void* ecuppTask(void* p) {

		printf("Start C++ demo task (cycle = %uus, ext event = %d, size = %u )\n", gTaskCycleTimerECUpp, gXcpEvent_ActiveEcuTask, (vuint32)sizeof(class EcuTask));
		for (;;) {
			sleepNs(gTaskCycleTimerECUpp * 1000);
			// Run the currently active ecu task
			gActiveEcuTask = gActiveEcuTaskId == gXcpEvent_EcuTask1 ? gEcuTask1 : gActiveEcuTaskId == gXcpEvent_EcuTask2 ? gEcuTask2 : NULL;
			if (gActiveEcuTask != NULL) {
				gActiveEcuTask->run();
				XcpEventExt(gXcpEvent_ActiveEcuTask, (vuint8*)gActiveEcuTask); // Trigger measurement date aquisition event for currently active task
			}
		}
		return 0;
	}

} // C

