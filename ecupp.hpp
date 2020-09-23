// ecu.hpp
// V1.0 23.9.2020


class ecu {


public:
	
	unsigned int counter;
	unsigned int* pCounter;
	unsigned char byte;
	unsigned short word;
	unsigned long dword;


	ecu();

	void task();

};



extern ecu* gEcu;

