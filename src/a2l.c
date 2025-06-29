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

#include <assert.h>  // for assert
#include <stdarg.h>  // for va_
#include <stdbool.h> // for bool
#include <stdint.h>  // for uintxx_t
#include <stdio.h>   // for fclose, fopen, fread, fseek, ftell
#include <string.h>  // for strlen, strncpy

#include "dbg_print.h" // for DBG_PRINTF3, DBG_PRINT4, DBG_PRINTF4, DBG...
#include "main_cfg.h"  // for OPTION_xxx
#include "platform.h"  // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex
#include "xcp.h"       // for CRC_XXX
#include "xcpLite.h"   // for tXcpDaqLists, XcpXxx, ApplXcpXxx, ...
#include "xcp_cfg.h"   // for XCP_xxx
#include "xcptl_cfg.h" // for XCPTL_xxx

MUTEX gA2lMutex;

static FILE *gA2lFile = NULL;
static FILE *gA2lTypedefsFile = NULL;
static FILE *gA2lGroupsFile = NULL;

static FILE *gA2lConversionsFile = NULL;
static char gA2lConvName[256];
static double gA2lConvFactor = 1.0;
static double gA2lConvOffset = 0.0;

static char gA2lAutoGroups = true;            // Automatically create groups for measurements and parameters
static const char *gA2lAutoGroupName = NULL;  // Current open group
static bool gA2lAutoGroupIsParameter = false; // Group is a parameter group

static bool gA2lUseTCP = false;
static uint16_t gA2lOptionPort = 5555;
static uint8_t gA2lOptionBindAddr[4] = {0, 0, 0, 0};

static tXcpEventId gA2lFixedEvent = XCP_UNDEFINED_EVENT_ID;
static tXcpEventId gA2lDefaultEvent = XCP_UNDEFINED_EVENT_ID;
static uint8_t gAl2AddrExt = XCP_ADDR_EXT_ABS; // Address extension
static const uint8_t *gA2lAddrBase = NULL;     // Event or calseg address for XCP_ADDR_EXT_REL, XCP_ADDR_EXT_SEG
static tXcpCalSegIndex gA2lAddrIndex = 0;      // Segment index for XCP_ADDR_EXT_SEG

static uint32_t gA2lMeasurements;
static uint32_t gA2lParameters;
static uint32_t gA2lTypedefs;
static uint32_t gA2lComponents;
static uint32_t gA2lInstances;
static uint32_t gA2lConversions;

//----------------------------------------------------------------------------------
static const char *gA2lHeader = "ASAP2_VERSION 1 71\n"
                                "/begin PROJECT %s \"\"\n\n"
                                "/begin HEADER \"\" VERSION \"1.0\" PROJECT_NO VECTOR /end HEADER\n\n"
                                "/begin MODULE %s \"\"\n\n"
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
                                "\n\n";

//----------------------------------------------------------------------------------
static const char *gA2lMemorySegment = "/begin MEMORY_SEGMENT\n"
                                       "%s \"\" DATA FLASH INTERN 0x%08X 0x%X -1 -1 -1 -1 -1\n" // name, start, size
                                       "/begin IF_DATA XCP\n"
                                       "/begin SEGMENT 0x1 0x2 0x0 0x0 0x0\n"
                                       "/begin CHECKSUM XCP_ADD_44 MAX_BLOCK_SIZE 0xFFFF EXTERNAL_FUNCTION \"\" /end CHECKSUM\n"
                                       // 2 calibration pages, 0=working page (RAM), 1=initial readonly page (FLASH), independent access to ECU and XCP page possible
                                       "/begin PAGE 0x1 ECU_ACCESS_DONT_CARE XCP_READ_ACCESS_DONT_CARE XCP_WRITE_ACCESS_NOT_ALLOWED /end PAGE\n"
                                       "/begin PAGE 0x0 ECU_ACCESS_DONT_CARE XCP_READ_ACCESS_DONT_CARE XCP_WRITE_ACCESS_DONT_CARE /end PAGE\n"
                                       "/end SEGMENT\n"
                                       "/end IF_DATA\n"
                                       "/end MEMORY_SEGMENT\n";

static const char *gA2lEpkMemorySegment = "/begin MEMORY_SEGMENT epk \"\" DATA FLASH INTERN 0x80000000 %u -1 -1 -1 -1 -1 /end MEMORY_SEGMENT\n";

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
//"OPTIONAL_CMD CC_GET_PAG_PROCESSOR_INFO\n"
//"OPTIONAL_CMD CC_GET_SEGMENT_INFO\n"
//"OPTIONAL_CMD CC_GET_PAGE_INFO\n"
//"OPTIONAL_CMD CC_SET_SEGMENT_MODE\n"
//"OPTIONAL_CMD CC_GET_SEGMENT_MODE\n"
#endif
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
    "DYNAMIC 0 %u 0 OPTIMISATION_TYPE_DEFAULT ADDRESS_EXTENSION_FREE IDENTIFICATION_FIELD_TYPE_RELATIVE_BYTE GRANULARITY_ODT_ENTRY_SIZE_DAQ_BYTE 0xF8 OVERLOAD_INDICATION_PID\n"
    "/begin TIMESTAMP_SUPPORTED\n"
    "0x1 SIZE_DWORD %s TIMESTAMP_FIXED\n"
    "/end TIMESTAMP_SUPPORTED\n";

// ... Event list follows, before EndDaq

//----------------------------------------------------------------------------------
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
        break;
    case A2L_TYPE_INT16:
        return "SWORD";
        break;
    case A2L_TYPE_INT32:
        return "SLONG";
        break;
    case A2L_TYPE_INT64:
        return "A_INT64";
        break;
    case A2L_TYPE_UINT8:
        return "UBYTE";
        break;
    case A2L_TYPE_UINT16:
        return "UWORD";
        break;
    case A2L_TYPE_UINT32:
        return "ULONG";
        break;
    case A2L_TYPE_UINT64:
        return "A_UINT64";
        break;
    case A2L_TYPE_FLOAT:
        return "FLOAT32_IEEE";
        break;
    case A2L_TYPE_DOUBLE:
        return "FLOAT64_IEEE";
        break;
    default:
        return NULL;
    }
}

const char *A2lGetA2lTypeName_M(tA2lTypeId type) {
    switch (type) {
    case A2L_TYPE_INT8:
        return "M_I8";
        break;
    case A2L_TYPE_INT16:
        return "M_I16";
        break;
    case A2L_TYPE_INT32:
        return "M_I32";
        break;
    case A2L_TYPE_INT64:
        return "M_I64";
        break;
    case A2L_TYPE_UINT8:
        return "M_U8";
        break;
    case A2L_TYPE_UINT16:
        return "M_U16";
        break;
    case A2L_TYPE_UINT32:
        return "M_U32";
        break;
    case A2L_TYPE_UINT64:
        return "M_U64";
        break;
    case A2L_TYPE_FLOAT:
        return "M_F32";
        break;
    case A2L_TYPE_DOUBLE:
        return "M_F64";
        break;
    default:
        return NULL;
    }
}

const char *A2lGetA2lTypeName_C(tA2lTypeId type) {

    switch (type) {
    case A2L_TYPE_INT8:
        return "C_I8";
        break;
    case A2L_TYPE_INT16:
        return "C_I16";
        break;
    case A2L_TYPE_INT32:
        return "C_I32";
        break;
    case A2L_TYPE_INT64:
        return "C_I64";
        break;
    case A2L_TYPE_UINT8:
        return "C_U8";
        break;
    case A2L_TYPE_UINT16:
        return "C_U16";
        break;
    case A2L_TYPE_UINT32:
        return "C_U32";
        break;
    case A2L_TYPE_UINT64:
        return "C_U64";
        break;
    case A2L_TYPE_FLOAT:
        return "C_F32";
        break;
    case A2L_TYPE_DOUBLE:
        return "C_F64";
        break;
    default:
        return NULL;
    }
}

const char *A2lGetRecordLayoutName_(tA2lTypeId type) {

    switch (type) {
    case A2L_TYPE_INT8:
        return "I8";
        break;
    case A2L_TYPE_INT16:
        return "I16";
        break;
    case A2L_TYPE_INT32:
        return "I32";
        break;
    case A2L_TYPE_INT64:
        return "I64";
        break;
    case A2L_TYPE_UINT8:
        return "U8";
        break;
    case A2L_TYPE_UINT16:
        return "U16";
        break;
    case A2L_TYPE_UINT32:
        return "U32";
        break;
    case A2L_TYPE_UINT64:
        return "U64";
        break;
    case A2L_TYPE_FLOAT:
        return "F32";
        break;
    case A2L_TYPE_DOUBLE:
        return "F64";
        break;
    default:
        return NULL;
    }
}

static const char *getTypeMinString(tA2lTypeId type) {
    const char *min;
    switch (type) {
    case A2L_TYPE_INT8:
        min = "-128";
        break;
    case A2L_TYPE_INT16:
        min = "-32768";
        break;
    case A2L_TYPE_INT32:
        min = "-2147483648";
        break;
    case A2L_TYPE_INT64:
        min = "-1e12";
        break;
    case A2L_TYPE_FLOAT:
        min = "-1e12";
        break;
    case A2L_TYPE_DOUBLE:
        min = "-1e12";
        break;
    default:
        min = "0";
    }
    return min;
}

static const char *getTypeMaxString(tA2lTypeId type) {
    const char *max;
    switch (type) {
    case A2L_TYPE_INT8:
        max = "127";
        break;
    case A2L_TYPE_INT16:
        max = "32767";
        break;
    case A2L_TYPE_INT32:
        max = "2147483647";
        break;
    case A2L_TYPE_UINT8:
        max = "255";
        break;
    case A2L_TYPE_UINT16:
        max = "65535";
        break;
    case A2L_TYPE_UINT32:
        max = "4294967295";
        break;
    default:
        max = "1e12";
    }
    return max;
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
        min = -2147483648;
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

    DBG_PRINTF3("A2L create %s\n", filename);

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

    // Notify XCP that there is an A2L file available for upload by the XCP client tool
    ApplXcpSetA2lName(filename);

    // Create headers
    fprintf(gA2lFile, gA2lHeader, projectname, projectname); // main file

    fprintf(gA2lTypedefsFile, "\n/* Typedefs */\n");       // typedefs temporary file
    fprintf(gA2lGroupsFile, "\n/* Groups */\n");           // groups temporary file
    fprintf(gA2lConversionsFile, "\n/* Conversions */\n"); // conversions temporary file

    // Create predefined conversions
    // In the conversions.a2l file - will be merges later as there might be more conversions during the generation process
    fprintf(gA2lConversionsFile, "/begin COMPU_METHOD conv.BOOL \"\" TAB_VERB \"%%.0\" \"\" COMPU_TAB_REF conv.BOOL.table /end COMPU_METHOD\n");
    fprintf(gA2lConversionsFile, "/begin COMPU_VTAB conv.BOOL.table \"\" TAB_VERB 2 0 \"false\" 1 \"true\" /end COMPU_VTAB\n");
    fprintf(gA2lConversionsFile, "\n");

    // Create predefined standard record layouts and typedefs for elementary types
    // In the typedefs.a2l file - will be merges later as there might be more typedefs during the generation process
    for (int i = -10; i <= +10; i++) {
        tA2lTypeId id = (tA2lTypeId)i;
        const char *at = A2lGetA2lTypeName(id);
        if (at != NULL) {
            const char *t = A2lGetRecordLayoutName_(id);
            // RECORD_LAYOUTs for standard types U8,I8,...,F64 (Position 1 increasing index)
            // Example: /begin RECORD_LAYOUT U64 FNC_VALUES 1 A_UINT64 ROW_DIR DIRECT /end RECORD_LAYOUT
            fprintf(gA2lTypedefsFile, "/begin RECORD_LAYOUT %s FNC_VALUES 1 %s ROW_DIR DIRECT /end RECORD_LAYOUT\n", t, at);
            // RECORD_LAYOUTs for axis points with standard types A_U8,A_I8,... (Positionn 1 increasing index)
            // Example: /begin RECORD_LAYOUT A_F32 AXIS_PTS_X 1 FLOAT32_IEEE INDEX_INCR DIRECT /end RECORD_LAYOUT
            fprintf(gA2lTypedefsFile, "/begin RECORD_LAYOUT A_%s AXIS_PTS_X 1 %s INDEX_INCR DIRECT /end RECORD_LAYOUT\n", t, at);
            // Example: /begin TYPEDEF_MEASUREMENT M_F64 "" FLOAT64_IEEE NO_COMPU_METHOD 0 0 -1e12 1e12 /end TYPEDEF_MEASUREMENT
            fprintf(gA2lTypedefsFile, "/begin TYPEDEF_MEASUREMENT M_%s \"\" %s NO_COMPU_METHOD 0 0 %s %s /end TYPEDEF_MEASUREMENT\n", t, at, getTypeMinString(id),
                    getTypeMaxString(id));
            // Example: /begin TYPEDEF_CHARACTERISTIC C_U8 "" VALUE U8 0 NO_COMPU_METHOD 0 255 /end TYPEDEF_CHARACTERISTIC
            fprintf(gA2lTypedefsFile, "/begin TYPEDEF_CHARACTERISTIC C_%s \"\" VALUE %s 0 NO_COMPU_METHOD %s %s /end TYPEDEF_CHARACTERISTIC\n", t, t, getTypeMinString(id),
                    getTypeMaxString(id));
        }
    }
    fprintf(gA2lTypedefsFile, "\n");

    return true;
}

// Memory segments
static void A2lCreate_MOD_PAR(void) {
    if (gA2lFile != NULL) {

#ifdef XCP_ENABLE_CALSEG_LIST
        fprintf(gA2lFile, "\n/begin MOD_PAR \"\"\n");
        const char *epk = XcpGetEpk();
        if (epk) {
            fprintf(gA2lFile, "EPK \"%s\" ADDR_EPK 0x80000000\n", epk);
            fprintf(gA2lFile, gA2lEpkMemorySegment, strlen(epk));
        }
        // Calibration segments are implicitly indexed
        // The segment number used in XCP commands XCP_SET_CAL_PAGE, GET_CAL_PAGE, XCP_GET_SEGMENT_INFO, ... are the indices of the segments starting with 0
        tXcpCalSegList const *calSegList = XcpGetCalSegList();
        for (uint32_t i = 0; i < calSegList->count; i++) {
            tXcpCalSeg const *calseg = &calSegList->calseg[i];
            fprintf(gA2lFile, gA2lMemorySegment, calseg->name, ((i + 1) << 16) | 0x80000000, calseg->size);
        }

        fprintf(gA2lFile, "/end MOD_PAR\n\n");
    }
#endif
}

static void A2lCreate_IF_DATA_DAQ(void) {

#if defined(XCP_ENABLE_DAQ_EVENT_LIST) && !defined(XCP_ENABLE_DAQ_EVENT_INFO)
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
#if defined(XCP_ENABLE_DAQ_EVENT_LIST) && !defined(XCP_ENABLE_DAQ_EVENT_INFO)
    eventList = XcpGetEventList();
    eventCount = eventList->count;
#endif

    fprintf(gA2lFile, gA2lIfDataBeginDAQ, eventCount, XCP_TIMESTAMP_UNIT_S);

    // Eventlist
#if defined(XCP_ENABLE_DAQ_EVENT_LIST) && !defined(XCP_ENABLE_DAQ_EVENT_INFO)
    for (uint32_t id = 0; id < eventCount; id++) {
        tXcpEvent *event = &eventList->event[id];
        uint16_t index = event->index;
        const char *name = event->name;
        if (index == 0) {
            fprintf(gA2lFile, "/begin EVENT \"%s\" \"%s\" 0x%X DAQ 0xFF %u %u %u CONSISTENCY EVENT", name, name, id, event->timeCycle, event->timeUnit, event->priority);
        } else {
            fprintf(gA2lFile, "/begin EVENT \"%s_%u\" \"%s_%u\" 0x%X DAQ 0xFF %u %u %u CONSISTENCY EVENT", name, index, name, index, id, event->timeCycle, event->timeUnit,
                    event->priority);
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
            socketGetLocalAddr(NULL, addr0);
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
        if (gAl2AddrExt == XCP_ADDR_EXT_REL || gAl2AddrExt == XCP_ADDR_EXT_DYN) {
            if (gA2lFixedEvent != XCP_UNDEFINED_EVENT_ID) {
                fprintf(gA2lFile, " /begin IF_DATA XCP /begin DAQ_EVENT FIXED_EVENT_LIST EVENT 0x%X /end DAQ_EVENT /end IF_DATA", gA2lFixedEvent);
            } else {
                assert(false); // Fixed event must be set before calling this function
            }
        }
        if (gAl2AddrExt == XCP_ADDR_EXT_ABS) {
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
// Mode

void A2lSetSegAddrMode_(tXcpCalSegIndex calseg_index, const uint8_t *calseg_instance_addr) {
    gA2lAddrIndex = calseg_index;
    gA2lAddrBase = calseg_instance_addr; // Address of a a the calibration segment instance which is used in the macros to create the components
    gAl2AddrExt = XCP_ADDR_EXT_SEG;
}

void A2lSetAbsAddrMode_(tXcpEventId default_event_id) {
    gA2lFixedEvent = XCP_UNDEFINED_EVENT_ID;
    gA2lDefaultEvent = default_event_id;
    gAl2AddrExt = XCP_ADDR_EXT_ABS;
}

void A2lSetRelAddrMode_(tXcpEventId event_id, const uint8_t *base) {
    gA2lAddrBase = base;
    gA2lFixedEvent = event_id;
    gA2lDefaultEvent = XCP_UNDEFINED_EVENT_ID;
    gAl2AddrExt = XCP_ADDR_EXT_REL;
}

void A2lSetDynAddrMode_(tXcpEventId event_id, const uint8_t *base) {
    gA2lAddrBase = base;
    gA2lFixedEvent = event_id;
    gA2lDefaultEvent = XCP_UNDEFINED_EVENT_ID;
    gAl2AddrExt = XCP_ADDR_EXT_DYN;
}

void A2lRstAddrMode(void) {
    gA2lFixedEvent = XCP_UNDEFINED_EVENT_ID;
    gA2lDefaultEvent = XCP_UNDEFINED_EVENT_ID;
    gA2lAddrBase = NULL;
    gA2lAddrIndex = 0;
    gAl2AddrExt = XCP_UNDEFINED_ADDR_EXT;
}

//----------------------------------------------------------------------------------
// Set addressing mode

#ifdef XCP_ENABLE_CALSEG_LIST

// Set relative address mode with calibration segment index
void A2lSetSegmentAddrMode_(tXcpCalSegIndex calseg_index, const uint8_t *calseg_instance_addr) {

    const tXcpCalSeg *calseg = XcpGetCalSeg(calseg_index);
    if (calseg == NULL) {
        DBG_PRINTF_ERROR("SetSegAddrMode: Calibration segment %u not found!\n", calseg_index);
        return;
    }

    A2lSetSegAddrMode_(calseg_index, (const uint8_t *)calseg_instance_addr);
    fprintf(gA2lFile, "\n/* Relative addressing mode: calseg=%s */\n", calseg->name);

    if (gA2lAutoGroups) {
        A2lBeginGroup(calseg->name, "Calibration Segment", true);
    }
}

#endif

#ifdef XCP_ENABLE_DAQ_EVENT_LIST

// Set relative address mode with event name
void A2lSetRelativeAddrMode_(const char *event_name, const uint8_t *base_addr) {

    assert(gA2lFile != NULL);

    tXcpEventId event = XcpFindEvent(event_name, NULL);
    if (event == XCP_UNDEFINED_EVENT_ID) {
        DBG_PRINTF_ERROR("SetRelativeAddrMode: Event %s not found!\n", event_name);
        return;
    }

    A2lSetDynAddrMode_(event, (uint8_t *)base_addr);

    if (gA2lAutoGroups) {
        A2lBeginGroup(event_name, "Measurement event group", false);
    }

    fprintf(gA2lFile, "\n/* Relative addressing mode: event=%s (%u), addr_ext=%u, addr_base=%p */\n", event_name, event, gAl2AddrExt, (void *)gA2lAddrBase);
}

#endif

// Set absolute address mode with fixed event name
void A2lSetAbsoluteAddrMode_(const char *event_name) {

    assert(gA2lFile != NULL);

    tXcpEventId event_id = XcpFindEvent(event_name, NULL);
    if (event_id == XCP_UNDEFINED_EVENT_ID) {
        DBG_PRINTF_ERROR("SetAbsoluteAddrMode: Event %s not found!\n", event_name);
        return;
    }

    A2lSetAbsAddrMode_(event_id);

    if (gA2lAutoGroups) {
        A2lBeginGroup(event_name, "Measurement event group", false);
    }

    fprintf(gA2lFile, "\n/* Absolute addressing mode: event=%s (%u), addr_ext=%u, addr_base=%p */\n", event_name, event_id, gAl2AddrExt, (void *)ApplXcpGetBaseAddr());
}

//----------------------------------------------------------------------------------
// Address encoding

uint8_t A2lGetAddrExt_(void) { return gAl2AddrExt; }

uint32_t A2lGetAddr_(const void *p) {

    switch (gAl2AddrExt) {
    case XCP_ADDR_EXT_ABS: {
        return ApplXcpGetAddr(p); // Calculate the XCP address from a pointer
    }
    case XCP_ADDR_EXT_REL: {
        uint64_t addr_diff = (uint64_t)p - (uint64_t)gA2lAddrBase;
        // Ensure the relative address does not overflow the address space
        uint64_t addr_high = (addr_diff >> 32);
        if (addr_high != 0 && addr_high != 0xFFFFFFFF) {
            DBG_PRINTF_ERROR("A2L XCP_ADDR_EXT_REL relative address overflow detected! addr: %p, base: %p\n", p, (void *)gA2lAddrBase);
            assert(0); // Ensure the relative address does not overflow the 32 Bit A2L address space
        }
        return (uint32_t)(addr_diff & 0xFFFFFFFF);
    }
    case XCP_ADDR_EXT_DYN: {
        uint64_t addr_diff = (uint64_t)p - (uint64_t)gA2lAddrBase;

        // Ensure the relative address does not overflow the address space
        uint64_t addr_high = (addr_diff >> 16);
        if (addr_high != 0 && addr_high != 0xFFFFFFFFFFFF) {
            DBG_PRINTF_ERROR("A2L XCP_ADDR_EXT_DYN relative address overflow detected! addr: %p, base: %p\n", p, (void *)gA2lAddrBase);
            assert(0); // Ensure the relative address does not overflow the 32 Bit A2L address space
        }
        return (uint32_t)(((uint32_t)gA2lFixedEvent) << 16 | (addr_diff & 0xFFFF));
    }
    case XCP_ADDR_EXT_SEG: {
        uint64_t addr_diff = (uint64_t)p - (uint64_t)gA2lAddrBase;
        // Ensure the relative address does not overflow the 16 Bit A2L address offset for calibration segment relative addressing
        assert((addr_diff >> 16) == 0);
        return XcpGetCalSegBaseAddress(gA2lAddrIndex) + (addr_diff & 0xFFFF);
    }
    }
    DBG_PRINTF_ERROR("A2L address extension %u is not supported!\n", gAl2AddrExt);
    assert(0);
}

//----------------------------------------------------------------------------------
// Conversions

void printPhysUnit(FILE *file, const char *unit_or_conversion) {

    // It is a phys unit if the string is not NULL and does not start with "conv."
    size_t len = strlen(unit_or_conversion);
    if (unit_or_conversion != NULL && !(len > 5 && strncmp(unit_or_conversion, "conv.", 5) == 0)) {
        fprintf(file, " PHYS_UNIT \"%s\"", unit_or_conversion);
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

    assert(gA2lFile != NULL);

    if (unit == NULL)
        unit = "";
    if (comment == NULL)
        comment = "";

    // Build the conversion name with prefix "conv." and store it in a static variable
    // Rememner factor and offset for later use
    SNPRINTF(gA2lConvName, sizeof(gA2lConvName), "conv.%s", name);
    gA2lConvFactor = factor;
    gA2lConvOffset = offset;

    fprintf(gA2lConversionsFile, "/begin COMPU_METHOD conv.%s \"%s\" LINEAR \"%%6.3\" \"%s\" COEFFS_LINEAR %g %g /end COMPU_METHOD\n", name, comment, unit, factor, offset);
    gA2lConversions++;

    // Return the conversion name for reference when creating measurements
    return gA2lConvName;
}

//----------------------------------------------------------------------------------
// Typedefs

// Begin a typedef structure
// Thread safe, but be aware of the mutex lock
void A2lTypedefBegin_(const char *name, uint32_t size, const char *comment) {

    assert(gA2lFile != NULL);
    mutexLock(&gA2lMutex);
    fprintf(gA2lFile, "/begin TYPEDEF_STRUCTURE %s \"%s\" 0x%X", name, comment, size);
    fprintf(gA2lFile, "\n");
    gA2lTypedefs++;
}

// End a typedef structure
void A2lTypedefEnd_(void) {

    assert(gA2lFile != NULL);
    fprintf(gA2lFile, "/end TYPEDEF_STRUCTURE\n");
    mutexUnlock(&gA2lMutex);
}

// For scalar measurement and parameter components
void A2lTypedefComponent_(const char *name, const char *type_name, uint16_t x_dim, uint32_t offset) {

    assert(gA2lFile != NULL);
    fprintf(gA2lFile, "  /begin STRUCTURE_COMPONENT %s %s 0x%X", name, type_name, offset);
    if (x_dim > 1)
        fprintf(gA2lFile, " MATRIX_DIM %u", x_dim);
    fprintf(gA2lFile, " /end STRUCTURE_COMPONENT\n");

    gA2lComponents++;
}

// For multidimensional parameter components with TYPEDEF_CHARACTERISTIC for fields with comment, unit, min, max
void A2lTypedefParameterComponent_(const char *name, const char *type_name, uint16_t x_dim, uint16_t y_dim, uint32_t offset, const char *comment, const char *unit, double min,
                                   double max, const char *x_axis, const char *y_axis) {

    assert(gA2lFile != NULL);

    // TYPEDEF_AXIS
    if (y_dim == 0 && x_dim > 1) {
        fprintf(gA2lTypedefsFile, "/begin TYPEDEF_AXIS A_%s \"%s\" NO_INPUT_QUANTITY A_%s 0 NO_COMPU_METHOD %u %g %g", name, comment, type_name, x_dim, min, max);
        printPhysUnit(gA2lTypedefsFile, unit);
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
            fprintf(gA2lTypedefsFile, "/begin TYPEDEF_CHARACTERISTIC C_%s \"%s\" VALUE %s 0 NO_COMPU_METHOD %g %g", name, comment, type_name, min, max);
        }
        //
        else {
            DBG_PRINTF_ERROR("Invalid dimensions: x_dim=%u, y_dim=%u\n", x_dim, y_dim);
            assert(0);
        }
        printPhysUnit(gA2lTypedefsFile, unit);
        fprintf(gA2lTypedefsFile, " /end TYPEDEF_CHARACTERISTIC\n");
        fprintf(gA2lFile, "  /begin STRUCTURE_COMPONENT %s C_%s 0x%X /end STRUCTURE_COMPONENT\n", name, name, offset);
    }

    gA2lComponents++;
}

void A2lCreateTypedefInstance_(const char *instance_name, const char *typeName, uint16_t x_dim, uint8_t ext, uint32_t addr, const char *comment) {

    assert(gA2lFile != NULL);

    if (gA2lAutoGroups) {
        A2lAddToGroup(instance_name);
    }

    mutexLock(&gA2lMutex);
    fprintf(gA2lFile, "/begin INSTANCE %s \"%s\" %s 0x%X", instance_name, comment, typeName, addr);
    printAddrExt(ext);
    if (x_dim > 1)
        fprintf(gA2lFile, " MATRIX_DIM %u", x_dim);
    A2lCreateMeasurement_IF_DATA();
    fprintf(gA2lFile, " /end INSTANCE\n");
    gA2lInstances++;
    mutexUnlock(&gA2lMutex);
}

//----------------------------------------------------------------------------------
// Measurements

void A2lCreateMeasurement_(const char *instance_name, const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, const char *unit_or_conversion, double phys_min,
                           double phys_max, const char *comment) {

    assert(gA2lFile != NULL);

    const char *symbol_name = A2lGetSymbolName(instance_name, name);
    if (gA2lAutoGroups) {
        A2lAddToGroup(name);
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
    if (gAl2AddrExt == XCP_ADDR_EXT_ABS || gAl2AddrExt == XCP_ADDR_EXT_DYN) { // Absolute or dynamic address mode allows write access
        fprintf(gA2lFile, " READ_WRITE");
    }

    A2lCreateMeasurement_IF_DATA();
    fprintf(gA2lFile, " /end MEASUREMENT\n");
    gA2lMeasurements++;
}

void A2lCreateMeasurementArray_(const char *instance_name, const char *name, tA2lTypeId type, int x_dim, int y_dim, uint8_t ext, uint32_t addr, const char *unit,
                                const char *comment) {

    assert(gA2lFile != NULL);

    if (gA2lAutoGroups) {
        A2lAddToGroup(name);
    }

    const char *symbol_name = A2lGetSymbolName(instance_name, name);
    if (unit == NULL)
        unit = "";
    if (comment == NULL)
        comment = "";
    const char *conv = "NO_COMPU_METHOD";

    fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"%s\" VAL_BLK 0x%X %s 0 %s %s %s MATRIX_DIM %u %u", symbol_name, comment, addr, A2lGetRecordLayoutName_(type), conv,
            getTypeMinString(type), getTypeMaxString(type), x_dim, y_dim);
    printAddrExt(ext);

    A2lCreateMeasurement_IF_DATA();
    fprintf(gA2lFile, " /end CHARACTERISTIC\n");
    gA2lMeasurements++;
}

//----------------------------------------------------------------------------------
// Parameters

void A2lCreateParameter_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, const char *comment, const char *unit, double min, double max) {

    assert(gA2lFile != NULL);
    if (gA2lAutoGroups) {
        A2lAddToGroup(name);
    }

    fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"%s\" VALUE 0x%X %s 0 NO_COMPU_METHOD %g %g", name, comment, addr, A2lGetRecordLayoutName_(type), min, max);
    printPhysUnit(gA2lFile, unit);
    printAddrExt(ext);
    A2lCreateMeasurement_IF_DATA();
    fprintf(gA2lFile, " /end CHARACTERISTIC\n");
    gA2lParameters++;
}

void A2lCreateMap_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, uint32_t xdim, uint32_t ydim, const char *comment, const char *unit, double min, double max) {

    assert(gA2lFile != NULL);
    if (gA2lAutoGroups) {
        A2lAddToGroup(name);
    }

    fprintf(gA2lFile,
            "/begin CHARACTERISTIC %s \"%s\" MAP 0x%X %s 0 NO_COMPU_METHOD %g %g"
            " /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR"
            " /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR",
            name, comment, addr, A2lGetRecordLayoutName_(type), min, max, xdim, xdim - 1, xdim, ydim, ydim - 1, ydim);
    printPhysUnit(gA2lFile, unit);
    printAddrExt(ext);
    A2lCreateMeasurement_IF_DATA();
    fprintf(gA2lFile, " /end CHARACTERISTIC\n");
    gA2lParameters++;
}

void A2lCreateCurve_(const char *name, tA2lTypeId type, uint8_t ext, uint32_t addr, uint32_t xdim, const char *comment, const char *unit, double min, double max) {

    assert(gA2lFile != NULL);
    if (gA2lAutoGroups) {
        A2lAddToGroup(name);
    }

    fprintf(gA2lFile,
            "/begin CHARACTERISTIC %s \"%s\" CURVE 0x%X %s 0 NO_COMPU_METHOD %g %g"
            " /begin AXIS_DESCR FIX_AXIS NO_INPUT_QUANTITY NO_COMPU_METHOD  %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR",
            name, comment, addr, A2lGetRecordLayoutName_(type), min, max, xdim, xdim - 1, xdim);
    printPhysUnit(gA2lFile, unit);
    printAddrExt(ext);
    A2lCreateMeasurement_IF_DATA();

    fprintf(gA2lFile, " /end CHARACTERISTIC\n");
    gA2lParameters++;
}

//----------------------------------------------------------------------------------
// Groups

// Begin a group for measurements or parameters
// Thread safe, but be aware of the mutex lock
void A2lBeginGroup(const char *name, const char *comment, bool is_parameter_group) {
    assert(gA2lGroupsFile != NULL);
    if (gA2lAutoGroupName == NULL || strcmp(name, gA2lAutoGroupName) != 0) { // Close previous group if any and new group name is different
        A2lEndGroup();

        mutexLock(&gA2lMutex);
        gA2lAutoGroupName = name;
        gA2lAutoGroupIsParameter = is_parameter_group;
        fprintf(gA2lGroupsFile, "/begin GROUP %s \"%s\"", name, comment);
        if (gA2lAutoGroupIsParameter) {
            fprintf(gA2lGroupsFile, " ROOT /begin REF_CHARACTERISTIC");
        } else {
            fprintf(gA2lGroupsFile, " /begin REF_MEASUREMENT");
        }
    }
}

// Add a measurement or parameter to the current open group
void A2lAddToGroup(const char *name) {
    assert(gA2lGroupsFile != NULL);
    if (gA2lAutoGroupName != NULL) {
        fprintf(gA2lGroupsFile, " %s", name);
    }
}

// End the current open group for measurements or parameters
void A2lEndGroup(void) {
    assert(gA2lGroupsFile != NULL);
    if (gA2lAutoGroupName == NULL)
        return;

    fprintf(gA2lGroupsFile, " /end REF_%s", gA2lAutoGroupIsParameter ? "CHARACTERISTIC" : "MEASUREMENT");
    fprintf(gA2lGroupsFile, " /end GROUP\n");
    mutexUnlock(&gA2lMutex);
}

void A2lCreateMeasurementGroup(const char *name, int count, ...) {

    va_list ap;

    assert(gA2lGroupsFile != NULL);
    mutexLock(&gA2lMutex);
    fprintf(gA2lGroupsFile, "/begin GROUP %s \"\" ROOT", name);
    fprintf(gA2lGroupsFile, " /begin REF_MEASUREMENT");
    va_start(ap, count);
    for (int i = 0; i < count; i++) {
        fprintf(gA2lGroupsFile, " %s", va_arg(ap, char *));
    }
    va_end(ap);
    fprintf(gA2lGroupsFile, " /end REF_MEASUREMENT");
    fprintf(gA2lGroupsFile, " /end GROUP\n\n");
    mutexUnlock(&gA2lMutex);
}

void A2lCreateMeasurementGroupFromList(const char *name, char *names[], uint32_t count) {

    assert(gA2lGroupsFile != NULL);
    mutexLock(&gA2lMutex);
    fprintf(gA2lGroupsFile, "/begin GROUP %s \"\" ROOT", name);
    fprintf(gA2lGroupsFile, " /begin REF_MEASUREMENT");
    for (uint32_t i1 = 0; i1 < count; i1++) {
        fprintf(gA2lGroupsFile, " %s", names[i1]);
    }
    fprintf(gA2lGroupsFile, " /end REF_MEASUREMENT");
    fprintf(gA2lGroupsFile, "\n/end GROUP\n\n");
    mutexUnlock(&gA2lMutex);
}

void A2lCreateParameterGroup(const char *name, int count, ...) {

    va_list ap;

    assert(gA2lGroupsFile != NULL);
    mutexLock(&gA2lMutex);
    fprintf(gA2lGroupsFile, "/begin GROUP %s \"\" ROOT", name);
    fprintf(gA2lGroupsFile, " /begin REF_CHARACTERISTIC\n");
    va_start(ap, count);
    for (int i = 0; i < count; i++) {
        fprintf(gA2lGroupsFile, " %s", va_arg(ap, char *));
    }
    va_end(ap);
    fprintf(gA2lGroupsFile, "\n/end REF_CHARACTERISTIC ");
    fprintf(gA2lGroupsFile, "/end GROUP\n\n");
    mutexUnlock(&gA2lMutex);
}

void A2lCreateParameterGroupFromList(const char *name, const char *pNames[], int count) {

    assert(gA2lGroupsFile != NULL);
    mutexLock(&gA2lMutex);
    fprintf(gA2lGroupsFile, "/begin GROUP %s \"\" ROOT", name);
    fprintf(gA2lGroupsFile, " /begin REF_CHARACTERISTIC\n");
    for (int i = 0; i < count; i++) {
        fprintf(gA2lGroupsFile, " %s", pNames[i]);
    }
    fprintf(gA2lGroupsFile, "\n/end REF_CHARACTERISTIC ");
    fprintf(gA2lGroupsFile, "/end GROUP\n\n");
    mutexUnlock(&gA2lMutex);
}

//----------------------------------------------------------------------------------

bool A2lOnce_(atomic_bool *value) {
    bool old_value = false;
    if (atomic_compare_exchange_strong_explicit(value, &old_value, true, memory_order_relaxed, memory_order_relaxed)) {
        return gA2lFile != NULL; // Return true if A2L file is open
    } else {
        return false;
    }
}

//-----------------------------------------------------------------------------------------------------
// A2L file generation and finalization on XCP connect

static bool file_exists(const char *path) {
    FILE *file = fopen(path, "r");
    if (file) {
        fclose(file);
        return true;
    }
    return false;
}

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

// Finalize A2L file generation
bool A2lFinalize(void) {

    if (gA2lFile != NULL) {

        // @@@@ TODO: Improve EPK generation
        // A different A2L EPK version is  be required for the same build, if the order of event or calibration segment creation  is different and leads to different ids !!!!
        // Set the EPK (software version number) for the A2L file
        char epk[64];
        sprintf(epk, "EPK_%s_%s", __DATE__, __TIME__);
        XcpSetEpk(epk);

        // Close the last group if any
        if (gA2lAutoGroups) {
            A2lEndGroup();
        }

        // Create sub groups for all event
#if defined(XCP_ENABLE_DAQ_EVENT_LIST) && !defined(XCP_ENABLE_DAQ_EVENT_INFO)
        if (gA2lAutoGroups) {
            tXcpEventList *eventList;
            eventList = XcpGetEventList();
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

        DBG_PRINTF3("A2L created: %u measurements, %u params, %u typedefs, %u components, %u instances, %u conversions\n\n", gA2lMeasurements, gA2lParameters, gA2lTypedefs,
                    gA2lComponents, gA2lInstances, gA2lConversions);
    }

    return true; // Do not refuse connect
}

// Open the A2L file and register the finalize callback
bool A2lInit(const char *a2l_filename, const char *a2l_projectname, const uint8_t *addr, uint16_t port, bool useTCP, bool finalize_on_connect, bool auto_groups) {

    assert(gA2lFile == NULL);
    assert(a2l_filename != NULL);
    assert(a2l_projectname != NULL);
    assert(addr != NULL);

    // Save transport layer parameters for A2l finalization
    memcpy(&gA2lOptionBindAddr, addr, 4);
    gA2lOptionPort = port;
    gA2lUseTCP = useTCP;
    gA2lAutoGroups = auto_groups;

    mutexInit(&gA2lMutex, true, 1000);

    // Check if A2L file already exists and rename it to 'name.old' if it does
    if (file_exists(a2l_filename)) {
        char old_filename[256];
        SNPRINTF(old_filename, sizeof(old_filename), "%s.old", a2l_filename);
        if (rename(a2l_filename, old_filename) != 0) {
            DBG_PRINTF_ERROR("Failed to rename existing A2L file %s to %s\n", a2l_filename, old_filename);
            return false;
        } else {
            DBG_PRINTF3("Renamed existing A2L file %s to %s\n", a2l_filename, old_filename);
        }
    }

    // Open A2L file
    if (!A2lOpen(a2l_filename, a2l_projectname)) {
        printf("Failed to open A2L file %s\n", a2l_filename);
        return false;
    }

    // Register finalize callback on XCP connect
    if (finalize_on_connect)
        ApplXcpRegisterConnectCallback(A2lFinalize);
    return true;
}
