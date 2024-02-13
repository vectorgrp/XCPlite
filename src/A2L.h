#pragma once
/* A2L.h */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#define OPTION_ENABLE_A2L_SYMBOL_LINKS  ON   // Enable generation of symbol links (required for CANape integrated linker map update)


#define A2L_TYPE_UINT8    1
#define A2L_TYPE_UINT16   2
#define A2L_TYPE_UINT32   4
#define A2L_TYPE_UINT64   8
#define A2L_TYPE_INT8    -1
#define A2L_TYPE_INT16   -2
#define A2L_TYPE_INT32   -4
#define A2L_TYPE_INT64   -8
#define A2L_TYPE_FLOAT   -9
#define A2L_TYPE_DOUBLE  -10

#define A2L_TYPE_BOOL sizeof(BOOL) 

#ifndef A2lGetAddr
    #define A2lGetAddr(p) ApplXcpGetAddr(p)
#endif

#ifdef __cplusplus

#include <typeinfo>

#ifndef A2lGetType
#define A2lGetSign_(var) ((int)(typeid(var).name()[0]=='u'?+1:-1))
#define A2lGetType(var) (A2lGetSign_(var)*(typeid(var).name()[0]=='f'?9:typeid(var).name()[0]=='d'?10:sizeof(var)))
#endif
#ifndef A2lGetOffset
#define A2lGetOffset(var) (uint32_t)((uint8_t*)&(var) - (uint8_t*)this)
#endif

 // Create parameters
#define A2lCreateParameter(name,comment,unit) A2lCreateParameter_(#name, A2lGetType(name), 0, A2lGetAddr((uint8_t*)&name), comment, unit)
#define A2lCreateParameterWithLimits(name,comment,unit,min,max) A2lCreateParameterWithLimits_(#name, A2lGetType(name), 0, A2lGetAddr((uint8_t*)&name), comment, unit, min, max)
#define A2lCreateCurve(name,xdim,comment,unit) A2lCreateCurve_(#name, A2lGetType(name[0]), 0, A2lGetAddr((uint8_t*)&name[0]), xdim, comment, unit)
#define A2lCreateMap(name,xdim,ydim,comment,unit) A2lCreateMap_(#name, A2lGetType(name[0][0]), 0, A2lGetAddr((uint8_t*)&name[0][0]), xdim, ydim, comment, unit)

// Create measurements
#define A2lCreateMeasurement(name,comment) A2lCreateMeasurement_(NULL, #name, A2lGetType(name), 0, A2lGetAddr((uint8_t*)&(name)), 1.0, 0.0, NULL, comment)
#define A2lCreatePhysMeasurement(name,comment,factor,offset,unit) A2lCreateMeasurement_(NULL, #name, A2lGetType(name), 0, A2lGetAddr((uint8_t*)&name), factor, offset, unit, comment) // unsigned integer (8/16/32) with linear physical conversion rule
#define A2lCreateMeasurementArray(name) A2lCreateMeasurementArray_(NULL,#name, A2lGetType(name[0]), sizeof(name)/sizeof(name[0]), 0, A2lGetAddr((uint8_t*)&name[0])) // unsigned integer (8/16/32) or double array

// Create typedefs
#define A2lTypedefComponent(name) A2lTypedefMeasurementComponent_(#name, A2lGetType(name), A2lGetOffset(name))

#else

// Create parameters
#define A2lCreateParameter(name,type,comment,unit) A2lCreateParameter_(#name, type, 0, A2lGetAddr((uint8_t*)&name), comment, unit)
#define A2lCreateParameterWithLimits(name,type,comment,unit,min,max) A2lCreateParameterWithLimits_(#name, type, 0, A2lGetAddr((uint8_t*)&name), comment, unit, min, max)
#define A2lCreateCurve(name,type,xdim,comment,unit) A2lCreateCurve_(#name, type, 0, A2lGetAddr((uint8_t*)&name[0]), xdim, comment, unit)
#define A2lCreateMap(name,type,xdim,ydim,comment,unit) A2lCreateMap_(#name, type, 0, A2lGetAddr((uint8_t*)&name[0][0]), xdim, ydim, comment, unit)

// Create measurements
#define A2lCreateMeasurement(name,type,comment) A2lCreateMeasurement_(NULL,#name,type, 0, A2lGetAddr((uint8_t*)&(name)), 1.0, 0.0, NULL, comment)
#define A2lCreatePhysMeasurement(name,type,comment,factor,offset,unit) A2lCreateMeasurement_(NULL, #name,type, 0, A2lGetAddr((uint8_t*)&name), factor, offset, unit, comment) // unsigned integer (8/16/32) with linear physical conversion rule
#define A2lCreateMeasurementArray(name,type) A2lCreateMeasurementArray_(NULL, #name, type, sizeof(name)/sizeof(name[0]), 0, A2lGetAddr((uint8_t*)&name[0])) // unsigned integer (8/16/32) or double array

// Create typedefs
#define A2lTypedefComponent(name,type,offset) A2lTypedefMeasurementComponent_(#name, type, offset)

#endif

#define A2lTypedefBegin(name,comment) A2lTypedefBegin_(#name,(uint32_t)sizeof(name),comment)
#define A2lTypedefEnd() A2lTypedefEnd_()
#define A2lCreateTypedefInstance(instanceName, typeName, addr, comment) A2lCreateTypedefInstance_(instanceName, typeName, 0, A2lGetAddr((uint8_t*)&instanceName), comment)
#define A2lCreateDynTypedefInstance(instanceName, typeName, comment) A2lCreateTypedefInstance_(instanceName, typeName, 1, 0, comment)

// Init A2L generation
extern BOOL A2lOpen(const char *filename, const char* projectName);

// Create memory segments
extern void A2lCreate_MOD_PAR( uint32_t startAddr, uint32_t size, char* epk);

// Create XCP IF_DATA
extern void A2lCreate_ETH_IF_DATA(BOOL useTCP, const uint8_t* addr, uint16_t port);
extern void A2lCreate_CAN_IF_DATA(BOOL useCANFD, uint16_t croId, uint16_t dtoId, uint32_t bitRate);

// Set fixec or default event for all following creates
extern void A2lSetFixedEvent(uint16_t event);
extern void A2lRstFixedEvent();
extern void A2lSetDefaultEvent(uint16_t event);
extern void A2lRstDefaultEvent();

// Create measurements
extern void A2lCreateMeasurement_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, double factor, double offset, const char* unit, const char* comment);
extern void A2lCreateMeasurementArray_(const char* instanceName, const char* name, int32_t type, int dim, uint8_t ext, uint32_t addr);

// Create typedefs
void A2lTypedefBegin_(const char* name, uint32_t size, const char* comment);
void A2lTypedefMeasurementComponent_(const char* name, int32_t type, uint32_t offset);
void A2lTypedefParameterComponent_(const char* name, int32_t type, uint32_t offset);
void A2lTypedefEnd_();
void A2lCreateTypedefInstance_(const char* instanceName, const char* typeName, uint8_t ext, uint32_t addr, const char* comment);

// Create parameters
void A2lCreateParameter_(const char* name, int32_t type, uint8_t ext, uint32_t addr, const char* comment, const char* unit);
void A2lCreateParameterWithLimits_(const char* name, int32_t type, uint8_t ext, uint32_t addr, const char* comment, const char* unit, double min, double max);
void A2lCreateMap_(const char* name, int32_t type, uint8_t ext, uint32_t addr, uint32_t xdim, uint32_t ydim, const char* comment, const char* unit);
void A2lCreateCurve_(const char* name, int32_t type, uint8_t ext, uint32_t addr, uint32_t xdim, const char* comment, const char* unit);

// Create groups
void A2lParameterGroup(const char* name, int count, ...);
void A2lParameterGroupFromList(const char* name, const char* pNames[], size_t count);
void A2lMeasurementGroup(const char* name, int count, ...);
void A2lMeasurementGroupFromList(const char *name, char* names[], uint32_t count);

// Finish A2L generation
extern void A2lClose();


// For A2L.CPP
extern const char* A2lGetSymbolName(const char* instanceName, const char* name);
extern uint16_t A2lGetFixedEvent();
