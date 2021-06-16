/* ecupp.hpp */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __ECUPP_HPP_
#define __ECUPP_HPP_

#ifdef __cplusplus

extern "C" {
	void* ecuppTask(void* p);
}

class EcuTask {

public:
	
	uint16_t taskId;

	uint16_t counter;
	double timer;
	double channel1;
	bool squarewave;

	volatile double offset = 0;
	volatile double period = 5;
	volatile double ampl = 50;

	uint8_t  byte;
	uint16_t word;
	uint32_t dword;
	int8_t  sbyte;
	int16_t sword;
	int32_t sdword;
	float float32;
	double float64;


	EcuTask( uint16_t taskId );

	void run();

#ifdef XCPSIM_ENABLE_A2L_GEN
	void createA2lClassDefinition();
	void createA2lClassInstance(const char* instanceName, const char* comment);
#endif
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

void ecuppInit(void);
void ecuppCreateA2lDescription(void);
void* ecuppTask(void* p);

#ifdef __cplusplus
}
#endif


#endif

