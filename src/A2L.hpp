#pragma once
/* A2L.h - A2L Generator */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

// Types
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

#define A2L_TYPE_REAL(size) ((size==4)?A2L_TYPE_FLOAT:A2L_TYPE_DOUBLE)
#define A2L_TYPE_UINT(size) (size)
#define A2L_TYPE_INT(size) -(size)


#ifndef __cplusplus
#error
#endif

#ifndef A2lGetAddr
#define A2lGetAddr(p) Xcp::getInstance()->getA2lAddr(p)
#endif
#ifndef A2lGetType
#define A2lGetSign_(var) ((int)(typeid(var).name()[0]=='u'?+1:-1))
#define A2lGetType(var) (A2lGetSign_(var)*(typeid(var).name()[0]=='f'?9:typeid(var).name()[0]=='d'?10:sizeof(var)))
#endif
#ifndef A2lGetOffset
#define A2lGetOffset(var) (unsigned int)((uint8_t*)&(var) - (uint8_t*)this)
#endif


// Create measurements
#define createMeasurement(name,comment)                                             createMeasurement_(NULL,#name,A2lGetType(name),0,A2lGetAddr((uint8_t*)&name),1.0,0.0,NULL,comment)
#define createDynMeasurement(instanceName,name,comment)                             createMeasurement_(instanceName,#name,A2lGetType(name),1,A2lGetOffset(name),1.0,0.0,NULL,comment)
#define createPhysMeasurement(name,comment,factor,offset,unit)                      createMeasurement_(NULL,#name,A2lGetType(name),0,A2lGetAddr((uint8_t*)&name),factor,offset,unit,comment) // unsigned integer (8/16/32) with linear physical conversion rule
#define createDynPhysMeasurement(instanceName,name,var,comment,factor,offset,unit)  createMeasurement_(instanceName,name,A2lGetType(var),1,A2lGetOffset(name),factor,offset,unit,comment) // named unsigned integer (8/16/32) with linear physical conversion rule
#define createMeasurementArray(name)                                                createMeasurementArray_(NULL,#name,A2lGetType(name[0]),sizeof(name)/sizeof(name[0]),0,A2lGetAddr((uint8_t*)&name[0])) // unsigned integer (8/16/32) or double array

// Create typedefs
#define createTypedefBegin(name,comment)                                            createTypedefBegin_(#name,(int)sizeof(name),comment)
#define createTypedefParameterComponent(name)                                       createTypedefParameterComponent_(#name,A2lGetType(name),A2lGetOffset(name))
#define createTypedefMeasurementComponent(name)                                     createTypedefMeasurementComponent_(#name,A2lGetType(name),A2lGetOffset(name))
#define createTypedefEnd()                                                          createTypedefEnd_()
#define createTypedefInstance(instanceName,typeName,comment)                        createTypedefInstance_(instanceName, typeName, 0, A2lGetAddr((uint8_t*)&(instanceName)), comment)
#define createDynTypedefInstance(instanceName,typeName,comment)                     createTypedefInstance_(instanceName, typeName, 1, 0, comment)

// Create parameters
#define createParameter(name,comment,unit)                                          createParameter_(NULL,#name,A2lGetType(name),0,A2lGetAddr((uint8_t*)&name),comment,unit)
#define createDynParameter(instanceName,name,comment,unit)                          createParameter_(instanceName,#name,A2lGetType(name),1,A2lGetOffset(name),comment,unit)
#define createParameterWithLimits(name,comment,unit,min,max)                        createParameterWithLimits_(NULL,#name, A2lGetType(name), 0, A2lGetAddr((uint8_t*)&name), comment, unit, min, max)
#define createDynParameterWithLimits(instanceName,name,comment,unit,min,max)        createParameterWithLimits_(instanceName,#name, A2lGetType(name), 1, A2lGetOffset(name), comment, unit, min, max)
#define createCurve(name,xdim,comment,unit)                                         createCurve_(NULL,#name, A2lGetType(name[0]), 0, A2lGetAddr((uint8_t*)&name[0]), xdim, comment, unit)
#define createDynCurve(instanceName,name,xdim,comment,unit)                         createCurve_(instanceName,#name, A2lGetType(name[0]), 0, A2lGetOffset(name[0]), xdim, comment, unit)
#define createMap(name,xdim,ydim,comment,unit)                                      createMap_(NULL,#name, A2lGetType(name[0][0]), 0, A2lGetAddr((uint8_t*)&name[0][0]), xdim, ydim, comment, unit)
#define createDynMap(instanceName,name,xdim,ydim,comment,unit)                      createMap_(instanceName,#name, A2lGetType(name[0][0]), 1, A2lGetOffet(name[0][0]), xdim, ydim, comment, unit)


 
class A2L {

private:

	 FILE* file;
	 uint32_t event;
	 	
     void printName(const char*type, const char* instanceName, const char* name);
     uint32_t encodeDynAddr(uint8_t ext, uint32_t addr);


public:

    const char* filename;

    unsigned int cntMeasurements;
    unsigned int cntParameters;
    unsigned int cntTypedefs;
    unsigned int cntComponents;
    unsigned int cntInstances;
    unsigned int cntConversions;

	A2L(const char* filename);
	~A2L();
 
    // Start A2L generation
    BOOL open(const char* projectName);
    
    // Create memory segments
#ifdef OPTION_ENABLE_CAL_SEGMENT
    void create_MOD_PAR(uint32_t startAddr, uint32_t size);
#endif

    // Create IF_DATA for XCP
    // All XCP events must have been be created before
    void create_XCP_IF_DATA(BOOL tcp, const uint8_t* addr, uint16_t port);

    // Set/reset fixed XCP event for all following creates
    void setEvent(uint16_t xcp_event);
    void rstEvent();

    // Create groups
    void createParameterGroup(const char* name, int count, ...);
    void createMeasurementGroup(const char* name, int count, ...);
    
    // Create measurements
    void createMeasurement_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, double factor, double offset, const char* unit, const char* comment);
    void createMeasurementArray_(const char* instanceName, const char* name, int32_t type, int32_t dim, uint8_t ext, uint32_t addr);
    
    // Create typedefs
    void createTypedefBegin_(const char* name, int32_t size, const char* comment);
    void createTypedefParameterComponent_(const char* name, int32_t type, uint32_t offset);
    void createTypedefMeasurementComponent_(const char* name, int32_t type, uint32_t offset);
    void createTypedefEnd_();
    void createTypedefInstance_(const char* instanceName, const char* typeName, uint8_t ext, uint32_t addr, const char* comment);

    // Create parameters
    void createParameter_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, const char* comment, const char* unit);
    void createParameterWithLimits_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, const char* comment, const char* unit, double min, double max);
    void createMap_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, uint32_t xdim, uint32_t ydim, const char* comment, const char* unit);
    void createCurve_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, uint32_t xdim, const char* comment, const char* unit);

    // Finalize and close A2L file
    void close();
};


