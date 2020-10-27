/* A2L.h */


#ifndef __A2L_H_ 
#define __A2L_H_

#include "xcpLite.h"

extern void A2lInit(void);

void A2lCreateEvent(const char* name);

extern void A2lHeader(void);

void A2lSetEvent(unsigned int event);
#define A2lCreateMeasurement(name) A2lCreateMeasurement_(#name,sizeof(name),(unsigned long)&name)
extern void A2lCreateMeasurement_(char* name, int size, unsigned long addr);

extern void A2lClose(void);


#endif
