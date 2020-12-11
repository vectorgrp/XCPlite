/* ecu.hpp */

#ifndef __ECUPP_H_
#define __ECUPP_H_

class ecu {

public:
	
	unsigned short ecuppCounter;

	unsigned char byte;
	unsigned short word;
	unsigned long dword;
	
	signed short sword;


	ecu();

	void task();

};



extern ecu* gEcu;

#endif
