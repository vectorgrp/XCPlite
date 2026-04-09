/*----------------------------------------------------------------------------
| File:
|   a2l_writer.c
|
| Description:
|   Create A2L file
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "a2l_writer.h"
#include "a2l.h"

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIu64
#include <stdarg.h>   // for va_
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t
#include <stdio.h>    // for fclose, fopen, fread, fseek, ftell
#include <string.h>   // for

#include "dbg_print.h"  // for DBG_PRINT
#include "xcp_cfg.h"    // for XCP_xxx
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcplite.h"    // for tXcpCalSeg, tXcpDaqLists, XcpXxx, ApplXcpXxx, ...
#include "xcptl_cfg.h"  // for XCPTL_xxx

#ifdef OPTION_ENABLE_A2L_GENERATOR

//----------------------------------------------------------------------------------
// Static

static FILE *gA2lFile = NULL;

static bool gA2lUseTCP = false;
static uint16_t gA2lOptionPort = 5555;
static uint8_t gA2lOptionBindAddr[4] = {0, 0, 0, 0};

static bool gA2lSymbolPrefix = false; // Prepend project name as prefix to all symbol names (measurements, parameters, typedefs, components)

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

// Returns name with optional project name prefix prepended ("project.name")
static const char *A2lGetPrefixedName_(const char *prefix, const char *name) {
    if (gA2lSymbolPrefix && prefix != NULL && prefix[0] != '\0') {
        static char s[XCP_A2L_MAX_SYMBOL_NAME_LENGTH]; // static buffer for prefixed name
        SNPRINTF(s, XCP_A2L_MAX_SYMBOL_NAME_LENGTH, "%s.%s", prefix, name);
        return s;
    }
    return name;
}

// Get the prefixed event name
static const char *A2lGetEventName_(tXcpEventId id) {
#ifdef OPTION_SHM_MODE // prefixed event name
    // Create a name prefixed with the application name stored in the the event
    if (XcpShmIsXcpServer()) {
        return A2lGetPrefixedName_(XcpShmGetAppProjectName(XcpGetEventAppId(id)), XcpGetEventName(id));
    }
#endif // SHM_MODE
    return A2lGetPrefixedName_(XcpGetProjectName(), XcpGetEventName(id));
}

// Get the prefixed memory segment name
static const char *A2lGetCalSegName_(uint8_t app_id, const char *name) {
#ifdef OPTION_SHM_MODE // prefixed memory segment name
    // Create a name prefixed with the application name stored in the calibration segment
    if (XcpShmIsXcpServer()) {
        return A2lGetPrefixedName_(XcpShmGetAppProjectName(app_id), name);
    }
#endif // SHM_MODE
    return A2lGetPrefixedName_(XcpGetProjectName(), name);
}

//----------------------------------------------------------------------------------

// Create MOD_PAR memory segments
static void A2lCreate_MOD_PAR(void) {

    assert(gA2lFile != NULL);

    fprintf(gA2lFile, "\n/begin MOD_PAR \"\"\n");

    // Write the current ECU EPK (in SHM mode for all existing applications)
    const char *epk = XcpGetEcuEpk();
    if (epk) {
        fprintf(gA2lFile, "EPK \"%s\" ADDR_EPK 0x%08X\n", epk, XCP_ADDR_EPK);
    }

    // Memory segments
#ifdef XCP_ENABLE_CALSEG_LIST
    {
        // Iterate cal_seg_list
        // Not all calibration segments are memory segments !
        uint16_t calSegCount = XcpGetCalSegCount();
        for (tXcpCalSegIndex i = 0; i < calSegCount; i++) {
            tXcpCalSegNumber n = XcpGetCalSegNumber(i);
            if (n != XCP_UNDEFINED_CALSEG_NUM) {
                const tXcpCalSeg *calseg = XcpGetCalSeg(i);
                const char *pname;
#ifdef OPTION_CAL_SEGMENT_EPK
                if (strcmp(calseg->h.name, XCP_EPK_CALSEG_NAME) != 0) { // Don't prefix the EPK segment name
                    pname = A2lGetCalSegName_(calseg->h.app_id, calseg->h.name);
                } else
#endif
                {
                    pname = calseg->h.name;
                }
                fprintf(gA2lFile, gA2lMemorySegment, pname, XcpGetCalSegBaseAddress(i), calseg->h.size, n, pname, pname, pname, calseg->h.size);
            }
        }
    }
#endif // XCP_ENABLE_CALSEG_LIST

    fprintf(gA2lFile, "/end MOD_PAR\n\n");
}

// Create IF_DATA for DAQ, including event list
static void A2lCreate_IF_DATA_DAQ(void) {

#if defined(XCP_ENABLE_DAQ_EVENT_LIST)
    const tXcpEventList *eventList = NULL;
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
        const tXcpEvent *event = &eventList->event[id];

        // Convert cycle time to ASAM XCP IF_DATA coding time cycle and time unit
        // RESOLUTION OF TIMESTAMP "UNIT_1NS" = 0, "UNIT_10NS" = 1, ...
        uint8_t timeUnit = 0;                      // timeCycle unit, 1ns=0, 10ns=1, 100ns=2, 1us=3, ..., 1ms=6, ...
        uint32_t timeCycle = event->cycle_time_ns; // cycle time in units, 0 = sporadic or unknown
        while (timeCycle >= 256) {
            timeCycle /= 10;
            timeUnit++;
        }

        // Long name and short name (max 8 chars)
        const char *name = XcpGetEventName(id);   // Short name is not build with prefix
        const char *pname = A2lGetEventName_(id); // Long name with prefix
        fprintf(gA2lFile, "/begin EVENT \"%s\" \"%.8s\" 0x%X DAQ 0xFF %u %u %u CONSISTENCY EVENT", pname, name, id, timeCycle, timeUnit,
                (event->flags & XCP_DAQ_EVENT_FLAG_PRIORITY) ? 0xFF : 0x00);

        fprintf(gA2lFile, " /end EVENT\n");
    }
#endif

    fprintf(gA2lFile, gA2lIfDataEndDAQ);
}

// Create IF_DATA for Ethernet transport layer
static void A2lCreate_ETH_IF_DATA(bool useTCP, const uint8_t *addr, uint16_t port) {

    assert(addr != NULL);
    assert(gA2lFile != NULL);

    fprintf(gA2lFile, gA2lIfDataBegin);

    // Protocol Layer info
    fprintf(gA2lFile, gA2lIfDataProtocolLayer, XCP_PROTOCOL_LAYER_VERSION, XCPTL_MAX_CTO_SIZE, XCPTL_MAX_DTO_SIZE);

    // DAQ info
    A2lCreate_IF_DATA_DAQ();

    // Transport Layer info (protocol, address, port)
    // Skip transport layer info completely, if no valid address is configured or detected
    // @@@@ NOTE: Workaround for CANape bug, (protocol, port, 0.0.0.0) is no option, as CANape considers this to be a valid address and tries to connect to it, instead of using the
    // user configured address
    uint8_t addr0[] = {0, 0, 0, 0};
    if (addr != NULL && addr[0] != 0) {
        memcpy(addr0, addr, 4);
    }
#ifdef OPTION_ENABLE_GET_LOCAL_ADDR // Guess the IP address of the local network interface if bound to ANY
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

//-----------------------------------------------------------------------------------------------------

// Create groups and enum conversion for for events
#if defined(XCP_ENABLE_DAQ_EVENT_LIST)
static void createEventGroupsAndConversions(bool event_groups, bool event_conversion) {

    assert(gA2lFile != NULL);

    uint16_t eventCount = XcpGetEventCount();
    const tXcpEventList *eventList = XcpGetEventList();
    if (eventList != NULL && eventCount > 0) {

        // Create a enum conversion with all event ids.
        if (event_conversion) {
            fprintf(gA2lFile, "\n/begin COMPU_METHOD conv.events \"\" TAB_VERB \"%%.0 \" \"\" COMPU_TAB_REF conv.events.table /end COMPU_METHOD\n");
            fprintf(gA2lFile, "/begin COMPU_VTAB conv.events.table \"\" TAB_VERB %u\n", eventCount);
            for (uint32_t id = 0; id < eventCount; id++) {
                fprintf(gA2lFile, " %u \"%s\"", id, A2lGetEventName_(id));
            }
            fprintf(gA2lFile, "\n/end COMPU_VTAB\n");
        }

        // Create a root group for all events
        if (event_groups) {
            fprintf(gA2lFile, "\n/begin GROUP Events \"Events\" ROOT /begin SUB_GROUP");
#ifdef OPTION_DAQ_ASYNC_EVENT
            uint32_t id = 1; // Skip event 0 which is the built-in asynchronous events
#else
            uint32_t id = 0;
#endif
            for (; id < eventCount; id++) {
                fprintf(gA2lFile, " %s", A2lGetEventName_(id));
            }
            fprintf(gA2lFile, " /end SUB_GROUP /end GROUP\n");
        }
    }
}
#endif // XCP_ENABLE_DAQ_EVENT_LIST

//----------------------------------------------------------------------------------------------

// Include the partial A2L files generated by the application(s) into the main file
static void includePartialA2lFiles(uint8_t a2l_mode, uint16_t count, const char **files) {

    assert(count > 0);

#ifndef OPTION_SHM_MODE // comment about merged A2L files
    fprintf(gA2lFile, "\n/*-----------------------------------------------------------------------------------------*/\n\n");
#endif

    for (int fi = 0; fi < count; fi++) {
        if (a2l_mode & A2L_MODE_WRITE_TEMPLATE) {

            fprintf(gA2lFile, "/* /include \"%s\" */\n", files[fi]);

        } else {

            FILE *fol = fopen(files[fi], "r");
            if (fol != NULL) {
#ifdef OPTION_SHM_MODE // comment about merged A2L files
                DBG_PRINTF3("Merging A2L file '%s'\n", files[fi]);
                fprintf(gA2lFile, "\n\n/*-----------------------------------------------------------------------------------------*/\n");
                fprintf(gA2lFile, "/* /include \"%s\" */\n\n", files[fi]);
#endif // SHM_MODE
                char line[512];
                while (fgets(line, sizeof(line), fol) != NULL) {
                    fprintf(gA2lFile, "%s", line);
                }
                fclose(fol);
            } else {
                DBG_PRINTF_WARNING("Could not open file '%s'\n", files[fi]);
            }
        }
    }
    fprintf(gA2lFile, "\n/*-----------------------------------------------------------------------------------------*/\n\n");
}

//----------------------------------------------------------------------------------------------
// Write the main A2L file, with options to include event groups, symbol name prefix, and partial A2L files

// Write the main A2L file
bool A2lWriter(const char *a2l_filename, uint8_t a2l_mode, uint16_t include_count, const char **include_files, const uint8_t *addr, uint16_t port, bool useTCP) {

    assert(addr != NULL);
    assert(port != 0);
    assert(a2l_filename != NULL);

    DBG_PRINTF3("Write A2L file %s\n", a2l_filename);

    // Save parameters
    memcpy(&gA2lOptionBindAddr, addr, 4);
    gA2lOptionPort = port;
    gA2lUseTCP = useTCP;
    gA2lSymbolPrefix = a2l_mode & A2L_MODE_SYMBOL_PREFIX;

    // Open a temporary file, because one of the include files may have the same name as the final file, and we don't want to overwrite it before including it
    gA2lFile = fopen("tmp.a2l", "w");
    if (gA2lFile == NULL) {
        DBG_PRINT_ERROR("Could not create file tmp.a2l!\n");
        return false;
    }

    // Create header
    fprintf(gA2lFile, gA2lHeader1, XcpGetProjectName(), XcpGetProjectName());
    if (a2l_mode & A2L_MODE_EMBED_AML_FILE) {
        assert(0 && "Not implemented yet: embedding AML file content into A2L file is not implemented yet");
    } else {
        fprintf(gA2lFile, "/include \"XCP_104.aml\"\n\n");
    }
    fprintf(gA2lFile, "%s", gA2lHeader2);

    // Create predefined conversions
    // In the conversions.a2l file - will be merges later as there might be more conversions during the generation process
    fprintf(gA2lFile, "/begin COMPU_METHOD conv.bool \"\" TAB_VERB \"%%.0\" \"\" COMPU_TAB_REF conv.bool.table /end COMPU_METHOD\n");
    fprintf(gA2lFile, "/begin COMPU_VTAB conv.bool.table \"\" TAB_VERB 2 0 \"false\" 1 \"true\" /end COMPU_VTAB\n");
    fprintf(gA2lFile, "\n");

    // Create predefined standard record layouts and typedefs for elementary types
    // In the typedefs.a2l file - will be merges later as there might be more typedefs during the generation process
    tA2lTypeId typeid_table[] = {A2L_TYPE_UINT8, A2L_TYPE_UINT16, A2L_TYPE_UINT32, A2L_TYPE_UINT64, A2L_TYPE_INT8,
                                 A2L_TYPE_INT16, A2L_TYPE_INT32,  A2L_TYPE_INT64,  A2L_TYPE_FLOAT,  A2L_TYPE_DOUBLE};
    for (size_t i = 0; i < sizeof(typeid_table); i++) {
        tA2lTypeId a2l_type_id = typeid_table[i];
        const char *a2l_type_name = A2lGetA2lTypeName(a2l_type_id);
        assert(a2l_type_name != NULL);
        const char *a2l_record_layout_name = A2lGetA2lRecordLayoutName(a2l_type_id);
        assert(a2l_record_layout_name != NULL);
        // RECORD_LAYOUTs for standard types U8,I8,...,F64 (Position 1 increasing index)
        // Example: /begin RECORD_LAYOUT U64 FNC_VALUES 1 A_UINT64 ROW_DIR DIRECT /end RECORD_LAYOUT
        fprintf(gA2lFile, "/begin RECORD_LAYOUT %s FNC_VALUES 1 %s ROW_DIR DIRECT /end RECORD_LAYOUT\n", a2l_record_layout_name, a2l_type_name);
        // RECORD_LAYOUTs for axis points with standard types A_U8,A_I8,... (Positionn 1 increasing index)
        // Example: /begin RECORD_LAYOUT A_F32 AXIS_PTS_X 1 FLOAT32_IEEE INDEX_INCR DIRECT /end RECORD_LAYOUT
        fprintf(gA2lFile, "/begin RECORD_LAYOUT A_%s AXIS_PTS_X 1 %s INDEX_INCR DIRECT /end RECORD_LAYOUT\n", a2l_record_layout_name, a2l_type_name);
        // Example: /begin TYPEDEF_MEASUREMENT M_F64 "" FLOAT64_IEEE NO_COMPU_METHOD 0 0 -1e12 1e12 /end TYPEDEF_MEASUREMENT
        const char *format_str =
            (a2l_type_id == A2L_TYPE_FLOAT || a2l_type_id == A2L_TYPE_DOUBLE)
                ? "/begin TYPEDEF_MEASUREMENT M_%s \"\" %s NO_COMPU_METHOD 0 0 %g %g /end TYPEDEF_MEASUREMENT\n"
                : "/begin TYPEDEF_MEASUREMENT M_%s \"\" %s NO_COMPU_METHOD 0 0 %.0f %.0f /end TYPEDEF_MEASUREMENT\n"; // Avoid exponential format for integer types
        fprintf(gA2lFile, format_str, a2l_record_layout_name, a2l_type_name, A2lGetTypeMin(a2l_type_id), A2lGetTypeMax(a2l_type_id));
        // Example: /begin TYPEDEF_CHARACTERISTIC C_U8 "" VALUE U8 0 NO_COMPU_METHOD 0 255 /end TYPEDEF_CHARACTERISTIC
        fprintf(gA2lFile, "/begin TYPEDEF_CHARACTERISTIC C_%s \"\" VALUE %s 0 NO_COMPU_METHOD %g %g /end TYPEDEF_CHARACTERISTIC\n", a2l_record_layout_name, a2l_record_layout_name,
                A2lGetTypeMin(a2l_type_id), A2lGetTypeMax(a2l_type_id));
    }
    fprintf(gA2lFile, "\n");

    // Include the partial A2L files generated by the application(s) into the main file
    includePartialA2lFiles(a2l_mode, include_count, include_files);

    // Create event conversions and groups
    createEventGroupsAndConversions(a2l_mode & A2L_MODE_AUTO_GROUPS, a2l_mode & A2L_MODE_EVENT_CONVERSION);

    // Create MOD_PAR section with EPK and calibration segments
    A2lCreate_MOD_PAR();

    // Create IF_DATA section with event list and transport layer info
    A2lCreate_ETH_IF_DATA(gA2lUseTCP, gA2lOptionBindAddr, gA2lOptionPort);

    // Append the footer and close
    fprintf(gA2lFile, "%s", gA2lFooter);
    fclose(gA2lFile);
    gA2lFile = NULL;

    // Rename the temporary file to the final name
    remove(a2l_filename);
    if (rename("tmp.a2l", a2l_filename) != 0) {
        DBG_PRINTF_ERROR("Could not rename file tmp.a2l to %s!\n", a2l_filename);
        return false;
    }

    DBG_PRINT3(ANSI_COLOR_GREEN "A2L created\n" ANSI_COLOR_RESET);
    return true;
}

#endif // XCP_ENABLE_A2L_GENERATOR
