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

#include "a2l.h"

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIu64
#include <stdarg.h>   // for va_
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t
#include <stdio.h>    // for fclose, fopen, fread, fseek, ftell
#include <string.h>   // for strlen, strncpy

#include "dbg_print.h"   // for DBG_PRINTF3, DBG_PRINT4, DBG_PRINTF4, DBG...
#include "main_cfg.h"    // for OPTION_xxx
#include "persistence.h" // for XcpBinWrite
#include "platform.h"    // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex
#include "xcp.h"         // for CRC_XXX
#include "xcpLite.h"     // for tXcpDaqLists, XcpXxx, ApplXcpXxx, ...
#include "xcp_cfg.h"     // for XCP_xxx
#include "xcptl_cfg.h"   // for XCPTL_xxx

#ifdef OPTION_ENABLE_A2L_GENERATOR

//----------------------------------------------------------------------------------

#define INCLUDE_AML_FILES // Use /include "file.aml"
// #define EMBED_AML_FILES // Embed AML files into generated A2L file

//----------------------------------------------------------------------------------

// A2L global options
static bool gA2lUseTCP = false;
static uint16_t gA2lOptionPort = 5555;
static uint8_t gA2lOptionBindAddr[4] = {0, 0, 0, 0};
static uint8_t gA2lMode = A2L_MODE_WRITE_ALWAYS | A2L_MODE_FINALIZE_ON_CONNECT | A2L_MODE_AUTO_GROUPS;
static bool gA2lFinalizeOnConnect = true; // Finalize A2L file on connect
static bool gA2lWriteAlways = true;       // Write A2L file always, even if no changes were made
static bool gA2lAutoGroups = true;        // Automatically create groups for measurements and parameters

// A2L file handles and state
static char gA2lFilename[256];
static bool gA2lFileWritten = false;
static FILE *gA2lFile = NULL;
static FILE *gA2lTypedefsFile = NULL;
static FILE *gA2lGroupsFile = NULL;
static FILE *gA2lConversionsFile = NULL;

// Thread safety and one time execution
static MUTEX gA2lMutex;
A2L_ONCE_TYPE gA2lOncePass = 1; // Allows to rerun the once checkers

// Conversion name buffer
static char gA2lConvName[256];

// Auto grouping
static const char *gA2lAutoGroupName = NULL;  // Current open group
static bool gA2lAutoGroupIsParameter = false; // Group is a parameter group

// Addressing mode
static tXcpEventId gA2lFixedEvent = XCP_UNDEFINED_EVENT_ID;
static tXcpEventId gA2lDefaultEvent = XCP_UNDEFINED_EVENT_ID;
static uint8_t gA2lAddrExt = XCP_ADDR_EXT_ABS;           // Address extension (addressing mode, default absolute)
static uint8_t gA2lAutoAddrExt = XCP_UNDEFINED_ADDR_EXT; // Address extension calculated in automatic mode (valid if gA2lAddrExt=XCP_UNDEFINED_ADDR_EXT)
static const uint8_t *gA2lFramePtr = NULL;               // Frame address for rel and dyn mode
static const uint8_t *gA2lBasePtr = NULL;                // Base address for rel and dyn mode
static tXcpCalSegIndex gA2lAddrIndex = 0;                // Segment index for seg mode

// Statistics
static uint32_t gA2lMeasurements;
static uint32_t gA2lParameters;
static uint32_t gA2lTypedefs;
static uint32_t gA2lComponents;
static uint32_t gA2lInstances;
static uint32_t gA2lConversions;

//----------------------------------------------------------------------------------

static bool A2lOpen(void);
static uint32_t A2lGetAddr_(const void *addr);
static uint8_t A2lGetAddrExt_(void);

//----------------------------------------------------------------------------------
static const char *gA2lHeader1 = "ASAP2_VERSION 1 71\n"
                                 "/begin PROJECT %s \"\"\n\n" // project name
                                 "/begin HEADER \"\" VERSION \"1.0\" PROJECT_NO " XCP_ADDRESS_MODE " /end HEADER\n\n"
                                 "/begin MODULE %s \"\"\n\n"; // module name

static const char *gA2lHeader2 = "/begin MOD_COMMON \"\"\n"
                                 "BYTE_ORDER MSB_LAST\n"
                                 "ALIGNMENT_BYTE 1\n"
                                 "ALIGNMENT_WORD 1\n"
                                 "ALIGNMENT_LONG 1\n"
                                 "ALIGNMENT_FLOAT16_IEEE 1\n"
                                 "ALIGNMENT_FLOAT32_IEEE 1\n"
                                 "ALIGNMENT_FLOAT64_IEEE 1\n"
                                 "ALIGNMENT_INT64 1\n"
                                 "/end MOD_COMMON\n"
                                 "\n\n";

//----------------------------------------------------------------------------------
#ifdef EMBED_AML_FILES
static const char *gA2lAml = "";
#endif

//----------------------------------------------------------------------------------
#ifdef XCP_ENABLE_CALSEG_LIST
static const char *gA2lMemorySegment =
#ifdef XCP_ENABLE_CAL_PAGE
    // 2 calibration pages, 0=working page (RAM), 1=initial readonly page (FLASH), independent access to ECU and XCP page possible using GET/SET_CAL_PAGE
    // DATA = program data allowed for online calibration
    "/begin MEMORY_SEGMENT %s \"\" DATA FLASH INTERN 0x%08X %u -1 -1 -1 -1 -1\n" // name, start addr, size
    "/begin IF_DATA XCP\n"
    "  /begin SEGMENT %u 2 0 0 0\n" // index
    "  /begin CHECKSUM XCP_CRC_16_CITT MAX_BLOCK_SIZE 0xFFFF EXTERNAL_FUNCTION \"\" /end CHECKSUM\n"
    "  /begin PAGE 0 ECU_ACCESS_DONT_CARE XCP_READ_ACCESS_DONT_CARE XCP_WRITE_ACCESS_DONT_CARE /end PAGE\n"
    "  /begin PAGE 1 ECU_ACCESS_DONT_CARE XCP_READ_ACCESS_DONT_CARE XCP_WRITE_ACCESS_NOT_ALLOWED /end PAGE\n"
    "  /end SEGMENT\n"
    "/end IF_DATA\n"
#else
    // 1 calibration page
    "/begin MEMORY_SEGMENT %s \"\" DATA RAM INTERN 0x%08X %u -1 -1 -1 -1 -1\n" // name, start addr, size
    "/begin IF_DATA XCP\n"
    "  /begin SEGMENT %u 1 0 0 0\n" // index
    "  /begin CHECKSUM XCP_CRC_16_CITT MAX_BLOCK_SIZE 0xFFFF EXTERNAL_FUNCTION \"\" /end CHECKSUM\n"
    "  /begin PAGE 0 ECU_ACCESS_DONT_CARE XCP_READ_ACCESS_WITH_ECU_ONLY XCP_WRITE_ACCESS_WITH_ECU_ONLY /end PAGE\n"
    "  /end SEGMENT\n"
    "/end IF_DATA\n"
#endif
#ifdef OPTION_CAL_SEGMENTS_ABS
    "/begin IF_DATA CANAPE_ADDRESS_UPDATE\n"
    "/begin MEMORY_SEGMENT \"%s\" FIRST \"%s\" 0 LAST \"%s\" %u /end MEMORY_SEGMENT\n"
    "/end IF_DATA\n"
#endif
    "/end MEMORY_SEGMENT\n";

#endif

//----------------------------------------------------------------------------------
static const char *const gA2lIfDataBegin = "\n/begin IF_DATA XCP\n";

//----------------------------------------------------------------------------------
static const char *gA2lIfDataProtocolLayer = // Parameter: XCP_PROTOCOL_LAYER_VERSION, MAX_CTO, MAX_DTO
    "/begin PROTOCOL_LAYER\n"
    " 0x%X"                                          // XCP_PROTOCOL_LAYER_VERSION
    " 1000 2000 0 0 0 0 0"                           // Timeouts T1-T7
    " %u %u "                                        // MAX_CTO, MAX_DTO
    "BYTE_ORDER_MSB_LAST ADDRESS_GRANULARITY_BYTE\n" // Intel and BYTE pointers
    "OPTIONAL_CMD GET_COMM_MODE_INFO\n"              // Optional commands
    "OPTIONAL_CMD GET_ID\n"
    "OPTIONAL_CMD SET_REQUEST\n"
    "OPTIONAL_CMD SET_MTA\n"
    "OPTIONAL_CMD UPLOAD\n"
    "OPTIONAL_CMD SHORT_UPLOAD\n"
    "OPTIONAL_CMD DOWNLOAD\n"
    "OPTIONAL_CMD SHORT_DOWNLOAD\n"
#ifdef XCP_ENABLE_CAL_PAGE
    "OPTIONAL_CMD GET_CAL_PAGE\n"
    "OPTIONAL_CMD SET_CAL_PAGE\n"
#ifdef XCP_ENABLE_COPY_CAL_PAGE
    "OPTIONAL_CMD COPY_CAL_PAGE\n"
#endif
#endif // XCP_ENABLE_CAL_PAGE
#ifdef XCP_ENABLE_CALSEG_LIST
    "OPTIONAL_CMD GET_PAG_PROCESSOR_INFO\n"
    "OPTIONAL_CMD GET_SEGMENT_INFO\n"
    "OPTIONAL_CMD GET_PAGE_INFO\n"
#ifdef XCP_ENABLE_FREEZE_CAL_PAGE
    "OPTIONAL_CMD GET_SEGMENT_MODE\n"
    "OPTIONAL_CMD SET_SEGMENT_MODE\n"
#endif // XCP_ENABLE_FREEZE_CAL_PAGE
#endif // XCP_ENABLE_CALSEG_LIST
#ifdef XCP_ENABLE_CHECKSUM
    "OPTIONAL_CMD BUILD_CHECKSUM\n"
#endif
    //"OPTIONAL_CMD TRANSPORT_LAYER_CMD\n"
    "OPTIONAL_CMD USER_CMD\n"
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
static const char *gA2lIfDataBeginDAQ = // Parameter: %u max event, %s timestamp unit
    "/begin DAQ\n"
    "DYNAMIC 0 %u 0 OPTIMISATION_TYPE_DEFAULT ADDRESS_EXTENSION_FREE IDENTIFICATION_FIELD_TYPE_RELATIVE_BYTE GRANULARITY_ODT_ENTRY_SIZE_DAQ_BYTE 0xF8 OVERLOAD_INDICATION_PID"
#ifdef XCP_ENABLE_DAQ_PRESCALER
    " PRESCALER_SUPPORTED"
#endif
    "\n/begin TIMESTAMP_SUPPORTED 0x1 SIZE_DWORD %s TIMESTAMP_FIXED /end TIMESTAMP_SUPPORTED\n";

// ... Event list follows, before EndDaq

static const char *const gA2lIfDataEndDAQ = "/end DAQ\n";

//----------------------------------------------------------------------------------
// XCP_ON_ETH
static const char *gA2lIfDataEth = // Parameter: %s TCP or UDP, %04X tl version, %u port, %s ip address string, %s TCP or UDP
    "/begin XCP_ON_%s_IP\n"        // Transport Layer
    "  0x%X %u ADDRESS \"%s\"\n"
//"OPTIONAL_TL_SUBCMD GET_SERVER_ID\n"
//"OPTIONAL_TL_SUBCMD GET_DAQ_ID\n"
//"OPTIONAL_TL_SUBCMD SET_DAQ_ID\n"
#if defined(XCPTL_ENABLE_MULTICAST) && defined(XCP_ENABLE_DAQ_CLOCK_MULTICAST)
    "  OPTIONAL_TL_SUBCMD GET_DAQ_CLOCK_MULTICAST\n"
#endif
    "/end XCP_ON_%s_IP\n" // Transport Layer
    ;

//----------------------------------------------------------------------------------
static const char *const gA2lIfDataEnd = "/end IF_DATA\n\n";

//----------------------------------------------------------------------------------
static const char *const gA2lFooter = "/end MODULE\n"
                                      "/end PROJECT\n";

//----------------------------------------------------------------------------------

#define printAddrExt(ext)                                                                                                                                                          \
    if (ext > 0)                                                                                                                                                                   \
        fprintf(gA2lFile, " ECU_ADDRESS_EXTENSION %u", ext);

const char *A2lGetSymbolName(const char *instance_name, const char *name) {
    static char s[256];
    if (instance_name != NULL && strlen(instance_name) > 0) {
        SNPRINTF(s, 256, "%s.%s", instance_name, name);
        return s;
    } else {
        return name;
    }
}

const char *A2lGetA2lTypeName(tA2lTypeId type) {
    switch (type) {
    case A2L_TYPE_INT8:
        return "SBYTE";
    case A2L_TYPE_INT16:
        return "SWORD";
    case A2L_TYPE_INT32:
        return "SLONG";
    case A2L_TYPE_INT64:
        return "A_INT64";
    case A2L_TYPE_UINT8:
        return "UBYTE";
    case A2L_TYPE_UINT16:
        return "UWORD";
    case A2L_TYPE_UINT32:
        return "ULONG";
    case A2L_TYPE_UINT64:
        return "A_UINT64";
    case A2L_TYPE_FLOAT:
        return "FLOAT32_IEEE";
    case A2L_TYPE_DOUBLE:
        return "FLOAT64_IEEE";
    default:
        assert(0);
        return NULL;
    }
}

const char *A2lGetA2lTypeName_M(tA2lTypeId type) {
    switch (type) {
    case A2L_TYPE_INT8:
        return "M_I8";
    case A2L_TYPE_INT16:
        return "M_I16";
    case A2L_TYPE_INT32:
        return "M_I32";
    case A2L_TYPE_INT64:
        return "M_I64";
    case A2L_TYPE_UINT8:
        return "M_U8";
    case A2L_TYPE_UINT16:
        return "M_U16";
    case A2L_TYPE_UINT32:
        return "M_U32";
    case A2L_TYPE_UINT64:
        return "M_U64";
    case A2L_TYPE_FLOAT:
        return "M_F32";
    case A2L_TYPE_DOUBLE:
        return "M_F64";
    default:
        assert(0);
        return NULL;
    }
}

const char *A2lGetA2lTypeName_C(tA2lTypeId type) {
    switch (type) {
    case A2L_TYPE_INT8:
        return "C_I8";
    case A2L_TYPE_INT16:
        return "C_I16";
    case A2L_TYPE_INT32:
        return "C_I32";
    case A2L_TYPE_INT64:
        return "C_I64";
    case A2L_TYPE_UINT8:
        return "C_U8";
    case A2L_TYPE_UINT16:
        return "C_U16";
    case A2L_TYPE_UINT32:
        return "C_U32";
    case A2L_TYPE_UINT64:
        return "C_U64";
    case A2L_TYPE_FLOAT:
        return "C_F32";
    case A2L_TYPE_DOUBLE:
        return "C_F64";
    default:
        assert(0);
        return NULL;
    }
}

const char *A2lGetRecordLayoutName_(tA2lTypeId type) {
    switch (type) {
    case A2L_TYPE_INT8:
        return "I8";
    case A2L_TYPE_INT16:
        return "I16";
    case A2L_TYPE_INT32:
        return "I32";
    case A2L_TYPE_INT64:
        return "I64";
    case A2L_TYPE_UINT8:
        return "U8";
    case A2L_TYPE_UINT16:
        return "U16";
    case A2L_TYPE_UINT32:
        return "U32";
    case A2L_TYPE_UINT64:
        return "U64";
    case A2L_TYPE_FLOAT:
        return "F32";
    case A2L_TYPE_DOUBLE:
        return "F64";
    default:
        assert(0);
        return NULL;
    }
}

static double getTypeMin(tA2lTypeId type) {
    double min;
    switch (type) {
    case A2L_TYPE_INT8:
        min = -128;
        break;
    case A2L_TYPE_INT16:
        min = -32768;
        break;
    case A2L_TYPE_INT32:
        min = -2147483647 - 1;
        break;
    case A2L_TYPE_INT64:
        min = -1e12;
        break;
    case A2L_TYPE_FLOAT:
        min = -1e12;
        break;
    case A2L_TYPE_DOUBLE:
        min = -1e12;
        break;
    default:
        min = 0;
    }
    return min;
}

static double getTypeMax(tA2lTypeId type) {
    double max;
    switch (type) {
    case A2L_TYPE_INT8:
        max = 127;
        break;
    case A2L_TYPE_INT16:
        max = 32767;
        break;
    case A2L_TYPE_INT32:
        max = 2147483647;
        break;
    case A2L_TYPE_UINT8:
        max = 255;
        break;
    case A2L_TYPE_UINT16:
        max = 65535;
        break;
    case A2L_TYPE_UINT32:
        max = 4294967295;
        break;
    default:
        max = 1e12;
    }
    return max;
}

// Start A2L file generation
// Filename gA2lFilename
static bool A2lOpen(void) {

    gA2lFile = NULL;
    gA2lTypedefsFile = NULL;
    gA2lGroupsFile = NULL;
    gA2lConversionsFile = NULL;

    A2lRstAddrMode();

    gA2lMeasurements = gA2lParameters = gA2lTypedefs = gA2lInstances = gA2lConversions = gA2lComponents = 0;

    // Start A2L generator
    DBG_PRINTF3("Start A2L generator, file=%s, write_always=%u, finalize_on_connect=%u, auto_groups=%u\n", gA2lFilename, gA2lWriteAlways, gA2lFinalizeOnConnect, gA2lAutoGroups);
    if (fexists(gA2lFilename)) {
        DBG_PRINTF_WARNING("A2L filename %s already exists!\n", gA2lFilename);
    }
    gA2lFile = fopen(gA2lFilename, "w");
    gA2lTypedefsFile = fopen("typedefs.a2l", "w");
    gA2lGroupsFile = fopen("groups.a2l", "w");
    gA2lConversionsFile = fopen("conversions.a2l", "w");
    if (gA2lFile == 0 || gA2lTypedefsFile == 0 || gA2lGroupsFile == 0 || gA2lConversionsFile == 0) {
        DBG_PRINT_ERROR("Could not create A2L file!\n");
        return false;
    }

    // Create headers
    fprintf(gA2lFile, gA2lHeader1, XcpGetProjectName(), XcpGetProjectName());
#ifdef INCLUDE_AML_FILES
    // To include multiple AML files, remove the /begin A2ML and /end A2LM in the XCP_104.aml and CANape.aml files and uncomment the following lines
    // fprintf(gA2lFile,"/begin A2ML\n"
    // "/include \"XCP_104.aml\"\n\n"
    // "/include \"CANape.aml\"\n\n"
    // "/end A2ML\n");
    fprintf(gA2lFile, "/include \"XCP_104.aml\"\n\n");
#endif
#ifdef EMBED_AML_FILES
    fprintf(gA2lFile, gA2lAml); // main file
#endif
    fprintf(gA2lFile, "%s", gA2lHeader2);

    fprintf(gA2lTypedefsFile, "\n/* Typedefs */\n");       // typedefs temporary file
    fprintf(gA2lGroupsFile, "\n/* Groups */\n");           // groups temporary file
    fprintf(gA2lConversionsFile, "\n/* Conversions */\n"); // conversions temporary file

    // Create predefined conversions
    // In the conversions.a2l file - will be merges later as there might be more conversions during the generation process
    fprintf(gA2lConversionsFile, "/begin COMPU_METHOD conv.bool \"\" TAB_VERB \"%%.0\" \"\" COMPU_TAB_REF conv.bool.table /end COMPU_METHOD\n");
    fprintf(gA2lConversionsFile, "/begin COMPU_VTAB conv.bool.table \"\" TAB_VERB 2 0 \"false\" 1 \"true\" /end COMPU_VTAB\n");

    // Create predefined standard record layouts and typedefs for elementary types
    // In the typedefs.a2l file - will be merges later as there might be more typedefs during the generation process
    tA2lTypeId typeid_table[] = {A2L_TYPE_UINT8, A2L_TYPE_UINT16, A2L_TYPE_UINT32, A2L_TYPE_UINT64, A2L_TYPE_INT8,
                                 A2L_TYPE_INT16, A2L_TYPE_INT32,  A2L_TYPE_INT64,  A2L_TYPE_FLOAT,  A2L_TYPE_DOUBLE};
    for (size_t i = 0; i < sizeof(typeid_table); i++) {
        tA2lTypeId a2l_type_id = typeid_table[i];
        const char *a2l_type_name = A2lGetA2lTypeName(a2l_type_id);
        assert(a2l_type_name != NULL);
        const char *a2l_record_layout_name = A2lGetRecordLayoutName_(a2l_type_id);
        assert(a2l_record_layout_name != NULL);
        // RECORD_LAYOUTs for standard types U8,I8,...,F64 (Position 1 increasing index)
        // Example: /begin RECORD_LAYOUT U64 FNC_VALUES 1 A_UINT64 ROW_DIR DIRECT /end RECORD_LAYOUT
        fprintf(gA2lTypedefsFile, "/begin RECORD_LAYOUT %s FNC_VALUES 1 %s ROW_DIR DIRECT /end RECORD_LAYOUT\n", a2l_record_layout_name, a2l_type_name);
        // RECORD_LAYOUTs for axis points with standard types A_U8,A_I8,... (Positionn 1 increasing index)
        // Example: /begin RECORD_LAYOUT A_F32 AXIS_PTS_X 1 FLOAT32_IEEE INDEX_INCR DIRECT /end RECORD_LAYOUT
        fprintf(gA2lTypedefsFile, "/begin RECORD_LAYOUT A_%s AXIS_PTS_X 1 %s INDEX_INCR DIRECT /end RECORD_LAYOUT\n", a2l_record_layout_name, a2l_type_name);
        // Example: /begin TYPEDEF_MEASUREMENT M_F64 "" FLOAT64_IEEE NO_COMPU_METHOD 0 0 -1e12 1e12 /end TYPEDEF_MEASUREMENT
        const char *format_str =
            (a2l_type_id == A2L_TYPE_FLOAT || a2l_type_id == A2L_TYPE_DOUBLE)
                ? "/begin TYPEDEF_MEASUREMENT M_%s \"\" %s NO_COMPU_METHOD 0 0 %g %g /end TYPEDEF_MEASUREMENT\n"
                : "/begin TYPEDEF_MEASUREMENT M_%s \"\" %s NO_COMPU_METHOD 0 0 %.0f %.0f /end TYPEDEF_MEASUREMENT\n"; // Avoid exponential format for integer types
        fprintf(gA2lTypedefsFile, format_str, a2l_record_layout_name, a2l_type_name, getTypeMin(a2l_type_id), getTypeMax(a2l_type_id));
        // Example: /begin TYPEDEF_CHARACTERISTIC C_U8 "" VALUE U8 0 NO_COMPU_METHOD 0 255 /end TYPEDEF_CHARACTERISTIC
        fprintf(gA2lTypedefsFile, "/begin TYPEDEF_CHARACTERISTIC C_%s \"\" VALUE %s 0 NO_COMPU_METHOD %g %g /end TYPEDEF_CHARACTERISTIC\n", a2l_record_layout_name,
                a2l_record_layout_name, getTypeMin(a2l_type_id), getTypeMax(a2l_type_id));
    }
    fprintf(gA2lTypedefsFile, "\n");

    return true;
}

// Memory segments
static void A2lCreate_MOD_PAR(void) {
    if (gA2lFile != NULL) {

        fprintf(gA2lFile, "\n/begin MOD_PAR \"\"\n");

        // EPK
        const char *epk = XcpGetEpk();
        if (epk) {
            fprintf(gA2lFile, "EPK \"%s\" ADDR_EPK 0x%08X\n", epk, XCP_ADDR_EPK);
        }

        // Memory segments
#ifdef XCP_ENABLE_CALSEG_LIST
        const tXcpCalSegList *calSegList = XcpGetCalSegList();
        if (calSegList != NULL &&
#ifdef OPTION_CAL_SEGMENT_EPK // Don't create calseg the EPK segment if it is the only one
            calSegList->count > 1)
#else
            calSegList->count > 0)
#endif
        {
            for (tXcpCalSegIndex i = 0; i < calSegList->count; i++) {
                const tXcpCalSeg *calseg = &calSegList->calseg[i];
                fprintf(gA2lFile, gA2lMemorySegment, calseg->name, XcpGetCalSegBaseAddress(i), calseg->size, i, calseg->name, calseg->name, calseg->name, calseg->size);
            }
        }
#endif // XCP_ENABLE_CALSEG_LIST

        fprintf(gA2lFile, "/end MOD_PAR\n\n");
    }
}

static void A2lCreate_IF_DATA_DAQ(void) {

#if defined(XCP_ENABLE_DAQ_EVENT_LIST)
    tXcpEventList *eventList = NULL;
#endif
    uint16_t eventCount = 0;

#if (XCP_TIMESTAMP_UNIT == DAQ_TIMESTAMP_UNIT_1NS)
#define XCP_TIMESTAMP_UNIT_S "UNIT_1NS"
#elif (XCP_TIMESTAMP_UNIT == DAQ_TIMESTAMP_UNIT_1US)
#define XCP_TIMESTAMP_UNIT_S "UNIT_1US"
#else
#error
#endif

    // Event list in A2L file (if event info by XCP is not active)
#if defined(XCP_ENABLE_DAQ_EVENT_LIST)
    eventCount = XcpGetEventCount();
    eventList = XcpGetEventList();
#endif

    fprintf(gA2lFile, gA2lIfDataBeginDAQ, eventCount, XCP_TIMESTAMP_UNIT_S);

    // Eventlist
#if defined(XCP_ENABLE_DAQ_EVENT_LIST)
    for (uint32_t id = 0; id < eventCount; id++) {
        tXcpEvent *event = &eventList->event[id];

        // Convert cycle time to ASAM XCP IF_DATA coding time cycle and time unit
        // RESOLUTION OF TIMESTAMP "UNIT_1NS" = 0, "UNIT_10NS" = 1, ...
        uint8_t timeUnit = 0;                    // timeCycle unit, 1ns=0, 10ns=1, 100ns=2, 1us=3, ..., 1ms=6, ...
        uint32_t timeCycle = event->cycleTimeNs; // cycle time in units, 0 = sporadic or unknown
        while (timeCycle >= 256) {
            timeCycle /= 10;
            timeUnit++;
        }

        // @@@@ TODO: Clarify
        // Short event names (the second name) must not be unique, but <= 8 characters ?
        fprintf(gA2lFile, "/begin EVENT \"%s\" \"%.8s\" 0x%X DAQ 0xFF %u %u %u CONSISTENCY EVENT", XcpGetEventName(id), XcpGetEventName(id), id, timeCycle, timeUnit,
                (event->flags & XCP_DAQ_EVENT_FLAG_PRIORITY) ? 0xFF : 0x00);

        fprintf(gA2lFile, " /end EVENT\n");
    }
#endif

    fprintf(gA2lFile, gA2lIfDataEndDAQ);
}

static void A2lCreate_ETH_IF_DATA(bool useTCP, const uint8_t *addr, uint16_t port) {
    if (gA2lFile != NULL) {

        fprintf(gA2lFile, gA2lIfDataBegin);

        // Protocol Layer info
        fprintf(gA2lFile, gA2lIfDataProtocolLayer, XCP_PROTOCOL_LAYER_VERSION, XCPTL_MAX_CTO_SIZE, XCPTL_MAX_DTO_SIZE);

        // DAQ info
        A2lCreate_IF_DATA_DAQ();

        // Transport Layer info (protocol, address, port)
        // Skip transport layer info completely, if no valid address is configured or detected
        // @@@@ (protocol, port, 0.0.0.0) is no option, as CANape considers this to be a valid address and tries to connect to it, instead of using the user configured address
        uint8_t addr0[] = {0, 0, 0, 0};
        if (addr != NULL && addr[0] != 0) {
            memcpy(addr0, addr, 4);
        }
#ifdef OPTION_ENABLE_GET_LOCAL_ADDR
        else {
            socketGetLocalAddr(NULL, addr0);
        }
#endif
        if (addr0[0] != 0) {
            char addrs[17];
            SPRINTF(addrs, "%u.%u.%u.%u", addr0[0], addr0[1], addr0[2], addr0[3]);
            char *prot = useTCP ? (char *)"TCP" : (char *)"UDP";
            fprintf(gA2lFile, gA2lIfDataEth, prot, XCP_TRANSPORT_LAYER_VERSION, port, addrs, prot);
            DBG_PRINTF3("A2L IF_DATA XCP_ON_%s, ip=%s, port=%u\n", prot, addrs, port);
        }
        fprintf(gA2lFile, gA2lIfDataEnd);
    }
}

static void A2lCreateMeasurement_IF_DATA(void) {
    if (gA2lFile != NULL) {

        uint8_t addr_ext = A2lGetAddrExt_();
        if (XcpAddrIsDyn(addr_ext)
#ifdef XCP_ENABLE_REL_ADDRESSING
            || XcpAddrIsRel(addr_ext)
#endif

        ) {
            if (gA2lFixedEvent != XCP_UNDEFINED_EVENT_ID) {
                fprintf(gA2lFile, " /begin IF_DATA XCP /begin DAQ_EVENT FIXED_EVENT_LIST EVENT 0x%X /end DAQ_EVENT /end IF_DATA", gA2lFixedEvent);
            } else {
                assert(false); // Fixed event must be set before calling this function
            }
        } else if (XcpAddrIsAbs(addr_ext)) {
            if (gA2lFixedEvent != XCP_UNDEFINED_EVENT_ID) {
                fprintf(gA2lFile, " /begin IF_DATA XCP /begin DAQ_EVENT FIXED_EVENT_LIST EVENT 0x%X /end DAQ_EVENT /end IF_DATA", gA2lFixedEvent);
            } else if (gA2lDefaultEvent != XCP_UNDEFINED_EVENT_ID) {
                fprintf(gA2lFile, " /begin IF_DATA XCP /begin DAQ_EVENT VARIABLE /begin DEFAULT_EVENT_LIST EVENT 0x%X /end DEFAULT_EVENT_LIST /end DAQ_EVENT /end IF_DATA",
                        gA2lDefaultEvent);
            }
        }
    }
}

//----------------------------------------------------------------------------------
// Raw functions to set addressing mode unchecked (by calibration segment index or event id)

// Debug print address and address extension (adressing mode, event, offset)
#if defined(OPTION_ENABLE_DBG_PRINTS) && (OPTION_MAX_DBG_LEVEL >= 4)
static const char *dbgPrintfAddrExt(uint8_t addr_ext, uint32_t addr) {
    static char buf1[64] = {0};
    static char buf2[64] = {0};
    const char *addr_str = NULL;

#ifdef XCP_ENABLE_ABS_ADDRESSING
    if (XcpAddrIsAbs(addr_ext)) {
        addr_str = "ABS";
    } else
#endif
#ifdef XCP_ENABLE_CALSEG_LIST
        if (XcpAddrIsSeg(addr_ext)) {
        addr_str = "SEG";
    } else
#endif
#ifdef XCP_ENABLE_DYN_ADDRESSING
        if (XcpAddrIsDyn(addr_ext)) {
        addr_str = buf2;
        SNPRINTF(buf2, 64, "DYN%u(event=%u,offset=%d)", addr_ext - XCP_ADDR_EXT_DYN, XcpAddrDecodeDynEvent(addr), XcpAddrDecodeDynOffset(addr));

    } else
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
        if (XcpAddrIsRel(addr_ext)) {
        addr_str = "REL";
    } else
#endif
    {
        SNPRINTF(buf1, 64, "%u:0x%08X", addr_ext, addr);
        return buf1;
    }
    SNPRINTF(buf1, 64, "%.32s:0x%08X", addr_str, addr);
    return buf1;
}
#endif

// Calibration segment addressing mode
// Used for calibration parameters ins a XCP calibration segment (A2L MEMORY_SEGMENT)
#if defined(XCP_ENABLE_CALSEG_LIST)
void A2lSetSegAddrMode(tXcpCalSegIndex calseg_index, const uint8_t *calseg_instance_addr) {
    gA2lAddrIndex = calseg_index;
    gA2lBasePtr = calseg_instance_addr; // Address of the calibration segment instance which is used in the macros to create the components
    gA2lAddrExt = XCP_ADDR_EXT_SEG;
}
#endif

// Absolute addressing mode
// XCP address is the absolute address of the variable relative to the main module load address
void A2lSetAbsAddrMode(tXcpEventId default_event_id) {
    gA2lFixedEvent = XCP_UNDEFINED_EVENT_ID;
    gA2lDefaultEvent = default_event_id; // May be XCP_UNDEFINED_EVENT_ID
    gA2lFramePtr = NULL;
    gA2lBasePtr = NULL;
    gA2lAddrExt = XCP_ADDR_EXT_ABS;
}

// Relative addressing mode
// Used for accessing stack variables relative to the stack frame pointer
#ifdef XCP_ENABLE_REL_ADDRESSING
void A2lSetRelAddrMode(tXcpEventId event_id, const uint8_t *base_ptr) {
    gA2lBasePtr = base_ptr;
    gA2lFramePtr = NULL;
    gA2lFixedEvent = event_id;
    gA2lDefaultEvent = XCP_UNDEFINED_EVENT_ID;
    gA2lAddrExt = XCP_ADDR_EXT_REL;
}
#endif

// Dynamic addressing mode
// Relative address, used for heap and class members
// Enables XCP polling access
void A2lSetDynAddrMode(tXcpEventId event_id, uint8_t i, const uint8_t *base_ptr) {
    gA2lBasePtr = base_ptr;
    gA2lFramePtr = NULL;
    gA2lFixedEvent = event_id;
    gA2lDefaultEvent = XCP_UNDEFINED_EVENT_ID;
    gA2lAddrExt = XCP_ADDR_EXT_DYN + i;
    assert(gA2lAddrExt <= XCP_ADDR_EXT_DYN_MAX);
}

// Automatic addressing mode
// A2lGetAddr and A2lGetAddrExt will determine the addressing mode at runtime
// Based on heuristic whether the variable address is within the stack frame or not
void A2lSetAutoAddrMode(tXcpEventId event_id, const uint8_t *frame_ptr, const uint8_t *base_ptr) {
    gA2lFramePtr = frame_ptr;
    gA2lBasePtr = base_ptr;
    gA2lFixedEvent = event_id;
    gA2lDefaultEvent = XCP_UNDEFINED_EVENT_ID;
    gA2lAddrExt = XCP_UNDEFINED_ADDR_EXT; // Auto
}

void A2lRstAddrMode(void) {
    gA2lFixedEvent = XCP_UNDEFINED_EVENT_ID;
    gA2lDefaultEvent = XCP_UNDEFINED_EVENT_ID;
    gA2lFramePtr = NULL;
    gA2lBasePtr = NULL;
    gA2lAddrIndex = 0;
    gA2lAddrExt = XCP_ADDR_EXT_ABS;
    gA2lAutoAddrExt = XCP_UNDEFINED_ADDR_EXT;
}

//----------------------------------------------------------------------------------
// Set addressing mode (by event name or calibration segment index lookup and runtime check)

#ifdef XCP_ENABLE_CALSEG_LIST

// Set relative address mode with calibration segment index
void A2lSetSegmentAddrMode__i(tXcpCalSegIndex calseg_index, const uint8_t *calseg_instance_addr) {
    if (gA2lFile != NULL) {
        const tXcpCalSeg *calseg = XcpGetCalSeg(calseg_index);
        if (calseg == NULL) {
            DBG_PRINTF_ERROR("SetSegAddrMode: Calibration segment %u not found!\n", calseg_index);
            return;
        }
#if XCP_ADDR_EXT_SEG == 0x00
        A2lSetSegAddrMode(calseg_index, (const uint8_t *)calseg_instance_addr);
        // fprintf(gA2lFile, "\n/* Segment relative addressing mode: calseg=%s */\n", calseg->name);
#else
        A2lSetAbsAddrMode(XCP_UNDEFINED_EVENT_ID);
        // fprintf(gA2lFile, "\n/* Absolute segment addressing mode: calseg=%s */\n", calseg->name);
#endif
        if (gA2lAutoGroups) {
            A2lBeginGroup(calseg->name, "Calibration Segment", true);
        }
    }
}

// Set segment relative address mode with calibration segment name
void A2lSetSegmentAddrMode__s(const char *calseg_name, const uint8_t *calseg_instance_addr) {
    if (gA2lFile != NULL) {
        tXcpCalSegIndex calseg_index = XcpFindCalSeg(calseg_name);
        if (calseg_index == XCP_UNDEFINED_CALSEG) {
            DBG_PRINTF_ERROR("SetSegAddrMode: Calibration segment %s not found!\n", calseg_name);
            return;
        }
        const tXcpCalSeg *calseg = XcpGetCalSeg(calseg_index);
        if (calseg == NULL) {
            DBG_PRINTF_ERROR("SetSegAddrMode: Calibration segment %u not found!\n", calseg_index);
            return;
        }
#if XCP_ADDR_EXT_SEG == 0x00
        A2lSetSegAddrMode(calseg_index, (const uint8_t *)calseg_instance_addr);
        // fprintf(gA2lFile, "\n/* Segment relative addressing mode: calseg=%s */\n", calseg->name);
#else
        A2lSetAbsAddrMode(XCP_UNDEFINED_EVENT_ID);
        // fprintf(gA2lFile, "\n/* Absolute segment addressing mode: calseg=%s */\n", calseg->name);
#endif
        if (gA2lAutoGroups) {
            A2lBeginGroup(calseg->name, "Calibration Segment", true);
        }
    }
}

#endif

#ifdef XCP_ENABLE_DAQ_EVENT_LIST

static void beginEventGroup(tXcpEventId event_id) {
    if (gA2lAutoGroups) {
        A2lBeginGroup(XcpGetEventName(event_id), "Measurement event group", false);
    }
}

// Set autodetect address mode with event name or event id
// Will result in using (ADDR_EXT_DYN+0) with stackframe pointer or (ADDR_EXT_DYN+1) user defined base addresses selected when a variable is created
void A2lSetAutoAddrMode__s(const char *event_name, const uint8_t *stack_frame, const uint8_t *base_addr) {
    if (gA2lFile != NULL) {
        tXcpEventId event_id = XcpFindEvent(event_name, NULL);
        if (event_id == XCP_UNDEFINED_EVENT_ID) {
            DBG_PRINTF_ERROR("A2lSetAutoAddrMode__s: Event %s not found!\n", event_name);
            A2lRstAddrMode();
            return;
        }
        A2lSetAutoAddrMode(event_id, stack_frame, base_addr);
        beginEventGroup(event_id);
        // fprintf(gA2lFile, "\n/* Auto addressing mode: event=%s (%u) */\n", event_name, event_id);
    }
}
void A2lSetAutoAddrMode__i(tXcpEventId event_id, const uint8_t *stack_frame, const uint8_t *base_addr) {
    if (gA2lFile != NULL) {
        const char *event_name = XcpGetEventName(event_id);
        if (event_name == NULL) {
            DBG_PRINTF_ERROR("A2lSetAutoAddrMode__i: Event %s not found!\n", event_name);
            A2lRstAddrMode();
            return;
        }
        A2lSetAutoAddrMode(event_id, stack_frame, base_addr);
        beginEventGroup(event_id);
        // fprintf(gA2lFile, "\n/* Auto addressing mode: event=%s (%u) */\n", event_name, event_id);
    }
}

// Set relative address mode with event name or event id
// Will result in using (ADDR_EXT_DYN+i) with user defined base addresses
void A2lSetRelativeAddrMode__s(const char *event_name, uint8_t i, const uint8_t *base_addr) {
    if (gA2lFile != NULL) {
        tXcpEventId event_id = XcpFindEvent(event_name, NULL);
        if (event_id == XCP_UNDEFINED_EVENT_ID) {
            DBG_PRINTF_ERROR("A2lSetRelativeAddrMode__s: Event %s not found!\n", event_name);
            A2lRstAddrMode();
            return;
        }
        A2lSetDynAddrMode(event_id, i, (uint8_t *)base_addr);
        beginEventGroup(event_id);
        // fprintf(gA2lFile, "\n/* Relative addressing mode: event=%s (%u), addr_ext=%u */\n", event_name, event_id, A2lGetAddrExt_());
    }
}
void A2lSetRelativeAddrMode__i(tXcpEventId event_id, uint8_t i, const uint8_t *base_addr) {
    if (gA2lFile != NULL) {
        const char *event_name = XcpGetEventName(event_id);
        if (event_name == NULL) {
            DBG_PRINTF_ERROR("A2lSetRelativeAddrMode__i: Event %s not found!\n", event_name);
            A2lRstAddrMode();
            return;
        }
        A2lSetDynAddrMode(event_id, i, (uint8_t *)base_addr);
        beginEventGroup(event_id);
        // fprintf(gA2lFile, "\n/* Relative addressing mode: event=%s (%u), addr_ext=%u */\n", event_name, event_id, A2lGetAddrExt_());
    }
}

// Set stack frame relative address mode with event name or event id
// Will result in using (ADDR_EXT_DYN+0) with stack frame pointer as base address
void A2lSetStackAddrMode__s(const char *event_name, const uint8_t *stack_frame) {
    if (gA2lFile != NULL) {
        tXcpEventId event_id = XcpFindEvent(event_name, NULL);
        if (event_id == XCP_UNDEFINED_EVENT_ID) {
            DBG_PRINTF_ERROR("A2lSetRelativeAddrMode__s: Event %s not found!\n", event_name);
            A2lRstAddrMode();
            return;
        }
        A2lSetDynAddrMode(event_id, 0, stack_frame);
        beginEventGroup(event_id);
        // fprintf(gA2lFile, "\n/* Stack frame relative addressing mode: event=%s (%u), addr_ext=%u */\n", event_name, event_id, A2lGetAddrExt_());
    }
}
void A2lSetStackAddrMode__i(tXcpEventId event_id, const uint8_t *stack_frame) {
    if (gA2lFile != NULL) {
        const char *event_name = XcpGetEventName(event_id);
        if (event_name == NULL) {
            DBG_PRINTF_ERROR("A2lSetRelativeAddrMode__i: Event %s not found!\n", event_name);
            A2lRstAddrMode();
            return;
        }
        A2lSetDynAddrMode(event_id, 0, stack_frame);
        beginEventGroup(event_id);
        // fprintf(gA2lFile, "\n/* Stack frame relative addressing mode: event=%s (%u), addr_ext=%u */\n", event_name, event_id, A2lGetAddrExt_());
    }
}

// Set absolute address mode with with default event name or event id (optional)
void A2lSetAbsoluteAddrMode__s(const char *event_name) {
    if (gA2lFile != NULL) {
        tXcpEventId event_id = XcpFindEvent(event_name, NULL);
        A2lSetAbsAddrMode(event_id);
        if (event_id != XCP_UNDEFINED_EVENT_ID) {
            beginEventGroup(event_id);
            // fprintf(gA2lFile, "\n/* Absolute addressing mode: default_event=%s (%u), addr_ext=%u */\n", event_name, event_id, A2lGetAddrExt_());
        }
    }
}
void A2lSetAbsoluteAddrMode__i(tXcpEventId event_id) {
    if (gA2lFile != NULL) {
        const char *event_name = XcpGetEventName(event_id);
        assert(event_name != NULL || event_id == XCP_UNDEFINED_EVENT_ID);
        A2lSetAbsAddrMode(event_id);
        if (event_id != XCP_UNDEFINED_EVENT_ID) {
            beginEventGroup(event_id);
            // fprintf(gA2lFile, "\n/* Stack frame absolute addressing mode: event=%s (%u), addr_ext=%u */\n", event_name, event_id, A2lGetAddrExt_());
        }
    }
}
#endif

//----------------------------------------------------------------------------------
// Address encoding

// Get the current address extension
// Must be called after A2lGetAddr to get the correct address extension in case of auto addressing mode
static uint8_t A2lGetAddrExt_(void) {

    if (gA2lAddrExt == XCP_UNDEFINED_ADDR_EXT) {
        return gA2lAutoAddrExt;
    }
    return gA2lAddrExt;
}

// Get encoded address for pointer p according to current address extension
// If auto addressing mode (address extension is undefined) is selected, the addressing extension is detected
static uint32_t A2lGetAddr_(const void *p) {

    if (gA2lFile != NULL) {

        if (gA2lAddrExt == XCP_UNDEFINED_ADDR_EXT) {
            uint64_t addr_diff = 0;
            const uint8_t *base_ptr = NULL;
            // If both base pointers are set, auto detect appropriate base pointer and addressing mode
            if (gA2lBasePtr != NULL && gA2lFramePtr != NULL) {
                uint64_t addr_diff1 = (uint64_t)p - (uint64_t)gA2lBasePtr;
                uint64_t addr_high1 = (addr_diff1 >> 16);
                uint64_t addr_diff2 = (uint64_t)p - (uint64_t)gA2lFramePtr;
                uint64_t addr_high2 = (addr_diff2 >> 16);
                DBG_PRINTF5("A2L auto dyn address mode: addr=%p, base1=%p, diff1=%llX, base2=%p, diff2=%llX\n", p, (void *)gA2lBasePtr, (unsigned long long)addr_diff1,
                            (void *)gA2lFramePtr, (unsigned long long)addr_diff2);
                // Prefer positive this, then negative stack, then positive stack
                // Positive base (heap or this relative)
                if (addr_high1 == 0) {
                    base_ptr = gA2lBasePtr;
                    addr_diff = addr_diff1;
                    gA2lAutoAddrExt = XCP_ADDR_EXT_DYN + 1; // Use base pointer addressing mode with index 1
                    DBG_PRINT5("Positive base selected\n");
                }
                // Negative stack (local variables)
                else if (addr_high2 == 0xFFFFFFFFFFFF) {
                    base_ptr = gA2lFramePtr;
                    addr_diff = addr_diff2;
                    gA2lAutoAddrExt = XCP_ADDR_EXT_DYN; // Use frame pointer addressing mode
                    DBG_PRINT5("Negative stack selected\n");
                }
                // Positive stack (function parameters)
                else if (addr_high2 == 0) {
                    base_ptr = gA2lFramePtr;
                    addr_diff = addr_diff2;
                    gA2lAutoAddrExt = XCP_ADDR_EXT_DYN; // Use frame pointer addressing mode
                    DBG_PRINT5("Positive stack selected\n");
                }
                // Negative base
                else if (addr_high1 == 0xFFFFFFFFFFFF) {
                    DBG_PRINTF_ERROR("A2L dyn address overflow detected! negative base offset, addr: %p, base1: %p, base2: %p\n", p, (void *)gA2lBasePtr, (void *)gA2lFramePtr);
                    assert(0);
                    return 0;
                }
                // Overflow
                else {
                    DBG_PRINTF_ERROR("A2L dyn address overflow detected! addr: %p, base1: %p, base2: %p\n", p, (void *)gA2lBasePtr, (void *)gA2lFramePtr);
                    assert(0);
                    return 0;
                }
            }
            // If only the frame pointer is set, use it as base pointer
            else if (gA2lFramePtr != NULL) {
                gA2lAutoAddrExt = XCP_ADDR_EXT_DYN;
                base_ptr = gA2lFramePtr;
            }
            // If only the base pointer is set, use it
            else if (gA2lBasePtr != NULL) {
                gA2lAutoAddrExt = XCP_ADDR_EXT_DYN + 1;
                base_ptr = gA2lBasePtr;
            }

            if (base_ptr != NULL) {
                addr_diff = (uint64_t)p - (uint64_t)base_ptr;
                // Ensure the address difference does not overflow the value range for signed int16_t
                uint64_t addr_high = (addr_diff >> 16);
                if (addr_high != 0 && addr_high != 0xFFFFFFFFFFFF) {
                    DBG_PRINTF5("A2L dyn address overflow detected! addr: %p, base: %p, trying absolute\n", p, (void *)base_ptr);
                    gA2lAutoAddrExt = XCP_ADDR_EXT_ABS;
                    return XcpAddrEncodeAbs(p);
                }
                return XcpAddrEncodeDyn(addr_diff, gA2lFixedEvent);
            } else {
                gA2lAutoAddrExt = XCP_ADDR_EXT_ABS;
                return XcpAddrEncodeAbs(p);
            }
        }

        else if (XcpAddrIsAbs(gA2lAddrExt)) {
            return XcpAddrEncodeAbs(p);
        }

#ifdef XCP_ENABLE_REL_ADDRESSING
        else if (XcpAddrIsRel(gA2lAddrExt)) {
            uint64_t addr_diff = 0;
            if (gA2lBasePtr != NULL) {
                addr_diff = (uint64_t)p - (uint64_t)gA2lBasePtr;
                // Ensure the address difference does not overflow the value range for signed int32_t
                uint64_t addr_high = (addr_diff >> 32);
                if (addr_high != 0 && addr_high != 0xFFFFFFFF) {
                    DBG_PRINTF_ERROR("A2L rel address overflow detected! addr: %p, base: %p\n", p, (void *)gA2lBasePtr);
                    assert(0);
                    return 0;
                }
            }
            return XcpAddrEncodeRel(addr_diff);
        }
#endif

        else if (XcpAddrIsDyn(gA2lAddrExt)) {
            uint64_t addr_diff = 0;
            if (gA2lBasePtr != NULL) {
                if (p != NULL) {
                    addr_diff = (uint64_t)p - (uint64_t)gA2lBasePtr;
                    // Ensure the address difference does not overflow the value range for signed int16_t
                    uint64_t addr_high = (addr_diff >> 16);
                    if (addr_high != 0 && addr_high != 0xFFFFFFFFFFFF) {
                        DBG_PRINTF_ERROR("A2L dyn address overflow detected! addr: %p, base: %p\n", p, (void *)gA2lBasePtr);
                        assert(0);
                        return 0;
                    }
                } else {
                    addr_diff = 0;
                }
            }
            return XcpAddrEncodeDyn(addr_diff, gA2lFixedEvent);
        }

        else
#ifdef XCP_ENABLE_CALSEG_LIST
            if (XcpAddrIsSeg(gA2lAddrExt)) {
            uint64_t addr_diff = 0;
            if (gA2lBasePtr != NULL) {
                addr_diff = (uint64_t)p - (uint64_t)gA2lBasePtr;
                // Ensure the relative address does not overflow the 16 Bit A2L address offset for calibration segment relative addressing
                if ((addr_diff >> 16) != 0) {
                    DBG_PRINTF_ERROR("A2L seg relative address overflow detected! addr: %p, base: %p\n", p, (void *)gA2lBasePtr);
                    assert(0);
                    return 0;
                }
            }
            return XcpGetCalSegBaseAddress(gA2lAddrIndex) + (addr_diff & 0xFFFF);
        } else
#endif
        {
            DBG_PRINTF_ERROR("A2L address extension %u is not supported!\n", gA2lAddrExt);
        }
    } // if (gA2lFile != NULL)

    return 0; // Return 0 if the A2L file is not open or the address extension is not supported
}

//----------------------------------------------------------------------------------
// Conversions

void printPhysUnit(FILE *file, const char *unit_or_conversion) {

    // It is a phys unit if the string is not NULL or empty and does not start with "conv."
    if (unit_or_conversion != NULL) {
        size_t len = strlen(unit_or_conversion);
        if (len > 0 && !(len > 5 && strncmp(unit_or_conversion, "conv.", 5) == 0)) {
            fprintf(file, " PHYS_UNIT \"%s\"", unit_or_conversion);
        }
    }
}

static const char *getConversion(const char *unit_or_conversion, double *min, double *max) {

    // If the unit_or_conversion string begins with "conv." it is a conversion method name, return it directly
    if (unit_or_conversion != NULL && strlen(unit_or_conversion) > 5 && strncmp(unit_or_conversion, "conv.", 5) == 0) {

        double factor = 1.0, offset = -50.0;

        // Adjust min and max values according to the conversion method
        if (min != NULL) {
            *min = *min * factor + offset;
        }
        if (max != NULL) {
            *max = *max * factor + offset;
        }
        // Conversion method
        return unit_or_conversion;
    } else {
        // Otherwise return "NO_COMPU_METHOD";
        return "NO_COMPU_METHOD";
    }
}

const char *A2lCreateLinearConversion_(const char *name, const char *comment, const char *unit, double factor, double offset) {
    if (gA2lFile != NULL) {
        if (unit == NULL)
            unit = "";
        if (comment == NULL)
            comment = "";
        SNPRINTF(gA2lConvName, sizeof(gA2lConvName), "conv.%s", name); // Build the conversion name with prefix "conv." and store it in a static variable
        fprintf(gA2lConversionsFile, "/begin COMPU_METHOD conv.%s \"%s\" LINEAR \"%%6.3\" \"%s\" COEFFS_LINEAR %g %g /end COMPU_METHOD\n", name, comment, unit, factor, offset);
        gA2lConversions++;

        // Return the conversion name for reference when creating measurements
        return gA2lConvName;
    }
    return "";
}

const char *A2lCreateEnumConversion_(const char *name, const char *enum_description) {
    if (gA2lFile != NULL) {
        SNPRINTF(gA2lConvName, sizeof(gA2lConvName), "conv.%s", name); // Build the conversion name with prefix "conv." and store it in a static variable
        fprintf(gA2lConversionsFile, "/begin COMPU_METHOD conv.%s \"\" TAB_VERB \"%%.0 \" \"\" COMPU_TAB_REF conv.%s.table /end COMPU_METHOD\n", name, name);
        fprintf(gA2lConversionsFile, "/begin COMPU_VTAB conv.%s.table \"\" TAB_VERB %s /end COMPU_VTAB\n", name, enum_description);
        gA2lConversions++;
        return gA2lConvName; // Return the conversion name for reference when creating measurements
    }
    return "";
}

//----------------------------------------------------------------------------------
// Typedefs

// Begin a typedef structure
void A2lTypedefBegin_(const char *name, uint32_t size, const char *format, ...) {
    if (gA2lFile != NULL) {

        // Format the comment string
        char comment[256];
        va_list args;
        va_start(args, format);
        vsnprintf(comment, sizeof(comment), format, args);
        va_end(args);

        DBG_PRINTF4("A2lTypedefBegin_: %s, size=%u, comment='%s'\n", name, size, comment);

        fprintf(gA2lFile, "\n/begin TYPEDEF_STRUCTURE %s \"%s\" 0x%X\n", name, comment, size);
        gA2lTypedefs++;
    }
}

// End a typedef structure
void A2lTypedefEnd_(void) {
    if (gA2lFile != NULL) {
        DBG_PRINT4("A2lTypedefEnd_\n");
        fprintf(gA2lFile, "/end TYPEDEF_STRUCTURE\n\n");
    }
}

// For scalar or one dimensional measurement and parameter components of specified type
// type_name is the name of another typedef, typedef_measurement or typedef_characteristic
void A2lTypedefComponent_(const char *name, const char *type_name, uint16_t x_dim, uint32_t offset) {
    if (gA2lFile != NULL) {
        DBG_PRINTF4("A2lTypedefComponent_: %s, %s, x_dim=%u, offset=0x%X\n", name, type_name, x_dim, offset);
        fprintf(gA2lFile, "  /begin STRUCTURE_COMPONENT %s %s 0x%X", name, type_name, offset);
        if (x_dim > 1)
            fprintf(gA2lFile, " MATRIX_DIM %u", x_dim);
        fprintf(gA2lFile, " /end STRUCTURE_COMPONENT\n");
        gA2lComponents++;
    }
}

// For measurement components with TYPEDEF_MEASUREMENT for fields with comment, unit, min, max
void A2lTypedefMeasurementComponent_(const char *name, const char *type_name, uint16_t x_dim, uint32_t offset, const char *comment, const char *unit_or_conversion, double min,
                                     double max) {
    if (gA2lFile != NULL) {
        DBG_PRINTF4("A2lTypedefMeasurementComponent_: %s, %s, x_dim=%u, offset=0x%X\n", name, type_name, x_dim, offset);

        // TYPEDEF_MEASUREMENT
        const char *conv = getConversion(unit_or_conversion, NULL, NULL);
        assert(gA2lTypedefsFile != NULL);
        fprintf(gA2lTypedefsFile, "/begin TYPEDEF_MEASUREMENT M_%s \"%s\" %s %s 0 0 %g %g", name, comment, type_name, conv, min, max);
        printPhysUnit(gA2lTypedefsFile, unit_or_conversion);
        fprintf(gA2lTypedefsFile, " /end TYPEDEF_MEASUREMENT\n");

        // STRUCTURE_COMPONENT
        fprintf(gA2lFile, "  /begin STRUCTURE_COMPONENT %s M_%s 0x%X", name, name, offset);
        if (x_dim > 1)
            fprintf(gA2lFile, " MATRIX_DIM %u", x_dim);
        fprintf(gA2lFile, " /end STRUCTURE_COMPONENT\n");
        gA2lComponents++;
    }
}

// For multidimensional parameter components with TYPEDEF_CHARACTERISTIC for fields with comment, unit, min, max
void A2lTypedefParameterComponent_(const char *name, const char *type_name, uint16_t x_dim, uint16_t y_dim, uint32_t offset, const char *comment, const char *unit_or_conversion,
                                   double min, double max, const char *x_axis, const char *y_axis) {
    if (gA2lFile != NULL) {
        DBG_PRINTF4("A2lTypedefParameterComponent_: %s, %s, x_dim=%u, y_dim=%u, offset=0x%X\n", name, type_name, x_dim, y_dim, offset);

        // TYPEDEF_AXIS (y_dim==0)
        if (y_dim == 0 && x_dim > 1) {
            fprintf(gA2lTypedefsFile, "/begin TYPEDEF_AXIS A_%s \"%s\" NO_INPUT_QUANTITY A_%s 0 NO_COMPU_METHOD %u %g %g", name, comment, type_name, x_dim, min, max);
            printPhysUnit(gA2lTypedefsFile, unit_or_conversion);
            fprintf(gA2lTypedefsFile, " /end TYPEDEF_AXIS\n");
            fprintf(gA2lFile, "  /begin STRUCTURE_COMPONENT %s A_%s 0x%X /end STRUCTURE_COMPONENT\n", name, name, offset);

        }

        // TYPEDEF_CHARACTERISTIC
        else {
            // MAP
            if (y_dim > 1) {
                fprintf(gA2lTypedefsFile, "/begin TYPEDEF_CHARACTERISTIC C_%s \"%s\" MAP %s 0 NO_COMPU_METHOD %g %g", name, comment, type_name, min, max);
                if (x_axis == NULL)
                    fprintf(gA2lTypedefsFile, " /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR", x_dim, x_dim - 1,
                            x_dim);
                else
                    fprintf(gA2lTypedefsFile, " /begin AXIS_DESCR COM_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD %u 0.0 0.0 AXIS_PTS_REF THIS.%s /end AXIS_DESCR", x_dim, x_axis);
                if (y_axis == NULL)
                    fprintf(gA2lTypedefsFile, " /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR", y_dim, y_dim - 1,
                            y_dim);
                else
                    fprintf(gA2lTypedefsFile, " /begin AXIS_DESCR COM_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD %u 0.0 0.0 AXIS_PTS_REF THIS.%s /end AXIS_DESCR", y_dim, y_axis);
            }
            // CURVE
            else if (x_dim > 1) {
                fprintf(gA2lTypedefsFile, "/begin TYPEDEF_CHARACTERISTIC C_%s \"%s\" CURVE %s 0 NO_COMPU_METHOD %g %g", name, comment, type_name, min, max);
                if (x_axis == NULL)
                    fprintf(gA2lTypedefsFile, " /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR", x_dim, x_dim - 1,
                            x_dim);
                else
                    fprintf(gA2lTypedefsFile, " /begin AXIS_DESCR COM_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD %u 0.0 0.0 AXIS_PTS_REF THIS.%s /end AXIS_DESCR", x_dim, x_axis);
            }
            // VALUE
            else if (x_dim == 1 && y_dim == 1) {
                const char *conv = getConversion(unit_or_conversion, NULL, NULL);
                fprintf(gA2lTypedefsFile, "/begin TYPEDEF_CHARACTERISTIC C_%s \"%s\" VALUE %s 0 %s %g %g", name, comment, type_name, conv, min, max);
            }
            //
            else {
                DBG_PRINTF_ERROR("Invalid dimensions: x_dim=%u, y_dim=%u\n", x_dim, y_dim);
                assert(0);
            }
            printPhysUnit(gA2lTypedefsFile, unit_or_conversion);
            fprintf(gA2lTypedefsFile, " /end TYPEDEF_CHARACTERISTIC\n");
            fprintf(gA2lFile, "  /begin STRUCTURE_COMPONENT %s C_%s 0x%X /end STRUCTURE_COMPONENT\n", name, name, offset);
        }

        gA2lComponents++;
    }
}

void A2lCreateInstance_(const char *instance_name, const char *typeName, const uint16_t x_dim, const void *ptr, const char *comment) {
    if (gA2lFile != NULL) {
        uint32_t addr = A2lGetAddr_(ptr);
        uint8_t ext = A2lGetAddrExt_();
        DBG_PRINTF4("A2lCreateInstance_: %s, \"%s\", %s, %s\n", instance_name, comment, typeName, dbgPrintfAddrExt(ext, addr));

        if (gA2lAutoGroups) {
            A2lAddToGroup(instance_name);
        }
        fprintf(gA2lFile, "/begin INSTANCE %s \"%s\" %s 0x%X", instance_name, comment, typeName, addr);
        printAddrExt(ext);

        // For measurements only: add MATRIX_DIM, READ_WRITE and IF_DATAif applicable
        if (x_dim > 1) { // Array of instance
            fprintf(gA2lFile, " MATRIX_DIM %u", x_dim);
        }
        uint8_t addr_ext = A2lGetAddrExt_();
        if (XcpAddrIsAbs(addr_ext) || XcpAddrIsDyn(addr_ext)) { // Measurements in absolute and dynamic mode allows write access
            fprintf(gA2lFile, " READ_WRITE");
        }
        A2lCreateMeasurement_IF_DATA(); // Create event definition for measurements

        fprintf(gA2lFile, " /end INSTANCE\n");
        gA2lInstances++;
    }
}

//----------------------------------------------------------------------------------
// Measurements

void A2lCreateMeasurement_(const char *instance_name, const char *name, tA2lTypeId type, const void *ptr, const char *unit_or_conversion, double phys_min, double phys_max,
                           const char *comment) {
    if (gA2lFile != NULL) {
        uint32_t addr = A2lGetAddr_(ptr);
        uint8_t ext = A2lGetAddrExt_();
        const char *symbol_name = A2lGetSymbolName(instance_name, name);
        DBG_PRINTF4("A2lCreateMeasurement_: %s, \"%s\", addr=%p, unit=\"%s\", min=%g, max=%g, %s\n", symbol_name, comment != NULL ? comment : "", ptr,
                    unit_or_conversion != NULL ? unit_or_conversion : "", phys_min, phys_max, dbgPrintfAddrExt(ext, addr));
        if (gA2lAutoGroups) {
            A2lAddToGroup(symbol_name);
        }
        if (comment == NULL) {
            comment = "";
        }
        double min, max;
        const char *conv;
        if (phys_min == 0.0 && phys_max == 0.0) {
            min = getTypeMin(type);
            max = getTypeMax(type);
            conv = getConversion(unit_or_conversion, &min, &max);
        } else {
            min = phys_min;
            max = phys_max;
            conv = getConversion(unit_or_conversion, NULL, NULL);
        }
        fprintf(gA2lFile, "/begin MEASUREMENT %s \"%s\" %s %s 0 0 %g %g ECU_ADDRESS 0x%X", symbol_name, comment, A2lGetA2lTypeName(type), conv, min, max, addr);
        printAddrExt(ext);
        printPhysUnit(gA2lFile, unit_or_conversion);
        uint8_t addr_ext = A2lGetAddrExt_();
        if (XcpAddrIsAbs(addr_ext) || XcpAddrIsDyn(addr_ext)) { // Absolute and dynamic mode allows write access
            fprintf(gA2lFile, " READ_WRITE");
        }
        A2lCreateMeasurement_IF_DATA();
        fprintf(gA2lFile, " /end MEASUREMENT\n");
        gA2lMeasurements++;
    }
}

void A2lCreateMeasurementArray_(const char *instance_name, const char *name, tA2lTypeId type, int x_dim, int y_dim, const void *ptr, const char *unit_or_conversion,
                                double phys_min, double phys_max, const char *comment) {
    if (gA2lFile != NULL) {
        uint32_t addr = A2lGetAddr_((const void *)ptr);
        uint8_t ext = A2lGetAddrExt_();
        const char *symbol_name = A2lGetSymbolName(instance_name, name);
        DBG_PRINTF4("A2lCreateMeasurementArray_: %s, \"%s\", addr=%p, x_dim=%d, y_dim=%d, unit=\"%s\", min=%g, max=%g, %s\n", symbol_name, comment != NULL ? comment : "", ptr,
                    x_dim, y_dim, unit_or_conversion != NULL ? unit_or_conversion : "", phys_min, phys_max, dbgPrintfAddrExt(ext, addr));
        if (gA2lAutoGroups) {
            A2lAddToGroup(symbol_name);
        }
        if (comment == NULL)
            comment = "";
        double min, max;
        const char *conv;
        if (phys_min == 0.0 && phys_max == 0.0) {
            min = getTypeMin(type);
            max = getTypeMax(type);
            conv = getConversion(unit_or_conversion, &min, &max);
        } else {
            min = phys_min;
            max = phys_max;
            conv = getConversion(unit_or_conversion, NULL, NULL);
        }
        fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"%s\" VAL_BLK 0x%X %s 0 %s %g %g MATRIX_DIM %u %u", symbol_name, comment, addr, A2lGetRecordLayoutName_(type), conv, min, max,
                x_dim, y_dim);
        printAddrExt(ext);
        A2lCreateMeasurement_IF_DATA();
        fprintf(gA2lFile, " /end CHARACTERISTIC\n");
        gA2lMeasurements++;
    }
}

//----------------------------------------------------------------------------------
// Parameters

void A2lCreateParameter_(const char *name, tA2lTypeId type, const void *ptr, const char *comment, const char *unit_or_conversion, double phys_min, double phys_max) {

    if (gA2lFile != NULL) {
        uint32_t addr = A2lGetAddr_(ptr);
        uint8_t ext = A2lGetAddrExt_();
        DBG_PRINTF4("A2lCreateParameter_: %s, \"%s\", addr=%p, unit=\"%s\", min=%g, max=%g, %s\n", name, comment != NULL ? comment : "", ptr,
                    unit_or_conversion != NULL ? unit_or_conversion : "", phys_min, phys_max, dbgPrintfAddrExt(ext, addr));
        if (gA2lAutoGroups) {
            A2lAddToGroup(name);
        }
        double min, max;
        const char *conv;
        if (phys_min == 0.0 && phys_max == 0.0) {
            min = getTypeMin(type);
            max = getTypeMax(type);
            conv = getConversion(unit_or_conversion, &min, &max);
        } else {
            min = phys_min;
            max = phys_max;
            conv = getConversion(unit_or_conversion, NULL, NULL);
        }
        fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"%s\" VALUE 0x%X %s 0 %s %g %g", name, comment, addr, A2lGetRecordLayoutName_(type), conv, min, max);
        printPhysUnit(gA2lFile, unit_or_conversion);
        printAddrExt(ext);
        A2lCreateMeasurement_IF_DATA();
        fprintf(gA2lFile, " /end CHARACTERISTIC\n");
        gA2lParameters++;
    }
}

void A2lCreateMap_(const char *name, tA2lTypeId type, const void *ptr, uint32_t xdim, uint32_t ydim, const char *comment, const char *unit, double min, double max,
                   const char *x_axis, const char *y_axis) {

    if (gA2lFile != NULL) {
        uint32_t addr = A2lGetAddr_(ptr);
        uint8_t ext = A2lGetAddrExt_();
        DBG_PRINTF4("A2lCreateMap_: %s, \"%s\", addr=%p, x_dim=%d, y_dim=%d, unit=\"%s\", min=%g, max=%g, %s\n", name, comment != NULL ? comment : "", ptr, xdim, ydim,
                    unit != NULL ? unit : "", min, max, dbgPrintfAddrExt(ext, addr));
        if (gA2lAutoGroups) {
            A2lAddToGroup(name);
        }
        fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"%s\" MAP 0x%X %s 0 NO_COMPU_METHOD %g %g", name, comment, addr, A2lGetRecordLayoutName_(type), min, max);
        if (x_axis == NULL) {
            fprintf(gA2lFile, " /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR", xdim, xdim - 1, xdim);
        } else {
            fprintf(gA2lFile, " /begin AXIS_DESCR COM_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0.0 0.0 AXIS_PTS_REF %s /end AXIS_DESCR", xdim, x_axis);
        }
        if (y_axis == NULL) {
            fprintf(gA2lFile, " /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR", ydim, ydim - 1, ydim);
        } else {
            fprintf(gA2lFile, " /begin AXIS_DESCR COM_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0.0 0.0 AXIS_PTS_REF %s /end AXIS_DESCR", ydim, y_axis);
        }
        printPhysUnit(gA2lFile, unit);
        printAddrExt(ext);
        A2lCreateMeasurement_IF_DATA();
        fprintf(gA2lFile, " /end CHARACTERISTIC\n");
        gA2lParameters++;
    }
}

void A2lCreateCurve_(const char *name, tA2lTypeId type, const void *ptr, uint32_t xdim, const char *comment, const char *unit, double min, double max, const char *x_axis) {

    if (gA2lFile != NULL) {
        uint32_t addr = A2lGetAddr_(ptr);
        uint8_t ext = A2lGetAddrExt_();
        DBG_PRINTF4("A2lCreateCurve_: %s, \"%s\", addr=%p, x_dim=%d, unit=\"%s\", min=%g, max=%g, %s\n", name, comment != NULL ? comment : "", ptr, xdim, unit != NULL ? unit : "",
                    min, max, dbgPrintfAddrExt(ext, addr));
        if (gA2lAutoGroups) {
            A2lAddToGroup(name);
        }
        fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"%s\" CURVE 0x%X %s 0 NO_COMPU_METHOD %g %g", name, comment, addr, A2lGetRecordLayoutName_(type), min, max);
        if (x_axis == NULL) {
            fprintf(gA2lFile, " /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR", xdim, xdim - 1, xdim);
        } else {
            fprintf(gA2lFile, " /begin AXIS_DESCR COM_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0.0 0.0 AXIS_PTS_REF %s /end AXIS_DESCR", xdim, x_axis);
        }
        printPhysUnit(gA2lFile, unit);
        printAddrExt(ext);
        A2lCreateMeasurement_IF_DATA();

        fprintf(gA2lFile, " /end CHARACTERISTIC\n");
        gA2lParameters++;
    }
}

void A2lCreateAxis_(const char *name, tA2lTypeId type, const void *ptr, uint32_t xdim, const char *comment, const char *unit, double min, double max) {

    if (gA2lFile != NULL) {
        uint32_t addr = A2lGetAddr_(ptr);
        uint8_t ext = A2lGetAddrExt_();
        DBG_PRINTF4("A2lCreateAxis_: %s, \"%s\", addr=%p, x_dim=%d, unit=\"%s\", min=%g, max=%g, %s\n", name, comment != NULL ? comment : "", ptr, xdim, unit != NULL ? unit : "",
                    min, max, dbgPrintfAddrExt(ext, addr));
        if (gA2lAutoGroups) {
            A2lAddToGroup(name);
        }
        fprintf(gA2lFile, "/begin AXIS_PTS %s \"%s\" 0x%X NO_INPUT_QUANTITY A_%s 0 NO_COMPU_METHOD %u %g %g", name, comment, addr, A2lGetRecordLayoutName_(type), xdim, min, max);
        printPhysUnit(gA2lFile, unit);
        printAddrExt(ext);
        A2lCreateMeasurement_IF_DATA();
        fprintf(gA2lFile, " /end AXIS_PTS\n");
        gA2lParameters++;
    }
}

//----------------------------------------------------------------------------------
// Groups

// Begin a group for measurements or parameters
void A2lBeginGroup(const char *name, const char *comment, bool is_parameter_group) {
    if (gA2lFile != NULL) {
        assert(gA2lGroupsFile != NULL);
        if ((gA2lAutoGroupName == NULL) || (strcmp(name, gA2lAutoGroupName) != 0) || (is_parameter_group != gA2lAutoGroupIsParameter)) {
            A2lEndGroup(); // Close previous group if any
            gA2lAutoGroupName = name;
            gA2lAutoGroupIsParameter = is_parameter_group;
            fprintf(gA2lGroupsFile, "/begin GROUP %s \"%s\" %s", name, comment, is_parameter_group ? "ROOT" : "");
            fprintf(gA2lGroupsFile, " /begin REF_%s", gA2lAutoGroupIsParameter ? "CHARACTERISTIC" : "MEASUREMENT");
        }
    }
}

// Add a measurement or parameter to the current open group
void A2lAddToGroup(const char *name) {
    if (gA2lFile != NULL) {
        assert(gA2lGroupsFile != NULL);
        if (gA2lAutoGroupName != NULL) {
            fprintf(gA2lGroupsFile, " %s", name);
        }
    }
}

// End the current open group for measurements or parameters
void A2lEndGroup(void) {
    if (gA2lFile != NULL) {
        assert(gA2lGroupsFile != NULL);
        if (gA2lAutoGroupName == NULL)
            return;
        fprintf(gA2lGroupsFile, " /end REF_%s", gA2lAutoGroupIsParameter ? "CHARACTERISTIC" : "MEASUREMENT");
        fprintf(gA2lGroupsFile, " /end GROUP\n");
        gA2lAutoGroupName = NULL;
    }
}

void A2lCreateMeasurementGroup(const char *name, int count, ...) {
    if (gA2lFile != NULL) {
        va_list ap;
        assert(gA2lGroupsFile != NULL);
        A2lEndGroup(); // End the previous group if any
        fprintf(gA2lGroupsFile, "/begin GROUP %s \"\" ROOT", name);
        fprintf(gA2lGroupsFile, " /begin REF_MEASUREMENT");
        va_start(ap, count);
        for (int i = 0; i < count; i++) {
            fprintf(gA2lGroupsFile, " %s", va_arg(ap, char *));
        }
        va_end(ap);
        fprintf(gA2lGroupsFile, " /end REF_MEASUREMENT");
        fprintf(gA2lGroupsFile, " /end GROUP\n");
    }
}

void A2lCreateMeasurementGroupFromList(const char *name, char *names[], uint32_t count) {
    if (gA2lFile != NULL) {
        assert(gA2lGroupsFile != NULL);
        A2lEndGroup(); // End the previous group if any
        fprintf(gA2lGroupsFile, "/begin GROUP %s \"\" ROOT", name);
        fprintf(gA2lGroupsFile, " /begin REF_MEASUREMENT");
        for (uint32_t i1 = 0; i1 < count; i1++) {
            fprintf(gA2lGroupsFile, " %s", names[i1]);
        }
        fprintf(gA2lGroupsFile, " /end REF_MEASUREMENT");
        fprintf(gA2lGroupsFile, " /end GROUP\n");
    }
}

void A2lCreateParameterGroup(const char *name, int count, ...) {
    if (gA2lFile != NULL) {
        va_list ap;
        assert(gA2lGroupsFile != NULL);
        A2lEndGroup(); // End the previous group if any
        fprintf(gA2lGroupsFile, "/begin GROUP %s \"\" ROOT", name);
        fprintf(gA2lGroupsFile, " /begin REF_CHARACTERISTIC");
        va_start(ap, count);
        for (int i = 0; i < count; i++) {
            fprintf(gA2lGroupsFile, " %s", va_arg(ap, char *));
        }
        va_end(ap);
        fprintf(gA2lGroupsFile, " /end REF_CHARACTERISTIC");
        fprintf(gA2lGroupsFile, " /end GROUP\n");
    }
}

void A2lCreateParameterGroupFromList(const char *name, const char *pNames[], int count) {
    if (gA2lFile != NULL) {
        assert(gA2lGroupsFile != NULL);
        A2lEndGroup(); // End the previous group if any
        fprintf(gA2lGroupsFile, "/begin GROUP %s \"\" ROOT", name);
        fprintf(gA2lGroupsFile, " /begin REF_CHARACTERISTIC");
        for (int i = 0; i < count; i++) {
            fprintf(gA2lGroupsFile, " %s", pNames[i]);
        }
        fprintf(gA2lGroupsFile, " /end REF_CHARACTERISTIC");
        fprintf(gA2lGroupsFile, " /end GROUP\n\n");
    }
}

//----------------------------------------------------------------------------------
// Helper function to ensure A2L generation blocks are executed only once
// This allows to use the macros in loops or functions without taking care of multiple executions

bool A2lOnce_(A2L_ONCE_TYPE *value) {
    if (gA2lFile != NULL) {
        A2L_ONCE_TYPE old_value = 0;
        if (atomic_compare_exchange_strong_explicit((A2L_ONCE_ATOMIC_TYPE *)value, &old_value, gA2lOncePass, memory_order_relaxed, memory_order_relaxed)) {
            return true; // Return true if A2L file is open
        }
    }
    return false;
}

//-----------------------------------------------------------------------------------------------------
// A2L file generation and finalization on XCP connect

static bool includeFile(FILE **filep, const char *filename) {

    if (filep != NULL && *filep != NULL && filename != NULL) {

        fclose(*filep);
        *filep = NULL;

        FILE *file = fopen(filename, "r");
        if (file != NULL) {
            char line[512];
            while (fgets(line, sizeof(line), file) != NULL) {
                fprintf(gA2lFile, "%s", line);
            }
            fclose(file);
            remove(filename);
            return true;
        }
    }

    return true;
}

// Callback on XCP client tool connect
bool A2lCheckFinalizeOnConnect(uint8_t connect_mode) {

    // Finalize A2l once on connect, if A2L generation is active
    if (gA2lFinalizeOnConnect && gA2lFile != NULL) {
        A2lFinalize();
    }

    // If A2l generation is active, refuse connect
    else if (gA2lFile != NULL) {
        DBG_PRINT_WARNING("A2L file not finalized yet, XCP connect refused!\n");
        return false; // Refuse connect, waiting for finalization by application
    }

    // If A2L generation is not active
    // Give the client a way to delete A2L and BIN file
    else {
        if (connect_mode == 0xAA) {
            DBG_PRINT_WARNING("Delete A2L and BIN file (connect_mode=0xAA)!\n");
            remove(gA2lFilename);
#ifdef OPTION_CAL_PERSISTENCE
            XcpBinDelete();
#endif
            return false;
        }
    }

    return true;
}

// Finalize A2L file generation
bool A2lFinalize(void) {

    // If A2l file is open, finalize it
    if (gA2lFile != NULL) {

        // Close the last group if any
        if (gA2lAutoGroups) {
            A2lEndGroup();
        }

        // Create event groups
#if defined(XCP_ENABLE_DAQ_EVENT_LIST)
        uint16_t eventCount = XcpGetEventCount();
        tXcpEventList *eventList = XcpGetEventList();
        if (eventList != NULL && eventCount > 0) {

            // Create a enum conversion with all event ids
            fprintf(gA2lConversionsFile, "/begin COMPU_METHOD conv.events \"\" TAB_VERB \"%%.0 \" \"\" COMPU_TAB_REF conv.events.table /end COMPU_METHOD\n");
            fprintf(gA2lConversionsFile, "/begin COMPU_VTAB conv.events.table \"\" TAB_VERB %u\n", eventCount);
            for (uint32_t id = 0; id < eventCount; id++) {
                fprintf(gA2lConversionsFile, " %u \"%s\"", id, XcpGetEventName(id));
            }
            fprintf(gA2lConversionsFile, "\n/end COMPU_VTAB\n");

            // Create a sub group for all events
            if (gA2lAutoGroups) {
                fprintf(gA2lGroupsFile, "/begin GROUP Events \"Events\" ROOT /begin SUB_GROUP");
#ifdef OPTION_DAQ_ASYNC_EVENT
                uint32_t id = 1; // Skip event 0 which is the built-in asynchronous events
#else
                uint32_t id = 0;
#endif
                for (; id < eventCount; id++) {
                    fprintf(gA2lGroupsFile, " %s", XcpGetEventName(id));
                }
                fprintf(gA2lGroupsFile, " /end SUB_GROUP /end GROUP\n");
            }
        }
#endif

        // Merge the include files with the main A2L file
        includeFile(&gA2lTypedefsFile, "typedefs.a2l");
        includeFile(&gA2lGroupsFile, "groups.a2l");
        includeFile(&gA2lConversionsFile, "conversions.a2l");

        // Create MOD_PAR section with EPK and calibration segments
        A2lCreate_MOD_PAR();

        // Create IF_DATA section with event list and transport layer info
        A2lCreate_ETH_IF_DATA(gA2lUseTCP, gA2lOptionBindAddr, gA2lOptionPort);

        fprintf(gA2lFile, "%s", gA2lFooter);

        fclose(gA2lFile);
        gA2lFile = NULL;
        gA2lFileWritten = true;

        DBG_PRINTF3("A2L created: %u measurements, %u params, %u typedefs, %u components, %u instances, %u conversions\n", gA2lMeasurements, gA2lParameters, gA2lTypedefs,
                    gA2lComponents, gA2lInstances, gA2lConversions);

        // Write the binary persistence file
        // This is required to make sure the A2L file remains valid, even if the creation order of event or calibration segment is different
#ifdef OPTION_CAL_PERSISTENCE
        if (!gA2lWriteAlways)
            XcpBinWrite(XCP_CALPAGE_WORKING_PAGE);
#endif

        // Notify XCP that there is an A2L file available for upload by the XCP client
        XcpSetA2lName(gA2lFilename);
        return true; // A2L file generation successful
    }

    return false; // A2L file generation not active
}

// Lock and unlock
void A2lLock(void) {
    if (gA2lFile != NULL) {
        mutexLock(&gA2lMutex);
    }
}
void A2lUnlock(void) {
    if (gA2lFile != NULL) {
        if (gA2lAutoGroups) {
            A2lEndGroup();
        }
        mutexUnlock(&gA2lMutex);
    }
}

// Open the A2L file and register the finalize callback
bool A2lInit(const uint8_t *addr, uint16_t port, bool useTCP, uint8_t mode) {

    assert(addr != NULL);

    // Check and ignore, if the XCP singleton has not been initialized and activated
    if (!XcpIsActivated()) {
        DBG_PRINT3("A2lInit: XCP is deactivated!\n");
        return true;
    }

    // Save communication parameters
    memcpy(&gA2lOptionBindAddr, addr, 4);
    gA2lOptionPort = port;
    gA2lUseTCP = useTCP;

    // Save mode
    gA2lMode = mode;
    gA2lWriteAlways = !!(mode & A2L_MODE_WRITE_ALWAYS);
#ifndef OPTION_CAL_PERSISTENCE
    assert(gA2lWriteAlways);
#endif
    gA2lAutoGroups = !!(mode & A2L_MODE_AUTO_GROUPS);
    gA2lFinalizeOnConnect = !!(mode & A2L_MODE_FINALIZE_ON_CONNECT);

    mutexInit(&gA2lMutex, false, 0);

    // Build A2L filename from project name and EPK
    // If A2l file is generated only once for a new build, the EPK is appended to the filename
    if (gA2lWriteAlways) {
        SNPRINTF(gA2lFilename, sizeof(gA2lFilename), "%s.a2l", XcpGetProjectName());
    } else {
        SNPRINTF(gA2lFilename, sizeof(gA2lFilename), "%s_%s.a2l", XcpGetProjectName(), gA2lWriteAlways ? "" : XcpGetEpk());
    }

    // Register a callback on XCP connect
    ApplXcpRegisterConnectCallback(A2lCheckFinalizeOnConnect);

    // In A2L_WRITE_ONCE mode:
    // Check if the A2L file already exists and the persistence BIN file has been loaded and checked
    // If yes, skip generation if not write always
    gA2lFileWritten = false;
    if (!gA2lWriteAlways && (XcpGetSessionStatus() & SS_PERSISTENCE_LOADED) && fexists(gA2lFilename)) {
        // Notify XCP that there is an A2L file available for upload by the XCP client
        XcpSetA2lName(gA2lFilename);
        DBG_PRINTF_WARNING("A2L file %s already exists, assuming it is still valid, disabling A2L generation\n", gA2lFilename);
        return true;
    }

    // Open A2L file for generation
    if (!A2lOpen()) {
        DBG_PRINTF_ERROR("Failed to open A2L file %s\n", gA2lFilename);
        return false;
    }

    return true;
}

#endif // XCP_ENABLE_A2L_GENERATOR
