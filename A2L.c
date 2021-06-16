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
#include "A2L.h"

#ifdef XCPSIM_ENABLE_A2L_GEN

static FILE* gA2lFile = NULL;
static const char *gA2lFilename = NULL;
static int gA2lEvent = 0;

unsigned int gA2lMeasurements;
unsigned int gA2lParameters;
unsigned int gA2lTypedefs;
unsigned int gA2lComponents;
unsigned int gA2lInstances;
unsigned int gA2lConversions;

static const char* gA2lHeader =
"ASAP2_VERSION 1 71\n"
"/begin PROJECT XCPlite \"\"\n"
"/begin HEADER \"\" VERSION \"1.0\" /end HEADER\n"
"/begin MODULE XCPlite \"\"\n"
"/include \"XCP_104.aml\"\n"

"/begin MOD_PAR \"\"\n"
"/begin MEMORY_SEGMENT\n"
"CALRAM \"\" DATA FLASH INTERN 0x%08X 0x%08X - 1 - 1 - 1 - 1 - 1\n" // CALRAM_START, CALRAM_SIZE
"/begin IF_DATA XCP\n"
"/begin SEGMENT 0x01 0x02 0x00 0x00 0x00 \n"
"/begin CHECKSUM XCP_ADD_44 MAX_BLOCK_SIZE 0xFFFF EXTERNAL_FUNCTION \"\" /end CHECKSUM\n"
"/begin PAGE 0x01 ECU_ACCESS_WITH_XCP_ONLY XCP_READ_ACCESS_WITH_ECU_ONLY XCP_WRITE_ACCESS_NOT_ALLOWED /end PAGE\n"
"/begin PAGE 0x00 ECU_ACCESS_WITH_XCP_ONLY XCP_READ_ACCESS_WITH_ECU_ONLY XCP_WRITE_ACCESS_WITH_ECU_ONLY /end PAGE\n"
"/end SEGMENT\n"
"/end IF_DATA\n"
"/end MEMORY_SEGMENT\n"
"/end MOD_PAR\n"

"/begin MOD_COMMON \"\"\n"
"BYTE_ORDER MSB_LAST\n"
"ALIGNMENT_BYTE 1\n"
"ALIGNMENT_WORD 1\n"
"ALIGNMENT_LONG 1\n"
"ALIGNMENT_FLOAT16_IEEE 1\n"
"ALIGNMENT_FLOAT32_IEEE 1\n"
"ALIGNMENT_FLOAT64_IEEE 1\n"
"ALIGNMENT_INT64 1\n"
"/end MOD_COMMON\n";

static const char* gA2lIfData1 = // Parameters %04X version, %u max cto, %u max dto, %u max event
"/begin IF_DATA XCP\n"

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
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0130
"OPTIONAL_CMD TIME_CORRELATION_PROPERTIES\n"
//"OPTIONAL_CMD DTO_CTR_PROPERTIES\n"
"OPTIONAL_LEVEL1_CMD GET_VERSION\n"
#endif
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0140
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
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0130
/*
"/begin TIME_CORRELATION\n" // TIME
"/end TIME_CORRELATION\n"
*/
#endif

"/begin DAQ\n" // DAQ
"DYNAMIC 0 %u 0 OPTIMISATION_TYPE_DEFAULT ADDRESS_EXTENSION_FREE IDENTIFICATION_FIELD_TYPE_RELATIVE_BYTE GRANULARITY_ODT_ENTRY_SIZE_DAQ_BYTE 0xF8 OVERLOAD_INDICATION_PID\n"
"/begin TIMESTAMP_SUPPORTED\n"
"0x01 SIZE_DWORD UNIT_1US TIMESTAMP_FIXED\n"
"/end TIMESTAMP_SUPPORTED\n"; // ... Event list follows

static const char* gA2lIfData2 = // Parameter %u port and %s ip address string
"/end DAQ\n"
"/begin XCP_ON_UDP_IP 0x%04X %u ADDRESS \"%s\" /end XCP_ON_UDP_IP\n" // Transport Layer
"/end IF_DATA\n"
;

static const char* gA2lFooter =
"/end MODULE\n"
"/end PROJECT\n\n\n\n\n\n"
;

static const char* getParType(int size) {
	char* type;
	switch (size) {
	case -1: type = "_SBYTE";  break;
	case -2: type = "_SWORD";  break;
	case -4: type = "_SLONG";  break;
	case -10: type = "_A_INT64";  break;
	case 1: type = "_UBYTE";   break;
	case 2: type = "_UWORD";   break;
	case 4: type = "_ULONG";   break;
	case 8: type = "_FLOAT64_IEEE";  break;
	case 10: type = "_A_UINT64";  break;
	default: type = NULL;
	}
	return type;
}

static const char* getMeaType(int size) {
	const char* type = getParType(size);
	if (type == NULL) return NULL;
	return &type[1];
}

static const char* getTypeMin(int size) {
	char* min;
	switch (size) {
	case -1: min = "-128";  break;
	case -2: min = "-32768"; break;
	case -4: min = "-2147483648";  break;
	case -8: 
	case -10: min = "-1E12"; break;
	default: min = "0";
	}
	return min;
}

static const char* getTypeMax(int size) {
	char* max;
	switch (size) {
	case -1: max = "127";  break;
	case -2: max = "32767";  break;
	case -4: max = "2147483647";  break;
	case -10:
	case -8: max = "1E12"; break;
	case 1: max = "255";  break;
	case 2: max = "65535";  break;
	case 4: max = "4294967295";  break;
	case 10:
	case 8: max = "1E12"; break;
	default: max = "1E12";
	}
	return max;
}



int A2lInit(const char *filename) {

	gA2lFile = 0;
	gA2lEvent = -1;
	gA2lMeasurements = gA2lParameters = gA2lTypedefs = gA2lInstances = gA2lConversions = gA2lComponents = 0;
	gA2lFilename = filename;

#ifndef _WIN // Linux
	gA2lFile = fopen(filename, "w");
#else
	fopen_s(&gA2lFile,filename, "w");
#endif
	if (gA2lFile == 0) {
		printf("ERROR: Could not create A2L file %s!\n", filename);
		return 0;
	}
	return 1;
}


void A2lHeader(void) {

  unsigned int protocolLayerVersion = (XCP_PROTOCOL_LAYER_VERSION & 0xFF00) | ((XCP_PROTOCOL_LAYER_VERSION >> 4) & 0x000F); // Protocol and A2L have different coding
  unsigned int transportLayerVersion = (XCP_TRANSPORT_LAYER_VERSION & 0xFF00) | ((XCP_TRANSPORT_LAYER_VERSION >> 4) & 0x000F); 

  assert(gA2lFile);

  printf("\nCreate A2L %s\n", gA2lFilename);

  fprintf(gA2lFile, gA2lHeader, (unsigned int)ApplXcpGetAddr((vuint8 *)&ecuPar), (unsigned int)sizeof(ecuPar)); 
  fprintf(gA2lFile, gA2lIfData1, protocolLayerVersion, XCPTL_CTO_SIZE, XCPTL_DTO_SIZE, ApplXcpEventCount);

  // Event list
#if defined( XCP_ENABLE_DAQ_EVENT_LIST ) && !defined ( XCP_ENABLE_DAQ_EVENT_INFO )
  for (unsigned int i = 0; i < ApplXcpEventCount; i++) {
	  char shortName[9];
	  strncpy_s(shortName, 9, ApplXcpEventList[i].name, 8);
	  fprintf(gA2lFile, "/begin EVENT \"%s\" \"%s\" 0x%X DAQ 0xFF 0x%X 0x%X 0x00 CONSISTENCY DAQ", ApplXcpEventList[i].name, shortName, i, ApplXcpEventList[i].timeCycle, ApplXcpEventList[i].timeUnit );
#ifdef XCP_ENABLE_PACKED_MODE
	  if (ApplXcpEventList[i].sampleCount!=0) {
		  fprintf(gA2lFile, " /begin DAQ_PACKED_MODE ELEMENT_GROUPED STS_LAST MANDATORY %u /end DAQ_PACKED_MODE",ApplXcpEventList[i].sampleCount);
	  }
#endif
	  fprintf(gA2lFile, " /end EVENT\n");
  }
#endif

fprintf(gA2lFile, gA2lIfData2, transportLayerVersion, getA2lSlavePort(), getA2lSlaveIP());
}


void A2lSetEvent(uint16_t event) {
	gA2lEvent = event;
}


void A2lTypedefBegin_(const char* name, int size, const char* comment) {
	fprintf(gA2lFile,"/begin TYPEDEF_STRUCTURE %s \"%s\" 0x%X SYMBOL_TYPE_LINK \"%s\"\n", name, comment, size, name);
	gA2lTypedefs++;
}

void A2lTypedefComponent_(const char* name, int size, vuint32 offset) {
	fprintf(gA2lFile,"  /begin STRUCTURE_COMPONENT %s %s 0x%X SYMBOL_TYPE_LINK \"%s\" /end STRUCTURE_COMPONENT\n", name, getParType(size), offset, name);
	gA2lComponents++;
}

void A2lTypedefEnd_() {
	fprintf(gA2lFile,"/end TYPEDEF_STRUCTURE\n");
}

void A2lCreateTypedefInstance_(const char* instanceName, const char* typeName, uint32_t addr, const char* comment) {
	fprintf(gA2lFile, "/begin INSTANCE %s \"%s\" %s 0x%X", instanceName, comment, typeName, (unsigned int)addr);
	if (gA2lEvent >= 0) {
		fprintf(gA2lFile, " /begin IF_DATA XCP /begin DAQ_EVENT FIXED_EVENT_LIST EVENT 0x%X /end DAQ_EVENT /end IF_DATA", gA2lEvent);
	}
	fprintf(gA2lFile, " /end INSTANCE\n");
	gA2lInstances++;

}


void A2lCreateMeasurement_(const char* instanceName, const char* name, int size, uint32_t addr, double factor, double offset, const char* unit, const char* comment) {

	if (unit == NULL) unit = "";
	if (comment == NULL) comment = "";
	const char *conv = "NO";
	if (factor != 0.0 || offset != 0.0) {
		fprintf(gA2lFile, "/begin COMPU_METHOD %s_COMPU_METHOD \"\" LINEAR \"%%6.3\" \"%s\" COEFFS_LINEAR %g %g /end COMPU_METHOD\n", name, unit!=NULL?unit:"", factor,offset);
		conv = name;
		gA2lConversions++;
	}
	if (instanceName!=NULL && strlen(instanceName)>0) {
		fprintf(gA2lFile, "/begin MEASUREMENT %s.%s \"%s\" %s %s_COMPU_METHOD 0 0 %s %s ECU_ADDRESS 0x%X", instanceName, name, comment, getMeaType(size), conv, getTypeMin(size), getTypeMax(size), (unsigned int)addr);
	}
	else {
		fprintf(gA2lFile, "/begin MEASUREMENT %s \"%s\" %s %s_COMPU_METHOD 0 0 %s %s ECU_ADDRESS 0x%X", name, comment, getMeaType(size), conv, getTypeMin(size), getTypeMax(size), (unsigned int)addr);
	}
	if (unit != NULL) fprintf(gA2lFile, " PHYS_UNIT \"%s\"", unit);
	if (gA2lEvent >= 0) {
		fprintf(gA2lFile," /begin IF_DATA XCP /begin DAQ_EVENT FIXED_EVENT_LIST EVENT 0x%X /end DAQ_EVENT /end IF_DATA", gA2lEvent);
	}
	fprintf(gA2lFile, " /end MEASUREMENT\n");
	gA2lMeasurements++;
}


void A2lCreateMeasurementArray_(const char* instanceName, const char* name, int size, int dim, uint32_t addr) {

	if (instanceName) {
		fprintf(gA2lFile, "/begin CHARACTERISTIC %s.%s \"\" VAL_BLK 0x%X %s 0 NO_COMPU_METHOD %s %s MATRIX_DIM %u", instanceName, name, (uint32_t)addr, getParType(size), getTypeMin(size), getTypeMax(size), dim);
	}
	else {
		fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"\" VAL_BLK 0x%X %s 0 NO_COMPU_METHOD %s %s MATRIX_DIM %u", name, (unsigned int)addr, getParType(size), getTypeMin(size), getTypeMax(size), dim);
	}
	if (gA2lEvent>=0) {
		fprintf(gA2lFile," /begin IF_DATA XCP /begin DAQ_EVENT FIXED_EVENT_LIST EVENT 0x%X /end DAQ_EVENT /end IF_DATA", gA2lEvent);
	}
	fprintf(gA2lFile, " /end CHARACTERISTIC\n");
	gA2lMeasurements++;
}


void A2lCreateParameterWithLimits_(const char* name, int size, uint32_t addr, const char* comment, const char* unit, double min, double max) {

	fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"%s\" VALUE 0x%X %s 0 NO_COMPU_METHOD %g %g PHYS_UNIT \"%s\" /end CHARACTERISTIC\n",
		name, comment, addr, getParType(size), min, max, unit);
	gA2lParameters++;
}

void A2lCreateParameter_(const char* name, int size, uint32_t addr, const char* comment, const char* unit) {

	fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"%s\" VALUE 0x%X %s 0 NO_COMPU_METHOD %s %s PHYS_UNIT \"%s\" /end CHARACTERISTIC\n",
		name, comment, addr, getParType(size), getTypeMin(size), getTypeMax(size), unit);
	gA2lParameters++;
}

void A2lCreateMap_(const char* name, int size, uint32_t addr, uint32_t xdim, uint32_t ydim, const char* comment, const char* unit) {

	fprintf(gA2lFile, 
		"/begin CHARACTERISTIC %s \"%s\" MAP 0x%X %s 0 NO_COMPU_METHOD %s %s"
		" /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR"
		" /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR"
		" PHYS_UNIT \"%s\" /end CHARACTERISTIC",
		name, comment, addr, getParType(size), getTypeMin(size), getTypeMax(size), xdim, xdim-1, xdim, ydim, ydim-1, ydim, unit);
	gA2lParameters++;
}



void A2lParameterGroup(const char* name, int count, ...) {

	va_list ap;

	fprintf(gA2lFile, "/begin GROUP %s \"\"", name);
	fprintf(gA2lFile, " /begin REF_CHARACTERISTIC\n");
	va_start(ap, count);
	for (int i = 0; i < count; i++) {
		fprintf(gA2lFile, " %s", va_arg(ap, char*));
	}
	va_end(ap);
	fprintf(gA2lFile, "\n/end REF_CHARACTERISTIC ");
	fprintf(gA2lFile, "/end GROUP\n");
}

void A2lMeasurementGroup(const char* name, int count, ...) {

	va_list ap;

	fprintf(gA2lFile, "/begin GROUP %s \"\"", name);
	fprintf(gA2lFile, " /begin REF_MEASUREMENT");
	va_start(ap, count);
	for (int i = 0; i < count; i++) {
		fprintf(gA2lFile, " %s", va_arg(ap, char*));
	}
	va_end(ap);
	fprintf(gA2lFile, " /end REF_MEASUREMENT");
	fprintf(gA2lFile, " /end GROUP\n");
}


void A2lMeasurementGroupFromList(const char *name, const char* names[], unsigned int count) {

	fprintf(gA2lFile, "/begin GROUP %s \"\" \n", name);
	fprintf(gA2lFile, " /begin REF_MEASUREMENT");
	for (unsigned int i1 = 0; i1 < count; i1++) {
		fprintf(gA2lFile, " %s", names[i1]);
	}
	fprintf(gA2lFile, " /end REF_MEASUREMENT");
	fprintf(gA2lFile, "\n/end GROUP\n");
}


void A2lClose(void) {

	// Create standard record layouts for elementary types
	for (int i = -8; i <= +8; i++) {
		const char* t = getMeaType(i);
		if (t != NULL) fprintf(gA2lFile, "/begin RECORD_LAYOUT _%s FNC_VALUES 1 %s ROW_DIR DIRECT /end RECORD_LAYOUT\n", t, t);
	}

	// Create standard typedefs for elementary types
	for (int i = -8; i <= +8; i++) {
		const char* t = getMeaType(i);
		if (t != NULL) fprintf(gA2lFile, "/begin TYPEDEF_MEASUREMENT _%s \"\" %s NO_COMPU_METHOD 0 0 %s %s /end TYPEDEF_MEASUREMENT\n",t,t,getTypeMin(i),getTypeMax(i));
	}

	fprintf(gA2lFile, gA2lFooter);
	fclose(gA2lFile);
	printf("  (%u meas, %u pars, %u typedefs, %u components, %u instances, %u conversions)\n\n",
		gA2lMeasurements, gA2lParameters, gA2lTypedefs, gA2lComponents, gA2lInstances, gA2lConversions);
}


#endif

