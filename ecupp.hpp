#pragma once

/* ecupp.hpp */
/*
| Code released into public domain, no attribution required
*/

#ifdef __cplusplus

extern "C" {
	void* ecuppTask(void* p);
}

class EcuTask {

public:

	uint16_t taskId;

	double offset = 0;
	double ampl = 50;

	uint16_t counter;
	double time;
	double channel1;
	uint8_t  byte;
	uint16_t word;
	uint32_t dword;
	int8_t  sbyte;
	int16_t sword;
	int32_t sdword;
	double float64;

	EcuTask( uint16_t taskId );

	void run();

#ifdef APP_ENABLE_A2L_GEN
	void createA2lClassDefinition();
	void createA2lClassInstance(const char* instanceName, const char* comment);
#endif
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

void ecuppInit();
void ecuppCreateA2lDescription();
void* ecuppTask(void* p);

#ifdef __cplusplus
}
#endif
