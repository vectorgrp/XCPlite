/* ecu.hpp */

#ifndef __ECUPP_H_
#define __ECUPP_H_

#include <math.h>

extern "C" {
	extern unsigned int gXcpEvent_EcuTasks;
}

class EcuTask {

public:
	
	unsigned int taskId;


	unsigned short counter;
	double timer;
	double channel1;
	bool squarewave;

	volatile double offset = 0;
	volatile double period = 5;
	volatile double ampl = 50;

	unsigned char byte;
	unsigned short word;
	unsigned long dword;
	signed char sbyte;
	signed short sword;
	signed long sdword;
	float float32;
	double float64;


	EcuTask( unsigned int taskId );

	void run();

#ifdef XCP_ENABLE_A2L
	void createA2lClassDefinition();
	void createA2lStaticClassInstance(const char* instanceName, const char* comment);
#endif
};





#endif
