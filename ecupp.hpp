/* ecu.hpp */

#ifndef __ECUPP_H_
#define __ECUPP_H_

class ecu {

public:
	
	unsigned short counter;

	unsigned char byte;
	unsigned short word;
	unsigned long dword;

	ecu();

	void task();

};



extern ecu* gEcu;

#endif
