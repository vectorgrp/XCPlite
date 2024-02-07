/*----------------------------------------------------------------------------
| File:
|   A2L.c
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
#include "dbg_print.h"
#include "xcpLite.h"
#include "A2L.h"

static FILE* gA2lFile = NULL;
static uint16_t gA2lFixedEvent = XCP_INVALID_EVENT;
static uint16_t gA2lDefaultEvent = XCP_INVALID_EVENT;

static uint32_t gA2lMeasurements;
static uint32_t gA2lParameters;
static uint32_t gA2lTypedefs;
static uint32_t gA2lComponents;
static uint32_t gA2lInstances;
static uint32_t gA2lConversions;

//----------------------------------------------------------------------------------
static const char* gA2lHeader =
"ASAP2_VERSION 1 71\n"
"/begin PROJECT %s \"\"\n"
"/begin HEADER \"\" VERSION \"1.0\" /end HEADER\n"
"/begin MODULE %s \"\"\n"
"/include \"XCP_104.aml\"\n\n"

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
static const char* gA2lMemorySegment =
"/begin MEMORY_SEGMENT\n"
"CALRAM \"\" DATA FLASH INTERN 0x%08X 0x%08X -1 -1 -1 -1 -1\n" // CALRAM_START, CALRAM_SIZE
"/begin IF_DATA XCP\n"
"/begin SEGMENT 0x01 0x02 0x00 0x00 0x00 \n"
"/begin CHECKSUM XCP_ADD_44 MAX_BLOCK_SIZE 0xFFFF EXTERNAL_FUNCTION \"\" /end CHECKSUM\n"
"/begin PAGE 0x01 ECU_ACCESS_WITH_XCP_ONLY XCP_READ_ACCESS_WITH_ECU_ONLY XCP_WRITE_ACCESS_NOT_ALLOWED /end PAGE\n"
"/begin PAGE 0x00 ECU_ACCESS_WITH_XCP_ONLY XCP_READ_ACCESS_WITH_ECU_ONLY XCP_WRITE_ACCESS_WITH_ECU_ONLY /end PAGE\n"
"/end SEGMENT\n"
"/end IF_DATA\n"
"/end MEMORY_SEGMENT\n";
#endif

//----------------------------------------------------------------------------------
static const char* const gA2lIfDataBegin =
"\n/begin IF_DATA XCP\n";

//----------------------------------------------------------------------------------
static const char* gA2lIfDataProtocolLayer = // Parameter: XCP_PROTOCOL_LAYER_VERSION, MAX_CTO, MAX_DTO 
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
#if XCP_TRANSPORT_LAYER_TYPE == XCP_TRANSPORT_LAYER_ETH
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
#endif // ETH
"/end PROTOCOL_LAYER\n"

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
/*
"/begin TIME_CORRELATION\n" // TIME
"/end TIME_CORRELATION\n"
*/
#endif
;

//----------------------------------------------------------------------------------
static const char* gA2lIfDataBeginDAQ = // Parameter: %u max event, %s timestamp unit
"/begin DAQ\n"
"DYNAMIC 0 %u 0 OPTIMISATION_TYPE_DEFAULT ADDRESS_EXTENSION_FREE IDENTIFICATION_FIELD_TYPE_RELATIVE_BYTE GRANULARITY_ODT_ENTRY_SIZE_DAQ_BYTE 0xF8 OVERLOAD_INDICATION_PID\n"
"/begin TIMESTAMP_SUPPORTED\n"
"0x01 SIZE_DWORD %s TIMESTAMP_FIXED\n"
"/end TIMESTAMP_SUPPORTED\n"; 

// ... Event list follows, before EndDaq

//----------------------------------------------------------------------------------
static const char * const gA2lIfDataEndDAQ =
"/end DAQ\n";


//----------------------------------------------------------------------------------
// XCP_ON_ETH
static const char* gA2lIfDataEth = // Parameter: %s TCP or UDP, %04X tl version, %u port, %s ip address string, %s TCP or UDP
"/begin XCP_ON_%s_IP\n" // Transport Layer
"  0x%04X %u ADDRESS \"%s\"\n"
//"OPTIONAL_TL_SUBCMD GET_SERVER_ID\n"
//"OPTIONAL_TL_SUBCMD GET_DAQ_ID\n"
//"OPTIONAL_TL_SUBCMD SET_DAQ_ID\n"
#if defined(XCPTL_ENABLE_MULTICAST) && defined(XCP_ENABLE_DAQ_CLOCK_MULTICAST)
"  OPTIONAL_TL_SUBCMD GET_DAQ_CLOCK_MULTICAST\n"
#endif
"/end XCP_ON_%s_IP\n" // Transport Layer
;

//----------------------------------------------------------------------------------
// XCP_ON_CAN (CAN_FD not implemented yet)
static const char* gA2lIfDataCan = // Parameter: TRANSPORT_LAYER_VERSION, CRO_ID, DTO_ID, BITRATE
"/begin XCP_ON_CAN\n" // Transport Layer
"  0x%04X\n"
"  CAN_ID_MASTER 0x%x\n"
"  CAN_ID_SLAVE 0x%x\n"
"  BAUDRATE %u\n"
"  SAMPLE_POINT 0x4B\n"
"  SAMPLE_RATE SINGLE\n"
"  BTL_CYCLES 0x08\n"
"  SJW 0x02\n"
"  SYNC_EDGE SINGLE\n"
"/end XCP_ON_CAN\n";


//----------------------------------------------------------------------------------
static const char* const gA2lIfDataEnd =
"/end IF_DATA\n\n";


//----------------------------------------------------------------------------------
static const char* const gA2lFooter =
	"/end MODULE\n"
	"/end PROJECT\n";



#define printPhysUnit(unit) if (unit != NULL && strlen(unit) > 0) fprintf(gA2lFile, " PHYS_UNIT \"%s\"", unit);
#define printAddrExt(ext) if (ext>0) fprintf(gA2lFile, " ECU_ADDRESS_EXTENSION %u",ext);

const char* A2lGetSymbolName(const char* instanceName, const char* name) {

	static char s[256];
	if (instanceName != NULL && strlen(instanceName) > 0) {
		SNPRINTF(s, 256, "%s.%s", instanceName, name);
		return s;
	}
	else {
		return name;
	}
}

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
	default: 
		types = NULL;
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
	case A2L_TYPE_FLOAT:	min = "-1E12"; break;
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

static const char* getPhysMin(int32_t type, double factor, double offset) {
	double value = 0.0;
	switch (type) {
	case A2L_TYPE_INT8:		value = -128; break;
	case A2L_TYPE_INT16:	value = -32768; break;
	case A2L_TYPE_INT32:	value = -(double)2147483648; break;
	case A2L_TYPE_INT64:	value = -1E12; break;
	case A2L_TYPE_FLOAT:	value = -1E12; break;
	case A2L_TYPE_DOUBLE:	value = -1E12; break;
	default:                value = 0.0;
	}

	static char str[20];
	snprintf(str, 20, "%f", factor * value + offset);
	return str;
}

static const char* getPhysMax(int32_t type, double factor, double offset) {
	double value = 0.0;
	switch (type) {
	case A2L_TYPE_INT8:		value = 127; break;
	case A2L_TYPE_INT16:	value = 32767; break;
	case A2L_TYPE_INT32:	value = 2147483647; break;
	case A2L_TYPE_UINT8:  value = 255; break;
	case A2L_TYPE_UINT16: value = 65535; break;
	case A2L_TYPE_UINT32: value = 4294967295; break;
	default:                value = 1E12;
	}

	static char str[20];
	snprintf(str, 20, "%f", factor * value + offset);
	return str;
}


BOOL A2lOpen(const char *filename, const char* projectName ) {

	DBG_PRINTF1("\nCreate A2L %s\n", filename);
	gA2lFile = NULL;
	gA2lFixedEvent = XCP_INVALID_EVENT;
	gA2lMeasurements = gA2lParameters = gA2lTypedefs = gA2lInstances = gA2lConversions = gA2lComponents = 0;
	gA2lFile = fopen(filename, "w");
	if (gA2lFile == 0) {
		DBG_PRINTF_ERROR("ERROR: Could not create A2L file %s!\n", filename);
		return FALSE;
	}

	// Create header
	fprintf(gA2lFile, gA2lHeader, projectName, projectName);

	// Create standard record layouts for elementary types
	for (int i = -10; i <= +10; i++) {
		const char* t = getType(i);
		if (t != NULL) {
			fprintf(gA2lFile, "/begin RECORD_LAYOUT R_%s FNC_VALUES 1 %s ROW_DIR DIRECT /end RECORD_LAYOUT\n", t, t);
			fprintf(gA2lFile, "/begin TYPEDEF_MEASUREMENT M_%s \"\" %s NO_COMPU_METHOD 0 0 %s %s /end TYPEDEF_MEASUREMENT\n", t, t, getTypeMin(i), getTypeMax(i));
			fprintf(gA2lFile, "/begin TYPEDEF_CHARACTERISTIC C_%s \"\" VALUE R_%s 0 NO_COMPU_METHOD %s %s /end TYPEDEF_CHARACTERISTIC\n", t, t, getTypeMin(i), getTypeMax(i));
		}
	}
	fprintf(gA2lFile, "\n");

	return TRUE;
}

// Memory segments
#if OPTION_ENABLE_CAL_SEGMENT
void A2lCreate_MOD_PAR(uint32_t startAddr, uint32_t size, char *epk) {

	assert(gA2lFile != NULL);
	fprintf(gA2lFile, "/begin MOD_PAR \"\"\n");
	fprintf(gA2lFile, "EPK \"%s\"\n", epk);
	fprintf(gA2lFile, "ADDR_EPK 0x%08X\n", ApplXcpGetAddr((uint8_t*)epk));
	fprintf(gA2lFile, gA2lMemorySegment, startAddr, size);
	DBG_PRINTF1("  A2L MOD_PAR MEMORY_SEGMENT 1: 0x%08X %u\n", startAddr, size);
	fprintf(gA2lFile, "/end MOD_PAR\n\n");
	if (epk) DBG_PRINTF1("  A2L MOD_PAR EPK \"%s\" 0x%08X\n", epk, ApplXcpGetAddr((uint8_t*)epk));
}
#endif


static void A2lCreate_IF_DATA_DAQ() {

	assert(gA2lFile != NULL);

#if defined( XCP_ENABLE_DAQ_EVENT_LIST ) && !defined( XCP_ENABLE_DAQ_EVENT_INFO )
	tXcpEvent* eventList;
#endif
	uint16_t eventCount = 0;

#if (XCP_TIMESTAMP_UNIT==DAQ_TIMESTAMP_UNIT_1NS)
#define XCP_TIMESTAMP_UNIT_S "UNIT_1NS"
#elif (XCP_TIMESTAMP_UNIT==DAQ_TIMESTAMP_UNIT_1US)
#define XCP_TIMESTAMP_UNIT_S "UNIT_1US"
#else
#error
#endif

	// Event list in A2L file (if event info by XCP is not active)
#if defined( XCP_ENABLE_DAQ_EVENT_LIST ) && !defined( XCP_ENABLE_DAQ_EVENT_INFO )
	eventList = XcpGetEventList(&eventCount);
#endif

	fprintf(gA2lFile, gA2lIfDataBeginDAQ, eventCount, XCP_TIMESTAMP_UNIT_S);

	// Eventlist
#if defined( XCP_ENABLE_DAQ_EVENT_LIST ) && !defined( XCP_ENABLE_DAQ_EVENT_INFO )
	for (uint32_t i = 0; i < eventCount; i++) {

		// Shortened name
		char shortName[9];
		strncpy(shortName, eventList[i].name, 8);
		shortName[8] = 0;

		fprintf(gA2lFile, "/begin EVENT \"%s\" \"%s\" 0x%X DAQ 0xFF %u %u %u CONSISTENCY EVENT", eventList[i].name, shortName, i, eventList[i].timeCycle, eventList[i].timeUnit, eventList[i].priority);
#ifdef XCP_ENABLE_PACKED_MODE
		if (eventList[i].sampleCount != 0) {
			fprintf(gA2lFile, " /begin DAQ_PACKED_MODE ELEMENT_GROUPED STS_LAST MANDATORY %u /end DAQ_PACKED_MODE", eventList[i].sampleCount);
		}
#endif
		fprintf(gA2lFile, " /end EVENT\n");
	}
#endif

	fprintf(gA2lFile, gA2lIfDataEndDAQ);

}

void A2lCreate_ETH_IF_DATA(BOOL useTCP, const uint8_t* addr, uint16_t port) {

	fprintf(gA2lFile, gA2lIfDataBegin);

	// Protocol Layer info
	fprintf(gA2lFile, gA2lIfDataProtocolLayer, XCP_PROTOCOL_LAYER_VERSION, XCPTL_MAX_CTO_SIZE, XCPTL_MAX_DTO_SIZE);

	// DAQ info
	A2lCreate_IF_DATA_DAQ();

	// Transport Layer info
	uint8_t addr0[] = { 127,0,0,1 }; // Use localhost if no other option
	if (addr != NULL && addr[0] != 0) {
		memcpy(addr0, addr, 4);
	} else {
		socketGetLocalAddr(NULL, addr0);
	}
	char addrs[17];
	SPRINTF(addrs, "%u.%u.%u.%u", addr0[0], addr0[1], addr0[2], addr0[3]);
	char* prot = useTCP ? (char*)"TCP" : (char*)"UDP";
	fprintf(gA2lFile, gA2lIfDataEth, prot, XCP_TRANSPORT_LAYER_VERSION, port, addrs, prot);

	fprintf(gA2lFile, gA2lIfDataEnd);

	DBG_PRINTF1("  IF_DATA XCP_ON_%s, ip=%s, port=%u\n", prot, addrs, port);
}

void A2lCreate_CAN_IF_DATA(BOOL useCANFD, uint16_t croId, uint16_t dtoId, uint32_t bitRate) {

	(void)useCANFD;

	fprintf(gA2lFile, gA2lIfDataBegin);
	
	// Protocol Layer info
	fprintf(gA2lFile, gA2lIfDataProtocolLayer, XCP_PROTOCOL_LAYER_VERSION, XCPTL_MAX_CTO_SIZE, XCPTL_MAX_DTO_SIZE);

	// DAQ info
	A2lCreate_IF_DATA_DAQ();

	// Transport Layer info
	uint32_t _croId = croId;
	uint32_t _dtoId = dtoId;
	if (useCANFD) {
		_croId |= 0x40000000;
		_dtoId |= 0x40000000;
	}
	fprintf(gA2lFile, gA2lIfDataCan, XCP_TRANSPORT_LAYER_VERSION, _croId, _dtoId, bitRate);

	fprintf(gA2lFile, gA2lIfDataEnd);
		
 	DBG_PRINTF1("  IF_DATA XCP_ON_CAN, CRO=%u, DTO=%u, BITRATE=%u\n", croId, dtoId, bitRate);
}


void A2lCreateMeasurement_IF_DATA() {

	assert(gA2lFile != NULL);
	if (gA2lFixedEvent != XCP_INVALID_EVENT) {
		fprintf(gA2lFile, " /begin IF_DATA XCP /begin DAQ_EVENT FIXED_EVENT_LIST EVENT 0x%X /end DAQ_EVENT /end IF_DATA", gA2lFixedEvent);
	}
	else if (gA2lDefaultEvent != XCP_INVALID_EVENT) {
		fprintf(gA2lFile, " /begin IF_DATA XCP /begin DAQ_EVENT VARIABLE DEFAULT_EVENT_LIST EVENT 0x%X /end DAQ_EVENT /end IF_DATA", gA2lDefaultEvent);
	}
}


void A2lSetDefaultEvent(uint16_t event) {

#if defined( XCP_ENABLE_DAQ_EVENT_LIST ) && !defined( XCP_ENABLE_DAQ_EVENT_INFO )
	uint16_t eventCount = 0;
	XcpGetEventList(&eventCount);
	assert(event >= 0 && event < eventCount);
#endif

	A2lRstFixedEvent();
	gA2lDefaultEvent = event;
}

void A2lSetFixedEvent(uint16_t event) {

#if defined( XCP_ENABLE_DAQ_EVENT_LIST ) && !defined( XCP_ENABLE_DAQ_EVENT_INFO )
	uint16_t eventCount = 0;
	XcpGetEventList(&eventCount);
	assert(event >= 0 && event < eventCount);
#endif

	gA2lFixedEvent = event;
}

uint16_t A2lGetFixedEvent() {
	return gA2lFixedEvent;
}

void A2lRstDefaultEvent() {
	gA2lDefaultEvent = XCP_INVALID_EVENT;
}

void A2lRstFixedEvent() {
	gA2lFixedEvent = XCP_INVALID_EVENT;
}


void A2lTypedefBegin_(const char* name, uint32_t size, const char* comment) {

	assert(gA2lFile != NULL);
	fprintf(gA2lFile,"/begin TYPEDEF_STRUCTURE %s \"%s\" 0x%X SYMBOL_TYPE_LINK \"%s\"\n", name, comment, size, name);
	gA2lTypedefs++;
}

void A2lTypedefMeasurementComponent_(const char* name, int32_t type, uint32_t offset) {

	assert(gA2lFile != NULL);
	fprintf(gA2lFile, "  /begin STRUCTURE_COMPONENT %s M_%s 0x%X SYMBOL_TYPE_LINK \"%s\" /end STRUCTURE_COMPONENT\n", name, getType(type), offset, name);
	gA2lComponents++;
}

void A2lTypedefParameterComponent_(const char* name, int32_t type, uint32_t offset) {

	assert(gA2lFile != NULL);
	fprintf(gA2lFile, "  /begin STRUCTURE_COMPONENT %s C_%s 0x%X SYMBOL_TYPE_LINK \"%s\" /end STRUCTURE_COMPONENT\n", name, getType(type), offset, name);
	gA2lComponents++;
}

void A2lTypedefEnd_() {

	assert(gA2lFile != NULL);
	fprintf(gA2lFile,"/end TYPEDEF_STRUCTURE\n");
}


void A2lCreateTypedefInstance_(const char* instanceName, const char* typeName, uint8_t ext, uint32_t addr, const char* comment) {

	assert(gA2lFile != NULL);
	fprintf(gA2lFile, "/begin INSTANCE %s \"%s\" %s 0x%X", instanceName, comment, typeName, addr);
	printAddrExt(ext);
	A2lCreateMeasurement_IF_DATA();
	fprintf(gA2lFile, " /end INSTANCE\n");
	gA2lInstances++;

}


void A2lCreateMeasurement_(const char* instanceName, const char* name, int32_t type, uint8_t ext, uint32_t addr, double factor, double offset, const char* unit, const char* comment, BOOL symbolLink) {
	
	assert(gA2lFile != NULL);
	if (unit == NULL) unit = "";
	if (comment == NULL) comment = "";
	const char *conv = "NO";
	if (factor != 0.0 || offset != 0.0) {
		fprintf(gA2lFile, "/begin COMPU_METHOD %s.Conversion \"\" LINEAR \"%%6.3\" \"%s\" COEFFS_LINEAR %g %g /end COMPU_METHOD\n", name, unit!=NULL?unit:"", factor,offset);
		conv = name;
		gA2lConversions++;
	}
  
	//fprintf(gA2lFile, "/begin MEASUREMENT %s \"%s\" %s %s.Conversion 0 0 %s %s ECU_ADDRESS 0x%X", A2lGetSymbolName(instanceName, name), comment, getType(type), conv, getTypeMin(type), getTypeMax(type), addr);
	fprintf(gA2lFile, "/begin MEASUREMENT %s \"%s\" %s %s.Conversion 0 0 %s %s ECU_ADDRESS 0x%X", A2lGetSymbolName(instanceName, name), comment, getType(type), conv, getPhysMin(type, factor, offset), getPhysMax(type, factor, offset), addr);
	printAddrExt(ext);	
	printPhysUnit(unit);
	fprintf(gA2lFile, " READ_WRITE");
#if OPTION_ENABLE_A2L_SYMBOL_LINKS
	if (symbolLink) fprintf(gA2lFile, " SYMBOL_LINK \"%s\" %u", A2lGetSymbolName(instanceName, name), 0);
#else
	(void)symbolLink;
#endif
	A2lCreateMeasurement_IF_DATA();
	fprintf(gA2lFile, " /end MEASUREMENT\n");
	gA2lMeasurements++;
}


void A2lCreateMeasurementArray_(const char* instanceName, const char* name, int32_t type, int dim, uint8_t ext, uint32_t addr) {

	assert(gA2lFile != NULL);
	fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"\" VAL_BLK 0x%X R_%s 0 NO_COMPU_METHOD %s %s MATRIX_DIM %u", A2lGetSymbolName(instanceName, name), addr, getType(type), getTypeMin(type), getTypeMax(type), dim);
	printAddrExt(ext);
#if OPTION_ENABLE_A2L_SYMBOL_LINKS
	fprintf(gA2lFile, " SYMBOL_LINK \"%s\" %u", A2lGetSymbolName(instanceName, name), 0);
#endif
	A2lCreateMeasurement_IF_DATA();
	fprintf(gA2lFile, " /end CHARACTERISTIC\n");
	gA2lMeasurements++;
}


void A2lCreateParameterWithLimits_(const char* name, int32_t type, uint8_t ext, uint32_t addr, const char* comment, const char* unit, double min, double max) {

	assert(gA2lFile != NULL);
	fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"%s\" VALUE 0x%X R_%s 0 NO_COMPU_METHOD %g %g",	name, comment, addr, getType(type), min, max);
	printPhysUnit(unit);
	printAddrExt(ext);
#if OPTION_ENABLE_A2L_SYMBOL_LINKS
	fprintf(gA2lFile, " SYMBOL_LINK \"%s\" %u", name, 0);
#endif
	fprintf(gA2lFile, " /end CHARACTERISTIC\n");
	gA2lParameters++;
}

void A2lCreateParameter_(const char* name, int32_t type, uint8_t ext, uint32_t addr, const char* comment, const char* unit) {

	assert(gA2lFile != NULL);
	fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"%s\" VALUE 0x%X R_%s 0 NO_COMPU_METHOD %s %s",	name, comment, addr, getType(type), getTypeMin(type), getTypeMax(type));
	printPhysUnit(unit);
	printAddrExt(ext);
#if OPTION_ENABLE_A2L_SYMBOL_LINKS
	fprintf(gA2lFile, " SYMBOL_LINK \"%s\" %u", name, 0);
#endif
	fprintf(gA2lFile, " /end CHARACTERISTIC\n");
	gA2lParameters++;
}

void A2lCreateMap_(const char* name, int32_t type, uint8_t ext, uint32_t addr, uint32_t xdim, uint32_t ydim, const char* comment, const char* unit) {

	assert(gA2lFile != NULL);
	fprintf(gA2lFile,
		"/begin CHARACTERISTIC %s \"%s\" MAP 0x%X R_%s 0 NO_COMPU_METHOD %s %s"
		" /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR"
		" /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR",
		name, comment, addr, getType(type), getTypeMin(type), getTypeMax(type), xdim, xdim-1, xdim,  ydim, ydim-1, ydim);
	printPhysUnit(unit);
	printAddrExt(ext);
#if OPTION_ENABLE_A2L_SYMBOL_LINKS
	fprintf(gA2lFile, " SYMBOL_LINK \"%s\" %u", name, 0);
#endif
	fprintf(gA2lFile, " /end CHARACTERISTIC\n");
	gA2lParameters++;
}

void A2lCreateCurve_(const char* name, int32_t type, uint8_t ext, uint32_t addr, uint32_t xdim, const char* comment, const char* unit) {

	assert(gA2lFile != NULL);
	fprintf(gA2lFile,
		"/begin CHARACTERISTIC %s \"%s\" CURVE 0x%X R_%s 0 NO_COMPU_METHOD %s %s"
		" /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR",
		name, comment, addr, getType(type), getTypeMin(type), getTypeMax(type),  xdim, xdim-1, xdim);
	printPhysUnit(unit);
	printAddrExt(ext);
#if OPTION_ENABLE_A2L_SYMBOL_LINKS
	fprintf(gA2lFile, " SYMBOL_LINK \"%s\" %u", name, 0);
#endif
	fprintf(gA2lFile, " /end CHARACTERISTIC\n");
	gA2lParameters++;
}


void A2lParameterGroup(const char* name, int count, ...) {

	va_list ap;

	assert(gA2lFile != NULL);
	fprintf(gA2lFile, "/begin GROUP %s \"\"", name);
	fprintf(gA2lFile, " /begin REF_CHARACTERISTIC\n");
	va_start(ap, count);
	for (int i = 0; i < count; i++) {
		fprintf(gA2lFile, " %s", va_arg(ap, char*));
	}
	va_end(ap);
	fprintf(gA2lFile, "\n/end REF_CHARACTERISTIC ");
	fprintf(gA2lFile, "/end GROUP\n\n");
}


void A2lParameterGroupFromList(const char* name, const char* pNames[], size_t count) {

	assert(gA2lFile != NULL);
	fprintf(gA2lFile, "/begin GROUP %s \"\"", name);
	fprintf(gA2lFile, " /begin REF_CHARACTERISTIC\n");
	for (size_t i = 0; i < count; i++) {
		fprintf(gA2lFile, " %s", pNames[i]);
	}
	fprintf(gA2lFile, "\n/end REF_CHARACTERISTIC ");
	fprintf(gA2lFile, "/end GROUP\n\n");
}


void A2lMeasurementGroup(const char* name, int count, ...) {

	va_list ap;

	assert(gA2lFile != NULL);
	fprintf(gA2lFile, "/begin GROUP %s \"\"", name);
	fprintf(gA2lFile, " /begin REF_MEASUREMENT");
	va_start(ap, count);
	for (int i = 0; i < count; i++) {
		fprintf(gA2lFile, " %s", va_arg(ap, char*));
	}
	va_end(ap);
	fprintf(gA2lFile, " /end REF_MEASUREMENT");
	fprintf(gA2lFile, " /end GROUP\n\n");
}


void A2lMeasurementGroupFromList(const char *name, char* names[], uint32_t count) {

	assert(gA2lFile != NULL);
	fprintf(gA2lFile, "/begin GROUP %s \"\" \n", name);
	fprintf(gA2lFile, " /begin REF_MEASUREMENT");
	for (uint32_t i1 = 0; i1 < count; i1++) {
		fprintf(gA2lFile, " %s", names[i1]);
	}
	fprintf(gA2lFile, " /end REF_MEASUREMENT");
	fprintf(gA2lFile, "\n/end GROUP\n");
}


void A2lClose() {

	if (gA2lFile != NULL) {
		fprintf(gA2lFile, "%s", gA2lFooter);
		fclose(gA2lFile);
		gA2lFile = NULL;
		DBG_PRINTF1("A2L created: %u measurements, %u params, %u typedefs, %u components, %u instances, %u conversions\n\n",
			gA2lMeasurements, gA2lParameters, gA2lTypedefs, gA2lComponents, gA2lInstances, gA2lConversions);
	}
}




