/* ecu.hpp */

#ifndef __ECUPP_H_
#define __ECUPP_H_


extern "C" {
	extern unsigned int gXcpEvent_EcuTasks;
}

class EcuTask {

public:
	
	unsigned int taskId;

	unsigned short counter;

	unsigned char byte;
	unsigned short word;
	unsigned long dword;
	
	signed short sword;


	EcuTask( unsigned int taskId );

	void run();

#ifdef XCP_ENABLE_A2L
	void CreateA2lClassDescription( int eventCount, const unsigned int eventList[] );
#endif
};





#endif
