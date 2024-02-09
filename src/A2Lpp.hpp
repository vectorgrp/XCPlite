#pragma once
/* A2L.h - A2L Generator */

/* Copyright(c) Vector Informatik GmbH.All rights reserved.
   Licensed under the MIT license.See LICENSE file in the project root for details. */

#ifndef __cplusplus
#error
#endif

#include "A2L.h"


#define A2lGetSign_(var) ((int)(typeid(var).name()[0]=='u'?+1:-1))
#define A2lGetType_(var) (A2lGetSign_(var)*(typeid(var).name()[0]=='f'?9:typeid(var).name()[0]=='d'?10:sizeof(var)))
#define A2lGetAddr_(var) Xcp::getInstance()->getA2lAddr((uint8_t*)&var)
#define A2lGetOffset_(struct_name,full_name) (uint16_t)((uint8_t*)&(full_name) - (uint8_t*)&struct_name) 


//---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// Create typedefs
#define createTypedefBegin(name,comment)                                            createTypedefBegin_(#name,(int)sizeof(name),comment)
#define createTypedefParameterComponent(struct_name,var_name)                       createTypedefParameterComponent_(#var_name,A2lGetType_(struct_name.var_name),A2lGetOffset_(struct_name,struct_name.var_name))
#define createTypedefMeasurementComponent(struct_name,var_name)                     createTypedefMeasurementComponent_(#var_name,A2lGetType_(struct_name.var_name),A2lGetOffset_(struct_name,struct_name.var_name))
#define createTypedefEnd()                                                          createTypedefEnd_()

// Create measurement or parameter from typedef
#define createTypedefInstance(instanceName,typeName,comment)                        createTypedefInstance_(#instanceName, #typeName, 0, A2lGetAddr_(instanceName), comment)

// Create measurements
#define createMeasurement(name,comment)                                             createMeasurement_(NULL,#name,A2lGetType_(name),0,A2lGetAddr_(name),1.0,0.0,NULL,comment)
#define createPhysMeasurement(name,comment,factor,offset,unit)                      createMeasurement_(NULL,#name,A2lGetType_(name),0,A2lGetAddr_(name),factor,offset,unit,comment) // unsigned integer (8/16/32) with linear physical conversion rule
#define createMeasurementArray(name)                                                createMeasurementArray_(NULL,#name,A2lGetType_(name[0]),sizeof(name)/sizeof(name[0]),0,A2lGetAddr_(name[0])) // unsigned integer (8/16/32) or double array

// Create parameters
#define createParameter(name,comment,unit)                                          createParameter_(NULL,#name,A2lGetType_(name),0,A2lGetAddr_(name),comment,unit)
#define createParameterWithLimits(name,comment,unit,min,max)                        createParameterWithLimits_(NULL,#name, A2lGetType_(name), 0, A2lGetAddr_(name), comment, unit, min, max)
#define createCurve(name,xdim,comment,unit)                                         createCurve_(NULL,#name, A2lGetType_(name[0]), 0, A2lGetAddr_(name[0]), xdim, comment, unit)
#define createMap(name,xdim,ydim,comment,unit)                                      createMap_(NULL,#name, A2lGetType_(name[0][0]), 0, A2lGetAddr_(name[0][0]), xdim, ydim, comment, unit)


//---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// The functions below are assumed to be called from methods of classes derived from XcpObject !

#define A2lGetDynAddr_(var) xcpGetA2lDynAddr(A2lGetDynOffset_(var)) 
#define A2lGetDynBaseAddr_() xcpGetA2lDynAddr(0) 
#define A2lGetDynOffset_(var) (uint16_t)((uint8_t*)&(var) - (uint8_t*)this) // object data size limited to 64K !

// Create measurement or parameter from typedef
#define createDynTypedefInstance(instanceName,typeName,comment)                     createTypedefInstance_(instanceName, typeName, 1, A2lGetDynBaseAddr_(), comment)
#define createDynTypedefParameterComponent(name)                                    createTypedefParameterComponent_(#name,A2lGetType_(name),A2lGetDynOffset_(name))
#define createDynTypedefMeasurementComponent(name)                                  createTypedefMeasurementComponent_(#name,A2lGetType_(name),A2lGetDynOffset_(name))

// Create measurements
#define createDynMeasurement(instanceName,name,comment)                             createMeasurement_(instanceName,#name,A2lGetType_(name),1,A2lGetDynAddr_(name),1.0,0.0,NULL,comment)
#define createDynPhysMeasurement(instanceName,name,var,comment,factor,offset,unit)  createMeasurement_(instanceName,name,A2lGetType_(var),1,A2lGetDynAddr_(name),factor,offset,unit,comment) // named unsigned integer (8/16/32) with linear physical conversion rule
#define createDynMeasurementArray(name)                                             createMeasurementArray_(NULL,#name,A2lGetType_(name[0]),sizeof(name)/sizeof(name[0]),0,A2lGetDynAddr_(name[0])) // unsigned integer (8/16/32) or double array

// Create parameters
#define createDynParameter(instanceName,name,comment,unit)                          createParameter_(instanceName,#name,A2lGetType_(name),1,A2lGetDynAddr_(name),comment,unit)
#define createDynParameterWithLimits(instanceName,name,comment,unit,min,max)        createParameterWithLimits_(instanceName,#name, A2lGetType_(name), 1, A2lGetDynAddr_(name), comment, unit, min, max)
#define createDynCurve(instanceName,name,xdim,comment,unit)                         createCurve_(instanceName,#name, A2lGetType_(name[0]), 0, A2lGetDynAddr_(name[0]), xdim, comment, unit)
#define createDynMap(instanceName,name,xdim,ydim,comment,unit)                      createMap_(instanceName,#name, A2lGetType_(name[0][0]), 1, A2lGetDynAddr_(name[0][0]), xdim, ydim, comment, unit)




//---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

 
class A2L {

private:

  const char* filename;

public:


	A2L(const char* filename);
	~A2L();
 
    // Start A2L generation
    BOOL open(const char* projectName);
    
    // Create memory segments
    void create_MOD_PAR(uint32_t startAddr, uint32_t size);

    // Create IF_DATA for XCP
    // All XCP events must have been be created before
    void create_XCP_IF_DATA(BOOL tcp, const uint8_t* addr, uint16_t port);

    // Set XCP events for all following creates
    void setFixedEvent(uint16_t xcp_event);
    void setDefaultEvent(uint16_t xcp_event);
    void rstFixedEvent();
    uint16_t getFixedEvent();

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


