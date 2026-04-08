/*****************************************************************************
| File:
|   cal.c
|
| Description:
|   XCPlite calibration segment RCU
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
|***************************************************************************/

#include "xcp_cfg.h"    // XCP protocol layer configuration parameters (XCP_xxx)
#include "xcplib_cfg.h" // for OPTION_xxx
#include "xcptl_cfg.h"  // XCP transport layer configuration parameters (XCPTL_xxx)

#include "cal.h"

#include <assert.h>   // for assert
#include <inttypes.h> // for PRIx32, PRIu64
#include <stdarg.h>   // for va_list, va_start, va_arg, va_end
#include <stdbool.h>  // for bool
#include <stdint.h>   // for uint8_t, uint16_t, ...
#include <stdio.h>    // for printf
#include <stdlib.h>   // for size_t, NULL, abort
#include <string.h>   // for memcpy, memset

#include "dbg_print.h"   // for DBG_LEVEL, DBG_PRINT3, DBG_PRINTF4, DBG...
#include "persistence.h" // for XcpBinFreezeCalSeg
#include "platform.h"    // for atomics OS abstraction
#include "xcp.h"         // XCP protocol definitions
#include "xcplite.h"     // XCP protocol layer interface functions

/**************************************************************************/
// State

#ifdef OPTION_SHM_MODE // extern declarations for gXcpData

extern tXcpData *gXcpData;                   // Shared state in SHM mode
#define shared (*(const tXcpData *)gXcpData) // Shortcut for read only access to the XCP singleton data
#define shared_mut (*gXcpData)               // Shortcut for mutable access to the XCP singleton data
#define shared_mut_safe (*gXcpData)          // Shortcut for mutable access to the XCP singleton data

#else

extern tXcpData gXcpData;
#define shared (*(const tXcpData *)&gXcpData) // Shortcut for read only access to the XCP singleton data
#define shared_mut gXcpData                   // Shortcut for mutable access to the XCP singleton data
#define shared_mut_safe gXcpData              // Shortcut for mutable access to the XCP singleton data

#endif

// Local state, not shared between processes in SHM mode
// The only local state used is the mutex to protect the calibration segment list and the MTA pointer in XcpGetSegInfo
// @@@@ TODO: Move the XCP protocol specific parts back to xcplite.c
extern tXcpLocalData gXcpLocalData;
// #define local (*(const tXcpLocalData *)&gXcpLocalData) // Read-only access to process-local state
#define local_mut gXcpLocalData // Mutable access to process-local state

// @@@@ TODO: Deactivate mode in C API
// Calibration instrumentation can currently not be deactivate at runtime, it has to be done at compile time
// An attempt to create a calibration segment or block with XCP not initialized (XcpInit not called) will fail and assert

// Global state checks
#ifdef OPTION_SHM_MODE // different isActivated check
#define isActivated() (gXcpData != NULL && 0 != (gXcpData->session_status & SS_ACTIVATED))
#else
#define isActivated() (0 != (gXcpData.session_status & SS_ACTIVATED))
#endif

/**************************************************************************/
// Command response buffer access shortcuts

// Command response buffer access shortcuts
#define CRM_LEN shared_mut.crm_len
#define CRM shared_mut.crm
#define CRM_BYTE(x) (shared_mut.crm.b[x])
#define CRM_WORD(x) (shared_mut.crm.w[x])
#define CRM_DWORD(x) (shared_mut.crm.dw[x])

/**************************************************************************/
/* Calibration segments                                                   */
/**************************************************************************/

#ifdef XCP_ENABLE_CALSEG_LIST

#if XCP_MAX_CALSEG_NAME & 1 == 0 || XCP_MAX_CALSEG_NAME >= 128
#error "XCP_MAX_CALSEG_NAME must be <128 and odd for null termination"
#endif

/**************************************************************************/
// Forward declarations

static void *XcpCalMemAlloc_(size_t size);
static bool XcpInitCalSeg_(tXcpCalSeg *calseg, const char *name, const void *default_page, FILE *default_page_file, uint16_t page_size, bool memory_segment);
static tXcpCalSegIndex XcpCreateCalSeg_(const char *name, bool lookup, const void *default_page, FILE *default_page_file, uint16_t page_size, bool memory_segment);

/**************************************************************************/

// Initialize the calibration segment list
void XcpInitCalSegList(void) {
    atomic_store_explicit(&shared_mut_safe.cal_seg_list.count, 0, memory_order_relaxed);
    atomic_store_explicit(&shared_mut_safe.cal_seg_list.cal_mem_used, 0, memory_order_relaxed);
    shared_mut.cal_seg_list.memory_segment_count = 0;
    shared_mut.cal_seg_list.write_delayed = false;
    mutexInit(&local_mut.cal_seg_list_mutex, false, 0); // Non-recursive mutex, no spin count

    DBG_PRINTF6("Calibration segment list initialized, sizeof(tXcpCalSegHeader) = %zu, sizeof(tXcpCalSegList) = %zu\n", sizeof(tXcpCalSegHeader), sizeof(tXcpCalSegList));
}

// Thread-safe bump allocator for calibration segment memory
// Memory is only freed as a whole when the calibration segment list is destroyed
static void *XcpCalMemAlloc_(size_t size) {
    assert(size > 0);
    assert((size % XCP_CALPAGE_ALIGNMENT) == 0);
    assert(size <= (size_t)XCP_CAL_MEM_SIZE);
    assert((uintptr_t)shared.cal_seg_list.cal_mem.pool % XCP_CALPAGE_ALIGNMENT == 0);
    uint_fast32_t old_used, new_used;
    do {
        old_used = atomic_load_explicit(&shared.cal_seg_list.cal_mem_used, memory_order_relaxed);
        new_used = old_used + (uint_fast32_t)size;
        if (new_used > XCP_CAL_MEM_SIZE) {
            DBG_PRINT_ERROR("XCP calibration memory pool exhausted\n");
            return NULL;
        }
    } while (!atomic_compare_exchange_weak_explicit(&shared_mut_safe.cal_seg_list.cal_mem_used, &old_used, new_used, memory_order_relaxed, memory_order_relaxed));
    return &shared_mut_safe.cal_seg_list.cal_mem.pool[old_used];
}

// Free the calibration segment list
void XcpDeinitCalSegList(void) {

#ifdef OPTION_SHM_MODE // not implemented clear pending state
    // @@@@ TODO: Deinit calibration segment list in SHM mode, clear pending states
#endif

    // Just destroy the local mutex
    mutexDestroy(&local_mut.cal_seg_list_mutex);
}

// Get the number of calibration segments
uint16_t XcpGetCalSegCount(void) {
    if (!isActivated()) {
        assert(0);
        return 0;
    }
    return (uint16_t)atomic_load_explicit(&shared.cal_seg_list.count, memory_order_acquire);
}

// Get the number of memory segments
uint8_t XcpGetMemSegCount(void) {
    if (!isActivated()) {
        assert(0);
        return XCP_UNDEFINED_CALSEG_NUM;
    }
    return (uint8_t)(shared.cal_seg_list.memory_segment_count & 0xFF);
}

// Get a pointer to the calibration segment struct of calseg index
const tXcpCalSeg *XcpGetCalSeg(tXcpCalSegIndex calseg_index) {
    if (calseg_index >= XcpGetCalSegCount()) {
        assert(0);
        return NULL;
    }
    return CalSegPtr(calseg_index);
}

// Get the index of a calibration segment by name
// Lock-free, thread-safe
tXcpCalSegIndex XcpFindCalSeg(const char *name) {

    uint16_t n = XcpGetCalSegCount();

    // Iterate cal_seg_list cal_seg_list
    for (tXcpCalSegIndex i = 0; i < n; i++) {
        const tXcpCalSeg *calseg = CalSegPtr(i);
        assert(calseg != NULL);
#ifdef OPTION_SHM_MODE // XcpFindCalSeg only among entries owned by this application process
// In SHM mode, match only entries owned by this application process — different apps may use the same name
#ifdef XCP_ENABLE_EPK_CALSEG
        // Not for index 0, which is reserved for the EPK segment
        if (i > 0 && calseg->h.app_id != XcpShmGetAppId())
            continue; // Skip entries owned by other processes
#else
        if (calseg->h.app_id != XcpShmGetAppId())
            continue; // Skip entries owned by other processes
#endif
        if (strcmp(calseg->h.name, name) == 0)
            return i;
#else
        if (strcmp(calseg->h.name, name) == 0)
            return i;
#endif
    }
    return XCP_UNDEFINED_CALSEG; // Not found
}

// Get the index of a calibration segment by address (inside of the default page)
// Lock-free, thread-safe
#if defined(XCP_ENABLE_ABS_ADDRESSING) && XCP_ADDR_EXT_ABS == 0x00
tXcpCalSegIndex XcpFindCalSegByAddr(uint8_t *addr) {
    // Iterate cal_seg_list cal_seg_list
    uint16_t n = XcpGetCalSegCount();
    for (tXcpCalSegIndex i = 0; i < n; i++) {
        const tXcpCalSeg *calseg = CalSegPtr(i);
        assert(calseg != NULL);
        if (addr >= calseg->h.default_page_ptr && addr < calseg->h.default_page_ptr + calseg->h.size) {
            return i;
        }
    }
    return XCP_UNDEFINED_CALSEG; // Not found
}
#endif

// Get the calibration segment index by memory segment number, returns XCP_UNDEFINED_CALSEG if not found
// Not all calibrations segments can be controlled by XCP
// XCP uses a uin8_t number to identify memory segments (tXcpCalSegNumber), while the calibration segment index (tXcpCalSegIndex) is uint16_t
// Lock-free, thread-safe
tXcpCalSegIndex XcpGetCalSegIndex(tXcpCalSegNumber segment_number) {
    // Iterate cal_seg_list cal_seg_list
    uint16_t n = XcpGetCalSegCount();
    for (uint16_t i = 0; i < n; i++) {
        const tXcpCalSeg *calseg = CalSegPtr(i);
        assert(calseg != NULL);
        if (calseg->h.calseg_number == segment_number) {
            return i;
        }
    }
    return XCP_UNDEFINED_CALSEG; // Not found
}

// Get the memory segment number of a calibration segment, returns 0xFF if not found
// Lock-free, thread-safe
tXcpCalSegNumber XcpGetCalSegNumber(tXcpCalSegIndex calseg_index) {
    if (calseg_index >= XcpGetCalSegCount())
        return XCP_UNDEFINED_CALSEG_NUM;
    return CalSegPtr(calseg_index)->h.calseg_number;
}

// Get the name of the calibration segment
const char *XcpGetCalSegName(tXcpCalSegIndex calseg_index) {
    if (calseg_index >= XcpGetCalSegCount()) {
        assert(0);
        return NULL;
    }
    return CalSegPtr(calseg_index)->h.name;
}

// Get the size of a calibration segment
uint16_t XcpGetCalSegSize(tXcpCalSegIndex calseg_index) {
    if (calseg_index >= XcpGetCalSegCount()) {
        assert(0);
        return 0;
    }
    return CalSegPtr(calseg_index)->h.size;
}

// Get the XCP/A2L address of a calibration segment
uint32_t XcpGetCalSegBaseAddress(tXcpCalSegIndex calseg_index) {
    if (calseg_index >= XcpGetCalSegCount()) {
        assert(0);
        return 0;
    }
#if XCP_ADDR_EXT_SEG == 0x00
    // Memory segments are addressed in relative mode
    return XcpAddrEncodeSegIndex(calseg_index, 0);
#else
#if defined(XCP_ENABLE_ABS_ADDRESSING) && XCP_ADDR_EXT_ABS != 0x00
#error "XCP_ADDR_EXT_ABS must be 0x00"
#endif
    return XcpAddrEncodeAbs(CalSegPtr(calseg_index)->h.default_page_ptr);
#endif
}

// Create a preloaded calibration segment, which is initialized with data from the binary persistence file at startup
// Return the default page to the caller for initialization with the preloaded data, or NULL on error (e.g. wrong index, wrong memory segment number, out of memory, etc.)
tXcpCalSegIndex XcpCreateCalSegPreloaded(const char *name, uint8_t app_id, uint16_t page_size, tXcpCalSegIndex index, tXcpCalSegNumber number, FILE *file, uint32_t file_pos) {

    // Create a calibration segment with given name, index and number without initial value to be loaded from file
    tXcpCalSegIndex seg_index = XcpCreateCalSeg_(name, false /* lookup */, NULL, file, page_size, number != XCP_UNDEFINED_CALSEG_NUM);
    if (seg_index != index) {
        assert(0);
        return XCP_UNDEFINED_CALSEG;
    }
    tXcpCalSeg *seg = CalSegPtrMut(seg_index);
    if (seg->h.calseg_number != number) {
        assert(0);
        return XCP_UNDEFINED_CALSEG;
    }

#ifdef XCP_ENABLE_CAL_PERSISTENCE
    // Mark the segment as preloaded
    seg->h.mode = PAG_PROPERTY_PRELOAD;
    seg->h.file_pos = file_pos; // Save the position of the segment page data in the file
    seg->h.app_id = app_id;     // Save the application ID
#else
    (void)app_id;   // Unused parameter
    (void)file_pos; // Unused parameter
#endif

    return seg_index;
}

// Create a calibration segment if not already exists
// Thread safe
// Returns the handle or XCP_UNDEFINED_CALSEG when out of memory
// Calibration segments have 2 pages and can be controlled via XCP through their memory segment number (XcpGetCalSegNumber)
// The number of memory segments is limited to 255
tXcpCalSegIndex XcpCreateCalSeg(const char *name, const void *default_page, uint16_t page_size) {
    // @@@@ TODO: Create a way to let the user call functions CreateCalSeg/Lock/Unlock without initializing the calibration segment list
    // tXcpCalSegIndex could be pointer size or introduce a simple array of default page pointers ??
    if (!isActivated()) {
        return XCP_UNDEFINED_CALSEG;
    }
    return XcpCreateCalSeg_(name, true /* lookup */, default_page, NULL, page_size, true);
}

// Create a calibration block if not already exists
// Thread safe
// Returns the handle or XCP_UNDEFINED_CALSEG when out of memory
// Calibration blocks don't have a memory segment and the related XCP features
tXcpCalSegIndex XcpCreateCalBlk(const char *name, const void *default_page, uint16_t page_size) {
    if (!isActivated()) {
        return XCP_UNDEFINED_CALSEG;
    }
    return XcpCreateCalSeg_(name, true /* lookup */, default_page, NULL, page_size, false);
}

// Helper for XcpCreateCalSeg_ to register a calibration segment in the calibration segment list
// Note: This is one of the compromises we make for this simple RCU: Uses a mutex for thread-safe creation
// Returns the list index
static tXcpCalSegIndex XcpRegisterCalSeg_(tXcpCalSeg *c) {

    // Mutex to make the operation list[count++]=offset atomic
    mutexLock(&local_mut.cal_seg_list_mutex);

    // Get the index for a new segment
    uint16_t calseg_index = XcpGetCalSegCount();

    // Check if out of list space
    if (calseg_index >= XCP_MAX_CALSEG_COUNT - 1) {
        mutexUnlock(&local_mut.cal_seg_list_mutex);
        DBG_PRINT_ERROR("too many calibration segments, increase XCP_MAX_CALSEG_COUNT\n");
        return XCP_UNDEFINED_CALSEG;
    }

    // Store the new segments memory offset in the list
    shared_mut_safe.cal_seg_list.offset[calseg_index] = (uint32_t)((uint8_t *)c - shared_mut_safe.cal_seg_list.cal_mem.pool);

    // Publish the new entry
    // Release store ensures all preceding writes (name, size, etc.) are visible to any thread that iterates on the calseg list
    atomic_store_explicit(&shared_mut_safe.cal_seg_list.count, calseg_index + 1, memory_order_release);
    mutexUnlock(&local_mut.cal_seg_list_mutex);
    return (tXcpCalSegIndex)calseg_index;
}

// Helper for XcpCreateCalSeg, XcpCreateCalBlk and XcpCreateCalSegPreloaded to create a calibration block with given page count (0 for blk, 2 for seg)
// A segment with this name may already exist, when preloaded - then it is reinitialized
// Lookup for existence can be skipped if lookup is false, which is the case for preloaded segments, because they have a predefined index and are loaded in order
// If default_page is NULL, it is a preloaded segment, the caller will initialize the default page
static tXcpCalSegIndex XcpCreateCalSeg_(const char *name, bool lookup, const void *default_page, FILE *default_page_file, uint16_t page_size, bool memory_segment) {

    tXcpCalSeg *calseg = NULL;
    tXcpCalSegIndex calseg_index = XCP_UNDEFINED_CALSEG;

    // If lookup is enabled, check for existence (in the calibration segment list for the current application)
    if (lookup) {
        // Check if the segment does not already exist, only if not create a new one
        // @@@@ TODO: Optimize XcpFindCalSeg, because this is the once execution check
        calseg_index = XcpFindCalSeg(name);
    }

    // Create, if not exists
    if (calseg_index == XCP_UNDEFINED_CALSEG) {

        // Align page size to XCP_CALPAGE_ALIGNMENT bytes for better performance
        uint16_t aligned_page_size = (page_size + XCP_CALPAGE_ALIGNMENT - 1) & ~(XCP_CALPAGE_ALIGNMENT - 1);

        // Allocate memory for the new segment from the embedded pool using the thread-safe bump allocator
        // Header + DEFAULT page + ECU page + XCP page + RCU swap page
        calseg = (tXcpCalSeg *)XcpCalMemAlloc_(sizeof(tXcpCalSegHeader) + 4 * (size_t)aligned_page_size);
        DBG_PRINTF3("Create CalSeg '%s' size=%u, memory_segment=%u\n", name, page_size, memory_segment);
        if (!XcpInitCalSeg_(calseg, name, default_page, default_page_file, page_size, memory_segment)) {
            return XCP_UNDEFINED_CALSEG;
        }
        if ((calseg_index = XcpRegisterCalSeg_(calseg)) == XCP_UNDEFINED_CALSEG) {
            return XCP_UNDEFINED_CALSEG;
        }

    } else {
        // Segment already exists
        calseg = CalSegPtrMut(calseg_index);
        DBG_PRINTF6("Calibration segment '%s' already exists\n", calseg->h.name);
    }

    // Must have the correct size
    if (page_size != calseg->h.size) {
        DBG_PRINTF_ERROR("Calibration segment '%s' already exists with wrong size %u, expected %u\n", calseg->h.name, calseg->h.size, page_size);
        return XCP_UNDEFINED_CALSEG;
    }

    // Special case are preloaded segments, which are loaded from the binary calibration segment image file on startup, and have the PAG_PROPERTY_PRELOAD bit set
#ifdef XCP_ENABLE_CAL_PERSISTENCE
    // Check if this is a preloaded segment
    // Preloaded segments have an initialized default page with data from the binary calibration segment image file on startup
    // The PAG_PROPERTY_PRELOAD bit is set to indicate this
    if ((calseg->h.mode & PAG_PROPERTY_PRELOAD) != 0) {

        assert(default_page_file == NULL); // For preloaded segments, the default page is initialized
        assert(strcmp(calseg->h.name, name) == 0);
        assert(page_size == calseg->h.size);

        // Nothing to do
        // Complete the initialization of the preloaded segment
        DBG_PRINTF5("CalSeg '%s' finalized from preloaded, index=%u, size=%u\n", calseg->h.name, calseg_index, calseg->h.size);
        if (default_page != NULL && memcmp(CalSegDefaultPage(calseg), default_page, page_size) != 0) {
            DBG_PRINTF3(ANSI_COLOR_YELLOW "Persisted (different) default page for %s loaded from binary persistence file! Update EPK to reset!\n" ANSI_COLOR_RESET, calseg->h.name);
        }

        calseg->h.mode = 0; // Clear PAG_PROPERTY_PRELOAD, reset mode flags (freeze not enabled, set by XCP command SET_SEGMENT_MODE)
        return calseg_index;
    }
#endif //   XCP_ENABLE_CAL_PERSISTENCE

#ifdef XCP_ENABLE_TEST_CHECKS
    // For an already existing segment, the default page should be the same as the existing one, because we assume it has static lifetime
    if (default_page != NULL && memcmp(default_page, CalSegDefaultPage(calseg), page_size) != 0) {
        DBG_PRINTF_WARNING("Calibration segment '%s' already exists with a different default page\n", calseg->h.name);
        // return XCP_UNDEFINED_CALSEG;
    }
#endif

    return calseg_index;
}

// Helper function to initialize a new calibration segment
// Thread-safe
// Note that preloaded calibration segments have an already existing initialized default page content from loading the persistence file
// This is indicated by default_page = NULL
static bool XcpInitCalSeg_(tXcpCalSeg *calseg, const char *name, const void *default_page, FILE *default_page_file, uint16_t page_size, bool memory_segment) {

    tXcpCalSeg *c = calseg; // Alias
    assert(c != NULL);

    // Align page size to 8 bytes for better performance
    uint16_t aligned_page_size = (page_size + XCP_CALPAGE_ALIGNMENT - 1) & ~(XCP_CALPAGE_ALIGNMENT - 1);

    size_t name_len = strnlen(name, XCP_MAX_CALSEG_NAME);
    memcpy(c->h.name, name, name_len);
    c->h.name[name_len] = '\0';
    c->h.size = page_size;
    // In SHM mode, assign the app_id to the segment
#ifdef OPTION_SHM_MODE // store application id in tXcpCalSeg
    c->h.app_id = XcpShmGetAppId();
#endif

    // Create a memory segment with a memory segment number, which can be used for XCP access and has the related XCP features
    if (memory_segment) {
        if (shared.cal_seg_list.memory_segment_count >= 0xFF) {
            DBG_PRINT_ERROR("Too many memory segments for calibration segments\n");
            assert(false);
            return false;
        }
        // @@@@ TODO: Memory segment number allocation not protected by mutex
        c->h.calseg_number = (tXcpCalSegNumber)shared_mut_safe.cal_seg_list.memory_segment_count++;
    } else {
        c->h.calseg_number = XCP_UNDEFINED_CALSEG_NUM; // Not a memory segment
    }

    // Initialize the default page
    // Standard: default page pointer provided by the caller
    if (default_page != NULL) {
        assert(default_page_file == NULL);
        memcpy(CalSegDefaultPage(c), default_page, page_size); // Copy default page to the allocated memory buffer
#if defined(XCP_ENABLE_ABS_ADDRESSING) && XCP_ADDR_EXT_ABS == 0x00
        // May have static lifetime, so keep the pointer in non SHM mode in addition to the copy
        c->h.default_page_ptr = (uint8_t *)default_page;
#endif
    }

    // Preload: Caller wants to create a preinitialized, preloaded segment from file
#ifdef XCP_ENABLE_CAL_PERSISTENCE
    else if (default_page_file != NULL) {
        // Load the default page content from the binary persistence file
        size_t read = fread(CalSegDefaultPage(c), 1, page_size, default_page_file);
        if (read != page_size) {
            DBG_PRINTF_ERROR("Failed to read the default page content from file, expected %u bytes, got %zu bytes\n", page_size, read);
            assert(false);
            return false;
        }
#if defined(XCP_ENABLE_ABS_ADDRESSING) && XCP_ADDR_EXT_ABS == 0x00
        c->h.default_page_ptr = NULL;
#endif
    }
#endif //   XCP_ENABLE_CAL_PERSISTENCE
    else {
        DBG_PRINT_ERROR("No default page provided for calibration segment\n");
        assert(false);
        return false;
    }

    // Reset RCU, XCP passive mode
    c->h.xcp_page = XCP_CALSEG_NO_PAGE;
    c->h.ecu_page = XCP_CALSEG_NO_PAGE;
    atomic_store_explicit(&c->h.free_page, XCP_CALSEG_NO_PAGE, memory_order_relaxed);
    c->h.free_page_hazard = false;
    atomic_store_explicit(&c->h.ecu_page_next, XCP_CALSEG_NO_PAGE, memory_order_relaxed);
    c->h.write_pending = false;
    c->h.xcp_access = XCP_CALPAGE_DEFAULT_PAGE;                                              // Default page for XCP access if XCP is not activated
    atomic_store_explicit(&c->h.ecu_access, XCP_CALPAGE_DEFAULT_PAGE, memory_order_relaxed); // Default page for ECU access if XCP is not activated
    atomic_store_explicit(&c->h.lock_count, 0, memory_order_relaxed);

    // Init RCU if XCP is activated
    if (isActivated()) {

        // Initialize the ECU working page (RAM page)
        c->h.ecu_page = ECU_PAGE_OFFSET(aligned_page_size);
        memcpy(CalSegEcuPage(c), CalSegDefaultPage(c), page_size); // Copy default page to ECU page

        // Initialize the XCP working page (RAM page)
        c->h.xcp_page = XCP_PAGE_OFFSET(aligned_page_size);
        memcpy(CalSegXcpPage(c), CalSegDefaultPage(c), page_size); // Copy default page to working page

        // Allocate a free uninitialized page
        atomic_store_explicit(&c->h.free_page, (uint_fast32_t)FREE_PAGE_OFFSET(aligned_page_size), memory_order_relaxed);

        // New ECU page version not updated
        atomic_store_explicit(&c->h.ecu_page_next, (uint_fast32_t)c->h.ecu_page, memory_order_relaxed);

#ifdef XCP_START_ON_REFERENCE_PAGE
        // Enable access to the reference page
        c->h.xcp_access = XCP_CALPAGE_DEFAULT_PAGE;                                              // Default page for XCP access is the reference page
        atomic_store_explicit(&c->h.ecu_access, XCP_CALPAGE_DEFAULT_PAGE, memory_order_relaxed); // Default page for ECU access is the reference page
#else
        // Enable access to the working page
        c->h.xcp_access = XCP_CALPAGE_WORKING_PAGE;                                              // Default page for XCP access is the working page
        atomic_store_explicit(&c->h.ecu_access, XCP_CALPAGE_WORKING_PAGE, memory_order_relaxed); // Default page for ECU access is the working page
#endif
    } // isActivated()

    return true;
}

// Lock a calibration segment and return a pointer to the ECU page
// Thread safe
// Shared atomic state is lock_count, ecu_page_next, free_page, ecu_access
// Shared non atomic is ecu_page, free_page_hazard, release on free_page
const uint8_t *XcpLockCalSeg(tXcpCalSegIndex calseg_index) {

    if (!isActivated()) {
        DBG_PRINT_ERROR("XCP not activated\n");
        // @@@@ TODO: Deactivate mode in C API
        assert(0);
        return NULL;
    }
    if (calseg_index >= atomic_load_explicit(&shared.cal_seg_list.count, memory_order_relaxed)) {
        DBG_PRINTF_ERROR("Invalid index %u\n", calseg_index);
        assert(0);
        return NULL; // Uninitialized or invalid calseg_index
    }

    tXcpCalSeg *c = CalSegPtrMut(calseg_index);

    // Update
    // Increment the lock count
    if (0 == atomic_fetch_add_explicit(&c->h.lock_count, 1, memory_order_relaxed)) {

        // Update if there is a new page version, free the old page
        uint32_t ecu_page_next = (uint32_t)atomic_load_explicit(&c->h.ecu_page_next, memory_order_acquire);
        uint32_t ecu_page = c->h.ecu_page;
        if (ecu_page != ecu_page_next) {
            c->h.free_page_hazard = true; // Free page might be acquired by some other thread, since we got the first lock on this segment
            c->h.ecu_page = ecu_page_next;
            atomic_store_explicit(&c->h.free_page, (uint_fast32_t)ecu_page, memory_order_release);
        } else {
            c->h.free_page_hazard = false; // There was no lock and no need for update, free page must be safe now, if there is one
        }
    }

    // Return the active ECU page (RAM or FLASH)
    if (atomic_load_explicit(&c->h.ecu_access, memory_order_relaxed) != XCP_CALPAGE_WORKING_PAGE) {
        return CalSegDefaultPage(c);
    } else {
        return CalSegEcuPage(c);
    }
}

// Unlock a calibration segment
// Thread safe
// Shared state is lock_count
uint8_t XcpUnlockCalSeg(tXcpCalSegIndex calseg_index) {

    if (!isActivated()) {
        DBG_PRINT_ERROR("XCP not activated\n");
        // @@@@ TODO: Deactivate mode in C API
        assert(0);
        return 0;
    }
    if (calseg_index >= atomic_load_explicit(&shared.cal_seg_list.count, memory_order_relaxed)) {
        DBG_PRINTF_ERROR("Invalid index %u\n", calseg_index);
        assert(0);
        return 0; // Uninitialized or invalid calseg_index
    }

    uint8_t oldLockCount = (uint8_t)atomic_fetch_sub_explicit(&CalSegPtrMut(calseg_index)->h.lock_count, 1, memory_order_relaxed); // Decrement the lock count
    assert(oldLockCount > 0);                                                                                                      // Calling XcpUnlockCalSeg without a prior lock
    return oldLockCount;
}

// XCP client memory read
// Read xcp or default page
// Read ecu page is not supported, calibration changes might be stale
// Note: This is one of the compromises we make for this simple RCU: XCP read operations may not reflect the current state of the ECU, actual calibration changes may happen later
// Single threaded function, called from XCP server thread !!!
uint8_t XcpCalSegReadMemory(uint32_t src, uint16_t size, uint8_t *dst) {

    // Decode the source address into calibration segment and offset
    uint16_t calseg_index = XcpAddrDecodeSegNumber(src); // Get the calibration segment number from the address
    uint16_t offset = XcpAddrDecodeSegOffset(src);       // Get the offset within the calibration segment

    if (calseg_index >= XcpGetCalSegCount()) {
        DBG_PRINTF_ERROR("invalid calseg index %u\n", calseg_index);
        return CRC_ACCESS_DENIED;
    }
    const tXcpCalSeg *c = CalSegPtr(calseg_index);
    assert(c != NULL);
    if (offset + size > c->h.size) {
        DBG_PRINTF_ERROR("out of bound calseg read access (addr=%08X, size=%u)\n", src, size);
        return CRC_ACCESS_DENIED;
    }

    memcpy(dst, c->h.xcp_access != XCP_CALPAGE_WORKING_PAGE ? CalSegDefaultPage(c) + offset : CalSegXcpPage(c) + offset, size);
    return CRC_CMD_OK;
}

// Publish a modified calibration segment
// Option to wait for this, or return unsuccessful with CRC_CMD_PENDING
// Wait timeout is XCP_CALSEG_AQUIRE_FREE_PAGE_TIMEOUT in ms
// Single threaded function, called from XcpCalSegPublishAll or XcpCalSegWriteMemory in the XCP server thread
static uint8_t XcpCalSegPublish(tXcpCalSeg *c, bool wait) {
    // Try allocate a new xcp page
    // In a multithreaded consumer use case, we must be sure the free page is really not in use anymore
    // We simply wait until all threads are updated, this is theoretically not free of starvation, but calibration changes are slow
    // Note: This is one of the compromises we make for this simple RCU: Calibration changes are delayed and dependand on calls to XcpLockCalSeg or XcpPublishAll
    // Acquire/release semantics with XcpCalSegLock on the free page pointer
    uint32_t free_page = (uint32_t)atomic_load_explicit(&c->h.free_page, memory_order_acquire);
    if (wait) {
        // Wait and delay the XCP server receive thread, until a free page becomes available
        for (int timeout = 0; timeout < XCP_CALSEG_AQUIRE_FREE_PAGE_TIMEOUT && (free_page == XCP_CALSEG_NO_PAGE || c->h.free_page_hazard); timeout++) {
            sleepUs(1000);
            free_page = (uint32_t)atomic_load_explicit(&c->h.free_page, memory_order_acquire);
        }
        if (free_page == XCP_CALSEG_NO_PAGE) {
            DBG_PRINTF_ERROR("Can not update calibration changes, timeout - calseg %s locked\n", c->h.name);
            return CRC_ACCESS_DENIED; // No free page available
        }
    } else {
        if (free_page == XCP_CALSEG_NO_PAGE || c->h.free_page_hazard) {
            DBG_PRINTF5("Can not update calibration changes of %s yet, %s\n", c->h.name, c->h.free_page_hazard ? "hazard" : "no free page");
            c->h.write_pending = true;
#ifdef TEST_ENABLE_DBG_METRICS
            gXcpWritePendingCount++;
#endif
            return CRC_CMD_PENDING; // No free page available
        }
    }

    // Acquire the free page
    uint32_t xcp_page_new = free_page;
    atomic_store_explicit(&c->h.free_page, (uint_fast32_t)XCP_CALSEG_NO_PAGE, memory_order_release);

    // Copy old xcp page to the new xcp page
    uint32_t xcp_page_old = c->h.xcp_page;
    memcpy(&c->b[xcp_page_new], &c->b[xcp_page_old], c->h.size); // Copy the xcp page
    c->h.xcp_page = xcp_page_new;

    // Publish the old xcp page
    // Acquire/release semantics with XcpCalSegLock on the ecu_page_next pointer
    c->h.write_pending = false; // No longer pending
    atomic_store_explicit(&c->h.ecu_page_next, (uint_fast32_t)xcp_page_old, memory_order_release);

    return CRC_CMD_OK;
}

// Publish all modified calibration segments
// Single threaded function, called from XCP server thread !!!
// Returns CRC_CMD_OK if all segments have been published
uint8_t XcpCalSegPublishAll(bool wait) {
    uint8_t res = CRC_CMD_OK;
    // If no atomic calibration operation is in progress
    if (!shared.cal_seg_list.write_delayed) {
        // Iterate cal_seg_list cal_seg_list
        uint16_t n = XcpGetCalSegCount();
        for (uint16_t i = 0; i < n; i++) {
            // @@@@ TODO: Could be called from foreign thread through XcpDisconnect, find a solution
            tXcpCalSeg *c = CalSegPtrMut(i);
            if (c->h.write_pending) {
                uint8_t res1 = XcpCalSegPublish(c, wait);
                if (res1 == CRC_CMD_OK) {
#ifdef TEST_ENABLE_DBG_METRICS
                    gXcpCalSegPublishAllCount++;
#endif
                } else {
                    res = res1;
                }
            }
        }
    }
    return res; // Return the last error code
}

// Memory write
// Write xcp page, error on write to default page or EPK segment
// Single threaded function, called from XCP server thread !!!
uint8_t XcpCalSegWriteMemory(uint32_t dst, uint16_t size, const uint8_t *src) {
    // Decode the destination address into calibration segment index and offset
    uint16_t calseg_index = XcpAddrDecodeSegNumber(dst);
    uint16_t offset = XcpAddrDecodeSegOffset(dst);

    if (calseg_index >= XcpGetCalSegCount()) {
        DBG_PRINTF_ERROR("invalid calseg number %u\n", calseg_index);
        return CRC_ACCESS_DENIED;
    }
    tXcpCalSeg *c = CalSegPtrMut(calseg_index);
    if (offset + size > c->h.size) {
        DBG_PRINTF_ERROR("out of bound calseg write access (number=%u, offset=%u, size=%u)\n", calseg_index, offset, size);
        return CRC_ACCESS_DENIED;
    }
    if (c->h.xcp_access != XCP_CALPAGE_WORKING_PAGE) {
        DBG_PRINTF_ERROR("attempt to write default page addr=%08X\n", dst);
        return CRC_ACCESS_DENIED;
    }

    // Update data in the current xcp page
    memcpy(CalSegXcpPage(c) + offset, src, size);

    // Calibration page RCU
    // If write delayed, we do not update the ECU page yet
    if (shared.cal_seg_list.write_delayed) {
        c->h.write_pending = true; // Modified
        return CRC_CMD_OK;
    } else {
#ifdef XCP_ENABLE_CALSEG_LAZY_WRITE
        // If lazy mode is enabled, try update, but we do not require to update the ECU page yet
        XcpCalSegPublish(c, false);
        return CRC_CMD_OK;
#else
        // If not lazy mode, we wait until a free page is available
        // This should succeed
        return XcpCalSegPublish(c, true);

#endif
    }
}

// Table 97 GET SEGMENT INFO command structure
// Returns information on a specific SEGMENT.
// If the specified SEGMENT is not available, ERR_OUT_OF_RANGE will be returned.
uint8_t XcpGetSegInfo(tXcpCalSegNumber segment_number, uint8_t mode, uint8_t seg_info, uint8_t map_index) {
    (void)map_index; // Mapping not supported

    tXcpCalSegIndex calseg_index = XcpGetCalSegIndex(segment_number);
    if (calseg_index == XCP_UNDEFINED_CALSEG) {
        DBG_PRINTF_ERROR("invalid segment number: %u\n", segment_number);
        return CRC_OUT_OF_RANGE;
    }
    const tXcpCalSeg *c = CalSegPtr(calseg_index);
    assert(c != NULL);
    // 0 - basic address info, 1 - standard info, 2 - mapping info
    switch (mode) {
    case 0: // Mode 0 - get address or length depending on seg_info
        CRM_LEN = CRM_GET_SEGMENT_INFO_LEN_MODE0;
        if (seg_info == 0) { // Get address
            CRM_GET_SEGMENT_INFO_BASIC_INFO = XcpGetCalSegBaseAddress(calseg_index);
            return CRC_CMD_OK;
        } else if (seg_info == 1) { // Get length
            CRM_GET_SEGMENT_INFO_BASIC_INFO = (uint32_t)c->h.size;
            return CRC_CMD_OK;
        } else if (seg_info == 2) { // Get segment name (Vector extension, name via MTA and upload)
            CRM_GET_SEGMENT_INFO_BASIC_INFO = (uint32_t)STRNLEN(c->h.name, XCP_MAX_CALSEG_NAME);
            // Segment name provided via upload
            local_mut.mta_ptr = (uint8_t *)c->h.name;
            local_mut.mta_ext = XCP_ADDR_EXT_PTR;
            return CRC_CMD_OK;
        } else {
            return CRC_OUT_OF_RANGE;
        }
        break;
    case 1: // Get standard info for this SEGMENT
        CRM_LEN = CRM_GET_SEGMENT_INFO_LEN_MODE1;
        CRM_GET_SEGMENT_INFO_MAX_PAGES = 2;
        CRM_GET_SEGMENT_INFO_ADDRESS_EXTENSION = 0; // Address extension for segments is always 0 (SEG or ABS address mode)
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

uint8_t XcpGetSegPageInfo(tXcpCalSegNumber segment_number, uint8_t page) {

    // Check if this segment number is valid
    tXcpCalSegIndex calseg_index = XcpGetCalSegIndex(segment_number);
    if (calseg_index == XCP_UNDEFINED_CALSEG) {
        DBG_PRINTF_ERROR("invalid segment number: %u\n", segment_number);
        return CRC_OUT_OF_RANGE;
    }

    CRM_LEN = CRM_GET_PAGE_INFO_LEN;

#ifdef XCP_ENABLE_CAL_PAGE // If GET/SET_CAL_PAGE enabled, support 2 pages
    if (page > 1)
        return CRC_PAGE_NOT_VALID;
#else
    if (page != 0)
        return CRC_PAGE_NOT_VALID;
#endif

    // All segments have the same page properties
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

#ifdef XCP_ENABLE_CAL_PAGE // Calibration page support with GET/SET_CAL_PAGE and COPY_CAL_PAGE enabled

// Get active ecu or xcp calibration page
// Note: XCP/A2L segment numbers are bytes, 0 is reserved for the EPK segment, tXcpCalSegIndex is the XCP/A2L segment number - 1
uint8_t XcpCalSegGetCalPage(tXcpCalSegNumber segment_number, uint8_t mode) {
    tXcpCalSegIndex calseg_index = XcpGetCalSegIndex(segment_number);
    if (calseg_index == XCP_UNDEFINED_CALSEG) {
        DBG_PRINTF_ERROR("invalid segment number: %u\n", segment_number);
        return XCP_CALPAGE_INVALID_PAGE;
    }
    if (mode == CAL_PAGE_MODE_ECU) {
        return (uint8_t)atomic_load_explicit(&CalSegPtr(calseg_index)->h.ecu_access, memory_order_relaxed);
    }
    if (mode == CAL_PAGE_MODE_XCP) {
        return CalSegPtr(calseg_index)->h.xcp_access;
    }
    DBG_PRINT_ERROR("invalid get cal page mode\n");
    return XCP_CALPAGE_INVALID_PAGE; // Invalid mode
}

// Set active ecu and/or xcp calibration page
// Note: XCP/A2L segment numbers are bytes, 0 is reserved for the EPK segment, tXcpCalSegIndex is the XCP/A2L segment number - 1
// Single threaded function, called from XCP command handler
uint8_t XcpCalSegSetCalPage(tXcpCalSegNumber segment_number, uint8_t page, uint8_t mode) {
    tXcpCalSegIndex calseg_index = XcpGetCalSegIndex(segment_number);
    if (calseg_index == XCP_UNDEFINED_CALSEG) {
        DBG_PRINTF_ERROR("invalid segment number: %u\n", segment_number);
        return CRC_ACCESS_DENIED; // Invalid calseg
    }
    if (page > 1) {
        DBG_PRINTF_ERROR("invalid cal page number %u\n", page);
        return CRC_ACCESS_DENIED; // Invalid calseg
    }
    if (mode & CAL_PAGE_MODE_ALL) { // Set all calibration segments to the same page
        // Iterate cal_seg_list cal_seg_list
        uint16_t n = XcpGetCalSegCount();
        for (tXcpCalSegIndex i = 0; i < n; i++) {
            if (mode & CAL_PAGE_MODE_ECU) {
                atomic_store_explicit(&CalSegPtrMut(i)->h.ecu_access, page, memory_order_relaxed);
            }
            if (mode & CAL_PAGE_MODE_XCP) {
                CalSegPtrMut(i)->h.xcp_access = page;
            }
        }
    } else {
        if (mode & CAL_PAGE_MODE_ECU) {
            atomic_store_explicit(&CalSegPtrMut(calseg_index)->h.ecu_access, page, memory_order_relaxed);
        }
        if (mode & CAL_PAGE_MODE_XCP) {
            CalSegPtrMut(calseg_index)->h.xcp_access = page;
        }
    }
    return CRC_CMD_OK;
}

// Copy calibration page
// Note: XCP/A2L segment numbers are bytes, 0 is reserved for the EPK segment, tXcpCalSegIndex is the XCP/A2L segment number - 1
// Single threaded function, called from XCP command handler
#ifdef XCP_ENABLE_COPY_CAL_PAGE
uint8_t XcpCalSegCopyCalPage(tXcpCalSegNumber src_seg_num, uint8_t src_page, tXcpCalSegNumber dst_seg_num, uint8_t dst_page) {

    // Only copy from default page to working page supported
    tXcpCalSegIndex dst_seg_index = XcpGetCalSegIndex(dst_seg_num);
    if (src_seg_num != dst_seg_num || dst_seg_index == XCP_UNDEFINED_CALSEG || dst_page != XCP_CALPAGE_WORKING_PAGE || src_page != XCP_CALPAGE_DEFAULT_PAGE) {
        DBG_PRINT_ERROR("unsupported or invalid calseg copy operation\n");
        return CRC_WRITE_PROTECTED;
    }

#ifdef XCP_ENABLE_COPY_CAL_PAGE_WORKAROUND

    // Older CANapes < 24SP1  do not send individual segment copy operations for each segment
    // Copy all existing segments from default page to working page, ignoring srcSeg/dstSeg
    // Iterate cal_seg_list cal_seg_list
    uint16_t n = XcpGetCalSegCount();
    for (tXcpCalSegIndex i = 0; i < n; i++) {
        const tXcpCalSeg *c = CalSegPtr(i);
        assert(c != NULL);
        uint16_t size = c->h.size;
        const uint8_t *srcPtr = CalSegDefaultPage(c);
        uint8_t res = XcpCalSegWriteMemory(XcpAddrEncodeSegIndex(i, 0), size, srcPtr);
        if (res != CRC_CMD_OK) {
            return res;
        }
    }
    return CRC_CMD_OK;

#else

    const tXcpCalSeg *c = CalSegPtr(dst_seg_index);
    assert(c != NULL);
    uint16_t size = c->h.size;
    const uint8_t *srcPtr = CalSegDefaultPage(c);
    return XcpCalSegWriteMemory(XcpAddrEncodeSegNumber(dst_seg_index, 0), size, srcPtr);

#endif
}
#endif // XCP_ENABLE_COPY_CAL_PAGE
#endif // XCP_ENABLE_CAL_PAGE

// Handle atomic calibration segment updates
// Single threaded function, called from XCP command handler
void XcpCalSegBeginAtomicTransaction(void) {
    shared_mut.cal_seg_list.write_delayed = true; // Set a flag to delay ECU page updates
    // Iterate cal_seg_list cal_seg_list
    uint16_t n = XcpGetCalSegCount();
    for (uint16_t i = 0; i < n; i++) {
        CalSegPtrMut(i)->h.write_pending = false;
    }
    DBG_PRINT4("Begin atomic calibration operation\n");
}
uint8_t XcpCalSegEndAtomicTransaction(void) {
    shared_mut.cal_seg_list.write_delayed = false; // Reset the write delay flag
    DBG_PRINT4("End atomic calibration operation\n");
    return XcpCalSegPublishAll(true); // Flush all pending writes, return true if successful
}

// Freeze calibration segment working pages
// Note: XCP/A2L segment numbers (tXcpCalSegNumber) are bytes, 0 is reserved for the EPK segment, tXcpCalSegIndex is the XCP/A2L segment number - 1
// Single threaded function, called from XCP command handler
#ifdef XCP_ENABLE_FREEZE_CAL_PAGE

uint8_t XcpGetCalSegMode(tXcpCalSegNumber segment_number) {
    tXcpCalSegIndex calseg_index = XcpGetCalSegIndex(segment_number);
    if (calseg_index == XCP_UNDEFINED_CALSEG)
        return 0;                           // Segment number out of range, ignore
    return CalSegPtr(calseg_index)->h.mode; // Return the segment mode
}

uint8_t XcpSetCalSegMode(tXcpCalSegNumber segment_number, uint8_t mode) {
    tXcpCalSegIndex calseg_index = XcpGetCalSegIndex(segment_number);
    if (calseg_index == XCP_UNDEFINED_CALSEG)
        return CRC_OUT_OF_RANGE;             // Segment number out of range
    if ((mode & PAG_PROPERTY_FREEZE) != 0) { // Set freeze enabled bit
        CalSegPtrMut(calseg_index)->h.mode |= PAG_PROPERTY_FREEZE;
    } else {
        CalSegPtrMut(calseg_index)->h.mode &= ~PAG_PROPERTY_FREEZE;
    }
    return CRC_CMD_OK;
}

// Freeze all segments or segments with freeze mode enabled
uint8_t XcpFreezeSelectedCalSegs(bool all) {

    // Iterate cal_seg_list cal_seg_list
    uint16_t n = XcpGetCalSegCount();
    for (uint16_t i = 0; i < n; i++) {
        const tXcpCalSeg *c = CalSegPtr(i);
        assert(c != NULL);
        if ((c->h.mode & PAG_PROPERTY_FREEZE) != 0 || all) {
            DBG_PRINTF3("Freeze cal seg '%s' (size=%u, pos=%u)\n", c->h.name, c->h.size, c->h.file_pos);
            if (!XcpBinFreezeCalSeg(i)) {
                return CRC_ACCESS_DENIED; // Access denied, freeze failed
            }
        }
    }
    return CRC_CMD_OK;
}

// Freeze all
bool XcpFreeze(void) { return CRC_CMD_OK == XcpFreezeSelectedCalSegs(true); }

// Set all segments to the default page
void XcpResetAllCalSegs(void) {

    // Iterate cal_seg_list cal_seg_list
    uint16_t n = XcpGetCalSegCount();
    for (uint16_t i = 0; i < n; i++) {
        tXcpCalSeg *c = CalSegPtrMut(i);
        atomic_store_explicit(&c->h.ecu_access, XCP_CALPAGE_DEFAULT_PAGE, memory_order_relaxed); // Default page for ECU access is the working page
    }
    XcpDisconnect(); // Reset the session status
}

#endif // XCP_ENABLE_FREEZE_CAL_PAGE

#endif // XCP_ENABLE_CALSEG_LIST
