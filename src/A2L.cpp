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
#include "main_cfg.h"
#include "platform.h"
#include "util.h"

#include <vector>
using namespace std;

#include "xcp.hpp"
#define A2L_GET_ADDR
#include "A2L.hpp"


static const char* sHeader =
"ASAP2_VERSION 1 71\n"
"/begin PROJECT %s \"\"\n"
"/begin HEADER \"\" VERSION \"1.0\" /end HEADER\n"
"/begin MODULE %s \"\"\n"
"/include \"XCP_104.aml\"\n\n"


//----------------------------------------------------------------------------------
"/begin MOD_COMMON \"\"\n"
"BYTE_ORDER MSB_LAST\n"
"ALIGNMENT_BYTE 1\n"
"ALIGNMENT_WORD 1\n"
"ALIGNMENT_LONG 1\n"
"ALIGNMENT_FLOAT16_IEEE 1\n"
"ALIGNMENT_FLOAT32_IEEE 1\n"
"ALIGNMENT_FLOAT64_IEEE 1\n"
"ALIGNMENT_INT64 1\n"
"/end MOD_COMMON\n"
"\n";


//----------------------------------------------------------------------------------
#if OPTION_ENABLE_CAL_SEGMENT
static const char* sModPar = 
"/begin MOD_PAR \"\"\n"
"/begin MEMORY_SEGMENT\n"
"CALRAM \"\" DATA FLASH INTERN 0x%08X 0x%08X -1 -1 -1 -1 -1\n" // CALRAM_START, CALRAM_SIZE
"/begin IF_DATA XCP\n"
"/begin SEGMENT 0x01 0x02 0x00 0x00 0x00 \n"
"/begin CHECKSUM XCP_ADD_44 MAX_BLOCK_SIZE 0xFFFF EXTERNAL_FUNCTION \"\" /end CHECKSUM\n"
"/begin PAGE 0x01 ECU_ACCESS_WITH_XCP_ONLY XCP_READ_ACCESS_WITH_ECU_ONLY XCP_WRITE_ACCESS_NOT_ALLOWED /end PAGE\n"
"/begin PAGE 0x00 ECU_ACCESS_WITH_XCP_ONLY XCP_READ_ACCESS_WITH_ECU_ONLY XCP_WRITE_ACCESS_WITH_ECU_ONLY /end PAGE\n"
"/end SEGMENT\n"
"/end IF_DATA\n"
"/end MEMORY_SEGMENT\n"
"/end MOD_PAR\n"
"\n";
#endif

 static const char* sIfData1 = // Parameters %04X version, %u max cto, %u max dto, %u max event, %s timestamp unit
"\n/begin IF_DATA XCP\n"

//----------------------------------------------------------------------------------
"/begin PROTOCOL_LAYER\n"
" 0x%04X" // XCP_PROTOCOL_LAYER_VERSION
" 1000 2000 0 0 0 0 0" // Timeouts T1-T7
" %u %u " // MAX_CTO, MAX_DTO
"BYTE_ORDER_MSB_LAST ADDRESS_GRANULARITY_BYTE\n" // Intel and BYTE pointers
"OPTIONAL_CMD GET_COMM_MODE_INFO\n" // Optional commands
"OPTIONAL_CMD GET_ID\n"
"OPTIONAL_CMD SET_MTA\n"
"OPTIONAL_CMD UPLOAD\n"
"OPTIONAL_CMD SHORT_UPLOAD\n"
"OPTIONAL_CMD DOWNLOAD\n"
"OPTIONAL_CMD SHORT_DOWNLOAD\n"
#ifdef XCP_ENABLE_CAL_PAGE
"OPTIONAL_CMD GET_CAL_PAGE\n"
"OPTIONAL_CMD SET_CAL_PAGE\n"
//"OPTIONAL_CMD CC_GET_PAG_PROCESSOR_INFO\n"    
//"OPTIONAL_CMD CC_GET_SEGMENT_INFO\n"          
//"OPTIONAL_CMD CC_GET_PAGE_INFO\n"             
//"OPTIONAL_CMD CC_SET_SEGMENT_MODE\n"          
//"OPTIONAL_CMD CC_GET_SEGMENT_MODE\n"          
//"OPTIONAL_CMD CC_COPY_CAL_PAGE\n"             
#endif
#ifdef XCP_ENABLE_CHECKSUM
"OPTIONAL_CMD BUILD_CHECKSUM\n"
#endif
//"OPTIONAL_CMD TRANSPORT_LAYER_CMD\n"
//"OPTIONAL_CMD USER_CMD\n"
"OPTIONAL_CMD GET_DAQ_RESOLUTION_INFO\n"
"OPTIONAL_CMD GET_DAQ_PROCESSOR_INFO\n"
#ifdef XCP_ENABLE_DAQ_EVENT_INFO
"OPTIONAL_CMD GET_DAQ_EVENT_INFO\n"
#endif
//"OPTIONAL_CMD GET_DAQ_LIST_INFO\n"
"OPTIONAL_CMD FREE_DAQ\n"
"OPTIONAL_CMD ALLOC_DAQ\n"
"OPTIONAL_CMD ALLOC_ODT\n"
"OPTIONAL_CMD ALLOC_ODT_ENTRY\n"
//"OPTIONAL_CMD CLEAR_DAQ_LIST\n"
//"OPTIONAL_CMD READ_DAQ\n"
"OPTIONAL_CMD SET_DAQ_PTR\n"
"OPTIONAL_CMD WRITE_DAQ\n"
"OPTIONAL_CMD GET_DAQ_LIST_MODE\n"
"OPTIONAL_CMD SET_DAQ_LIST_MODE\n"
"OPTIONAL_CMD START_STOP_SYNCH\n"
"OPTIONAL_CMD START_STOP_DAQ_LIST\n"
"OPTIONAL_CMD GET_DAQ_CLOCK\n"
"OPTIONAL_CMD WRITE_DAQ_MULTIPLE\n"
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
"OPTIONAL_CMD TIME_CORRELATION_PROPERTIES\n"
//"OPTIONAL_CMD DTO_CTR_PROPERTIES\n"
#endif
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
"OPTIONAL_LEVEL1_CMD GET_VERSION\n"
#ifdef XCP_ENABLE_PACKED_MODE
"OPTIONAL_LEVEL1_CMD SET_DAQ_PACKED_MODE\n"
"OPTIONAL_LEVEL1_CMD GET_DAQ_PACKED_MODE\n"
  #endif
#endif
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0150
//"OPTIONAL_LEVEL1_CMD SW_DBG_COMMAND_SPACE\n"
//"OPTIONAL_LEVEL1_CMD POD_COMMAND_SPACE\n"
#endif
"/end PROTOCOL_LAYER\n"

//----------------------------------------------------------------------------------
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
/*
"/begin TIME_CORRELATION\n" // TIME
"/end TIME_CORRELATION\n"
*/
#endif

//----------------------------------------------------------------------------------
"/begin DAQ\n" // DAQ
"DYNAMIC 0 %u 0 OPTIMISATION_TYPE_DEFAULT ADDRESS_EXTENSION_FREE IDENTIFICATION_FIELD_TYPE_RELATIVE_BYTE GRANULARITY_ODT_ENTRY_SIZE_DAQ_BYTE 0xF8 OVERLOAD_INDICATION_PID\n"
"/begin TIMESTAMP_SUPPORTED\n"
"0x01 SIZE_DWORD %s TIMESTAMP_FIXED\n"
"/end TIMESTAMP_SUPPORTED\n"; // ... Event list follows

 static const char* sIfData2 = // Parameter %s TCP or UDP, %04X tl version, %u port, %s ip address string, %s TCP or UDP
"/end DAQ\n"

"/begin XCP_ON_%s_IP\n" // Transport Layer
"  0x%04X %u ADDRESS \"%s\"\n"
//"OPTIONAL_TL_SUBCMD GET_SERVER_ID\n"
//"OPTIONAL_TL_SUBCMD GET_DAQ_ID\n"
//"OPTIONAL_TL_SUBCMD SET_DAQ_ID\n"
#if defined(XCPTL_ENABLE_MULTICAST) && defined(XCP_ENABLE_DAQ_CLOCK_MULTICAST)
"  OPTIONAL_TL_SUBCMD GET_DAQ_CLOCK_MULTICAST\n"
#endif
"/end XCP_ON_%s_IP\n" // Transport Layer

"/end IF_DATA\n\n"
;

 static const char* sFooter =
"/end MODULE\n"
"/end PROJECT\n\n\n\n\n\n"
;

 //---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

 static const char* getType(int32_t type) {
	const char* types;
	switch (type) {
	case A2L_TYPE_INT8:    types = "SBYTE";  break;
	case A2L_TYPE_INT16:   types = "SWORD";  break;
	case A2L_TYPE_INT32:   types = "SLONG";  break;
	case A2L_TYPE_INT64:   types = "A_INT64";  break;
	case A2L_TYPE_UINT8:   types = "UBYTE";  break;
	case A2L_TYPE_UINT16:  types = "UWORD";  break;
	case A2L_TYPE_UINT32:  types = "ULONG";  break;
	case A2L_TYPE_UINT64:  types = "A_UINT64";  break;
	case A2L_TYPE_FLOAT:   types = "FLOAT32_IEEE";  break;
	case A2L_TYPE_DOUBLE:  types = "FLOAT64_IEEE";  break;
	default: types = NULL;
	}
	return types;
}

 static const char* getTypeMin(int32_t type) {
	const char* min;
	switch (type) {
	case A2L_TYPE_INT8:		min = "-128"; break; 
	case A2L_TYPE_INT16:	min = "-32768"; break; 
	case A2L_TYPE_INT32:	min = "-2147483648"; break; 
	case A2L_TYPE_INT64:	min = "-1E12"; break; 
	case A2L_TYPE_DOUBLE:	min = "-1E12"; break; 
	default:                min = "0";
	}
	return min;
}

 static const char* getTypeMax(int32_t type) {
	const char* max;
	switch (type) {
	case A2L_TYPE_INT8:	   max = "127"; break;
	case A2L_TYPE_INT16:   max = "32767"; break;
	case A2L_TYPE_INT32:   max = "2147483647"; break;
	case A2L_TYPE_UINT8:   max = "255"; break;
	case A2L_TYPE_UINT16:  max = "65535"; break;
	case A2L_TYPE_UINT32:  max = "4294967295"; break;
	default:               max = "1E12";
	}
	return max;
}


 uint32_t A2L::encodeDynAddr(uint8_t ext, uint32_t addr) {
	 if (ext == 1) { // dynamic addr, addr is offset to this
		 if (addr > 0xFFFF) DBG_PRINT_ERROR("ERROR: Offset too large!\n");
		 return (addr & 0xFFFF) | ((uint32_t)event << 16); // Use event and offset coding for address

	 }
	 return addr;
 }

#define printPhysUnit(unit) if (unit != NULL && strlen(unit) > 0) fprintf(file, " PHYS_UNIT \"%s\" ", unit);
#define printAddrExt(ext) if (ext>0) fprintf(file, " ECU_ADDRESS_EXTENSION %u ",ext);

 void A2L::printName(const char* type, const char* instanceName, const char* name) {
	 if (instanceName != NULL && strlen(instanceName) > 0) {
		 fprintf(file, "/begin %s %s.%s ", type, instanceName, name);
	 }
	 else {
		 fprintf(file, "/begin %s %s ", type, name);
	 }
 }

 //---------------------------------------------------------------------------------------------------------------------------------------------------------------------------


 A2L::A2L(const char * name) {
	 
	filename = name;
	file = NULL;
	event = 0xFFFF;
	cntMeasurements = cntParameters = cntTypedefs = cntComponents = cntInstances = cntConversions = 0;
 }

 A2L::~A2L() {

	 close();
 }

void A2L::close() {

	if (!file) return;

	fprintf(file, "%s", sFooter);
	fclose(file);
	DBG_PRINTF2("A2L close: %u measurements, %u params, %u typedefs, %u components, %u instances, %u conversions\n",
		cntMeasurements, cntParameters, cntTypedefs, cntComponents, cntInstances, cntConversions);

	file = NULL;
 }


BOOL A2L::open(const char *projectName) {

	if (file) return FALSE;

	DBG_PRINTF2("A2L open %s\n", filename);
#ifdef _LINUX // Linux
	file = fopen(filename, "w");
	if (!file) {
#else
	if (fopen_s(&file, filename, "w")) {
#endif
		    DBG_PRINTF_ERROR("ERROR: Could not create A2L file %s!\n", filename);
			return FALSE;
	}
	
	fprintf(file, sHeader, projectName, projectName);

  // Create standard record layouts for elementary types
  for (int i = -10; i <= +10; i++) {
	  const char* t = getType(i);
	  if (t != NULL) {
		  fprintf(file, "/begin RECORD_LAYOUT R_%s FNC_VALUES 1 %s ROW_DIR DIRECT /end RECORD_LAYOUT\n", t, t);
		  fprintf(file, "/begin TYPEDEF_MEASUREMENT M_%s \"\" %s NO_COMPU_METHOD 0 0 %s %s /end TYPEDEF_MEASUREMENT\n", t, t, getTypeMin(i), getTypeMax(i));
		  fprintf(file, "/begin TYPEDEF_CHARACTERISTIC C_%s \"\" VALUE R_%s 0 NO_COMPU_METHOD %s %s /end TYPEDEF_CHARACTERISTIC\n", t, t, getTypeMin(i), getTypeMax(i));
	  }
  }
  fprintf(file, "\n");
   
  return TRUE;
}

// Create memory segments
#if OPTION_ENABLE_CAL_SEGMENT
void A2L::create_MOD_PAR(uint32_t startAddr, uint32_t size) {
	fprintf(file, sModPar, startAddr, size);
}
#endif
// Create XCP IF_DATA
void A2L::create_XCP_IF_DATA(BOOL tcp, const uint8_t* addr, uint16_t port) {

#if (XCP_TIMESTAMP_UNIT==DAQ_TIMESTAMP_UNIT_1NS)
#define XCP_TIMESTAMP_UNIT_S "UNIT_1NS"
#elif (XCP_TIMESTAMP_UNIT==DAQ_TIMESTAMP_UNIT_1US)
#define XCP_TIMESTAMP_UNIT_S "UNIT_1US"
#else
#error
#endif

	// Event list in A2L file (if event info by XCP is not active)
	vector<Xcp::XcpEventDescriptor>* eventList = Xcp::getInstance()->getEventList();
	fprintf(file, sIfData1, XCP_PROTOCOL_LAYER_VERSION, XCPTL_MAX_CTO_SIZE, XCPTL_MAX_DTO_SIZE, eventList->size(), XCP_TIMESTAMP_UNIT_S);
	for (unsigned int i = 0; i < eventList->size(); i++) {
		Xcp::XcpEventDescriptor e = eventList->at(i);
		// Shortened name
		char shortName[9];
		strncpy(shortName, e.name, 8);
		shortName[8] = 0;

		fprintf(file, "/begin EVENT \"%s\" \"%s\" 0x%X DAQ 0xFF %u %u %u CONSISTENCY DAQ", e.name, shortName, i, e.timeCycle, e.timeUnit, e.priority);
#ifdef XCP_ENABLE_PACKED_MODE
		if (eventList[i].sampleCount != 0) {
			fprintf(file, " /begin DAQ_PACKED_MODE ELEMENT_GROUPED STS_LAST MANDATORY %u /end DAQ_PACKED_MODE", eventList[i].sampleCount);
		}
#endif
		fprintf(file, " /end EVENT\n");
	}

	// Transport Layer info ipaddr, port, protocol, version
	uint8_t addr0[] = { 127,0,0,1 }; // Use localhost if no other option
	if (addr != NULL && addr[0] != 0) {
		memcpy(addr0, addr, 4);
	}
	char addrs[17];
	SPRINTF(addrs, "%u.%u.%u.%u", addr0[0], addr0[1], addr0[2], addr0[3]);
	char* prot = tcp ? (char*)"TCP" : (char*)"UDP";
	fprintf(file, sIfData2, prot, XCP_TRANSPORT_LAYER_VERSION, port, addrs, prot);
}


//---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

 void A2L::setEvent(uint16_t e) {
	event = e;
}

 void A2L::rstEvent() {
	event = 0xFFFF;
}

 //---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

 void A2L::createTypedefBegin_(const char* name, int32_t size, const char* comment) {
	if (!file) return;
	fprintf(file,"/begin TYPEDEF_STRUCTURE %s \"%s\" 0x%X SYMBOL_TYPE_LINK \"%s\"\n", name, comment, size, name);
	cntTypedefs++;
}

 void A2L::createTypedefMeasurementComponent_(const char* name, int32_t type, uint32_t offset) {
	if (!file) return;
	fprintf(file, "  /begin STRUCTURE_COMPONENT %s M_%s 0x%X SYMBOL_TYPE_LINK \"%s\" /end STRUCTURE_COMPONENT\n", name, getType(type), offset, name);
	cntComponents++;
 }

 void A2L::createTypedefParameterComponent_(const char* name, int32_t type, uint32_t offset) {
	if (!file) return;
	fprintf(file, "  /begin STRUCTURE_COMPONENT %s C_%s 0x%X SYMBOL_TYPE_LINK \"%s\" /end STRUCTURE_COMPONENT\n", name, getType(type), offset, name);
	cntComponents++;
 }

 void A2L::createTypedefEnd_() {
	if (!file) return;
	fprintf(file,"/end TYPEDEF_STRUCTURE\n");
}

 void A2L::createTypedefInstance_(const char* instanceName, const char* typeName, uint8_t ext, uint32_t addr, const char* comment) {
	if (!file) return;
	fprintf(file, "/begin INSTANCE %s \"%s\" %s 0x%X", instanceName, comment, typeName, encodeDynAddr(ext, addr));
	printAddrExt(ext);
	if (event >= 0) {
		fprintf(file, " /begin IF_DATA XCP /begin DAQ_EVENT FIXED_EVENT_LIST EVENT 0x%X /end DAQ_EVENT /end IF_DATA", event);
	}
	fprintf(file, " /end INSTANCE\n");
	cntInstances++;
}

 //---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

 void A2L::createMeasurement_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, double factor, double offset, const char* unit, const char* comment) {
	if (!file) return;
	if (comment == NULL) comment = "";
	const char *conv = "NO";
	if (factor != 0.0 || offset != 0.0) {
		fprintf(file, "/begin COMPU_METHOD %s_COMPU_METHOD \"\" LINEAR \"%%6.3\" \"%s\" COEFFS_LINEAR %g %g /end COMPU_METHOD\n", name, unit!=NULL?unit:"", factor,offset);
		conv = name;
		cntConversions++;
	}
	printName("MEASUREMENT", instanceName, name);
	fprintf(file, "\"%s\" %s %s_COMPU_METHOD 0 0 %s %s ECU_ADDRESS 0x%X", comment, getType(type), conv, getTypeMin(type), getTypeMax(type), encodeDynAddr(ext, addr));
	printAddrExt(ext);
	printPhysUnit(unit);
	if (event >= 0) {
		fprintf(file," /begin IF_DATA XCP /begin DAQ_EVENT FIXED_EVENT_LIST EVENT 0x%X /end DAQ_EVENT /end IF_DATA", event);
	}
	fprintf(file, " /end MEASUREMENT\n");
	cntMeasurements++;
}


 void A2L::createMeasurementArray_(const char* instanceName, const char* name, int32_t type, int dim, uint8_t ext, uint32_t addr) {

	 if (!file) return;
	 printName("CHARACTERISTIC", instanceName, name);
	 fprintf(file, "\"\" VAL_BLK 0x%X R_%s 0 NO_COMPU_METHOD %s %s MATRIX_DIM %u", encodeDynAddr(ext, addr), getType(type), getTypeMin(type), getTypeMax(type), dim);
	 printAddrExt(ext);
	if (event>=0) {
		fprintf(file," /begin IF_DATA XCP /begin DAQ_EVENT FIXED_EVENT_LIST EVENT 0x%X /end DAQ_EVENT /end IF_DATA", event);
	}
	fprintf(file, " /end CHARACTERISTIC\n");
	cntMeasurements++;
}

 //---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
  
 
 void A2L::createParameterWithLimits_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, const char* comment, const char* unit, double min, double max) {

	 if (!file) return;
	 printName("CHARACTERISTIC",instanceName, name);
	 fprintf(file, "\"%s\" VALUE 0x%X R_%s 0 NO_COMPU_METHOD %g %g ", comment, encodeDynAddr(ext, addr), getType(type), min, max);
     printAddrExt(ext);
	 printPhysUnit(unit); 
	 fprintf(file, "/end CHARACTERISTIC\n");
	 cntParameters++;
}

 
 void A2L::createParameter_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, const char* comment, const char* unit) {

	 if (!file) return;
	 printName("CHARACTERISTIC", instanceName, name);
	 fprintf(file, "\"%s\" VALUE 0x%X R_%s 0 NO_COMPU_METHOD %s %s ",
		comment, encodeDynAddr(ext, addr), getType(type), getTypeMin(type), getTypeMax(type));
	 printAddrExt(ext);
	 printPhysUnit(unit);
	 fprintf(file, "/end CHARACTERISTIC\n");
	 cntParameters++;
}

 void A2L::createMap_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, uint32_t xdim, uint32_t ydim, const char* comment, const char* unit) {

	 if (!file) return;
	 printName("CHARACTERISTIC", instanceName, name);
	 fprintf(file,
		"\"%s\" MAP 0x%X R_%s 0 NO_COMPU_METHOD %s %s"
		" /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR"
		" /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR",
		comment, encodeDynAddr(ext, addr), getType(type), getTypeMin(type), getTypeMax(type), xdim, xdim-1, xdim,  ydim, ydim-1, ydim);
	 printAddrExt(ext);
	 printPhysUnit(unit);
	 fprintf(file, "/end CHARACTERISTIC\n");
	 cntParameters++;
}

 void A2L::createCurve_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, uint32_t xdim, const char* comment, const char* unit) {

	 if (!file) return;
	 printName("CHARACTERISTIC", instanceName, name);
	 fprintf(file,
	    "\"%s\" CURVE 0x%X R_%s 0 NO_COMPU_METHOD %s %s"
		" /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR",
		comment, encodeDynAddr(ext, addr), getType(type), getTypeMin(type), getTypeMax(type),  xdim, xdim-1, xdim);
	 printAddrExt(ext);
	 printPhysUnit(unit);
	 fprintf(file, "/end CHARACTERISTIC\n");
	 cntParameters++;
}
 
 //---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

 void A2L::createParameterGroup(const char* name, int count, ...) {

	va_list ap;

	if (!file) return;
	fprintf(file, "/begin GROUP %s \"\"", name);
	fprintf(file, " /begin REF_CHARACTERISTIC\n");
	va_start(ap, count);
	for (int i = 0; i < count; i++) {
		fprintf(file, " %s", va_arg(ap, char*));
	}
	va_end(ap);
	fprintf(file, "\n/end REF_CHARACTERISTIC ");
	fprintf(file, "/end GROUP\n\n");
}

 void A2L::createMeasurementGroup(const char* name, int count, ...) {

	va_list ap;

	if (!file) return;
	fprintf(file, "/begin GROUP %s \"\"", name);
	fprintf(file, " /begin REF_MEASUREMENT");
	va_start(ap, count);
	for (int i = 0; i < count; i++) {
		fprintf(file, " %s", va_arg(ap, char*));
	}
	va_end(ap);
	fprintf(file, " /end REF_MEASUREMENT");
	fprintf(file, " /end GROUP\n\n");
}


 




