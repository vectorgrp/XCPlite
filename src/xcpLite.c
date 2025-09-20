/*****************************************************************************
| File:
|   xcpLite.c
|
|  Description:
|    Implementation of the ASAM XCP Protocol Layer V1.4
|    Version V0.9.2
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
|       - DAQ does not support prescaler
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
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
|  No limitations and full compliance are available with the commercial version
|  from Vector Informatik GmbH, please contact Vector
|***************************************************************************/

#include "main_cfg.h"  // for OPTION_xxx
#include "xcp_cfg.h"   // XCP protocol layer configuration parameters (XCP_xxx)
#include "xcptl_cfg.h" // XCP transport layer configuration parameters (XCPTL_xxx)

#include "xcpLite.h" // XCP protocol layer interface functions

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIx32, PRIu64
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uint8_t, uint16_t, uint32_t, int32_t, uin...
#include <stdio.h>    // for printf
#include <stdlib.h>   // for free, malloc
#include <string.h>   // for memcpy, memset, strlen

#include "dbg_print.h"   // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#include "persistency.h" // for XcpBinFreezeCalSeg
#include "platform.h"    // for atomics
#include "xcp.h"         // XCP protocol definitions
#include "xcpEthTl.h"    // for transport layer XcpTlWaitForTransmitQueueEmpty and XcpTlSendCrm
#include "xcpQueue.h"    // for QueueXxx transport queue layer interface

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
#else
#error "Please define XCPTL_DTO_SIZE"
#endif

#if XCPTL_MAX_CTO_SIZE > XCPTL_MAX_DTO_SIZE
#error "XCPTL_MAX_CTO_SIZE>XCPTL_MAX_DTO_SIZE!"
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

/* Check XCP_MAX_DAQ_COUNT */
/* Default 256 - 2 Byte ODT header */
#if defined(XCP_MAX_DAQ_COUNT)
#if (XCP_MAX_DAQ_COUNT > 0xFFFE)
#error "XCP_MAX_DAQ_COUNT must be <= 0xFFFE"
#endif
#else
#define XCP_MAX_DAQ_COUNT 256
#endif

/****************************************************************************/
/* Protocol layer state data                                                */
/****************************************************************************/

// XCP singleton
// Calling XCP functions (e.g. XcpEvent()) before XcpInit() will assert
tXcpData gXcp = {0};

/****************************************************************************/
/* Forward declarations of static functions                                 */
/****************************************************************************/

static uint8_t XcpAsyncCommand(bool async, const uint32_t *cmdBuf, uint8_t cmdLen);

/****************************************************************************/
/* Macros                                                                   */
/****************************************************************************/

/* Shortcuts for gXcp->DaqLists */
/* j is absolute odt number */
/* i is daq number */
#define DaqListOdtTable ((tXcpOdt *)&gXcp.DaqLists->u.daq_list[gXcp.DaqLists->daq_count])
#define OdtEntryAddrTable ((int32_t *)&DaqListOdtTable[gXcp.DaqLists->odt_count])
#define OdtEntrySizeTable ((uint8_t *)&OdtEntryAddrTable[gXcp.DaqLists->odt_entry_count])
#ifdef XCP_ENABLE_DAQ_ADDREXT
#define OdtEntryAddrExtTable ((uint8_t *)&OdtEntrySizeTable[gXcp.DaqLists->odt_entry_count])
#endif
#define DaqListOdtEntryCount(j) ((DaqListOdtTable[j].last_odt_entry - DaqListOdtTable[j].first_odt_entry) + 1)
#define DaqListOdtCount(i) ((gXcp.DaqLists->u.daq_list[i].last_odt - gXcp.DaqLists->u.daq_list[i].first_odt) + 1)
#define DaqListLastOdt(i) gXcp.DaqLists->u.daq_list[i].last_odt
#define DaqListFirstOdt(i) gXcp.DaqLists->u.daq_list[i].first_odt
#define DaqListMode(i) gXcp.DaqLists->u.daq_list[i].mode
#define DaqListState(i) gXcp.DaqLists->u.daq_list[i].state
#define DaqListEventChannel(i) gXcp.DaqLists->u.daq_list[i].EVENT_ID
#define DaqListAddrExt(i) gXcp.DaqLists->u.daq_list[i].addr_ext
#define DaqListPriority(i) gXcp.DaqLists->u.daq_list[i].priority
#ifdef XCP_MAX_EVENT_COUNT
#define DaqListFirst(event) gXcp.DaqLists->daq_first[event]
#define DaqListNext(daq) gXcp.DaqLists->u.daq_list[daq].next
#endif

/* Shortcuts for gXcpCrm */
#define CRM (gXcp.Crm)
#define CRM_LEN (gXcp.CrmLen)
#define CRM_BYTE(x) (gXcp.Crm.b[x])
#define CRM_WORD(x) (gXcp.Crm.w[x])
#define CRM_DWORD(x) (gXcp.Crm.dw[x])

/* Error handling */
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

// State checks
#define isInitialized() (0 != (gXcp.SessionStatus & SS_INITIALIZED))
#define isActivated() ((SS_ACTIVATED | SS_INITIALIZED) == ((gXcp.SessionStatus & (SS_ACTIVATED | SS_INITIALIZED))))
#define isStarted() (0 != (gXcp.SessionStatus & SS_STARTED))
#define isConnected() (0 != (gXcp.SessionStatus & SS_CONNECTED))
#define isLegacyMode() (0 != (gXcp.SessionStatus & SS_LEGACY_MODE))

// Thread safe state checks
#define isDaqRunning() atomic_load_explicit(&gXcp.DaqRunning, memory_order_relaxed)

/****************************************************************************/
// Logging
/****************************************************************************/

#if defined(OPTION_ENABLE_DBG_PRINTS) && !defined(OPTION_FIXED_DBG_LEVEL) && defined(OPTION_DEFAULT_DBG_LEVEL)

uint8_t gDebugLevel = OPTION_DEFAULT_DBG_LEVEL;

// Set the log level
void XcpSetLogLevel(uint8_t level) {
    if (level > 3)
        DBG_PRINTF_WARNING("Set log level %u -> %u\n", gDebugLevel, level);
    gDebugLevel = level;
}

#else

// Set the log level dummy, log level is a constant or logging is off
void XcpSetLogLevel(uint8_t level) {
    (void)level;
    DBG_PRINT_WARNING("XcpSetLogLevel ignored\n");
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

uint16_t XcpGetSessionStatus(void) { return gXcp.SessionStatus; }

bool XcpIsInitialized(void) { return isInitialized(); }
bool XcpIsActivated(void) { return isActivated(); }
bool XcpIsStarted(void) { return isStarted(); }
bool XcpIsConnected(void) { return isConnected(); }
bool XcpIsDaqRunning(void) { return isDaqRunning(); }

bool XcpIsDaqEventRunning(uint16_t event) {

    if (!isDaqRunning())
        return false; // DAQ not running

    for (uint16_t daq = 0; daq < gXcp.DaqLists->daq_count; daq++) {
        if ((DaqListState(daq) & DAQ_STATE_RUNNING) == 0)
            continue; // DAQ list not active
        if (DaqListEventChannel(daq) == event)
            return true; // Event is associated to this DAQ list
    }

    return false;
}

#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
uint16_t XcpGetClusterId(void) { return gXcp.ClusterId; }
#endif

uint64_t XcpGetDaqStartTime(void) { return gXcp.DaqStartClock64; }

uint32_t XcpGetDaqOverflowCount(void) { return gXcp.DaqOverflowCount; }

/**************************************************************************/
/* EPK version string                                                     */
/**************************************************************************/

// Set/get the EPK (A2l file version string)
void XcpSetEpk(const char *epk) {
    assert(epk != NULL);
    size_t epk_len = STRNLEN(epk, XCP_EPK_MAX_LENGTH);
    STRNCPY(gXcp.Epk, epk, epk_len);
    gXcp.Epk[XCP_EPK_MAX_LENGTH] = 0; // Ensure null-termination
    // Remove unwanted characters from the EPK string
    for (char *p = gXcp.Epk; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == ':') {
            *p = '_'; // Replace with underscores
        }
    }
    DBG_PRINTF3("EPK = '%s'\n", gXcp.Epk);
}
const char *XcpGetEpk(void) {
    if (STRNLEN(gXcp.Epk, XCP_EPK_MAX_LENGTH) == 0)
        return NULL;
    return gXcp.Epk;
}

/**************************************************************************/
/* Calibration segments                                                   */
/**************************************************************************/

#ifdef XCP_ENABLE_CALSEG_LIST

// Initialize the calibration segment list
static void XcpInitCalSegList(void) {
    gXcp.CalSegList.count = 0;
    gXcp.CalSegList.write_delayed = false;
    mutexInit(&gXcp.CalSegList.mutex, false, 0); // Non-recursive mutex, no spin count
}

// Free the calibration segment list
static void XcpFreeCalSegList(void) {
    assert(isInitialized());
    for (uint16_t i = 0; i < gXcp.CalSegList.count; i++) {
        tXcpCalSeg *calseg = &gXcp.CalSegList.calseg[i];
        if (calseg->xcp_page != NULL) {
            free(calseg->xcp_page);
            calseg->xcp_page = NULL;
        }
        if (calseg->ecu_page != NULL) {
            free(calseg->ecu_page);
            calseg->ecu_page = NULL;
        }
        uintptr_t free_page = atomic_load_explicit(&calseg->free_page, memory_order_relaxed);
        if (free_page != 0) {
            free((void *)free_page);
            atomic_store_explicit(&calseg->free_page, (uintptr_t)NULL, memory_order_relaxed);
        }
    }
    gXcp.CalSegList.count = 0;
    mutexDestroy(&gXcp.CalSegList.mutex);
}

// Get a pointer to the list and the size of the list
tXcpCalSegList const *XcpGetCalSegList(void) {
    assert(isInitialized());
    return &gXcp.CalSegList;
}

// Get a pointer to the calibration segment struct of calseg index
tXcpCalSeg *XcpGetCalSeg(tXcpCalSegIndex calseg) {
    assert(isInitialized());
    if (calseg >= gXcp.CalSegList.count)
        return NULL;
    return &gXcp.CalSegList.calseg[calseg];
}

// Get the index of a calibration segment by name
tXcpCalSegIndex XcpFindCalSeg(const char *name) {
    assert(isInitialized());
    for (tXcpCalSegIndex i = 0; i < gXcp.CalSegList.count; i++) {
        tXcpCalSeg *calseg = &gXcp.CalSegList.calseg[i];
        if (strcmp(calseg->name, name) == 0) {
            return i;
        }
    }
    return XCP_UNDEFINED_CALSEG; // Not found
}

// Get the name of the calibration segment
const char *XcpGetCalSegName(tXcpCalSegIndex calseg) {
    assert(isInitialized());
    assert(calseg < gXcp.CalSegList.count);
    return gXcp.CalSegList.calseg[calseg].name;
}

// Get the XCP/A2L address (address mode XCP_ADDR_MODE_SEG) of a calibration segment
uint32_t XcpGetCalSegBaseAddress(tXcpCalSegIndex calseg) {
    assert(isInitialized());
    assert(calseg < gXcp.CalSegList.count);
    return XcpAddrEncodeSegIndex(calseg, 0);
}

// Create a calibration segment
// Thread safe
// Returns the handle (calibration segment index) or XCP_UNDEFINED_CALSEG when out of memory
tXcpCalSegIndex XcpCreateCalSeg(const char *name, const void *default_page, uint16_t size) {

    assert(isInitialized());

    mutexLock(&gXcp.CalSegList.mutex);

    // Check out of memory
    if (gXcp.CalSegList.count >= XCP_MAX_CALSEG_COUNT) {
        mutexUnlock(&gXcp.CalSegList.mutex);
        DBG_PRINT_ERROR("too many calibration segments\n");
        return XCP_UNDEFINED_CALSEG;
    }

    // Check if already existing
    tXcpCalSeg *c = NULL;
    tXcpCalSegIndex index = XcpFindCalSeg(name);
    if (index != XCP_UNDEFINED_CALSEG) {
#ifdef XCP_ENABLE_FREEZE_CAL_PAGE
        // Check if this is a preloaded segment
        // Preloaded segments have the correct size and a valid default page, loaded from the binary calibration segment image file on startup
        c = &gXcp.CalSegList.calseg[index];
        if (size == c->size && c->default_page != NULL && (c->mode & PAG_PROPERTY_PRELOAD) != 0) {
            DBG_PRINTF3("Use preloaded CalSeg %u: %s index=%u, addr=0x%08X, size=%u\n", index, c->name, index + 1, XcpGetCalSegBaseAddress(index), c->size);
        } else
#endif
        {
            // Error if segment already exists
            mutexUnlock(&gXcp.CalSegList.mutex);
            DBG_PRINTF_ERROR("Calibration segment '%s' already exists with size %u\n", name, c->size);
            return XCP_UNDEFINED_CALSEG;
        }
    } else {
        // Create a new calibration segment with given default page
        index = gXcp.CalSegList.count;
        c = &gXcp.CalSegList.calseg[index];
        gXcp.CalSegList.count++;
        STRNCPY(c->name, name, XCP_MAX_CALSEG_NAME);
        c->name[XCP_MAX_CALSEG_NAME] = 0;
        c->size = size;
        c->default_page = (uint8_t *)default_page;
#ifdef XCP_ENABLE_FREEZE_CAL_PAGE
        c->file_pos = 0;
#endif
        DBG_PRINTF3("Create CalSeg %u: %s index=%u, addr=0x%08X, size=%u\n", index, c->name, index + 1, XcpGetCalSegBaseAddress(index), c->size);
    }

    // Init
    c->xcp_page = NULL;
    c->ecu_page = NULL;
    atomic_store_explicit(&c->free_page, (uintptr_t)NULL, memory_order_relaxed);
    c->free_page_hazard = false; // Free page is not in use yet, no hazard
    atomic_store_explicit(&c->ecu_page_next, (uintptr_t)NULL, memory_order_relaxed);
    c->write_pending = false;
    c->xcp_access = XCP_CALPAGE_DEFAULT_PAGE;                                              // Default page for XCP access is the working page
    atomic_store_explicit(&c->ecu_access, XCP_CALPAGE_DEFAULT_PAGE, memory_order_relaxed); // Default page for ECU access is the working page
    atomic_store_explicit(&c->lock_count, 0, memory_order_relaxed);                        // No locks
#ifdef XCP_ENABLE_FREEZE_CAL_PAGE
    c->mode = 0; // Default mode is freeze not enabled, set by XCP command SET_SEGMENT_MODE
#endif

    // Allocate the working page and initialize RCU, if XCP has been activated
    if (isActivated()) {

        // Allocate the ecu working page (RAM page)
        c->xcp_page = malloc(size);
        memcpy(c->xcp_page, c->default_page, size); // Copy default page to XCP working page copy

        // Allocate the xcp page
        c->ecu_page = malloc(size);
        memcpy(c->ecu_page, c->default_page, size); // Copy default page to ECU working page copy

        // Allocate a free uninitialized page
        atomic_store_explicit(&c->free_page, (uintptr_t)malloc(size), memory_order_relaxed);

        // New ECU page version not updated
        atomic_store_explicit(&c->ecu_page_next, (uintptr_t)c->ecu_page, memory_order_relaxed);

        // Enable access to the working page
        c->xcp_access = XCP_CALPAGE_WORKING_PAGE;                                              // Default page for XCP access is the working page
        atomic_store_explicit(&c->ecu_access, XCP_CALPAGE_WORKING_PAGE, memory_order_relaxed); // Default page for ECU access is the working page
                                                                                               // No write pending
    }

    mutexUnlock(&gXcp.CalSegList.mutex);
    return index;
}

// Lock a calibration segment and return a pointer to the ECU page
uint8_t const *XcpLockCalSeg(tXcpCalSegIndex calseg) {

    assert(isInitialized());

    if (calseg >= gXcp.CalSegList.count) {
        DBG_PRINTF_ERROR("Invalid calseg %u\n", calseg);
        return NULL; // Uninitialized or invalid calseg
    }

    tXcpCalSeg *c = &gXcp.CalSegList.calseg[calseg];

    // Not activated, return default page
    if (!isActivated()) {
        return c->default_page;
    }

    // Update
    // Increment the lock count
    if (0 == atomic_fetch_add_explicit(&c->lock_count, 1, memory_order_relaxed)) {

        // Update if there is a new page version, free the old page
        uint8_t *ecu_page_next = (uint8_t *)atomic_load_explicit(&c->ecu_page_next, memory_order_acquire);
        if (c->ecu_page != ecu_page_next) {
            c->free_page_hazard = true; // Free page might be acquired by some other thread, since we got the first lock on this segment
            atomic_store_explicit(&c->free_page, (uintptr_t)c->ecu_page, memory_order_release);
            c->ecu_page = ecu_page_next;
        } else {
            c->free_page_hazard = false; // There was no lock and no need for update, free page must be safe now, if there is one
        }
    }

    // Return the active ECU page (RAM or FLASH)
    if (atomic_load_explicit(&c->ecu_access, memory_order_relaxed) != XCP_CALPAGE_WORKING_PAGE) {
        return c->default_page;
    } else {
        return c->ecu_page;
    }
}

// Unlock a calibration segment
void XcpUnlockCalSeg(tXcpCalSegIndex calseg) {

    if (!isActivated())
        return;

    if (calseg >= gXcp.CalSegList.count) {
        DBG_PRINT_ERROR("XCP not initialized or invalid calseg\n");
        return; // Uninitialized or invalid calseg
    }

    atomic_fetch_sub_explicit(&gXcp.CalSegList.calseg[calseg].lock_count, 1, memory_order_relaxed); // Decrement the lock count
}

// XCP client memory read
// Read xcp or default page
// Read ecu page is not supported, calibration changes might be stale
static uint8_t XcpCalSegReadMemory(uint32_t src, uint16_t size, uint8_t *dst) {

    // Decode the source address into calibration segment and offset
    uint16_t calseg = XcpAddrDecodeSegNumber(src); // Get the calibration segment number from the address
    uint16_t offset = XcpAddrDecodeSegOffset(src); // Get the offset within the calibration segment

    // Check for EPK read access
    if (calseg == 0) {
        const char *epk = XcpGetEpk();
        if (epk != NULL) {
            uint16_t epk_len = (uint16_t)strlen(epk);
            if (size + offset <= epk_len) {
                memcpy(dst, epk + offset, size);
                return CRC_CMD_OK;
            }
        }
    }

    if (calseg > gXcp.CalSegList.count) {
        DBG_PRINTF_ERROR("invalid calseg number %u\n", calseg);
        return CRC_ACCESS_DENIED;
    }
    tXcpCalSeg *c = &gXcp.CalSegList.calseg[calseg - 1];
    if (offset + size > c->size) {
        DBG_PRINTF_ERROR("out of bound calseg read access (addr=%08X, size=%u)\n", src, size);
        return CRC_ACCESS_DENIED;
    }

    memcpy(dst, c->xcp_access != XCP_CALPAGE_WORKING_PAGE ? c->default_page + offset : c->xcp_page + offset, size);
    return CRC_CMD_OK;
}

// Publish a modified calibration segment
// Option to wait for this, or return unsuccessful with CRC_CMD_PENDING
static uint8_t XcpCalSegPublish(tXcpCalSeg *c, bool wait) {
    // Try allocate a new xcp page
    // Im a multithreaded consumer use case, we must be sure the free page is really not in use anymore
    // We simply wait until all threads are updated, this is theoretically not free of starvation, but calibration changes are slow
    uint8_t *free_page = (uint8_t *)atomic_load_explicit(&c->free_page, memory_order_acquire);
    if (wait) {
        // Wait and delay the XCP server receive thread, until a free page becomes available
        for (int timeout = 0; timeout < XCP_CALSEG_AQUIRE_FREE_PAGE_TIMEOUT && (free_page == NULL || c->free_page_hazard); timeout++) {
            sleepUs(1000);
            free_page = (uint8_t *)atomic_load_explicit(&c->free_page, memory_order_acquire);
        }
        if (free_page == NULL) {
            DBG_PRINTF_ERROR("Can not update calibration changes, timeout - calseg %s locked\n", c->name);
            return CRC_ACCESS_DENIED; // No free page available
        }
    } else {
        if (free_page == NULL || c->free_page_hazard) {
            DBG_PRINTF5("Can not update calibration changes of %s yet\n", c->name);
            return CRC_CMD_PENDING; // No free page available
        }
    }

    // Acquire the free page
    uint8_t *xcp_page_new = free_page;
    atomic_store_explicit(&c->free_page, (uintptr_t)NULL, memory_order_release);

    // Copy old xcp page to the new xcp page
    uint8_t *xcp_page_old = c->xcp_page;
    memcpy(xcp_page_new, xcp_page_old, c->size); // Copy the xcp page
    c->xcp_page = xcp_page_new;

    // Publish the old xcp page
    atomic_store_explicit(&c->ecu_page_next, (uintptr_t)xcp_page_old, memory_order_release);

    return CRC_CMD_OK;
}

// Publish all modified calibration segments
// Must be call from the same thread that calles XcpCommend
uint8_t XcpCalSegPublishAll(bool wait) {
    uint8_t res = CRC_CMD_OK;
    // If no atomic calibration operation is in progress
    if (!gXcp.CalSegList.write_delayed) {
        for (uint16_t i = 0; i < gXcp.CalSegList.count; i++) {
            tXcpCalSeg *c = &gXcp.CalSegList.calseg[i];
            if (c->write_pending) {
                uint8_t res1 = XcpCalSegPublish(c, wait);
                if (res1 == CRC_CMD_OK) {
                    c->write_pending = false;
                } else {
                    res = res1;
                }
            }
        }
    }
    return res; // Return the last error code
}

// Xcp client memory write
// Write xcp page, error on write to default page or EPK segment
static uint8_t XcpCalSegWriteMemory(uint32_t dst, uint16_t size, const uint8_t *src) {
    // Decode the destination address into calibration segment index and offset
    uint16_t calseg = XcpAddrDecodeSegNumber(dst);
    uint16_t offset = XcpAddrDecodeSegOffset(dst);

    if (calseg == 0) {
        DBG_PRINT_ERROR("invalid write access to calseg number 0 (EPK)\n");
        return CRC_ACCESS_DENIED;
    }
    if (calseg > gXcp.CalSegList.count) {
        DBG_PRINTF_ERROR("invalid calseg number %u\n", calseg);
        return CRC_ACCESS_DENIED;
    }
    tXcpCalSeg *c = &gXcp.CalSegList.calseg[calseg - 1];
    if (offset + size > c->size) {
        DBG_PRINTF_ERROR("out of bound calseg write access (number=%u, offset=%u, size=%u)\n", calseg, offset, size);
        return CRC_ACCESS_DENIED;
    }
    if (c->xcp_access != XCP_CALPAGE_WORKING_PAGE) {
        DBG_PRINTF_ERROR("attempt to write default page addr=%08X\n", dst);
        return CRC_ACCESS_DENIED;
    }

    // Update data in the current xcp page
    memcpy(c->xcp_page + offset, src, size);

    // Calibration page RCU
    // If write delayed, we do not update the ECU page yet
    if (gXcp.CalSegList.write_delayed) {
        c->write_pending = true; // Modified
    } else {
#ifdef XCP_ENABLE_CALSEG_LAZY_WRITE
        // If lazy mode is enabled, we do not need to update the ECU page yet
        uint8_t res = XcpCalSegPublish(c, false);
        if (res)
            c->write_pending = true;
#else
        // If not lazy mode, we wait until a free page is available
        uint8_t res = XcpCalSegPublish(c, true);
        if (res)
            return res;
#endif
    }
    return CRC_CMD_OK;
}

// Table 97 GET SEGMENT INFO command structure
// Returns information on a specific SEGMENT.
// If the specified SEGMENT is not available, ERR_OUT_OF_RANGE will be returned.
static uint8_t XcpGetSegInfo(uint8_t segment, uint8_t mode, uint8_t seg_info, uint8_t map_index) {
    (void)map_index; // Mapping not supported

    if (segment > gXcp.CalSegList.count) {
        DBG_PRINTF_ERROR("invalid segment number: %u\n", segment);
        return CRC_OUT_OF_RANGE;
    }

    // EPK segment (segment = 0) does not support calibration pages or mappings
    // @@@@ TODO: better handling if EPK is not set
    if (segment == 0) {
        const char *epk = XcpGetEpk();
        if (epk == NULL) {
            DBG_PRINT_ERROR("EPK segment not available\n");
            return CRC_OUT_OF_RANGE;
        }
        switch (mode) {
        case 0: // Mode 0 - get get basic info (address, length or name)
            CRM_LEN = CRM_GET_SEGMENT_INFO_LEN_MODE0;
            if (seg_info == 0) {
                CRM_GET_SEGMENT_INFO_BASIC_INFO = XCP_ADDR_EPK; // EPK segment address
                return CRC_CMD_OK;
            } else if (seg_info == 1) {
                CRM_GET_SEGMENT_INFO_BASIC_INFO = (uint16_t)strlen(epk); // EPK segment size
                return CRC_CMD_OK;
            } else if (seg_info == 2) {              // EPK segment name (Vector extension, name via MTA and upload)
                CRM_GET_SEGMENT_INFO_BASIC_INFO = 3; // Length of the name
                gXcp.MtaPtr = (uint8_t *)"epk";
                gXcp.MtaExt = XCP_ADDR_EXT_PTR;
                return CRC_CMD_OK;
            } else {
                return CRC_OUT_OF_RANGE;
            }
            break;
        case 1: // Mode 1 - get standard info
            CRM_LEN = CRM_GET_SEGMENT_INFO_LEN_MODE1;
            CRM_GET_SEGMENT_INFO_MAX_PAGES = 1;
            CRM_GET_SEGMENT_INFO_ADDRESS_EXTENSION = XCP_ADDR_EXT_EPK;
            CRM_GET_SEGMENT_INFO_MAX_MAPPING = 0;
            CRM_GET_SEGMENT_INFO_COMPRESSION = 0;
            CRM_GET_SEGMENT_INFO_ENCRYPTION = 0;
            return CRC_CMD_OK;
        case 2: // Mode 2 - get mapping info not supported
            return CRC_OUT_OF_RANGE;
        default: // Illegal mode
            return CRC_CMD_SYNTAX;
        }
    }

    // Calibration segment (segment >= 1)
    tXcpCalSegIndex calseg = segment - 1;
    tXcpCalSeg *c = &gXcp.CalSegList.calseg[calseg];
    // 0 - basic address info, 1 - standard info, 2 - mapping info
    switch (mode) {
    case 0: // Mode 0 - get address or length depending on seg_info
        CRM_LEN = CRM_GET_SEGMENT_INFO_LEN_MODE0;
        if (seg_info == 0) { // Get address
            CRM_GET_SEGMENT_INFO_BASIC_INFO = XcpGetCalSegBaseAddress(calseg);
            return CRC_CMD_OK;
        } else if (seg_info == 1) { // Get length
            CRM_GET_SEGMENT_INFO_BASIC_INFO = c->size;
            return CRC_CMD_OK;
        } else if (seg_info == 2) { // Get segment name (Vector extension, name via MTA and upload)
            CRM_GET_SEGMENT_INFO_BASIC_INFO = strlen(c->name);
            gXcp.MtaPtr = (uint8_t *)c->name;
            gXcp.MtaExt = XCP_ADDR_EXT_PTR;
            return CRC_CMD_OK;
        } else {
            return CRC_OUT_OF_RANGE;
        }
        break;
    case 1: // Get standard info for this SEGMENT
        CRM_LEN = CRM_GET_SEGMENT_INFO_LEN_MODE1;
        CRM_GET_SEGMENT_INFO_MAX_PAGES = 2;
        CRM_GET_SEGMENT_INFO_ADDRESS_EXTENSION = XCP_ADDR_EXT_SEG;
        CRM_GET_SEGMENT_INFO_MAX_MAPPING = 0;
        CRM_GET_SEGMENT_INFO_COMPRESSION = 0;
        CRM_GET_SEGMENT_INFO_ENCRYPTION = 0;
        return CRC_CMD_OK;
    case 2: // Mode 2 - get mapping info not supported
        return CRC_OUT_OF_RANGE;
    default: // Illegal mode
        return CRC_CMD_SYNTAX;
    }
}

uint8_t XcpGetSegPageInfo(uint8_t segment, uint8_t page) {

    if (segment > gXcp.CalSegList.count)
        return CRC_OUT_OF_RANGE;
    if ((segment == 0 && page > 0) || page > 1)
        return CRC_PAGE_NOT_VALID;

    CRM_LEN = CRM_GET_PAGE_INFO_LEN;

    if (segment == 0) {
        CRM_GET_PAGE_INFO_PROPERTIES = 0x0F; // EPK segment, write access not allowed, read access don't care
        CRM_GET_PAGE_INFO_INIT_SEGMENT = 0;
        return CRC_CMD_OK;
    }

    // PAGE 0: ECU_ACCESS_DONT_CARE, XCP_READ_ACCESS_DONT_CARE, XCP_WRITE_ACCESS_DONT_CARE
    // PAGE 1: ECU_ACCESS_DONT_CARE, XCP_READ_ACCESS_DONT_CARE, XCP_WRITE_ACCESS_NOT_ALLOWED
    if (page == XCP_CALPAGE_WORKING_PAGE) {
        CRM_GET_PAGE_INFO_PROPERTIES = 0x3F; // All bits 0..5 can be set for "don't care"
    } else if (page == XCP_CALPAGE_DEFAULT_PAGE) {
        CRM_GET_PAGE_INFO_PROPERTIES = 0x0F; // All bits 0..3 can be set for "don't care", but XCP write access not allowed (bits 4,5 not set)
    }
    CRM_GET_PAGE_INFO_INIT_SEGMENT = (uint8_t)(XCP_CALPAGE_DEFAULT_PAGE);
    return CRC_CMD_OK;
}

#ifdef XCP_ENABLE_CAL_PAGE

// Get active ecu or xcp calibration page
// Note: XCP/A2L segment numbers are bytes, 0 is reserved for the EPK segment, tXcpCalSegIndex is the XCP/A2L segment number - 1
static uint8_t XcpCalSegGetCalPage(tXcpCalSegNumber segment, uint8_t mode) {
    if (segment > gXcp.CalSegList.count) {
        DBG_PRINTF_ERROR("invalid segment number: %u\n", segment);
        return XCP_CALPAGE_INVALID_PAGE;
    }
    if (segment == 0) {
        return XCP_CALPAGE_DEFAULT_PAGE; // EPK segment does not have calibration pages
    }
    tXcpCalSegIndex calseg = segment - 1; // Convert to index
    if (mode == CAL_PAGE_MODE_ECU) {
        return (uint8_t)atomic_load_explicit(&gXcp.CalSegList.calseg[calseg].ecu_access, memory_order_relaxed);
    }
    if (mode == CAL_PAGE_MODE_XCP) {
        return gXcp.CalSegList.calseg[calseg].xcp_access;
    }
    DBG_PRINT_ERROR("invalid get cal page mode\n");
    return XCP_CALPAGE_INVALID_PAGE; // Invalid mode
}

// Set active ecu and/or xcp calibration page
// Note: XCP/A2L segment numbers are bytes, 0 is reserved for the EPK segment, tXcpCalSegIndex is the XCP/A2L segment number - 1
static uint8_t XcpCalSegSetCalPage(tXcpCalSegNumber segment, uint8_t page, uint8_t mode) {
    if (page > 1) {
        DBG_PRINTF_ERROR("invalid cal page number %u\n", page);
        return CRC_ACCESS_DENIED; // Invalid calseg
    }
    if (mode & CAL_PAGE_MODE_ALL) { // Set all calibration segments to the same page
        for (tXcpCalSegIndex calseg = 0; calseg < gXcp.CalSegList.count; calseg++) {
            XcpCalSegSetCalPage((uint8_t)(calseg + 1), page, mode & (CAL_PAGE_MODE_ECU | CAL_PAGE_MODE_XCP));
        }
    } else {
        if (segment < 1 || segment > gXcp.CalSegList.count) {
            DBG_PRINTF_ERROR("invalid segment number %u\n", segment);
            return XCP_CALPAGE_INVALID_PAGE;
        }
        tXcpCalSegIndex calseg = segment - 1; // Convert to index
        if (mode & CAL_PAGE_MODE_ECU) {
            atomic_store_explicit(&gXcp.CalSegList.calseg[calseg].ecu_access, page, memory_order_relaxed);
        }
        if (mode & CAL_PAGE_MODE_XCP) {
            gXcp.CalSegList.calseg[calseg].xcp_access = page;
        }
    }
    return CRC_CMD_OK;
}

// Copy calibration page
// Note: XCP/A2L segment numbers are bytes, 0 is reserved for the EPK segment, tXcpCalSegIndex is the XCP/A2L segment number - 1
#ifdef XCP_ENABLE_COPY_CAL_PAGE
static uint8_t XcpCalSegCopyCalPage(tXcpCalSegNumber srcSeg, uint8_t srcPage, tXcpCalSegNumber dstSeg, uint8_t dstPage) {
    // Only copy from default page to working page supported
    if (srcSeg != dstSeg || srcSeg > gXcp.CalSegList.count || dstPage != XCP_CALPAGE_WORKING_PAGE || srcPage != XCP_CALPAGE_DEFAULT_PAGE) {
        DBG_PRINT_ERROR("invalid calseg copy operation\n");
        return CRC_WRITE_PROTECTED;
    }
    if (dstSeg >= 1) {
        uint16_t size = gXcp.CalSegList.calseg[dstSeg - 1].size;
        const uint8_t *srcPtr = gXcp.CalSegList.calseg[srcSeg - 1].default_page;
        return XcpCalSegWriteMemory(0x80000000 | dstSeg, size, srcPtr);
    } else {
        return CRC_CMD_OK; // Silently ignore copy operations on EPK segment
    }
}
#endif

// Handle atomic calibration segment commands
#ifdef XCP_ENABLE_USER_COMMAND
uint8_t XcpCalSegCommand(uint8_t cmd) {
    switch (cmd) {
    // Begin atomic calibration operation
    case 0x01:
        gXcp.CalSegList.write_delayed = true; // Set a flag to delay ECU page updates
        for (uint16_t i = 0; i < gXcp.CalSegList.count; i++) {
            gXcp.CalSegList.calseg[i].write_pending = false;
        }
        DBG_PRINT4("Begin atomic calibration operation\n");
        return CRC_CMD_OK;

    // End atomic calibration operation
    case 0x02:
        gXcp.CalSegList.write_delayed = false; // Reset the write delay flag
        DBG_PRINT4("End atomic calibration operation\n");
        return XcpCalSegPublishAll(true); // Flush all pending writes
    }
    return CRC_CMD_UNKNOWN;
}
#endif // XCP_ENABLE_USER_COMMAND

// Freeze calibration segment working pages
// Note: XCP/A2L segment numbers (tXcpCalSegNumber) are bytes, 0 is reserved for the EPK segment, tXcpCalSegIndex is the XCP/A2L segment number - 1
#ifdef XCP_ENABLE_FREEZE_CAL_PAGE

static uint8_t XcpGetCalSegMode(tXcpCalSegNumber segment) {
    if (segment == 0 || segment > gXcp.CalSegList.count)
        return 0;                                    // EPK segment has no mode
    return gXcp.CalSegList.calseg[segment - 1].mode; // Return the segment mode
}

static uint8_t XcpSetCalSegMode(tXcpCalSegNumber segment, uint8_t mode) {
    if (segment == 0 || segment > gXcp.CalSegList.count)
        return CRC_OUT_OF_RANGE;
    gXcp.CalSegList.calseg[segment - 1].mode = mode;
    return CRC_CMD_OK;
}

// Freeze all segments or segments with freeze mode enabled
static uint8_t XcpFreezeSelectedCalSegs(bool all) {

    for (uint16_t i = 0; i < gXcp.CalSegList.count; i++) {
        tXcpCalSeg *c = &gXcp.CalSegList.calseg[i];
        if ((c->mode & PAG_PROPERTY_FREEZE) != 0 || all) {
            DBG_PRINTF3("Freeze cal seg '%s' (size=%u, pos=%u)\n", c->name, c->size, c->file_pos);
            if (!XcpBinFreezeCalSeg(i)) {
                return CRC_ACCESS_DENIED; // Access denied, freeze failed
            }
        }
    }
    return CRC_CMD_OK;
}

// Freeze all segments
bool XcpFreezeAllCalSegs(void) { return XcpFreezeSelectedCalSegs(true) != CRC_CMD_OK; }

// Set all segments to the default page
void XcpResetAllCalSegs(void) {

    for (uint16_t i = 0; i < gXcp.CalSegList.count; i++) {
        tXcpCalSeg *c = &gXcp.CalSegList.calseg[i];
        atomic_store_explicit(&c->ecu_access, XCP_CALPAGE_DEFAULT_PAGE, memory_order_relaxed); // Default page for ECU access is the working page
    }
    XcpDisconnect(); // Reset the session status
}

#endif // XCP_ENABLE_FREEZE_CAL_PAGE

#endif // XCP_ENABLE_CAL_PAGE

#endif // XCP_ENABLE_CALSEG_LIST

/****************************************************************************/
/* Calibration memory access                                                */
/****************************************************************************/

/*
XcpWriteMta is not performance critical, but critical for data consistency.
It is used to modify calibration variables.
When using memcpy, it is not guaranteed that is uses multibyte move operations specifically for alignment to provide thread safety.
Its primary responsibility is only to copy memory. Any considerations regarding thread safety must be explicitly managed.
This is also a requirement to the tool, which must ensure that the data is consistent by choosing the right granularity for DOWNLOAD and SHORT_DOWNLOAD operations.
*/

// Copy of size bytes from data to gXcp.MtaPtr or gXcp.MtaAddr depending on the addressing mode
uint8_t XcpWriteMta(uint8_t size, const uint8_t *data) {

    // EXT == XCP_ADDR_EXT_SEG calibration segment memory access
#ifdef XCP_ENABLE_CALSEG_LIST
    if (XcpAddrIsSeg(gXcp.MtaExt)) {
        uint8_t res = XcpCalSegWriteMemory(gXcp.MtaAddr, size, data);
        gXcp.MtaAddr += size;
        return res;
    }
#endif

    // EXT == XCP_ADDR_EXT_APP Application specific memory access
#ifdef XCP_ENABLE_APP_ADDRESSING
    if (XcpAddrIsApp(gXcp.MtaExt)) {
        uint8_t res = ApplXcpWriteMemory(gXcp.MtaAddr, size, data);
        gXcp.MtaAddr += size;
        return res;
    }
#endif

    // Standard memory access by pointer gXcp.MtaPtr
    if (gXcp.MtaExt == XCP_ADDR_EXT_PTR) {

        if (gXcp.MtaPtr == NULL)
            return CRC_ACCESS_DENIED;

        // TEST
        // Test data consistency: slow bytewise write to increase probability for multithreading data consistency problems
        // while (size-->0) {
        //     *gXcp.MtaPtr++ = *data++;
        //     sleepUs(1);
        // }

        // Fast write with atomic copies of basic types, assuming correctly aligned target memory locations
        switch (size) {
        case 1:
            *gXcp.MtaPtr = *data;
            break;
        case 2:
            *(uint16_t *)gXcp.MtaPtr = *(uint16_t *)data;
            break;
        case 4:
            *(uint32_t *)gXcp.MtaPtr = *(uint32_t *)data;
            break;
        case 8:
            *(uint64_t *)gXcp.MtaPtr = *(uint64_t *)data;
            break;
        default:
            memcpy(gXcp.MtaPtr, data, size);
            break;
        }
        gXcp.MtaPtr += size;
        return 0; // Ok
    }

    return CRC_ACCESS_DENIED; // Access violation, illegal address or extension
}

// Copying of size bytes from data to gXcp.MtaPtr or gXcp.MtaAddr, depending on the addressing mode
static uint8_t XcpReadMta(uint8_t size, uint8_t *data) {

    // EXT == XCP_ADDR_EXT_SEG calibration segment memory access
#ifdef XCP_ENABLE_CALSEG_LIST
    if (XcpAddrIsSeg(gXcp.MtaExt)) {
        uint8_t res = XcpCalSegReadMemory(gXcp.MtaAddr, size, data);
        gXcp.MtaAddr += size;
        return res;
    }
#endif

    // EXT == XCP_ADDR_EXT_APP Application specific memory access
#ifdef XCP_ENABLE_APP_ADDRESSING
    if (XcpAddrIsApp(gXcp.MtaExt)) {
        uint8_t res = ApplXcpReadMemory(gXcp.MtaAddr, size, data);
        gXcp.MtaAddr += size;
        return res;
    }
#endif

    // Ext == XCP_ADDR_EXT_ABS Standard memory access by absolute address pointer
    if (gXcp.MtaExt == XCP_ADDR_EXT_PTR) {
        if (gXcp.MtaPtr == NULL)
            return CRC_ACCESS_DENIED;
        memcpy(data, gXcp.MtaPtr, size);
        gXcp.MtaPtr += size;
        return 0; // Ok
    }

    // Ext == XCP_ADDR_EXT_A2L A2L file upload address space
#ifdef XCP_ENABLE_IDT_A2L_UPLOAD
    if (gXcp.MtaExt == XCP_ADDR_EXT_A2L) {
        if (!ApplXcpReadA2L(size, gXcp.MtaAddr, data))
            return CRC_ACCESS_DENIED; // Access violation
        gXcp.MtaAddr += size;
        return 0; // Ok
    }
#endif

    return CRC_ACCESS_DENIED; // Access violation, illegal address or extension
}

// Set MTA
uint8_t XcpSetMta(uint8_t ext, uint32_t addr) {

    gXcp.MtaExt = ext;
    gXcp.MtaAddr = addr;
#ifdef XCP_ENABLE_DYN_ADDRESSING
    // Event relative addressing mode, MtaPtr unknown yet
    if (XcpAddrIsDyn(gXcp.MtaExt)) {
        gXcp.MtaPtr = NULL; // MtaPtr not used
    } else
#endif
#ifdef XCP_ENABLE_CALSEG_LIST
        // Segment relative addressing mode
        if (XcpAddrIsSeg(gXcp.MtaExt)) {
            gXcp.MtaPtr = NULL; // MtaPtr not used
        } else
#endif
#if defined(XCP_ENABLE_APP_ADDRESSING)
            // Application specific addressing mode
            if (XcpAddrIsApp(gXcp.MtaExt)) {
                gXcp.MtaPtr = NULL; // MtaPtr not used
            } else
#endif
#ifdef XCP_ENABLE_ABS_ADDRESSING
                // Absolute addressing mode
                if (XcpAddrIsAbs(gXcp.MtaExt)) {
                    gXcp.MtaPtr = ApplXcpGetBaseAddr() + gXcp.MtaAddr;
                    gXcp.MtaExt = XCP_ADDR_EXT_PTR;
                } else
#endif
                {
                    return CRC_OUT_OF_RANGE; // Unsupported addressing mode for direct memory access
                }

    return CRC_CMD_OK;
}

/**************************************************************************/
/* Checksum calculation                                                   */
/**************************************************************************/

/* Table for CCITT checksum calculation */
#ifdef XCP_ENABLE_CHECKSUM

#if (XCP_CHECKSUM_TYPE == XCP_CHECKSUM_TYPE_CRC16CCITT)
static const uint16_t CRC16CCITTtab[256] = {
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
        sum = CRC16CCITTtab[((uint8_t)(sum >> 8)) ^ value] ^ (uint16_t)(sum << 8);
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

void XcpInitEventList(void) {

    gXcp.EventList.count = 0; // Reset event list
    mutexInit(&gXcp.EventList.mutex, false, 1000);
}

// Get a pointer to and the size of the XCP event list
tXcpEventList *XcpGetEventList(void) {
    if (!isActivated())
        return NULL;
    return &gXcp.EventList;
}

// Get a pointer to the XCP event struct
tXcpEvent *XcpGetEvent(tXcpEventId event) {
    if (!isActivated() || event >= gXcp.EventList.count)
        return NULL;
    return &gXcp.EventList.event[event];
}

// Get the event name
const char *XcpGetEventName(tXcpEventId event) {
    if (!isActivated() || event >= gXcp.EventList.count)
        return NULL;
    return (const char *)&gXcp.EventList.event[event].name;
}

// Get the event index (1..), return 0 if not found
uint16_t XcpGetEventIndex(tXcpEventId event) {
    if (!isActivated() || event >= gXcp.EventList.count)
        return 0;
    return gXcp.EventList.event[event].index;
}

// Find an event by name, return XCP_UNDEFINED_EVENT_ID if not found
tXcpEventId XcpFindEvent(const char *name, uint16_t *pcount) {
    uint16_t id = XCP_UNDEFINED_EVENT_ID;
    if (isActivated()) {
        uint16_t count = gXcp.EventList.count;
        if (pcount != NULL)
            *pcount = 0;
        for (uint16_t i = 0; i < count; i++) {
            if (strcmp(gXcp.EventList.event[i].name, name) == 0) {
                if (pcount != NULL)
                    *pcount += 1;
                id = i; // Remember the last found event
            }
        }
    }
    return id;
}

// Create an XCP event
// Not thread safe
// Returns the XCP event number for XcpEventXxx() or XCP_UNDEFINED_EVENT_ID when out of memory
tXcpEventId XcpCreateIndexedEvent(const char *name, uint16_t index, uint32_t cycleTimeNs, uint8_t priority) {

    if (!isActivated()) {
        return XCP_UNDEFINED_EVENT_ID; // Uninitialized
    }

    assert(name != NULL);

    // Check name length
    size_t nameLen = STRNLEN(name, XCP_MAX_EVENT_NAME + 1);
    if (nameLen > XCP_MAX_EVENT_NAME) {
        DBG_PRINTF_ERROR("event name '%.*s...' too long (%zu > %d chars)\n", XCP_MAX_EVENT_NAME, name, nameLen, XCP_MAX_EVENT_NAME);
        return XCP_UNDEFINED_EVENT_ID;
    }

    // Check event count
    uint16_t e = gXcp.EventList.count;
    if (e >= XCP_MAX_EVENT_COUNT) {
        DBG_PRINT_ERROR("too many events\n");
        return XCP_UNDEFINED_EVENT_ID; // Out of memory
    }

    gXcp.EventList.count++;
    gXcp.EventList.event[e].index = index; // Index of the event instance
    STRNCPY(gXcp.EventList.event[e].name, name, XCP_MAX_EVENT_NAME);
    gXcp.EventList.event[e].name[XCP_MAX_EVENT_NAME] = 0;
    gXcp.EventList.event[e].priority = priority;
    gXcp.EventList.event[e].cycleTimeNs = cycleTimeNs;
    DBG_PRINTF3("Create Event %u: %s index=%u, cycle=%uns, prio=%u\n", e, gXcp.EventList.event[e].name, index, cycleTimeNs, priority);
    return e;
}

// Add a measurement event to event list, return event number (0..MAX_EVENT-1), thread safe, if name exists, an instance id is appended to the name
// Thread safe
tXcpEventId XcpCreateEventInstance(const char *name, uint32_t cycleTimeNs, uint8_t priority) {

    if (!isActivated()) {
        return XCP_UNDEFINED_EVENT_ID; // Uninitialized
    }

    uint16_t count = 0;
    mutexLock(&gXcp.EventList.mutex);
    tXcpEventId id = XcpFindEvent(name, &count);
    // @@@@ TODO use preloaded event instances instead of creating a new instance
    // Event instances have no identity, could use any unused preload event instance with this name
    id = XcpCreateIndexedEvent(name, count + 1, cycleTimeNs, priority);
    mutexUnlock(&gXcp.EventList.mutex);
    return id;
}

// Add a measurement event to the event list, return event number (0..MAX_EVENT-1), thread safe, error if name exists
// Thread safe
tXcpEventId XcpCreateEvent(const char *name, uint32_t cycleTimeNs, uint8_t priority) {

    if (!isActivated()) {
        return XCP_UNDEFINED_EVENT_ID; // Uninitialized
    }

    uint16_t count = 0;
    mutexLock(&gXcp.EventList.mutex);
    tXcpEventId id = XcpFindEvent(name, &count);
    if (id != XCP_UNDEFINED_EVENT_ID) {
        mutexUnlock(&gXcp.EventList.mutex);
        DBG_PRINTF4("Event '%s' already defined, id=%u\n", name, id);
        assert(count == 1); // Creating additional event instances is not supported, use XcpCreateEventInstance
        return id;          // Event already exists, return the existing event id, event could be preloaded from binary freeze file for A2L stability
    }
    id = XcpCreateIndexedEvent(name, 0, cycleTimeNs, priority);
    mutexUnlock(&gXcp.EventList.mutex);
    return id;
}

#endif // XCP_ENABLE_DAQ_EVENT_LIST

/****************************************************************************/
/* Data Acquisition Setup                                                    */
/****************************************************************************/

// ODT header size is hardcoded to 4 bytes
// Switch to smaller 2 byte ODT header, if maximum required number of DAQ list is <= 256
#if XCP_MAX_DAQ_COUNT > 256
#define ODT_HEADER_SIZE 4 // ODT,align,DAQ_WORD header
#else
#define ODT_HEADER_SIZE 2 // ODT,DAQ header
#endif

// Timestamp size is hardcoded to 4 bytes
#define ODT_TIMESTAMP_SIZE 4

// Free all dynamic DAQ lists
static void XcpClearDaq(void) {

    gXcp.SessionStatus &= (uint16_t)(~SS_DAQ);
    atomic_store_explicit(&gXcp.DaqRunning, false, memory_order_release);

    if (gXcp.DaqLists == NULL)
        return;
    memset((uint8_t *)gXcp.DaqLists, 0, sizeof(tXcpDaqLists));
    gXcp.DaqLists->res = 0xBEAC;

#ifdef XCP_MAX_EVENT_COUNT
    uint16_t event;
    for (event = 0; event < XCP_MAX_EVENT_COUNT; event++) {
        gXcp.DaqLists->daq_first[event] = XCP_UNDEFINED_DAQ_LIST;
    }
#endif
}

// Check if there is sufficient memory for the values of DaqCount, OdtCount and OdtEntryCount
// Return CRC_MEMORY_OVERFLOW if not
static uint8_t XcpCheckMemory(void) {

    uint32_t s;

    if (gXcp.DaqLists == NULL)
        return CRC_MEMORY_OVERFLOW;

#ifdef XCP_ENABLE_DAQ_ADDREXT
#define ODT_ENTRY_SIZE 6
#else
#define ODT_ENTRY_SIZE 5
#endif

    /* Check memory overflow */
    s = (gXcp.DaqLists->daq_count * (uint32_t)sizeof(tXcpDaqList)) + (gXcp.DaqLists->odt_count * (uint32_t)sizeof(tXcpOdt)) + (gXcp.DaqLists->odt_entry_count * ODT_ENTRY_SIZE);
    if (s >= XCP_DAQ_MEM_SIZE) {
        DBG_PRINTF_ERROR("DAQ memory overflow, %u of %u Bytes required\n", s, XCP_DAQ_MEM_SIZE);
        return CRC_MEMORY_OVERFLOW;
    }

    assert(sizeof(tXcpDaqList) == 12);                  // Check size
    assert(sizeof(tXcpOdt) == 8);                       // Check size
    assert(((uint64_t)gXcp.DaqLists % 4) == 0);         // Check alignment
    assert(((uint64_t)&DaqListOdtTable[0] % 4) == 0);   // Check alignment
    assert(((uint64_t)&OdtEntryAddrTable[0] % 4) == 0); // Check alignment
    assert(((uint64_t)&OdtEntrySizeTable[0] % 4) == 0); // Check alignment

    DBG_PRINTF5("[XcpCheckMemory] %u of %u Bytes used\n", s, XCP_DAQ_MEM_SIZE);
    return 0;
}

// Allocate daqCount DAQ lists
static uint8_t XcpAllocDaq(uint16_t daqCount) {

    uint16_t daq;
    uint8_t r;

    if (gXcp.DaqLists == NULL || gXcp.DaqLists->odt_count != 0 || gXcp.DaqLists->odt_entry_count != 0)
        return CRC_SEQUENCE;
    if (daqCount == 0 || daqCount > XCP_MAX_DAQ_COUNT)
        return CRC_OUT_OF_RANGE;

    // Initialize
    if (0 != (r = XcpCheckMemory()))
        return r; // Memory overflow
    for (daq = 0; daq < daqCount; daq++) {
        DaqListEventChannel(daq) = XCP_UNDEFINED_EVENT_ID;
        DaqListAddrExt(daq) = XCP_UNDEFINED_ADDR_EXT;
#ifdef XCP_MAX_EVENT_COUNT
        DaqListNext(daq) = XCP_UNDEFINED_DAQ_LIST;
#endif
    }
    gXcp.DaqLists->daq_count = daqCount;
    return 0;
}

// Allocate odtCount ODTs in a DAQ list
static uint8_t XcpAllocOdt(uint16_t daq, uint8_t odtCount) {

    uint32_t n;

    if (odtCount == 0)
        return CRC_DAQ_CONFIG;
    if (gXcp.DaqLists == NULL || gXcp.DaqLists->daq_count == 0 || gXcp.DaqLists->odt_entry_count != 0)
        return CRC_SEQUENCE;
    if (daq >= gXcp.DaqLists->daq_count)
        return CRC_DAQ_CONFIG;

#ifdef XCP_ENABLE_OVERRUN_INDICATION_PID
    if (odtCount == 0 || odtCount >= 0x7C)
        return CRC_OUT_OF_RANGE; // MSB of ODT number is reserved for overflow indication, 0xFC-0xFF for response, error, event and service
#else
    if (odtCount == 0 || odtCount >= 0xFC)
        return CRC_OUT_OF_RANGE; // 0xFC-0xFF for response, error, event and service
#endif
    n = (uint32_t)gXcp.DaqLists->odt_count + (uint32_t)odtCount;
    if (n > 0xFFFF)
        return CRC_OUT_OF_RANGE; // Overall number of ODTs limited to 64K
    gXcp.DaqLists->u.daq_list[daq].first_odt = gXcp.DaqLists->odt_count;
    gXcp.DaqLists->odt_count = (uint16_t)n;
    gXcp.DaqLists->u.daq_list[daq].last_odt = (uint16_t)(gXcp.DaqLists->odt_count - 1);
    return XcpCheckMemory();
}

// Increase current ODT size (absolute ODT index) size by n
static bool XcpAdjustOdtSize(uint16_t daq, uint16_t odt, uint8_t n) {

    DaqListOdtTable[odt].size = (uint16_t)(DaqListOdtTable[odt].size + n);

#ifdef XCP_ENABLE_TEST_CHECKS
    assert(odt >= DaqListFirstOdt(daq));
    uint16_t max_size = (XCPTL_MAX_DTO_SIZE - ODT_HEADER_SIZE) - ((odt - DaqListFirstOdt(daq)) == 0 ? 4 : 0); // Leave space for ODT header and timestamp in first ODT
    if (DaqListOdtTable[odt].size > max_size) {
        DBG_PRINTF_ERROR("DAQ %u, ODT %u overflow, max ODT = %u!\n", daq, odt - DaqListFirstOdt(daq), max_size);
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
    if (gXcp.DaqLists == NULL || gXcp.DaqLists->daq_count == 0 || gXcp.DaqLists->odt_count == 0)
        return CRC_SEQUENCE;
    if (daq >= gXcp.DaqLists->daq_count || odtEntryCount == 0 || odt >= DaqListOdtCount(daq))
        return CRC_OUT_OF_RANGE;

    /* Absolute ODT entry count is limited to 64K */
    n = (uint32_t)gXcp.DaqLists->odt_entry_count + (uint32_t)odtEntryCount;
    if (n > 0xFFFF)
        return CRC_MEMORY_OVERFLOW;

    xcpFirstOdt = gXcp.DaqLists->u.daq_list[daq].first_odt;
    DaqListOdtTable[xcpFirstOdt + odt].first_odt_entry = gXcp.DaqLists->odt_entry_count;
    gXcp.DaqLists->odt_entry_count = (uint16_t)n;
    DaqListOdtTable[xcpFirstOdt + odt].last_odt_entry = (uint16_t)(gXcp.DaqLists->odt_entry_count - 1);
    DaqListOdtTable[xcpFirstOdt + odt].size = 0;
    return XcpCheckMemory();
}

// Set ODT entry pointer
static uint8_t XcpSetDaqPtr(uint16_t daq, uint8_t odt, uint8_t idx) {

    if (gXcp.DaqLists == NULL || daq >= gXcp.DaqLists->daq_count || odt >= DaqListOdtCount(daq))
        return CRC_OUT_OF_RANGE;
    if (XcpIsDaqRunning())
        return CRC_DAQ_ACTIVE;
    uint16_t odt0 = (uint16_t)(DaqListFirstOdt(daq) + odt); // Absolute odt index
    if (idx >= DaqListOdtEntryCount(odt0))
        return CRC_OUT_OF_RANGE;
    // Save info for XcpAddOdtEntry from WRITE_DAQ and WRITE_DAQ_MULTIPLE
    gXcp.WriteDaqOdtEntry = (uint16_t)(DaqListOdtTable[odt0].first_odt_entry + idx); // Absolute odt entry index
    gXcp.WriteDaqOdt = odt0;                                                         // Absolute ODT index
    gXcp.WriteDaqDaq = daq;
    return 0;
}

// Add an ODT entry to current DAQ/ODT
// Supports XCP_ADDR_EXT_/ABS/DYN
// All ODT entries of a DAQ list must have the same address extension,returns CRC_DAQ_CONFIG if not
// In XCP_ADDR_EXT_/DYN/REL addressing mode, the event must be unique
static uint8_t XcpAddOdtEntry(uint32_t addr, uint8_t ext, uint8_t size) {

    if (size == 0 || size > XCP_MAX_ODT_ENTRY_SIZE)
        return CRC_OUT_OF_RANGE;
    if (gXcp.DaqLists == NULL || 0 == gXcp.DaqLists->daq_count || 0 == gXcp.DaqLists->odt_count || 0 == gXcp.DaqLists->odt_entry_count)
        return CRC_DAQ_CONFIG;
    if (gXcp.WriteDaqOdtEntry - DaqListOdtTable[gXcp.WriteDaqOdt].first_odt_entry >= DaqListOdtEntryCount(gXcp.WriteDaqOdt))
        return CRC_OUT_OF_RANGE;
    if (XcpIsDaqRunning())
        return CRC_DAQ_ACTIVE;

    // DAQ list must have unique address extension
#ifndef XCP_ENABLE_DAQ_ADDREXT
    uint8_t daq_ext = DaqListAddrExt(gXcp.WriteDaqDaq);
    if (daq_ext != XCP_UNDEFINED_ADDR_EXT && ext != daq_ext) {
        DBG_PRINTF_ERROR("DAQ list must have unique address extension, DAQ=%u, ODT=%u, ext=%u, daq_ext=%u\n", gXcp.WriteDaqDaq, gXcp.WriteDaqOdt, ext, daq_ext);
        return CRC_DAQ_CONFIG; // Error not unique address extension
    }
    DaqListAddrExt(gXcp.WriteDaqDaq) = ext;
#endif

    int32_t base_offset = 0;
#ifdef XCP_ENABLE_DYN_ADDRESSING
    // DYN addressing mode, base pointer will given to XcpEventExt()
    // Max address range base-0x8000 - base+0x7FFF
    if (XcpAddrIsDyn(ext)) {
        uint16_t event = XcpAddrDecodeDynEvent(addr);
        int16_t offset = XcpAddrDecodeDynOffset(addr);
        base_offset = (int32_t)offset; // sign extend to 32 bit, the relative address may be negative
        uint16_t e0 = DaqListEventChannel(gXcp.WriteDaqDaq);
        if (e0 != XCP_UNDEFINED_EVENT_ID && e0 != event)
            return CRC_OUT_OF_RANGE; // Error event channel redefinition
        DaqListEventChannel(gXcp.WriteDaqDaq) = event;
    } else
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
        // REL addressing mode, base pointer will given to XcpEventExt()
        // Max address range base-0x80000000 - base+0x7FFFFFFF
        if (XcpAddrIsRel(ext)) { // relative addressing mode
            base_offset = XcpAddrDecodeRelOffset(addr);
        } else
#endif
#ifdef XCP_ENABLE_ABS_ADDRESSING
            // ABS addressing mode, base pointer will ApplXcpGetBaseAddr()
            // Max address range 0-0x7FFFFFFF
            // @@@@ TODO: This range checking here is too late, should be assured by the A2L creator
            if (XcpAddrIsAbs(ext)) { // absolute addressing mode
                base_offset = (int32_t)XcpAddrDecodeAbsOffset(addr);
                if (base_offset & 0x80000000)
                    return CRC_ACCESS_DENIED; // Access out of range, because ODT entry addr is signed
            } else
#endif
                return CRC_ACCESS_DENIED;

    OdtEntrySizeTable[gXcp.WriteDaqOdtEntry] = size;
    OdtEntryAddrTable[gXcp.WriteDaqOdtEntry] = base_offset; // Signed 32 bit offset relative to base pointer given to XcpEvent
#ifdef XCP_ENABLE_DAQ_ADDREXT
    OdtEntryAddrExtTable[gXcp.WriteDaqOdtEntry] = ext;
#endif
    if (!XcpAdjustOdtSize(gXcp.WriteDaqDaq, gXcp.WriteDaqOdt, size))
        return CRC_DAQ_CONFIG;
    gXcp.WriteDaqOdtEntry++; // Autoincrement to next ODT entry, no autoincrementing over ODTs
    return 0;
}

// Set DAQ list mode
// All DAQ lists associated with an event, must have the same address extension
static uint8_t XcpSetDaqListMode(uint16_t daq, uint16_t event, uint8_t mode, uint8_t prio) {

    if (gXcp.DaqLists == NULL || daq >= gXcp.DaqLists->daq_count)
        return CRC_DAQ_CONFIG;

#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    // If any events are registered, check if this event exists
    if (gXcp.EventList.count > 0) {
        tXcpEvent *e = XcpGetEvent(event); // Check if event exists
        if (e == NULL)
            return CRC_OUT_OF_RANGE;
    }
#endif

#ifdef XCP_ENABLE_DYN_ADDRESSING

    // Check if the DAQ list requires a specific event and it matches
    uint16_t event0 = DaqListEventChannel(daq);
    if (event0 != XCP_UNDEFINED_EVENT_ID && event != event0)
        return CRC_DAQ_CONFIG; // Error event not unique

    // Check all DAQ lists with same event have the same address extension
    // @@@@ TODO: This restriction is not compliant to the XCP standard, must be ensured in the tool or the API
#ifndef XCP_ENABLE_DAQ_ADDREXT
    uint8_t ext = DaqListAddrExt(daq);
    for (uint16_t daq0 = 0; daq0 < gXcp.DaqLists->daq_count; daq0++) {
        if (DaqListEventChannel(daq0) == event) {
            uint8_t ext0 = DaqListAddrExt(daq0);
            if (ext != ext0)
                return CRC_DAQ_CONFIG; // Error address extension not unique
        }
    }
#endif
#endif

    DaqListEventChannel(daq) = event;
    DaqListMode(daq) = mode;
    DaqListPriority(daq) = prio;

    // Add daq to linked list of daq lists already associated to this event
#ifdef XCP_MAX_EVENT_COUNT
    uint16_t daq0 = DaqListFirst(event);
    uint16_t *daq0_next = &DaqListFirst(event);
    while (daq0 != XCP_UNDEFINED_DAQ_LIST) {
        assert(daq0 < gXcp.DaqLists->daq_count);
        daq0 = DaqListNext(daq0);
        daq0_next = &DaqListNext(daq0);
    }
    *daq0_next = daq;
#endif

    return 0;
}

// Check if DAQ lists are fully and consistently initialized
bool XcpCheckPreparedDaqLists(void) {

    for (uint16_t daq = 0; daq < gXcp.DaqLists->daq_count; daq++) {
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
                    if (OdtEntrySizeTable[e] == 0) {
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

    gXcp.DaqStartClock64 = ApplXcpGetClock64();
    gXcp.DaqOverflowCount = 0;

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 4) {
        char ts[64];
        clockGetString(ts, sizeof(ts), gXcp.DaqStartClock64);
        DBG_PRINTF3("DAQ processing start at t=%s\n", ts);
    }
#endif

    // XcpStartDaq might be called multiple times, if DAQ lists are started individually
    // CANape never does this
    ApplXcpStartDaq();

    gXcp.SessionStatus |= SS_DAQ; // Start processing DAQ events
    atomic_store_explicit(&gXcp.DaqRunning, true, memory_order_release);
}

// Stop DAQ
static void XcpStopDaq(void) {

    gXcp.SessionStatus &= (uint16_t)(~SS_DAQ); // Stop processing DAQ events
    atomic_store_explicit(&gXcp.DaqRunning, false, memory_order_release);

    // Reset all DAQ list states
    for (uint16_t daq = 0; daq < gXcp.DaqLists->daq_count; daq++) {
        DaqListState(daq) = DAQ_STATE_STOPPED_UNSELECTED;
    }

    ApplXcpStopDaq();

    DBG_PRINT3("DAQ processing stop\n");
}

// Start DAQ list
// Do not start DAQ event processing yet
static void XcpStartDaqList(uint16_t daq) {

    if (gXcp.DaqLists == NULL)
        return;
    DaqListState(daq) |= DAQ_STATE_RUNNING;

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 4) {
        XcpPrintDaqList(daq);
    }
#endif
}

// Start all selected DAQ lists
// Do not start DAQ event processing yet
static void XcpStartSelectedDaqLists(void) {

    if (gXcp.DaqLists == NULL)
        return;

    // Start all selected DAQ lists, reset the selected state
    for (uint16_t daq = 0; daq < gXcp.DaqLists->daq_count; daq++) {
        if ((DaqListState(daq) & DAQ_STATE_SELECTED) != 0) {
            DaqListState(daq) &= (uint8_t)(~DAQ_STATE_SELECTED);
            XcpStartDaqList(daq);
        }
    }
}

// Stop individual DAQ list
// If all DAQ lists are stopped, stop event processing
static void XcpStopDaqList(uint16_t daq) {

    if (gXcp.DaqLists == NULL)
        return;
    DaqListState(daq) &= (uint8_t)(~(DAQ_STATE_OVERRUN | DAQ_STATE_RUNNING));

    /* Check if all DAQ lists are stopped */
    for (uint16_t d = 0; d < gXcp.DaqLists->daq_count; d++) {
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

    if (gXcp.DaqLists == NULL)
        return;

    // Stop all selected DAQ lists, reset the selected state
    for (uint16_t daq = 0; daq < gXcp.DaqLists->daq_count; daq++) {
        if ((DaqListState(daq) & DAQ_STATE_SELECTED) != 0) {
            XcpStopDaqList(daq);
            DaqListState(daq) = DAQ_STATE_STOPPED_UNSELECTED;
        }
    }
}

/****************************************************************************/
/* Data Acquisition Event Processor                                         */
/****************************************************************************/

// Trigger daq list
#ifdef XCP_ENABLE_DAQ_ADDREXT
static void XcpTriggerDaqList(tQueueHandle queueHandle, uint16_t daq, const uint8_t **base, uint64_t clock) {
#else
static void XcpTriggerDaqList(tQueueHandle queueHandle, uint16_t daq, const uint8_t *base, uint64_t clock) {
#endif
    uint8_t *d0;
    uint16_t odt, hs;

    // Outer loop
    // Loop over all ODTs of the current DAQ list
    for (hs = ODT_HEADER_SIZE + ODT_TIMESTAMP_SIZE, odt = DaqListFirstOdt(daq); odt <= DaqListLastOdt(daq); hs = ODT_HEADER_SIZE, odt++) {

        // Get DTO buffer
        tQueueBuffer queueBuffer = QueueAcquire(queueHandle, DaqListOdtTable[odt].size + hs);
        d0 = queueBuffer.buffer;

        // DAQ queue overflow
        if (d0 == NULL) {
#ifdef XCP_ENABLE_OVERRUN_INDICATION_PID
            gXcp.DaqOverflowCount++;
            DaqListState(daq) |= DAQ_STATE_OVERRUN;
            DBG_PRINTF4("DAQ queue overrun, daq=%u, odt=%u, overruns=%u\n", daq, odt, gXcp.DaqOverflowCount);
#else
            // Queue overflow has to be handled and indicated by the transmit queue
            DBG_PRINTF4("DAQ queue overflow, daq=%u, odt=%u\n", daq, odt);
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
            uint32_t e = DaqListOdtTable[odt].first_odt_entry; // first ODT entry index
            uint32_t el = DaqListOdtTable[odt].last_odt_entry; // last ODT entry index
            int32_t *addr_ptr = &OdtEntryAddrTable[e];         // pointer to ODT entry addr offset (signed 32 bit)
            uint8_t *size_ptr = &OdtEntrySizeTable[e];         // pointer to ODT entry size
#ifdef XCP_ENABLE_DAQ_ADDREXT
            uint8_t *addr_ext_ptr = &OdtEntryAddrExtTable[e]; // pointer to ODT entry address extension
#endif
            while (e <= el) {
                uint8_t n = *size_ptr++;
                assert(n != 0);
#ifdef XCP_ENABLE_DAQ_ADDREXT
                const uint8_t *src = (const uint8_t *)&base[*addr_ext_ptr++][*addr_ptr++];
#else
                const uint8_t *src = (const uint8_t *)&base[*addr_ptr++];
#endif
                memcpy(dst, src, n);
                dst += n;
                e++;
            }
        }

        QueuePush(queueHandle, &queueBuffer, DaqListPriority(daq) != 0 && odt == DaqListLastOdt(daq));

    } /* odt */
}

// Trigger event
// DAQ must be running
static void XcpTriggerDaqEvent(tQueueHandle queueHandle, tXcpEventId event, const uint8_t *dyn_base, const uint8_t *rel_base, uint64_t clock) {

    uint16_t daq;

    // No DAQ lists allocated
    if (gXcp.DaqLists == NULL)
        return;

    // DAQ not running
    if (!isDaqRunning())
        return;

    // Event is invalid
    if (event >= XCP_MAX_EVENT_COUNT) {
        DBG_PRINTF_ERROR("Event %u out of range\n", event);
        return;
    }

    // Get clock, if not given as parameter
    if (clock == 0)
        clock = ApplXcpGetClock64();

    // Build base pointers for each addressing mode
    // @@@@ For optimization, we assume that all the addressing modes may be indexed in an array base_addr[4]
#ifdef XCP_ENABLE_DAQ_ADDREXT
    const uint8_t *base_addr[4] = {NULL, NULL, NULL, NULL}; // Base address for each addressing mode
#ifdef XCP_ENABLE_ABS_ADDRESSING
    static_assert(XCP_ADDR_EXT_ABS < 4, "XCP_ADDR_EXT_ABS must be less than 4");
    base_addr[XCP_ADDR_EXT_ABS] = ApplXcpGetBaseAddr(); // Absolute address base
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
    static_assert(XCP_ADDR_EXT_REL < 4, "XCP_ADDR_EXT_REL must be less than 4");
    base_addr[XCP_ADDR_EXT_REL] = rel_base;
#endif
#ifdef XCP_ENABLE_DYN_ADDRESSING
    static_assert(XCP_ADDR_EXT_DYN < 4, "XCP_ADDR_EXT_DYN must be less than 4");
    base_addr[XCP_ADDR_EXT_DYN] = dyn_base;
#endif
#endif

#ifndef XCP_MAX_EVENT_COUNT

    // @@@@ Non optimized version for arbitrary event ids

    // Loop over all active DAQ lists associated to the current event
    for (daq = 0; daq < gXcp.DaqLists->daq_count; daq++) {
        if ((DaqListState(daq) & DAQ_STATE_RUNNING) == 0)
            continue; // DAQ list not active
        if (DaqListEventChannel(daq) != event)
            continue; // DAQ list not associated with this event

        // Build base pointer for this DAQ list
// Address extension unique per DAQ list
#ifndef XCP_ENABLE_DAQ_ADDREXT
        const uint8_t *base_addr = NULL;
        uint8_t ext = DaqListAddrExt(daq);
#ifdef XCP_ENABLE_ABS_ADDRESSING
        if (XcpAddrIsAbs(ext)) {
            // Absolute addressing mode for this DAQ list, base pointer is ApplXcpGetBaseAddr()
            base_addr = ApplXcpGetBaseAddr();
        } else
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
            if (XcpAddrIsRel(ext)) {
            // Relative addressing mode for this DAQ list, base pointer is given as parameter
            base_addr = rel_base;
        } else
#endif
#ifdef XCP_ENABLE_DYN_ADDRESSING
            if (XcpAddrIsDyn(ext)) {
            // Dynamic addressing mode for this DAQ list, base pointer is given as parameter
            base_addr = dyn_base;
        }
#endif
#endif // XCP_ENABLE_DAQ_ADDREXT

        XcpTriggerDaqList(daq_lists, queueHandle, daq, base_addr, clock); // Trigger DAQ list
    } /* daq */

#else

    // Optimized
    // Loop over linked list of daq lists associated to event
    if (event >= XCP_MAX_EVENT_COUNT)
        return; // Event out of range

    daq = DaqListFirst(event);
    while (daq != XCP_UNDEFINED_DAQ_LIST) {
        assert(daq < gXcp.DaqLists->daq_count);
        if (DaqListState(daq) & DAQ_STATE_RUNNING) { // DAQ list active

            // Build base pointer for this DAQ list
            // Address extension unique per DAQ list
#ifndef XCP_ENABLE_DAQ_ADDREXT
            const uint8_t *base_addr = NULL;
            uint8_t ext = DaqListAddrExt(daq);
#ifdef XCP_ENABLE_ABS_ADDRESSING
            if (XcpAddrIsAbs(ext)) {
                // Absolute addressing mode for this DAQ list, base pointer is ApplXcpGetBaseAddr()
                base_addr = ApplXcpGetBaseAddr();
            } else
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
                if (XcpAddrIsRel(ext)) {
                // Relative addressing mode for this DAQ list, base pointer is given as parameter
                base_addr = rel_base;
            } else
#endif
#ifdef XCP_ENABLE_DYN_ADDRESSING
                if (XcpAddrIsDyn(ext)) {
                // Dynamic addressing mode for this DAQ list, base pointer is given as parameter
                base_addr = dyn_base;
            }
#endif
#endif // XCP_ENABLE_DAQ_ADDREXT

            XcpTriggerDaqList(queueHandle, daq, base_addr, clock); // Trigger DAQ list
        }
        daq = DaqListNext(daq);
    }
#endif
}

// Async command processing for pending command
#ifdef XCP_ENABLE_DYN_ADDRESSING
static void XcpProcessPendingCommand(tXcpEventId event, const uint8_t *dyn_base) {
    if (!isStarted())
        return;
    if (atomic_load_explicit(&gXcp.CmdPending, memory_order_acquire)) {
        // Check if the pending command can be executed in this context
        if (XcpAddrIsDyn(gXcp.MtaExt) && XcpAddrDecodeDynEvent(gXcp.MtaAddr) == event) {
            ATOMIC_BOOL_TYPE old_value = true;
            if (atomic_compare_exchange_weak_explicit(&gXcp.CmdPending, &old_value, false, memory_order_release, memory_order_relaxed)) {
                // Convert relative signed 16 bit addr in MtaAddr to pointer MtaPtr
                gXcp.MtaPtr = (uint8_t *)(dyn_base + XcpAddrDecodeDynOffset(gXcp.MtaAddr));
                gXcp.MtaExt = XCP_ADDR_EXT_PTR;
                XcpAsyncCommand(true, (const uint32_t *)&gXcp.CmdPendingCrm, gXcp.CmdPendingCrmLen);
            }
        }
    }
}
#endif // XCP_ENABLE_DYN_ADDRESSING

// Dyn addressing mode event at a given clock
// Base for ADDR_EXT_REL and ADDR_EXT_DYN is given as parameter
// Base for ADDR_EXT_ABS is ApplXcpGetBaseAddr()
void XcpEventDynRelAt(tXcpEventId event, const uint8_t *dyn_base, const uint8_t *rel_base, uint64_t clock) {

    // Async command processing for pending command
#ifdef XCP_ENABLE_DYN_ADDRESSING
    XcpProcessPendingCommand(event, dyn_base);
#endif // XCP_ENABLE_DYN_ADDRESSING

    // Daq
    if (!isDaqRunning())
        return; // DAQ not running
    XcpTriggerDaqEvent(gXcp.Queue, event, dyn_base, rel_base, clock);
}

// Trigger an event with given base base address for ADDR_EXT_DYN and ADDR_EXT_REL
void XcpEventExt(tXcpEventId event, const uint8_t *base) {

    // Async command processing for pending command
#ifdef XCP_ENABLE_DYN_ADDRESSING
    XcpProcessPendingCommand(event, base);
#endif // XCP_ENABLE_DYN_ADDRESSING

    if (!isDaqRunning())
        return; // DAQ not running
    XcpTriggerDaqEvent(gXcp.Queue, event, base, base, 0);
}

// ABS addressing mode event
// Base for ADDR_EXT_ABS is ApplXcpGetBaseAddr()
#ifdef XCP_ENABLE_ABS_ADDRESSING
void XcpEvent(tXcpEventId event) {
    if (!isDaqRunning())
        return; // DAQ not running
    XcpTriggerDaqEvent(gXcp.Queue, event, NULL, NULL, 0);
}
#endif

/****************************************************************************/
/* Command Processor                                                        */
/****************************************************************************/

// Stops DAQ and goes to disconnected state
void XcpDisconnect(void) {
    if (!isStarted())
        return;

    if (isConnected()) {

        if (isDaqRunning()) {
            XcpStopDaq();
            XcpTlWaitForTransmitQueueEmpty(200);
        }

#ifdef XCP_ENABLE_CALSEG_LAZY_WRITE
        XcpCalSegPublishAll(true);
#endif

        gXcp.SessionStatus &= (uint16_t)(~SS_CONNECTED);
        ApplXcpDisconnect();
    }
}

// Transmit command response packet
static void XcpSendResponse(bool async, const tXcpCto *crm, uint8_t crmLen) {

    // Send async command responses via the transmit queue
    if (async) {
        tQueueBuffer queueBuffer = QueueAcquire(gXcp.Queue, crmLen);
        if (queueBuffer.buffer != NULL) {
            memcpy(queueBuffer.buffer, crm, crmLen);
            QueuePush(gXcp.Queue, &queueBuffer, true); // High priority
        }
    } else {
        XcpTlSendCrm((const uint8_t *)crm, crmLen);
    }

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 4)
        XcpPrintRes(crm);
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
    if (!atomic_compare_exchange_strong_explicit(&gXcp.CmdPending, &old_value, true, memory_order_acq_rel, memory_order_relaxed)) {
        return CRC_CMD_BUSY;
    }
    gXcp.CmdPendingCrmLen = cmdLen;
    memcpy(&gXcp.CmdPendingCrm, cmdBuf, cmdLen);
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
#ifdef DBG_LEVEL
        DBG_PRINTF3("CONNECT mode=%u\n", CRO_CONNECT_MODE);
        if ((gXcp.SessionStatus & SS_CONNECTED) != 0)
            DBG_PRINT_WARNING("Already connected! DAQ setup cleared! Legacy mode activated!\n");
#endif

        // Check application is ready for XCP connect
        if (!ApplXcpConnect())
            return CRC_CMD_OK; // Application not ready, ignore

        // Initialize Session Status
        gXcp.SessionStatus = (SS_INITIALIZED | SS_ACTIVATED | SS_STARTED | SS_CONNECTED | SS_LEGACY_MODE);

        /* Reset DAQ */
        XcpClearDaq();

        // Response
        CRM_LEN = CRM_CONNECT_LEN;
        CRM_CONNECT_TRANSPORT_VERSION = (uint8_t)((uint16_t)XCP_TRANSPORT_LAYER_VERSION >> 8); /* Major versions of the XCP Protocol Layer and Transport Layer Specifications. */
        CRM_CONNECT_PROTOCOL_VERSION = (uint8_t)((uint16_t)XCP_PROTOCOL_LAYER_VERSION >> 8);
        CRM_CONNECT_MAX_CTO_SIZE = XCPTL_MAX_CTO_SIZE;
        CRM_CONNECT_MAX_DTO_SIZE = XCPTL_MAX_DTO_SIZE;
        CRM_CONNECT_RESOURCE = RM_DAQ | RM_CAL_PAG;   /* DAQ and CAL supported */
        CRM_CONNECT_COMM_BASIC = CMB_OPTIONAL;        // GET_COMM_MODE_INFO available, byte order Intel, address granularity byte, no server block mode
        assert(*(uint8_t *)&gXcp.SessionStatus == 0); // Intel byte order
    }

    // Handle other all other commands
    else {

#ifdef DBG_LEVEL
        if (DBG_LEVEL >= 4 && !async)
            XcpPrintCmd(CRO);
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
            // Check for user defined commands for begin/end consistent calibration sequence
            uint8_t err = XcpCalSegCommand(subcmd);
            if (err == CRC_CMD_UNKNOWN) {
                check_error(ApplXcpUserCommand(subcmd));
            } else {
                check_error(err);
            }
#else
            check_error(ApplXcpUserCommand(subcmd));
#endif
        } break;
#endif

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
#ifdef XCP_ENABLE_INTERLEAVED
            CRM_GET_COMM_MODE_INFO_COMM_OPTIONAL = 0; // CMO_INTERLEAVED_MODE;
            CRM_GET_COMM_MODE_INFO_QUEUE_SIZE = XCP_INTERLEAVED_QUEUE_SIZE;
#else
            CRM_GET_COMM_MODE_INFO_COMM_OPTIONAL = 0;
            CRM_GET_COMM_MODE_INFO_QUEUE_SIZE = 0;
#endif
            CRM_GET_COMM_MODE_INFO_MAX_BS = 0;
            CRM_GET_COMM_MODE_INFO_MIN_ST = 0;
        } break;

        case CC_DISCONNECT: {
            DBG_PRINT3("DISCONNECT\n");
            XcpDisconnect();
        } break;

        case CC_GET_ID: {
            check_len(CRO_GET_ID_LEN);
            CRM_LEN = CRM_GET_ID_LEN;
            CRM_GET_ID_MODE = 0x00; // Default transfer mode is "Uncompressed data upload"
            CRM_GET_ID_LENGTH = 0;
            switch (CRO_GET_ID_TYPE) {
            case IDT_ASCII: // All other informations are provided in the response
            case IDT_ASAM_NAME:
            case IDT_ASAM_PATH:
            case IDT_ASAM_URL:
                CRM_GET_ID_LENGTH = ApplXcpGetId(CRO_GET_ID_TYPE, CRM_GET_ID_DATA, CRM_GET_ID_DATA_MAX_LEN);
                CRM_LEN = (uint8_t)(CRM_GET_ID_LEN + CRM_GET_ID_LENGTH);
                CRM_GET_ID_MODE = 0x01; // Transfer mode is "Uncompressed data in response"
                break;
#ifdef XCP_ENABLE_IDT_A2L_UPLOAD // A2L and EPK are always provided via upload
            case IDT_ASAM_EPK:
                gXcp.MtaAddr = XCP_ADDR_EPK;
                gXcp.MtaExt = XCP_ADDR_EXT_EPK;
                CRM_GET_ID_LENGTH = ApplXcpGetId(CRO_GET_ID_TYPE, NULL, 0);
                break;
            case IDT_ASAM_UPLOAD:
                gXcp.MtaAddr = XCP_ADDR_A2l;
                gXcp.MtaExt = XCP_ADDR_EXT_A2L;
                CRM_GET_ID_LENGTH = ApplXcpGetId(CRO_GET_ID_TYPE, NULL, 0);
                break;
#endif
            default:
                error(CRC_OUT_OF_RANGE);
            }
        } break;

/* Not implemented, no gXcp.ProtectionStatus checks */
#if 0
#ifdef XCP_ENABLE_SEED_KEY
          case CC_GET_SEED:
          {
             if (CRO_GET_SEED_MODE != 0x00) error(CRC_OUT_OF_RANGE)
             if ((gXcp.ProtectionStatus & CRO_GET_SEED_RESOURCE) != 0) {  // locked
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
                gXcp.ProtectionStatus &= ~resource; // unlock (reset) the appropriate resource protection mask bit
              }
              CRM_UNLOCK_PROTECTION = gXcp.ProtectionStatus; // return the current resource protection status
              CRM_LEN = CRM_UNLOCK_LEN;
            }
            break;
#endif /* XCP_ENABLE_SEED_KEY */
#endif

        case CC_GET_STATUS: {
            CRM_LEN = CRM_GET_STATUS_LEN;
            CRM_GET_STATUS_STATUS = (uint8_t)(gXcp.SessionStatus & 0xFF);
            CRM_GET_STATUS_PROTECTION = 0;
#ifdef XCP_ENABLE_DAQ_RESUME
            /* Return the session configuration id */
            CRM_GET_STATUS_CONFIG_ID = gXcp.DaqLists->config_id;
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
                gXcp.DaqLists->config_id = config_id;
                // gXcp.SessionStatus |= SS_STORE_DAQ_REQ;
                check_error(ApplXcpDaqResumeStore(config_id));
                /* @@@@ TODO Send an event message */
                // gXcp.SessionStatus &= (uint16_t)(~SS_STORE_DAQ_REQ);
            } break;
            case SS_CLEAR_DAQ_REQ:
                // gXcp.SessionStatus |= SS_CLEAR_DAQ_REQ;
                check_error(ApplXcpDaqResumeClear());
                /* @@@@ TODO Send an event message */
                // gXcp.SessionStatus &= (uint16_t)(~SS_CLEAR_DAQ_REQ);
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
                    if (XcpAddrIsDyn(gXcp.MtaExt)) {
                    if (XcpPushCommand(CRO, CRO_LEN) == CRC_CMD_BUSY)
                        goto busy_response;
                    goto no_response;
                }
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
            if (XcpAddrIsRel(gXcp.MtaExt)) {
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
            if (XcpAddrIsDyn(gXcp.MtaExt)) {
                if (XcpPushCommand(CRO, CRO_LEN) == CRC_CMD_BUSY)
                    goto busy_response;
                goto no_response;
            }
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
            if (XcpAddrIsRel(gXcp.MtaExt)) {
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
            if (XcpAddrIsDyn(gXcp.MtaExt)) {
                if (XcpPushCommand(CRO, CRO_LEN) == CRC_CMD_BUSY)
                    goto busy_response;
                goto no_response;
            }
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
            if (XcpAddrIsRel(gXcp.MtaExt)) {
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
            if (XcpAddrIsDyn(gXcp.MtaExt)) {
                if (XcpPushCommand(CRO, CRO_LEN) == CRC_CMD_BUSY)
                    goto busy_response;
                goto no_response;
            }
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
            if (XcpAddrIsRel(gXcp.MtaExt)) {
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

#ifdef XCP_ENABLE_CALSEG_LIST

        case CC_GET_PAG_PROCESSOR_INFO: {
            check_len(CRO_GET_PAG_PROCESSOR_INFO_LEN);
            CRM_LEN = CRM_GET_PAG_PROCESSOR_INFO_LEN;
            CRM_GET_PAG_PROCESSOR_INFO_MAX_SEGMENTS = (uint8_t)(gXcp.CalSegList.count + 1); // +1 for segment 0 (EPK)
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
#endif // XCP_ENABLE_CAL_PAGE

#ifdef XCP_ENABLE_CHECKSUM
        case CC_BUILD_CHECKSUM: {
            check_len(CRO_BUILD_CHECKSUM_LEN);
#ifdef XCP_ENABLE_DYN_ADDRESSING
            if (XcpAddrIsDyn(gXcp.MtaExt)) {
                XcpPushCommand(CRO, CRO_LEN);
                goto no_response;
            } // Execute in async mode
#endif
#ifdef XCP_ENABLE_REL_ADDRESSING
            if (XcpAddrIsRel(gXcp.MtaExt)) {
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
            CRM_GET_DAQ_PROCESSOR_INFO_MIN_DAQ = 0;                                                      // Total number of predefined DAQ lists
            CRM_GET_DAQ_PROCESSOR_INFO_MAX_DAQ = gXcp.DaqLists != NULL ? (gXcp.DaqLists->daq_count) : 0; // Number of currently dynamically allocated DAQ lists
#if defined(XCP_ENABLE_DAQ_EVENT_INFO) && defined(XCP_ENABLE_DAQ_EVENT_LIST)
            CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT = gXcp.EventList.count; // Number of currently available event channels
#else
            CRM_GET_DAQ_PROCESSOR_INFO_MAX_EVENT = 0; // 0 - unknown
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

            // Dynamic DAQ list configuration, timestamps, resume and overload indication supported
            // Identification field can not be switched off, bitwise data stimulation not supported, Prescaler not supported
            CRM_GET_DAQ_PROCESSOR_INFO_PROPERTIES = (uint8_t)(
                DAQ_PROPERTY_CONFIG_TYPE | 
                DAQ_PROPERTY_TIMESTAMP |
#ifdef XCP_ENABLE_OVERRUN_INDICATION_PID
                DAQ_OVERLOAD_INDICATION_PID) |
#endif
#ifdef XCP_ENABLE_OVERRUN_INDICATION_EVENT
                DAQ_OVERLOAD_INDICATION_EVENT |
#endif
#ifdef XCP_ENABLE_DAQ_RESUME
                DAQ_PROPERTY_RESUME |
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
            tXcpEvent *event = XcpGetEvent(eventNumber);
            if (event == NULL)
                error(CRC_OUT_OF_RANGE);
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
            CRM_LEN = CRM_GET_DAQ_EVENT_INFO_LEN;
            CRM_GET_DAQ_EVENT_INFO_PROPERTIES = DAQ_EVENT_PROPERTIES_DAQ | DAQ_EVENT_PROPERTIES_EVENT_CONSISTENCY;
            CRM_GET_DAQ_EVENT_INFO_MAX_DAQ_LIST = 0xFF;
            CRM_GET_DAQ_EVENT_INFO_NAME_LENGTH = (uint8_t)strlen(event->name);
            CRM_GET_DAQ_EVENT_INFO_TIME_CYCLE = timeCycle;
            CRM_GET_DAQ_EVENT_INFO_TIME_UNIT = timeUnit;
            CRM_GET_DAQ_EVENT_INFO_PRIORITY = event->priority;
            gXcp.MtaPtr = (uint8_t *)event->name;
            gXcp.MtaExt = XCP_ADDR_EXT_PTR;
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
            if (gXcp.DaqLists == NULL)
                error(CRC_SEQUENCE);
            if (daq >= gXcp.DaqLists->daq_count)
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
            if (gXcp.DaqLists == NULL)
                error(CRC_SEQUENCE);
            if (daq >= gXcp.DaqLists->daq_count)
                error(CRC_OUT_OF_RANGE);
            if ((mode & (DAQ_MODE_ALTERNATING | DAQ_MODE_DIRECTION | DAQ_MODE_DTO_CTR | DAQ_MODE_PID_OFF)) != 0)
                error(CRC_OUT_OF_RANGE); // none of these modes implemented
            if ((mode & DAQ_MODE_TIMESTAMP) == 0)
                error(CRC_CMD_SYNTAX); // timestamp is fixed on
            if (CRO_SET_DAQ_LIST_MODE_PRESCALER > 1)
                error(CRC_OUT_OF_RANGE); // prescaler is not implemented
            check_error(XcpSetDaqListMode(daq, event, mode, prio));
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
            if (gXcp.DaqLists == NULL)
                error(CRC_SEQUENCE);
            if (daq >= gXcp.DaqLists->daq_count)
                error(CRC_OUT_OF_RANGE);
            CRM_LEN = CRM_START_STOP_DAQ_LIST_LEN;
            CRM_START_STOP_DAQ_LIST_FIRST_PID = 0; // PID one byte header type not supported
            if (mode == 2) {                       // select
                DaqListState(daq) |= DAQ_STATE_SELECTED;
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
            if (gXcp.DaqLists == NULL)
                error(CRC_SEQUENCE);
            if ((0 == gXcp.DaqLists->daq_count) || (0 == gXcp.DaqLists->odt_count) || (0 == gXcp.DaqLists->odt_entry_count))
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
                if (!XcpTlWaitForTransmitQueueEmpty(1000 /* timeout_ms */)) { // Wait until daq transmit queue empty before sending command response
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
                gXcp.SessionStatus &= (uint16_t)(~SS_LEGACY_MODE);
            }
#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
            if ((CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES & TIME_SYNCH_SET_PROPERTIES_CLUSTER_ID) != 0) { // set cluster id
                DBG_PRINTF4("  Cluster id set to %u\n", CRO_TIME_SYNCH_PROPERTIES_CLUSTER_ID);
                gXcp.ClusterId = CRO_TIME_SYNCH_PROPERTIES_CLUSTER_ID; // Set cluster id
                XcpEthTlSetClusterId(gXcp.ClusterId);
            }
            CRM_TIME_SYNCH_PROPERTIES_CLUSTER_ID = gXcp.ClusterId;
#else
            if ((CRO_TIME_SYNCH_PROPERTIES_SET_PROPERTIES & TIME_SYNCH_SET_PROPERTIES_CLUSTER_ID) != 0) { // set cluster id
                // error(CRC_OUT_OF_RANGE); // CANape insists on setting a cluster id, even if Multicast is not enabled
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
            if (ApplXcpGetClockInfoGrandmaster(gXcp.ClockInfo.grandmaster.UUID, &gXcp.ClockInfo.grandmaster.epochOfGrandmaster,
                                               &gXcp.ClockInfo.grandmaster.stratumLevel)) { // Update UUID and clock details
                CRM_TIME_SYNCH_PROPERTIES_OBSERVABLE_CLOCKS = LOCAL_CLOCK_SYNCHED | GRANDM_CLOCK_READABLE | ECU_CLOCK_NONE;
                DBG_PRINTF4("  GrandmasterClock: UUID=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X stratumLevel=%u, epoch=%u\n", gXcp.ClockInfo.grandmaster.UUID[0],
                            gXcp.ClockInfo.grandmaster.UUID[1], gXcp.ClockInfo.grandmaster.UUID[2], gXcp.ClockInfo.grandmaster.UUID[3], gXcp.ClockInfo.grandmaster.UUID[4],
                            gXcp.ClockInfo.grandmaster.UUID[5], gXcp.ClockInfo.grandmaster.UUID[6], gXcp.ClockInfo.grandmaster.UUID[7], gXcp.ClockInfo.grandmaster.stratumLevel,
                            gXcp.ClockInfo.grandmaster.epochOfGrandmaster);
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
                gXcp.MtaPtr = (uint8_t *)&gXcp.ClockInfo.server;
                gXcp.MtaExt = XCP_ADDR_EXT_PTR;
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
                if (gXcp.ClusterId != clusterId)
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

void XcpBackgroundTasks(void) {

    if (!isActivated()) { // Ignore
        return;
    }

// Publish all modified calibration segments
#ifdef XCP_ENABLE_CALSEG_LAZY_WRITE
    XcpCalSegPublishAll(false);
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

    tQueueBuffer queueBuffer = QueueAcquire(gXcp.Queue, l + 2);
    tXcpCto *crm = (tXcpCto *)queueBuffer.buffer;
    if (crm != NULL) {
        crm->b[0] = PID_EV; /* Event */
        crm->b[1] = evc;    /* Eventcode */
        if (d != NULL && l > 0) {
            for (uint8_t i = 0; i < l; i++)
                crm->b[i + 2] = d[i];
        }
        QueuePush(gXcp.Queue, &queueBuffer, true);
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
    tQueueBuffer queueBuffer = QueueAcquire(gXcp.Queue, l + 4);
    uint8_t *crm = queueBuffer.buffer;
    if (crm != NULL) {
        crm[0] = PID_SERV; /* Event */
        crm[1] = 0x01;     /* Eventcode SERV_TEXT */
        uint8_t i;
        for (i = 0; i < l && i < XCPTL_MAX_CTO_SIZE - 4; i++)
            crm[i + 2] = (uint8_t)str[i];
        crm[i + 2] = '\n';
        crm[i + 3] = 0;
        QueuePush(gXcp.Queue, &queueBuffer, true);
    } else { // Queue overflow
        DBG_PRINT_WARNING("queue overflow\n");
    }
}

#endif // XCP_ENABLE_SERV_TEXT

/****************************************************************************/
/* Initialization and start of the XCP Protocol Layer                       */
/****************************************************************************/

// Init XCP protocol layer singleton once
// This is a once initialization of the static gXcp singleton data structure
// Memory for the DAQ lists are provided by the caller if daq_lists != NULL
void XcpInit(bool activate) {
    // Once
    if (isActivated()) { // Already initialized, just ignore
        return;
    }

    // Clear XCP state
    memset((uint8_t *)&gXcp, 0, sizeof(gXcp));

    if (!activate) {
        gXcp.SessionStatus = SS_INITIALIZED;
        return; // Do not activate XCP protocol layer
    }

    // Allocate DAQ list memory
    gXcp.DaqLists = malloc(sizeof(tXcpDaqLists));
    assert(gXcp.DaqLists != NULL);
    XcpClearDaq();

#ifdef XCP_ENABLE_CALSEG_LIST
    XcpInitCalSegList();
#endif

#ifdef XCP_ENABLE_DAQ_EVENT_LIST
    XcpInitEventList();
#endif

#ifdef XCP_ENABLE_DAQ_CLOCK_MULTICAST
    gXcp.ClusterId = XCP_MULTICAST_CLUSTER_ID; // XCP default cluster id (multicast addr 239,255,0,1, group 127,0,1 (mac 01-00-5E-7F-00-01)
    XcpEthTlSetClusterId(gXcp.ClusterId);
#endif

    // Initialize high resolution clock
    clockInit();

    gXcp.SessionStatus = SS_INITIALIZED | SS_ACTIVATED;
}

// Start XCP protocol layer
// Assume the transport layer is running
void XcpStart(tQueueHandle queueHandle, bool resumeMode) {

    (void)resumeMode; // Start in resume mode, not implemented yet

    if (!isActivated())
        return;

#ifdef DBG_LEVEL
    if (DBG_LEVEL >= 3) {
        DBG_PRINT3("Init XCP protocol layer\n");
        DBG_PRINTF3("  Version=%u.%u, MAX_CTO=%u, MAX_DTO=%u, DAQ_MEM=%u, MAX_DAQ=%u, MAX_ODT_ENTRY=%u, MAX_ODT_ENTRYSIZE=%u, %u KiB memory used\n",
                    XCP_PROTOCOL_LAYER_VERSION >> 8, XCP_PROTOCOL_LAYER_VERSION & 0xFF, XCPTL_MAX_CTO_SIZE, XCPTL_MAX_DTO_SIZE, XCP_DAQ_MEM_SIZE, (1 << sizeof(uint16_t) * 8) - 1,
                    (1 << sizeof(uint16_t) * 8) - 1, (1 << (sizeof(uint8_t) * 8)) - 1, (unsigned int)sizeof(gXcp) / 1024);
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
#ifdef XCP_ENABLE_IDT_A2L_UPLOAD // Enable A2L upload to host
        DBG_PRINT("A2L_UPLOAD,");
#endif
#ifdef XCP_ENABLE_IDT_A2L_HTTP_GET // Enable A2L upload to hostRust
        DBG_PRINT("A2L_URL,");
#endif
#ifdef XCP_ENABLE_DAQ_EVENT_LIST // Enable XCP event registration and optimization
        DBG_PRINT("DAQ_EVENT_LIST,");
#endif
#ifdef XCP_ENABLE_CALSEG_LIST // Enable XCP calibration segments
        DBG_PRINT("CALSEG_LIST,");
#endif
#ifdef XCP_ENABLE_DAQ_EVENT_INFO // Enable XCP event info by protocol instead of A2L
        DBG_PRINT("DAQ_EVT_INFO,");
#endif
#ifdef XCP_ENABLE_CHECKSUM // Enable BUILD_CHECKSUM command
        DBG_PRINT("CHECKSUM,");
#endif
#ifdef XCP_ENABLE_INTERLEAVED // Enable interleaved command execution
        DBG_PRINT("INTERLEAVED,");
#endif
        DBG_PRINT(")\n");
    }
#endif // DBG_LEVEL

    gXcp.Queue = queueHandle;

#ifdef XCP_ENABLE_PROTOCOL_LAYER_ETH
#if XCP_PROTOCOL_LAYER_VERSION >= 0x0103

    // XCP server clock default description
    gXcp.ClockInfo.server.timestampTicks = XCP_TIMESTAMP_TICKS;
    gXcp.ClockInfo.server.timestampUnit = XCP_TIMESTAMP_UNIT;
    gXcp.ClockInfo.server.stratumLevel = XCP_STRATUM_LEVEL_UNKNOWN;
#ifdef XCP_DAQ_CLOCK_64BIT
    gXcp.ClockInfo.server.nativeTimestampSize = 8; // NATIVE_TIMESTAMP_SIZE_DLONG;
    gXcp.ClockInfo.server.valueBeforeWrapAround = 0xFFFFFFFFFFFFFFFFULL;
#else
    gXcp.ClockInfo.server.nativeTimestampSize = 4; // NATIVE_TIMESTAMP_SIZE_LONG;
    gXcp.ClockInfo.server.valueBeforeWrapAround = 0xFFFFFFFFULL;
#endif
#endif // XCP_PROTOCOL_LAYER_VERSION >= 0x0103
#ifdef XCP_ENABLE_PTP

    uint8_t uuid[8] = XCP_DAQ_CLOCK_UIID;
    memcpy(gXcp.ClockInfo.server.UUID, uuid, 8);

    DBG_PRINTF4("  ServerClock: ticks=%u, unit=%s, size=%u, UUID=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n\n", gXcp.ClockInfo.server.timestampTicks,
                (gXcp.ClockInfo.server.timestampUnit == DAQ_TIMESTAMP_UNIT_1NS) ? "ns" : "us", gXcp.ClockInfo.server.nativeTimestampSize, gXcp.ClockInfo.server.UUID[0],
                gXcp.ClockInfo.server.UUID[1], gXcp.ClockInfo.server.UUID[2], gXcp.ClockInfo.server.UUID[3], gXcp.ClockInfo.server.UUID[4], gXcp.ClockInfo.server.UUID[5],
                gXcp.ClockInfo.server.UUID[6], gXcp.ClockInfo.server.UUID[7]);

    // If the server clock is PTP synchronized, both origin and local timestamps are considered to be the same.
    gXcp.ClockInfo.relation.timestampLocal = 0;
    gXcp.ClockInfo.relation.timestampOrigin = 0;

    // XCP grandmaster clock default description
    gXcp.ClockInfo.grandmaster.timestampTicks = XCP_TIMESTAMP_TICKS;
    gXcp.ClockInfo.grandmaster.timestampUnit = XCP_TIMESTAMP_UNIT;
    gXcp.ClockInfo.grandmaster.nativeTimestampSize = 8; // NATIVE_TIMESTAMP_SIZE_DLONG;
    gXcp.ClockInfo.grandmaster.valueBeforeWrapAround = 0xFFFFFFFFFFFFFFFFULL;
    gXcp.ClockInfo.grandmaster.stratumLevel = XCP_STRATUM_LEVEL_UNKNOWN;
    gXcp.ClockInfo.grandmaster.epochOfGrandmaster = XCP_EPOCH_ARB;
    if (ApplXcpGetClockInfoGrandmaster(gXcp.ClockInfo.grandmaster.UUID, &gXcp.ClockInfo.grandmaster.epochOfGrandmaster, &gXcp.ClockInfo.grandmaster.stratumLevel)) {
        DBG_PRINTF5("  GrandmasterClock: UUID=%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X stratumLevel=%u, epoch=%u\n", gXcp.ClockInfo.grandmaster.UUID[0],
                    gXcp.ClockInfo.grandmaster.UUID[1], gXcp.ClockInfo.grandmaster.UUID[2], gXcp.ClockInfo.grandmaster.UUID[3], gXcp.ClockInfo.grandmaster.UUID[4],
                    gXcp.ClockInfo.grandmaster.UUID[5], gXcp.ClockInfo.grandmaster.UUID[6], gXcp.ClockInfo.grandmaster.UUID[7], gXcp.ClockInfo.grandmaster.stratumLevel,
                    gXcp.ClockInfo.grandmaster.epochOfGrandmaster);
        DBG_PRINT5("  ClockRelation: local=0, origin=0\n");
    }
#endif // PTP
#endif // XCP_ENABLE_PROTOCOL_LAYER_ETH

    DBG_PRINT3("Start XCP protocol layer\n");

    gXcp.SessionStatus |= SS_STARTED;

    // Resume DAQ
#ifdef XCP_ENABLE_DAQ_RESUME
    if (resumeMode) {
        if (XcpCheckPreparedDaqLists()) {
            /* Goto temporary disconnected mode and start all selected DAQ lists */
            gXcp.SessionStatus |= SS_RESUME;
            /* Start DAQ */
            XcpStartSelectedDaqLists();
            // @@@@ TODO Send an event message to indicate resume mode

#ifdef DBG_LEVEL
            if (DBG_LEVEL != 0) {
                printf("Started in resume mode\n");
                for (uint16_t daq = 0; daq < gXcp.DaqLists->daq_count; daq++) {
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

// Reset XCP protocol layer back to not init state
void XcpReset(void) {

    if (!isActivated()) { // Ignore
        return;
    }

    free(gXcp.DaqLists);
    gXcp.DaqLists = NULL;
#ifdef XCP_ENABLE_CALSEG_LIST
    XcpFreeCalSegList();
#endif

    XcpInit(true); // Reset XCP state
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

    gXcp.CmdLast = CRO_CMD;
    gXcp.CmdLast1 = CRO_LEVEL_1_COMMAND_CODE;
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
        printf(" GET_DAQ_EVENT_INFO event=%u\n", CRO_GET_DAQ_EVENT_INFO_EVENT);
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
        printf(" SET_DAQ_LIST_MODE daq=%u, mode=%02Xh, eventchannel=%u\n", CRO_SET_DAQ_LIST_MODE_DAQ, CRO_SET_DAQ_LIST_MODE_MODE, CRO_SET_DAQ_LIST_MODE_EVENTCHANNEL);
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
        switch (gXcp.CmdLast) {

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

        case CC_GET_DAQ_RESOLUTION_INFO:
            printf(" <- mode=%02Xh, , ticks=%02Xh\n", CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_MODE, CRM_GET_DAQ_RESOLUTION_INFO_TIMESTAMP_TICKS);
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
            switch (gXcp.CmdLast1) {

            case CC_GET_VERSION:
                printf(" <- protocol layer version: major=%02Xh/minor=%02Xh, transport layer version: major=%02Xh/minor=%02Xh\n", CRM_GET_VERSION_PROTOCOL_VERSION_MAJOR,
                       CRM_GET_VERSION_PROTOCOL_VERSION_MINOR, CRM_GET_VERSION_TRANSPORT_VERSION_MAJOR, CRM_GET_VERSION_TRANSPORT_VERSION_MINOR);
                break;
            }
            break;
#endif

        case CC_TRANSPORT_LAYER_CMD:
            switch (gXcp.CmdLast1) {
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
            }
            break;

        default:
            if (DBG_LEVEL >= 5) {
                printf(" <- OK\n");
            }
            break;

        } /* switch */
    }
}

static void XcpPrintDaqList(uint16_t daq) {

    if (gXcp.DaqLists == NULL || daq >= gXcp.DaqLists->daq_count)
        return;

    printf("  DAQ %u:", daq);
    printf(" eventchannel=%04Xh,", DaqListEventChannel(daq));
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
            printf("      ODT_ENTRY %u (%u): %u:%08X,%u\n", e - DaqListOdtTable[i].first_odt_entry, e, OdtEntryAddrExtTable[e], OdtEntryAddrTable[e], OdtEntrySizeTable[e]);
#else
            printf("      ODT_ENTRY %u (%u): %08X,%u\n", e - DaqListOdtTable[i].first_odt_entry, e, OdtEntryAddrTable[e], OdtEntrySizeTable[e]);
#endif
        }

    } /* j */
}

#endif
