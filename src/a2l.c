/*----------------------------------------------------------------------------
| File:
|   a2l.c
|
| Description:
|   Create A2L file
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/

#include "a2l.h"
#include "a2l_writer.h"

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIu64
#include <stdarg.h>   // for va_
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uintxx_t
#include <stdio.h>    // for fclose, fopen, fread, fseek, ftell
// #include <string.h>   // for

#include "dbg_print.h"   // for DBG_PRINT
#include "persistence.h" // for XcpBinWrite, XcpBinDelete
#include "platform.h"    // for platform defines (WIN_, LINUX_, MACOS_) and specific implementation of sockets, clock, thread, mutex
#include "xcp_cfg.h"     // for XCP_xxx
#include "xcplib_cfg.h"  // for OPTION_xxx
#include "xcptl_cfg.h"   // for XCPTL_xxx

#ifdef OPTION_ENABLE_A2L_GENERATOR

//----------------------------------------------------------------------------------

// A2L global options
static bool gA2lUseTCP = false;
static uint16_t gA2lOptionPort = 5555;
static uint8_t gA2lOptionBindAddr[4] = {0, 0, 0, 0};
static uint8_t gA2lMode = 0;

// A2L file handles and state
static bool gA2lIsFinalized = false;
static FILE *gA2lFile = NULL;
static FILE *gA2lTypedefsFile = NULL;
static FILE *gA2lGroupsFile = NULL;
static FILE *gA2lConversionsFile = NULL;

// Thread safety and one time execution
static MUTEX gA2lMutex;

// Conversion name buffer
static char gA2lConvName[XCP_A2L_MAX_SYMBOL_NAME_LENGTH] = {0}; // static buffer for current conversion name

// Auto grouping
static char gA2lAutoGroupName[XCP_A2L_MAX_SYMBOL_NAME_LENGTH] = {0}; // Current open group
static bool gA2lAutoGroupIsParameter = false;                        // Group is a parameter group
static bool gA2lAutoGroupIsMeasurement = false;                      // Group is a measurement group

// Addressing mode
static tXcpEventId gA2lFixedEvent = XCP_UNDEFINED_EVENT_ID;
static tXcpEventId gA2lDefaultEvent = XCP_UNDEFINED_EVENT_ID;
static uint8_t gA2lAddrExt = XCP_UNDEFINED_ADDR_EXT;     // Address extension (addressing mode)
static uint8_t gA2lAutoAddrExt = XCP_UNDEFINED_ADDR_EXT; // Address extension calculated in automatic mode (valid if gA2lAddrExt=XCP_UNDEFINED_ADDR_EXT)
static const uint8_t *gA2lFramePtr = NULL;               // Frame address for rel and dyn mode
static const uint8_t *gA2lBasePtr = NULL;                // Base address for rel and dyn mode
static tXcpCalSegIndex gA2lAddrIndex = 0;                // Segment index for seg mode

// Input quantities
static const char *gA2lInputQuantity_x = NULL;
static const char *gA2lInputQuantity_y = NULL;

// Statistics
static uint32_t gA2lMeasurements;
static uint32_t gA2lParameters;
static uint32_t gA2lTypedefs;
static uint32_t gA2lComponents;
static uint32_t gA2lInstances;
static uint32_t gA2lConversions;

//----------------------------------------------------------------------------------
// Helper functions

static uint32_t A2lGetAddr_(const void *addr);

static uint8_t A2lGetAddrExt_(void);

#define printAddrExt(ext)                                                                                                                                                          \
    if ((ext) > 0)                                                                                                                                                                 \
        fprintf(gA2lFile, " ECU_ADDRESS_EXTENSION %u", ext);

// Returns name with optional project name prefix prepended ("project.name")
static const char *A2lGetPrefixedName_(const char *prefix, const char *name) {
    if ((gA2lMode & A2L_MODE_SYMBOL_PREFIX) && prefix != NULL && prefix[0] != '\0') {
        static char s[XCP_A2L_MAX_SYMBOL_NAME_LENGTH]; // static buffer for prefixed name
        SNPRINTF(s, XCP_A2L_MAX_SYMBOL_NAME_LENGTH, "%s.%s", prefix, name);
        return s;
    }
    return name;
}

// Returns symbol instance name with optional project name prefix and instance name prepended ("project.name.instance_name.name")
static const char *A2lGetPrefixedInstanceName_(const char *instance_name, const char *name) {
    static char s[XCP_A2L_MAX_SYMBOL_NAME_LENGTH]; // static buffer for prefixed instance name
    if (instance_name != NULL && STRNLEN(instance_name, XCP_A2L_MAX_SYMBOL_NAME_LENGTH) > 0) {
        if ((gA2lMode & A2L_MODE_SYMBOL_PREFIX)) {
            SNPRINTF(s, XCP_A2L_MAX_SYMBOL_NAME_LENGTH, "%s.%s.%s", XcpGetProjectName(), instance_name, name);
        } else {
            SNPRINTF(s, XCP_A2L_MAX_SYMBOL_NAME_LENGTH, "%s.%s", instance_name, name);
        }
        return s;
    } else {
        if ((gA2lMode & A2L_MODE_SYMBOL_PREFIX)) {
            SNPRINTF(s, XCP_A2L_MAX_SYMBOL_NAME_LENGTH, "%s.%s", XcpGetProjectName(), name);
            return s;
        } else {
            return name;
        }
    }
}

//----------------------------------------------------------------------------------

const char *A2lGetA2lTypeName(tA2lTypeId type_id) {
    switch (type_id) {
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
        assert(0 && "Invalid A2L type id");
        return NULL;
    }
}

const char *A2lGetA2lTypeName_M(tA2lTypeId type_id) {
    switch (type_id) {
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
        assert(0 && "Invalid A2L type id");
        return NULL;
    }
}

const char *A2lGetA2lTypeName_C(tA2lTypeId type_id) {
    switch (type_id) {
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
        assert(0 && "Invalid A2L type id");
        return NULL;
    }
}

const char *A2lGetA2lRecordLayoutName(tA2lTypeId type_id) {
    switch (type_id) {
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
        assert(0 && "Invalid A2L type id");
        return NULL;
    }
}

double A2lGetTypeMin(tA2lTypeId type_id) {
    double min;
    switch (type_id) {
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
    case A2L_TYPE_FLOAT:
    case A2L_TYPE_DOUBLE:
        min = -1e12;
        break;
    default:
        min = 0;
    }
    return min;
}

double A2lGetTypeMax(tA2lTypeId type_id) {
    double max;
    switch (type_id) {
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

//----------------------------------------------------------------------------------
// Build filenames for the different (temporary) files used

#define A2L_FILE 1
#define A2L_TYPEDEFS_FILE 2
#define A2L_GROUPS_FILE 3
#define A2L_CONVERSIONS_FILE 4
#define A2L_MAIN_FILE 0xFF

static const char *A2lGetFilename(uint8_t file_type) {
    static char file_name[XCP_A2L_FILENAME_MAX_LENGTH + 1] = {0}; // static buffer for filename
    const char *project_name = XcpGetProjectName();
    const char *postfix;
    bool add_epk;
    switch (file_type) {
#ifdef OPTION_SHM_MODE // main A2L file name
        // In SHM mode, the server build a master file with a generic name and includes all applications partial A2L files
    case A2L_MAIN_FILE:
        project_name = XcpShmGetEcuProjectName(); // Use the ECU name as filename for main file in SHM mode
        postfix = "";
        add_epk = false;
        break;
#endif
    case A2L_TYPEDEFS_FILE:
        postfix = "_typedefs";
        add_epk = false;
        break;
    case A2L_GROUPS_FILE:
        postfix = "_groups";
        add_epk = false;
        break;
    case A2L_CONVERSIONS_FILE:
        postfix = "_conversions";
        add_epk = false;
        break;
    default:
        postfix = "";
        add_epk = !(gA2lMode & A2L_MODE_WRITE_ALWAYS);
        break;
    }

    // Build A2L base filename from project name and EPK
    // If A2l file is generated only once for a new build, the EPK is appended to the filename
    if (add_epk) {
        SNPRINTF(file_name, sizeof(file_name), "%s%s_%s.a2l", project_name, postfix, XcpGetEpk());
    } else {
        SNPRINTF(file_name, sizeof(file_name), "%s%s.a2l", project_name, postfix);
    }
    return file_name;
}

// Helper function to include the content of some temporary files into the main file and remove the temporary files
static bool includeFilesAndRemove(FILE *main_file, FILE **filep, const char *filename) {
    if (filep != NULL && *filep != NULL && filename != NULL) {
        fclose(*filep);
        *filep = NULL;
        FILE *file = fopen(filename, "r");
        if (file != NULL) {
            char line[XCP_A2L_MAX_LINE_LENGTH];
            while (fgets(line, sizeof(line), file) != NULL) {
                fprintf(main_file, "%s", line);
            }
            fclose(file);
            remove(filename);
            return true;
        }
    }
    return true;
}

//----------------------------------------------------------------------------------

// Create IF_DATA for a measurment object
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
                assert(0 && "Fixed event not set"); // Fixed event must be set before calling this function
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

// Debug print address and address extension (addressing mode, event, offset)
#if defined(OPTION_ENABLE_DBG_PRINTS) && (OPTION_MAX_DBG_LEVEL >= 5)
static const char *dbgPrintfAddrExt(uint8_t addr_ext, uint32_t addr) {
#define A2L_ADDR_EXT_STR_MAX_LENGTH 64
    static char buf1[A2L_ADDR_EXT_STR_MAX_LENGTH] = {0}; // static buffer for address string
    static char buf2[A2L_ADDR_EXT_STR_MAX_LENGTH] = {0}; // static buffer for address extension string
    const char *addr_str = NULL;
#ifdef XCP_ENABLE_ABS_ADDRESSING
    if (XcpAddrIsAbs(addr_ext)) {
#ifdef OPTION_SHM_MODE // print absolute address extension with application id
        addr_str = buf2;
        SNPRINTF(buf2, A2L_ADDR_EXT_STR_MAX_LENGTH, "ABS%u", addr_ext - XCP_ADDR_EXT_ABS);
#else
        addr_str = "ABS";
#endif
    }
#endif
#ifdef XCP_ENABLE_CALSEG_LIST
    if (XcpAddrIsSeg(addr_ext)) {
        addr_str = "SEG";
    }
#endif
#ifdef XCP_ENABLE_DYN_ADDRESSING
    if (XcpAddrIsDyn(addr_ext)) {
        addr_str = buf2;
        SNPRINTF(buf2, A2L_ADDR_EXT_STR_MAX_LENGTH, "DYN%u(event=%u,offset=%d)", addr_ext - XCP_ADDR_EXT_DYN, XcpAddrDecodeDynEvent(addr), XcpAddrDecodeDynOffset(addr));
    }
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
    if (XcpAddrIsRel(addr_ext)) {
        addr_str = "REL";
    }
#endif
    if (addr_str == NULL) {
        SNPRINTF(buf1, A2L_ADDR_EXT_STR_MAX_LENGTH, "%u:0x%08X", addr_ext, addr);
    } else {
        SNPRINTF(buf1, A2L_ADDR_EXT_STR_MAX_LENGTH, "%.32s:0x%08X", addr_str, addr);
    }
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
#ifdef XCP_ENABLE_ABS_ADDRESSING
    gA2lAddrExt = ApplXcpGetAddrExt(NULL);
#else
    gA2lAddrExt = XCP_UNDEFINED_ADDR_EXT;
    assert(0 && "Absolute addressing mode is not enabled, check XCP_ENABLE_ABS_ADDRESSING");
#endif
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
    gA2lAddrExt = XCP_UNDEFINED_ADDR_EXT;
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
        if ((gA2lMode & A2L_MODE_AUTO_GROUPS)) {
            A2lBeginGroup(calseg->h.name, "Calibration Segment", true, true);
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
        if ((gA2lMode & A2L_MODE_AUTO_GROUPS)) {
            A2lBeginGroup(calseg->h.name, "Calibration Segment", true, true);
        }
    }
}

#endif

#ifdef XCP_ENABLE_DAQ_EVENT_LIST

static void beginEventGroup(tXcpEventId event_id) {
    DBG_PRINTF5("beginEventGroup: event_id=%u\n", event_id);
    if ((gA2lMode & A2L_MODE_AUTO_GROUPS)) {
        A2lBeginGroup(XcpGetEventName(event_id), "Measurement event group", false, false);
    }
}

// Set autodetect address mode with event name or event id
// Will result in using (ADDR_EXT_DYN+0) with stackframe pointer or (ADDR_EXT_DYN+1) user defined base addresses selected when a variable is created
void A2lSetAutoAddrMode__s(const char *event_name, const uint8_t *stack_frame, const uint8_t *base_addr) {
    if (gA2lFile != NULL) {
        tXcpEventId event_id = XcpFindEvent(event_name);
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
            DBG_PRINTF_ERROR("A2lSetAutoAddrMode__i: Event %u not found!\n", event_id);
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
        tXcpEventId event_id = XcpFindEvent(event_name);
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
            DBG_PRINTF_ERROR("A2lSetRelativeAddrMode__i: Event %u not found!\n", event_id);
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
        tXcpEventId event_id = XcpFindEvent(event_name);
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
            DBG_PRINTF_ERROR("A2lSetRelativeAddrMode__i: Event %u not found!\n", event_id);
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
        tXcpEventId event_id = XcpFindEvent(event_name);
        A2lSetAbsAddrMode(event_id);
        if (event_id != XCP_UNDEFINED_EVENT_ID) {
            beginEventGroup(event_id);
            // fprintf(gA2lFile, "\n/* Absolute addressing mode: default_event=%s (%u), addr_ext=%u */\n", event_name, event_id, A2lGetAddrExt_());
        }
    }
}
void A2lSetAbsoluteAddrMode__i(tXcpEventId event_id) {
    if (gA2lFile != NULL) {
        assert(event_id == XCP_UNDEFINED_EVENT_ID || XcpGetEventName(event_id) != NULL);
        A2lSetAbsAddrMode(event_id);
        if (event_id != XCP_UNDEFINED_EVENT_ID) {
            beginEventGroup(event_id);
            // fprintf(gA2lFile, "\n/* Stack frame absolute addressing mode: event=%s (%u), addr_ext=%u */\n", XcpGetEventName(event_id), event_id, A2lGetAddrExt_());
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

        // Auto
        if (gA2lAddrExt == XCP_UNDEFINED_ADDR_EXT) {
            const uint8_t *base_ptr = NULL;

            // If both base pointers are set
            if (gA2lBasePtr != NULL && gA2lFramePtr != NULL) {
                uint64_t addr_diff1 = (uint64_t)p - (uint64_t)gA2lBasePtr;
                uint64_t addr_high1 = (addr_diff1 >> XCP_DYN_ADDR_OFFSET_BITS); // remaining bits of the address after shifting out the offset bits
                uint64_t addr_diff2 = (uint64_t)p - (uint64_t)gA2lFramePtr;
                uint64_t addr_high2 = (addr_diff2 >> XCP_DYN_ADDR_OFFSET_BITS);
                DBG_PRINTF6("A2L auto dyn address mode: addr=%p, base1=%p, diff1=%llX, base2=%p, diff2=%llX\n", p, (void *)gA2lBasePtr, (unsigned long long)addr_diff1,
                            (void *)gA2lFramePtr, (unsigned long long)addr_diff2);
                // Both valid ? Prefer the smaller one
                if (addr_high1 == 0 && addr_high2) {
                    if (addr_diff1 < addr_diff2) {
                        base_ptr = gA2lBasePtr;
                        gA2lAutoAddrExt = XCP_ADDR_EXT_DYN + 1; // Use base pointer addressing mode with index 1
                    } else {
                        base_ptr = gA2lFramePtr;
                        gA2lAutoAddrExt = XCP_ADDR_EXT_DYN; // Use frame pointer addressing mode
                    }
                }
                // Base valid
                else if (addr_high1 == 0) {
                    base_ptr = gA2lBasePtr;
                    gA2lAutoAddrExt = XCP_ADDR_EXT_DYN + 1; // Use frame pointer addressing mode
                }
                // Stack valid
                else if (addr_high1 == 0) {
                    base_ptr = gA2lFramePtr;
                    gA2lAutoAddrExt = XCP_ADDR_EXT_DYN; // Use frame pointer addressing mode
                }
                // Overflow
                else {
                    DBG_PRINTF_ERROR("A2L address overflow detected! addr: %p, base1: %p, base2: %p\n", p, (void *)gA2lBasePtr, (void *)gA2lFramePtr);
                    assert(0 && "A2L address overflow");
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

            // Try absolute on overflow
            assert(base_ptr != NULL);
            uint64_t addr_diff = (uint64_t)p - (uint64_t)base_ptr;
            // Check overflow
            if ((addr_diff >> XCP_DYN_ADDR_OFFSET_BITS) != 0) {
                DBG_PRINTF6("A2L dyn address overflow detected! addr: %p, base: %p, trying absolute\n", p, (void *)base_ptr);
#ifdef XCP_ENABLE_ABS_ADDRESSING
                // Fallback to absolute if overflow
                gA2lAutoAddrExt = ApplXcpGetAddrExt(p);
                return XcpAddrEncodeAbs(p);
#else
                DBG_PRINTF_ERROR("A2L address overflow detected! addr: %p\n", p);
                assert(0 && "A2L address overflow");
                return 0;
#endif
            }
            return XcpAddrEncodeDyn(addr_diff, gA2lFixedEvent);

        } // gA2lAddrExt == XCP_UNDEFINED_ADDR_EXT

        // Absolute
        else if (XcpAddrIsAbs(gA2lAddrExt)) {
            // Range checking is in the XcpAddrEncodeAbs function
            return XcpAddrEncodeAbs(p);
        }

        // Relative
#ifdef XCP_ENABLE_REL_ADDRESSING
        else if (XcpAddrIsRel(gA2lAddrExt)) {
            uint64_t addr_diff = 0;
            if (gA2lBasePtr != NULL) {
                addr_diff = (uint64_t)p - (uint64_t)gA2lBasePtr;
                // Ensure the address difference does not overflow the value range for absolute addressing (uint32_t)
                if ((addr_diff >> 32) != 0) {
                    DBG_PRINTF_ERROR("A2L rel address overflow detected! addr: %p, base: %p\n", p, (void *)gA2lBasePtr);
                    assert(0 && "A2L address overflow");
                    return 0;
                }
            }
            return XcpAddrEncodeRel(addr_diff);
        }
#endif

        // Dynamic
        else if (XcpAddrIsDyn(gA2lAddrExt)) {
            uint64_t addr_diff = 0;
            if (gA2lBasePtr != NULL) {
                if (p != NULL) {
                    addr_diff = (uint64_t)p - (uint64_t)gA2lBasePtr;
                    // Ensure the address difference does not overflow the offset bits for dynamic addressing
                    if ((addr_diff >> XCP_DYN_ADDR_OFFSET_BITS) != 0) {
                        DBG_PRINTF_ERROR("A2L dyn address overflow detected! addr: %p, base: %p\n", p, (void *)gA2lBasePtr);
                        assert(0 && "A2L address overflow");
                        return 0;
                    }
                } else {
                    addr_diff = 0;
                }
            }
            return XcpAddrEncodeDyn(addr_diff, gA2lFixedEvent);
        }
        // Calibration segment relative
        else
#ifdef XCP_ENABLE_CALSEG_LIST
            if (XcpAddrIsSeg(gA2lAddrExt)) {
            uint64_t addr_diff = 0;
            if (gA2lBasePtr != NULL) {
                addr_diff = (uint64_t)p - (uint64_t)gA2lBasePtr;
                // Ensure the relative address does not overflow the 16 Bit address offset for calibration segment relative addressing
                if ((addr_diff >> 16) != 0) {
                    DBG_PRINTF_ERROR("A2L seg relative address overflow detected! addr: %p, base: %p\n", p, (void *)gA2lBasePtr);
                    assert(0 && "A2L address overflow");
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
// Input quantity

void A2lSetInputQuantity_x(const char *input_quantity) { gA2lInputQuantity_x = input_quantity; }

void A2lSetInputQuantity_y(const char *input_quantity) { gA2lInputQuantity_y = input_quantity; }

static const char *A2lGetInputQuantity_x(void) { return gA2lInputQuantity_x ? gA2lInputQuantity_x : "NO_INPUT_QUANTITY"; }
static const char *A2lGetInputQuantity_y(void) { return gA2lInputQuantity_y ? gA2lInputQuantity_y : "NO_INPUT_QUANTITY"; }

//----------------------------------------------------------------------------------
// Conversions

static void printPhysUnit(FILE *file, const char *unit_or_conversion) {

    // It is a phys unit if the string is not NULL or empty and does not start with "conv."
    if (unit_or_conversion != NULL) {
        size_t len = STRNLEN(unit_or_conversion, XCP_A2L_MAX_SYMBOL_NAME_LENGTH);
        if (len > 0 && !(len > 5 && strncmp(unit_or_conversion, "conv.", 5) == 0)) {
            fprintf(file, " PHYS_UNIT \"%s\"", unit_or_conversion);
        }
    }
}

static const char *getConversion(const char *unit_or_conversion, double *min, double *max) {

    // If the unit_or_conversion string begins with "conv." it is a conversion method name, return it directly
    if (unit_or_conversion != NULL && STRNLEN(unit_or_conversion, XCP_A2L_MAX_SYMBOL_NAME_LENGTH) > 5 && strncmp(unit_or_conversion, "conv.", 5) == 0) {

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

const char *A2lCreateLinearConversion_(const char *conv_name, const char *comment, const char *unit, double factor, double offset) {
    if (gA2lFile != NULL && gA2lConversionsFile != NULL) {
        if (unit == NULL)
            unit = "";
        if (comment == NULL)
            comment = "";
        SNPRINTF(gA2lConvName, sizeof(gA2lConvName), "conv.%s", conv_name); // Build the conversion symbol_name with prefix "conv." and store it in a static variable
        fprintf(gA2lConversionsFile, "/begin COMPU_METHOD conv.%s \"%s\" LINEAR \"%%6.3\" \"%s\" COEFFS_LINEAR %g %g /end COMPU_METHOD\n", conv_name, comment, unit, factor,
                offset);
        gA2lConversions++;

        // Return the conversion symbol_name for reference when creating measurements
        return gA2lConvName;
    }
    return "";
}

const char *A2lCreateEnumConversion_(const char *conv_name, const char *enum_description) {
    if (gA2lFile != NULL && gA2lConversionsFile != NULL) {
        SNPRINTF(gA2lConvName, sizeof(gA2lConvName), "conv.%s", conv_name); // Build the conversion symbol_name with prefix "conv." and store it in a static variable
        fprintf(gA2lConversionsFile, "/begin COMPU_METHOD conv.%s \"\" TAB_VERB \"%%.0 \" \"\" COMPU_TAB_REF conv.%s.table /end COMPU_METHOD\n", conv_name, conv_name);
        fprintf(gA2lConversionsFile, "/begin COMPU_VTAB conv.%s.table \"\" TAB_VERB %s /end COMPU_VTAB\n", conv_name, enum_description);
        gA2lConversions++;
        return gA2lConvName; // Return the conversion symbol_name for reference when creating measurements
    }
    return "";
}

//----------------------------------------------------------------------------------
// Typedefs

// Begin a typedef structure
void A2lTypedefBegin_(const char *symbol_name, uint32_t size, const char *format, ...) {
    if (gA2lFile != NULL) {
        const char *pname = A2lGetPrefixedName_(XcpGetProjectName(), symbol_name);
        // Format the comment string
        char comment[XCP_A2L_MAX_COMMENT_LENGTH];
        va_list args;
        va_start(args, format);
        vsnprintf(comment, sizeof(comment), format, args);
        va_end(args);
        DBG_PRINTF5("A2lTypedefBegin_: %s, size=%u, comment='%s'\n", pname, size, comment);
        fprintf(gA2lFile, "\n/begin TYPEDEF_STRUCTURE %s \"%s\" 0x%X\n", pname, comment, size);
        gA2lTypedefs++;
    }
}

// End a typedef structure
void A2lTypedefEnd_(void) {
    if (gA2lFile != NULL) {
        DBG_PRINT5("A2lTypedefEnd_\n");
        fprintf(gA2lFile, "/end TYPEDEF_STRUCTURE\n\n");
    }
}

// For scalar or one dimensional measurement and parameter components of specified type
// type_name is the name of another typedef, typedef_measurement or typedef_characteristic
void A2lTypedefComponent_(const char *field_name, const char *type_name, uint16_t x_dim, size_t offset) {
    if (gA2lFile != NULL) {
        DBG_PRINTF5("A2lTypedefComponent_: %s, %s, x_dim=%u, offset=0x%zX\n", field_name, type_name, x_dim, offset);
        fprintf(gA2lFile, "  /begin STRUCTURE_COMPONENT %s %s 0x%zX", field_name, type_name, offset);
        if (x_dim > 1)
            fprintf(gA2lFile, " MATRIX_DIM %u", x_dim);
        fprintf(gA2lFile, " /end STRUCTURE_COMPONENT\n");
        gA2lComponents++;
    }
}

// For measurement components with TYPEDEF_MEASUREMENT for fields with comment, unit, min, max
void A2lTypedefMeasurementComponent_(const char *field_name, tA2lTypeId type_id, uint16_t x_dim, size_t offset, const char *comment, const char *unit_or_conversion, double min,
                                     double max) {
    if (gA2lFile != NULL && gA2lTypedefsFile != NULL) {
        const char *type_name = A2lGetA2lTypeName(type_id);
        DBG_PRINTF5("A2lTypedefMeasurementComponent_: %s, %s, x_dim=%u, offset=0x%zX\n", field_name, type_name, x_dim, offset);

        // TYPEDEF_MEASUREMENT
        const char *conv = getConversion(unit_or_conversion, NULL, NULL);
        if (min == 0.0 && max == 0.0) {
            min = A2lGetTypeMin(type_id);
            max = A2lGetTypeMax(type_id);
        }
        fprintf(gA2lTypedefsFile, "/begin TYPEDEF_MEASUREMENT M_%s \"%s\" %s %s 0 0 %g %g", field_name, comment, type_name, conv, min, max);
        printPhysUnit(gA2lTypedefsFile, unit_or_conversion);
        fprintf(gA2lTypedefsFile, " /end TYPEDEF_MEASUREMENT\n");

        // STRUCTURE_COMPONENT
        fprintf(gA2lFile, "  /begin STRUCTURE_COMPONENT %s M_%s 0x%zX", field_name, field_name, offset);
        if (x_dim > 1)
            fprintf(gA2lFile, " MATRIX_DIM %u", x_dim);
        fprintf(gA2lFile, " /end STRUCTURE_COMPONENT\n");
        gA2lComponents++;
    }
}

// For multidimensional parameter components with TYPEDEF_CHARACTERISTIC for fields with comment, unit, min, max
void A2lTypedefParameterComponent_(const char *field_name, tA2lTypeId type_id, uint16_t x_dim, uint16_t y_dim, size_t offset, const char *comment, const char *unit_or_conversion,
                                   double min, double max, const char *x_axis, const char *y_axis) {
    if (gA2lFile != NULL && gA2lTypedefsFile != NULL) {
        const char *type_name = A2lGetA2lRecordLayoutName(type_id);
        DBG_PRINTF5("A2lTypedefParameterComponent_: %s, %s, x_dim=%u, y_dim=%u, offset=0x%zX\n", field_name, type_name, x_dim, y_dim, offset);

        // TYPEDEF_AXIS (y_dim==0)
        if (y_dim == 0 && x_dim > 1) {
            fprintf(gA2lTypedefsFile, "/begin TYPEDEF_AXIS A_%s \"%s\" %s A_%s 0 NO_COMPU_METHOD %u %g %g", field_name, comment, A2lGetInputQuantity_x(), type_name, x_dim, min,
                    max);
            printPhysUnit(gA2lTypedefsFile, unit_or_conversion);
            fprintf(gA2lTypedefsFile, " /end TYPEDEF_AXIS\n");
            fprintf(gA2lFile, "  /begin STRUCTURE_COMPONENT %s A_%s 0x%zX /end STRUCTURE_COMPONENT\n", field_name, field_name, offset);

        }

        // TYPEDEF_CHARACTERISTIC
        else {
            // MAP
            if (y_dim > 1) {
                fprintf(gA2lTypedefsFile, "/begin TYPEDEF_CHARACTERISTIC C_%s \"%s\" MAP %s 0 NO_COMPU_METHOD %g %g", field_name, comment, type_name, min, max);
                if (x_axis == NULL)
                    fprintf(gA2lTypedefsFile, " /begin AXIS_DESCR FIX_AXIS %s NO_COMPU_METHOD %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR", A2lGetInputQuantity_x(), x_dim,
                            x_dim - 1, x_dim);
                else
                    fprintf(gA2lTypedefsFile, " /begin AXIS_DESCR COM_AXIS %s NO_COMPU_METHOD %u 0.0 0.0 AXIS_PTS_REF THIS.%s /end AXIS_DESCR", A2lGetInputQuantity_x(), x_dim,
                            x_axis);
                if (y_axis == NULL)
                    fprintf(gA2lTypedefsFile, " /begin AXIS_DESCR FIX_AXIS %s NO_COMPU_METHOD %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR", A2lGetInputQuantity_y(), y_dim,
                            y_dim - 1, y_dim);
                else
                    fprintf(gA2lTypedefsFile, " /begin AXIS_DESCR COM_AXIS %s NO_COMPU_METHOD %u 0.0 0.0 AXIS_PTS_REF THIS.%s /end AXIS_DESCR", A2lGetInputQuantity_y(), y_dim,
                            y_axis);
            }
            // CURVE
            else if (x_dim > 1) {
                fprintf(gA2lTypedefsFile, "/begin TYPEDEF_CHARACTERISTIC C_%s \"%s\" CURVE %s 0 NO_COMPU_METHOD %g %g", field_name, comment, type_name, min, max);
                if (x_axis == NULL)
                    fprintf(gA2lTypedefsFile, " /begin AXIS_DESCR FIX_AXIS %s NO_COMPU_METHOD %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR", A2lGetInputQuantity_x(), x_dim,
                            x_dim - 1, x_dim);
                else
                    fprintf(gA2lTypedefsFile, " /begin AXIS_DESCR COM_AXIS %s NO_COMPU_METHOD %u 0.0 0.0 AXIS_PTS_REF THIS.%s /end AXIS_DESCR", A2lGetInputQuantity_x(), x_dim,
                            x_axis);
            }
            // VALUE
            else if (x_dim == 1 && y_dim == 1) {
                const char *conv = getConversion(unit_or_conversion, NULL, NULL);
                fprintf(gA2lTypedefsFile, "/begin TYPEDEF_CHARACTERISTIC C_%s \"%s\" VALUE %s 0 %s %g %g", field_name, comment, type_name, conv, min, max);
            }
            //
            else {
                DBG_PRINTF_ERROR("Invalid dimensions: x_dim=%u, y_dim=%u\n", x_dim, y_dim);
                assert(0 && "Invalid dimensions");
            }
            printPhysUnit(gA2lTypedefsFile, unit_or_conversion);
            fprintf(gA2lTypedefsFile, " /end TYPEDEF_CHARACTERISTIC\n");
            fprintf(gA2lFile, "  /begin STRUCTURE_COMPONENT %s C_%s 0x%zX /end STRUCTURE_COMPONENT\n", field_name, field_name, offset);
        }

        gA2lComponents++;
    }
}

void A2lCreateInstance_(const char *instance_name, const char *typeName, const uint16_t x_dim, const void *ptr, const char *comment) {
    if (gA2lFile != NULL) {
        uint32_t addr = A2lGetAddr_(ptr);
        uint8_t ext = A2lGetAddrExt_();
        const char *pname = A2lGetPrefixedName_(XcpGetProjectName(), instance_name);
        DBG_PRINTF5("A2lCreateInstance_: %s, \"%s\", %s, %s\n", pname, comment, typeName, dbgPrintfAddrExt(ext, addr));

        if ((gA2lMode & A2L_MODE_AUTO_GROUPS)) {
            A2lAddToGroup(pname);
        }
        fprintf(gA2lFile, "/begin INSTANCE %s \"%s\"", pname, comment);
        fprintf(gA2lFile, " %s 0x%X", A2lGetPrefixedName_(XcpGetProjectName(), typeName), addr);
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

void A2lCreateMeasurement_(const char *instance_name, const char *symbol_name, tA2lTypeId type_id, uint16_t dim, const void *ptr, const char *unit_or_conversion, double phys_min,
                           double phys_max, const char *comment) {
    if (gA2lFile != NULL) {
        uint32_t addr = A2lGetAddr_(ptr);
        uint8_t ext = A2lGetAddrExt_();
        const char *pname = A2lGetPrefixedInstanceName_(instance_name, symbol_name);
        DBG_PRINTF5("A2lCreateMeasurement_: %s, \"%s\", addr=%p, unit=\"%s\", min=%g, max=%g, %s\n", pname, comment != NULL ? comment : "", ptr,
                    unit_or_conversion != NULL ? unit_or_conversion : "", phys_min, phys_max, dbgPrintfAddrExt(ext, addr));
        if ((gA2lMode & A2L_MODE_AUTO_GROUPS)) {
            A2lAddToGroup(pname);
        }
        if (comment == NULL) {
            comment = "";
        }
        double min, max;
        const char *conv;
        if (phys_min == 0.0 && phys_max == 0.0) {
            min = A2lGetTypeMin(type_id);
            max = A2lGetTypeMax(type_id);
            conv = getConversion(unit_or_conversion, &min, &max);
        } else {
            min = phys_min;
            max = phys_max;
            conv = getConversion(unit_or_conversion, NULL, NULL);
        }
        fprintf(gA2lFile, "/begin MEASUREMENT %s \"%s\" %s %s 0 0 %g %g ", pname, comment, A2lGetA2lTypeName(type_id), conv, min, max);
        if (dim > 1) {
            fprintf(gA2lFile, "MATRIX_DIM %u ", dim);
        }
        fprintf(gA2lFile, " ECU_ADDRESS 0x%X", addr);
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

void A2lCreateMeasurementArray_(const char *instance_name, const char *symbol_name, tA2lTypeId type_id, int x_dim, int y_dim, const void *ptr, const char *unit_or_conversion,
                                double phys_min, double phys_max, const char *comment) {
    if (gA2lFile != NULL) {
        uint32_t addr = A2lGetAddr_((const void *)ptr);
        uint8_t ext = A2lGetAddrExt_();
        const char *pname = A2lGetPrefixedInstanceName_(instance_name, symbol_name);
        DBG_PRINTF5("A2lCreateMeasurementArray_: %s, \"%s\", addr=%p, x_dim=%d, y_dim=%d, unit=\"%s\", min=%g, max=%g, %s\n", pname, comment != NULL ? comment : "", ptr, x_dim,
                    y_dim, unit_or_conversion != NULL ? unit_or_conversion : "", phys_min, phys_max, dbgPrintfAddrExt(ext, addr));
        if ((gA2lMode & A2L_MODE_AUTO_GROUPS)) {
            A2lAddToGroup(pname);
        }
        if (comment == NULL)
            comment = "";
        double min, max;
        const char *conv;
        if (phys_min == 0.0 && phys_max == 0.0) {
            min = A2lGetTypeMin(type_id);
            max = A2lGetTypeMax(type_id);
            conv = getConversion(unit_or_conversion, &min, &max);
        } else {
            min = phys_min;
            max = phys_max;
            conv = getConversion(unit_or_conversion, NULL, NULL);
        }
        fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"%s\" VAL_BLK 0x%X %s 0 %s %g %g MATRIX_DIM %u %u", pname, comment, addr, A2lGetA2lRecordLayoutName(type_id), conv, min, max,
                x_dim, y_dim);
        printAddrExt(ext);
        A2lCreateMeasurement_IF_DATA();
        fprintf(gA2lFile, " /end CHARACTERISTIC\n");
        gA2lMeasurements++;
    }
}

//----------------------------------------------------------------------------------
// Parameters

void A2lCreateParameter_(const char *symbol_name, tA2lTypeId type, const void *ptr, const char *comment, const char *unit_or_conversion, double phys_min, double phys_max) {

    if (gA2lFile != NULL) {
        uint32_t addr = A2lGetAddr_(ptr);
        uint8_t ext = A2lGetAddrExt_();
        const char *pname = A2lGetPrefixedName_(XcpGetProjectName(), symbol_name);
        DBG_PRINTF5("A2lCreateParameter_: %s, \"%s\", addr=%p, unit=\"%s\", min=%g, max=%g, %s\n", pname, comment != NULL ? comment : "", ptr,
                    unit_or_conversion != NULL ? unit_or_conversion : "", phys_min, phys_max, dbgPrintfAddrExt(ext, addr));
        if ((gA2lMode & A2L_MODE_AUTO_GROUPS)) {
            A2lAddToGroup(pname);
        }
        double min, max;
        const char *conv;
        if (phys_min == 0.0 && phys_max == 0.0) {
            min = A2lGetTypeMin(type);
            max = A2lGetTypeMax(type);
            conv = getConversion(unit_or_conversion, &min, &max);
        } else {
            min = phys_min;
            max = phys_max;
            conv = getConversion(unit_or_conversion, NULL, NULL);
        }
        fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"%s\" VALUE 0x%X %s 0 %s %g %g", pname, comment, addr, A2lGetA2lRecordLayoutName(type), conv, min, max);
        printPhysUnit(gA2lFile, unit_or_conversion);
        printAddrExt(ext);
        A2lCreateMeasurement_IF_DATA();
        fprintf(gA2lFile, " /end CHARACTERISTIC\n");
        gA2lParameters++;
    }
}

void A2lCreateMap_(const char *symbol_name, tA2lTypeId type_id, const void *ptr, uint32_t xdim, uint32_t ydim, const char *comment, const char *unit, double min, double max,
                   const char *x_axis, const char *y_axis) {

    if (gA2lFile != NULL) {
        uint32_t addr = A2lGetAddr_(ptr);
        uint8_t ext = A2lGetAddrExt_();
        const char *pname = A2lGetPrefixedName_(XcpGetProjectName(), symbol_name);
        DBG_PRINTF5("A2lCreateMap_: %s, \"%s\", addr=%p, x_dim=%d, y_dim=%d, unit=\"%s\", min=%g, max=%g, %s\n", pname, comment != NULL ? comment : "", ptr, xdim, ydim,
                    unit != NULL ? unit : "", min, max, dbgPrintfAddrExt(ext, addr));
        if ((gA2lMode & A2L_MODE_AUTO_GROUPS)) {
            A2lAddToGroup(pname);
        }
        fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"%s\" MAP 0x%X %s 0 NO_COMPU_METHOD %g %g", pname, comment, addr, A2lGetA2lRecordLayoutName(type_id), min, max);
        if (x_axis == NULL) {
            fprintf(gA2lFile, " /begin AXIS_DESCR FIX_AXIS %s NO_COMPU_METHOD %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR", A2lGetInputQuantity_x(), xdim, xdim - 1, xdim);
        } else {
            fprintf(gA2lFile, " /begin AXIS_DESCR COM_AXIS %s NO_COMPU_METHOD %u 0.0 0.0 AXIS_PTS_REF %s /end AXIS_DESCR", A2lGetInputQuantity_x(), xdim, x_axis);
        }
        if (y_axis == NULL) {
            fprintf(gA2lFile, " /begin AXIS_DESCR FIX_AXIS %s NO_COMPU_METHOD %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR", A2lGetInputQuantity_y(), ydim, ydim - 1, ydim);
        } else {
            fprintf(gA2lFile, " /begin AXIS_DESCR COM_AXIS %s NO_COMPU_METHOD %u 0.0 0.0 AXIS_PTS_REF %s /end AXIS_DESCR", A2lGetInputQuantity_y(), ydim, y_axis);
        }
        printPhysUnit(gA2lFile, unit);
        printAddrExt(ext);
        A2lCreateMeasurement_IF_DATA();
        fprintf(gA2lFile, " /end CHARACTERISTIC\n");
        gA2lParameters++;
    }
}

void A2lCreateCurve_(const char *symbol_name, tA2lTypeId type_id, const void *ptr, uint32_t xdim, const char *comment, const char *unit, double min, double max,
                     const char *x_axis) {

    if (gA2lFile != NULL) {
        uint32_t addr = A2lGetAddr_(ptr);
        uint8_t ext = A2lGetAddrExt_();
        const char *pname = A2lGetPrefixedName_(XcpGetProjectName(), symbol_name);
        DBG_PRINTF5("A2lCreateCurve_: %s, \"%s\", addr=%p, x_dim=%d, unit=\"%s\", min=%g, max=%g, %s\n", pname, comment != NULL ? comment : "", ptr, xdim, unit != NULL ? unit : "",
                    min, max, dbgPrintfAddrExt(ext, addr));
        if ((gA2lMode & A2L_MODE_AUTO_GROUPS)) {
            A2lAddToGroup(pname);
        }
        fprintf(gA2lFile, "/begin CHARACTERISTIC %s \"%s\" CURVE 0x%X %s 0 NO_COMPU_METHOD %g %g", pname, comment, addr, A2lGetA2lRecordLayoutName(type_id), min, max);
        if (x_axis == NULL) {
            fprintf(gA2lFile, " /begin AXIS_DESCR FIX_AXIS %s NO_COMPU_METHOD %u 0 %u FIX_AXIS_PAR_DIST 0 1 %u /end AXIS_DESCR", A2lGetInputQuantity_x(), xdim, xdim - 1, xdim);
        } else {
            fprintf(gA2lFile, " /begin AXIS_DESCR COM_AXIS %s NO_COMPU_METHOD %u 0.0 0.0 AXIS_PTS_REF %s /end AXIS_DESCR", A2lGetInputQuantity_x(), xdim, x_axis);
        }
        printPhysUnit(gA2lFile, unit);
        printAddrExt(ext);
        A2lCreateMeasurement_IF_DATA();

        fprintf(gA2lFile, " /end CHARACTERISTIC\n");
        gA2lParameters++;
    }
}

void A2lCreateAxis_(const char *symbol_name, tA2lTypeId type_id, const void *ptr, uint32_t xdim, const char *comment, const char *unit, double min, double max) {

    if (gA2lFile != NULL) {
        uint32_t addr = A2lGetAddr_(ptr);
        uint8_t ext = A2lGetAddrExt_();
        const char *pname = A2lGetPrefixedName_(XcpGetProjectName(), symbol_name);
        DBG_PRINTF5("A2lCreateAxis_: %s, \"%s\", addr=%p, x_dim=%d, unit=\"%s\", min=%g, max=%g, %s\n", pname, comment != NULL ? comment : "", ptr, xdim, unit != NULL ? unit : "",
                    min, max, dbgPrintfAddrExt(ext, addr));
        if ((gA2lMode & A2L_MODE_AUTO_GROUPS)) {
            A2lAddToGroup(pname);
        }
        fprintf(gA2lFile, "/begin AXIS_PTS %s \"%s\" 0x%X %s A_%s 0 NO_COMPU_METHOD %u %g %g", pname, comment, addr, A2lGetInputQuantity_x(), A2lGetA2lRecordLayoutName(type_id),
                xdim, min, max);
        printPhysUnit(gA2lFile, unit);
        printAddrExt(ext);
        A2lCreateMeasurement_IF_DATA();
        fprintf(gA2lFile, " /end AXIS_PTS\n");
        gA2lParameters++;
    }
}

//----------------------------------------------------------------------------------
// Automatic group generation

// gA2lAutoGroupName  is the name of the current open group
// gA2lAutoGroupIsParameter or gA2lAutoGroupIsMeasurement indicates if the current group is for parameters or measurements

// Begin a group for measurements or parameters
void A2lBeginGroup(const char *symbol_name, const char *comment, bool is_parameter_group, bool is_root_group) {
    if (gA2lFile != NULL && gA2lGroupsFile != NULL) {
        const char *pname = A2lGetPrefixedName_(XcpGetProjectName(), symbol_name);
        if ((strcmp(pname, gA2lAutoGroupName) != 0)) {
            DBG_PRINTF5("A2lBeginGroup: %s, comment='%s', is_parameter_group=%d, is_root_group=%d\n", pname, comment != NULL ? comment : "", is_parameter_group, is_root_group);
            A2lEndGroup(); // Close previous group if any
            // Groups are a global name space, prefix the group name if needed
            strncpy(gA2lAutoGroupName, pname, XCP_A2L_MAX_SYMBOL_NAME_LENGTH - 1);
            gA2lAutoGroupName[XCP_A2L_MAX_SYMBOL_NAME_LENGTH - 1] = '\0'; // Ensure null termination
            gA2lAutoGroupIsParameter = is_parameter_group;
            gA2lAutoGroupIsMeasurement = !is_parameter_group;
            fprintf(gA2lGroupsFile, "/begin GROUP %s \"%s\" %s", pname, comment, is_root_group ? "ROOT" : "");
            fprintf(gA2lGroupsFile, " /begin REF_%s", is_parameter_group ? "CHARACTERISTIC" : "MEASUREMENT");
        }
    }
}

// Add a measurement or parameter to the current open group
// Assuming name is already prefixed
void A2lAddToGroup(const char *name) {
    if (gA2lFile != NULL && gA2lGroupsFile != NULL) {
        if (gA2lAutoGroupIsParameter || gA2lAutoGroupIsMeasurement) {
            fprintf(gA2lGroupsFile, " %s", name);
        }
    }
}

// End the current open group for measurements or parameters
void A2lEndGroup(void) {
    if (gA2lFile != NULL && gA2lGroupsFile != NULL) {
        if (gA2lAutoGroupIsParameter || gA2lAutoGroupIsMeasurement) {
            DBG_PRINTF5("A2lEndGroup: %s\n", gA2lAutoGroupName);
            fprintf(gA2lGroupsFile, " /end REF_%s", gA2lAutoGroupIsParameter ? "CHARACTERISTIC" : "MEASUREMENT");
            fprintf(gA2lGroupsFile, " /end GROUP\n");
            gA2lAutoGroupName[0] = '\0';
            gA2lAutoGroupIsParameter = false;
            gA2lAutoGroupIsMeasurement = false;
        }
    }
}

//----------------------------------------------------------------------------------
// Create groups

void A2lCreateMeasurementGroup(const char *symbol_name, int count, ...) {
    if (gA2lFile != NULL && gA2lGroupsFile != NULL) {
        va_list ap;
        A2lEndGroup(); // End the previous group if any
        const char *pname = A2lGetPrefixedName_(XcpGetProjectName(), symbol_name);
        fprintf(gA2lGroupsFile, "/begin GROUP %s \"\" ROOT", pname);
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

void A2lCreateMeasurementGroupFromList(const char *symbol_name, char *names[], uint32_t count) {
    if (gA2lFile != NULL && gA2lGroupsFile != NULL) {
        A2lEndGroup(); // End the previous group if any
        const char *pname = A2lGetPrefixedName_(XcpGetProjectName(), symbol_name);
        fprintf(gA2lGroupsFile, "/begin GROUP %s \"\" ROOT", pname);
        fprintf(gA2lGroupsFile, " /begin REF_MEASUREMENT");
        for (uint32_t i1 = 0; i1 < count; i1++) {
            fprintf(gA2lGroupsFile, " %s", names[i1]);
        }
        fprintf(gA2lGroupsFile, " /end REF_MEASUREMENT");
        fprintf(gA2lGroupsFile, " /end GROUP\n");
    }
}

void A2lCreateParameterGroup(const char *symbol_name, int count, ...) {
    if (gA2lFile != NULL && gA2lGroupsFile != NULL) {
        va_list ap;
        A2lEndGroup(); // End the previous group if any
        const char *pname = A2lGetPrefixedName_(XcpGetProjectName(), symbol_name);
        fprintf(gA2lGroupsFile, "/begin GROUP %s \"\" ROOT", pname);
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

void A2lCreateParameterGroupFromList(const char *symbol_name, const char *pNames[], int count) {
    if (gA2lFile != NULL && gA2lGroupsFile != NULL) {
        A2lEndGroup(); // End the previous group if any
        const char *pname = A2lGetPrefixedName_(XcpGetProjectName(), symbol_name);
        fprintf(gA2lGroupsFile, "/begin GROUP %s \"\" ROOT", pname);
        fprintf(gA2lGroupsFile, " /begin REF_CHARACTERISTIC");
        for (int i = 0; i < count; i++) {
            fprintf(gA2lGroupsFile, " %s", pNames[i]);
        }
        fprintf(gA2lGroupsFile, " /end REF_CHARACTERISTIC");
        fprintf(gA2lGroupsFile, " /end GROUP\n\n");
    }
}

//----------------------------------------------------------------------------------
// User helper function to ensure A2L generation blocks are executed only once
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

// Cleanup temporary files in case of A2L generation cancellation
void A2lCleanupTemporaryFiles(void) {
    DBG_PRINT3(ANSI_COLOR_YELLOW "Cleanup temporary A2L files ...\n" ANSI_COLOR_RESET);
    remove(A2lGetFilename(A2L_TYPEDEFS_FILE));
    gA2lTypedefsFile = NULL;
    remove(A2lGetFilename(A2L_GROUPS_FILE));
    gA2lGroupsFile = NULL;
    remove(A2lGetFilename(A2L_CONVERSIONS_FILE));
    gA2lConversionsFile = NULL;
}

// Callback on XCP client tool connect
bool A2lCheckFinalizeOnConnect(uint8_t connect_mode) {

    // Finalize A2l once on connect, if A2L generation is active
    if ((gA2lMode & A2L_MODE_FINALIZE_ON_CONNECT) && gA2lFile != NULL) {
        A2lFinalize();
    }

    // If A2l generation is active, refuse connect
    else if (gA2lFile != NULL) {
        DBG_PRINT_WARNING("A2L file not finalized yet, XCP connect refused!\n");
        return false; // Refuse connect, waiting for finalization by application
    }

    return true;
}

// Lock and unlock
void A2lLock(void) {
    if (gA2lFile != NULL) {
        mutexLock(&gA2lMutex);
    }
}
void A2lUnlock(void) {
    if (gA2lFile != NULL) {
        if ((gA2lMode & A2L_MODE_AUTO_GROUPS)) {
            A2lEndGroup();
        }
        mutexUnlock(&gA2lMutex);
    }
}

// Start A2L generator
bool A2lInit(const uint8_t *addr, uint16_t port, bool useTCP, uint8_t mode) {

    assert(addr != NULL);

    // Check and ignore, if the XCP singleton has not been initialized and activated
    if (!XcpIsActivated()) {
        DBG_PRINT5("A2lInit: XCP is deactivated!\n");
        return true;
    }

    // Check and ignore, if the A2L file is already finalized or A2L generation is already started
    if (gA2lIsFinalized || gA2lFile != NULL) {
        DBG_PRINT5("A2lInit: A2L file is already finalized or running!\n");
        return true;
    }

    // Save communication parameters
    memcpy(&gA2lOptionBindAddr, addr, 4);
    gA2lOptionPort = port;
    gA2lUseTCP = useTCP;

    // Save mode
    gA2lMode = mode;

#ifndef OPTION_ENABLE_PERSISTENCE
    assert(gA2lMode & A2L_MODE_WRITE_ALWAYS && "Persistence mode not enabled, mode A2L_MODE_WRITE_ONCE cannot be used!");
#else
    if ((gA2lMode & A2L_MODE_WRITE_ALWAYS) == 0 && (XcpGetInitMode() & XCP_MODE_PERSISTENCE) == 0) {
        mode |= A2L_MODE_WRITE_ALWAYS;
        DBG_PRINT_WARNING("Persistence mode not enabled, mode A2L_MODE_WRITE_ONCE ignored!\n");
    }
#endif

    // Initialize
    A2lRstAddrMode();
    gA2lMeasurements = gA2lParameters = gA2lTypedefs = gA2lInstances = gA2lConversions = gA2lComponents = 0;
    mutexInit(&gA2lMutex, false, 0);

    // Register a callback called on XCP connect
    ApplXcpRegisterConnectCallback(A2lCheckFinalizeOnConnect);

    DBG_PRINTF3("Initialized A2L generation: mode=(%s%s%s%s%s%s)\n", (mode & A2L_MODE_WRITE_ALWAYS) ? "WRITE_ALWAYS " : "", (mode & A2L_MODE_WRITE_ONCE) ? "WRITE_ONCE " : "",
                (mode & A2L_MODE_FINALIZE_ON_CONNECT) ? "FINALIZE_ON_CONNECT " : "", (mode & A2L_MODE_AUTO_GROUPS) ? "AUTO_GROUPS " : "",
                (mode & A2L_MODE_SYMBOL_PREFIX) ? "SYMBOL_PREFIX " : "", (mode & A2L_MODE_WRITE_TEMPLATE) ? "WRITE_TEMPLATE " : "");

    // In A2L_WRITE_ONCE mode:
    // Check if the A2L file already exists, if yes, skip A2L generation
    // @@@@ TODO: Maybe better be sure the persistence BIN file has been successfully loaded
    const char *a2l_filename = A2lGetFilename(A2L_FILE);
    if ((gA2lMode & A2L_MODE_WRITE_ALWAYS) == 0 && fexists(a2l_filename)) {
#ifdef OPTION_SHM_MODE // Set finalized A2L name inSHM application list
        // Notify the application A2L file is finalized, not the master A2L file
        XcpShmSetA2lFinalized(XcpShmGetAppId(), a2l_filename);
#else
        XcpSetA2lName(a2l_filename); // Notify XCP that there is an existing A2L file available on disk, ready for upload
#endif
        DBG_PRINTF3(ANSI_COLOR_GREEN "A2L file %s already exists with matching version, disabling A2L generation\n" ANSI_COLOR_RESET, a2l_filename);
        return true; // Skip A2L generation
    }

#ifdef OPTION_SHM_MODE // prefix all symbols in SHM mode to avoid conflicts between applications
    gA2lMode |= A2L_MODE_SYMBOL_PREFIX;
#endif

    // Start A2L generator
    if (!(gA2lMode & A2L_MODE_WRITE_TEMPLATE)) {

        DBG_PRINTF3(ANSI_COLOR_GREEN "Start A2L generator, file=%s\n" ANSI_COLOR_RESET, a2l_filename);
        gA2lFile = fopen(a2l_filename, "w");
        if (gA2lFile == NULL) {
            DBG_PRINTF_ERROR("Could not create file %s!\n", a2l_filename);
            return false;
        }

        // Open the temporary files for typedefs, groups and conversions
        gA2lTypedefsFile = fopen(A2lGetFilename(A2L_TYPEDEFS_FILE), "w");
        gA2lGroupsFile = fopen(A2lGetFilename(A2L_GROUPS_FILE), "w");
        gA2lConversionsFile = fopen(A2lGetFilename(A2L_CONVERSIONS_FILE), "w");
        if (gA2lFile == 0 || gA2lTypedefsFile == 0 || gA2lGroupsFile == 0 || gA2lConversionsFile == 0) {
            DBG_PRINT_ERROR("Could not create file!\n");
            return false;
        }
        fprintf(gA2lTypedefsFile, "\n/* Typedefs */\n");       // typedefs temporary file
        fprintf(gA2lGroupsFile, "\n/* Groups */\n");           // groups temporary file
        fprintf(gA2lConversionsFile, "\n/* Conversions */\n"); // conversions temporary file
    }

    return true;
}

// Finalize A2L file generation user function
// Return true if A2L file generation was active and is now finalized, false if A2L file generation was not active
bool A2lFinalize(void) {

    if (gA2lFile == NULL && !(gA2lMode & A2L_MODE_WRITE_TEMPLATE))
        return false; // Not active, nothing to finalize

    if (gA2lIsFinalized)
        return true; // Already finalized

    DBG_PRINT3(ANSI_COLOR_GREEN "Finalizing A2L file...\n" ANSI_COLOR_RESET);

#ifdef OPTION_SHM_MODE // server signals all applications to finalize their A2L files
    // Later we will wait for the results and merge the partial A2L files into the main A2L file
    if (XcpShmIsXcpServer()) {
        XcpShmRequestA2lFinalize();
    }
#endif // SHM_MODE

    // Finalize the partial A2L file for this application
    if (gA2lFile != NULL) {

        // Close the last group if any
        if ((gA2lMode & A2L_MODE_AUTO_GROUPS)) {
            A2lEndGroup();
        }

        // Merge the temporary files for typedefs, groups and conversions into the partial A2L file, and remove the temporary files
        includeFilesAndRemove(gA2lFile, &gA2lTypedefsFile, A2lGetFilename(A2L_TYPEDEFS_FILE));
        includeFilesAndRemove(gA2lFile, &gA2lGroupsFile, A2lGetFilename(A2L_GROUPS_FILE));
        includeFilesAndRemove(gA2lFile, &gA2lConversionsFile, A2lGetFilename(A2L_CONVERSIONS_FILE));

        // Close the partial A2L file
        fclose(gA2lFile);
        gA2lFile = NULL;
    }

// Generate the final, complete A2L file
#ifdef OPTION_SHM_MODE // finalize and generate A2L file, server includes all others
    // only the server generates the main A2L file and includes all the partial A2L files created by the applications
    const char *files[SHM_MAX_APP_COUNT];
    int count = 0;
    XcpShmSetA2lFinalized(XcpShmGetAppId(), A2lGetFilename(A2L_FILE));
    if (XcpShmIsXcpServer()) {
        count = XcpShmCollectA2lFiles(1000 /* ms */, files, SHM_MAX_APP_COUNT);
    }
#else
    // In non-SHM mode, generate the main A2L file by including the single partial A2L file created by this application
    // Note that the A2lWriter can handle if the same file is both the source and the destination
    const char *files[1] = {A2lGetFilename(A2L_FILE)};
    int count = 1;
#endif
    if (count > 0) {
        A2lWriter(A2lGetFilename(A2L_MAIN_FILE), gA2lMode, count, files, gA2lOptionBindAddr, gA2lOptionPort, gA2lUseTCP);
    }

// Write the binary persistence file
// This is required to make sure the A2L file(s) remains valid, even if the creation order of application, events or calibration segments is different
#ifdef OPTION_SHM_MODE // write the binary persistence file and udate EPK hash

    // In SHM mode, the server provides the main file for upload
    // The server provides the main file for upload and creates the binary persistence file
    if (XcpShmIsXcpServer()) {
        // Regenerate the EPK and write to the EPK segment, so the the BIN file get it as well and the client can upload it
        const char *epk = XcpGetEcuEpk(); // Get (generate) the current ECU EPK for all existing applications
        const uint16_t epk_len = (uint16_t)STRNLEN(epk, XCP_EPK_MAX_LENGTH) + 1;
        // @@@@ TODO: Remove magic number for EPK segment address,
        XcpCalSegWriteMemory(0x80000000, epk_len, (const uint8_t *)epk);
        const char *epk_calseg = (const char *)XcpLockCalSeg(XCP_EPK_CALSEG_INDEX);
        assert(strncmp(epk, epk_calseg, epk_len) == 0);
        XcpUnlockCalSeg(0);
        // Write the binary persistence file
        XcpBinWrite(epk);
        // Notify the XCP server A2L file is available for upload
        XcpSetA2lName(A2lGetFilename(A2L_MAIN_FILE)); // Notify XCP that there is an A2L file available for upload by the XCP client
    }

#else

    // Create the binary persistence file associated to this A2L file
#ifdef OPTION_ENABLE_PERSISTENCE
    if ((XcpGetInitMode() & XCP_MODE_PERSISTENCE)) {
        // Get the current EPK set by the application
        const char *epk = XcpGetEpk();
        // Write the binary persistence file
        XcpBinWrite(epk);
    }
#endif
    // Notify the XCP server A2L file is available for upload
    XcpSetA2lName(A2lGetFilename(A2L_FILE));

#endif

    gA2lIsFinalized = true;
    DBG_PRINTF3(ANSI_COLOR_GREEN "A2L created: %u measurements, %u params, %u typedefs, %u components, %u instances, %u conversions\n" ANSI_COLOR_RESET, gA2lMeasurements,
                gA2lParameters, gA2lTypedefs, gA2lComponents, gA2lInstances, gA2lConversions);
    return true; // A2L file generation successful
}

#endif // XCP_ENABLE_A2L_GENERATOR
