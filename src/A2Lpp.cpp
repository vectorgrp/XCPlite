/*----------------------------------------------------------------------------
| File:
|   A2L.cpp
|
| Description:
|   Create A2L file 
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "main.h"
#include "platform.h"
#include "xcp.hpp"
#include "A2L.h"
#include "A2Lpp.hpp"

 //---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 
 A2L::A2L(const char * name) {
	 
	filename = name;
 }

 A2L::~A2L() {

	 close();
 }

void A2L::close() {

  A2lClose();
 }

BOOL A2L::open(const char *projectName) {
   
  return A2lOpen(filename, projectName);
}

// Create memory segments
void A2L::create_MOD_PAR(uint32_t startAddr, uint32_t size) {
	A2lCreate_MOD_PAR(startAddr, size, NULL);
}

// Create XCP IF_DATA
void A2L::create_XCP_IF_DATA(BOOL tcp, const uint8_t* addr, uint16_t port) {

	A2lCreate_ETH_IF_DATA(tcp, addr, port);
}


//---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void A2L::setDefaultEvent(uint16_t e) {
	A2lSetDefaultEvent(e);
}

void A2L::setFixedEvent(uint16_t e) {
	A2lSetFixedEvent(e);
}

void A2L::rstFixedEvent() {
  A2lRstFixedEvent();
}

uint16_t A2L::getFixedEvent() {
  return A2lGetFixedEvent();
}

 //---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

 void A2L::createTypedefBegin_(const char* name, int32_t size, const char* comment) {
	 A2lTypedefBegin_(name,size,comment);
}

 void A2L::createTypedefMeasurementComponent_(const char* name, int32_t type, uint32_t offset) {
	 A2lTypedefMeasurementComponent_(name,type,offset);
 }

 void A2L::createTypedefParameterComponent_(const char* name, int32_t type, uint32_t offset) {
	 A2lTypedefParameterComponent_(name, type, offset);
 }

 void A2L::createTypedefEnd_() {
	 A2lTypedefEnd_();
}

 
 void A2L::createTypedefInstance_(const char* instanceName, const char* typeName, uint8_t ext, uint32_t addr, const char* comment) {
   A2lCreateTypedefInstance_(instanceName, typeName, ext, addr, comment);
}

 //---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

 void A2L::createMeasurement_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, double factor, double offset, const char* unit, const char* comment) {
	 A2lCreateMeasurement_(instanceName, name, type, ext, addr, factor, offset, unit, comment);
}
 
 void A2L::createMeasurementArray_(const char* instanceName, const char* name, int32_t type, int dim, uint8_t ext, uint32_t addr) {
   A2lCreateMeasurementArray_(instanceName, name, type, dim, ext, addr);
}

 //---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
   
 void A2L::createParameterWithLimits_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, const char* comment, const char* unit, double min, double max) {
   A2lCreateParameterWithLimits_(A2lGetSymbolName(instanceName, name), type, ext, addr, comment, unit, min, max);
}

 
 void A2L::createParameter_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, const char* comment, const char* unit) {
	 A2lCreateParameter_(A2lGetSymbolName(instanceName, name), type, ext, addr, comment, unit);
}

 void A2L::createMap_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, uint32_t xdim, uint32_t ydim, const char* comment, const char* unit) {
   A2lCreateMap_(A2lGetSymbolName(instanceName, name), type, ext, addr, xdim, ydim, comment, unit);
}

 void A2L::createCurve_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, uint32_t xdim, const char* comment, const char* unit) {
   A2lCreateCurve_(A2lGetSymbolName(instanceName, name), type, ext, addr, xdim, comment, unit);
}
 
 
 