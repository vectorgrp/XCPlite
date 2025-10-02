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
#include "persistency.h" // for XcpBinWrite, XcpBinLoad
#include "platform.h"    // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex
#include "xcp.h"         // for CRC_XXX
#include "xcpLite.h"     // for tXcpDaqLists, XcpXxx, ApplXcpXxx, ...
#include "xcp_cfg.h"     // for XCP_xxx
#include "xcptl_cfg.h"   // for XCPTL_xxx

#ifdef _WIN32
#ifdef OPTION_ENABLE_GET_LOCAL_ADDR
#include <stdlib.h> // for free, malloc in getLocalAddr
#endif
#endif

//----------------------------------------------------------------------------------

#define INCLUDE_AML_FILES // Use /include "file.aml"
// #define EMBED_AML_FILES // Embed AML files into generated A2L file

//----------------------------------------------------------------------------------

static FILE *gA2lFile = NULL;
static bool gA2lFileFinalized = false;

static char gA2lFilename[256];
#ifdef OPTION_CAL_PERSISTENCE
static char gBinFilename[256];
#endif

static bool gA2lFinalizeOnConnect = false; // Finalize A2L file on connect
static bool gA2lWriteAlways = true;        // Write A2L file always, even if no changes were made

static MUTEX gA2lMutex;

static FILE *gA2lTypedefsFile = NULL;
static FILE *gA2lGroupsFile = NULL;

static FILE *gA2lConversionsFile = NULL;
static char gA2lConvName[256];

static char gA2lAutoGroups = true;            // Automatically create groups for measurements and parameters
static const char *gA2lAutoGroupName = NULL;  // Current open group
static bool gA2lAutoGroupIsParameter = false; // Group is a parameter group

static bool gA2lUseTCP = false;
static uint16_t gA2lOptionPort = 5555;
static uint8_t gA2lOptionBindAddr[4] = {0, 0, 0, 0};

static tXcpEventId gA2lFixedEvent = XCP_UNDEFINED_EVENT_ID;
static tXcpEventId gA2lDefaultEvent = XCP_UNDEFINED_EVENT_ID;
static uint8_t gAl2AddrExt = XCP_ADDR_EXT_ABS; // Address extension (addressing mode, default absolute)
static const uint8_t *gA2lAddrBase = NULL;     // Base address for rel and dyn mode
static tXcpCalSegIndex gA2lAddrIndex = 0;      // Segment index for seg mode

static uint32_t gA2lMeasurements;
static uint32_t gA2lParameters;
static uint32_t gA2lTypedefs;
static uint32_t gA2lComponents;
static uint32_t gA2lInstances;
static uint32_t gA2lConversions;

//----------------------------------------------------------------------------------
static const char *gA2lHeader1 = "ASAP2_VERSION 1 71\n"
                                 "/begin PROJECT %s \"\"\n\n" // project name
                                 "/begin HEADER \"\" VERSION \"1.0\" PROJECT_NO XCPlite /end HEADER\n\n"
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
static const char *gA2lMemorySegment = "/begin MEMORY_SEGMENT %s \"\" DATA FLASH INTERN 0x%08X 0x%X -1 -1 -1 -1 -1\n" // name, start, size
                                       "/begin IF_DATA XCP\n"
                                       "/begin SEGMENT %u 2 0 0 0\n" // index
                                       "/begin CHECKSUM XCP_CRC_16_CITT MAX_BLOCK_SIZE 0xFFFF EXTERNAL_FUNCTION \"\" /end CHECKSUM\n"
                                       // 2 calibration pages, 0=working page (RAM), 1=initial readonly page (FLASH), independent access to ECU and XCP page possible
                                       "/begin PAGE 0 ECU_ACCESS_DONT_CARE XCP_READ_ACCESS_DONT_CARE XCP_WRITE_ACCESS_DONT_CARE /end PAGE\n"
                                       "/begin PAGE 1 ECU_ACCESS_DONT_CARE XCP_READ_ACCESS_DONT_CARE XCP_WRITE_ACCESS_NOT_ALLOWED /end PAGE\n"
                                       "/end SEGMENT\n"
                                       "/end IF_DATA\n"
                                       "/end MEMORY_SEGMENT\n";

#ifdef XCP_ENABLE_EPK_CALSEG
static const char *gA2lEpkMemorySegment = "/begin MEMORY_SEGMENT epk \"\" DATA FLASH INTERN 0x%08X %u -1 -1 -1 -1 -1\n"
                                          "/begin IF_DATA XCP\n"
                                          "/begin SEGMENT 0 2 0 0 0\n"
                                          // @@@@ TODO: Workaround: EPK segment has 2 readonly pages, CANape would not care for a single page EPK segment, reads active page always
                                          // from segment 0 and uses only SET_CAL_PAGE ALL mode
                                          "/begin CHECKSUM XCP_CRC_16_CITT MAX_BLOCK_SIZE 0xFFFF EXTERNAL_FUNCTION \"\" /end CHECKSUM\n"
                                          "/begin PAGE 0 ECU_ACCESS_DONT_CARE XCP_READ_ACCESS_DONT_CARE XCP_WRITE_ACCESS_NOT_ALLOWED /end PAGE\n"
                                          "/begin PAGE 1 ECU_ACCESS_DONT_CARE XCP_READ_ACCESS_DONT_CARE XCP_WRITE_ACCESS_NOT_ALLOWED /end PAGE\n"
                                          "/end SEGMENT\n"
                                          "/end IF_DATA\n"
                                          "/end MEMORY_SEGMENT\n";
#endif

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
    "OPTIONAL_CMD COPY_CAL_PAGE\n"
#ifdef XCP_ENABLE_CALSEG_LIST
    "OPTIONAL_CMD GET_PAG_PROCESSOR_INFO\n"
    "OPTIONAL_CMD GET_SEGMENT_INFO\n"
    "OPTIONAL_CMD GET_PAGE_INFO\n"
#ifdef XCP_ENABLE_FREEZE_CAL_PAGE
    "OPTIONAL_CMD GET_SEGMENT_MODE\n"
    "OPTIONAL_CMD SET_SEGMENT_MODE\n"
#endif // XCP_ENABLE_FREEZE_CAL_PAGE
#endif // XCP_ENABLE_CALSEG_LIST
#endif // XCP_ENABLE_CAL_PAGE
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

static bool A2lOpen(const char *filename, const char *projectname) {

    assert(!gA2lFileFinalized);

    gA2lFile = NULL;
    gA2lTypedefsFile = NULL;
    gA2lFixedEvent = XCP_UNDEFINED_EVENT_ID;
    gA2lMeasurements = gA2lParameters = gA2lTypedefs = gA2lInstances = gA2lConversions = gA2lComponents = 0;
    gA2lFile = fopen(filename, "w");
    gA2lTypedefsFile = fopen("typedefs.a2l", "w");
    gA2lGroupsFile = fopen("groups.a2l", "w");
    gA2lConversionsFile = fopen("conversions.a2l", "w");

    if (gA2lFile == 0 || gA2lTypedefsFile == 0 || gA2lGroupsFile == 0 || gA2lConversionsFile == 0) {
        DBG_PRINT_ERROR("Could not create A2L file!\n");
        return false;
    }

    // Create headers
    fprintf(gA2lFile, gA2lHeader1, projectname, projectname);
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
    fprintf(gA2lFile, gA2lHeader2);

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

            // EPK memory segment is segment 0
#ifdef XCP_ENABLE_CALSEG_LIST
#ifdef XCP_ENABLE_EPK_CALSEG
            fprintf(gA2lFile, gA2lEpkMemorySegment, XCP_ADDR_EPK, strlen(epk));
#endif
#endif // XCP_ENABLE_CALSEG_LIST
        }

        // Memory segments
#ifdef XCP_ENABLE_CALSEG_LIST
        tXcpCalSegList const *calSegList = XcpGetCalSegList();
        if (calSegList != NULL && calSegList->count > 0) {
            for (tXcpCalSegIndex i = 0; i < calSegList->count; i++) {
                tXcpCalSeg const *calseg = &calSegList->calseg[i];
                fprintf(gA2lFile, gA2lMemorySegment, calseg->name, XcpGetCalSegBaseAddress(i), calseg->size,
#ifdef XCP_ENABLE_EPK_CALSEG
                        i + 1
#else
                        i
#endif

                );
            }
        }
#endif // XCP_ENABLE_CALSEG_LIST

        fprintf(gA2lFile, "/end MOD_PAR\n\n");
    }
}

static void A2lCreate_IF_DATA_DAQ(void) {

#if defined(XCP_ENABLE_DAQ_EVENT_LIST)
    tXcpEventList *eventList;
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
    eventList = XcpGetEventList();
    eventCount = eventList != NULL ? eventList->count : 0;
#endif

    fprintf(gA2lFile, gA2lIfDataBeginDAQ, eventCount, XCP_TIMESTAMP_UNIT_S);

    // Eventlist

#if defined(XCP_ENABLE_DAQ_EVENT_LIST)
    for (uint32_t id = 0; id < eventCount; id++) {
        tXcpEvent *event = &eventList->event[id];
        uint16_t index = event->index;
        const char *name = event->name;

        // Convert cycle time to ASAM coding time cycle and time unit
        // RESOLUTION OF TIMESTAMP "UNIT_1NS" = 0, "UNIT_10NS" = 1, ...
        uint8_t timeUnit = 0; // timeCycle unit, 1ns=0, 10ns=1, 100ns=2, 1us=3, ..., 1ms=6, ...
        uint8_t timeCycle;    // cycle time in units, 0 = sporadic or unknown
        uint32_t c = event->cycleTimeNs;
        while (c >= 256) {
            c /= 10;
            timeUnit++;
        }
        timeCycle = (uint8_t)c;

        if (index == 0) {
            fprintf(gA2lFile, "/begin EVENT \"%s\" \"%s\" 0x%X DAQ 0xFF %u %u %u CONSISTENCY EVENT", name, name, id, timeCycle, timeUnit, event->priority);
        } else {
            fprintf(gA2lFile, "/begin EVENT \"%s_%u\" \"%s_%u\" 0x%X DAQ 0xFF %u %u %u CONSISTENCY EVENT", name, index, name, index, id, timeCycle, timeUnit, event->priority);
        }

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

        // Transport Layer info
        uint8_t addr0[] = {127, 0, 0, 1}; // Use localhost if no other option
        if (addr != NULL && addr[0] != 0) {
            memcpy(addr0, addr, 4);
        } else {
#ifdef OPTION_ENABLE_GET_LOCAL_ADDR
            socketGetLocalAddr(NULL, addr0);
#endif
        }
        char addrs[17];
        SPRINTF(addrs, "%u.%u.%u.%u", addr0[0], addr0[1], addr0[2], addr0[3]);
        char *prot = useTCP ? (char *)"TCP" : (char *)"UDP";
        fprintf(gA2lFile, gA2lIfDataEth, prot, XCP_TRANSPORT_LAYER_VERSION, port, addrs, prot);

        fprintf(gA2lFile, gA2lIfDataEnd);

        DBG_PRINTF3("A2L IF_DATA XCP_ON_%s, ip=%s, port=%u\n", prot, addrs, port);
    }
}

static void A2lCreateMeasurement_IF_DATA(void) {
    if (gA2lFile != NULL) {
        if (XcpAddrIsDyn(gAl2AddrExt)
#ifdef XCP_ENABLE_REL_ADDRESSING
            || XcpAddrIsRel(gAl2AddrExt)
#endif

        ) {
            if (gA2lFixedEvent != XCP_UNDEFINED_EVENT_ID) {
                fprintf(gA2lFile, " /begin IF_DATA XCP /begin DAQ_EVENT FIXED_EVENT_LIST EVENT 0x%X /end DAQ_EVENT /end IF_DATA", gA2lFixedEvent);
            } else {
                assert(false); // Fixed event must be set before calling this function
            }
        } else if (XcpAddrIsAbs(gAl2AddrExt)) {
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

// Calibration segment addressing mode
// Used for calibration parameters ins a XCP calibration segment (A2L MEMORY_SEGMENT)
#if defined(XCP_ENABLE_CALSEG_LIST)
void A2lSetSegAddrMode(tXcpCalSegIndex calseg_index, const uint8_t *calseg_instance_addr) {
    gA2lAddrIndex = calseg_index;
    gA2lAddrBase = calseg_instance_addr; // Address of the calibration segment instance which is used in the macros to create the components
    gAl2AddrExt = XCP_ADDR_EXT_SEG;
}
#endif

// Absolute addressing mode
// XCP address is the absolute address of the variable relative to the main module load address
void A2lSetAbsAddrMode(tXcpEventId default_event_id) {
    gA2lFixedEvent = XCP_UNDEFINED_EVENT_ID;
    gA2lDefaultEvent = default_event_id; // May be XCP_UNDEFINED_EVENT_ID
    gAl2AddrExt = XCP_ADDR_EXT_ABS;
}

// Relative addressing mode
// Used for accessing stack variables relative to the stack frame pointer
#ifdef XCP_ENABLE_REL_ADDRESSING
void A2lSetRelAddrMode(tXcpEventId event_id, const uint8_t *base) {
    gA2lAddrBase = base;
    gA2lFixedEvent = event_id;
    gA2lDefaultEvent = XCP_UNDEFINED_EVENT_ID;
    gAl2AddrExt = XCP_ADDR_EXT_REL;
}
#endif

// Dynamic addressing mode
// Relative address, used for heap and class members
// Enables XCP polling access
void A2lSetDynAddrMode(tXcpEventId event_id, uint8_t i, const uint8_t *base) {
    gA2lAddrBase = base;
    gA2lFixedEvent = event_id;
    gA2lDefaultEvent = XCP_UNDEFINED_EVENT_ID;
    gAl2AddrExt = XCP_ADDR_EXT_DYN + i;
    assert(gAl2AddrExt <= XCP_ADDR_EXT_DYN_MAX);
}

void A2lRstAddrMode(void) {
    gA2lFixedEvent = XCP_UNDEFINED_EVENT_ID;
    gA2lDefaultEvent = XCP_UNDEFINED_EVENT_ID;
    gA2lAddrBase = NULL;
    gA2lAddrIndex = 0;
    gAl2AddrExt = XCP_UNDEFINED_ADDR_EXT;
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
        fprintf(gA2lFile, "\n/* Segment relative addressing mode: calseg=%s */\n", calseg->name);
#else
        A2lSetAbsAddrMode(XCP_UNDEFINED_EVENT_ID);
        fprintf(gA2lFile, "\n/* Absolute segment addressing mode: calseg=%s */\n", calseg->name);
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
        fprintf(gA2lFile, "\n/* Segment relative addressing mode: calseg=%s */\n", calseg->name);
#else
        A2lSetAbsAddrMode(XCP_UNDEFINED_EVENT_ID);
        fprintf(gA2lFile, "\n/* Absolute segment addressing mode: calseg=%s */\n", calseg->name);
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
        const char *event_name = XcpGetEventName(event_id);
        if (XcpGetEventIndex(event_id) == 0) {
            // Use the event name as group name if the event index is 0
            A2lBeginGroup(event_name, "Measurement event group", false);
        } else {
            // Use the event name with index as group name if the event index is not 0
            char group_name[64];
            SNPRINTF(group_name, sizeof(group_name), "%s_%u", event_name, XcpGetEventIndex(event_id));
            A2lBeginGroup(group_name, "Measurement event group", false);
        }
    }
}

// Set relative address mode with event name or event id
// Will result in using ADDR_EXT_DYN for user defined base, ADDR_EXT_REL is used for stack frame relative addressing
void A2lSetRelativeAddrMode__s(const char *event_name, uint8_t i, const uint8_t *base_addr) {
    if (gA2lFile != NULL) {
        tXcpEventId event_id = XcpFindEvent(event_name, NULL);
        assert(event_id != XCP_UNDEFINED_EVENT_ID);
        A2lSetDynAddrMode(event_id, i, (uint8_t *)base_addr);
        beginEventGroup(event_id);
        fprintf(gA2lFile, "\n/* Relative addressing mode: event=%s (%u), addr_ext=%u */\n", event_name, event_id, gAl2AddrExt);
    }
}
void A2lSetRelativeAddrMode__i(tXcpEventId event_id, uint8_t i, const uint8_t *base_addr) {
    if (gA2lFile != NULL) {
        const char *event_name = XcpGetEventName(event_id);
        assert(event_name != NULL);
        A2lSetDynAddrMode(event_id, i, (uint8_t *)base_addr);
        beginEventGroup(event_id);
        fprintf(gA2lFile, "\n/* Relative addressing mode: event=%s (%u), addr_ext=%u */\n", event_name, event_id, gAl2AddrExt);
    }
}

// Set stack frame relative address mode with event name or event id
// Will result in using ADDR_EXT_REL
void A2lSetStackAddrMode__s(const char *event_name, const uint8_t *stack_frame) {
    if (gA2lFile != NULL) {
        tXcpEventId event_id = XcpFindEvent(event_name, NULL);
        assert(event_id != XCP_UNDEFINED_EVENT_ID);
        A2lSetDynAddrMode(event_id, 0, stack_frame);
        beginEventGroup(event_id);
        fprintf(gA2lFile, "\n/* Stack frame relative addressing mode: event=%s (%u), addr_ext=%u */\n", event_name, event_id, gAl2AddrExt);
    }
}
void A2lSetStackAddrMode__i(tXcpEventId event_id, const uint8_t *stack_frame) {
    if (gA2lFile != NULL) {
        const char *event_name = XcpGetEventName(event_id);
        assert(event_name != NULL);
        A2lSetDynAddrMode(event_id, 0, stack_frame);
        beginEventGroup(event_id);
        fprintf(gA2lFile, "\n/* Stack frame relative addressing mode: event=%s (%u), addr_ext=%u */\n", event_name, event_id, gAl2AddrExt);
    }
}

// Set absolute address mode with with default event name or event id (optional)
void A2lSetAbsoluteAddrMode__s(const char *event_name) {
    if (gA2lFile != NULL) {
        tXcpEventId event_id = XcpFindEvent(event_name, NULL);
        A2lSetAbsAddrMode(event_id);
        if (event_id != XCP_UNDEFINED_EVENT_ID) {
            beginEventGroup(event_id);
            fprintf(gA2lFile, "\n/* Absolute addressing mode: default_event=%s (%u), addr_ext=%u */\n", event_name, event_id, gAl2AddrExt);
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
            fprintf(gA2lFile, "\n/* Stack frame absolute addressing mode: event=%s (%u), addr_ext=%u */\n", event_name, event_id, gAl2AddrExt);
        }
    }
}
#endif

//----------------------------------------------------------------------------------
// Address encoding

uint8_t A2lGetAddrExt_(void) { return gAl2AddrExt; }

uint32_t A2lGetAddr_(const void *p) {

    if (gA2lFile != NULL) {

        if (XcpAddrIsAbs(gAl2AddrExt)) {
            return XcpAddrEncodeAbs(p);
        }

#ifdef XCP_ENABLE_REL_ADDRESSING
        else if (XcpAddrIsRel(gAl2AddrExt)) {
            uint64_t addr_diff = 0;
            if (gA2lAddrBase != NULL) {
                addr_diff = (uint64_t)p - (uint64_t)gA2lAddrBase;
                // Ensure the address difference does not overflow the value range for signed int32_t
                uint64_t addr_high = (addr_diff >> 32);
                if (addr_high != 0 && addr_high != 0xFFFFFFFF) {
                    DBG_PRINTF_ERROR("A2L rel address overflow detected! addr: %p, base: %p\n", p, (void *)gA2lAddrBase);
                    assert(0);
                    return 0;
                }
            }
            return XcpAddrEncodeRel(addr_diff);
        }
#endif
        else if (XcpAddrIsDyn(gAl2AddrExt)) {
            uint64_t addr_diff = 0;
            if (gA2lAddrBase != NULL) {
                addr_diff = (uint64_t)p - (uint64_t)gA2lAddrBase;
                // Ensure the address difference does not overflow the value range for signed int16_t
                uint64_t addr_high = (addr_diff >> 16);
                if (addr_high != 0 && addr_high != 0xFFFFFFFFFFFF) {
                    DBG_PRINTF_ERROR("A2L dyn address overflow detected! addr: %p, base: %p\n", p, (void *)gA2lAddrBase);
                    assert(0);
                    return 0;
                }
            }
            return XcpAddrEncodeDyn(addr_diff, gA2lFixedEvent);
        } else
#ifdef XCP_ENABLE_CALSEG_LIST
            if (XcpAddrIsSeg(gAl2AddrExt)) {
            uint64_t addr_diff = 0;
            if (gA2lAddrBase != NULL) {
                addr_diff = (uint64_t)p - (uint64_t)gA2lAddrBase;
                // Ensure the relative address does not overflow the 16 Bit A2L address offset for calibration segment relative addressing
                if ((addr_diff >> 16) != 0) {
                    DBG_PRINTF_ERROR("A2L seg relative address overflow detected! addr: %p, base: %p\n", p, (void *)gA2lAddrBase);
                    assert(0);
                    return 0;
                }
            }
            return XcpGetCalSegBaseAddress(gA2lAddrIndex) + (addr_diff & 0xFFFF);
        } else
#endif
        {
            DBG_PRINTF_ERROR("A2L address extension %u is not supported!\n", gAl2AddrExt);
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
void A2lTypedefBegin_(const char *name, uint32_t size, const char *comment) {
    if (gA2lFile != NULL) {
        fprintf(gA2lFile, "\n/begin TYPEDEF_STRUCTURE %s \"%s\" 0x%X\n", name, comment, size);
        gA2lTypedefs++;
    }
}

// End a typedef structure
void A2lTypedefEnd_(void) {
    if (gA2lFile != NULL) {
        fprintf(gA2lFile, "/end TYPEDEF_STRUCTURE\n");
    }
}

// For scalar or one dimensional measurement and parameter components of specified type
// type_name is the name of another typedef, typedef_measurement or typedef_characteristic
void A2lTypedefComponent_(const char *name, const char *type_name, uint16_t x_dim, uint32_t offset) {
    if (gA2lFile != NULL) {
        fprintf(gA2lFile, "  /begin STRUCTURE_COMPONENT %s %s 0x%X", name, type_name, offset);
        if (x_dim > 1)
            fprintf(gA2lFile, " MATRIX_DIM %u", x_dim);
        fprintf(gA2lFile, " /end STRUCTURE_COMPONENT\n");
        gA2lComponents++;
    }
}

// For measurement components with TYPEDEF_MEASUREMENT for fields with comment, unit, min, max
void A2lTypedefMeasurementComponent_(const char *name, const char *type_name, uint32_t offset, const char *comment, const char *unit_or_conversion, double min, double max) {
    if (gA2lFile != NULL) {
        // TYPEDEF_MEASUREMENT
        const char *conv = getConversion(unit_or_conversion, NULL, NULL);
        assert(gA2lTypedefsFile != NULL);
        fprintf(gA2lTypedefsFile, "/begin TYPEDEF_MEASUREMENT M_%s \"%s\" %s %s 0 0 %g %g", name, comment, type_name, conv, min, max);
        printPhysUnit(gA2lTypedefsFile, unit_or_conversion);
        fprintf(gA2lTypedefsFile, " /end TYPEDEF_MEASUREMENT\n");

        // STRUCTURE_COMPONENT
        fprintf(gA2lFile, "  /begin STRUCTURE_COMPONENT %s M_%s 0x%X /end STRUCTURE_COMPONENT\n", name, name, offset);

        gA2lComponents++;
    }
}

// For multidimensional parameter components with TYPEDEF_CHARACTERISTIC for fields with comment, unit, min, max
void A2lTypedefParameterComponent_(const char *name, const char *type_name, uint16_t x_dim, uint16_t y_dim, uint32_t offset, const char *comment, const char *unit_or_conversion,
                                   double min, double max, const char *x_axis, const char *y_axis) {
    if (gA2lFile != NULL) {

        assert(gA2lTypedefsFile != NULL);

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

void A2lCreateTypedefMeasurementInstance_(const char *instance_name, const char *typeName, uint16_t x_dim, uint8_t ext, uint32_t addr, const char *comment) {
    if (gA2lFile != NULL) {
        if (gA2lAutoGroups) {
            A2lAddToGroup(instance_name);
        }
        fprintf(gA2lFile, "/begin INSTANCE %s \"%s\" %s 0x%X", instance_name, comment, typeName, addr);
        printAddrExt(ext);
        if (x_dim > 1)
            fprintf(gA2lFile, " MATRIX_DIM %u", x_dim);
        if (XcpAddrIsAbs(gAl2AddrExt) || XcpAddrIsDyn(gAl2AddrExt)) { // Absolute and dynamic mode allows write access
            fprintf(gA2lFile, " READ_WRITE");
        }
        A2lCreateMeasurement_IF_DATA();
        fprintf(gA2lFile, " /end INSTANCE\n");
        gA2lInstances++;
    }
}

void A2lCreateTypedefParameterInstance_(const char *instance_name, const char *typeName, uint8_t ext, uint32_t addr, const char *comment) {
    if (gA2lFile != NULL) {
        if (gA2lAutoGroups) {
            A2lAddToGroup(instance_name);
        }
        fprintf(gA2lFile, "/begin INSTANCE %s \"%s\" %s 0x%X", instance_name, comment, typeName, addr);
        printAddrExt(ext);
        fprintf(gA2lFile, " /end INSTANCE\n");
        gA2lInstances++;
    }
}

//----------------------------------------------------------------------------------
// Measurements

void A2lCreateMeasurement_(const char *instance_name, const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, const char *unit_or_conversion, double phys_min,
                           double phys_max, const char *comment) {

    if (gA2lFile != NULL) {
        const char *symbol_name = A2lGetSymbolName(instance_name, name);
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
        if (XcpAddrIsAbs(gAl2AddrExt) || XcpAddrIsDyn(gAl2AddrExt)) { // Absolute and dynamic mode allows write access
            fprintf(gA2lFile, " READ_WRITE");
        }
        A2lCreateMeasurement_IF_DATA();
        fprintf(gA2lFile, " /end MEASUREMENT\n");
        gA2lMeasurements++;
    }
}

void A2lCreateMeasurementArray_(const char *instance_name, const char *name, tA2lTypeId type, int x_dim, int y_dim, uint8_t ext, uint32_t addr, const char *unit_or_conversion,
                                double phys_min, double phys_max, const char *comment) {
    if (gA2lFile != NULL) {
        const char *symbol_name = A2lGetSymbolName(instance_name, name);
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

void A2lCreateParameter_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, const char *comment, const char *unit_or_conversion, double phys_min, double phys_max) {

    if (gA2lFile != NULL) {
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

void A2lCreateMap_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, uint32_t xdim, uint32_t ydim, const char *comment, const char *unit, double min, double max,
                   const char *x_axis, const char *y_axis) {

    if (gA2lFile != NULL) {
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

void A2lCreateCurve_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, uint32_t xdim, const char *comment, const char *unit, double min, double max,
                     const char *x_axis) {

    if (gA2lFile != NULL) {
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

void A2lCreateAxis_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, uint32_t xdim, const char *comment, const char *unit, double min, double max) {

    if (gA2lFile != NULL) {
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
            fprintf(gA2lGroupsFile, "/begin GROUP %s \"%s\"", name, comment);
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
        if (atomic_compare_exchange_strong_explicit((A2L_ONCE_ATOMIC_TYPE *)value, &old_value, 1, memory_order_relaxed, memory_order_relaxed)) {
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
bool A2lCheckFinalizeOnConnect(void) {

    // Finalize A2l once on connect
    if (gA2lFinalizeOnConnect) {
        if (gA2lFileFinalized) {
            return true; // A2L file already finalized, allow connect
        } else {
            return A2lFinalize(); // Finalize A2L file generation
        }
    } else {

        // If A2l generation is active, refuse connect
        if (gA2lFile != NULL) {
            DBG_PRINT_WARNING("A2L file not finalized, XCP connect refused!\n");
            return false; // Refuse connect
        }
    }

    return true; // Do not refuse connect
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
        tXcpEventList *eventList = XcpGetEventList();
        if (eventList != NULL && eventList->count > 0) {

            // Create a enum conversion with all event ids
            fprintf(gA2lConversionsFile, "/begin COMPU_METHOD conv.events \"\" TAB_VERB \"%%.0 \" \"\" COMPU_TAB_REF conv.events.table /end COMPU_METHOD\n");
            fprintf(gA2lConversionsFile, "/begin COMPU_VTAB conv.events.table \"\" TAB_VERB %u\n", eventList->count);
            for (uint32_t id = 0; id < eventList->count; id++) {
                tXcpEvent *event = &eventList->event[id];
                fprintf(gA2lConversionsFile, " %u \"%s\"", id, event->name);
            }
            fprintf(gA2lConversionsFile, "\n/end COMPU_VTAB\n");

            // Create a sub group for all events
            if (gA2lAutoGroups) {
                fprintf(gA2lGroupsFile, "/begin GROUP Events \"Events\" ROOT /begin SUB_GROUP");
                for (uint32_t id = 0; id < eventList->count; id++) {
                    tXcpEvent *event = &eventList->event[id];
                    uint16_t index = event->index;
                    const char *name = event->name;
                    if (index <= 1) {
                        fprintf(gA2lGroupsFile, " %s", name);
                    }
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
        gA2lFileFinalized = true;

        DBG_PRINTF3("A2L created: %u measurements, %u params, %u typedefs, %u components, %u instances, %u conversions\n", gA2lMeasurements, gA2lParameters, gA2lTypedefs,
                    gA2lComponents, gA2lInstances, gA2lConversions);

        // Write the binary persistence file if calsegment list and DAQ event list are enabled
#ifdef OPTION_CAL_PERSISTENCE
        if (!gA2lWriteAlways)
            XcpBinWrite(gBinFilename);
#endif

        // Notify XCP that there is an A2L file available for upload by the XCP client
        ApplXcpSetA2lName(gA2lFilename);

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
bool A2lInit(const char *a2l_projectname, const char *a2l_version, const uint8_t *addr, uint16_t port, bool useTCP, uint8_t mode) {

    assert(a2l_projectname != NULL);
    assert(addr != NULL);

    // Check and ignore, if the XCP singleton has not been initialized and activated
    if (!XcpIsActivated()) {
        DBG_PRINT3("A2lInit: XCP not initialized and activated!\n");
        return true;
    }

    // Save communication parameters
    memcpy(&gA2lOptionBindAddr, addr, 4);
    gA2lOptionPort = port;
    gA2lUseTCP = useTCP;

    // Save mode
    gA2lWriteAlways = mode & A2L_MODE_WRITE_ALWAYS;
#ifndef OPTION_CAL_PERSISTENCE
    assert(gA2lWriteAlways);
#endif
    gA2lAutoGroups = mode & A2L_MODE_AUTO_GROUPS;
    gA2lFinalizeOnConnect = mode & A2L_MODE_FINALIZE_ON_CONNECT;

    mutexInit(&gA2lMutex, false, 0);

    // EPK generation if not provided
    // Set the EPK (software version number) for the A2L file
    char epk[64];
    if (a2l_version == NULL) {
        SNPRINTF(epk, sizeof(epk), "_%s_%s", __DATE__, __TIME__);
    } else {
        SNPRINTF(epk, sizeof(epk), "_%s", a2l_version);
    }
    XcpSetEpk(epk);

    // Build filenames
    // If A2l file is build once for a new build, the EPK is appended to the filename
    const char *epk_suffix = gA2lWriteAlways ? "" : XcpGetEpk();
    SNPRINTF(gA2lFilename, sizeof(gA2lFilename), "%s%s.a2l", a2l_projectname, epk_suffix);
    DBG_PRINTF3("Start A2L generator, file=%s, write_always=%u, finalize_on_connect=%u, auto_groups=%u\n", gA2lFilename, gA2lWriteAlways, gA2lFinalizeOnConnect, gA2lAutoGroups);

    // Check if the BIN file and the A2L exists and load the binary file
#ifdef OPTION_CAL_PERSISTENCE
    SNPRINTF(gBinFilename, sizeof(gBinFilename), "%s%s.bin", a2l_projectname, epk_suffix);
    if (!gA2lWriteAlways) {

        if (fexists(gA2lFilename)) {
            if (XcpBinLoad(gBinFilename, XcpGetEpk())) {
                DBG_PRINTF3("Loaded binary file %s, A2L generation has been disabled\n", gBinFilename);

                // Notify XCP that there is an A2L file available for upload by the XCP client
                ApplXcpSetA2lName(gA2lFilename);

                return true; // Do not generate A2L, but still provide the existing file, if binary file exists
            }
        }
    }
#endif

    // Open A2L file for generation
    if (!A2lOpen(gA2lFilename, a2l_projectname)) {
        printf("Failed to open A2L file %s\n", gA2lFilename);
        return false;
    }

    // Register a callback on XCP connect
    ApplXcpRegisterConnectCallback(A2lCheckFinalizeOnConnect);

    return true;
}
