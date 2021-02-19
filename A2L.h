/* A2L.h */


#ifndef __A2L_H_ 
#define __A2L_H_

#include "xcpLite.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start A2L generation
extern int A2lInit(const char *filename);

// Create a measurement event (must be called before A2lHeader() 
unsigned int A2lCreateEvent(const char* name);

extern void A2lHeader(void);

// Set fixed event for all following creates
void A2lSetEvent(unsigned int event);

// Create measurements
extern void A2lCreateMeasurement_(const char* instanceName, const char* name, int size, unsigned long addr, double factor, double offset, const char* unit, const char* comment);
extern void A2lCreateMeasurementArray_(const char* instanceName, const char* name, int size, int dim, unsigned long addr);

#define A2lCreateMeasurement(name) A2lCreateMeasurement_(NULL,#name,sizeof(name),ApplXcpGetAddr((BYTEPTR)&(name)),0.0,0.0,NULL,"")
#define A2lCreateMeasurement_s(name) A2lCreateMeasurement_(NULL,#name,-(int)sizeof(name),ApplXcpGetAddr((BYTEPTR)&(name)),0.0,0.0,NULL,"") // signed integer (8/16/32) or double
#define A2lCreatePhysMeasurement(name,factor,offset,unit,comment) A2lCreateMeasurement_(NULL,#name,sizeof(name),ApplXcpGetAddr((BYTEPTR)&name),factor,offset,unit,comment) // unsigned integer (8/16/32) with linear physical conversion rule
#define A2lCreatePhysMeasurement_s(name,factor,offset,unit,comment) A2lCreateMeasurement_(NULL,#name,-(int)sizeof(name),ApplXcpGetAddr((BYTEPTR)&name),factor,offset,unit,comment) // signed integer (8/16/32) with linear physical conversion rule
#define A2lCreateMeasurementArray(name) A2lCreateMeasurementArray_(NULL,#name,sizeof(name[0]),sizeof(name)/sizeof(name[0]),ApplXcpGetAddr((BYTEPTR)&name[0])) // unsigned integer (8/16/32) or double array
#define A2lCreateMeasurementArray_s(name) A2lCreateMeasurementArray_(NULL,#name,-(int)sizeof(name[0]),sizeof(name)/sizeof(name[0]),ApplXcpGetAddr((BYTEPTR)&name[0])) // signed integer (8/16/32) or double array

// Create typedefs
#define A2lCreateMeasurementType(name,comment) A2lCreateMeasurementType_(name,(int)sizeof(name),comment)
void A2lCreateMeasurementType_(const char* name, int size, const char* comment);

// Create measurements for c++ class instance variables
#define A2lCreateMeasurement_abs(instanceName,name,variable) A2lCreateMeasurement_(instanceName,name,sizeof(variable),ApplXcpGetAddr((BYTEPTR)&(name)),0.0,0.0,NULL,"") // specific instances
#define A2lCreateMeasurement_rel(instanceName,name,variable,offset) A2lCreateMeasurement_(instanceName,name,sizeof(variable),offset,0.0,0.0,NULL,"") // dynamic instances with XcpEventExt


// Create parameters
void A2lCreateParameter_(const char* name, int size, unsigned long addr, const char* comment, const char* unit);
void A2lCreateMap_(const char* name, int size, unsigned long addr, unsigned int xdim, unsigned int ydim, const char* comment, const char* unit);
#define A2lCreateParameter(name,comment,unit) A2lCreateParameter_(#name,sizeof(name),ApplXcpGetAddr((BYTEPTR)&name),unit,comment)
#define A2lCreateCurve(name,xdim,comment,unit) A2lCreateMap_(#name,sizeof(name[0]),ApplXcpGetAddr((BYTEPTR)&name),xdim,1,unit,comment)
#define A2lCreateMap(name,xdim,ydim,comment,unit) A2lCreateMap_(#name,sizeof(name[0][0]),ApplXcpGetAddr((BYTEPTR)&name),xdim,ydim,unit,comment)

// Create groups
void A2lCreateGroup(const char* name, int count, ...);

// Finish A2L generation
extern void A2lClose(void);

#ifdef __cplusplus
}
#endif

#endif
