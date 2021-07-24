/* A2L.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __A2L_H_ 
#define __A2L_H_

#ifdef XCPSIM_ENABLE_A2L_GEN

#ifdef __cplusplus
extern "C" {
#endif

#define A2L_TYPE_UINT8   1
#define A2L_TYPE_UINT16  2
#define A2L_TYPE_UINT32  4
#define A2L_TYPE_UINT64  10
#define A2L_TYPE_INT8    -1
#define A2L_TYPE_INT16   -2
#define A2L_TYPE_INT32   -4
#define A2L_TYPE_INT64   -10
#define A2L_TYPE_DOUBLE  8


// Init A2L generation
extern int A2lInit(const char *filename);

// Start A2L generation
extern void A2lHeader();

// Set fixed event for all following creates
void A2lSetEvent(uint16_t event);


// Create measurements
extern void A2lCreateMeasurement_(const char* instanceName, const char* name, int size, uint32_t addr, double factor, double offset, const char* unit, const char* comment);
extern void A2lCreateMeasurementArray_(const char* instanceName, const char* name, int size, int dim, uint32_t addr);
#define A2lCreateMeasurement(name,comment) A2lCreateMeasurement_(NULL,#name,sizeof(name),ApplXcpGetAddr((vuint8*)&(name)),1.0,0.0,NULL,comment)
#define A2lCreateMeasurement_64(name,comment) A2lCreateMeasurement_(NULL,#name,A2L_TYPE_UINT64,ApplXcpGetAddr((vuint8*)&(name)),1.0,0.0,NULL,comment)
#define A2lCreateMeasurement_s64(name,comment) A2lCreateMeasurement_(NULL,#name,A2L_TYPE_INT64,ApplXcpGetAddr((vuint8*)&(name)),1.0,0.0,NULL,comment)
#define A2lCreateMeasurement_s(name,comment) A2lCreateMeasurement_(NULL,#name,-(int)sizeof(name),ApplXcpGetAddr((vuint8*)&(name)),1.0,0.0,NULL,comment) // signed integer (8/16/32) or double
#define A2lCreatePhysMeasurement(name,comment,factor,offset,unit) A2lCreateMeasurement_(NULL,#name,sizeof(name),ApplXcpGetAddr((vuint8*)&name),factor,offset,unit,comment) // unsigned integer (8/16/32) with linear physical conversion rule
#define A2lCreatePhysMeasurement_s(name,comment,factor,offset,unit) A2lCreateMeasurement_(NULL,#name,-(int)sizeof(name),ApplXcpGetAddr((vuint8*)&name),factor,offset,unit,comment) // signed integer (8/16/32) with linear physical conversion rule
#define A2lCreatePhysMeasurement_64(name,comment,factor,offset,unit) A2lCreateMeasurement_(NULL,#name,A2L_TYPE_UINT64,ApplXcpGetAddr((vuint8*)&name),factor,offset,unit,comment) // unsigned integer (8/16/32) with linear physical conversion rule
#define A2lCreatePhysMeasurement_s64(name,comment,factor,offset,unit) A2lCreateMeasurement_(NULL,#name,A2L_TYPE_INT64,ApplXcpGetAddr((vuint8*)&name),factor,offset,unit,comment) // signed integer (8/16/32) with linear physical conversion rule
#define A2lCreatePhysMeasurementExt(name,var,comment,factor,offset,unit) A2lCreateMeasurement_(NULL,name,sizeof(var),ApplXcpGetAddr((vuint8*)&var),factor,offset,unit,comment) // named unsigned integer (8/16/32) with linear physical conversion rule
#define A2lCreatePhysMeasurementExt_s(name,var,comment,factor,offset,unit) A2lCreateMeasurement_(NULL,name,-(int)sizeof(var),ApplXcpGetAddr((vuint8*)&var),factor,offset,unit,comment) // named signed integer (8/16/32) with linear physical conversion rule
#define A2lCreateMeasurementArray(name) A2lCreateMeasurementArray_(NULL,#name,sizeof(name[0]),sizeof(name)/sizeof(name[0]),ApplXcpGetAddr((vuint8*)&name[0])) // unsigned integer (8/16/32) or double array
#define A2lCreateMeasurementArray_s(name) A2lCreateMeasurementArray_(NULL,#name,-(int)sizeof(name[0]),sizeof(name)/sizeof(name[0]),ApplXcpGetAddr((vuint8*)&name[0])) // signed integer (8/16/32) or double array

// Create typedefs
void A2lTypedefBegin_(const char* name, int size, const char* comment);
void A2lTypedefComponent_(const char* name, int size, uint32_t offset);
void A2lTypedefEnd_();
void A2lCreateTypedefInstance_(const char* instanceName, const char* typeName, uint32_t addr, const char* comment);
#define A2lTypedefBegin(name,comment) A2lTypedefBegin_(#name,(int)sizeof(name),comment)
#define A2lTypedefComponent(name,offset) A2lTypedefComponent_(#name,(int)sizeof(name),offset)
#define A2lTypedefEnd() A2lTypedefEnd_()
#define A2lCreateTypedefInstance(instanceName, typeName, addr, comment) A2lCreateTypedefInstance_(instanceName, typeName, addr, comment)
#define A2lCreateDynamicTypedefInstance(instanceName, typeName, comment) A2lCreateTypedefInstance_(instanceName, typeName, 0, comment)

// Create measurements for c++ class instance variables
#define A2lCreateMeasurement_abs(instanceName,name,variable) A2lCreateMeasurement_(instanceName,name,sizeof(variable),ApplXcpGetAddr((vuint8*)&(name)),0.0,0.0,NULL,"") // specific instances
#define A2lCreateMeasurement_rel(instanceName,name,variable,offset) A2lCreateMeasurement_(instanceName,name,sizeof(variable),offset,0.0,0.0,NULL,"") // dynamic instances with XcpEventExt

// Create parameters
void A2lCreateParameter_(const char* name, int size, uint32_t addr, const char* comment, const char* unit);
void A2lCreateParameterWithLimits_(const char* name, int size, uint32_t addr, const char* comment, const char* unit, double min, double max);
void A2lCreateMap_(const char* name, int size, uint32_t addr, uint32_t xdim, uint32_t ydim, const char* comment, const char* unit);
void A2lCreateCurve_(const char* name, int size, uint32_t addr, uint32_t xdim, const char* comment, const char* unit);
#define A2lCreateParameter(name,comment,unit) A2lCreateParameter_(#name,sizeof(name),ApplXcpGetAddr((vuint8*)&name),unit,comment)
#define A2lCreateParameterWithLimits(name,comment,unit,min,max) A2lCreateParameterWithLimits_(#name,sizeof(name),ApplXcpGetAddr((vuint8*)&name),comment,unit,min,max)
#define A2lCreateCurve(name,xdim,comment,unit) A2lCreateCurve_(#name,sizeof(name[0]),ApplXcpGetAddr((vuint8*)&name),xdim,unit,comment)
#define A2lCreateMap(name,xdim,ydim,comment,unit) A2lCreateMap_(#name,sizeof(name[0][0]),ApplXcpGetAddr((vuint8*)&name),xdim,ydim,unit,comment)

// Create groups
void A2lParameterGroup(const char* name, int count, ...);
void A2lMeasurementGroup(const char* name, int count, ...);
void A2lMeasurementGroupFromList(const char *name, const char* names[], unsigned int count);

// Finish A2L generation
extern void A2lClose();

#ifdef __cplusplus
}
#endif

#endif
#endif
