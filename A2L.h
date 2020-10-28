/* A2L.h */


#ifndef __A2L_H_ 
#define __A2L_H_

#include "xcpLite.h"

// Start A2L generation
extern int A2lInit(const char *filename);

// Create a measurement event (must be called before A2lHeader() 
void A2lCreateEvent(const char* name);

extern void A2lHeader(void);

// Set fixed event for all following creates
void A2lSetEvent(unsigned int event);

// Create measurements and parameters
#define A2lCreateMeasurement_s(name) A2lCreateMeasurement_(#name,-(int)sizeof(name),(unsigned long)&name,0.0,0.0,NULL,"")
#define A2lCreateMeasurement(name) A2lCreateMeasurement_(#name,sizeof(name),(unsigned long)&name,0.0,0.0,NULL,"")
#define A2lCreatePhysMeasurement(name,factor,offset,unit,comment) A2lCreateMeasurement_(#name,sizeof(name),(unsigned long)&name,factor,offset,unit,comment)
extern void A2lCreateMeasurement_(const char* name, int size, unsigned long addr, double factor, double offset, const char* unit, const char* comment);
#define A2lCreateMeasurementArray(name) A2lCreateMeasurementArray_(#name,sizeof(name[0]),sizeof(name)/sizeof(name[0]),(unsigned long)&name[0])
extern void A2lCreateMeasurementArray_(const char* name, int size, int dim, unsigned long addr);
#define A2lCreateParameter(name,comment,unit) A2lCreateParameter_(#name,sizeof(name),(unsigned long)&name,unit,comment)
void A2lCreateParameter_(const char* name, int size, unsigned long addr, const char* comment, const char* unit);
#define A2lCreateCurve(name,xdim,comment,unit) A2lCreateMap_(#name,sizeof(name[0]),(unsigned long)&name,xdim,1,unit,comment)
#define A2lCreateMap(name,xdim,ydim,comment,unit) A2lCreateMap_(#name,sizeof(name[0][0]),(unsigned long)&name,xdim,ydim,unit,comment)
void A2lCreateMap_(const char* name, int size, unsigned long addr, unsigned int xdim, unsigned int ydim, const char* comment, const char* unit);

// Create groups
void A2lCreateGroup(const char* name, int count, ...);

// Finish A2L generation
extern void A2lClose(void);


#endif
