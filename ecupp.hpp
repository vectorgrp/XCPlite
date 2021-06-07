/* ecupp.hpp */

#ifndef __ECUPP_HPP_
#define __ECUPP_HPP_

#ifdef __cplusplus

class EcuTask {

public:
	
	unsigned short taskId;

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


	EcuTask( unsigned short taskId );

	void run();

#ifdef XCPSIM_ENABLE_A2L_GEN
	void createA2lClassDefinition();
	void createA2lClassInstance(const char* instanceName, const char* comment);
#endif
};


#endif


#endif
