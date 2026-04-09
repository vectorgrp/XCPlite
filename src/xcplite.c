/*****************************************************************************
| File:
|   xcplite.c
|
|  Description:
|    Implementation of the ASAM XCP Protocol Layer V1.4
|    Version V1.2.0
|       - Designed and optimized for 64 bit POSIX based platforms (Linux or MacOS)
|       - Tested on x86 strong and ARM weak memory model
|       - Can be adapted for 32 bit platforms
|       - Runs on Windows for demonstration purposes with some limitations

|
|  Limitations:
|       - 8 bit and 16 bit CPUs are not supported
|       - No Motorola byte sex
|       - No misra compliance
|       - Overall number of ODTs limited to 64K
|       - Overall number of ODT entries is limited to 64K
|       - Fixed ODT-BYTE,res-BYTE, DAQ-WORD DTO header
|       - Fixed 32 bit time stamp
|       - Only dynamic DAQ list allocation supported
|       - Resume is not supported
|       - Overload indication by event is not supported
|       - ODT optimization not supported
|       - Seed & key is not supported
|       - Flash programming is not supported
|
|  For micro-controllers, more features and more transport layers (CAN, FlexRay) are provided
|  by the free XCP basic version available from Vector Informatik GmbH at www.vector.com
|
|  Limitations of the XCP basic version:
|     - Stimulation (Bypassing) is not available|
|     - Bit stimulation is not available
|     - SHORT_DOWNLOAD is not implemented
|     - MODIFY_BITS is not available|
|     - FLASH and EEPROM Programming is not available|
|     - Block mode for UPLOAD, DOWNLOAD and PROGRAM is not available
|     - Resume mode is not available|
|     - Memory write and read protection is not supported
|     - Checksum calculation supports only CRC CCITT16 or ADD44
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
|  No limitations and full compliance are available with the commercial version
|  from Vector Informatik GmbH, please contact Vector
|***************************************************************************/

#include "xcplib_cfg.h" // for OPTION_xxx

#include "xcp_cfg.h"   // XCP protocol layer configuration parameters (XCP_xxx)
#include "xcptl_cfg.h" // XCP transport layer configuration parameters (XCPTL_xxx)

#include "xcplite.h" // XCP protocol layer interface functions

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIx32, PRIu64
#include <stdarg.h>   // for va_list, va_start, va_arg, va_end
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uint8_t, uint16_t,...
#include <stdio.h>    // for printf
#include <stdlib.h>   // for size_t, NULL, abort
#include <string.h>   // for memcpy, memset, strlen, strncpy

#ifdef OPTION_SHM_MODE
#include <unistd.h> // for getpid()
#endif

#include "dbg_print.h" // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#include "platform.h"  // for atomics

#include "cal.h"         // for XcpCalSegXxx
#include "persistence.h" // for XcpBinFreezeCalSeg
#include "queue.h"       // for QueueXxx transport queue layer interface
#include "shm.h"         // for shared memory management
#include "xcp.h"         // XCP protocol definitions

#include "xcptl.h" // for transport layer abstraction XcpTlWaitForTransmitQueueEmpty and XcpTlSendCrm
#if defined(XCPTL_ENABLE_MULTICAST)
#include "xcpethtl.h" // for ethernet specific transport layer functions XcpEthTl
#endif

/****************************************************************************/
/* Defaults and checks                                                      */
/****************************************************************************/

/* Check limits of the XCP imnplementation */
#if defined(XCPTL_MAX_CTO_SIZE)
#if (XCPTL_MAX_CTO_SIZE > 255)
#error "XCPTL_MAX_CTO_SIZE must be <= 255"
#endif
#if (XCPTL_MAX_CTO_SIZE < 8)
#error "XCPTL_MAX_CTO_SIZE must be >= 8"
#endif
#else
#error "Please define XCPTL_CTO_SIZE"
#endif

#if defined(XCPTL_MAX_DTO_SIZE)
#if (XCPTL_MAX_DTO_SIZE > (XCPTL_MAX_SEGMENT_SIZE - XCPTL_HEADER_SIZE))
#error "XCPTL_MAX_DTO_SIZE too large"
#endif
#if (XCPTL_MAX_DTO_SIZE < 8)
#error "XCPTL_MAX_DTO_SIZE must be >= 8"
#endif
#if (XCPTL_MAX_DTO_SIZE < XCPTL_MAX_CTO_SIZE)
#error "XCPTL_MAX_DTO_SIZE must be >= XCPTL_MAX_CTO_SIZE"
#endif
#else
#error "Please define XCPTL_MAX_DTO_SIZE"
#endif

/* Max. size of an object referenced by an ODT entry XCP_MAX_ODT_ENTRY_SIZE may be limited  */
/* Default 248 */
#if defined(XCP_MAX_ODT_ENTRY_SIZE)
#if (XCP_MAX_DTO_ENTRY_SIZE > 255)
#error "XCP_MAX_ODT_ENTRY_SIZE too large"
#endif
#else
#define XCP_MAX_ODT_ENTRY_SIZE 248 // mod 4 = 0 to optimize DAQ copy granularity
#endif

/* Check XCP_DAQ_MEM_SIZE */
#if defined(XCP_DAQ_MEM_SIZE)
#if (XCP_DAQ_MEM_SIZE > 0xFFFFFFFF)
#error "XCP_DAQ_MEM_SIZE must be <= 0xFFFFFFFF"
#endif
#else
#error "Please define XCP_DAQ_MEM_SIZE"
#endif

/* Check XCP_CAL_MEM_SIZE */
#if defined(XCP_CAL_MEM_SIZE)
#if (XCP_CAL_MEM_SIZE > 0xFFFFFFFF)
#error "XCP_CAL_MEM_SIZE must be <= 0xFFFFFFFF"
#endif
#else
#error "Please define XCP_CAL_MEM_SIZE"
#endif

/* Check length of of names with null termination must be even*/
#if XCP_EPK_MAX_LENGTH & 1 == 0 || XCP_EPK_MAX_LENGTH >= 128
#error "XCP_EPK_MAX_LENGTH must be <128 and odd for null termination"
#endif

#if XCP_MAX_EVENT_NAME & 1 == 0 || XCP_MAX_EVENT_NAME >= 128
#error "XCP_MAX_EVENT_NAME must be <128 and odd for null termination"
#endif

/****************************************************************************/
/* XCPlite memory signature (XCPLITE__XXXXX)                               */
/****************************************************************************/

// Current supported addressing schemes are:
// For A2L-Toolset compatibility: Absolute addressing mode - XCP_ADDRESS_MODE_XCPLITE__ACSDD
// For SHM mode: Relative segment addressing mode - XCP_ADDRESS_MODE_XCPLITE__CXSDD
// Default: Segment relative addressing mode - XCP_ADDRESS_MODE_XCPLITE__CASDD
#ifndef _WIN
__attribute__((used))
#endif
#if defined(XCP_ADDRESS_MODE_XCPLITE__CXSDD)
const uint16_t XCPLITE__CXSDD = XCP_DRIVER_VERSION;
#elif defined(XCP_ADDRESS_MODE_XCPLITE__ACSDD)
const uint16_t XCPLITE__ACSDD = XCP_DRIVER_VERSION;
#elif defined(XCP_ADDRESS_MODE_XCPLITE__CASDD)
const uint16_t XCPLITE__CASDD = XCP_DRIVER_VERSION;
#else
#error "Please define one of XCP_ADDRESS_MODE_XCPLITE__ACSDD, XCP_ADDRESS_MODE_XCPLITE__CASDD, XCP_ADDRESS_MODE_XCPLITE__CXSDD"
#endif

/****************************************************************************/
/* Protocol layer state data                                                */
/****************************************************************************/

// XCP singleton state
#ifdef OPTION_SHM_MODE // define gXcpData as a pointer
tXcpData *gXcpData = NULL;
#else
tXcpData gXcpData = {0};
#endif
tXcpLocalData gXcpLocalData = {0}; // XCP_MODE_DEACTIVATE by default

// Debug
// Test the thread safety concept
// - Assert mutable access to the XCP singleton is either safe or allowed to the owner thread
// - Assert read access by other threads is only allowed during DAQ running
#ifdef TEST_MUTABLE_ACCESS_OWNERSHIP

#include <pthread.h>

static pthread_t gXcpOwnerThread;
static bool gXcpOwnerThreadValid = false;

static void XcpBindOwnerThread(void) {
    pthread_t self = pthread_self();
    gXcpOwnerThread = self;
    gXcpOwnerThreadValid = true;
}

static inline tXcpData *XcpMut_(const char *file, int line) {
    if (!pthread_equal(gXcpOwnerThread, pthread_self())) {
        DBG_PRINTF_ERROR("Mutable access to XCP singleton data from non-owner thread in file %s, line %d!\n", file, line);
    }
    return gXcpData;
}

#define shared (*(const tXcpData *)gXcpData)           // Shortcut for read only access to the XCP singleton data
#define shared_mut (*XcpMut_(__FILE__, __LINE__))      // Shortcut for mutable access to the XCP singleton data (checked ownership)
#define shared_mut_safe (*gXcpData)                    // Shortcut for mutable access to the XCP singleton data (not checked)
#define local (*(const tXcpLocalData *)&gXcpLocalData) // Read-only access to process-local state
#define local_mut gXcpLocalData                        // Mutable access to process-local state

#elif defined(OPTION_SHM_MODE) // aliases for gXcpData which is a pointer to the shared memory in SHM mode

#define shared (*(const tXcpData *)gXcpData) // Shortcut for read only access to the XCP singleton data
#define shared_mut (*gXcpData)               // Shortcut for mutable access to the XCP singleton data
#define shared_mut_safe (*gXcpData)          // Shortcut for mutable access to the XCP singleton data

#else

#define shared (*(const tXcpData *)&gXcpData) // Shortcut for read only access to the XCP singleton data
#define shared_mut gXcpData                   // Shortcut for mutable access to the XCP singleton data
#define shared_mut_safe gXcpData              // Shortcut for mutable access to the XCP singleton data

#endif

#define local (*(const tXcpLocalData *)&gXcpLocalData) // Read-only access to process-local state
#define local_mut gXcpLocalData                        // Mutable access to process-local state

// Global state checks
#ifdef OPTION_SHM_MODE // gXcpData is a pointer to the shared state in SHM mode
#define isActivated() (gXcpData != NULL && 0 != (gXcpData->session_status & SS_ACTIVATED))
#define isStarted() (gXcpData != NULL && 0 != (gXcpData->session_status & SS_STARTED))
#define isConnected() (gXcpData != NULL && 0 != (gXcpData->session_status & SS_CONNECTED))
#define isLegacyMode() (gXcpData != NULL && 0 != (gXcpData->session_status & SS_LEGACY_MODE))
#define isDaqRunning() (gXcpData != NULL && atomic_load_explicit(&gXcpData->daq_running, memory_order_relaxed))
#else
#define isActivated() (0 != (gXcpData.session_status & SS_ACTIVATED))
#define isStarted() (0 != (gXcpData.session_status & SS_STARTED))
#define isConnected() (0 != (gXcpData.session_status & SS_CONNECTED))
#define isLegacyMode() (0 != (gXcpData.session_status & SS_LEGACY_MODE))
#define isDaqRunning() (0 != (gXcpData.session_status & SS_STARTED) && atomic_load_explicit(&gXcpData.daq_running, memory_order_relaxed))
#endif

// Thread safe state checks

/****************************************************************************/
/* Forward declarations of static functions                                 */
/****************************************************************************/

static uint8_t XcpAsyncCommand(bool async, const uint32_t *cmdBuf, uint8_t cmdLen);

/****************************************************************************/
/* Macros                                                                   */
/****************************************************************************/

// DAQ list access shortcuts
// j is absolute odt number
// i is daq number
#define DaqListOdtTable ((const tXcpOdt *)&shared.daq_lists.u.daq_list[shared.daq_lists.daq_count])
#define DaqListOdtEntryAddrTable ((const uint32_t *)&DaqListOdtTable[shared.daq_lists.odt_count])
#define DaqListOdtEntrySizeTable ((const uint8_t *)&DaqListOdtEntryAddrTable[shared.daq_lists.odt_entry_count])
#ifdef XCP_ENABLE_DAQ_ADDREXT
#define DaqListOdtEntryAddrExtTable ((const uint8_t *)&DaqListOdtEntrySizeTable[shared.daq_lists.odt_entry_count])
#endif
#define DaqListOdtTableMut ((tXcpOdt *)&shared_mut.daq_lists.u.daq_list[shared.daq_lists.daq_count])
#define DaqListOdtEntryAddrTableMut ((uint32_t *)&DaqListOdtTableMut[shared.daq_lists.odt_count])
#define DaqListOdtEntrySizeTableMut ((uint8_t *)&DaqListOdtEntryAddrTableMut[shared.daq_lists.odt_entry_count])
#ifdef XCP_ENABLE_DAQ_ADDREXT
#define DaqListOdtEntryAddrExtTableMut ((uint8_t *)&DaqListOdtEntrySizeTableMut[shared.daq_lists.odt_entry_count])
#endif
#define DaqListOdtEntryCount(j) ((DaqListOdtTable[j].last_odt_entry - DaqListOdtTable[j].first_odt_entry) + 1)
#define DaqListOdtCount(i) ((shared.daq_lists.u.daq_list[i].last_odt - shared.daq_lists.u.daq_list[i].first_odt) + 1)
#define DaqListLastOdt(i) shared.daq_lists.u.daq_list[i].last_odt
#define DaqListFirstOdt(i) shared.daq_lists.u.daq_list[i].first_odt
#define DaqListMode(i) shared.daq_lists.u.daq_list[i].mode
#define DaqListModeMut(i) shared_mut.daq_lists.u.daq_list[i].mode
#define DaqListState(i) shared.daq_lists.u.daq_list[i].state
#define DaqListStateMut(i) shared_mut.daq_lists.u.daq_list[i].state
#define DaqListEventChannel(i) shared.daq_lists.u.daq_list[i].event_id
#define DaqListEventChannelMut(i) shared_mut.daq_lists.u.daq_list[i].event_id
#define DaqListAddrExt(i) shared.daq_lists.u.daq_list[i].addr_ext
#define DaqListAddrExtMut(i) shared_mut.daq_lists.u.daq_list[i].addr_ext
#define DaqListPriority(i) shared.daq_lists.u.daq_list[i].priority
#define DaqListPriorityMut(i) shared_mut.daq_lists.u.daq_list[i].priority
#ifdef XCP_MAX_EVENT_COUNT
#ifdef XCP_ENABLE_DAQ_EVENT_LIST
#define DaqListFirst(event_id) shared.event_list.event[event_id].daq_first
#define DaqListFirstMut(event_id) shared_mut.event_list.event[event_id].daq_first
#else
#define DaqListFirst(event_id) shared.daq_lists.daq_first[event_id]
#define DaqListFirstMut(event_id) shared_mut.daq_lists.daq_first[event_id]
#endif
#define DaqListNext(daq) shared.daq_lists.u.daq_list[daq].next
#define DaqListNextMut(daq) shared_mut.daq_lists.u.daq_list[daq].next
#endif

// Command response buffer access shortcuts
#define CRM_LEN shared_mut.crm_len
#define CRM shared_mut.crm
#define CRM_BYTE(x) (shared_mut.crm.b[x])
#define CRM_WORD(x) (shared_mut.crm.w[x])
#define CRM_DWORD(x) (shared_mut.crm.dw[x])

// Error handling
#define error(e)                                                                                                                                                                   \
    {                                                                                                                                                                              \
        err = (e);                                                                                                                                                                 \
        goto negative_response;                                                                                                                                                    \
    }
#define check_error(e)                                                                                                                                                             \
    {                                                                                                                                                                              \
        err = (e);                                                                                                                                                                 \
        if (err != 0)                                                                                                                                                              \
            goto negative_response;                                                                                                                                                \
    }

/****************************************************************************/
// Metrics
/****************************************************************************/

#ifdef TEST_ENABLE_DBG_METRICS
uint32_t gXcpWritePendingCount = 0;
uint32_t gXcpCalSegPublishAllCount = 0;
uint32_t gXcpDaqEventCount = 0;
uint32_t gXcpTxPacketCount = 0;
uint32_t gXcpTxMessageCount = 0;
uint32_t gXcpTxIoVectorCount = 0;
uint32_t gXcpRxPacketCount = 0;
#endif

/****************************************************************************/
// Logging
/****************************************************************************/

#if defined(OPTION_ENABLE_DBG_PRINTS) && !defined(OPTION_FIXED_DBG_LEVEL) && defined(OPTION_DEFAULT_DBG_LEVEL)

uint8_t gXcpLogLevel = OPTION_DEFAULT_DBG_LEVEL;

// Set the log level
void XcpSetLogLevel(uint8_t level) {
#ifdef OPTION_MAX_DBG_LEVEL
    if (level > OPTION_MAX_DBG_LEVEL) {
        DBG_PRINTF_ERROR("Set log level %u > OPTION_MAX_DBG_LEVEL %u\n", level, OPTION_MAX_DBG_LEVEL);
    } else
#endif
        if (level > 3) {
        DBG_PRINTF_WARNING("Set log level %u -> %u\n", gXcpLogLevel, level);
    }
    gXcpLogLevel = level;
}

#else

// Set the log level dummy, log level is a constant or logging is off
void XcpSetLogLevel(uint8_t level) {
    (void)level;
    DBG_PRINTF_ERROR("XcpSetLogLevel ignored, fixed log level = %u\n", OPTION_FIXED_DBG_LEVEL);
}

#endif

/****************************************************************************/
// Test instrumentation
/****************************************************************************/

#ifdef XCP_ENABLE_TEST_CHECKS
#define check_len(n)                                                                                                                                                               \
    if (CRO_LEN < (n)) {                                                                                                                                                           \
        err = CRC_CMD_SYNTAX;                                                                                                                                                      \
        goto negative_response;                                                                                                                                                    \
    }
#else
#define check_len(n)
#endif

#ifdef DBG_LEVEL
static void XcpPrintCmd(const tXcpCto *cro);
static void XcpPrintRes(const tXcpCto *crm);
static void XcpPrintDaqList(uint16_t daq);
#endif

/****************************************************************************/
/* Status                                                                   */
/****************************************************************************/

bool XcpIsActivated(void) { return isActivated(); }
uint8_t XcpGetInitMode(void) { return local.init_mode; }
uint16_t XcpGetSessionStatus(void) { return shared.session_status; }
bool XcpIsStarted(void) { return isStarted(); }
bool XcpIsConnected(void) { return isConnected(); }
bool XcpIsDaqRunning(void) { return isDaqRunning(); }

bool XcpIsDaqEventRunning(uint16_t event) {

    if (!isDaqRunning())
        return false; // DAQ not running

    for (uint16_t daq = 0; daq < shared.daq_lists.daq_count; daq++) {
        if ((DaqListState(daq) & DAQ_STATE_RUNNING) == 0)
            continue; // DAQ list not active
        if (DaqListEventChannel(daq) == event)
            return true; // Event is associated to this DAQ list
    }

    return false;
}

#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
uint16_t XcpGetClusterId(void) { return local.cluster_id; }
#endif

uint64_t XcpGetDaqStartTime(void) { return local.daq_start_clock; }

uint32_t XcpGetDaqOverflowCount(void) { return shared.daq_overflow_count; }

/**************************************************************************/
/* Project/ECU name                                                       */
/**************************************************************************/

// Set the project name
static void XcpSetProjectName(const char *name) {

    assert(name != NULL);

    strncpy(local_mut.project_name, name, XCP_PROJECT_NAME_MAX_LENGTH);
    local_mut.project_name[XCP_PROJECT_NAME_MAX_LENGTH] = 0;
    DBG_PRINTF3("Project Name = '%s'\n", local.project_name);
}

// Get the project name
const char *XcpGetProjectName(void) {
    if (STRNLEN(local.project_name, XCP_PROJECT_NAME_MAX_LENGTH) == 0) {
        assert(0 && "Project name not set, returning empty string");
        return "";
    }
    return local.project_name;
}

/**************************************************************************/
/* EPK version string                                                     */
/**************************************************************************/

// Set the EPK, used by XcpInit()
// Copy the EPK string to a static buffer in the local state and remove unwanted characters (space, tab, colon)
static void XcpSetEpk(const char *epk) {

    assert(epk != NULL);

    strncpy(local_mut.epk, epk, XCP_EPK_MAX_LENGTH);
    local_mut.epk[XCP_EPK_MAX_LENGTH] = 0;
    // Remove unwanted characters from the EPK string
    for (char *p = local_mut.epk; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == ':') {
            *p = '_'; // Replace with underscores
        }
    }
    DBG_PRINTF3("EPK = '%s'\n", local.epk);
}

// Get the EPK from the static buffer in the local state
const char *XcpGetEpk(void) {
    if (STRNLEN(local.epk, XCP_EPK_MAX_LENGTH) == 0) {
        assert(0 && "EPK not set");
        return "";
    }
    return local.epk;
}

// Get the EPK from the static buffer in the local state
// Only in SHM mode there is a difference to XcpGetEpk(), the ECU EPK is for the complete multi application system, while XcpGetEpk() is for the application
const char *XcpGetEcuEpk(void) {
#ifdef OPTION_SHM_MODE // get ecu epk which is a hash for all applications
    return XcpShmGetEcuEpk();
#else // ecu epk is the same as application epk
    return XcpGetEpk();
#endif
}

/****************************************************************************/
/* Calibration memory access                                                */
/****************************************************************************/

/*
XcpWriteMta is not performance critical, but critical for data consistency.
It is used to modify calibration variables.
For size 1, 2, 4, 8 it uses single "atomic" writes assuming valid aligned target memory locations.
Its responsibility is only to copy memory. Any considerations regarding thread safety must be explicitly managed.
This is alsoa requirement to the tool, which must ensure that the data is consistent by choosing the right granularity for DOWNLOAD and SHORT_DOWNLOAD operations.
*/

// Copy of size bytes from data to local.mta_ptr or local.mta_addr depending on the addressing mode
uint8_t XcpWriteMta(uint8_t size, const uint8_t *data) {

#ifdef XCP_ENABLE_SEG_ADDRESSING
    // EXT == XCP_ADDR_EXT_SEG calibration segment memory access
    if (XcpAddrIsSeg(local.mta_ext)) {
        uint8_t res = XcpCalSegWriteMemory(local.mta_addr, size, data);
        local_mut.mta_addr += size;
        return res;
    }
#endif

#ifdef XCP_ENABLE_APP_ADDRESSING
    // EXT == XCP_ADDR_EXT_APP Application specific memory access
    if (XcpAddrIsApp(local.mta_ext)) {
        uint8_t res = ApplXcpWriteMemory(local.mta_addr, size, data);
        local_mut.mta_addr += size;
        return res;
    }
#endif

    // Standard memory access by pointer local.mta_ptr
    if (local.mta_ext == XCP_ADDR_EXT_PTR) {

        if (local.mta_ptr == NULL)
            return CRC_ACCESS_DENIED;

        // TEST
        // Test data consistency: slow bytewise write to increase probability for multithreading data consistency problems
        // while (size-->0) {
        //     *local_mut.mta_ptr++ = *data++;
        //     sleepUs(1);
        // }

        // Fast write with "atomic" copies of basic types, assuming correctly aligned target memory locations
        switch (size) {
        case 1:
            *local_mut.mta_ptr = *data;
            break;
        case 2:
            *(uint16_t *)local_mut.mta_ptr = *(uint16_t *)data;
            break;
        case 4:
            *(uint32_t *)local_mut.mta_ptr = *(uint32_t *)data;
            break;
        case 8:
            *(uint64_t *)local_mut.mta_ptr = *(uint64_t *)data;
            break;
        default:
            memcpy(local_mut.mta_ptr, data, size);
            break;
        }
        local_mut.mta_ptr += size;
        return 0; // Ok
    }

    return CRC_ACCESS_DENIED; // Access violation, illegal address or extension
}

// Copying of size bytes from data to local.mta_ptr or local.mta_addr, depending on the addressing mode
static uint8_t XcpReadMta(uint8_t size, uint8_t *data) {

#ifdef XCP_ENABLE_SEG_ADDRESSING
    // EXT == XCP_ADDR_EXT_SEG calibration segment memory access
    if (XcpAddrIsSeg(local.mta_ext)) {
        uint8_t res = XcpCalSegReadMemory(local.mta_addr, size, data);
        local_mut.mta_addr += size;
        return res;
    }
#endif

#ifdef XCP_ENABLE_APP_ADDRESSING
    // EXT == XCP_ADDR_EXT_APP Application specific memory access
    if (XcpAddrIsApp(local.mta_ext)) {
        uint8_t res = ApplXcpReadMemory(local.mta_addr, size, data);
        local_mut.mta_addr += size;
        return res;
    }
#endif

    // Ext == XCP_ADDR_EXT_PTR - Standard memory access by pointer
    if (local.mta_ext == XCP_ADDR_EXT_PTR) {
        if (local.mta_ptr == NULL)
            return CRC_ACCESS_DENIED;
        memcpy(data, local.mta_ptr, size);
        local_mut.mta_ptr += size;
        return 0; // Ok
    }

#ifdef XCP_ENABLE_IDT_A2L_UPLOAD
    // Ext == XCP_ADDR_EXT_FILE - A2L file upload address space
    if (local.mta_ext == XCP_ADDR_EXT_FILE) {
        if (!ApplXcpReadFile(size, local.mta_addr, data))
            return CRC_ACCESS_DENIED; // Access violation
        local_mut.mta_addr += size;
        return 0; // Ok
    }
#endif

    return CRC_ACCESS_DENIED; // Access violation, illegal address or addressing mode
}

// Set MTA
// Sets the memory transfer address in local.mta_addr/local.mta_ext
// Absolute addressing mode:
//   Converted to pointer addressing mode local.mta_ptr=ApplXcpGetBaseAddr()+XcpAddrDecodeAbsOffset(local.mta_addr) and local.mta_ext=XCP_ADDR_EXT_PTR
//   EPK access is local.mta_ptr=XcpGetEpk() and local.mta_ext=XCP_ADDR_EXT_PTR
//   Absolute access to calibration segments is converted to segment relative addressing mode local.mta_ext=XCP_ADDR_EXT_SEG
// Other addressing mode are left unchanged
// Called by XCP commands SET_MTA, SHORT_DOWNLOAD and SHORT_UPLOAD
uint8_t XcpSetMta(uint8_t ext_, uint32_t addr_) {

    local_mut.mta_ext = ext_;
    local_mut.mta_addr = addr_;
    local_mut.mta_ptr = NULL; // MtaPtr not defined

    // If not EPK calibration segment or addressing mode 0 is absolute
#if !defined(XCP_ENABLE_EPK_CALSEG) || (defined(XCP_ENABLE_ABS_ADDRESSING) && (XCP_ADDR_EXT_ABS == 0))
    // Direct EPK access
    if (local.mta_ext == XCP_ADDR_EXT_EPK && local.mta_addr == XCP_ADDR_EPK) {
        local_mut.mta_ptr = (uint8_t *)XcpGetEpk();
        local_mut.mta_ext = XCP_ADDR_EXT_PTR;
        return CRC_CMD_OK;
    }
#endif

#ifdef XCP_ENABLE_DYN_ADDRESSING
    // Event relative addressing mode
    if (XcpAddrIsDyn(local.mta_ext)) {
        return CRC_CMD_OK;
    }
#endif

#ifdef XCP_ENABLE_REL_ADDRESSING
    // Relative addressing mode
    if (XcpAddrIsRel(local.mta_ext)) {
        return CRC_CMD_OK;
    }
#endif

#ifdef XCP_ENABLE_SEG_ADDRESSING
    // Segment relative addressing mode
    if (XcpAddrIsSeg(local.mta_ext)) {
        if ((local.mta_addr & 0x80000000) == 0) {
            // @@@@ TODO: Workaround CANape bug, address extension != 0 for calibration variables sometimes ignored
            DBG_PRINTF_WARNING("XcpSetMta: Address extension SEG < 0x80000000, converting to ABS addressing mode, addr=0x%08" PRIx32 "\n", local.mta_addr);
            local_mut.mta_ext = XCP_ADDR_EXT_ABS;
        }
        return CRC_CMD_OK;
    }
#endif

#ifdef XCP_ENABLE_APP_ADDRESSING
    // Application specific addressing mode
    if (XcpAddrIsApp(local.mta_ext)) {
        return CRC_CMD_OK;
    }
#endif

#ifdef XCP_ENABLE_ABS_ADDRESSING
    // Absolute addressing mode
    if (XcpAddrIsAbs(local.mta_ext)) {

#ifdef OPTION_SHM_MODE // decode app_id from address extension and check it matches the current process application id
        // In SHM mode, check application id matches the address extension
        uint8_t app_id = XcpAddrExtDecodeAppId(local.mta_ext);
        if (app_id != XcpShmGetAppId()) {
            DBG_PRINTF_ERROR("XcpSetMta: Absolute address extension must have the application id of the current process, ext=%u, app_id=%u, current_app_id=%u\n", local.mta_ext,
                             app_id, XcpShmGetAppId());
            return CRC_ACCESS_DENIED; // Error invalid application id
        }
#endif // SHM_MODE

        local_mut.mta_ptr = (uint8_t *)ApplXcpGetBaseAddr() + XcpAddrDecodeAbsOffset(local.mta_addr);
        local_mut.mta_ext = XCP_ADDR_EXT_PTR;

#if defined(XCP_ENABLE_CALSEG_LIST) && defined(XCP_ENABLE_ABS_ADDRESSING) && (XCP_ADDR_EXT_ABS == 0x00)
        // Check for calibration segment absolute address (XcpSetMta is not performance critical)
        tXcpCalSegIndex calseg_index = XcpFindCalSegByAddr(local.mta_ptr);
        if (calseg_index != XCP_UNDEFINED_CALSEG) {
            const tXcpCalSeg *c = CalSegPtr(calseg_index);
            local_mut.mta_ext = XCP_ADDR_EXT_SEG;
            local_mut.mta_addr = XcpAddrEncodeSegIndex(calseg_index, local.mta_ptr - c->h.default_page_ptr); // Convert to segment relative address
        }
#endif

        return ApplXcpCheckMemory(local.mta_ext, local.mta_addr, 0 /* size not known here */);
    }
#endif

    return CRC_OUT_OF_RANGE; // Unsupported addressing mode
}

/**************************************************************************/
/* Checksum calculation                                                   */
/**************************************************************************/

/* Table for CCITT checksum calculation */
#ifdef XCP_ENABLE_CHECKSUM

#if (XCP_CHECKSUM_TYPE == XCP_CHECKSUM_TYPE_CRC16CCITT)
static const uint16_t gXcpCRC16CCITTtab[256] = {
    0x0000, 0x1021, 0x2042, 0x3063,  0x4084, 0x50a5, 0x60c6, 0x70e7u, 0x8108, 0x9129, 0xa14a, 0xb16b,  0xc18c, 0xd1ad, 0xe1ce, 0xf1efu, 0x1231, 0x0210, 0x3273, 0x2252,
    0x52b5, 0x4294, 0x72f7, 0x62d6u, 0x9339, 0x8318, 0xb37b, 0xa35a,  0xd3bd, 0xc39c, 0xf3ff, 0xe3deu, 0x2462, 0x3443, 0x0420, 0x1401,  0x64e6, 0x74c7, 0x44a4, 0x5485u,
    0xa56a, 0xb54b, 0x8528, 0x9509,  0xe5ee, 0xf5cf, 0xc5ac, 0xd58du, 0x3653, 0x2672, 0x1611, 0x0630,  0x76d7, 0x66f6, 0x5695, 0x46b4u, 0xb75b, 0xa77a, 0x9719, 0x8738,
    0xf7df, 0xe7fe, 0xd79d, 0xc7bcu, 0x48c4, 0x58e5, 0x6886, 0x78a7,  0x0840, 0x1861, 0x2802, 0x3823u, 0xc9cc, 0xd9ed, 0xe98e, 0xf9af,  0x8948, 0x9969, 0xa90a, 0xb92bu,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96,  0x1a71, 0x0a50, 0x3a33, 0x2a12u, 0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e,  0x9b79, 0x8b58, 0xbb3b, 0xab1au, 0x6ca6, 0x7c87, 0x4ce4, 0x5cc5,
    0x2c22, 0x3c03, 0x0c60, 0x1c41u, 0xedae, 0xfd8f, 0xcdec, 0xddcd,  0xad2a, 0xbd0b, 0x8d68, 0x9d49u, 0x7e97, 0x6eb6, 0x5ed5, 0x4ef4,  0x3e13, 0x2e32, 0x1e51, 0x0e70u,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc,  0xbf1b, 0xaf3a, 0x9f59, 0x8f78u, 0x9188, 0x81a9, 0xb1ca, 0xa1eb,  0xd10c, 0xc12d, 0xf14e, 0xe16fu, 0x1080, 0x00a1, 0x30c2, 0x20e3,
    0x5004, 0x4025, 0x7046, 0x6067u, 0x83b9, 0x9398, 0xa3fb, 0xb3da,  0xc33d, 0xd31c, 0xe37f, 0xf35eu, 0x02b1, 0x1290, 0x22f3, 0x32d2,  0x4235, 0x5214, 0x6277, 0x7256u,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589,  0xf56e, 0xe54f, 0xd52c, 0xc50du, 0x34e2, 0x24c3, 0x14a0, 0x0481,  0x7466, 0x6447, 0x5424, 0x4405u, 0xa7db, 0xb7fa, 0x8799, 0x97b8,
    0xe75f, 0xf77e, 0xc71d, 0xd73cu, 0x26d3, 0x36f2, 0x0691, 0x16b0,  0x6657, 0x7676, 0x4615, 0x5634u, 0xd94c, 0xc96d, 0xf90e, 0xe92f,  0x99c8, 0x89e9, 0xb98a, 0xa9abu,
    0x5844, 0x4865, 0x7806, 0x6827,  0x18c0, 0x08e1, 0x3882, 0x28a3u, 0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e,  0x8bf9, 0x9bd8, 0xabbb, 0xbb9au, 0x4a75, 0x5a54, 0x6a37, 0x7a16,
    0x0af1, 0x1ad0, 0x2ab3, 0x3a92u, 0xfd2e, 0xed0f, 0xdd6c, 0xcd4d,  0xbdaa, 0xad8b, 0x9de8, 0x8dc9u, 0x7c26, 0x6c07, 0x5c64, 0x4c45,  0x3ca2, 0x2c83, 0x1ce0, 0x0cc1u,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c,  0xaf9b, 0xbfba, 0x8fd9, 0x9ff8u, 0x6e17, 0x7e36, 0x4e55, 0x5e74,  0x2e93, 0x3eb2, 0x0ed1, 0x1ef0u};

#endif

static uint8_t calcChecksum(uint32_t checksum_size, uint32_t *checksum_result) {
    assert(checksum_size > 0);
    assert(checksum_result != NULL);

#if (XCP_CHECKSUM_TYPE == XCP_CHECKSUM_TYPE_CRC16CCITT)

    // CRC16 CCITT
    uint16_t sum = 0xFFFF;
    uint8_t value = 0;
    for (uint32_t n = checksum_size; n > 0; n--) {
        uint8_t res = XcpReadMta(1, &value);
        if (res != CRC_CMD_OK)
            return res;
        sum = gXcpCRC16CCITTtab[((uint8_t)(sum >> 8)) ^ value] ^ (uint16_t)(sum << 8);
    }
    *checksum_result = (uint32_t)sum;

#else

    // ADD44
    uint32_t sum = 0;
    uint32_t value = 0;
    for (uint32_t n = checksum_size; n >= sizeof(value); n -= sizeof(value)) {
        uint8_t res = XcpReadMta(sizeof(value), (uint8_t *)&value);
        if (res != CRC_CMD_OK)
            return res;
        sum += value;
    }
    *checksum_result = (uint32_t)sum;

#endif

    return CRC_CMD_OK;
}

#endif // XCP_ENABLE_CHECKSUM

/**************************************************************************/
/* Eventlist                                                              */
/**************************************************************************/

#ifdef XCP_ENABLE_DAQ_EVENT_LIST

// Using the less expensive getEventCount() is a deliberate choice for performance critical code paths like XcpEvent(),
// where the visibility of new events created by other threads is not critical, while acquireEventCount() is used for thread safe access to the event count with guaranteed
// visibility of new events created by other threads.
#define getEventCount() (uint16_t)atomic_load_explicit(&shared.event_list.count, memory_order_relaxed)
#define acquireEventCount() (uint16_t)atomic_load_explicit(&shared.event_list.count, memory_order_acquire)
#define releaseEventCount(n) atomic_store_explicit(&shared_mut_safe.event_list.count, n, memory_order_release)

// Initialize the XCP event list
void XcpInitEventList(void) {

    // Reset event list count
    releaseEventCount(0);
    mutexInit(&local_mut.event_list_mutex, false, 1000);
}

// Get a pointer to and the size of the XCP event list
const tXcpEventList *XcpGetEventList(void) {
    if (!isActivated())
        return NULL;
    return &shared.event_list;
}

// Get a pointer to the XCP event struct
const tXcpEvent *XcpGetEvent(tXcpEventId event) {
    if (!isActivated() || event >= acquireEventCount())
        return NULL;
    return &shared.event_list.event[event];
}

// Get the current event count, thread safe but visibility of new events created by other threads is not guaranteed
uint16_t XcpGetEventCount(void) { return getEventCount(); }

// Get the events application id
#ifdef OPTION_SHM_MODE // get event application id
uint8_t XcpGetEventAppId(tXcpEventId event) {
    if (!isActivated() || event >= getEventCount())
        return 0;
    return shared.event_list.event[event].app_id;
}
#endif // SHM_MODE

// Get the full event name, including instance index postfix if applicable
// Result has lifetime until next call of XcpGetEventName()
const char *XcpGetEventName(tXcpEventId event) {
    if (!isActivated() || event >= getEventCount())
        return NULL;
    const tXcpEvent *e = &shared.event_list.event[event];
    if (e->index > 0) {
        // Event instance, append instance index to the name
        static char nameBuf[XCP_MAX_EVENT_NAME + 8];
        SNPRINTF(nameBuf, sizeof(nameBuf), "%s_%u", e->name, e->index);
        return nameBuf;
    }
    return (const char *)&shared.event_list.event[event].name;
}

// Get the event index (1..), return 0 if not found
uint16_t XcpGetEventIndex(tXcpEventId event) {
    if (!isActivated() || event >= getEventCount())
        return 0;
    return shared.event_list.event[event].index;
}

// @@@@ TODO: Not process-safe
static tXcpEventId XcpFindEventInstances(const char *name, uint16_t *pcount) {
    uint16_t id = XCP_UNDEFINED_EVENT_ID;
    if (pcount != NULL)
        *pcount = 0;
    if (isActivated()) {
        uint16_t count = acquireEventCount();
        for (uint16_t i = 0; i < count; i++) {
#ifdef OPTION_SHM_MODE // find only events owned by this application process
            if (shared.event_list.event[i].app_id == XcpShmGetAppId() && strcmp(shared.event_list.event[i].name, name) == 0) {
#else
            if (strcmp(shared.event_list.event[i].name, name) == 0) {
#endif
                if (pcount != NULL)
                    *pcount += 1;
                if (id == XCP_UNDEFINED_EVENT_ID)
                    id = i; // Remember the first found event
            }
        }
    }
    return id;
}

// Find an event by name, return XCP_UNDEFINED_EVENT_ID if not found
// Thread safe
tXcpEventId XcpFindEvent(const char *name) { return XcpFindEventInstances(name, NULL); }

// Create an XCP event
// Not thread safe
// Returns the new XCP event id or XCP_UNDEFINED_EVENT_ID when out of memory
tXcpEventId XcpCreateIndexedEvent(const char *name, uint16_t index, uint32_t cycle_time_ns, uint8_t priority) {

    if (!isActivated()) {
        return XCP_UNDEFINED_EVENT_ID; // Uninitialized
    }

    assert(name != NULL);

    // Check name length
    size_t nameLen = STRNLEN(name, XCP_MAX_EVENT_NAME + 1);
    if (nameLen > XCP_MAX_EVENT_NAME) {
        DBG_PRINTF_ERROR("event name '%.*s' too long (%zu > %d chars)\n", XCP_MAX_EVENT_NAME, name, nameLen, XCP_MAX_EVENT_NAME);
        return XCP_UNDEFINED_EVENT_ID;
    }

    // Check event count
    uint16_t e = acquireEventCount();
    if (e >= XCP_MAX_EVENT_COUNT) {
        DBG_PRINT_ERROR("too many events\n");
        return XCP_UNDEFINED_EVENT_ID; // Out of memory
    }

    // Caller is responsible for thread safety
    shared_mut_safe.event_list.event[e].index = index; // Index of the event instance
    strncpy(shared_mut_safe.event_list.event[e].name, name, XCP_MAX_EVENT_NAME);
    shared_mut_safe.event_list.event[e].name[XCP_MAX_EVENT_NAME] = 0;
    shared_mut_safe.event_list.event[e].flags = (priority > 0) ? XCP_DAQ_EVENT_FLAG_PRIORITY : 0;
#ifdef XCP_ENABLE_DAQ_PRESCALER
#ifndef XCP_ENABLE_DAQ_EVENT_LIST
#error "XCP_ENABLE_DAQ_PRESCALER requires XCP_ENABLE_DAQ_EVENT_LIST"
#endif
    shared_mut_safe.event_list.event[e].daq_prescaler = 0;
    shared_mut_safe.event_list.event[e].daq_prescaler_cnt = 0;
#endif
    shared_mut_safe.event_list.event[e].daq_first = XCP_UNDEFINED_DAQ_LIST;
    shared_mut_safe.event_list.event[e].cycle_time_ns = cycle_time_ns;
    // In SHM mode, assign event to the application, different apps have different namespace
#ifdef OPTION_SHM_MODE // store event application id in event
    shared_mut_safe.event_list.event[e].app_id = XcpShmGetAppId();
#endif // SHM_MODE

    releaseEventCount(e + 1); // Publish new event, visibility assured, when others acquire the event count, but overall function not thread safe, must be called with locked mutex

    DBG_PRINTF3("Create Event %u: %s index=%u, cycle=%uns, prio=%u\n", e, shared.event_list.event[e].name, index, cycle_time_ns, priority);
    return e;
}

// Add a measurement event to event list, return event id (0..MAX_EVENT-1),
// If name exists, an event instance index is generated (and only in A2L appended to the event name)
// Thread safe by mutex
// @@@@ TODO: Find a process safe solution for SHM mode
tXcpEventId XcpCreateEventInstance(const char *name, uint32_t cycle_time_ns, uint8_t priority) {

    if (!isActivated()) {
        return XCP_UNDEFINED_EVENT_ID; // Uninitialized
    }

    uint16_t count = 0;
    mutexLock(&local_mut.event_list_mutex);
    XcpFindEventInstances(name, &count);
    // @@@@ TODO: Use preloaded event instances instead of creating a new instance
    // Event instances have no identity, could use any unused preload event instance with this name
    tXcpEventId id = XcpCreateIndexedEvent(name, count + 1, cycle_time_ns, priority);
    mutexUnlock(&local_mut.event_list_mutex);
    return id;
}

// Add a measurement event to the event list, return event id (0..MAX_EVENT-1)
// If name already exists, just return the existing id
// Thread safe by mutex
// @@@@ TODO: Find a process safe solution for SHM mode
tXcpEventId XcpCreateEvent(const char *name, uint32_t cycle_time_ns, uint8_t priority) {

    if (!isActivated()) {
        return XCP_UNDEFINED_EVENT_ID; // Uninitialized
    }

    uint16_t count = 0;
    mutexLock(&local_mut.event_list_mutex);
    tXcpEventId id = XcpFindEventInstances(name, &count);
    if (id != XCP_UNDEFINED_EVENT_ID) {
        mutexUnlock(&local_mut.event_list_mutex);
        DBG_PRINTF5("Event '%s' already defined, id=%u\n", name, id);
        assert(count == 1); // Creating additional event instances is not supported, use XcpCreateEventInstance
        return id;          // Event already exists, return the existing event id, event could be preloaded from binary freeze file for A2L stability
    }
    id = XcpCreateIndexedEvent(name, 0, cycle_time_ns, priority);
    mutexUnlock(&local_mut.event_list_mutex);
    return id;
}

#else  // XCP_ENABLE_DAQ_EVENT_LIST
const char *XcpGetEventName(tXcpEventId event) {
    (void)event;
    return "";
}
#endif // XCP_ENABLE_DAQ_EVENT_LIST

/****************************************************************************/
/* Data Acquisition Setup                                                    */
/****************************************************************************/

// ODT header size is hardcoded to 4 bytes
#define ODT_HEADER_SIZE 4 // ODT,align,DAQ_WORD header

// Timestamp size is hardcoded to 4 bytes
#define ODT_TIMESTAMP_SIZE 4

// Free all dynamic DAQ lists
static void XcpClearDaq(void) {

    shared_mut.session_status &= (uint16_t)(~SS_DAQ);
    atomic_store_explicit(&shared_mut_safe.daq_running, false, memory_order_release);

    memset((uint8_t *)&shared.daq_lists, 0, sizeof(tXcpDaqLists));
    shared_mut.daq_lists.res = 0xBEAC;

#ifdef XCP_MAX_EVENT_COUNT
#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    uint16_t eventCount = getEventCount();
    for (uint16_t event = 0; event < eventCount; event++) {
        shared_mut.event_list.event[event].daq_first = XCP_UNDEFINED_DAQ_LIST;
    }
#else
    for (uint16_t event = 0; event < XCP_MAX_EVENT_COUNT; event++) {
        shared_mut.daq_lists.daq_first[event] = XCP_UNDEFINED_DAQ_LIST;
    }
#endif
#endif
}

// Check if there is sufficient memory for the values of DaqCount, OdtCount and OdtEntryCount
// Return CRC_MEMORY_OVERFLOW if not
static uint8_t XcpCheckMemory(void) {

    uint32_t s;

#ifdef XCP_ENABLE_DAQ_ADDREXT
#define ODT_ENTRY_SIZE 6
#else
#define ODT_ENTRY_SIZE 5
#endif

    /* Check memory overflow */
    s = (shared.daq_lists.daq_count * (uint32_t)sizeof(tXcpDaqList)) + (shared.daq_lists.odt_count * (uint32_t)sizeof(tXcpOdt)) +
        (shared.daq_lists.odt_entry_count * ODT_ENTRY_SIZE);
    if (s >= XCP_DAQ_MEM_SIZE) {
        DBG_PRINTF_ERROR("DAQ memory overflow, %u of %u Bytes required\n", s, XCP_DAQ_MEM_SIZE);
        return CRC_MEMORY_OVERFLOW;
    }

    static_assert(sizeof(tXcpDaqList) == 12, "Invalid tXcpDaqList size"); // Check size
    static_assert(sizeof(tXcpOdt) == 8, "Invalid tXcpOdt size");          // Check size
    assert(((uint64_t)&shared.daq_lists % 4) == 0);                       // Check alignment
    assert(((uint64_t)&DaqListOdtTable[0] % 4) == 0);                     // Check alignment
    assert(((uint64_t)&DaqListOdtEntryAddrTable[0] % 4) == 0);            // Check alignment
    assert(((uint64_t)&DaqListOdtEntrySizeTable[0] % 4) == 0);            // Check alignment

    DBG_PRINTF6("[XcpCheckMemory] %u of %u Bytes used\n", s, XCP_DAQ_MEM_SIZE);
    return 0;
}

// Allocate daqCount DAQ lists
static uint8_t XcpAllocDaq(uint16_t daqCount) {

    uint16_t daq;
    uint8_t r;

    if (shared.daq_lists.odt_count != 0 || shared.daq_lists.odt_entry_count != 0)
        return CRC_SEQUENCE;
    if (daqCount == 0)
        return CRC_OUT_OF_RANGE;

    // Initialize
    if (0 != (r = XcpCheckMemory()))
        return r; // Memory overflow
    for (daq = 0; daq < daqCount; daq++) {
        DaqListEventChannelMut(daq) = XCP_UNDEFINED_EVENT_ID;
        DaqListAddrExtMut(daq) = XCP_UNDEFINED_ADDR_EXT;
#ifdef XCP_MAX_EVENT_COUNT
        DaqListNextMut(daq) = XCP_UNDEFINED_DAQ_LIST;
#endif
    }
    shared_mut.daq_lists.daq_count = daqCount;
    return 0;
}

// Allocate odtCount ODTs in a DAQ list
static uint8_t XcpAllocOdt(uint16_t daq, uint8_t odtCount) {

    uint32_t n;

    if (odtCount == 0)
        return CRC_DAQ_CONFIG;
    if (shared.daq_lists.daq_count == 0 || shared.daq_lists.odt_entry_count != 0)
        return CRC_SEQUENCE;
    if (daq >= shared.daq_lists.daq_count)
        return CRC_DAQ_CONFIG;

#ifdef XCP_ENABLE_OVERRUN_INDICATION_PID
    if (odtCount == 0 || odtCount >= 0x7C)
        return CRC_OUT_OF_RANGE; // MSB of ODT number is reserved for overflow indication, 0xFC-0xFF for response, error, event and service
#else
    if (odtCount == 0 || odtCount >= 0xFC)
        return CRC_OUT_OF_RANGE; // 0xFC-0xFF for response, error, event and service
#endif
    n = (uint32_t)shared.daq_lists.odt_count + (uint32_t)odtCount;
    if (n > 0xFFFF)
        return CRC_OUT_OF_RANGE; // Overall number of ODTs limited to 64K
    shared_mut.daq_lists.u.daq_list[daq].first_odt = shared.daq_lists.odt_count;
    shared_mut.daq_lists.odt_count = (uint16_t)n;
    shared_mut.daq_lists.u.daq_list[daq].last_odt = (uint16_t)(shared.daq_lists.odt_count - 1);
    return XcpCheckMemory();
}

// Increase current ODT size (absolute ODT index) size by n
static bool XcpAdjustOdtSize(uint16_t daq, uint16_t odt, uint8_t n) {

    uint16_t size = (uint16_t)(DaqListOdtTable[odt].size + n);
    DaqListOdtTableMut[odt].size = size;

#ifdef XCP_ENABLE_TEST_CHECKS
    assert(odt >= DaqListFirstOdt(daq));
    uint16_t daq_odt = odt - DaqListFirstOdt(daq);
    uint16_t max_size = (XCPTL_MAX_DTO_SIZE - ODT_HEADER_SIZE) - (daq_odt == 0 ? 4 : 0); // Leave space for ODT header and timestamp in first ODT
    if (size > max_size) {
        DBG_PRINTF_ERROR("DAQ %u, ODT %u overflow, %u bytes requested, max ODT size = %u, DTO size = %u!\n", daq, daq_odt, size, max_size, XCPTL_MAX_DTO_SIZE);
        return false;
    }
#else
    (void)daq;
#endif
    return true;
}

// Allocate all ODT entries, Parameter odt is relative odt number
static uint8_t XcpAllocOdtEntry(uint16_t daq, uint8_t odt, uint8_t odtEntryCount) {

    int xcpFirstOdt;
    uint32_t n;

    if (odtEntryCount == 0)
        return CRC_DAQ_CONFIG;
    if (shared.daq_lists.daq_count == 0 || shared.daq_lists.odt_count == 0)
        return CRC_SEQUENCE;
    if (daq >= shared.daq_lists.daq_count || odtEntryCount == 0 || odt >= DaqListOdtCount(daq))
        return CRC_OUT_OF_RANGE;

    /* Absolute ODT entry count is limited to 64K */
    n = (uint32_t)shared.daq_lists.odt_entry_count + (uint32_t)odtEntryCount;
    if (n > 0xFFFF)
        return CRC_MEMORY_OVERFLOW;

    xcpFirstOdt = shared.daq_lists.u.daq_list[daq].first_odt;
    DaqListOdtTableMut[xcpFirstOdt + odt].first_odt_entry = shared.daq_lists.odt_entry_count;
    shared_mut.daq_lists.odt_entry_count = (uint16_t)n;
    DaqListOdtTableMut[xcpFirstOdt + odt].last_odt_entry = (uint16_t)(shared.daq_lists.odt_entry_count - 1);
    DaqListOdtTableMut[xcpFirstOdt + odt].size = 0;
    return XcpCheckMemory();
}

// Set ODT entry pointer
static uint8_t XcpSetDaqPtr(uint16_t daq, uint8_t odt, uint8_t idx) {

    if (daq >= shared.daq_lists.daq_count || odt >= DaqListOdtCount(daq))
        return CRC_OUT_OF_RANGE;
    if (XcpIsDaqRunning())
        return CRC_DAQ_ACTIVE;
    uint16_t odt0 = (uint16_t)(DaqListFirstOdt(daq) + odt); // Absolute odt index
    if (idx >= DaqListOdtEntryCount(odt0))
        return CRC_OUT_OF_RANGE;
    // Save info for XcpAddOdtEntry from WRITE_DAQ and WRITE_DAQ_MULTIPLE
    local_mut.write_daq_odt_entry = (uint16_t)(DaqListOdtTable[odt0].first_odt_entry + idx); // Absolute odt entry index
    local_mut.write_daq_odt = odt0;                                                          // Absolute ODT index
    local_mut.write_daq_daq = daq;
    return 0;
}

// Add an ODT entry to current DAQ/ODT
// Supports XCP_ADDR_EXT_/ABS/DYN
// All ODT entries of a DAQ list must have the same address extension,returns CRC_DAQ_CONFIG if not
// In XCP_ADDR_EXT_/DYN/REL addressing mode, the event must be unique
static uint8_t XcpAddOdtEntry(uint32_t addr, uint8_t ext, uint8_t size) {

    if (size == 0 || size > XCP_MAX_ODT_ENTRY_SIZE)
        return CRC_OUT_OF_RANGE;
    if (0 == shared.daq_lists.daq_count || 0 == shared.daq_lists.odt_count || 0 == shared.daq_lists.odt_entry_count)
        return CRC_DAQ_CONFIG;
    if (local.write_daq_odt_entry - DaqListOdtTable[local.write_daq_odt].first_odt_entry >= DaqListOdtEntryCount(local.write_daq_odt))
        return CRC_OUT_OF_RANGE;
    if (XcpIsDaqRunning())
        return CRC_DAQ_ACTIVE;

    DBG_PRINTF5("Add ODT entry, DAQ=%u, ODT=%u, idx=%u, addr=0x%X, ext=%u, size=%u\n", local.write_daq_daq, local.write_daq_odt,
                local.write_daq_odt_entry - DaqListOdtTable[local.write_daq_odt].first_odt_entry, addr, ext, size);

    // DAQ list must have unique address extension
#ifndef XCP_ENABLE_DAQ_ADDREXT
    uint8_t daq_ext = DaqListAddrExt(local.write_daq_daq);
    if (daq_ext != XCP_UNDEFINED_ADDR_EXT && ext != daq_ext) {
        DBG_PRINTF_ERROR("DAQ list must have unique address extension, DAQ=%u, ODT=%u, ext=%u, daq_ext=%u\n", local.write_daq_daq, local.write_daq_odt, ext, daq_ext);
        return CRC_DAQ_CONFIG; // Error not unique address extension
    }
    DaqListAddrExt(local.write_daq_daq) = ext;
#endif

    uint32_t base_offset = 0;
#ifdef XCP_ENABLE_DYN_ADDRESSING
    // DYN addressing mode, base pointer will given to XcpEventExt, event is encoded in the address
    if (XcpAddrIsDyn(ext)) {
        uint16_t event = XcpAddrDecodeDynEvent(addr);
        base_offset = XcpAddrDecodeDynOffset(addr);
        uint16_t e0 = DaqListEventChannel(local.write_daq_daq);
        if (e0 != XCP_UNDEFINED_EVENT_ID && e0 != event) {
            DBG_PRINTF_ERROR("DAQ list must have unique event channel, DAQ=%u, ODT=%u, event=%u, DaqListEventChannel=%u\n", local.write_daq_daq, local.write_daq_odt, event, e0);
            return CRC_OUT_OF_RANGE; // Error event channel redefinition
        }
        DaqListEventChannelMut(local_mut.write_daq_daq) = event;
    } else
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
        // REL addressing mode, base pointer will given to XcpEventExt
        // Max address range base - base+0xFFFFFFFF
        if (XcpAddrIsRel(ext)) { // relative addressing mode
            base_offset = XcpAddrDecodeRelOffset(addr);
        } else
#endif
#ifdef XCP_ENABLE_ABS_ADDRESSING
            // ABS addressing mode, base pointer is ApplXcpGetAbsAddrBase
            if (XcpAddrIsAbs(ext)) {
                base_offset = XcpAddrDecodeAbsOffset(addr);
#ifdef OPTION_SHM_MODE // add ODT entry with absolute addressing mode
// Remove the application id from ext
// In SHM mode, absolute base address is always at index 1 in the bases array in each application
#ifndef XCP_ADDRESS_MODE_XCPLITE__CXSDD
#error "SHM mode requires XCP_ADDRESS_MODE_XCPLITE__CXSDD"
#endif
                ext = 1;
#endif // SHM_MODE
            } else
#endif
                return CRC_ACCESS_DENIED;

    DaqListOdtEntrySizeTableMut[local.write_daq_odt_entry] = size;
    DaqListOdtEntryAddrTableMut[local.write_daq_odt_entry] = base_offset; // Signed 32 bit offset relative to base pointer given to XcpEvent
#ifdef XCP_ENABLE_DAQ_ADDREXT
    DaqListOdtEntryAddrExtTableMut[local.write_daq_odt_entry] = ext;
#endif
    if (!XcpAdjustOdtSize(local.write_daq_daq, local.write_daq_odt, size))
        return CRC_DAQ_CONFIG;
    local_mut.write_daq_odt_entry++; // Autoincrement to next ODT entry, no autoincrementing over ODTs
    return 0;
}

// Set DAQ list mode
// All DAQ lists associated with an event, must have the same address extension
static uint8_t XcpSetDaqListMode(uint16_t daq, uint16_t event_id, uint8_t mode, uint8_t prescaler, uint8_t prio) {

    if (daq >= shared.daq_lists.daq_count)
        return CRC_DAQ_CONFIG;

#ifndef XCP_ENABLE_DAQ_PRESCALER
    if (prescaler > 1)
        return CRC_OUT_OF_RANGE; // prescaler is not supported
#else
    (void)prescaler;
#endif

#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    // Check if this event exists
    const tXcpEvent *event = XcpGetEvent(event_id); // Check if event exists
    if (event == NULL)
        return CRC_OUT_OF_RANGE;

// Set the prescaler to the associated event, individual prescaler per DAQ list are not supported
#ifdef XCP_ENABLE_DAQ_PRESCALER
    event->daq_prescaler = prescaler; // Conflicts are not checked
    event->daq_prescaler_cnt = 0;
#endif

#endif

#ifdef XCP_ENABLE_DYN_ADDRESSING

    // Check if the DAQ list requires a specific event and it matches
    uint16_t event_id0 = DaqListEventChannel(daq);
    if (event_id0 != XCP_UNDEFINED_EVENT_ID && event_id != event_id0) {
        DBG_PRINTF_ERROR("SET_DAQ_LIST_MODE (daq=%u,event=%u) event channel redefinition, DaqListEventChannel=%u\n", daq, event_id, event_id0);
        return CRC_DAQ_CONFIG; // Error event not unique
    }

    // Check all DAQ lists with same event have the same address extension
    // @@@@ TODO: This restriction is not compliant to the XCP standard, must be ensured in the tool or the API
#ifndef XCP_ENABLE_DAQ_ADDREXT
    uint8_t ext = DaqListAddrExt(daq);
    for (uint16_t daq0 = 0; daq0 < shared.daq_lists.daq_count; daq0++) {
        if (DaqListEventChannel(daq0) == event) {
            uint8_t ext0 = DaqListAddrExt(daq0);
            if (ext != ext0)
                return CRC_DAQ_CONFIG; // Error address extension not unique
        }
    }
#endif
#endif

    DaqListEventChannelMut(daq) = event_id;
    DaqListModeMut(daq) = mode;
    DaqListPriorityMut(daq) = prio;

    // Append daq to linked list of daq lists already associated to this event
#ifdef XCP_MAX_EVENT_COUNT
    uint16_t daq0 = DaqListFirst(event_id);
    uint16_t *daq0_next = &DaqListFirstMut(event_id);
    while (daq0 != XCP_UNDEFINED_DAQ_LIST) {
        assert(daq0 < shared.daq_lists.daq_count);
        daq0 = DaqListNext(daq0);
        daq0_next = &DaqListNextMut(daq0);
    }
    *daq0_next = daq;
#endif

    return 0;
}

// Check if DAQ lists are fully and consistently initialized
bool XcpCheckPreparedDaqLists(void) {

    for (uint16_t daq = 0; daq < shared.daq_lists.daq_count; daq++) {
        if (DaqListState(daq) & DAQ_STATE_SELECTED) {
            if (DaqListEventChannel(daq) == XCP_UNDEFINED_EVENT_ID) {
                DBG_PRINTF_ERROR("DAQ list %u event channel not initialized!\n", daq);
                return false;
            }
#ifndef XCP_ENABLE_DAQ_ADDREXT
            if (DaqListAddrExt(daq) == XCP_UNDEFINED_ADDR_EXT) {
                DBG_PRINTF_ERROR("DAQ list %u address extension not set!\n", daq);
                return false;
            }
#endif
            for (uint16_t i = DaqListFirstOdt(daq); i <= DaqListLastOdt(daq); i++) {
                for (uint16_t e = DaqListOdtTable[i].first_odt_entry; e <= DaqListOdtTable[i].last_odt_entry; e++) {

                    uint32_t addr = DaqListOdtEntryAddrTable[e];
                    uint8_t ext = DaqListOdtEntryAddrExtTable[e];
                    uint8_t size = DaqListOdtEntrySizeTable[e];
                    DBG_PRINTF6("Check DAQ %u, ODT %u, ODT entry %u: addr=0x%X, ext=%u, size=%u\n", daq, i - DaqListFirstOdt(daq), e - DaqListOdtTable[i].first_odt_entry, addr,
                                ext, size);
                    uint8_t res = ApplXcpCheckMemory(ext, addr, size);
                    if (res != CRC_CMD_OK) {
                        DBG_PRINTF_ERROR("DAQ %u, ODT %u, ODT entry %u invalid, callback read failed with error %u!\n", daq, i - DaqListFirstOdt(daq),
                                         e - DaqListOdtTable[i].first_odt_entry, res);
                        return false;
                    }

                    if (DaqListOdtEntrySizeTable[e] == 0) {
                        DBG_PRINTF_ERROR("DAQ %u, ODT %u, ODT entry %u size not set!\n", daq, i - DaqListFirstOdt(daq), e - DaqListOdtTable[i].first_odt_entry);
                        return false;
                    }
                }

            } /* j */
        }
    }

    return true;
}

// Start DAQ
static void XcpStartDaq(void) {

    // If not already running
    if (isDaqRunning())
        return;

    local_mut.daq_start_clock = ApplXcpGetClock64();
    shared_mut.daq_overflow_count = 0;

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 4) {
        char ts[64];
        clockGetString(ts, sizeof(ts), local.daq_start_clock);
        DBG_PRINTF3("DAQ processing start at t=%s\n", ts);
    }
#endif

    // XcpStartDaq might be called multiple times, if DAQ lists are started individually
    // CANape never does this
    ApplXcpStartDaq();

    shared_mut.session_status |= SS_DAQ; // Start processing DAQ events
    atomic_store_explicit(&shared_mut_safe.daq_running, true, memory_order_release);
}

// Stop DAQ
static void XcpStopDaq(void) {

    shared_mut.session_status &= (uint16_t)(~SS_DAQ); // Stop processing DAQ events
    atomic_store_explicit(&shared_mut_safe.daq_running, false, memory_order_release);

    // Reset all DAQ list states
    for (uint16_t daq = 0; daq < shared.daq_lists.daq_count; daq++) {
        DaqListStateMut(daq) = DAQ_STATE_STOPPED_UNSELECTED;
    }

    ApplXcpStopDaq();

    DBG_PRINT3("DAQ processing stop\n");
}

// Start DAQ list
// Do not start DAQ event processing yet
static void XcpStartDaqList(uint16_t daq) {

    DaqListStateMut(daq) |= DAQ_STATE_RUNNING;

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 4) {
        XcpPrintDaqList(daq);
    }
#endif
}

// Start all selected DAQ lists
// Do not start DAQ event processing yet
static void XcpStartSelectedDaqLists(void) {

    // Start all selected DAQ lists, reset the selected state
    for (uint16_t daq = 0; daq < shared.daq_lists.daq_count; daq++) {
        if ((DaqListState(daq) & DAQ_STATE_SELECTED) != 0) {
            DaqListStateMut(daq) &= (uint8_t)(~DAQ_STATE_SELECTED);
            XcpStartDaqList(daq);
        }
    }
}

// Stop individual DAQ list
// If all DAQ lists are stopped, stop event processing
static void XcpStopDaqList(uint16_t daq) {

    DaqListStateMut(daq) &= (uint8_t)(~(DAQ_STATE_OVERRUN | DAQ_STATE_RUNNING));

    /* Check if all DAQ lists are stopped */
    for (uint16_t d = 0; d < shared.daq_lists.daq_count; d++) {
        if ((DaqListState(d) & DAQ_STATE_RUNNING) != 0) {
            return; // Not all DAQ lists stopped yet
        }
    }

    // All DAQ lists are stopped, stop DAQ event processing
    XcpStopDaq();
}

// Stop all selected DAQ lists
// If all DAQ lists are stopped, stop event processing
static void XcpStopSelectedDaqLists(void) {

    // Stop all selected DAQ lists, reset the selected state
    for (uint16_t daq = 0; daq < shared.daq_lists.daq_count; daq++) {
        if ((DaqListState(daq) & DAQ_STATE_SELECTED) != 0) {
            XcpStopDaqList(daq);
            DaqListStateMut(daq) = DAQ_STATE_STOPPED_UNSELECTED;
        }
    }
}

/****************************************************************************/
/* Data Acquisition Event Processor                                         */
/****************************************************************************/

// Trigger DAQ list
#ifdef XCP_ENABLE_DAQ_ADDREXT
static void XcpTriggerDaqList_(tQueueHandle queue_handle, uint16_t daq, int count, const uint8_t **bases, uint64_t clock) {
#else
static void XcpTriggerDaqList_(tQueueHandle queue_handle, uint16_t daq, const uint8_t *base, uint64_t clock) {
#endif
    uint8_t *d0;
    uint16_t odt, hs;

    // Outer loop
    // Loop over all ODTs of the current DAQ list
    for (hs = ODT_HEADER_SIZE + ODT_TIMESTAMP_SIZE, odt = DaqListFirstOdt(daq); odt <= DaqListLastOdt(daq); hs = ODT_HEADER_SIZE, odt++) {

        // Get DTO buffer
        tQueueBuffer queue_buffer = queueAcquire(queue_handle, DaqListOdtTable[odt].size + hs);
        d0 = queue_buffer.buffer;

        // DAQ queue overflow
        if (d0 == NULL) {
#ifdef XCP_ENABLE_OVERRUN_INDICATION_PID
            shared_mut.daq_overflow_count++;
            DaqListState(daq) |= DAQ_STATE_OVERRUN;
            DBG_PRINTF4("DAQ queue overrun, daq=%u, odt=%u, overruns=%u\n", daq, odt, shared.daq_overflow_count);
#else
            // Queue overflow has to be handled and indicated by the transmit queue
            DBG_PRINTF6("DAQ queue overflow, daq=%u, odt=%u\n", daq, odt);
#endif
            return; // Skip rest of this event on queue overrun, to simplify resynchronisation of the client
        }

        // ODT header (ODT8,FIL8,DAQ16 or ODT8,DAQ8)
        d0[0] = (uint8_t)(odt - DaqListFirstOdt(daq)); /* Relative odt number as byte*/
#if ODT_HEADER_SIZE == 4
        d0[1] = 0xAA; // Align byte
        *((uint16_t *)&d0[2]) = daq;
#else
        d0[1] = (uint8_t)daq;
#endif

        // Use MSB of ODT to indicate overruns
#ifdef XCP_ENABLE_OVERRUN_INDICATION_PID
        if ((DaqListState(daq) & DAQ_STATE_OVERRUN) != 0) {
            d0[0] |= 0x80; // Set MSB of ODT number
            DaqListState(daq) &= (uint8_t)(~DAQ_STATE_OVERRUN);
        }
#endif

        // Timestamp 32 bit
        if (hs == ODT_HEADER_SIZE + ODT_TIMESTAMP_SIZE) { // First ODT always has a 32 bit timestamp
#if ODT_TIMESTAMP_SIZE != 4
#error "Supports only 32 bit timestamps"
#endif
            *((uint32_t *)&d0[ODT_HEADER_SIZE]) = (uint32_t)clock;
        }

        // Inner loop
        // Loop over all ODT entries in a ODT
        {
            uint8_t *dst = &d0[hs];
            uint32_t e = DaqListOdtTable[odt].first_odt_entry;       // first ODT entry index
            uint32_t el = DaqListOdtTable[odt].last_odt_entry;       // last ODT entry index
            const uint32_t *addr_ptr = &DaqListOdtEntryAddrTable[e]; // pointer to ODT entry addr offset
            const uint8_t *size_ptr = &DaqListOdtEntrySizeTable[e];  // pointer to ODT entry size
#ifdef XCP_ENABLE_DAQ_ADDREXT
            const uint8_t *addr_ext_ptr = &DaqListOdtEntryAddrExtTable[e]; // pointer to ODT entry address extension
#endif
            while (e <= el) {
                uint8_t n = *size_ptr++;
#ifdef XCP_ENABLE_TEST_CHECKS
                assert(n != 0);
#endif
#ifdef XCP_ENABLE_DAQ_ADDREXT
#ifdef XCP_ENABLE_TEST_CHECKS
                assert(*addr_ext_ptr < count && bases[*addr_ext_ptr] != NULL);
#else
                (void)count;
#endif
                const uint8_t *src = (const uint8_t *)&bases[*addr_ext_ptr++][*addr_ptr++];
#else
                const uint8_t *src = (const uint8_t *)&base[*addr_ptr++];
#endif
                memcpy(dst, src, n);
                dst += n;
                e++;
            }
        }

        queuePush(queue_handle, &queue_buffer, DaqListPriority(daq) != 0 && odt == DaqListLastOdt(daq));

    } /* odt */
}

// Trigger DAQ event
// DAQ lists must be valid and DAQ must be running
static void XcpTriggerDaqEvent_(tQueueHandle queue_handle, tXcpEventId event_id, int count, const uint8_t **bases, uint64_t clock) {

#ifdef TEST_ENABLE_DBG_METRICS
    gXcpDaqEventCount++;
#endif

    // Take event timestamp if not already done
    if (clock == 0) {
        clock = ApplXcpGetClock64();
    }

#ifndef XCP_MAX_EVENT_COUNT // Basic mode for arbitrary event ids

    // Loop over all active DAQ lists associated to the current event
    for (uint16_t daq = 0; daq < shared.daq_lists.daq_count; daq++) {
        if ((DaqListState(daq) & DAQ_STATE_RUNNING) == 0)
            continue; // DAQ list not active
        if (DaqListEventChannel(daq) != event_id)
            continue; // DAQ list not associated with this event

#ifndef XCP_ENABLE_DAQ_ADDREXT
        // Address extension unique per DAQ list
        // Build base pointer for this DAQ list
        uint8_t ext = DaqListAddrExt(daq);
        assert(ext < count);
        XcpTriggerDaqList_(queue_handle, daq, bases[ext], clock); // Trigger DAQ list
#else
        XcpTriggerDaqList_(queue_handle, daq, count, bases, clock); // Trigger DAQ list
#endif

    } /* daq */

#else // Event number space is limited to XCP_MAX_EVENT_COUNT, with and without event list

#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    if (event_id >= getEventCount()) {
        DBG_PRINTF_ERROR("Event id %u out of range\n", event_id);
        return;
    }
    const tXcpEvent *event = &shared.event_list.event[event_id];

#ifdef XCP_ENABLE_DAQ_CONTROL
    if ((event->flags & XCP_DAQ_EVENT_FLAG_DISABLED) != 0) {
        return; // Event disabled
    }
#endif

#ifdef XCP_ENABLE_DAQ_PRESCALER
    {
        tXcpEvent *event = &shared_mut.event_list.event[event_id];
        event->daq_prescaler_cnt++;
        if (event->daq_prescaler_cnt >= event->daq_prescaler) {
            event->daq_prescaler_cnt = 0;
        } else {
            return;
        }
    }
#endif

#else
    if (event_id >= XCP_MAX_EVENT_COUNT) {
        DBG_PRINTF_ERROR("Event id %u out of range\n", event_id);
        return;
    }
#endif

    // Loop over all active DAQ lists associated to the current event
    for (uint16_t daq = DaqListFirst(event_id); daq != XCP_UNDEFINED_DAQ_LIST; daq = DaqListNext(daq)) {
        assert(daq < shared.daq_lists.daq_count);
        if (DaqListState(daq) & DAQ_STATE_RUNNING) { // DAQ list active
#ifndef XCP_ENABLE_DAQ_ADDREXT
            // Address extension unique per DAQ list
            // Build base pointer for this DAQ list
            uint8_t ext = DaqListAddrExt(daq);
            XcpTriggerDaqList_(queue_handle, daq, base[ext], clock); // Trigger DAQ list
#else
            XcpTriggerDaqList_(queue_handle, daq, count, bases, clock); // Trigger DAQ list
#endif
        }
    }
#endif
}

// Async command processing for pending command
#ifdef XCP_ENABLE_DYN_ADDRESSING
static void XcpProcessPendingCommand(tXcpEventId event, int count, const uint8_t **bases) {
    if (atomic_load_explicit(&shared.cmd_pending, memory_order_acquire)) {
        // Check if the pending command can be executed in this context
        if (XcpAddrIsDyn(local.mta_ext) && XcpAddrDecodeDynEvent(local.mta_addr) == event) {
            ATOMIC_BOOL_TYPE old_value = true;
            if (atomic_compare_exchange_weak_explicit(&shared_mut_safe.cmd_pending, &old_value, false, memory_order_release, memory_order_relaxed)) {
                // Convert relative signed 16 bit addr in MtaAddr to pointer MtaPtr
                assert(local.mta_ext < count);
                local_mut.mta_ptr = (uint8_t *)(bases[local.mta_ext] + XcpAddrDecodeDynOffset(local.mta_addr));
                local_mut.mta_ext = XCP_ADDR_EXT_PTR;
                XcpAsyncCommand(true, (const uint32_t *)&shared.cmd_pending_crm, shared.cmd_pending_crm_len);
            }
        }
    }
}
#endif // XCP_ENABLE_DYN_ADDRESSING

void XcpEventExtAt_(tXcpEventId event, int count, const uint8_t **bases, uint64_t clock) {

    if (!isStarted())
        return;

#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    if (event >= getEventCount()) {
        DBG_PRINTF_ERROR("Event id %u out of range\n", event);
        return;
    }
#endif

    // Async command processing for pending command
#ifdef XCP_ENABLE_DYN_ADDRESSING
    XcpProcessPendingCommand(event, count, bases);
#endif // XCP_ENABLE_DYN_ADDRESSING

    // Daq
    if (!isDaqRunning())
        return; // DAQ not running
    XcpTriggerDaqEvent_(local.queue, event, count, bases, clock);
}

void XcpEventExt_(tXcpEventId event, int count, const uint8_t **bases) {

    if (!isStarted())
        return;

#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    if (event >= getEventCount()) {
        DBG_PRINTF_ERROR("Event id %u out of range\n", event);
        return;
    }
#endif

    // Async command processing for pending command
#ifdef XCP_ENABLE_DYN_ADDRESSING
    XcpProcessPendingCommand(event, count, bases);
#endif // XCP_ENABLE_DYN_ADDRESSING

    XcpTriggerDaqEvent_(local.queue, event, count, bases, ApplXcpGetClock64());
}

//----------------------------------------------------------------------------
// Public API

#if defined(XCP_ENABLE_DYN_ADDRESSING)

// Supports absolute and dynamic addressing mode with given base pointer
void XcpEventExt(tXcpEventId event, const uint8_t *base) {
#if defined(XCP_ADDRESS_MODE_XCPLITE__ACSDD)
    const uint8_t *bases[3] = {xcp_get_base_addr(), NULL, base};
#else
    const uint8_t *bases[3] = {NULL, xcp_get_base_addr(), base};
#endif
    XcpEventExt_(event, 3, bases);
}
void XcpEventExtAt(tXcpEventId event, const uint8_t *base, uint64_t clock) {
#if defined(XCP_ADDRESS_MODE_XCPLITE__ACSDD)
    const uint8_t *bases[3] = {xcp_get_base_addr(), NULL, base};
#else
    const uint8_t *bases[3] = {NULL, xcp_get_base_addr(), base};
#endif
    XcpEventExtAt_(event, 3, bases, clock);
}

#endif // XCP_ENABLE_DYN_ADDRESSING

// Supports absolute addressing only
void XcpEvent(tXcpEventId event) {
    if (!isDaqRunning())
        return; // DAQ not running

#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    if (event >= getEventCount()) {
        DBG_PRINTF_ERROR("Event id %u out of range\n", event);
        return;
    }
#endif

#if defined(XCP_ADDRESS_MODE_XCPLITE__ACSDD)
    const uint8_t *bases[2] = {xcp_get_base_addr(), NULL};
#else
    const uint8_t *bases[2] = {NULL, xcp_get_base_addr()};
#endif

    XcpTriggerDaqEvent_(local.queue, event, 2, bases, ApplXcpGetClock64());
}
void XcpEventAt(tXcpEventId event, uint64_t clock) {
    if (!isDaqRunning())
        return; // DAQ not running

#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    if (event >= getEventCount()) {
        DBG_PRINTF_ERROR("Event id %u out of range\n", event);
        return;
    }
#endif

#if defined(XCP_ADDRESS_MODE_XCPLITE__ACSDD)
    const uint8_t *bases[2] = {xcp_get_base_addr(), NULL};
#else
    const uint8_t *bases[2] = {NULL, xcp_get_base_addr()};
#endif

    XcpTriggerDaqEvent_(local.queue, event, 2, bases, clock);
}

#if defined(XCP_ENABLE_DYN_ADDRESSING)

// Use int args_count = 0..XCP_ADDR_EXT_DYN_COUNT-1 to avoid parameter promotion

void XcpEventExt_Var(tXcpEventId event, int args_count, ...) {

    if (!isStarted())
        return;

#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    if (event >= getEventCount()) {
        DBG_PRINTF_ERROR("Event id %u out of range\n", event);
        return;
    }
#endif

    va_list args;
    va_start(args, args_count);
    const uint8_t *bases[XCP_ADDR_EXT_DYN_MAX + 1] = {xcp_get_base_addr(), xcp_get_base_addr()};
    assert(args_count < XCP_ADDR_EXT_DYN_COUNT);
    for (int i = 0; i < args_count; i++) {
        bases[XCP_ADDR_EXT_DYN + i] = va_arg(args, uint8_t *);
    }
    va_end(args);

    // Async command processing for pending command
    XcpProcessPendingCommand(event, XCP_ADDR_EXT_DYN + args_count, bases);

    if (!isDaqRunning())
        return; // DAQ not running

    XcpTriggerDaqEvent_(local.queue, event, XCP_ADDR_EXT_DYN + args_count, bases, ApplXcpGetClock64());
}

void XcpEventExtAt_Var(tXcpEventId event, uint64_t clock, int args_count, ...) {

    if (!isStarted())
        return;

#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    if (event >= getEventCount()) {
        DBG_PRINTF_ERROR("Event id %u out of range\n", event);
        return;
    }
#endif

    va_list args;
    va_start(args, args_count);
    const uint8_t *bases[XCP_ADDR_EXT_DYN_MAX + 1] = {xcp_get_base_addr(), xcp_get_base_addr()};
    assert(args_count < XCP_ADDR_EXT_DYN_COUNT);
    for (int i = 0; i < args_count; i++) {
        bases[XCP_ADDR_EXT_DYN + i] = va_arg(args, uint8_t *);
    }
    va_end(args);

    // Async command processing for pending command
    XcpProcessPendingCommand(event, XCP_ADDR_EXT_DYN + args_count, bases);

    if (!isDaqRunning())
        return; // DAQ not running

    XcpTriggerDaqEvent_(local.queue, event, XCP_ADDR_EXT_DYN + args_count, bases, clock);
}

#endif // XCP_ENABLE_DYN_ADDRESSING

#ifdef XCP_ENABLE_DAQ_EVENT_LIST
#ifdef XCP_ENABLE_DAQ_CONTROL
void XcpEventEnable(tXcpEventId event, bool enable) {
    if (!isStarted())
        return;
    if (event == XCP_UNDEFINED_EVENT_ID) {
        DBG_PRINT_WARNING("XcpEventEnable: Undefined event id\n");
        return;
    }
    if (event >= getEventCount()) {
        DBG_PRINTF_WARNING("XcpEventEnableInvalid event id %u\n", event);
        return;
    }
    tXcpEvent *evt = &shared_mut.event_list.event[event];
    if (enable) {
        evt->flags &= (uint8_t)(~XCP_DAQ_EVENT_FLAG_DISABLED);
    } else {
        evt->flags |= XCP_DAQ_EVENT_FLAG_DISABLED;
    }
}

#endif
#endif

/****************************************************************************/
/* Command Processor                                                        */
/****************************************************************************/

// Stops DAQ and goes to disconnected state
void XcpDisconnect(void) {
    if (!isStarted())
        return;

    // In SHM mode, only the server can disconnect
#ifdef OPTION_SHM_MODE // XcpDisconnect in an application which is not the XCP server
    if (!XcpShmIsXcpServer()) {
        return;
    }
#endif // SHM_MODE

    if (isConnected()) {

        if (isDaqRunning()) {
            XcpStopDaq();
            XcpTlWaitForTransmitQueueEmpty(XCP_TRANSMIT_QUEUE_FLUSH_TIMEOUT_MS);
        }

#ifdef XCP_ENABLE_CALSEG_LAZY_WRITE
        XcpCalSegPublishAll(true);
#endif

        // @@@@ TODO: XcpDisconnect is a user function, foreign thread access possible, should be atomic
        shared_mut.session_status &= (uint16_t)(~SS_CONNECTED);
        ApplXcpDisconnect();

        // Freeze working page data
#if defined(OPTION_ENABLE_PERSISTENCE) && defined(XCP_ENABLE_FREEZE_ON_DISCONNECT)
        if ((XcpGetInitMode() & XCP_MODE_PERSISTENCE) != 0) {
            XcpFreeze();
        }
#endif
        DBG_PRINT3("Disconnected\n");
    }
}

// Transmit command response packet
static void XcpSendResponse(bool async, const tXcpCto *crm, uint8_t crmLen) {

#ifndef XCPTL_CRM_VIA_TRANSMIT_QUEUE
    // Send only async command responses via the transmit queue, to ensure thread safety
    // async XcpSendResponse can be called from anywhere in the application threads by the DAQ trigger events !
    // Low latency transmit with XcpTlSendCrm is not thread safe !
    if (!async) {
        // Send directly
        XcpTlSendCrm((const uint8_t *)crm, crmLen);
    } else
#endif
    {
        // Send via transmit queue
        tQueueBuffer queue_buffer = queueAcquire(local.queue, crmLen);
        if (queue_buffer.buffer != NULL) {
            memcpy(queue_buffer.buffer, crm, crmLen);
            queuePush(local.queue, &queue_buffer, true); // High priority = true, disable further packet accumulation
        }
    }

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 4) {
        if (async)
            printf(ANSI_COLOR_BLUE " ASYNC: " ANSI_COLOR_RESET);
        XcpPrintRes(crm);
    }
#endif
}

// Transmit multicast command response
#ifdef XCPTL_ENABLE_MULTICAST
static void XcpSendMulticastResponse(const tXcpCto *crm, uint8_t crmLen, uint8_t *addr, uint16_t port) {

    XcpEthTlSendMulticastCrm((const uint8_t *)crm, crmLen, addr, port);
#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 4)
        XcpPrintRes(crm);
#endif
}
#endif

#ifdef XCP_ENABLE_DYN_ADDRESSING

// Push XCP command which can not be executed in this context for later async execution
// Returns CRC_CMD_BUSY, if there is already a pending async command
static uint8_t XcpPushCommand(const tXcpCto *cmdBuf, uint8_t cmdLen) {

    // Set pending command flag
    ATOMIC_BOOL_TYPE old_value = false;
    if (!atomic_compare_exchange_strong_explicit(&shared_mut_safe.cmd_pending, &old_value, true, memory_order_acq_rel, memory_order_relaxed)) {
        return CRC_CMD_BUSY;
    }
    shared_mut.cmd_pending_crm_len = cmdLen;
    memcpy(&shared_mut.cmd_pending_crm, cmdBuf, cmdLen);
    return CRC_CMD_OK;
}
#endif // XCP_ENABLE_DYN_ADDRESSING

// Execute XCP commands
// Returns
//  CRC_CMD_IGNORED if not in connected state and no response was sent
//  CRC_CMD_BUSY if a command response is pending, while receiving another command or connect denied
//  The XCP error if an error response was pushed to the queue
uint8_t XcpCommand(const uint32_t *cmdBuf, uint8_t cmdLen) { return XcpAsyncCommand(false, cmdBuf, cmdLen); }

//  Handles incoming or async XCP commands
static uint8_t XcpAsyncCommand(bool async, const uint32_t *cmdBuf, uint8_t cmdLen) {

#define CRO ((tXcpCto *)cmdBuf)
#define CRO_LEN (cmdLen)
#define CRO_BYTE(x) (CRO->b[x])
#define CRO_WORD(x) (CRO->w[x])
#define CRO_DWORD(x) (CRO->dw[x])

    uint8_t err = 0;

    assert(isStarted());
    assert(CRO_LEN <= XCPTL_MAX_CTO_SIZE);

    // Prepare the default response
    CRM_CMD = PID_RES; /* Response, no error */
    CRM_LEN = 1;       /* Length = 1 */

    // CONNECT ?
#ifdef XCP_ENABLE_PROTOCOL_LAYER_ETH
    if (CRO_LEN == CRO_CONNECT_LEN && CRO_CMD == CC_CONNECT)
#else
    if (CRO_LEN >= CRO_CONNECT_LEN && CRO_CMD == CC_CONNECT)
#endif
    {
        uint8_t mode = CRO_CONNECT_MODE;
#ifdef DBG_LEVEL
        DBG_PRINTF3("CONNECT mode=%u\n", mode);
        if ((shared.session_status & SS_CONNECTED) != 0)
            DBG_PRINT_WARNING("Already connected! DAQ setup cleared! Legacy mode activated!\n");
#endif

        // Check application is ready for XCP connect
        if (!ApplXcpConnect(mode))
            return CRC_CMD_OK; // Application not ready, ignore

        // Initialize XCP Session Status on connect
        shared_mut.session_status = (SS_ACTIVATED | SS_STARTED | SS_CONNECTED | SS_LEGACY_MODE);

        /* Reset DAQ */
        XcpClearDaq();

        // Response
        CRM_LEN = CRM_CONNECT_LEN;
        CRM_CONNECT_TRANSPORT_VERSION = (uint8_t)((uint16_t)XCP_TRANSPORT_LAYER_VERSION >> 8); /* Major versions of the XCP Protocol Layer and Transport Layer Specifications. */
        CRM_CONNECT_PROTOCOL_VERSION = (uint8_t)((uint16_t)XCP_PROTOCOL_LAYER_VERSION >> 8);
        CRM_CONNECT_MAX_CTO_SIZE = XCPTL_MAX_CTO_SIZE;
        CRM_CONNECT_MAX_DTO_SIZE = XCPTL_MAX_DTO_SIZE;
        CRM_CONNECT_RESOURCE = RM_DAQ | RM_CAL_PAG;      /* DAQ and CAL supported */
        CRM_CONNECT_COMM_BASIC = CMB_OPTIONAL;           // GET_COMM_MODE_INFO available, byte order Intel, address granularity byte, no server block mode
        assert(*(uint8_t *)&shared.session_status == 0); // Intel byte order
    }

    // Handle other all other commands
    else {

#ifdef DBG_LEVEL
        if (DBG_LEVEL >= 4 && !async) {
            XcpPrintCmd(CRO);
        }
#endif
        if (!isConnected() && CRO_CMD != CC_TRANSPORT_LAYER_CMD) { // Must be connected, exception are the transport layer commands
            DBG_PRINT_WARNING("Command ignored because not in connected state, no response sent!\n");
            return CRC_CMD_IGNORED;
        }

        if (CRO_LEN < 1 || CRO_LEN > XCPTL_MAX_CTO_SIZE)
            error(CRC_CMD_SYNTAX);
        switch (CRO_CMD) {

#ifdef XCP_ENABLE_USER_COMMAND
        case CC_USER_CMD: {
            check_len(CRO_USER_CMD_LEN);
            uint8_t subcmd = CRO_USER_CMD_SUBCOMMAND;
#ifdef XCP_ENABLE_CALSEG_LIST
            if (subcmd == 0x01) {
                XcpCalSegBeginAtomicTransaction();
            } else if (subcmd == 0x02) {
                check_error(XcpCalSegEndAtomicTransaction());
            } else
#endif
            {
                check_error(ApplXcpUserCommand(subcmd));
            }
        } break;
#endif // XCP_ENABLE_USER_COMMAND

        // Always return a negative response with the error code ERR_CMD_SYNCH
        case CC_SYNCH: {
            CRM_LEN = CRM_SYNCH_LEN;
            CRM_CMD = PID_ERR;
            CRM_ERR = CRC_CMD_SYNCH;
        } break;

        // Don_t respond, just ignore, no error unkown command, used for testing
        case CC_NOP:
            goto no_response;

        case CC_GET_COMM_MODE_INFO: {
            CRM_LEN = CRM_GET_COMM_MODE_INFO_LEN;
            CRM_GET_COMM_MODE_INFO_DRIVER_VERSION = XCP_DRIVER_VERSION;
            CRM_GET_COMM_MODE_INFO_COMM_OPTIONAL = 0;
            CRM_GET_COMM_MODE_INFO_QUEUE_SIZE = 0;
            CRM_GET_COMM_MODE_INFO_MAX_BS = 0;
            CRM_GET_COMM_MODE_INFO_MIN_ST = 0;
        } break;

        case CC_DISCONNECT: {
            XcpDisconnect();
        } break;

        case CC_GET_ID: {
            check_len(CRO_GET_ID_LEN);
            CRM_LEN = CRM_GET_ID_LEN;
            CRM_GET_ID_LENGTH = 0;
            switch (CRO_GET_ID_TYPE) {
            case IDT_ASCII:
            case IDT_ASAM_NAME:
            case IDT_ASAM_PATH:
            case IDT_ASAM_URL:
                CRM_GET_ID_LENGTH = ApplXcpGetId(CRO_GET_ID_TYPE, CRM_GET_ID_DATA, CRM_GET_ID_DATA_MAX_LEN);
                CRM_LEN = (uint8_t)(CRM_GET_ID_LEN + CRM_GET_ID_LENGTH);
                CRM_GET_ID_MODE = 0x01; // Transfer mode is "Uncompressed data in response"
                break;
            case IDT_ASAM_EPK: {
                /* @@@@ TODO: Remove workaround: EPK is always provided via upload, CANape ignores mode = 0x01 */
                /*
                uint32_t len;
                len = ApplXcpGetId(CRO_GET_ID_TYPE, CRM_GET_ID_DATA, CRM_GET_ID_DATA_MAX_LEN);
                if (len > 0) { // EPK provided in the response
                    CRM_GET_ID_LENGTH = len;
                    CRM_LEN = (uint8_t)(CRM_GET_ID_LEN + CRM_GET_ID_LENGTH);
                    CRM_GET_ID_MODE = 0x01; // Transfer mode is "Uncompressed data in response"
                } else
                */
                {
                    // EPK provided via upload
                    local_mut.mta_addr = XCP_ADDR_EPK;
                    local_mut.mta_ptr = (uint8_t *)XcpGetEcuEpk();
                    local_mut.mta_ext = XCP_ADDR_EXT_PTR;
                    CRM_GET_ID_LENGTH = ApplXcpGetId(CRO_GET_ID_TYPE, NULL, 0);
                    CRM_GET_ID_MODE = 0x00; // Transfer mode is "Uncompressed data upload"
                }
            } break;
#if defined(XCP_ENABLE_IDT_A2L_UPLOAD) || defined(XCP_ENABLE_IDT_ELF_UPLOAD) // A2L and EPK are always provided via upload
#if defined(XCP_ENABLE_IDT_A2L_UPLOAD)
            case IDT_ASAM_UPLOAD:
#endif
#if defined(XCP_ENABLE_IDT_ELF_UPLOAD)
            case IDT_VECTOR_ELF_UPLOAD:
#endif
                local_mut.mta_addr = 0;
                local_mut.mta_ext = XCP_ADDR_EXT_FILE;
                CRM_GET_ID_LENGTH = ApplXcpGetId(CRO_GET_ID_TYPE, NULL, 0);
                CRM_GET_ID_MODE = 0x00; // Transfer mode is "Uncompressed data upload"
                break;
#endif
            default:
                error(CRC_OUT_OF_RANGE);
            }
        } break;

/* Not implemented, no shared.ProtectionStatus checks */
#if 0
#ifdef XCP_ENABLE_SEED_KEY
          case CC_GET_SEED:
          {
             if (CRO_GET_SEED_MODE != 0x00) error(CRC_OUT_OF_RANGE)
             if ((shared.ProtectionStatus & CRO_GET_SEED_RESOURCE) != 0) {  // locked
                  CRM_GET_SEED_LENGTH = ApplXcpGetSeed(CRO_GET_SEED_RESOURCE, CRM_GET_SEED_DATA);;
              } else { // unlocked
                  CRM_GET_SEED_LENGTH = 0; // return 0 if the resource is unprotected
              }
              CRM_LEN = CRM_GET_SEED_LEN;
            }
            break;
 
          case CC_UNLOCK:
            {
              uint8_t resource = ApplXcpUnlock(CRO_UNLOCK_KEY, CRO_UNLOCK_LENGTH);           
              if (0x00 == resource) { // Key wrong !, send ERR_ACCESS_LOCKED and go to disconnected state
                XcpDisconnect();
                error(CRC_ACCESS_LOCKED)
              } else {
                shared_mut.ProtectionStatus &= ~resource; // unlock (reset) the appropriate resource protection mask bit
              }
              CRM_UNLOCK_PROTECTION = shared.ProtectionStatus; // return the current resource protection status
              CRM_LEN = CRM_UNLOCK_LEN;
            }
            break;
#endif /* XCP_ENABLE_SEED_KEY */
#endif

        case CC_GET_STATUS: {
            CRM_LEN = CRM_GET_STATUS_LEN;
            CRM_GET_STATUS_STATUS = (uint8_t)(shared.session_status & 0xFF);
            CRM_GET_STATUS_PROTECTION = 0;
#ifdef XCP_ENABLE_DAQ_RESUME
            /* Return the session configuration id */
            CRM_GET_STATUS_CONFIG_ID = shared.daq_lists.config_id;
#else
            CRM_GET_STATUS_CONFIG_ID = 0x00;
#endif
        } break;

        case CC_SET_MTA: {
            check_len(CRO_SET_MTA_LEN);
            check_error(XcpSetMta(CRO_SET_MTA_EXT, CRO_SET_MTA_ADDR));
        } break;

        case CC_SET_REQUEST: {
            check_len(CRO_SET_REQUEST_LEN);
            CRM_LEN = CRM_SET_REQUEST_LEN;
            switch (CRO_SET_REQUEST_MODE) {
#ifdef XCP_ENABLE_DAQ_RESUME
            case SS_STORE_DAQ_REQ: {
                uint16_t config_id = CRO_SET_REQUEST_CONFIG_ID;
                shared.daq_lists.config_id = config_id;
                // shared_mut.session_status |= SS_STORE_DAQ_REQ;
                check_error(ApplXcpDaqResumeStore(config_id));
                // @@@@ TODO: Send an event message
                // shared_mut.session_status &= (uint16_t)(~SS_STORE_DAQ_REQ);
            } break;
            case SS_CLEAR_DAQ_REQ:
                // shared_mut.session_status |= SS_CLEAR_DAQ_REQ;
                check_error(ApplXcpDaqResumeClear());
                // @@@@ TODO: Send an event message
                // shared_mut.session_status &= (uint16_t)(~SS_CLEAR_DAQ_REQ);
                break;
#endif /* XCP_ENABLE_DAQ_RESUME */
#ifdef XCP_ENABLE_FREEZE_CAL_PAGE
            case SET_REQUEST_MODE_STORE_CAL:
#ifdef XCP_ENABLE_CALSEG_LIST
                check_error(XcpFreezeSelectedCalSegs(false));
#else
                check_error(ApplXcpCalFreeze());
#endif
                break;
#endif // XCP_ENABLE_FREEZE_CAL_PAGE
            default:
                error(CRC_OUT_OF_RANGE) /* SS_STORE_CAL_REQ is not implemented */
            }
        } break;

        case CC_DOWNLOAD: {
            check_len(CRO_DOWNLOAD_LEN);
            uint8_t size = CRO_DOWNLOAD_SIZE; // Variable CRO_LEN
            if (size > CRO_DOWNLOAD_MAX_SIZE || size > CRO_LEN - CRO_DOWNLOAD_LEN)
                error(CRC_CMD_SYNTAX)
#ifdef XCP_ENABLE_DYN_ADDRESSING
                    if (XcpAddrIsDyn(local.mta_ext)) {
                    if (XcpPushCommand(CRO, CRO_LEN) == CRC_CMD_BUSY)
                        goto busy_response;
                    goto no_response;
                }
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
            if (XcpAddrIsRel(local.mta_ext)) {
                error(CRC_ACCESS_DENIED);
            }
#endif
            check_error(XcpWriteMta(size, CRO_DOWNLOAD_DATA));
        } break;

        case CC_SHORT_DOWNLOAD: {
            check_len(CRO_SHORT_DOWNLOAD_LEN);
            uint8_t size = CRO_SHORT_DOWNLOAD_SIZE; // Variable CRO_LEN
            if (size > CRO_SHORT_DOWNLOAD_MAX_SIZE || size > CRO_LEN - CRO_SHORT_DOWNLOAD_LEN)
                error(CRC_CMD_SYNTAX);
            if (!async) { // When SHORT_DOWNLOAD is executed async, MtaXxx was already set
                check_error(XcpSetMta(CRO_SHORT_DOWNLOAD_EXT, CRO_SHORT_DOWNLOAD_ADDR));
            }
#ifdef XCP_ENABLE_DYN_ADDRESSING
            if (XcpAddrIsDyn(local.mta_ext)) {
                if (XcpPushCommand(CRO, CRO_LEN) == CRC_CMD_BUSY)
                    goto busy_response;
                goto no_response;
            }
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
            if (XcpAddrIsRel(local.mta_ext)) {
                error(CRC_ACCESS_DENIED);
            }
#endif
            check_error(XcpWriteMta(size, CRO_SHORT_DOWNLOAD_DATA));
        } break;

        case CC_UPLOAD: {
            check_len(CRO_UPLOAD_LEN);
            uint8_t size = CRO_UPLOAD_SIZE;
            if (size > CRM_UPLOAD_MAX_SIZE)
                error(CRC_OUT_OF_RANGE);
#ifdef XCP_ENABLE_DYN_ADDRESSING
            if (XcpAddrIsDyn(local.mta_ext)) {
                if (XcpPushCommand(CRO, CRO_LEN) == CRC_CMD_BUSY)
                    goto busy_response;
                goto no_response;
            }
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
            if (XcpAddrIsRel(local.mta_ext)) {
                error(CRC_ACCESS_DENIED);
            }
#endif
            check_error(XcpReadMta(size, CRM_UPLOAD_DATA));
            CRM_LEN = (uint8_t)(CRM_UPLOAD_LEN + size);
        } break;

        case CC_SHORT_UPLOAD: {
            check_len(CRO_SHORT_UPLOAD_LEN);
            uint8_t size = CRO_SHORT_UPLOAD_SIZE;
            if (size > CRM_SHORT_UPLOAD_MAX_SIZE)
                error(CRC_OUT_OF_RANGE);
            if (!async) { // When SHORT_UPLOAD is executed async, MtaXxx was already set
                check_error(XcpSetMta(CRO_SHORT_UPLOAD_EXT, CRO_SHORT_UPLOAD_ADDR));
            }
#ifdef XCP_ENABLE_DYN_ADDRESSING
            if (XcpAddrIsDyn(local.mta_ext)) {
                if (XcpPushCommand(CRO, CRO_LEN) == CRC_CMD_BUSY)
                    goto busy_response;
                goto no_response;
            }
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
            if (XcpAddrIsRel(local.mta_ext)) {
                error(CRC_ACCESS_DENIED);
            }
#endif
            check_error(XcpReadMta(size, CRM_SHORT_UPLOAD_DATA));
            CRM_LEN = (uint8_t)(CRM_SHORT_UPLOAD_LEN + size);
        } break;

#ifdef XCP_ENABLE_CAL_PAGE
        case CC_SET_CAL_PAGE: {
            check_len(CRO_SET_CAL_PAGE_LEN);
            uint8_t segment = CRO_SET_CAL_PAGE_SEGMENT;
#ifdef XCP_ENABLE_CALSEG_LIST
            check_error(XcpCalSegSetCalPage(segment, CRO_SET_CAL_PAGE_PAGE, CRO_SET_CAL_PAGE_MODE));
#else
            check_error(ApplXcpSetCalPage(segment, CRO_SET_CAL_PAGE_PAGE, CRO_SET_CAL_PAGE_MODE));
#endif
        } break;

        case CC_GET_CAL_PAGE: {
            uint8_t page;
            check_len(CRO_GET_CAL_PAGE_LEN);
            uint8_t segment = CRO_SET_CAL_PAGE_SEGMENT;
            CRM_LEN = CRM_GET_CAL_PAGE_LEN;
#ifdef XCP_ENABLE_CALSEG_LIST
            page = XcpCalSegGetCalPage(segment, CRO_GET_CAL_PAGE_MODE);
#else
            page = ApplXcpGetCalPage(segment, CRO_GET_CAL_PAGE_MODE);
#endif
            if (page == 0xFF)
                error(CRC_MODE_NOT_VALID);
            CRM_GET_CAL_PAGE_PAGE = page;
        } break;

#ifdef XCP_ENABLE_COPY_CAL_PAGE
        case CC_COPY_CAL_PAGE: {
            CRM_LEN = CRM_COPY_CAL_PAGE_LEN;
            uint8_t src_segment = CRO_COPY_CAL_PAGE_SRC_SEGMENT;
            uint8_t src_page = CRO_COPY_CAL_PAGE_SRC_PAGE;
            uint8_t dst_segment = CRO_COPY_CAL_PAGE_DEST_SEGMENT;
            uint8_t dst_page = CRO_COPY_CAL_PAGE_DEST_PAGE;
#ifdef XCP_ENABLE_CALSEG_LIST
            check_error(XcpCalSegCopyCalPage(src_segment, src_page, dst_segment, dst_page));
#else
            check_error(ApplXcpCopyCalPage(src_segment, src_page, dst_segment, dst_page));
#endif
        } break;
#endif // XCP_ENABLE_COPY_CAL_PAGE
#endif // XCP_ENABLE_CAL_PAGE

#ifdef XCP_ENABLE_CALSEG_LIST

        case CC_GET_PAG_PROCESSOR_INFO: {
            check_len(CRO_GET_PAG_PROCESSOR_INFO_LEN);
            CRM_LEN = CRM_GET_PAG_PROCESSOR_INFO_LEN;
            CRM_GET_PAG_PROCESSOR_INFO_MAX_SEGMENTS = XcpGetMemSegCount();
#ifdef XCP_ENABLE_FREEZE_CAL_PAGE
            CRM_GET_PAG_PROCESSOR_INFO_PROPERTIES = PAG_PROPERTY_FREEZE; // Freeze mode supported
#else
            CRM_GET_PAG_PROCESSOR_INFO_PROPERTIES = 0;
#endif
        } break;

#ifdef XCP_ENABLE_FREEZE_CAL_PAGE
        case CC_SET_SEGMENT_MODE: {
            check_len(CRO_SET_SEGMENT_MODE_LEN);
            uint8_t segment = CRO_SET_SEGMENT_MODE_SEGMENT;
            uint8_t mode = CRO_SET_SEGMENT_MODE_MODE;
            check_error(XcpSetCalSegMode(segment, mode));
        } break;

        case CC_GET_SEGMENT_MODE: {
            check_len(CRO_GET_SEGMENT_MODE_LEN);
            uint8_t segment = CRO_GET_SEGMENT_MODE_SEGMENT;
            CRM_LEN = CRM_GET_SEGMENT_MODE_LEN;
            CRM_GET_SEGMENT_MODE_MODE = XcpGetCalSegMode(segment);
        } break;

#endif // XCP_ENABLE_FREEZE_CAL_PAGE

        case CC_GET_SEGMENT_INFO: {
            check_len(CRO_GET_SEGMENT_INFO_LEN);
            uint8_t mode = CRO_GET_SEGMENT_INFO_MODE;
            uint8_t segment = CRO_GET_SEGMENT_INFO_SEGMENT_NUMBER;
            uint8_t segInfo = CRO_GET_SEGMENT_INFO_SEGMENT_INFO;
            uint8_t mapIndex = CRO_GET_SEGMENT_INFO_MAPPING_INDEX;
            check_error(XcpGetSegInfo(segment, mode, segInfo, mapIndex));
        } break;

        case CC_GET_PAGE_INFO: {
            check_len(CRO_GET_PAGE_INFO_LEN);
            uint8_t segment = CRO_GET_PAGE_INFO_SEGMENT_NUMBER;
            uint8_t page = CRO_GET_PAGE_INFO_PAGE_NUMBER;
            check_error(XcpGetSegPageInfo(segment, page));
        } break;

#endif // XCP_ENABLE_CALSEG_LIST

#ifdef XCP_ENABLE_CHECKSUM
        case CC_BUILD_CHECKSUM: {
            check_len(CRO_BUILD_CHECKSUM_LEN);
#ifdef XCP_ENABLE_DYN_ADDRESSING
            if (XcpAddrIsDyn(local.mta_ext)) {
                XcpPushCommand(CRO, CRO_LEN);
                goto no_response;
            } // Execute in async mode
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
            if (XcpAddrIsRel(local.mta_ext)) {
                error(CRC_ACCESS_DENIED);
            }
#endif
            check_error(calcChecksum(CRO_BUILD_CHECKSUM_SIZE, &CRM_BUILD_CHECKSUM_RESULT));
            CRM_BUILD_CHECKSUM_TYPE = XCP_CHECKSUM_TYPE;
            CRM_LEN = CRM_BUILD_CHECKSUM_LEN;
        } break;
#endif // XCP_ENABLE_CHECKSUM

        case CC_GET_DAQ_PROCESSOR_INFO: {
            CRM_LEN = CRM_GET_DAQ_PROCESSOR_INFO_LEN;
            CRM_GET_DAQ_PROCESSOR_INFO_MIN_DAQ = 0;                          // Total number of predefined DAQ lists
            CRM_GET_DAQ_PROCESSOR_INFO_MAX_DAQ = shared.daq_lists.daq_count; // Number of currently dynamically allocated DAQ lists
#if defined(XCP_ENABLE_DAQ_EVENT_INFO) && defined(XCP_ENABLE_DAQ_EVENT_LIST)
            CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT = getEventCount(); // Number of currently available event channels which can be queried by GET_DAQ_EVENT_INFO
#else
            CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT = 0; // 0 - unknown, because GET_DAQ_EVENT_INFO is not enabled
#endif
            // Optimization type: default
            // Address extension type:
            //   DAQ_EXT_DAQ: Address extension to be the same for all entries within one DAQ
            // DTO identification field type:
            //   DAQ_HDR_ODT_DAQB: Relative ODT number (BYTE), absolute DAQ list number (BYTE)
            //   DAQ_HDR_ODT_FIL_DAQW: Relative ODT number (BYTE), fill byte, absolute DAQ list number (WORD, aligned)
#if ODT_HEADER_SIZE == 4
            CRM_GET_DAQ_PROCESSOR_INFO_DAQ_KEY_BYTE = DAQ_HDR_ODT_FIL_DAQW;
#else
            CRM_GET_DAQ_PROCESSOR_INFO_DAQ_KEY_BYTE = DAQ_HDR_ODT_DAQB;
#endif
#ifdef XCP_ENABLE_DAQ_ADDREXT
            CRM_GET_DAQ_PROCESSOR_INFO_DAQ_KEY_BYTE |= DAQ_EXT_FREE;
#else
            CRM_GET_DAQ_PROCESSOR_INFO_DAQ_KEY_BYTE |= DAQ_EXT_DAQ;
#endif

            // Dynamic DAQ list configuration, timestamps, resume, prescaler and overrun indication options
            // Identification field can not be switched off, bitwise data stimulation not supported
            CRM_GET_DAQ_PROCESSOR_INFO_PROPERTIES = (uint8_t)(DAQ_PROPERTY_CONFIG_TYPE | DAQ_PROPERTY_TIMESTAMP |
#ifdef XCP_ENABLE_OVERRUN_INDICATION_PID
                                                              DAQ_OVERLOAD_INDICATION_PID |
#endif
#ifdef XCP_ENABLE_OVERRUN_INDICATION_EVENT
                                                              DAQ_OVERLOAD_INDICATION_EVENT |
#endif
#ifdef XCP_ENABLE_DAQ_RESUME
                                                              DAQ_PROPERTY_RESUME |
#endif
#ifdef XCP_ENABLE_DAQ_PRESCALER
                                                              DAQ_PROPERTY_PRESCALER |
#endif
                                                              0);

        } break;

        case CC_GET_DAQ_RESOLUTION_INFO: {
            CRM_LEN = CRM_GET_DAQ_RESOLUTION_INFO_LEN;
            CRM_GET_DAQ_RESOLUTION_INFO_GRANULARITY_DAQ = 1;
            CRM_GET_DAQ_RESOLUTION_INFO_GRANULARITY_STIM = 1;
            CRM_GET_DAQ_RESOLUTION_INFO_MAX_SIZE_DAQ = (uint8_t)XCP_MAX_ODT_ENTRY_SIZE;
            CRM_GET_DAQ_RESOLUTION_INFO_MAX_SIZE_STIM = (uint8_t)XCP_MAX_ODT_ENTRY_SIZE;
#if ODT_TIMESTAMP_SIZE != 4
#error "Supports only 32 bit timestamps"
#endif
            CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE = XCP_TIMESTAMP_UNIT | DAQ_TIMESTAMP_FIXED | DAQ_TIMESTAMP_DWORD;
            CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS = XCP_TIMESTAMP_TICKS;
        } break;

#ifdef XCP_ENABLE_DAQ_EVENT_INFO
        case CC_GET_DAQ_EVENT_INFO: {
            check_len(CRO_GET_DAQ_EVENT_INFO_LEN);
            uint16_t eventNumber = CRO_GET_DAQ_EVENT_INFO_EVENT;
            const tXcpEvent *event = XcpGetEvent(eventNumber);
            if (event == NULL) {
                error(CRC_OUT_OF_RANGE);
            }
            const char *eventName = XcpGetEventName(eventNumber);
            assert(eventName != NULL);

            // Convert cycle time to ASAM XCP IF_DATA coding time cycle and time unit
            // RESOLUTION OF TIMESTAMP "UNIT_1NS" = 0, "UNIT_10NS" = 1, ...
            uint8_t timeUnit = 0;                      // timeCycle unit, 1ns=0, 10ns=1, 100ns=2, 1us=3, ..., 1ms=6, ...
            uint32_t timeCycle = event->cycle_time_ns; // cycle time in units, 0 = sporadic or unknown
            while (timeCycle >= 256) {
                timeCycle /= 10;
                timeUnit++;
            }
            CRM_LEN = CRM_GET_DAQ_EVENT_INFO_LEN;
            CRM_GET_DAQ_EVENT_INFO_PROPERTIES = DAQ_EVENT_PROPERTIES_DAQ | DAQ_EVENT_PROPERTIES_EVENT_CONSISTENCY;
            CRM_GET_DAQ_EVENT_INFO_MAX_DAQ_LIST = 0xFF;
            CRM_GET_DAQ_EVENT_INFO_NAME_LENGTH = (uint8_t)STRNLEN(eventName, XCP_MAX_EVENT_NAME);
            CRM_GET_DAQ_EVENT_INFO_TIME_CYCLE = (uint8_t)timeCycle;
            CRM_GET_DAQ_EVENT_INFO_TIME_UNIT = timeUnit;
            CRM_GET_DAQ_EVENT_INFO_PRIORITY = (event->flags & XCP_DAQ_EVENT_FLAG_PRIORITY) ? 0xFF : 0x00;
            // Event name provided via upload
            local_mut.mta_ptr = (uint8_t *)eventName;
            local_mut.mta_ext = XCP_ADDR_EXT_PTR;
        } break;
#endif // XCP_ENABLE_DAQ_EVENT_INFO

        case CC_FREE_DAQ: {
            XcpClearDaq();
        } break;

        case CC_ALLOC_DAQ: {
            check_len(CRO_ALLOC_DAQ_LEN);
            if (XcpIsDaqRunning())
                error(CRC_DAQ_ACTIVE);
            uint16_t count = CRO_ALLOC_DAQ_COUNT;
            check_error(XcpAllocDaq(count));
        } break;

        case CC_ALLOC_ODT: {
            check_len(CRO_ALLOC_ODT_LEN);
            if (XcpIsDaqRunning())
                error(CRC_DAQ_ACTIVE);
            uint16_t daq = CRO_ALLOC_ODT_DAQ;
            uint8_t count = CRO_ALLOC_ODT_COUNT;
            check_error(XcpAllocOdt(daq, count))
        } break;

        case CC_ALLOC_ODT_ENTRY: {
            check_len(CRO_ALLOC_ODT_ENTRY_LEN);
            if (XcpIsDaqRunning())
                error(CRC_DAQ_ACTIVE);
            uint16_t daq = CRO_ALLOC_ODT_ENTRY_DAQ;
            uint8_t odt = CRO_ALLOC_ODT_ENTRY_ODT;
            uint8_t count = CRO_ALLOC_ODT_ENTRY_COUNT;
            check_error(XcpAllocOdtEntry(daq, odt, count))
        } break;

        case CC_GET_DAQ_LIST_MODE: {
            check_len(CRO_GET_DAQ_LIST_MODE_LEN);
            uint16_t daq = CRO_GET_DAQ_LIST_MODE_DAQ;

            if (daq >= shared.daq_lists.daq_count)
                error(CRC_OUT_OF_RANGE);
            CRM_LEN = CRM_GET_DAQ_LIST_MODE_LEN;
            CRM_GET_DAQ_LIST_MODE_MODE = DaqListMode(daq);
            CRM_GET_DAQ_LIST_MODE_PRESCALER = 1;
            CRM_GET_DAQ_LIST_MODE_EVENTCHANNEL = DaqListEventChannel(daq);
            CRM_GET_DAQ_LIST_MODE_PRIORITY = DaqListPriority(daq);
        } break;

        case CC_SET_DAQ_LIST_MODE: {
            check_len(CRO_SET_DAQ_LIST_MODE_LEN);
            if (XcpIsDaqRunning())
                error(CRC_DAQ_ACTIVE);
            uint16_t daq = CRO_SET_DAQ_LIST_MODE_DAQ;
            uint16_t event = CRO_SET_DAQ_LIST_MODE_EVENTCHANNEL;
            uint8_t mode = CRO_SET_DAQ_LIST_MODE_MODE;
            uint8_t prio = CRO_SET_DAQ_LIST_MODE_PRIORITY;
            uint8_t prescaler = CRO_SET_DAQ_LIST_MODE_PRESCALER;

            if (daq >= shared.daq_lists.daq_count)
                error(CRC_OUT_OF_RANGE);
            if ((mode & (DAQ_MODE_ALTERNATING | DAQ_MODE_DIRECTION | DAQ_MODE_DTO_CTR | DAQ_MODE_PID_OFF)) != 0)
                error(CRC_OUT_OF_RANGE); // none of these modes implemented
            if ((mode & DAQ_MODE_TIMESTAMP) == 0)
                error(CRC_CMD_SYNTAX); // timestamp is fixed on
            check_error(XcpSetDaqListMode(daq, event, mode, prescaler, prio));
            break;
        }

        case CC_SET_DAQ_PTR: {
            check_len(CRO_SET_DAQ_PTR_LEN);
            uint16_t daq = CRO_SET_DAQ_PTR_DAQ;
            uint8_t odt = CRO_SET_DAQ_PTR_ODT;
            uint8_t idx = CRO_SET_DAQ_PTR_IDX;
            check_error(XcpSetDaqPtr(daq, odt, idx));
        } break;

        case CC_WRITE_DAQ: {
            check_len(CRO_WRITE_DAQ_LEN);
            check_error(XcpAddOdtEntry(CRO_WRITE_DAQ_ADDR, CRO_WRITE_DAQ_EXT, CRO_WRITE_DAQ_SIZE));
        } break;

        case CC_WRITE_DAQ_MULTIPLE: {
            check_len(CRO_WRITE_DAQ_MULTIPLE_LEN(1));
            uint8_t n = CRO_WRITE_DAQ_MULTIPLE_NODAQ;
            check_len(CRO_WRITE_DAQ_MULTIPLE_LEN(n));
            for (int i = 0; i < n; i++) {
                check_error(XcpAddOdtEntry(CRO_WRITE_DAQ_MULTIPLE_ADDR(i), CRO_WRITE_DAQ_MULTIPLE_EXT(i), CRO_WRITE_DAQ_MULTIPLE_SIZE(i)));
            }
        } break;

        case CC_START_STOP_DAQ_LIST: // start, stop, select individual daq list
        {
            check_len(CRO_START_STOP_DAQ_LIST_LEN);
            uint16_t daq = CRO_START_STOP_DAQ_LIST_DAQ;
            uint8_t mode = CRO_START_STOP_DAQ_LIST_MODE;

            if (daq >= shared.daq_lists.daq_count)
                error(CRC_OUT_OF_RANGE);
            CRM_LEN = CRM_START_STOP_DAQ_LIST_LEN;
            CRM_START_STOP_DAQ_LIST_FIRST_PID = 0; // PID one byte header type not supported
            if (mode == 2) {                       // select
                DaqListStateMut(daq) |= DAQ_STATE_SELECTED;
            } else if (mode == 1) { // start
#ifdef XCP_ENABLE_TEST_CHECKS
                DBG_PRINT_ERROR("START_STOP_DAQ_LIST to start individual DAQ list is not supported, START_STOP_SYNCH start selected is mandatory!\n");
                error(CRC_MODE_NOT_VALID);
#endif
                XcpStartDaqList(daq); // start DAQ list
                XcpStartDaq();        // start event processing, if not already running
            } else if (mode == 0) {   // stop
                XcpStopDaqList(daq);  // stop individual daq list, stop event processing if all DAQ lists are stopped
            } else {                  // illegal mode
                error(CRC_MODE_NOT_VALID);
            }
        } break;

        case CC_START_STOP_SYNCH: // prepare, start, stop selected daq lists or stop all
        {

            if ((0 == shared.daq_lists.daq_count) || (0 == shared.daq_lists.odt_count) || (0 == shared.daq_lists.odt_entry_count))
                error(CRC_DAQ_CONFIG);
            check_len(CRO_START_STOP_SYNCH_LEN);
            switch (CRO_START_STOP_SYNCH_MODE) {
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
            case 3: /* prepare for start selected */
#ifdef XCP_ENABLE_TEST_CHECKS
                if (!XcpCheckPreparedDaqLists())
                    error(CRC_DAQ_CONFIG);
#endif
                if (!ApplXcpPrepareDaq())
                    error(CRC_RESOURCE_TEMPORARY_NOT_ACCESSIBLE);
                break;
#endif
            case 2:                        /* stop selected */
                XcpStopSelectedDaqLists(); // stop event processing, if all DAQ lists are stopped
                break;
            case 1: /* start selected */
#ifdef XCP_ENABLE_TEST_CHECKS
                if (XcpIsDaqRunning()) {
                    DBG_PRINT_ERROR("DAQ is already running, start of additional DAQ list sets is not supported!\n");
                    error(CRC_DAQ_ACTIVE);
                }
#endif
                XcpSendResponse(async, &CRM, CRM_LEN); // Transmit response first and then start DAQ
                XcpStartSelectedDaqLists();
                XcpStartDaq();    // start DAQ event processing, if not already running
                goto no_response; // Do not send response again
            case 0:               /* stop all */
                XcpStopDaq();
                if (!XcpTlWaitForTransmitQueueEmpty(XCP_TRANSMIT_QUEUE_FLUSH_TIMEOUT_MS)) { // Wait until daq transmit queue empty before sending command response
                    DBG_PRINT_WARNING("Transmit queue flush timeout!\n");
                }
                break;
            default:
                error(CRC_MODE_NOT_VALID);
            }
        } break;

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103 && defined(XCP_ENABLE_PROTOCOL_LAYER_ETH)
        case CC_TIME_CORRELATION_PROPERTIES: {
            check_len(CRO_TIME_SYNCH_PROPERTIES_LEN);
            CRM_LEN = CRM_TIME_SYNCH_PROPERTIES_LEN;
            if ((CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES & TIME_SYNCH_SET_PROPERTIES_RESPONSE_FMT) >= 1) { // set extended format
                DBG_PRINTF4("  Timesync extended mode activated (RESPONSE_FMT=%u)\n", CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES & TIME_SYNCH_SET_PROPERTIES_RESPONSE_FMT);
                shared_mut.session_status &= (uint16_t)(~SS_LEGACY_MODE);
            }
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
            if ((CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES & TIME_SYNCH_SET_PROPERTIES_CLUSTER_ID) != 0) { // set cluster id
                DBG_PRINTF4("  Cluster id set to %u\n", CRO_TIME_SYNCH_PROPERTIES_CLUSTER_ID);
                local_mut.cluster_id = CRO_TIME_SYNCH_PROPERTIES_CLUSTER_ID; // Set cluster id
                XcpEthTlSetClusterId(local.cluster_id);
            }
            CRM_TIME_SYNCH_PROPERTIES_CLUSTER_ID = local.cluster_id;
#else
            if ((CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES & TIME_SYNCH_SET_PROPERTIES_CLUSTER_ID) != 0) { // set cluster id
                // @@@@ TODO: Workaround for CANape bug
                // error(CRC_OUT_OF_RANGE);
                DBG_PRINTF4("  Cluster id = %u setting ignored\n", CRO_TIME_SYNCH_PROPERTIES_CLUSTER_ID);
            }
            CRM_TIME_SYNCH_PROPERTIES_CLUSTER_ID = 0;
#endif
            if ((CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES & TIME_SYNCH_SET_PROPERTIES_TIME_SYNCH_BRIDGE) != 0)
                error(CRC_OUT_OF_RANGE); // set time sync bride is not supported -> error
            CRM_TIME_SYNCH_PROPERTIES_SERVER_CONFIG =
                SERVER_CONFIG_RESPONSE_FMT_ADVANCED | SERVER_CONFIG_DAQ_TS_SERVER | SERVER_CONFIG_TIME_SYNCH_BRIDGE_NONE; // SERVER_CONFIG_RESPONSE_FMT_LEGACY
            CRM_TIME_SYNCH_PROPERTIES_RESERVED = 0x0;
#ifndef XCP_ENABLE_PTP
            CRM_TIME_SYNCH_PROPERTIES_OBSERVABLE_CLOCKS = LOCAL_CLOCK_FREE_RUNNING | GRANDM_CLOCK_NONE | ECU_CLOCK_NONE;
            CRM_TIME_SYNCH_PROPERTIES_SYNCH_STATE = LOCAL_CLOCK_STATE_FREE_RUNNING;
            CRM_TIME_SYNCH_PROPERTIES_CLOCK_INFO = CLOCK_INFO_SERVER;
#else                                                                                                               // XCP_ENABLE_PTP
            if (ApplXcpGetClockInfoGrandmaster(local_mut.clock_info.server.UUID, local_mut.clock_info.grandmaster.UUID, &local_mut.clock_info.grandmaster.epochOfGrandmaster,
                                               &local_mut.clock_info.grandmaster.stratumLevel)) { // Update UUID and clock details
                CRM_TIME_SYNCH_PROPERTIES_OBSERVABLE_CLOCKS = LOCAL_CLOCK_SYNCHED | GRANDM_CLOCK_READABLE | ECU_CLOCK_NONE;
                DBG_PRINTF4("  GrandmasterClock: UUID=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X stratumLevel=%u, epoch=%u\n", local.clock_info.grandmaster.UUID[0],
                            local.clock_info.grandmaster.UUID[1], local.clock_info.grandmaster.UUID[2], local.clock_info.grandmaster.UUID[3], local.clock_info.grandmaster.UUID[4],
                            local.clock_info.grandmaster.UUID[5], local.clock_info.grandmaster.UUID[6], local.clock_info.grandmaster.UUID[7],
                            local.clock_info.grandmaster.stratumLevel, local.clock_info.grandmaster.epochOfGrandmaster);
                CRM_TIME_SYNCH_PROPERTIES_SYNCH_STATE = ApplXcpGetClockState();
                DBG_PRINTF4("  SyncState: %u\n", CRM_TIME_SYNCH_PROPERTIES_SYNCH_STATE);
                CRM_TIME_SYNCH_PROPERTIES_CLOCK_INFO = CLOCK_INFO_SERVER | CLOCK_INFO_GRANDM | CLOCK_INFO_RELATION;
            } else {
                CRM_TIME_SYNCH_PROPERTIES_OBSERVABLE_CLOCKS = LOCAL_CLOCK_FREE_RUNNING | GRANDM_CLOCK_NONE | ECU_CLOCK_NONE;
                CRM_TIME_SYNCH_PROPERTIES_SYNCH_STATE = LOCAL_CLOCK_STATE_FREE_RUNNING;
                CRM_TIME_SYNCH_PROPERTIES_CLOCK_INFO = CLOCK_INFO_SERVER;
            }
#endif                                                                                                              // XCP_ENABLE_PTP
            if ((CRO_TIME_SYNCH_PROPERTIES_GET_PROPERTIES_REQUEST & TIME_SYNCH_GET_PROPERTIES_GET_CLK_INFO) != 0) { // check whether MTA based upload is requested
                local_mut.mta_ptr = (uint8_t *)&local_mut.clock_info.server;                                        // updated above
                local_mut.mta_ext = XCP_ADDR_EXT_PTR;
            }
        } break;
#endif // >= 0x0103

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103

        case CC_TRANSPORT_LAYER_CMD:
            switch (CRO_TL_SUBCOMMAND) {

#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
            case CC_TL_GET_DAQ_CLOCK_MULTICAST: {
                check_len(CRO_GET_DAQ_CLOC_MCAST_LEN);
                uint16_t clusterId = CRO_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER;
                if (local.cluster_id != clusterId)
                    error(CRC_OUT_OF_RANGE);
                CRM_CMD = PID_EV;
                CRM_EVENTCODE = EVC_TIME_SYNCH;
                CRM_GET_DAQ_CLOCK_MCAST_TRIGGER_INFO =
                    0x18 + 0x02;       // TIME_OF_SAMPLING (Bitmask 0x18, 3 - Sampled on reception) + TRIGGER_INITIATOR ( Bitmask 0x07, 2 - GET_DAQ_CLOCK_MULTICAST)
                if (!isLegacyMode()) { // Extended format
#ifdef XCP_DAQ_CLOCK_64BIT
                    CRM_LEN = CRM_GET_DAQ_CLOCK_MCAST_LEN + 8;
                    CRM_GET_DAQ_CLOCK_MCAST_PAYLOAD_FMT = DAQ_CLOCK_PAYLOAD_FMT_ID | DAQ_CLOCK_PAYLOAD_FMT_SLV_64; // size of timestamp is DLONG + CLUSTER_ID
                    CRM_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER64 = CRO_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER;
                    CRM_GET_DAQ_CLOCK_MCAST_COUNTER64 = CRO_GET_DAQ_CLOCK_MCAST_COUNTER;
                    uint64_t clock = ApplXcpGetClock64();
                    CRM_GET_DAQ_CLOCK_MCAST_TIME64_LOW = (uint32_t)(clock);
                    CRM_GET_DAQ_CLOCK_MCAST_TIME64_HIGH = (uint32_t)(clock >> 32);
                    CRM_GET_DAQ_CLOCK_MCAST_SYNCH_STATE64 = ApplXcpGetClockState();
#else
                    CRM_LEN = CRM_GET_DAQ_CLOCK_MCAST_LEN + 4;
                    CRM_GET_DAQ_CLOCK_MCAST_PAYLOAD_FMT = DAQ_CLOCK_PAYLOAD_FMT_ID | DAQ_CLOCK_PAYLOAD_FMT_SLV_32; // size of timestamp is DWORD + CLUSTER_ID
                    CRM_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER = CRO_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER;
                    CRM_GET_DAQ_CLOCK_MCAST_COUNTER = CRO_GET_DAQ_CLOCK_MCAST_COUNTER;
                    CRM_GET_DAQ_CLOCK_MCAST_TIME = (uint32_t)ApplXcpGetClock64();
                    CRM_GET_DAQ_CLOCK_MCAST_SYNCH_STATE = ApplXcpGetClockState();
#endif
                    if (CRM_LEN > XCPTL_MAX_CTO_SIZE)
                        error(CRC_CMD_UNKNOWN); // Extended mode needs enough CTO size
                } else {                        // Legacy format
                    CRM_LEN = CRM_GET_DAQ_CLOCK_MCAST_LEN;
                    CRM_GET_DAQ_CLOCK_MCAST_PAYLOAD_FMT = DAQ_CLOCK_PAYLOAD_FMT_SLV_32; // size of timestamp is DWORD
                    CRM_GET_DAQ_CLOCK_MCAST_TIME = (uint32_t)ApplXcpGetClock64();
                }
            } break;
#endif // XCP_ENABLE_DAQ_CLOCK_MULTICAST

#ifdef XCPTL_ENABLE_MULTICAST
            case CC_TL_GET_SERVER_ID:
                goto no_response; // Not supported, no response, response has atypical layout

            case CC_TL_GET_SERVER_ID_EXTENDED:
                check_len(CRO_TL_GET_SERVER_ID_LEN);
                bool server_isTCP;
                uint16_t server_port;
                uint8_t server_addr[4] = {0, 0, 0, 0};
                uint8_t server_mac[6] = {0, 0, 0, 0, 0, 0};
                uint16_t client_port;
                uint8_t client_addr[4];
                client_port = CRO_TL_GET_SERVER_ID_PORT;
                memcpy(client_addr, &CRO_TL_GET_SERVER_ID_ADDR(0), 4);
                XcpEthTlGetInfo(&server_isTCP, server_mac, server_addr, &server_port);
                memcpy(&CRM_TL_GET_SERVER_ID_ADDR(0), server_addr, 4);
                CRM_TL_GET_SERVER_ID_PORT = server_port;
                CRM_TL_GET_SERVER_ID_STATUS = (server_isTCP ? GET_SERVER_ID_STATUS_PROTOCOL_TCP : GET_SERVER_ID_STATUS_PROTOCOL_UDP) | // protocol type
                                              (isConnected() ? GET_SERVER_ID_STATUS_SLV_AVAILABILITY_BUSY : 0) |                       // In use
                                              0; // TL_SLV_DETECT_STATUS_SLV_ID_EXT_SUPPORTED; // GET_SERVER_ID_EXTENDED supported
                CRM_TL_GET_SERVER_ID_RESOURCE = RM_DAQ;
                CRM_TL_GET_SERVER_ID_ID_LEN = (uint8_t)ApplXcpGetId(IDT_ASCII, &CRM_TL_GET_SERVER_ID_ID, CRM_TL_GET_SERVER_ID_MAX_LEN);
                memcpy((uint8_t *)&CRM_TL_GET_SERVER_ID_MAC(CRM_TL_GET_SERVER_ID_ID_LEN), server_mac, 6);
                CRM_LEN = (uint8_t)CRM_TL_GET_SERVER_ID_LEN(CRM_TL_GET_SERVER_ID_ID_LEN);
                XcpSendMulticastResponse(&CRM, CRM_LEN, client_addr, client_port); // Transmit multicast command response
                goto no_response;
#endif // XCPTL_ENABLE_MULTICAST

            case 0:
            default: /* unknown transport layer command */
                error(CRC_CMD_UNKNOWN);
            }
            break;
#endif // >= 0x0103

        case CC_GET_DAQ_CLOCK: {
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
            CRM_GET_DAQ_CLOCK_RES1 = 0x00;         // Placeholder for event code
            CRM_GET_DAQ_CLOCK_TRIGGER_INFO = 0x18; // TIME_OF_SAMPLING (Bitmask 0x18, 3 - Sampled on reception)
            if (!isLegacyMode()) {                 // Extended format
#ifdef XCP_DAQ_CLOCK_64BIT
                CRM_LEN = CRM_GET_DAQ_CLOCK_LEN + 5;
                CRM_GET_DAQ_CLOCK_PAYLOAD_FMT = DAQ_CLOCK_PAYLOAD_FMT_SLV_64; // FMT_XCP_SLV = size of timestamp is DLONG
                uint64_t clock = ApplXcpGetClock64();
                CRM_GET_DAQ_CLOCK_TIME64_LOW = (uint32_t)(clock);
                CRM_GET_DAQ_CLOCK_TIME64_HIGH = (uint32_t)(clock >> 32);
                CRM_GET_DAQ_CLOCK_SYNCH_STATE64 = ApplXcpGetClockState();
#else
                CRM_LEN = CRM_GET_DAQ_CLOCK_LEN + 1;
                CRM_GET_DAQ_CLOCK_PAYLOAD_FMT = DAQ_CLOCK_PAYLOAD_FMT_SLV_32; // FMT_XCP_SLV = size of timestamp is DWORD
                CRM_GET_DAQ_CLOCK_TIME = (uint32_t)ApplXcpGetClock64();
                CRM_GET_DAQ_CLOCK_SYNCH_STATE = ApplXcpGetClockState();
#endif
                if (CRM_LEN > XCPTL_MAX_CTO_SIZE)
                    error(CRC_CMD_UNKNOWN); // Extended mode needs enough CTO size
            } else
#endif                                                                        // >= 0x0103
            {                                                                 // Legacy format
                CRM_GET_DAQ_CLOCK_PAYLOAD_FMT = DAQ_CLOCK_PAYLOAD_FMT_SLV_32; // FMT_XCP_SLV = size of timestamp is DWORD
                CRM_LEN = CRM_GET_DAQ_CLOCK_LEN;
                CRM_GET_DAQ_CLOCK_TIME = (uint32_t)ApplXcpGetClock64();
            }
        } break;

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
        case CC_LEVEL_1_COMMAND:
            switch (CRO_LEVEL_1_COMMAND_CODE) {

            /* Major and minor versions */
            case CC_GET_VERSION:
                CRM_LEN = CRM_GET_VERSION_LEN;
                CRM_GET_VERSION_RESERVED = 0;
                CRM_GET_VERSION_PROTOCOL_VERSION_MAJOR = (uint8_t)((uint16_t)XCP_PROTOCOL_LAYER_VERSION >> 8);
                CRM_GET_VERSION_PROTOCOL_VERSION_MINOR = (uint8_t)(XCP_PROTOCOL_LAYER_VERSION & 0xFF);
                CRM_GET_VERSION_TRANSPORT_VERSION_MAJOR = (uint8_t)((uint16_t)XCP_TRANSPORT_LAYER_VERSION >> 8);
                CRM_GET_VERSION_TRANSPORT_VERSION_MINOR = (uint8_t)(XCP_TRANSPORT_LAYER_VERSION & 0xFF);
                break;

            default: /* unknown command */
                error(CRC_CMD_UNKNOWN);
            }
            break;
#endif // >= 0x0104

        default: /* unknown command */
        {
            error(CRC_CMD_UNKNOWN)
        }

        } // switch()
    }

    // Transmit normal command response
    XcpSendResponse(async, &CRM, CRM_LEN);
    return CRC_CMD_OK;

// Transmit error response
negative_response:
    CRM_LEN = 2;
    CRM_CMD = PID_ERR;
    CRM_ERR = err;
    XcpSendResponse(async, &CRM, CRM_LEN);
    return err;

// Transmit busy response, if another command is already pending
// Interleaved mode is not supported
// @@@@ TODO: Find a better solution
#ifdef XCP_ENABLE_DYN_ADDRESSING
busy_response:
    CRM_LEN = 2;
    CRM_CMD = PID_ERR;
    CRM_ERR = CRC_CMD_BUSY;
    XcpSendResponse(async, &CRM, CRM_LEN);
    return CRC_CMD_BUSY;
#endif

// No response in these cases:
// - Transmit multicast command response
// - Command will be executed delayed, during execution of the associated synchronisation event
no_response:
    return CRC_CMD_OK;
}

/*****************************************************************************
| Non realtime critical background tasks
******************************************************************************/

// Let XCP handle non realtime critical background tasks
// This function must be called periodically when a XCP client is connected
// Must be called in regular intervals from the same thread that calls XcpCommand() !!
void XcpBackgroundTasks(void) {

    if (!isActivated()) { // Ignore
        return;
    }

    DBG_PRINT6("XcpBackgroundTasks\n");

// Publish all modified calibration segments
#ifdef XCP_ENABLE_CALSEG_LAZY_WRITE
    uint64_t now = clockGetMonotonicNsLast();
    static uint64_t last_success_time = 0;
    bool res = XcpCalSegPublishAll(false);
    if (res != CRC_CMD_OK && res != CRC_CMD_PENDING) {
        DBG_PRINT_WARNING("XcpBackgroundTasks: Calibration segment publish failed!\n");
    }
    if (res == CRC_CMD_OK) {
        // All segments published
        last_success_time = now;
    } else if (res == CRC_CMD_PENDING) {
        // Warn if delayed by more than 200ms
        if (now - last_success_time > CLOCK_TICKS_PER_MS * 200) {
            if (last_success_time != 0)
                DBG_PRINT_WARNING("XcpBackgroundTasks: Calibration segment publish delayed by more than 200ms!\n");
            last_success_time = now;
        }
    }

#endif
}

/*****************************************************************************
| Events
******************************************************************************/

void XcpSendEvent(uint8_t evc, const uint8_t *d, uint8_t l) {
    if (!isConnected())
        return;

    assert(l < XCPTL_MAX_CTO_SIZE - 2);
    if (l >= XCPTL_MAX_CTO_SIZE - 2)
        return;

    tQueueBuffer queue_buffer = queueAcquire(local.queue, l + 2);
    tXcpCto *crm = (tXcpCto *)queue_buffer.buffer;
    if (crm != NULL) {
        crm->b[0] = PID_EV; /* Event */
        crm->b[1] = evc;    /* Eventcode */
        if (d != NULL && l > 0) {
            for (uint8_t i = 0; i < l; i++)
                crm->b[i + 2] = d[i];
        }
        queuePush(local.queue, &queue_buffer, true);
    } else { // Queue overflow
        DBG_PRINT_WARNING("queue overflow\n");
    }
}

// Send terminate session signal event
void XcpSendTerminateSessionEvent(void) { XcpSendEvent(EVC_SESSION_TERMINATED, NULL, 0); }

/****************************************************************************/
/* Print via SERV/SERV_TEXT                                                 */
/****************************************************************************/

#if defined(XCP_ENABLE_SERV_TEXT)

void XcpPrint(const char *str) {
    if (!isConnected())
        return;

    uint16_t l = (uint16_t)STRNLEN(str, XCPTL_MAX_CTO_SIZE - 4);
    tQueueBuffer queue_buffer = queueAcquire(local.queue, l + 4);
    uint8_t *crm = queue_buffer.buffer;
    if (crm != NULL) {
        crm[0] = PID_SERV; /* Event */
        crm[1] = 0x01;     /* Eventcode SERV_TEXT */
        uint8_t i;
        for (i = 0; i < l && i < XCPTL_MAX_CTO_SIZE - 4; i++)
            crm[i + 2] = (uint8_t)str[i];
        crm[i + 2] = '\n';
        crm[i + 3] = 0;
        queuePush(local.queue, &queue_buffer, true);
    } else { // Queue overflow
        DBG_PRINT_WARNING("queue overflow\n");
    }
}

#endif // XCP_ENABLE_SERV_TEXT

/****************************************************************************/
/* Initialization and start of the XCP Protocol Layer                       */
/****************************************************************************/

// Init XCP protocol layer driver
// This is a once initialization of the gXcpData and gXcpLocalData singleton data structures
bool XcpInit(const char *name, const char *epk, uint8_t mode) {

    if (isActivated()) { // Already initialized, just ignore
        return true;
    }

    DBG_PRINTF3(ANSI_COLOR_GREEN "XcpInit name=%s, epk=%s, mode=%02X\n" ANSI_COLOR_RESET, name, epk, mode);
    DBG_PRINTF5("  sizeof(tXcpData)=%zu  sizeof(tXcpLocalData)=%zu\n", sizeof(tXcpData), sizeof(tXcpLocalData));

    // Mode checks and adjustments
    if (mode != XCP_MODE_DEACTIVATE) {
#ifdef OPTION_SHM_MODE // XcpInit adjust XCP mode
        // Adjust mode flags for SHM mode, if enabled, to ensure consistency of dependent flags
        mode |= XCP_MODE_SHM;         // SHM mode must be active, no fallback to normal XCP mode possible
        mode |= XCP_MODE_PERSISTENCE; // Be sure XCP_MODE_PERSISTENCE has been set
        if ((mode & XCP_MODE_LOCAL) != 0) {
            DBG_PRINT_WARNING("XcpInit: xcplib is in SHM mode, switch to XCP_MODE_SHM_AUTO\n");
            mode &= ~XCP_MODE_LOCAL;
            mode |= XCP_MODE_SHM_AUTO;
        }
#ifdef TEST_MUTABLE_ACCESS_OWNERSHIP
        XcpBindOwnerThread();
#endif
#else
        // Not compiled for SHM mode
        if ((mode & (XCP_MODE_SHM | XCP_MODE_SHM_AUTO | XCP_MODE_SHM_SERVER)) != 0) {
            DBG_PRINT_ERROR("XcpInit: SHM mode requested, but xcplib is compiled in non-SHM mode, switch to XCP_MODE_LOCAL\n");
        }
        mode |= XCP_MODE_LOCAL;
        mode &= ~(XCP_MODE_SHM | XCP_MODE_SHM_AUTO | XCP_MODE_SHM_SERVER);
#endif
    }

    // Clear local XCP state
    memset((uint8_t *)&gXcpLocalData, 0, sizeof(tXcpLocalData));
    local_mut.init_mode = mode;

// Initialize the base address for absolute addressing
#ifdef XCP_ENABLE_ABS_ADDRESSING
    ApplXcpGetBaseAddr();
    assert(xcp_get_base_addr() != NULL);
#endif

    // Initialize high resolution clock
    clockInit();

    // Set the project name (local XCP state) of this application
    if (name == NULL || STRNLEN(name, XCP_PROJECT_NAME_MAX_LENGTH) == 0) {
        assert(false && "Project name is mandatory");
        goto error_deactivate; // Project name is mandatory, deactivate XCP
    }
    XcpSetProjectName(name);

    // Set the EPK version string(local XCP state)  for this application, used for version checking and compatibility checks with the A2L file
    if (epk == NULL || STRNLEN(epk, XCP_EPK_MAX_LENGTH) == 0) {
        assert(false && "EPK version string is mandatory");
        goto error_deactivate; // EPK version string is mandatory, deactivate XCP
    }
    XcpSetEpk(epk);

    // Now, after minimum initialization deactivate
    if ((mode & (XCP_MODE_LOCAL | XCP_MODE_SHM)) == 0) {
        local_mut.init_mode = XCP_MODE_DEACTIVATE;
#ifdef OPTION_SHM_MODE
        gXcpData = NULL;
#else
        gXcpData.session_status = 0;
#endif
        return true;
    }

#ifdef OPTION_SHM_MODE // XcpInit init SHM mode
    // In SHM mode, attach to shared memory, or create it if not existing (leader)
    gXcpData = XcpShmAttachOrCreate(&local_mut.shm_leader);
    if (gXcpData == NULL) {
        DBG_PRINT_ERROR("XcpInit: Failed to attach to shared memory\n");
        goto error_deactivate; // SHM problem, deactivate XCP
    }

    // Shared memory already exists, register application and done
    if (!local_mut.shm_leader) {

        DBG_PRINT3(ANSI_COLOR_BLUE "Attached to '/xcpdata'\n" ANSI_COLOR_RESET);

        uint8_t server_id = XcpShmGetServer();
        // Server mode requested
        if ((mode & XCP_MODE_SHM_SERVER) != 0) { // Become server, if requested by mode flags
            // Error, if there already is a server
            if (server_id != SHM_INVALID_APP_ID) {
                DBG_PRINTF_ERROR("XcpInit: Server mode requested, but a server with app_id=%u already exists\n", server_id);
                goto error_deactivate;
            }
            local_mut.shm_server = true;
            DBG_PRINT3(ANSI_COLOR_YELLOW "Server mode requested, there already is a leader\n" ANSI_COLOR_RESET);
        }
        // Auto server mode, become server if there is none
        else if ((mode & XCP_MODE_SHM_AUTO) != 0) {
            if (server_id == SHM_INVALID_APP_ID) { // No server yet, become server
                DBG_PRINT3("Server mode activated (auto), there already is a leader, but but no server yet\n");
                local_mut.shm_server = true;
            } else {
                local_mut.shm_server = false;
            }
        }
        // Never be the server
        else {
            if (server_id != SHM_INVALID_APP_ID) {
                DBG_PRINT3(ANSI_COLOR_YELLOW "Application attached, no server yet\n" ANSI_COLOR_RESET);
            }
            local_mut.shm_server = false;
        }
        // Register
        int16_t app_id = XcpShmRegisterApp(name, epk, (uint32_t)getpid(), mode, local.shm_leader, local.shm_server);
        if (app_id < 0) {
            DBG_PRINT_ERROR("XcpInit: Failed to register application\n");
            goto error_deactivate; // SHM application registration problem, deactivate XCP
        }
        local_mut.shm_app_id = (uint8_t)app_id;

        // Early return for non SHM leaders here
        return true;
    } else {
        DBG_PRINT3(ANSI_COLOR_BLUE "Created '/xcpdata', initializing as leader\n" ANSI_COLOR_RESET);
    }
#else
    // Using static memory for tXcpData
    // Clear global XCP state
    memset((uint8_t *)&gXcpData, 0, sizeof(tXcpData));
#endif

#ifdef OPTION_SHM_MODE // XcpInit init SHM mode specific data and state

    // In SHM mode, only the leader reaches this point
    // Initialize shared memory, followers already returned early after attaching and registering
    // Note that SHM mode must not be activated, header will always be initialized
    assert(local.shm_leader);

    assert(gXcpData != NULL);
    tShmHeader *hdr = &gXcpData->shm_header;
    hdr->magic = SHM_MAGIC;
    hdr->version = SHM_VERSION;
    hdr->size = (uint32_t)sizeof(tXcpData);
    hdr->leader_pid = (uint32_t)getpid();
    atomic_store(&hdr->app_count, 0U);
    atomic_store(&hdr->a2l_finalize_requested, 0U);
    memset(hdr->ecu_epk, 0, sizeof(hdr->ecu_epk));

    local_mut.shm_app_id = 0; // Not defined yet, leader will registered after loading the binary persistence file

    // Check, if the leader should also be the server, or if a separate server application will be started later
    local_mut.shm_server = (mode & XCP_MODE_SHM) != 0 && (mode & (XCP_MODE_SHM_SERVER | XCP_MODE_SHM_AUTO)) != 0;

#endif

    // Reset DAQ list memory
    XcpClearDaq();

#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
    local_mut.cluster_id = XCP_MULTICAST_CLUSTER_ID; // XCP default cluster id (multicast addr 239,255,0,1, group 127,0,1 (mac 01-00-5E-7F-00-01)
    XcpEthTlSetClusterId(local.cluster_id);
#endif

#ifdef XCP_ENABLE_CALSEG_LIST
    XcpInitCalSegList();
#endif

#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    XcpInitEventList();
#endif

    // Activate XCP protocol layer, initialization done
    shared_mut.session_status |= SS_ACTIVATED;

// Check if the binary persistence file exists and load all segments, events
// In SHM mode also load applications
#ifdef OPTION_ENABLE_PERSISTENCE
    if ((mode & XCP_MODE_PERSISTENCE) != 0) {
        if (XcpBinLoad()) {
            // @@@@ TODO: Maybe remember this
            // shared_mut.bin_loaded = true;
        }
    }
#endif

// In SHM mode, the leader registered itself, after loading the binary persistence file
#ifdef OPTION_SHM_MODE // XcpInit register application
    assert(mode & XCP_MODE_PERSISTENCE);
    DBG_PRINTF3("XcpInit: Registering application '%s', epk=%s, mode=%02X\n", name, epk, mode);
    int16_t app_id = XcpShmRegisterApp(name, epk, (uint32_t)getpid(), mode, local_mut.shm_leader, local_mut.shm_server);
    if (app_id < 0) {
        DBG_PRINT_ERROR("XcpInit: failed to register application\n");
        goto error_deactivate; // SHM application registration problem, deactivate XCP
    }
    local_mut.shm_app_id = (uint8_t)app_id;
#endif

#ifdef XCP_ENABLE_CALSEG_LIST
#ifdef XCP_ENABLE_EPK_CALSEG
    // Create the EPK calibration segment with index 0
    // In SHM multiapplication mode, only the leader reaches this point, and creates a EPK segment for the whole system
    // @@@@ TODO: Currently the EPK segment is treated like any other segment, even if it is read-only and should only expose the default page
    static tXcpCalSegIndex cal__epk = XCP_UNDEFINED_CALSEG; // Create the linker file marker for the EPK segment
    cal__epk = XcpCreateCalSeg(XCP_EPK_CALSEG_NAME, XcpGetEcuEpk(), XCP_EPK_MAX_LENGTH + 1);
    (void)cal__epk; // Avoid unused variable warning
    assert(cal__epk == 0);
#endif
#endif

#ifdef OPTION_SHM_MODE // XcpInit print inital SHM state
    if (DBG_LEVEL >= 3) {
        DBG_PRINT3(ANSI_COLOR_GREEN "XCP shared memory initialized by leader\n" ANSI_COLOR_RESET);
        XcpShmDebugPrint();
    }
#endif

    return true;

error_deactivate:
    // Deactivate XCP, to keep the application in a safe state
    // Don't it is the users responsibility to check succes of XcpInit
    DBG_PRINT_WARNING("XcpInit: Deactivate XCP\n");
    local_mut.init_mode = XCP_MODE_DEACTIVATE;
#ifdef OPTION_SHM_MODE // XcpInit deactivate XCP on error
    gXcpData = NULL;
#else
    gXcpData.session_status = 0;
#endif
    return false;
}

// Start XCP protocol layer
// Assume the transport layer is running
void XcpStart(tQueueHandle queue_handle, bool resumeMode) {

    (void)resumeMode; // Start in resume mode, not implemented yet

    if (!isActivated())
        return;

#ifdef TEST_MUTABLE_ACCESS_OWNERSHIP
    XcpBindOwnerThread();
#endif

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 3) {
        DBG_PRINT3("Init XCP protocol layer\n");
        DBG_PRINTF3("  Version=%u.%u, MAX_CTO=%u, MAX_DTO=%u, DAQ_MEM=%u, MAX_DAQ=%u, MAX_ODT_ENTRY=%u, MAX_ODT_ENTRYSIZE=%u, %u KiB memory used\n",
                    XCP_PROTOCOL_LAYER_VERSION >> 8, XCP_PROTOCOL_LAYER_VERSION & 0xFF, XCPTL_MAX_CTO_SIZE, XCPTL_MAX_DTO_SIZE, XCP_DAQ_MEM_SIZE, (1 << sizeof(uint16_t) * 8) - 1,
                    (1 << sizeof(uint16_t) * 8) - 1, (1 << (sizeof(uint8_t) * 8)) - 1, (unsigned int)sizeof(tXcpData) / 1024);

        DBG_PRINT3("  Addressing scheme=" XCP_ADDRESS_MODE "\n");
        DBG_PRINT3("  Options=(");

        // Print activated XCP protocol options
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST // Enable GET_DAQ_CLOCK_MULTICAST
        DBG_PRINT("DAQ_CLK_MULTICAST (not recomended),");
#endif
#ifdef XCP_DAQ_CLOCK_64BIT // Use 64 Bit time stamps
        DBG_PRINT("DAQ_CLK_64BIT,");
#endif
#ifdef XCP_ENABLE_PTP // Enable server clock synchronized to PTP grandmaster clock
        DBG_PRINT("GM_CLK_INFO,");
#endif
#ifdef XCP_ENABLE_DAQ_ADDREXT // Enable DAQ with individual address extension per entry
        DBG_PRINT("DAQ_ADDREXT,");
#endif
#ifdef XCP_ENABLE_DAQ_PRESCALER // Enable DAQ prescaler
        DBG_PRINT("DAQ_PRESCALER,");
#endif
#ifdef XCP_ENABLE_DAQ_CONTROL // Enable DAQ control commands
        DBG_PRINT("DAQ_CONTROL,");
#endif
#ifdef XCP_ENABLE_DAQ_EVENT_LIST // Enable XCP event registration and optimization
        DBG_PRINT("DAQ_EVENT_LIST,");
#endif
#ifdef XCP_ENABLE_DAQ_RESUME // Enable DAQ resume mode
        DBG_PRINT("DAQ_RESUME,");
#endif
#ifdef XCP_ENABLE_DAQ_EVENT_INFO // Enable XCP event info by protocol instead of A2L
        DBG_PRINT("DAQ_EVT_INFO,");
#endif
#ifdef XCP_ENABLE_CALSEG_LIST // Enable XCP calibration segments
        DBG_PRINT("CALSEG_LIST,");
#endif
#ifdef XCP_ENABLE_EPK_CALSEG // Enable EPK calibration segment
        DBG_PRINT("EPK_CALSEG,");
#endif
#ifdef XCP_ENABLE_COPY_CAL_PAGE
        DBG_PRINT("COPY_CAL_PAGE,");
#endif
#ifdef XCP_ENABLE_IDT_A2L_UPLOAD // Enable A2L upload to host
        DBG_PRINT("A2L_UPLOAD,");
#endif
#ifdef XCP_ENABLE_IDT_ELF_UPLOAD // Enable ELF upload to host
        DBG_PRINT("ELF_UPLOAD,");
#endif
#ifdef XCP_ENABLE_IDT_A2L_HTTP_GET // Enable A2L upload to hostRust
        DBG_PRINT("A2L_URL,");
#endif
#ifdef XCP_ENABLE_FREEZE_CAL_PAGE // Enable freeze calibration page
        DBG_PRINT("FREEZE_CAL_PAGE,");
#endif
#ifdef XCP_ENABLE_CHECKSUM // Enable BUILD_CHECKSUM command
        DBG_PRINT("CHECKSUM,");
#endif
        DBG_PRINT(")\n");
    }
#endif // DBG_LEVEL

    local_mut.queue = queue_handle;

#ifdef XCP_ENABLE_PROTOCOL_LAYER_ETH
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103

    // XCP server clock default description
    local_mut.clock_info.server.timestampTicks = XCP_TIMESTAMP_TICKS;
    local_mut.clock_info.server.timestampUnit = XCP_TIMESTAMP_UNIT;
    local_mut.clock_info.server.stratumLevel = XCP_STRATUM_LEVEL_UNKNOWN;
#ifdef XCP_DAQ_CLOCK_64BIT
    local_mut.clock_info.server.nativeTimestampSize = 8; // NATIVE_TIMESTAMP_SIZE_DLONG;
    local_mut.clock_info.server.valueBeforeWrapAround = 0xFFFFFFFFFFFFFFFFULL;
#else
    local_mut.clock_info.server.nativeTimestampSize = 4; // NATIVE_TIMESTAMP_SIZE_LONG;
    local_mut.clock_info.server.valueBeforeWrapAround = 0xFFFFFFFFULL;
#endif
#endif // XCP_PROTOCOL_LAYER_VERSION >= 0x0103
#ifdef XCP_ENABLE_PTP

    // Default UUID of the XCP server clock
    uint8_t uuid[8] = XCP_DAQ_CLOCK_UUID;
    memcpy(local_mut.clock_info.server.UUID, uuid, 8);

    // If the server clock is PTP synchronized, both origin and local timestamps are considered to be the same.
    local_mut.clock_info.relation.timestampLocal = 0;
    local_mut.clock_info.relation.timestampOrigin = 0;

    // XCP grandmaster clock default description
    local_mut.clock_info.grandmaster.timestampTicks = XCP_TIMESTAMP_TICKS;
    local_mut.clock_info.grandmaster.timestampUnit = XCP_TIMESTAMP_UNIT;
    local_mut.clock_info.grandmaster.nativeTimestampSize = 8; // NATIVE_TIMESTAMP_SIZE_DLONG;
    local_mut.clock_info.grandmaster.valueBeforeWrapAround = 0xFFFFFFFFFFFFFFFFULL;
    local_mut.clock_info.grandmaster.stratumLevel = XCP_STRATUM_LEVEL_UNKNOWN;
    local_mut.clock_info.grandmaster.epochOfGrandmaster = XCP_EPOCH_ARB;
    if (ApplXcpGetClockInfoGrandmaster(local_mut.clock_info.server.UUID, local_mut.clock_info.grandmaster.UUID, &local_mut.clock_info.grandmaster.epochOfGrandmaster,
                                       &local_mut.clock_info.grandmaster.stratumLevel)) {
        DBG_PRINTF5("  GrandmasterClock: UUID=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X stratumLevel=%u, epoch=%u\n", local.clock_info.grandmaster.UUID[0],
                    local.clock_info.grandmaster.UUID[1], local.clock_info.grandmaster.UUID[2], local.clock_info.grandmaster.UUID[3], local.clock_info.grandmaster.UUID[4],
                    local.clock_info.grandmaster.UUID[5], local.clock_info.grandmaster.UUID[6], local.clock_info.grandmaster.UUID[7], local.clock_info.grandmaster.stratumLevel,
                    local.clock_info.grandmaster.epochOfGrandmaster);
        DBG_PRINT5("  ClockRelation: local=0, origin=0\n");
    }

    DBG_PRINTF4("  ClientClock: ticks=%u, unit=%s, size=%u, UUID=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n", local.clock_info.server.timestampTicks,
                (local.clock_info.server.timestampUnit == DAQ_TIMESTAMP_UNIT_1NS) ? "ns" : "us", local.clock_info.server.nativeTimestampSize, local.clock_info.server.UUID[0],
                local.clock_info.server.UUID[1], local.clock_info.server.UUID[2], local.clock_info.server.UUID[3], local.clock_info.server.UUID[4], local.clock_info.server.UUID[5],
                local.clock_info.server.UUID[6], local.clock_info.server.UUID[7]);

#endif // PTP
#endif // XCP_ENABLE_PROTOCOL_LAYER_ETH

    DBG_PRINT3("Start XCP protocol layer\n");

    shared_mut.session_status |= SS_STARTED;

    // Resume DAQ
#ifdef XCP_ENABLE_DAQ_RESUME
    if (resumeMode) {
        if (XcpCheckPreparedDaqLists()) {
            /* Goto temporary disconnected mode and start all selected DAQ lists */
            shared_mut.session_status |= SS_RESUME;
            /* Start DAQ */
            XcpStartSelectedDaqLists();
            // @@@@ TODO: Send an event message to indicate resume mode

#ifdef DBG_LEVEL
            if (DBG_LEVEL != 0) {
                printf("Started in resume mode\n");
                for (uint16_t daq = 0; daq < shared.daq_lists.daq_count; daq++) {
                    if (DaqListState(daq) & DAQ_STATE_SELECTED) {
                        XcpPrintDaqList(daq);
                    }
                }
            }
#endif
        }
    }
#endif /* XCP_ENABLE_DAQ_RESUME */
}

// Reset XCP protocol layer back to not init state, free resources,server is going down
void XcpDeinit(void) {

    if (!isActivated()) { // Ignore
        return;
    }

#ifdef TEST_MUTABLE_ACCESS_OWNERSHIP
    XcpBindOwnerThread();
#endif

    // Deinit calibration list (destroy the local mutex)
#ifdef XCP_ENABLE_CALSEG_LIST
    XcpDeinitCalSegList();
#endif

#ifdef OPTION_SHM_MODE // XcpDeinit SHM mode specific deinitialization
    XcpShmShutdownApp(local_mut.shm_app_id);
#else
    // Reset shared XCP state to not activated
    shared_mut.session_status = 0;
#endif
}

/****************************************************************************/
/* Test printing                                                            */
/****************************************************************************/

#ifdef DBG_LEVEL

static void XcpPrintCmd(const tXcpCto *cmdBuf) {

#undef CRO_LEN
#undef CRO
#undef CRO_BYTE
#undef CRO_WORD
#undef CRO_DWORD
#define CRO_BYTE(x) (cmdBuf->b[x])
#define CRO_WORD(x) (cmdBuf->w[x])
#define CRO_DWORD(x) (cmdBuf->dw[x])

    shared_mut.cmd_last = CRO_CMD;
    shared_mut.cmd_last1 = CRO_LEVEL_1_COMMAND_CODE;
    switch (CRO_CMD) {

    case CC_SET_CAL_PAGE:
        printf(" SET_CAL_PAGE segment=%u,page=%u,mode=%02Xh %s\n", CRO_SET_CAL_PAGE_SEGMENT, CRO_SET_CAL_PAGE_PAGE, CRO_SET_CAL_PAGE_MODE,
               (CRO_SET_CAL_PAGE_MODE & CAL_PAGE_MODE_ALL) ? "(all)" : "");
        break;
    case CC_GET_CAL_PAGE:
        printf(" GET_CAL_PAGE segment=%u, mode=%u\n", CRO_GET_CAL_PAGE_SEGMENT, CRO_GET_CAL_PAGE_MODE);
        break;
    case CC_COPY_CAL_PAGE:
        printf(" COPY_CAL_PAGE srcSegment=%u, srcPage=%u, dstSegment=%u, dstPage=%u\n", CRO_COPY_CAL_PAGE_SRC_SEGMENT, CRO_COPY_CAL_PAGE_SRC_PAGE, CRO_COPY_CAL_PAGE_DEST_SEGMENT,
               CRO_COPY_CAL_PAGE_DEST_PAGE);
        break;
    case CC_GET_PAG_PROCESSOR_INFO:
        printf(" GET_PAG_PROCESSOR_INFO\n");
        break;
    case CC_SET_SEGMENT_MODE:
        printf(" SET_SEGMENT_MODE segment=%u, mode=%u\n", CRO_SET_SEGMENT_MODE_SEGMENT, CRO_SET_SEGMENT_MODE_MODE);
        break;
    case CC_GET_SEGMENT_MODE:
        printf(" GET_SEGMENT_MODE segment=%u\n", CRO_GET_SEGMENT_MODE_SEGMENT);
        break;
    case CC_GET_SEGMENT_INFO:
        printf(" GET_SEGMENT_INFO segment=%u, mode=%u, info=%u, mapIndex=%u\n", CRO_GET_SEGMENT_INFO_SEGMENT_NUMBER, CRO_GET_SEGMENT_INFO_MODE, CRO_GET_SEGMENT_INFO_SEGMENT_INFO,
               CRO_GET_SEGMENT_INFO_MAPPING_INDEX);
        break;
    case CC_GET_PAGE_INFO:
        printf(" GET_PAGE_INFO segment=%u, page=%u\n", CRO_GET_PAGE_INFO_SEGMENT_NUMBER, CRO_GET_PAGE_INFO_PAGE_NUMBER);
        break;
    case CC_BUILD_CHECKSUM:
        printf(" BUILD_CHECKSUM size=%u\n", CRO_BUILD_CHECKSUM_SIZE);
        break;
    case CC_SET_MTA:
        printf(" SET_MTA addr=%08Xh, addrext=%02Xh\n", CRO_SET_MTA_ADDR, CRO_SET_MTA_EXT);
        break;
    case CC_SYNCH:
        printf(" SYNCH\n");
        break;
    case CC_GET_COMM_MODE_INFO:
        printf(" GET_COMM_MODE_INFO\n");
        break;
    case CC_DISCONNECT:
        printf(" DISCONNECT\n");
        break;
    case CC_GET_ID:
        printf(" GET_ID type=%u\n", CRO_GET_ID_TYPE);
        break;
    case CC_GET_STATUS:
        printf(" GET_STATUS\n");
        break;
    case CC_GET_DAQ_PROCESSOR_INFO:
        printf(" GET_DAQ_PROCESSOR_INFO\n");
        break;
    case CC_GET_DAQ_RESOLUTION_INFO:
        printf(" GET_DAQ_RESOLUTION_INFO\n");
        break;
    case CC_GET_DAQ_EVENT_INFO:
        printf(" GET_DAQ_EVENT_INFO event=%u (%s)\n", CRO_GET_DAQ_EVENT_INFO_EVENT, XcpGetEventName(CRO_GET_DAQ_EVENT_INFO_EVENT));
        break;
    case CC_FREE_DAQ:
        printf(" FREE_DAQ\n");
        break;
    case CC_ALLOC_DAQ:
        printf(" ALLOC_DAQ count=%u\n", CRO_ALLOC_DAQ_COUNT);
        break;
    case CC_ALLOC_ODT:
        printf(" ALLOC_ODT daq=%u, count=%u\n", CRO_ALLOC_ODT_DAQ, CRO_ALLOC_ODT_COUNT);
        break;
    case CC_ALLOC_ODT_ENTRY:
        printf(" ALLOC_ODT_ENTRY daq=%u, odt=%u, count=%u\n", CRO_ALLOC_ODT_ENTRY_DAQ, CRO_ALLOC_ODT_ENTRY_ODT, CRO_ALLOC_ODT_ENTRY_COUNT);
        break;
    case CC_GET_DAQ_LIST_MODE:
        printf(" GET_DAQ_LIST_MODE daq=%u\n", CRO_GET_DAQ_LIST_MODE_DAQ);
        break;
    case CC_SET_DAQ_LIST_MODE:
        printf(" SET_DAQ_LIST_MODE daq=%u, mode=%02Xh, event=%u (%s)\n", CRO_SET_DAQ_LIST_MODE_DAQ, CRO_SET_DAQ_LIST_MODE_MODE, CRO_SET_DAQ_LIST_MODE_EVENTCHANNEL,
               XcpGetEventName(CRO_SET_DAQ_LIST_MODE_EVENTCHANNEL));
        break;
    case CC_SET_DAQ_PTR:
        printf(" SET_DAQ_PTR daq=%u,odt=%u,idx=%u\n", CRO_SET_DAQ_PTR_DAQ, CRO_SET_DAQ_PTR_ODT, CRO_SET_DAQ_PTR_IDX);
        break;
    case CC_WRITE_DAQ:
        printf(" WRITE_DAQ size=%u,addr=%08Xh,%02Xh\n", CRO_WRITE_DAQ_SIZE, CRO_WRITE_DAQ_ADDR, CRO_WRITE_DAQ_EXT);
        break;
    case CC_START_STOP_DAQ_LIST:
        printf(" START_STOP mode=%s, daq=%u\n",
               (CRO_START_STOP_DAQ_LIST_MODE == 2)   ? "select"
               : (CRO_START_STOP_DAQ_LIST_MODE == 1) ? "start"
                                                     : "stop",
               CRO_START_STOP_DAQ_LIST_DAQ);
        break;
    case CC_START_STOP_SYNCH:
        printf(" START_STOP_SYNCH mode=%s\n", (CRO_START_STOP_SYNCH_MODE == 3)   ? "prepare"
                                              : (CRO_START_STOP_SYNCH_MODE == 2) ? "stop_selected"
                                              : (CRO_START_STOP_SYNCH_MODE == 1) ? "start_selected"
                                                                                 : "stop_all");
        break;
    case CC_GET_DAQ_CLOCK:
        printf(" GET_DAQ_CLOCK\n");
        break;

    case CC_USER_CMD:
        printf(" USER_CMD SUB_COMMAND=%02X\n", CRO_USER_CMD_SUBCOMMAND);
        break;

    case CC_DOWNLOAD: {
        uint16_t i;
        printf(" DOWNLOAD size=%u, data=", CRO_DOWNLOAD_SIZE);
        for (i = 0; (i < CRO_DOWNLOAD_SIZE) && (i < CRO_DOWNLOAD_MAX_SIZE); i++) {
            printf("%02X ", CRO_DOWNLOAD_DATA[i]);
        }
        printf("\n");
    } break;

    case CC_SHORT_DOWNLOAD: {
        uint16_t i;
        printf(" SHORT_DOWNLOAD addr=%08Xh, addrext=%02Xh, size=%u, data=", CRO_SHORT_DOWNLOAD_ADDR, CRO_SHORT_DOWNLOAD_EXT, CRO_SHORT_DOWNLOAD_SIZE);
        for (i = 0; (i < CRO_SHORT_DOWNLOAD_SIZE) && (i < CRO_SHORT_DOWNLOAD_MAX_SIZE); i++) {
            printf("%02X ", CRO_SHORT_DOWNLOAD_DATA[i]);
        }
        printf("\n");
    } break;

    case CC_UPLOAD: {
        printf(" UPLOAD size=%u\n", CRO_UPLOAD_SIZE);
    } break;

    case CC_SHORT_UPLOAD: {
        printf(" SHORT_UPLOAD addr=%08Xh, addrext=%02Xh, size=%u\n", CRO_SHORT_UPLOAD_ADDR, CRO_SHORT_UPLOAD_EXT, CRO_SHORT_UPLOAD_SIZE);
    } break;

    case CC_WRITE_DAQ_MULTIPLE: {
        printf(" WRITE_DAQ_MULTIPLE count=%u\n", CRO_WRITE_DAQ_MULTIPLE_NODAQ);
        for (int i = 0; i < CRO_WRITE_DAQ_MULTIPLE_NODAQ; i++) {
            printf("   %u: size=%u,addr=%08Xh,%02Xh\n", i, CRO_WRITE_DAQ_MULTIPLE_SIZE(i), CRO_WRITE_DAQ_MULTIPLE_ADDR(i), CRO_WRITE_DAQ_MULTIPLE_EXT(i));
        }
    } break;

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
    case CC_TIME_CORRELATION_PROPERTIES:
        printf(" GET_TIME_CORRELATION_PROPERTIES set=%02Xh, request=%u, clusterId=%u\n", CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES, CRO_TIME_SYNCH_PROPERTIES_GET_PROPERTIES_REQUEST,
               CRO_TIME_SYNCH_PROPERTIES_CLUSTER_ID);
        break;
#endif

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104

    case CC_LEVEL_1_COMMAND:
        switch (CRO_LEVEL_1_COMMAND_CODE) {
        case CC_GET_VERSION:
            printf(" GET_VERSION\n");
            break;

        default:
            printf(" UNKNOWN LEVEL 1 COMMAND %02X\n", CRO_LEVEL_1_COMMAND_CODE);
            break;
        } // switch (CRO_LEVEL_1_COMMAND_CODE)
        break;

#endif // >= 0x0104

    case CC_TRANSPORT_LAYER_CMD:
        switch (CRO_TL_SUBCOMMAND) {
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
        case CC_TL_GET_DAQ_CLOCK_MULTICAST: {
            printf(" GET_DAQ_CLOCK_MULTICAST counter=%u, cluster=%u\n", CRO_GET_DAQ_CLOCK_MCAST_COUNTER, CRO_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER);
        } break;
        case CC_TL_GET_SERVER_ID_EXTENDED:
        case CC_TL_GET_SERVER_ID:
            printf(" GET_SERVER_ID %u:%u:%u:%u:%u\n", CRO_TL_GET_SERVER_ID_ADDR(0), CRO_TL_GET_SERVER_ID_ADDR(1), CRO_TL_GET_SERVER_ID_ADDR(2), CRO_TL_GET_SERVER_ID_ADDR(3),
                   CRO_TL_GET_SERVER_ID_PORT);
            break;
#endif // XCP_ENABLE_DAQ_CLOCK_MULTICAST
        default:
            printf(" UNKNOWN TRANSPORT LAYER COMMAND %02X\n", CRO_TL_SUBCOMMAND);
            break;
        } // switch (CRO_TL_SUBCOMMAND)

    case CC_NOP:
        printf(" NOP\n");
        break;

    default:
        printf(" UNKNOWN COMMAND 0x%02X\n", CRO_CMD);
    } // switch (CRO_CMD)
}

static void XcpPrintRes(const tXcpCto *crm) {

#undef CRM_LEN
#undef CRM_BYTE
#undef CRM_WORD
#undef CRM_DWORD
#define CRM_LEN (crmLen)
#define CRM_BYTE(x) (crm->b[x])
#define CRM_WORD(x) (crm->w[x])
#define CRM_DWORD(x) (crm->dw[x])

    if (CRM_CMD == PID_ERR) {
        const char *e;
        switch (CRM_ERR) {
        case CRC_CMD_SYNCH:
            e = "CRC_CMD_SYNCH";
            break;
        case CRC_CMD_BUSY:
            e = "CRC_CMD_BUSY";
            break;
        case CRC_DAQ_ACTIVE:
            e = "CRC_DAQ_ACTIVE";
            break;
        case CRC_PGM_ACTIVE:
            e = "CRC_PGM_ACTIVE";
            break;
        case CRC_CMD_IGNORED:
            e = "CRC_CMD_IGNORED";
            break;
        case CRC_CMD_UNKNOWN:
            e = "CRC_CMD_UNKNOWN";
            break;
        case CRC_CMD_SYNTAX:
            e = "CRC_CMD_SYNTAX";
            break;
        case CRC_OUT_OF_RANGE:
            e = "CRC_OUT_OF_RANGE";
            break;
        case CRC_WRITE_PROTECTED:
            e = "CRC_WRITE_PROTECTED";
            break;
        case CRC_ACCESS_DENIED:
            e = "CRC_ACCESS_DENIED";
            break;
        case CRC_ACCESS_LOCKED:
            e = "CRC_ACCESS_LOCKED";
            break;
        case CRC_PAGE_NOT_VALID:
            e = "CRC_PAGE_NOT_VALID";
            break;
        case CRC_MODE_NOT_VALID:
            e = "CRC_MODE_NOT_VALID";
            break;
        case CRC_SEGMENT_NOT_VALID:
            e = "CRC_SEGMENT_NOT_VALID";
            break;
        case CRC_SEQUENCE:
            e = "CRC_SEQUENCE";
            break;
        case CRC_DAQ_CONFIG:
            e = "CRC_DAQ_CONFIG";
            break;
        case CRC_MEMORY_OVERFLOW:
            e = "CRC_MEMORY_OVERFLOW";
            break;
        case CRC_GENERIC:
            e = "CRC_GENERIC";
            break;
        case CRC_VERIFY:
            e = "CRC_VERIFY";
            break;
        case CRC_RESOURCE_TEMPORARY_NOT_ACCESSIBLE:
            e = "CRC_RESOURCE_TEMPORARY_NOT_ACCESSIBLE";
            break;
        case CRC_SUBCMD_UNKNOWN:
            e = "CRC_SUBCMD_UNKNOWN";
            break;
        case CRC_TIMECORR_STATE_CHANGE:
            e = "CRC_TIMECORR_STATE_CHANGE";
            break;
        default:
            e = "Unknown errorcode";
        }
        printf(" <- ERROR: %02Xh - %s\n", CRM_ERR, e);
    } else {
        switch (shared.cmd_last) {

        case CC_CONNECT:
            printf(" <- version=%02Xh/%02Xh, maxcro=%u, maxdto=%u, resource=%02X, mode=%u\n", CRM_CONNECT_PROTOCOL_VERSION, CRM_CONNECT_TRANSPORT_VERSION, CRM_CONNECT_MAX_CTO_SIZE,
                   CRM_CONNECT_MAX_DTO_SIZE, CRM_CONNECT_RESOURCE, CRM_CONNECT_COMM_BASIC);
            break;

        case CC_GET_COMM_MODE_INFO:
            printf(" <- version=%02Xh, opt=%u, queue=%u, max_bs=%u, min_st=%u\n", CRM_GET_COMM_MODE_INFO_DRIVER_VERSION, CRM_GET_COMM_MODE_INFO_COMM_OPTIONAL,
                   CRM_GET_COMM_MODE_INFO_QUEUE_SIZE, CRM_GET_COMM_MODE_INFO_MAX_BS, CRM_GET_COMM_MODE_INFO_MIN_ST);
            break;

        case CC_GET_STATUS:
            printf(" <- sessionstatus=%02Xh, protectionstatus=%02Xh\n", CRM_GET_STATUS_STATUS, CRM_GET_STATUS_PROTECTION);
            break;

        case CC_GET_ID:
            printf(" <- mode=%u,len=%u\n", CRM_GET_ID_MODE, CRM_GET_ID_LENGTH);
            break;

#ifdef XCP_ENABLE_CAL_PAGE
        case CC_GET_CAL_PAGE:
            printf(" <- page=%u\n", CRM_GET_CAL_PAGE_PAGE);
            break;
#endif

#ifdef XCP_ENABLE_CHECKSUM
        case CC_BUILD_CHECKSUM:
            printf(" <- sum=%08Xh\n", CRM_BUILD_CHECKSUM_RESULT);
            break;
#endif

        case CC_GET_PAG_PROCESSOR_INFO:
            printf(" <- segments=%u, properties=%02Xh\n", CRM_GET_PAG_PROCESSOR_INFO_MAX_SEGMENTS, CRM_GET_PAG_PROCESSOR_INFO_PROPERTIES);
            break;

        case CC_GET_DAQ_RESOLUTION_INFO:
            printf(" <- mode=%02Xh, ticks=%02Xh\n", CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE, CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS);
            break;

        case CC_GET_DAQ_PROCESSOR_INFO:
            printf(" <- min=%u, max=%u, events=%u, keybyte=%02Xh, properties=%02Xh\n", CRM_GET_DAQ_PROCESSOR_INFO_MIN_DAQ, CRM_GET_DAQ_PROCESSOR_INFO_MAX_DAQ,
                   CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT, CRM_GET_DAQ_PROCESSOR_INFO_DAQ_KEY_BYTE, CRM_GET_DAQ_PROCESSOR_INFO_PROPERTIES);
            break;

        case CC_GET_DAQ_EVENT_INFO:
            printf(" <- 0xFF properties=%02Xh, unit=%u, cycle=%u\n", CRM_GET_DAQ_EVENT_INFO_PROPERTIES, CRM_GET_DAQ_EVENT_INFO_TIME_UNIT, CRM_GET_DAQ_EVENT_INFO_TIME_CYCLE);
            break;

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103
        case CC_GET_DAQ_CLOCK: {
            if (isLegacyMode()) {
                printf(" <- L t=0x%" PRIx32 "\n", CRM_GET_DAQ_CLOCK_TIME);
            } else {
                if (CRM_GET_DAQ_CLOCK_PAYLOAD_FMT == DAQ_CLOCK_PAYLOAD_FMT_SLV_32) {
                    printf(" <- X32 t=0x%" PRIx32 " sync=%u\n", CRM_GET_DAQ_CLOCK_TIME, CRM_GET_DAQ_CLOCK_SYNCH_STATE);
                } else {
                    char ts[64];
                    uint64_t t = (((uint64_t)CRM_GET_DAQ_CLOCK_TIME64_HIGH) << 32) | CRM_GET_DAQ_CLOCK_TIME64_LOW;
                    clockGetString(ts, sizeof(ts), t);
                    printf(" <- X64 t=%" PRIu64 " (%s), sync=%u\n", t & 0xFFFFFFFF, ts, CRM_GET_DAQ_CLOCK_SYNCH_STATE64);
                }
            }
        } break;

        case CC_TIME_CORRELATION_PROPERTIES:
            printf(" <- config=%02Xh, clocks=%02Xh, state=%02Xh, info=%02Xh, clusterId=%u\n", CRM_TIME_SYNCH_PROPERTIES_SERVER_CONFIG, CRM_TIME_SYNCH_PROPERTIES_OBSERVABLE_CLOCKS,
                   CRM_TIME_SYNCH_PROPERTIES_SYNCH_STATE, CRM_TIME_SYNCH_PROPERTIES_CLOCK_INFO, CRM_TIME_SYNCH_PROPERTIES_CLUSTER_ID);
            break;
#endif // >= 0x0103

#if XCP_PROTOCOL_LAYER_VERSION >= 0x0104
        case CC_LEVEL_1_COMMAND:
            switch (shared.cmd_last1) {
            case CC_GET_VERSION:
                printf(" <- protocol layer version: major=%02Xh/minor=%02Xh, transport layer version: major=%02Xh/minor=%02Xh\n", CRM_GET_VERSION_PROTOCOL_VERSION_MAJOR,
                       CRM_GET_VERSION_PROTOCOL_VERSION_MINOR, CRM_GET_VERSION_TRANSPORT_VERSION_MAJOR, CRM_GET_VERSION_TRANSPORT_VERSION_MINOR);
                break;
            default:
                break;
            }

            break;
#endif

        case CC_TRANSPORT_LAYER_CMD:
            switch (shared.cmd_last1) {
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
            case CC_TL_GET_DAQ_CLOCK_MULTICAST: {
                if (isLegacyMode()) {
                    printf(" <- L t=0x%" PRIx32 "\n", CRM_GET_DAQ_CLOCK_MCAST_TIME);
                } else {
                    if ((CRM_GET_DAQ_CLOCK_MCAST_PAYLOAD_FMT & ~DAQ_CLOCK_PAYLOAD_FMT_ID) == DAQ_CLOCK_PAYLOAD_FMT_SLV_32) {
                        printf(" <- X t=0x%" PRIx32 " sync=%u", CRM_GET_DAQ_CLOCK_MCAST_TIME, CRM_GET_DAQ_CLOCK_MCAST_SYNCH_STATE);
                        if (CRM_GET_DAQ_CLOCK_MCAST_PAYLOAD_FMT & DAQ_CLOCK_PAYLOAD_FMT_ID)
                            printf(" counter=%u, cluster=%u", CRM_GET_DAQ_CLOCK_MCAST_COUNTER, CRM_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER);
                    } else {
                        char ts[64];
                        clockGetString(ts, sizeof(ts), (((uint64_t)CRM_GET_DAQ_CLOCK_MCAST_TIME64_HIGH) << 32) | CRM_GET_DAQ_CLOCK_MCAST_TIME64_LOW);
                        printf(" <- X t=%s, sync=%u", ts, CRM_GET_DAQ_CLOCK_MCAST_SYNCH_STATE64);
                        if (CRM_GET_DAQ_CLOCK_MCAST_PAYLOAD_FMT & DAQ_CLOCK_PAYLOAD_FMT_ID)
                            printf(" counter=%u, cluster=%u", CRM_GET_DAQ_CLOCK_MCAST_COUNTER64, CRM_GET_DAQ_CLOCK_MCAST_CLUSTER_IDENTIFIER64);
                    }
                    printf("\n");
                }

            }

            break;
#endif // XCP_ENABLE_DAQ_CLOCK_MULTICAST
#ifdef XCPTL_ENABLE_MULTICAST
            case CC_TL_GET_SERVER_ID:
                printf(" <- %u.%u.%u.%u:%u %s\n", CRM_TL_GET_SERVER_ID_ADDR(0), CRM_TL_GET_SERVER_ID_ADDR(1), CRM_TL_GET_SERVER_ID_ADDR(2), CRM_TL_GET_SERVER_ID_ADDR(3),
                       CRM_TL_GET_SERVER_ID_PORT, &CRM_TL_GET_SERVER_ID_ID);
                break;
#endif
            default:
                break;
            }
            break;

        default:
            printf(" <- OK\n");
            break;

        } /* switch */
    }
}

static void XcpPrintDaqList(uint16_t daq) {

    if (daq >= shared.daq_lists.daq_count)
        return;

    printf("  DAQ %u:", daq);
    printf(" event=%u (%s),", DaqListEventChannel(daq), XcpGetEventName(DaqListEventChannel(daq)));
    printf(" ext=%02Xh,", DaqListAddrExt(daq));
    printf(" firstOdt=%u,", DaqListFirstOdt(daq));
    printf(" lastOdt=%u,", DaqListLastOdt(daq));
    printf(" mode=%02Xh,", DaqListMode(daq));
    printf(" state=%02Xh\n", DaqListState(daq));

    for (int i = DaqListFirstOdt(daq); i <= DaqListLastOdt(daq); i++) {
        printf("    ODT %u (%u):", i - DaqListFirstOdt(daq), i);
        printf(" firstOdtEntry=%u, lastOdtEntry=%u, size=%u:\n", DaqListOdtTable[i].first_odt_entry, DaqListOdtTable[i].last_odt_entry, DaqListOdtTable[i].size);
        for (int e = DaqListOdtTable[i].first_odt_entry; e <= DaqListOdtTable[i].last_odt_entry; e++) {
#ifdef XCP_ENABLE_DAQ_ADDREXT
            printf("      ODT_ENTRY %u (%u): %u:%08X,%u\n", e - DaqListOdtTable[i].first_odt_entry, e, DaqListOdtEntryAddrExtTable[e], DaqListOdtEntryAddrTable[e],
                   DaqListOdtEntrySizeTable[e]);
#else
            printf("      ODT_ENTRY %u (%u): %08X,%u\n", e - DaqListOdtTable[i].first_odt_entry, e, DaqListOdtEntryAddrTable[e], DaqListOdtEntrySizeTable[e]);
#endif
        }

    } /* j */
}

#endif // DBG_LEVEL
